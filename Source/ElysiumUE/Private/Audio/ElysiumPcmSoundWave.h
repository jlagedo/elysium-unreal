#pragma once

#include "CoreMinimal.h"
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

private:
	FElysiumSoundCache::FDecodedPtr Decoded;
	bool bLoopDecodedPcm = false;
};
