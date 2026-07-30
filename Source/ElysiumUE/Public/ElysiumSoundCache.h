#pragma once

#include "CoreMinimal.h"

class USoundWaveProcedural;

// Which single-header decoder produced an FDecoded. Chosen from the file extension: WAV goes
// through dr_wav (MS-ADPCM/IMA/PCM — 6.1), MP3 through dr_mp3 (dialogue/music/radio — 6.2).
enum class EElysiumAudioCodec : uint8
{
	Wav,
	Mp3,
};

// Metadata for one decoded clip (WAV or MP3), surfaced to the audio debug window / verbs. Filled
// by the decoder whether it succeeded or not (Error empty == success).
struct FElysiumSoundInfo
{
	FString Path;                    // Dir/Rel the bytes were read from
	EElysiumAudioCodec Codec = EElysiumAudioCodec::Wav;   // decoder path taken (by file extension)
	uint16  RawFormatTag = 0;        // WAV only: fmt.formatTag as stored (0x02 MS-ADPCM, 0x11 IMA, 0x01 PCM, 0xFFFE ext)
	uint16  FormatTag = 0;           // WAV only: dr_wav translatedFormatTag (resolves WAVE_FORMAT_EXTENSIBLE)
	int32   Channels = 0;
	int32   SampleRate = 0;
	int32   BitsPerSample = 0;       // source bit depth for WAV; 16 for MP3 (decoded output width)
	int64   FrameCount = 0;          // PCM frames (per-channel samples)
	float   DurationSeconds = 0.f;
	double  DecodeMilliseconds = 0.0;
	FString Error;                   // empty on success; set = decode failed

	// Human label for the encoding ("MP3" / "MS-ADPCM" / "IMA-ADPCM" / "PCM" / hex tag).
	FString FormatName() const;
};

// Process-wide, thread-safe, byte-budgeted path -> decoded-PCM LRU. Unlike
// FElysiumTextureCache (which caches a UObject), this caches the raw PCM16 bytes + metadata:
// a USoundWaveProcedural's queue is consumed on playback, so a fresh wave is minted per play
// from the cached bytes. The cache is plain memory (no UObject), so there is nothing for the
// GC to trace. Configured UI/foley entries are pinned; other entries are evictable.
struct FElysiumSoundCache
{
	// One decoded clip: interleaved int16 PCM + its metadata (WAV or MP3 -- MakeWave is agnostic).
	struct FDecoded
	{
		TArray<uint8> Pcm16;         // interleaved little-endian int16, Channels-interleaved
		FElysiumSoundInfo Info;
	};
	using FDecodedPtr = TSharedPtr<const FDecoded, ESPMode::ThreadSafe>;

	// Decode (or fetch cached) the clip at Dir/Rel, picking the codec from the file extension
	// (.mp3 -> dr_mp3, everything else -> dr_wav). Returns nullptr only when the file is
	// missing/unreadable; a decode *error* is a cached entry whose Info.Error is set, so a
	// bad file is decoded once and then reported cheaply (same policy as the texture cache).
	static FDecodedPtr LoadSoundDecoded(const FString& Dir, const FString& Rel);

	// Build a fresh procedural wave backed by an Audio Mixer sound generator. Call once per
	// playback. Returns nullptr if the decode carried no samples.
	//
	// bLoop wraps the generator's sample cursor. A one-shot instead reports generator EOF after
	// its final sample, allowing UAudioComponent::OnAudioFinished to drive request completion.
	static USoundWaveProcedural* MakeWave(FDecodedPtr Decoded, bool bLoop = false);

	// Drop every cached decode (map unload / manual flush).
	static void FlushAll();
};
