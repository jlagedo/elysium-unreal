#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "ElysiumLightingSettings.generated.h"

/**
 * The light rig's global calibration knobs, Project Settings -> Elysium -> Lighting, tracked at
 * `Config/DefaultElysium.ini`. These
 * were `UElysiumLightRig`'s own hardcoded field defaults (`ElysiumLightRig.h` ~157-188) plus three
 * boot-time console variables (`elysium.LightScale`, `elysium.LightFit`, `elysium.LightCurve`); both
 * are retired in favour of this one object, so a live rig, a fresh map load and the Project Settings
 * page all read the same values through the same path.
 *
 * Every value shipped here is today's faithful default, unchanged -- this task moves *where* the
 * numbers live, not what they are.
 *
 * Unlike `UElysiumSurfaceSettings` (which follows an interactive slider drag live, because it only
 * has to touch a parameter collection), this settings object pushes on the terminal `ValueSet` event
 * only: a light rig is a per-world scene component with real per-light state (shadows, MegaLights,
 * source shape), and re-deriving 400+ lights on every tick of a drag is not what a 60 Hz slider
 * needs to pay for. `PostEditChangeProperty` below mirrors `UElysiumSurfaceSettings`'s bracket but
 * drops the interactive branch entirely: an in-progress drag pushes nothing, and only the drag's
 * final commit calls `PushToWorlds`.
 */
UCLASS(Config = Elysium, DefaultConfig, meta = (DisplayName = "Lighting"))
class ELYSIUMUE_API UElysiumLightingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UElysiumLightingSettings();

	// --- point/spot ---------------------------------------------------------------------------
	// Intensity (unitless) = clamp(max(rgb) * PointSpotScale, 0, ceiling); reach = radius * RadiusScale.
	// Non-inverse-square with a gentle exponent: the baked lightmap calibration
	// (`docs/vtmb/lighting.md`) shows VtMB's authored reach is nearly flat before its radius cutoff.
	UPROPERTY(EditAnywhere, Config, Category = "Point/Spot", meta = (ClampMin = "0.0"))
	float PointSpotScale = 0.003f;

	UPROPERTY(EditAnywhere, Config, Category = "Point/Spot", meta = (ClampMin = "0.0"))
	float MaxBrightness = 8.0f;

	// A/B against MaxBrightness (was `elysium.LightCurve 1`): the same PointSpotScale against a
	// ceiling high enough that no source reaches it, so the clip becomes the only variable.
	UPROPERTY(EditAnywhere, Config, Category = "Point/Spot", meta = (ClampMin = "0.0"))
	float ExtendedMaxBrightness = 512.f;

	/** Clip against `ExtendedMaxBrightness` instead of `MaxBrightness` (was `elysium.LightCurve`). */
	UPROPERTY(EditAnywhere, Config, Category = "Point/Spot")
	bool bUseExtendedBrightnessCeiling = false;

	UPROPERTY(EditAnywhere, Config, Category = "Point/Spot", meta = (ClampMin = "0.1"))
	float FalloffExponent = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Point/Spot", meta = (ClampMin = "0.01"))
	float RadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Point/Spot", meta = (ClampMin = "1.0"))
	float FallbackRadiusCm = 2500.f;

	// No `SpecularScale` here (R5.5): the light rig's specular scale is
	// `UElysiumSurfaceSettings::LightSpecularScale`, one global knob on the surfaces page, and a
	// second field for the same number would be a second writer.

	// --- per-area rebalance --------------------------------------------------------------------
	/**
	 * Apply `<map>.lightfit`'s per-area brightness rebalance (was `elysium.LightFit`), one multiplier
	 * per `.lights` line, reverse-engineered by `probe_light_attribution.py`.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Per-area rebalance")
	bool bApplyLightFit = false;

	// --- Lumen / fog ---------------------------------------------------------------------------
	/** Per-light Lumen surface-cache injection multiplier (not the post-process precomputed-lighting control). */
	UPROPERTY(EditAnywhere, Config, Category = "Lumen", meta = (ClampMin = "0.0"))
	float IndirectLightingScale = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Lumen", meta = (ClampMin = "0.0"))
	float VolumetricScatteringScale = 1.0f;

	// --- sun -------------------------------------------------------------------------------------
	UPROPERTY(EditAnywhere, Config, Category = "Sun", meta = (ClampMin = "0.0"))
	float SunScaleLux = 8.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Sun", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float SunSourceAngleDegrees = 0.5357f;

	UPROPERTY(EditAnywhere, Config, Category = "Sun", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float SunSoftSourceAngleDegrees = 0.0f;

	// --- 3D-skybox miniature -------------------------------------------------------------------
	/** Floors a sky source's scaled reach so an authored radius near zero still lights something. */
	UPROPERTY(EditAnywhere, Config, Category = "Sky miniature", meta = (ClampMin = "0.0"))
	float MinSkyReachCm = 5000.f;

	// --- shadows -------------------------------------------------------------------------------
	// The baked-lightmap calibration shows the moody contrast is driven by shadowing, not falloff,
	// so points shadow too (MegaLights keeps hundreds of shadowed lights ~constant cost). Drop to
	// false only if a map is shadow-cost-bound.
	UPROPERTY(EditAnywhere, Config, Category = "Shadows")
	bool bPointShadows = true;

	UPROPERTY(EditAnywhere, Config, Category = "Shadows")
	bool bSpotShadows = true;

	UPROPERTY(EditAnywhere, Config, Category = "Shadows")
	bool bSunShadows = true;

	/**
	 * Copy every calibration field into `Rig` (a live rig's own mirrors of these values) and re-derive
	 * its non-overridden sources (`UElysiumLightRig::ApplyLiveTuning`). Called for every light rig in
	 * every live world context; a rig with no world context (not yet adopted) is untouched.
	 *
	 * `BlueprintCallable` for parity with `UElysiumSurfaceSettings::PushToCollection`, even though a
	 * settings edit is the only caller today.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Lighting")
	void PushToWorlds() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
