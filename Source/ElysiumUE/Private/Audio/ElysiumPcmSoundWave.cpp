#include "Audio/ElysiumPcmSoundWave.h"

#include "Sound/SoundGenerator.h"

namespace
{
	class FElysiumPcmGenerator final : public ISoundGenerator
	{
	public:
		FElysiumPcmGenerator(FElysiumSoundCache::FDecodedPtr InDecoded, bool bInLooping,
			float StartTimeSeconds, int32 InSamplesPerCallback, FElysiumVoiceRenderProbePtr InProbe)
			: Decoded(MoveTemp(InDecoded))
			, Probe(MoveTemp(InProbe))
			, SamplesPerCallback(FMath::Max(InSamplesPerCallback, 1))
			, bLooping(bInLooping)
		{
			if (!Decoded || Decoded->Info.Channels <= 0 || Decoded->Info.SampleRate <= 0)
			{
				bFinished = true;
				return;
			}
			NumChannels = Decoded->Info.Channels;
			const int64 TotalSamples = Decoded->Pcm16.Num() / static_cast<int64>(sizeof(int16));
			const int64 StartFrame = FMath::Max<int64>(
				FMath::FloorToInt64(FMath::Max(StartTimeSeconds, 0.f) * Decoded->Info.SampleRate), 0);
			Cursor = StartFrame * NumChannels;
			if (bLooping && TotalSamples > 0)
			{
				Cursor %= TotalSamples;
			}
			else if (Cursor >= TotalSamples)
			{
				bFinished = true;
			}
			if (Probe)
			{
				Probe->SampleRate.store(Decoded->Info.SampleRate, std::memory_order_relaxed);
				Probe->StartFrame.store(Cursor / FMath::Max(NumChannels, 1), std::memory_order_relaxed);
			}
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			// Stamped before the early-outs: the mixer having asked at all is what dates the
			// voice's first audible frame, whether or not this call produces samples.
			if (Probe)
			{
				const double Now = FPlatformTime::Seconds();
				double Expected = -1.0;
				Probe->FirstRenderSeconds.compare_exchange_strong(Expected, Now,
					std::memory_order_relaxed);
				Probe->LastRenderSeconds.store(Now, std::memory_order_relaxed);
			}
			if (bFinished || !Decoded || NumSamples <= 0)
			{
				return 0;
			}

			const int16* Pcm = reinterpret_cast<const int16*>(Decoded->Pcm16.GetData());
			const int64 TotalSamples = Decoded->Pcm16.Num() / static_cast<int64>(sizeof(int16));
			if (TotalSamples <= 0)
			{
				bFinished = true;
				return 0;
			}

			int32 Written = 0;
			while (Written < NumSamples)
			{
				if (Cursor >= TotalSamples)
				{
					if (!bLooping)
					{
						bFinished = true;
						break;
					}
					Cursor = 0;
				}
				OutAudio[Written++] = static_cast<float>(Pcm[Cursor++]) / 32768.0f;
			}
			if (!bLooping && Cursor >= TotalSamples)
			{
				bFinished = true;
			}
			if (Probe && NumChannels > 0)
			{
				Probe->FramesRendered.fetch_add(Written / NumChannels, std::memory_order_relaxed);
			}
			return Written;
		}

		virtual int32 GetDesiredNumSamplesToRenderPerCallback() const override
		{
			return SamplesPerCallback;
		}

		virtual int32 GetNumChannels() const override
		{
			return NumChannels;
		}

		virtual bool IsFinished() const override
		{
			return bFinished;
		}

	private:
		FElysiumSoundCache::FDecodedPtr Decoded;
		FElysiumVoiceRenderProbePtr Probe;
		int64 Cursor = 0;
		int32 NumChannels = 0;
		int32 SamplesPerCallback = 1024;
		bool bLooping = false;
		bool bFinished = false;
	};
}

void UElysiumPcmSoundWave::Initialize(
	FElysiumSoundCache::FDecodedPtr InDecoded, bool bInLooping)
{
	Decoded = MoveTemp(InDecoded);
	bLoopDecodedPcm = bInLooping;
	Probe = MakeShared<FElysiumVoiceRenderProbe, ESPMode::ThreadSafe>();
	if (!Decoded)
	{
		return;
	}
	Probe->SampleRate.store(Decoded->Info.SampleRate, std::memory_order_relaxed);
	SetSampleRate(static_cast<uint32>(Decoded->Info.SampleRate));
	NumChannels = Decoded->Info.Channels;
	Duration = bInLooping ? 10000.0f : Decoded->Info.DurationSeconds;
	SoundGroup = SOUNDGROUP_Default;
	SampleByteSize = static_cast<int32>(sizeof(int16));
	// Procedural sources always use LOOP_Never inside Audio Mixer. The generator itself owns wrap
	// versus EOF and reports IsFinished for a one-shot.
	bLooping = false;
	// Spatial procedural audio must keep its media clock advancing while inaudible. Otherwise
	// Audio Mixer can stop a component without reaching generator EOF or firing OnAudioFinished;
	// one-shots then leak and loops lose their authored phase when the listener returns.
	VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
}

ISoundGeneratorPtr UElysiumPcmSoundWave::CreateSoundGenerator(
	const FSoundGeneratorInitParams& InParams)
{
	const int32 CallbackSamples =
		FMath::Max(InParams.NumFramesPerCallback, 1) * FMath::Max(NumChannels, 1);
	return MakeShared<FElysiumPcmGenerator, ESPMode::ThreadSafe>(
		Decoded, bLoopDecodedPcm, InParams.StartTime, CallbackSamples, Probe);
}
