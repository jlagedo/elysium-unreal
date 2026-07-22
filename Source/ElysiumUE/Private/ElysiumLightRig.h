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

	// Styled lights (style 1-11): their base intensity and pattern index, scaled per
	// frame by the lightstyle curve so fluorescents/candles flicker.
	struct FAnimatedLight
	{
		TWeakObjectPtr<ULightComponent> Light;
		float BaseIntensity = 0.f;
		int32 Style = 0;
	};
	TArray<FAnimatedLight> Animated;
	float StyleTime = 0.f;
	bool bLightsVisible = true;
};
