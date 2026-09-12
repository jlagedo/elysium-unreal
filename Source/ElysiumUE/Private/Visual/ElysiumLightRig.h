#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ElysiumLightRig.generated.h"

class ULightComponent;
class UPrimitiveComponent;
class UElysiumLightCalibration;
class UElysiumLightingSettings;
class UElysiumSurfaceSettings;
class UElysiumMapLightQueryData;

// Real-time light rig: one Unreal light per VtMB WORLDLIGHTS source. The light *actors* are baked
// into the map's level (pipeline/unreal/bake_map.py, one per `<map>.lights` line, tagged with its line
// index); this rig adopts them and owns their behaviour — it re-derives every intensity and reach
// from the raw sidecar row at load, animates the lightstyles, and re-applies the whole calibration
// on demand. Global calibration comes from `UElysiumLightingSettings` (R4.3), per-light hand-tunes
// from the map's own `UElysiumLightCalibration` asset when one exists; the Cog Lights window is a
// read-only viewer, not an editor, since both are edited the ordinary Unreal way. The sidecar
// (UE_bsp_to_scene) is already Unreal cm / Z-up / left-handed. A component on the map actor, so it
// unloads with the map.
//
//   type 1 point      -> UPointLightComponent
//   type 2 spot       -> USpotLightComponent   (inner/outer cone from stopdot/stopdot2)
//   type 3 skylight   -> UDirectionalLightComponent (the sun; its own lux scale)
//   type 5 skyambient -> SkyAmbient colour + SkyAmbientMag (no light; the map actor drives
//                        its SkyLight's tint AND level from them)
//   type 0 emit_surface (texlight) -> shadowless point light (clustering deferred)
//
// Point/spot use a unitless, non-inverse-square brightness with a gentle exponent: the baked
// lightmap calibration shows VtMB's authored reach is nearly flat before its radius cutoff, while
// inverse-square produces a hot source and a cliff to black. The sun keeps its own lux scale.
UCLASS()
class UElysiumLightRig : public USceneComponent
{
	GENERATED_BODY()

public:
	UElysiumLightRig();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// One light actor the baked level offered up, with the `<map>.lights` line it was baked from.
	// `Type` and `Style` are the R5.6 `elysium.type=`/`elysium.style=` tags a converted map's
	// actors carry; the legacy `Adopt` ignores them (it reads both off the sidecar row).
	struct FAdoptedLight
	{
		ULightComponent* Light = nullptr;
		int32 SourceIndex = INDEX_NONE;
		int32 Type = 1;
		int32 Style = 0;
	};

	// Legacy lane. Bind the baked level's light components to their sidecar rows and take
	// ownership of their values: every intensity, reach and falloff is re-derived here from the
	// raw source data, so the live calibration — not whatever the bake happened to write — is
	// what the map renders. Returns the number of lights bound.
	//
	// `SkyReach` is the 3D-skybox miniature's uniform scale (`<map>.sky`, 16 where there is one,
	// 1 otherwise). A source flagged sky in the sidecar lit the *miniature*, never the playable
	// world — VtMB's light cache read exactly those lump-15 rows to light the skybox props — so
	// its reach is authored in miniature units and has to scale with the geometry it lights.
	// Its position is already scaled by the bake; only the reach is re-derived here.
	int32 Adopt(const TArray<FAdoptedLight>& Adopted, const FString& LightsPath, float SkyReach = 1.f);

	// V2 lane.
	// The bake already wrote every derived value -- intensity, reach, falloff, cone, shadows,
	// specular, Lumen/fog scales and the MegaLights policy -- from `worldLights[]` through the same
	// formulas `ApplyToSource` holds, so this opens no file and derives nothing: each source is
	// snapshotted from its actor (the baked values are its baseline; `RevertSource` returns to
	// them, and a settings-page push leaves them alone -- the page reaches a converted map through
	// the bake's recipe). What the rig still owns here is the R4.3 calibration asset's merge rows
	// (keyed by the same lump-15 ordinal the `elysium.src` tag carries) and the per-frame
	// lightstyle animation off the `elysium.style` tag. Returns the number of lights bound.
	int32 AdoptBaked(const TArray<FAdoptedLight>& Adopted, const FString& InMapName);

	// The lightstyle
	// pattern table, Source's `engine->LightStyle(style, pattern)`. 64 entries (MAX_LIGHTSTYLES):
	// 0-11 the engine's own animated patterns, everything else "m" (full) until an entity writes
	// it -- a named `light`'s TurnOn/TurnOff/SetPattern/FadeToPattern lands here, keyed by the
	// style VRAD gave it (>= 32), and reaches every source carrying that style on the next tick.
	// The table outlives Adopt/AdoptBaked, so the entity world may spawn before or after the rig
	// adopts. Returns false for a style outside 0..63 or an empty pattern (nothing written).
	static constexpr int32 MaxLightStyles = 64;
	bool SetStylePattern(int32 Style, const FString& Pattern);
	FString StylePattern(int32 Style) const;
	// The multiplier the tick applies right now for a style: 'a' = 0, 'm' = 1, 'z' ~ 2.08, 10 Hz
	// keyframes lerped on the rig's own clock. 1 for a style outside the table.
	float StyleMultiplier(int32 Style) const;
	// engine.dll 0x20076eb0 writes one discrete 10-Hz letter * 22; slot 118 divides by 264.
	float GameplayStyleMultiplier(int32 Style) const;
	bool IsGameplayLightAvailable() const { return bGameplayLightAvailable; }
	float QueryGameplayLight(const FVector& PointCm) const;
	// The engine PVS test `0x101d1a90`, over the same decoded cluster partition the light query
	// walks. A point that resolves to no cluster answers TRUE — see `ArePointsInSamePvs`.
	bool ArePointsInSamePvs(const FVector& APointCm, const FVector& BPointCm) const;
	// How many adopted sources carry a style >= 32 (entity-switched), for the readout.
	int32 SwitchedSourceCount() const;

	// Owner decision: a lightstyle on a FACE, not on
	// a light. VtMB modulates the face's lightmap page by the style's pattern -- the pier's 34
	// `objects/surf` foam cards carry style 1, 21 of them 32 as well -- and Lumen replaced the page
	// project-wide, so the style survives as a brightness the lit base colour and the emissive are
	// multiplied by. The bake splits such faces into their own chunk keyed by (material, style) and
	// tags it `elysium.style=n`; the visuals walk hands the chunks here, and this clock writes
	// `ElysiumLightStyle::SlotBrightness` on each of them every tick, off the same
	// `StylePatterns`/`StyleTime` pair a styled light already animates against.
	//
	// Primitives, not lights: the two share only the clock. A styled chunk has no `.lights` row, no
	// intensity to scale and no calibration to honour.
	struct FStyledPrimitive
	{
		UPrimitiveComponent* Component = nullptr;
		int32 Style = 0;
	};
	// Bind the styled chunks and stamp their first value. Replaces any previous set (one map, one
	// walk). Returns how many were taken -- a row with a style outside 1..63 or a null component is
	// dropped rather than animated against a pattern nothing wrote.
	int32 AdoptStyledPrimitives(const TArray<FStyledPrimitive>& Adopted);
	// One more styled primitive, after the level walk has already run. A brush ENTITY's visual is
	// built by the runtime when the entity world embodies it, which is later than
	// `AdoptBakedLevel` -- and `sm_pier_1`'s 17 `objects/surf` foam bodies, the census's motivating
	// case for G6, are exactly that. Same rules as `AdoptStyledPrimitives`: a null component or a
	// style outside 1..63 is dropped, and the first value is stamped now rather than next tick.
	// Registering the same component twice replaces the earlier row.
	bool AddStyledPrimitive(UPrimitiveComponent* Component, int32 Style);
	int32 StyledPrimitiveCount() const { return StyledPrimitives.Num(); }
	// The brightness the clock is writing for a style right now -- the readout, and what the tests
	// compare the stamped slot against. Same value `StyleMultiplier` answers.
	float StyledPrimitiveBrightness(int32 Style) const { return StyleMultiplier(Style); }

	// R6.2: a `light_dynamic` -- the one light with no lump-15 row -- stands through the legacy
	// derivation: `Light` becomes a non-baked source with the raw magnitude, reach (cm), spot
	// cosines and style, and `ApplyToSource` derives it under the page's calibration exactly as a
	// `.lights` row is (retires with that lane, R9). Returns the source index, or INDEX_NONE.
	int32 AddRuntimeSource(ULightComponent* Light, int32 Type, const FLinearColor& Color, float Mag,
		float RadiusCm, float StopDot, float StopDot2, int32 Style);
	void RemoveRuntimeSource(ULightComponent* Light);

	// Show/hide every spawned light (bound to elysium.lights / the pawn's L key).
	void SetLightsVisible(bool bShow);
	bool AreLightsVisible() const { return bLightsVisible; }

	// One spawned light plus the raw source data needed to re-derive its intensity/reach live
	// (so a settings push or a calibration-asset row applies without a map reload). Populated by Build.
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
		float StopDot = 0.f;        // spotlight inner-cone cosine
		float StopDot2 = 0.f;       // spotlight outer-cone cosine
		float FitMult = 1.f;        // per-area .lightfit rebalance multiplier
		int32 Style = 0;            // animated lightstyle index (0 = unanimated)
		float BaseIntensity = 0.f;  // current pre-style intensity (styled lights scale this per frame)
		FLinearColor Color = FLinearColor::White;   // the sidecar's normalized hue, for revert
		FTransform AuthoredTransform = FTransform::Identity;
		float AuthoredSourceRadiusCm = 0.f;
		float AuthoredSoftSourceRadiusCm = 0.f;
		float AuthoredSourceLengthCm = 0.f;
		bool bAuthoredCastVolumetricShadow = true;
		bool bOverridden = false;   // set by a calibration-asset row or a hand edit; calibration passes skip it
		bool bDisabled = false;     // switched off by a calibration-asset row or a hand edit
		bool bSky = false;          // lights the 3D-skybox miniature, not the playable world
		// R5.6: adopted off a converted map's actor, whose baked values are the baseline. `Mag`,
		// `RadiusCm` and the cosines above are 0 on such a source -- the bake consumed them --
		// and `ApplyToSource` restores this snapshot instead of deriving.
		bool bBaked = false;
		float BakedIntensity = 0.f;
		float BakedReachCm = 0.f;
	};

	// The spawned light sources, for the Cog Lights window's read-only viewer. Skyambient (type 5)
	// is not a light and is absent here.
	const TArray<FLightSource>& Sources() const { return LightSources; }

	// Per-source live override.
	// A hand-set or `UElysiumLightCalibration`-applied source is marked overridden, which takes it
	// out of both passes that would otherwise write over it: the global calibration (ApplyLiveTuning,
	// which every settings push triggers) and the per-frame lightstyle animation. The rest of the rig
	// keeps following the calibration as usual.
	ULightComponent* SourceLight(int32 Index) const;
	bool IsSourceOverridden(int32 Index) const;
	void SetSourceOverridden(int32 Index, bool bOverride);
	// Set one source's intensity by hand, marking it overridden. Goes through the rig rather than
	// straight to the component so the list's readout and the styled base stay in step with it.
	void SetSourceIntensity(int32 Index, float Intensity);
	// Set one source's reach (local/spot attenuation radius) by hand, marking it overridden. No
	// effect on the sun, which has no reach.
	void SetSourceReach(int32 Index, float ReachCm);
	// Set one source's colour by hand, marking it overridden.
	void SetSourceColor(int32 Index, FLinearColor Color);
	// Re-derive one source from its sidecar row + the current calibration, and drop its override.
	// The disable switch below is a separate axis and survives a revert.
	void RevertSource(int32 Index);
	void RevertAllSources();

	// Per-source disable.
	// Switching one source off by hand, orthogonal to the override: it changes no value, so a
	// disabled light keeps its calibrated intensity/reach and comes back exactly as it was. This
	// outranks the master toggle — nothing turns a hand-disabled light back on but enabling it.
	bool IsSourceDisabled(int32 Index) const;
	void SetSourceDisabled(int32 Index, bool bDisable);
	void EnableAllSources();
	// Project terminology: "volumetric lights" means every non-spot source (tex, point and sun),
	// not Unreal's VolumetricScatteringIntensity.
	void SetNonSpotSourcesDisabled(bool bDisable);
	// Should this source be lit right now, per the master toggle and its own disable?
	bool ShouldSourceBeLit(int32 Index) const;

	// Copy every calibration field from `Settings` (`UElysiumLightingSettings`) -- plus the one
	// light-specular knob, `UElysiumSurfaceSettings::LightSpecularScale` (R5.5) -- into this rig's
	// own mirrors. Called at Adopt (so a fresh map load always starts from the current Project
	// Settings pages) and by `UElysiumLightingSettings::PushToWorlds` on every live rig when either
	// page is edited. Does not re-derive sources on its own; call `ApplyLiveTuning` after.
	void ApplySettings(const UElysiumLightingSettings& Settings, const UElysiumSurfaceSettings& Surfaces);

	// Apply a per-map `UElysiumLightCalibration`'s merge rows on top of the calibrated baseline:
	// a row's `bDisabled` and each of its set overrides are applied through the same per-source
	// setters a hand edit uses, so an overridden source is excluded from both `ApplyLiveTuning` and
	// lightstyle animation exactly as before. A row whose `SourceIndex` matches nothing this rig
	// adopted (a stale row after a re-export) is silently skipped. Returns the number of rows applied.
	int32 ApplyCalibrationAsset(const UElysiumLightCalibration* Asset);

	// Re-derive every non-overridden light from the current calibration, including light transport,
	// shadows, source shape, exact spot cone, and the renderer-critical MegaLights policy.
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

	// Calibration — the rig's own live mirror of `UElysiumLightingSettings` (R4.3), kept as separate
	// fields (rather than reading the settings singleton at every derive) so a per-instance edit in
	// the PIE Details panel still works exactly as before, and so ApplyToSource has one cheap,
	// uniform read path regardless of where the value last came from. `ApplySettings` is the only
	// writer that matters in practice; Adopt calls it before deriving anything.
	//
	// Point/spot use Unreal's *non*-inverse-square falloff with a gentle exponent, matching VtMB's
	// soft baked look: inverse-square + candela read too hard — hot speculars, over-bright at the
	// source, then a cliff to black. Specular is the surfaces page's `LightSpecularScale` (R5.5;
	// the legacy 0 was the "pure Lambert" premise the owner repudiated). Intensity
	// (unitless) = clamp(max(rgb) * PointSpotScale, 0, MaxBrightness); reach = radius * RadiusScale.
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float PointSpotScale = 0.003f;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float MaxBrightness = 8.0f;
	// A/B against MaxBrightness (UElysiumLightingSettings::bUseExtendedBrightnessCeiling): the same
	// PointSpotScale against a ceiling high enough that no source reaches it, so the clip becomes the
	// only variable. Every light that was already under MaxBrightness keeps its exact intensity and
	// only the clipped ones move, which puts the whole authored range in front of the filmic tone
	// curve instead of flattening its top half here.
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float ExtendedMaxBrightness = 512.f;
	// Fitting the baked lightmaps (probe_light_calibration.py) shows brightness barely
	// varies with distance-to-light (Spearman ~0, falloff slope ~0): VtMB light is ~flat
	// within its authored radius, so a gentle exponent + authored reach, not inverse-square.
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float FalloffExponent = 1.0f;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float RadiusScale = 1.0f;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float SpecularScale = 1.0f;
	// Per-light Lumen injection multiplier (not the post-process precomputed-lighting control).
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float IndirectLightingScale = 1.0f;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float VolumetricScatteringScale = 1.0f;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float SunScaleLux = 8.0f;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float SunSourceAngleDegrees = 0.5357f;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float SunSoftSourceAngleDegrees = 0.0f;
	UPROPERTY(EditAnywhere, Category = "Elysium|Lighting") float FallbackRadiusCm = 2500.f;
	// The 3D-skybox miniature's uniform scale, applied to a sky source's reach only (its
	// position is baked already scaled). Set from `<map>.sky` at Adopt; 1 on a map with no
	// miniature. Per-map (not a `UElysiumLightingSettings` field) and MinSkyReachCm floors it, so an
	// authored radius near zero still lights something after the scale rather than collapsing.
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
	void AdoptGameplayLight(const FString& InMapName);
	UPROPERTY(Transient) TObjectPtr<UElysiumMapLightQueryData> GameplayLightData;
	UPROPERTY(Transient) TArray<TObjectPtr<UPrimitiveComponent>> GameplayLightColliders;
	UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> GameplaySkyCollider;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> GameplayShadowProps;
	bool bGameplayLightAvailable = false;
	// CWorld seeds only styles0..11; other engine patterns stay empty until LightStyle writes.
	TSet<int32> AssignedGameplayStyles;

	// R7.4 (G6): one styled chunk. `LastBrightness` is kept so the tick writes custom primitive
	// data only when the pattern actually moved -- a style whose keyframe has not changed costs a
	// float compare, not a render-state touch.
	struct FStyledPrimitiveEntry
	{
		TWeakObjectPtr<UPrimitiveComponent> Component;
		int32 Style = 0;
		float LastBrightness = -1.f;
	};
	TArray<FStyledPrimitiveEntry> StyledPrimitives;

	float StyleTime = 0.f;
	// The 64 lightstyle patterns (R6.2), seeded by the constructor.
	FString StylePatterns[MaxLightStyles];
	bool bLightsVisible = true;

	// Per-area rebalance and extended-ceiling toggles, mirrored from `UElysiumLightingSettings` by
	// `ApplySettings` alongside the fields above. Not `UPROPERTY`-exposed: neither is meaningful to
	// edit per-instance (the per-area table is loaded once, at Adopt, from `<map>.lightfit`).
	bool bApplyLightFit = false;
	bool bExtendedRange = false;

	// The map this rig adopted (the `.lights` base name), which keys its `UElysiumLightCalibration`
	// asset lookup.
	FString MapName;

	// Derive one source's intensity, reach, falloff and specular from the tuning constants. The
	// single place that math lives; ApplyLiveTuning is this over every non-overridden source.
	void ApplyToSource(FLightSource& S);
};
