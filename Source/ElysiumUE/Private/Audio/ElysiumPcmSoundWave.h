#pragma once

#include "CoreMinimal.h"
#include "ElysiumAudioLatency.h"
#include "ElysiumSoundCache.h"
#include "Sound/SoundWaveProcedural.h"
#include "ElysiumPcmSoundWave.generated.h"

// A finite PCM-backed procedural wave. USoundWaveProcedural's legacy QueueAudio path emits
// underrun silence forever and therefore never completes a one-shot. Supplying an ISoundGenerator
// lets the Audio Mixer observe the exact end of the decoded sample buffer and publish the
// component's ordinary OnAudioFinished event. Looping uses the same generator with a wrapped
// sample cursor, so both policies share one render path.
UCLASS()
class UElysiumPcmSoundWave final : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	void Initialize(FElysiumSoundCache::FDecodedPtr InDecoded, bool bInLooping);

	virtual ISoundGeneratorPtr CreateSoundGenerator(
		const FSoundGeneratorInitParams& InParams) override;

	// The generator's own view of the mixer's render head, shared with whoever submitted
	// the voice. It is created here rather than by the caller because the generator is the only
	// thing that knows when the mixer actually pulled, and it outlives this wave (Audio Mixer holds
	// the generator until the source is released), so a shared block is what both ends can hold.
	const FElysiumVoiceRenderProbePtr& RenderProbe() const { return Probe; }

private:
	FElysiumSoundCache::FDecodedPtr Decoded;
	FElysiumVoiceRenderProbePtr Probe;
	bool bLoopDecodedPcm = false;
};
