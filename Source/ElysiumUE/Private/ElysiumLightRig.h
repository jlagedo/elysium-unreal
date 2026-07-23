#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ElysiumLightRig.generated.h"

class ULightComponent;

// Real-time light rig: one Unreal light per VtMB WORLDLIGHTS source, read from a
// `<map>.lights` sidecar (UE_bsp_to_scene, already Unreal cm / Z-up / left-handed).
// A component on the map actor, so it unloads with the map.
//
//   type 1 point      -> UPointLightComponent
//   type 2 spot       -> USpotLightComponent   (cone from stopdot2)
//   type 3 skylight   -> UDirectionalLightComponent (the sun; its own lux scale)
//   type 5 skyambient -> SkyAmbient colour (no light; the map actor tints its SkyLight)
//   type 0 emit_surface (texlight) -> shadowless point light (clustering deferred)
//
// VtMB point/spot intensities are pure inverse-square radiosity magnitudes with a
// `radius` cutoff — which is exactly Unreal's physical light model, so radius maps
// straight onto AttenuationRadius and intensity onto candelas (no Godot-style falloff
// exponent hack). The sun's near-unit intensity gets its own lux scale.
UCLASS()
class UElysiumLightRig : public USceneComponent
{
	GENERATED_BODY()

public:
	UElysiumLightRig();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// Parse the sidecar and spawn the lights. Returns the number of lights created.
	int32 Build(const FString& LightsPath);

	// Show/hide every spawned light (bound to elysium.lights / the pawn's L key).
	void SetLightsVisible(bool bShow);
	bool AreLightsVisible() const { return bLightsVisible; }

	// One spawned light plus the raw source data needed to re-derive its intensity/reach live
	// (so the Lights Cog window's tuning sliders apply without a map reload). Populated by Build.
	struct FLightSource
	{
		TWeakObjectPtr<ULightComponent> Light;
		int32 Type = 1;             // 0 texlight, 1 point, 2 spot, 3 sun/directional
		float Mag = 0.f;            // raw linear intensity magnitude (max of rgb)
		float RadiusCm = 0.f;       // authored cutoff radius (0 -> FallbackRadiusCm)
		float FitMult = 1.f;        // per-area .lightfit rebalance multiplier
		int32 Style = 0;            // animated lightstyle index (0 = unanimated)
		float BaseIntensity = 0.f;  // current pre-style intensity (styled lights scale this per frame)
	};

	// The spawned light sources, for the Lights window's source list. Skyambient (type 5) is
	// not a light and is absent here.
	const TArray<FLightSource>& Sources() const { return LightSources; }

	// Re-derive every light's intensity, reach, falloff exponent, and specular from the current
	// tuning fields (PointSpotScale, MaxBrightness, RadiusScale, FalloffExponent, SunScaleLux,
	// SpecularScale). Lets the Lights window tune the live rig without re-travelling the map.
	void ApplyLiveTuning();

	// Filled by Build, read by the debug HUD / the map actor.
	int32 LightCount = 0;
	bool bHasSun = false;
	bool bHasSkyAmbient = false;
	FLinearColor SkyAmbient = FLinearColor(0.12f, 0.13f, 0.18f);   // fallback cool-night tint

	// Calibration — tunable per-instance in the editor, or via elysium.LightScale (which
	// overrides PointSpotScale). Point/spot use Unreal's *non*-inverse-square falloff with a
	// gentle exponent, matching VtMB's soft baked look (the Godot rig's proven constants):
	// inverse-square + candela read too hard — hot speculars, over-bright at the source, then
	// a cliff to black. Specular is killed (VtMB world is pure Lambert). Intensity (unitless)
	// = clamp(max(rgb) * PointSpotScale, 0, MaxBrightness); reach = radius * RadiusScale.
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float PointSpotScale = 0.003f;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float MaxBrightness = 8.0f;
	// Fitting the baked lightmaps (probe_light_calibration.py) shows brightness barely
	// varies with distance-to-light (Spearman ~0, falloff slope ~0): VtMB light is ~flat
	// within its authored radius, so a gentle exponent + authored reach, not inverse-square.
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float FalloffExponent = 1.0f;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float RadiusScale = 1.0f;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float SpecularScale = 0.0f;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float SunScaleLux = 8.0f;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float FallbackRadiusCm = 2500.f;
	// The same calibration shows the moody contrast is driven by *shadowing*, not falloff —
	// so points shadow too (MegaLights keeps hundreds of shadowed lights ~constant cost).
	// Drop these to false only if a map is shadow-cost-bound.
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") bool bPointShadows = true;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") bool bSpotShadows = true;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") bool bSunShadows = true;

private:
	UPROPERTY() TArray<TObjectPtr<ULightComponent>> Lights;

	// Every spawned light plus its raw source data. Styled sources (Style 1-11) are scaled per
	// frame by the lightstyle curve so fluorescents/candles flicker; all sources can be re-tuned
	// live from this data (ApplyLiveTuning).
	TArray<FLightSource> LightSources;
	float StyleTime = 0.f;
	bool bLightsVisible = true;
};
