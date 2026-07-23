#include "ElysiumSoundCache.h"

#include "Sound/SoundWaveProcedural.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Declarations only -- the implementations are emitted once each in ElysiumDrWav.cpp / ElysiumDrMp3.cpp.
THIRD_PARTY_INCLUDES_START
#include "dr_wav.h"
#include "dr_mp3.h"
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogElysiumAudio, Log, All);

// The audio engine treats a wave whose Duration is >= this sentinel as endlessly looping. Mirrors
// AudioMixerCore's INDEFINITELY_LOOPING_DURATION (10000.0f) without pulling in that module's header.
static constexpr float ElysiumIndefiniteLoopDuration = 10000.0f;

namespace
{
	// Process-lifetime path -> decoded PCM cache. A miss (Info.Error set) is cached too, so a
	// broken/undecodable file is only decoded once. M0/M1 load one map at a time, so unbounded
	// caching is fine; FlushAll drops it on unload.
	TMap<FString, TUniquePtr<FElysiumSoundCache::FDecoded>> GSoundCache;

	// Decode a WAV (MS-ADPCM / IMA / PCM) to interleaved int16 with dr_wav. FileData must outlive
	// the call -- dr_wav decodes from memory without copying the input. Fills Decoded->Info/Pcm16;
	// sets Info.Error on failure.
	void DecodeWav(const TArray<uint8>& FileData, FElysiumSoundCache::FDecoded& Decoded)
	{
		FElysiumSoundInfo& Info = Decoded.Info;
		Info.Codec = EElysiumAudioCodec::Wav;

		drwav Wav;
		if (!drwav_init_memory(&Wav, FileData.GetData(), FileData.Num(), nullptr))
		{
			Info.Error = TEXT("drwav_init_memory failed (not RIFF/WAVE or unsupported format)");
			return;
		}

		Info.RawFormatTag = Wav.fmt.formatTag;
		Info.FormatTag = Wav.translatedFormatTag;
		Info.Channels = int32(Wav.channels);
		Info.SampleRate = int32(Wav.sampleRate);
		Info.BitsPerSample = int32(Wav.bitsPerSample);
		Info.FrameCount = int64(Wav.totalPCMFrameCount);

		if (Info.FrameCount <= 0 || Info.Channels <= 0)
		{
			Info.Error = TEXT("WAV carries no samples");
			drwav_uninit(&Wav);
			return;
		}

		// Decode every frame to interleaved int16 -- one path covers MS-ADPCM, IMA and PCM.
		const int64 NumSamples = Info.FrameCount * Info.Channels;
		Decoded.Pcm16.SetNumUninitialized(NumSamples * int64(sizeof(int16)));
		const drwav_uint64 FramesRead = drwav_read_pcm_frames_s16(
			&Wav, Wav.totalPCMFrameCount, reinterpret_cast<drwav_int16*>(Decoded.Pcm16.GetData()));
		drwav_uninit(&Wav);

		if (FramesRead != drwav_uint64(Info.FrameCount))
		{
			// Short read: keep what decoded (still playable) and shrink the buffer to match.
			Decoded.Pcm16.SetNum(int64(FramesRead) * Info.Channels * int64(sizeof(int16)), EAllowShrinking::No);
			Info.FrameCount = int64(FramesRead);
		}
	}

	// Decode an MP3 (dialogue / music / radio) to interleaved int16 with dr_mp3. Same memory rule
	// as DecodeWav (FileData must outlive the call -- dr_mp3 keeps a pointer to the input, not a
	// copy). MP3 stores no frame count, so it's a two pass: count frames, then read from the top.
	void DecodeMp3(const TArray<uint8>& FileData, FElysiumSoundCache::FDecoded& Decoded)
	{
		FElysiumSoundInfo& Info = Decoded.Info;
		Info.Codec = EElysiumAudioCodec::Mp3;
		Info.BitsPerSample = 16;   // dr_mp3 decodes to int16; MP3 has no "source" bit depth

		drmp3 Mp3;
		if (!drmp3_init_memory(&Mp3, FileData.GetData(), FileData.Num(), nullptr))
		{
			Info.Error = TEXT("drmp3_init_memory failed (no decodable MP3 frames)");
			return;
		}

		Info.Channels = int32(Mp3.channels);
		Info.SampleRate = int32(Mp3.sampleRate);
		Info.FrameCount = int64(drmp3_get_pcm_frame_count(&Mp3));   // walks the stream; restores cursor

		if (Info.FrameCount <= 0 || Info.Channels <= 0)
		{
			Info.Error = TEXT("MP3 carries no samples");
			drmp3_uninit(&Mp3);
			return;
		}

		// Rewind explicitly -- the frame-count pass leaves the read cursor at end-of-stream.
		drmp3_seek_to_pcm_frame(&Mp3, 0);

		const int64 NumSamples = Info.FrameCount * Info.Channels;
		Decoded.Pcm16.SetNumUninitialized(NumSamples * int64(sizeof(int16)));
		const drmp3_uint64 FramesRead = drmp3_read_pcm_frames_s16(
			&Mp3, drmp3_uint64(Info.FrameCount), reinterpret_cast<drmp3_int16*>(Decoded.Pcm16.GetData()));
		drmp3_uninit(&Mp3);

		if (FramesRead != drmp3_uint64(Info.FrameCount))
		{
			// Short read: keep what decoded and shrink to match (same policy as the WAV path).
			Decoded.Pcm16.SetNum(int64(FramesRead) * Info.Channels * int64(sizeof(int16)), EAllowShrinking::No);
			Info.FrameCount = int64(FramesRead);
		}
	}
}

FString FElysiumSoundInfo::FormatName() const
{
	if (Codec == EElysiumAudioCodec::Mp3)
	{
		return TEXT("MP3");
	}
	switch (FormatTag)
	{
	case DR_WAVE_FORMAT_PCM:        return TEXT("PCM");
	case DR_WAVE_FORMAT_ADPCM:      return TEXT("MS-ADPCM");
	case DR_WAVE_FORMAT_DVI_ADPCM:  return TEXT("IMA-ADPCM");
	case DR_WAVE_FORMAT_IEEE_FLOAT: return TEXT("float");
	default:                        return FString::Printf(TEXT("tag 0x%04X"), FormatTag);
	}
}

const FElysiumSoundCache::FDecoded* FElysiumSoundCache::LoadSoundDecoded(const FString& Dir, const FString& Rel)
{
	const FString Path = FPaths::Combine(Dir, Rel);
	if (const TUniquePtr<FDecoded>* Found = GSoundCache.Find(Path))
	{
		return Found->Get();
	}

	TUniquePtr<FDecoded> Decoded = MakeUnique<FDecoded>();
	Decoded->Info.Path = Path;

	auto CacheAndReturn = [&Path, &Decoded]() -> const FDecoded*
	{
		return GSoundCache.Add(Path, MoveTemp(Decoded)).Get();
	};

	// Read the whole file. Both dr_wav and dr_mp3 decode from memory and do NOT copy the input,
	// so FileData must outlive the decode (it does -- it stays in scope through Decode*'s uninit).
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *Path, FILEREAD_Silent))
	{
		// File-not-found is the one case that returns nullptr rather than a cached error entry:
		// the caller distinguishes "no such asset" from "asset present but undecodable".
		return nullptr;
	}

	const double T0 = FPlatformTime::Seconds();

	// Codec by extension -- the on-disk tree mirrors VtMB's sound/ (WAV for SFX/ambience,
	// MP3 for dialogue/music/radio). An unknown extension is tried as WAV (dr_wav reports the error).
	if (FPaths::GetExtension(Rel).Equals(TEXT("mp3"), ESearchCase::IgnoreCase))
	{
		DecodeMp3(FileData, *Decoded);
	}
	else
	{
		DecodeWav(FileData, *Decoded);
	}

	FElysiumSoundInfo& Info = Decoded->Info;
	Info.DurationSeconds = Info.SampleRate > 0 ? float(double(Info.FrameCount) / Info.SampleRate) : 0.f;
	Info.DecodeMilliseconds = (FPlatformTime::Seconds() - T0) * 1000.0;

	if (!Info.Error.IsEmpty())
	{
		UE_LOG(LogElysiumAudio, Warning, TEXT("Sound decode failed: %s -- %s"), *Path, *Info.Error);
	}
	else
	{
		UE_LOG(LogElysiumAudio, Verbose, TEXT("Decoded %s: %s %dch %dHz %lld frames (%.2f ms)"),
			*Path, *Info.FormatName(), Info.Channels, Info.SampleRate, Info.FrameCount, Info.DecodeMilliseconds);
	}
	return CacheAndReturn();
}

USoundWaveProcedural* FElysiumSoundCache::MakeWave(const FDecoded& Decoded, bool bLoop)
{
	const FElysiumSoundInfo& Info = Decoded.Info;
	if (Decoded.Pcm16.Num() == 0 || Info.Channels <= 0 || Info.SampleRate <= 0)
	{
		return nullptr;
	}

	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>();
	// Cast to uint32 so the canonical SetSampleRate(uint32,bool) overload is selected -- it sets
	// the sample rate the audio mixer actually reads (the int32 overload only touches a proxy field).
	Wave->SetSampleRate(static_cast<uint32>(Info.SampleRate));
	Wave->NumChannels = Info.Channels;
	Wave->Duration = bLoop ? ElysiumIndefiniteLoopDuration : Info.DurationSeconds;
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->SampleByteSize = int32(sizeof(int16));   // interleaved int16 PCM
	Wave->bLooping = false;   // procedural ignores this flag; looping = re-queue on underflow (below)

	// QueueAudio copies the bytes into the wave's internal queue, so the cached PCM stays owned
	// by the cache and can back many plays. BufferSize is bytes and must be a multiple of the
	// sample byte size (2) -- always true for interleaved int16.
	Wave->QueueAudio(Decoded.Pcm16.GetData(), Decoded.Pcm16.Num());

	if (bLoop)
	{
		// The mixer drains the queue and, when it runs dry, calls this to ask for more; re-queue the
		// whole clip to loop seamlessly. &Decoded is cache-stable (GSoundCache holds it until FlushAll,
		// which runs on map unload after every voice is stopped), so the raw capture never dangles.
		const FDecoded* Src = &Decoded;
		Wave->OnSoundWaveProceduralUnderflow.BindLambda(
			[Src](USoundWaveProcedural* InWave, int32 /*SamplesRequired*/)
			{
				InWave->QueueAudio(Src->Pcm16.GetData(), Src->Pcm16.Num());
			});
	}
	return Wave;
}

void FElysiumSoundCache::FlushAll()
{
	GSoundCache.Empty();
}
