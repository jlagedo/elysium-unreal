#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "ElysiumAudioSettings.generated.h"

/**
 * `SoundScheme` playback taste knobs, Project Settings -> Elysium -> Audio, tracked at
 * `Config/DefaultElysium.ini` (R4.5, `docs/project/seam_migration.md` -> "Wire first, tune later").
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
};
