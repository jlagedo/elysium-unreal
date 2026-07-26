#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ElysiumLightRig.generated.h"

class ULightComponent;

// Real-time light rig: one Unreal light per VtMB WORLDLIGHTS source. The light *actors* are baked
// into the map's level (tools/bake_map.py, one per `<map>.lights` line, tagged with its line
// index); this rig adopts them and owns their behaviour — it re-derives every intensity and reach
// from the raw sidecar row at load, animates the lightstyles, and re-applies the whole calibration
// on demand so the Lights Cog window can tune the map live. The sidecar (UE_bsp_to_scene) is
// already Unreal cm / Z-up / left-handed. A component on the map actor, so it unloads with the map.
//
//   type 1 point      -> UPointLightComponent
//   type 2 spot       -> USpotLightComponent   (cone from stopdot2)
//   type 3 skylight   -> UDirectionalLightComponent (the sun; its own lux scale)
//   type 5 skyambient -> SkyAmbient colour + SkyAmbientMag (no light; the map actor drives
//                        its SkyLight's tint AND level from them)
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

	// One light actor the baked level offered up, with the `<map>.lights` line it was baked from.
	struct FAdoptedLight
	{
		ULightComponent* Light = nullptr;
		int32 SourceIndex = INDEX_NONE;
	};

	// Bind the baked level's light components to their sidecar rows and take ownership of their
	// values: every intensity, reach and falloff is re-derived here from the raw source data, so
	// the live calibration — not whatever the bake happened to write — is what the map renders.
	// Returns the number of lights bound.
	//
	// `SkyReach` is the 3D-skybox miniature's uniform scale (`<map>.sky`, 16 where there is one,
	// 1 otherwise). A source flagged sky in the sidecar lit the *miniature*, never the playable
	// world — VtMB's light cache read exactly those lump-15 rows to light the skybox props — so
	// its reach is authored in miniature units and has to scale with the geometry it lights.
	// Its position is already scaled by the bake; only the reach is re-derived here.
	int32 Adopt(const TArray<FAdoptedLight>& Adopted, const FString& LightsPath, float SkyReach = 1.f);

	// Show/hide every spawned light (bound to elysium.lights / the pawn's L key).
	void SetLightsVisible(bool bShow);
	bool AreLightsVisible() const { return bLightsVisible; }

	// One spawned light plus the raw source data needed to re-derive its intensity/reach live
	// (so the Lights Cog window's tuning sliders apply without a map reload). Populated by Build.
	struct FLightSource
	{
		TWeakObjectPtr<ULightComponent> Light;
		// The `<map>.lights` line this came from — the stable identity of a source. Not the same as
		// this entry's position in the array: sources are appended in the order the baked level
		// offered its actors up, and the skyambient row and any unmatched row are skipped, so the
		// two only look alike. Anything joining back to the sidecar keys on this.
		int32 SourceIndex = INDEX_NONE;
		int32 Type = 1;             // 0 texlight, 1 point, 2 spot, 3 sun/directional
		float Mag = 0.f;            // raw linear intensity magnitude (max of rgb)
		float RadiusCm = 0.f;       // authored cutoff radius (0 -> FallbackRadiusCm)
		float FitMult = 1.f;        // per-area .lightfit rebalance multiplier
		int32 Style = 0;            // animated lightstyle index (0 = unanimated)
		float BaseIntensity = 0.f;  // current pre-style intensity (styled lights scale this per frame)
		FLinearColor Color = FLinearColor::White;   // the sidecar's normalized hue, for revert
		bool bOverridden = false;   // hand-set in the Lights window; the calibration passes skip it
		bool bDisabled = false;     // switched off by hand in the Lights window
		bool bReviewed = false;     // survey verdict recorded (kept or killed) in the Lights window
		bool bSky = false;          // lights the 3D-skybox miniature, not the playable world
	};

	// The spawned light sources, for the Lights window's source list. Skyambient (type 5) is
	// not a light and is absent here.
	const TArray<FLightSource>& Sources() const { return LightSources; }

	// --- per-source live override ----------------------------------------------------------------
	// The Lights window's per-light inspector edits one source by hand. Such a source is marked
	// overridden, which takes it out of both passes that would otherwise write over the edit: the
	// global calibration (ApplyLiveTuning, which every calibration slider triggers) and the
	// per-frame lightstyle animation. The rest of the rig keeps following the sliders as usual.
	// Nothing here persists — a map reload re-derives every source from the sidecar.
	ULightComponent* SourceLight(int32 Index) const;
	bool IsSourceOverridden(int32 Index) const;
	void SetSourceOverridden(int32 Index, bool bOverride);
	// Set one source's intensity by hand, marking it overridden. Goes through the rig rather than
	// straight to the component so the list's readout and the styled base stay in step with it.
	void SetSourceIntensity(int32 Index, float Intensity);
	// Re-derive one source from its sidecar row + the current calibration, and drop its override.
	// The disable switch below is a separate axis and survives a revert.
	void RevertSource(int32 Index);
	void RevertAllSources();

	// --- per-source disable ----------------------------------------------------------------------
	// Switching one source off by hand, orthogonal to the override: it changes no value, so a
	// disabled light keeps its calibrated intensity/reach and comes back exactly as it was. This is
	// the switch the fill-light survey is driven from (the Lights window saves the disabled set),
	// so it outranks the master toggle and the window's isolate both — nothing turns a
	// hand-disabled light back on but enabling it.
	bool IsSourceDisabled(int32 Index) const;
	void SetSourceDisabled(int32 Index, bool bDisable);
	void EnableAllSources();
	// Should this source be lit right now, per the master toggle and its own disable? The Lights
	// window's isolate pass restores visibility through this rather than to a plain "on".
	bool ShouldSourceBeLit(int32 Index) const;

	// --- per-source reviewed mark ----------------------------------------------------------------
	// The survey's "judged" bit, distinct from the disable: a save records disabled lights only, so
	// without it a kept light and a never-visited one are indistinguishable. Disabling a source
	// marks it reviewed by itself (a kill is a verdict); re-enabling does not clear the mark (that
	// is a deliberate keep). Pure bookkeeping — no visual effect, nothing reads it but the save.
	bool IsSourceReviewed(int32 Index) const;
	void SetSourceReviewed(int32 Index, bool bReviewed);

	// Re-apply the map's saved survey (`_lights/<map>.json`, written by the Lights window) to the
	// running rig: the disabled set and the reviewed marks, joined on the `.lights` line index.
	// Additive — it sets marks, never clears them — and attribute overrides are not restored.
	// Adopt runs this automatically when a save exists (elysium.LightSurvey 0 turns that off), so
	// the survey is the map's standing hand-authored light state; the Lights window's Load button
	// is the same call mid-session. Returns false and fills OutMessage on failure.
	bool LoadSurvey(FString& OutMessage);

	// Re-derive every light's intensity, reach, falloff exponent, and specular from the current
	// tuning fields (PointSpotScale, MaxBrightness, RadiusScale, FalloffExponent, SunScaleLux,
	// SpecularScale). Lets the Lights window tune the live rig without re-travelling the map.
	void ApplyLiveTuning();

	// Filled by Build, read by the debug HUD / the map actor.
	int32 LightCount = 0;
	bool bHasSun = false;
	bool bHasSkyAmbient = false;
	FLinearColor SkyAmbient = FLinearColor(0.12f, 0.13f, 0.18f);   // fallback cool-night tint
	// The type-5 row's own magnitude, kept rather than normalised away (C1). VRAD divides no
	// falloff out of a `light_environment`, so this IS a lump-8 luxel value / 255 — the radiance
	// VtMB's light cache returns for a sky-hitting bounce ray (RE-A3/RE-A5). It spans 0.0050 to
	// 0.0980 over the game's 25 pair maps (1.28 to 25.00 in stored-luxel units) and is
	// **authored to exactly zero** on two of them, so zero is a value to honour, not a missing
	// reading to default. 0 when the map has no pair at all.
	float SkyAmbientMag = 0.f;

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
	// B7 — the 3D-skybox miniature's uniform scale, applied to a sky source's reach only (its
	// position is baked already scaled). Set from `<map>.sky` at Adopt; 1 on a map with no
	// miniature. MinSkyReachCm floors it, so an authored radius near zero still lights something
	// after the scale rather than collapsing.
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float SkyReachScale = 1.f;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float MinSkyReachCm = 5000.f;
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

	// The map this rig adopted (the `.lights` base name), which keys the survey save file.
	FString SurveyMapName;

	// Derive one source's intensity, reach, falloff and specular from the tuning constants. The
	// single place that math lives; ApplyLiveTuning is this over every non-overridden source.
	void ApplyToSource(FLightSource& S);
};
