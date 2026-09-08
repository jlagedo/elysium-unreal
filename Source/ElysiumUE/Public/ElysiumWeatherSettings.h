#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "ElysiumWeatherSettings.generated.h"

/**
 * The rain "enhanced" presentation preset, Project Settings -> Elysium -> Weather, tracked at
 * `Config/DefaultElysium.ini`.
 *
 * Every field here used to live only as a literal argument on the Cog Environment window's
 * "Enhanced defaults" button (`ElysiumCogWindow_Environment.cpp`, retired by this task), which
 * pushed the combination onto the live `elysium.Rain*`/`elysium.EnvironmentWetnessScale` cvars in
 * one click and had no other home. Those seven cvars are kept — each already has its own live Cog
 * slider (each already classed "drop to editor", not "delete";
 * this page is that drop for the one combination the button hardcoded) — this page is simply
 * where the preset combination itself now lives, editable rather than buried in a button.
 *
 * Nothing here is auto-applied: the button's one-click apply is gone with it, per the roadmap
 * line's "the Cog button removed." A future preset-apply surface (editor utility, console command)
 * reads these fields; today they are the recorded values, matching every cvar's own registered
 * default except `RainEnhancement` (0 baseline vs 1 here, the ON/OFF the button actually toggled).
 */
UCLASS(Config = Elysium, DefaultConfig, meta = (DisplayName = "Weather"))
class ELYSIUMUE_API UElysiumWeatherSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UElysiumWeatherSettings();

	/** `elysium.RainEnhancement` under the preset: 1 turns the enhanced branch on. */
	UPROPERTY(EditAnywhere, Config, Category = "Enhanced preset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RainEnhancement = 1.0f;

	/** `elysium.RainWetDarken` under the preset: maximum enhanced full-wet base-color darkening. */
	UPROPERTY(EditAnywhere, Config, Category = "Enhanced preset", meta = (ClampMin = "0.0", ClampMax = "0.25"))
	float RainWetDarken = 0.06f;

	/** `elysium.RainWetRoughness` under the preset: maximum enhanced full-wet roughness reduction. */
	UPROPERTY(EditAnywhere, Config, Category = "Enhanced preset", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float RainWetRoughness = 0.10f;

	/** `elysium.RainLightResponse` under the preset: translucent rain response to local lights. */
	UPROPERTY(EditAnywhere, Config, Category = "Enhanced preset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RainLightResponse = 0.25f;

	/** `elysium.RainSourceRetain` under the preset: source cubemap weight retained at full wetness. */
	UPROPERTY(EditAnywhere, Config, Category = "Enhanced preset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RainSourceRetain = 1.0f;

	/** `elysium.RainWetSpecular` under the preset: enhanced wet-surface dielectric specular level. */
	UPROPERTY(EditAnywhere, Config, Category = "Enhanced preset", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RainWetSpecular = 0.50f;

	/** `elysium.EnvironmentWetnessScale` under the preset: global multiplier over authored wetness. */
	UPROPERTY(EditAnywhere, Config, Category = "Enhanced preset", meta = (ClampMin = "0.0"))
	float EnvironmentWetnessScale = 1.0f;
};
