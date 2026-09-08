#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "ElysiumAudioSettings.generated.h"

/**
 * `SoundScheme` playback taste knobs, Project Settings -> Elysium -> Audio, tracked at
 * `Config/DefaultElysium.ini`.
 * Distinct from `UElysiumUserSettings` (`UGameUserSettings`), which is the player-facing volume-slider
 * save; these are developer-tuned playback constants read at scheme-evaluation time
 * (`ElysiumSoundScheme.cpp`), the same way `UElysiumModelSettings`'s knobs are.
 */
UCLASS(Config = Elysium, DefaultConfig, meta = (DisplayName = "Audio"))
class ELYSIUMUE_API UElysiumAudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UElysiumAudioSettings();

	/** SoundScheme music state-change crossfade time in seconds (explore<->combat<->alert). */
	UPROPERTY(EditAnywhere, Config, Category = "Music", meta = (ClampMin = "0.0"))
	float MusicCrossfade = 2.0f;

	/**
	 * Mean seconds between plays of a Frequency-10 RandomSound; a scheme's per-sound cadence scales
	 * as `SchemeRandomBase * 10 / Frequency`. VtMB's exact Frequency curve is client-side and not
	 * reverse-engineered.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Ambient", meta = (ClampMin = "0.0"))
	float SchemeRandomBase = 8.0f;

	/**
	 * The third term of the scheduling lead (`FElysiumAudioLatency::Lead()`): submit → the mixer's
	 * first pull, in seconds. The other two terms are read off the running device; this one is not
	 * reported by any interface, so it is measured once and written here.
	 *
	 * AUD1.3 (2026-09-08): it became a constant when rendering moved onto baked `USoundWave`
	 * assets. The hand-rolled decoder minted a procedural wave whose generator could stamp its own
	 * first pull; a plain wave has nowhere to hang that probe, and there is nothing left to
	 * measure per line either — a primed asset feeds the mixer from Unreal's stream cache instead
	 * of paying a whole-file decode, so the term is a property of the path rather than of the
	 * content. Shipped at 0.0: the pre-asset readings (37–62 ms per spoken line) were dominated by
	 * the decode that no longer happens, so re-using one would lead every line by a delay no line
	 * pays. The owner re-stamps it from `elysium.audio_latency` against asset playback, and the
	 * number goes in `docs/vtmb/audio_pipeline.md`.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Latency", meta = (ClampMin = "0.0"))
	float SubmitToRenderSeconds = 0.0f;
};
