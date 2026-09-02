#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ElysiumEnvironment.h"   // FElysiumEnvDef — a plain by-value member
#include "ElysiumMapVisuals.generated.h"

class AElysiumDetailPropActor;
class AElysiumSpriteActor;
class AStaticMeshActor;
class APostProcessVolume;
class UCableComponent;
class UElysiumLightRig;
class UExponentialHeightFogComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class USkyLightComponent;
struct FElysiumSkyDef;

// The map's LOOK. A component on AElysiumMapActor holding everything that decides what the map
// renders and nothing that decides what it does: the baked level's actors and the handles onto
// them, the sky cube and its backdrop, the sky light's level, the two fog sets, the light rig, and
// the overhead cables.
//
// The split is the module's engine/rendering seam made physical:
// the map actor orchestrates the load and IS the substrate's engine side (`ElysiumWorldServices.h`),
// while every `elysium.*` look knob, every adopted primitive and every material lives here. Nothing
// in this class knows what an entity is.
//
// Its own child components (the backdrop mesh, the light rig) are built at load through the
// standard runtime-component recipe — NewObject on the owning actor, SetupAttachment, Register —
// so a map with no sky builds no backdrop, and everything dies with the actor on unload.
UCLASS()
class UElysiumMapVisuals : public USceneComponent
{
	GENERATED_BODY()

public:
	UElysiumMapVisuals();

	virtual void BeginPlay() override;

	// Build, in load order.
	// Walk the baked level once and bucket its actors by the tag pipeline/unreal/bake_map.py stamped on them
	// (elysium.world / .sky / .prop / .light / .skylight / .fog / .postprocess / .decal), filling the
	// actor buckets, SkyLight, HeightFog and PostProcess, and handing the light rig its sources to
	// adopt. Returns the number of tagged actors found; 0 means this world is not a baked level.
	int32 AdoptBakedLevel(const FString& MapName, const FElysiumSkyDef& SkyDef);

	// Build the overhead cables from <map>.ropes: one Verlet UCableComponent per segment,
	// fixed at both endpoints, rest length straight off the sidecar (below the span for a taut cable,
	// above it for one that hangs), width from the sidecar, material a dynamic child of the `MI_`
	// the sidecar's `vtmb:material:` id names (R6.5). No-op when the sidecar is absent or
	// elysium.Ropes is 0.
	void BuildRopes(const FString& MapName);

	// Assemble the six exported sky faces into one UTextureCube and use it twice: as the visible
	// backdrop on SkyDomeMesh (through M_Sky), and as the adopted SkyLight's IBL source. A real
	// cubemap on the sky light is what gives Lumen sky occlusion — interiors then darken because
	// they cannot see the sky, instead of receiving a constant fill through solid walls.
	// `Env` is the map's already-resolved `.env` values — the baked `UElysiumMapEnvironment` when
	// R4.4 converted this map, the sidecar otherwise — resolved once by the caller alongside SkyDef
	// (`ElysiumMapEnvironmentSource::Load`), not re-read here.
	//
	// `MapName` is used for exactly one question (R5.2): is this map on `MapsOnV2Models`? A map on
	// that flag was baked with its own real SkyLight (`SLS_SpecifiedCubemap`, the true cube, the
	// true intensity) and its own real backdrop dome — both authored once at bake by the same
	// `ElysiumEnvironment::BuildSkyCubeFrom` join this function still runs for every other map — so
	// this function returns right after `ApplySceneFog` and touches neither: rebuilding a transient
	// cube here would not merely waste the work, it would silently fight the baked asset on the
	// SkyLight's next `RecaptureSky`.
	void ApplyEnvironment(const FElysiumEnvDef& Env, const FString& MapName);

	// Run at map activation, once everything the map places is standing: walk every mesh component
	// in the level and report the mesh assets carrying a slot bound to nothing or to the engine's
	// WorldGridMaterial fallback. Unreal substitutes that fallback without a word, so a bake that
	// wrote no material renders as grey checker with nothing in the log to say so. Diagnostic
	// only — the offenders are warnings and the map goes on loading.
	void AuditMaterials(const FString& MapName) const;

	// The live knobs (each is also a cvar callback, so each is idempotent).
	// Push elysium.SkyBrightness onto the live backdrop MID. The faithful value is 1 (D7): VtMB's
	// sky transfer is the identity, so this is an A/B knob, not a calibration. No-op with no sky.
	void ApplySkyBrightness();
	// Stamp each adopted primitive with the fog set that owns it: `worldspawn`'s on the world
	// and its props, the `sky_camera`'s on the 3D-skybox miniature. Custom primitive data, because
	// the two share screen depth and a deferred fog pass cannot scope by anything else
	// (ElysiumFog.h). Re-run by elysium.Fog, which stamps zeros instead.
	void ApplySceneFog();
	void RegisterRuntimeBrush(UStaticMeshComponent* Comp, bool bSky);

	// Visibility A/Bs.
	// Show/hide the 3D skybox miniature and the backdrop dome together (elysium.togglesky).
	void ToggleSkybox();
	bool IsSkyboxVisible() const { return bSkyVisible; }
	// Show/hide the static-prop instances (elysium.props).
	void ToggleProps();
	bool ArePropsVisible() const { return bPropsVisible; }
	// Show/hide the real-time light rig (elysium.lights).
	void ToggleLights();
	bool AreLightsVisible() const;

	// The adopted scene.
	// The baked level's actors, bucketed by the bake's tags. Not owned — they belong to the level
	// and die with it; these are handles for the visibility toggles and the debug pick.
	const TArray<TObjectPtr<AStaticMeshActor>>& GetWorldActors() const { return WorldActors; }
	const TArray<TObjectPtr<AStaticMeshActor>>& GetSkyActors() const { return SkyActors; }
	const TArray<TObjectPtr<AStaticMeshActor>>& GetPropActors() const { return PropActors; }
	// R6.3: one per detail model per map, every `dprp` record of that model as an instance.
	const TArray<TObjectPtr<AElysiumDetailPropActor>>& GetDetailActors() const { return DetailActors; }
	// R6.1: one per `env_sprite`, bucketed by the entity index its tag carries.
	const TArray<TObjectPtr<AElysiumSpriteActor>>& GetSpriteActors() const { return SpriteActors; }
	// R6.1: CSprite's draw switch for the sprite standing for `EntityIndex`. False when this map
	// bakes no such sprite (a legacy-lane map, or an index that is not an env_sprite).
	bool SetSpriteVisible(int32 EntityIndex, bool bShown);

	// The real-time light rig for this map (the Cog Lights window's read-only viewer reaches it
	// through here), or null before the map is built.
	UElysiumLightRig* GetLightRig() const { return LightRig; }
	// The baked sky light and height fog, adopted from the level. Null if the bake did not place
	// them. Ambience tuning has no live surface yet (R4.4's per-map environment asset owns it); this
	// is exposed for the actors that already touch it directly (SkyAmbientIntensity, ApplySceneFog).
	USkyLightComponent* GetSkyLight() const { return SkyLight; }
	UExponentialHeightFogComponent* GetHeightFog() const { return HeightFog; }
	APostProcessVolume* GetPostProcess() const { return PostProcess; }

	// Live stats for the debug overlay, filled by the build. The geometry counts are actors adopted
	// from the baked level, not surfaces built at runtime.
	int32 WorldSurfaceCount = 0;
	int32 SkySurfaceCount = 0;
	int32 WorldLightCount = 0;
	int32 PropInstanceCount = 0;
	int32 PropModelCount = 0;
	// Detail props (R6.3): instances summed over the adopted `elysium.detail` actors, and the
	// distinct models behind them.
	int32 DetailInstanceCount = 0;
	int32 DetailModelCount = 0;
	// Sprites (R6.1): the adopted `elysium.sprite` actors, and how many of them are coronas
	// (rendermode 3/9, the glow rule and its occlusion query).
	int32 SpriteCount = 0;
	int32 SpriteGlowCount = 0;
	// Decals: number of deferred decal actors adopted from the baked level.
	int32 DecalCount = 0;
	// Ropes: number of UCableComponents built from <map>.ropes (0 if the map has no ropes or
	// elysium.Ropes is off).
	int32 RopeCount = 0;

private:
	// The SkyLight's intensity for this map, from the type-5 `emit_skyambient` magnitude and the
	// cube's own upper-hemisphere mean radiance (C1/C2, D2). Zero on the 83 maps with no sky pair,
	// zero where the pair authors a zero, and otherwise the factor that makes the cube deliver
	// VtMB's stated sky radiance. `CubeUpperMean` 0 means "no cube".
	float SkyAmbientIntensity(float CubeUpperMean) const;

	// The 2D six-face skybox backdrop: a large inward box sampling the sky cubemap through M_Sky.
	// Built at runtime because its cubemap is assembled from the six exported face images, which is
	// also what feeds the SkyLight's IBL. Distinct from the baked 3D-skybox miniature geometry.
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> SkyDomeMesh;
	// The backdrop's own MID (off M_Sky), kept so elysium.SkyBrightness can re-apply live.
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SkyMid;
	// The BAKED backdrop (R5.2, `MapsOnV2Models` maps only): a StaticMeshActor adopted off the
	// `elysium.skydome` tag instead of built at runtime. `SkyDomeMesh`/`SkyMid` stay null on
	// these maps — `ApplyEnvironment` returns before ever touching them.
	UPROPERTY() TObjectPtr<AStaticMeshActor> BakedSkyDomeActor;
	UPROPERTY() TObjectPtr<UElysiumLightRig> LightRig;

	// `<map>.env`: the sky name and orientation convention, and the map's TWO fog sets
	// (`worldspawn`'s for the world, `sky_camera`'s for the miniature). Kept past load so
	// `elysium.Fog` can re-stamp the primitives live.
	FElysiumEnvDef EnvDef;

	// Adopted from the baked level (not owned): the map's ambience.
	UPROPERTY() TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> HeightFog;
	// C3/D3 — the map's unbound PostProcessVolume, where a per-map Lumen art-direction value lives.
	// Baked neutral (nothing overridden), so it changes no pixel until a future bake puts a number
	// on it (the R4.5-retired live A/B knobs are gone; the settled home for these values is a bake
	// output, not a runtime override).
	UPROPERTY() TObjectPtr<APostProcessVolume> PostProcess;

	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> WorldActors;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> SkyActors;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> PropActors;
	UPROPERTY() TArray<TObjectPtr<AElysiumDetailPropActor>> DetailActors;
	UPROPERTY() TArray<TObjectPtr<AElysiumSpriteActor>> SpriteActors;
	// The sprite actor per entity index, the key the leaf's writes arrive by.
	TMap<int32, TWeakObjectPtr<AElysiumSpriteActor>> SpritesByEntity;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> RuntimeWorldBrushes;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> RuntimeSkyBrushes;
	bool bPropsVisible = true;
	bool bSkyVisible = true;

	// Ropes: one Verlet UCableComponent per <map>.ropes segment (an overhead cable), kept
	// alive for the map's lifetime. Each binds a dynamic child of the imported `MI_` its line
	// names; the cable's fixed endpoints and rest length come straight from the sidecar.
	UPROPERTY() TArray<TObjectPtr<UCableComponent>> Ropes;
};
