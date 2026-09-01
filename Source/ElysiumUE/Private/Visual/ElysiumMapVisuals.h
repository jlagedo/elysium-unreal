#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Templates/PimplPtr.h"
#include "ElysiumEnvironment.h"   // FElysiumEnvDef — a plain by-value member
#include "ElysiumMapVisuals.generated.h"

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
struct FElysiumTextureCache;

// The map's LOOK. A component on AElysiumMapActor holding everything that decides what the map
// renders and nothing that decides what it does: the baked level's actors and the handles onto
// them, the material-override MIDs the look-tuning cvars reach through, the sky cube and its
// backdrop, the sky light's level, the two fog sets, the Lumen art-direction knobs on the map's
// PostProcessVolume, the light rig, and the overhead cables.
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
	// adopt. Stands the material overrides up in the same pass. Returns the number of tagged actors
	// found; 0 means this world is not a baked level.
	int32 AdoptBakedLevel(const FString& MapName, const FElysiumSkyDef& SkyDef);

	// Build the overhead cables from <map>.ropes: one Verlet UCableComponent per segment,
	// fixed at both endpoints, rest length straight off the sidecar (below the span for a taut cable,
	// above it for one that hangs), width/texture from the sidecar, material a MID off M_World_Opaque.
	// No-op when the sidecar is absent or elysium.Ropes is 0.
	void BuildRopes(const FString& MapName);

	// Assemble the six exported sky faces into one UTextureCube and use it twice: as the visible
	// backdrop on SkyDomeMesh (through M_Sky), and as the adopted SkyLight's IBL source. A real
	// cubemap on the sky light is what gives Lumen sky occlusion — interiors then darken because
	// they cannot see the sky, instead of receiving a constant fill through solid walls.
	// `Env` is the map's already-resolved `.env` values — the baked `UElysiumMapEnvironment` when
	// R4.4 converted this map, the sidecar otherwise — resolved once by the caller alongside SkyDef
	// (`ElysiumMapEnvironmentSource::Load`), not re-read here. Takes no `MapName`: every path this
	// function still touches (the sky-face directories) is keyed by the sky's own name in `Env`,
	// not the map's.
	void ApplyEnvironment(const FElysiumEnvDef& Env);

	// Run at map activation, once everything the map places is standing: walk every mesh component
	// in the level and report the mesh assets carrying a slot bound to nothing or to the engine's
	// WorldGridMaterial fallback. Unreal substitutes that fallback without a word, so a bake that
	// wrote no material renders as grey checker with nothing in the log to say so. Diagnostic
	// only — the offenders are warnings and the map goes on loading.
	void AuditMaterials(const FString& MapName) const;

	// The live knobs (each is also a cvar callback, so each is idempotent).
	// Stand a UMaterialInstanceDynamic in front of every unique baked material on the world, sky
	// and prop components, so the look-tuning cvars can reach them (a baked MaterialInstanceConstant
	// has no runtime setter). Builds the MID set on the first call and re-applies the current cvar
	// values on every call, so a cvar callback is just a re-run. No-op under
	// elysium.MaterialOverrides 0, which leaves the baked values exactly as authored.
	void ApplyMaterialOverrides();
	// Push the elysium.* art-direction cvars onto the adopted PPV. A negative value means "leave it
	// neutral" — the override is cleared, not set to a default — so the shipped state and an
	// experiment are distinguishable rather than merely equal-looking.
	void ApplyPostProcessKnobs();
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

	// The real-time light rig for this map (the Cog Lights window's read-only viewer reaches it
	// through here), or null before the map is built.
	UElysiumLightRig* GetLightRig() const { return LightRig; }
	// The baked sky light and height fog, adopted from the level. Null if the bake did not place
	// them. Ambience tuning has no live surface yet (R4.4's per-map environment asset owns it); this
	// is exposed for the actors that already touch it directly (SkyAmbientIntensity, ApplySceneFog).
	USkyLightComponent* GetSkyLight() const { return SkyLight; }
	UExponentialHeightFogComponent* GetHeightFog() const { return HeightFog; }
	APostProcessVolume* GetPostProcess() const { return PostProcess; }

	// This map's decoded-texture dedup index, created with the component and released with it.
	// Anything building a material at runtime off this map's images shares it.
	FElysiumTextureCache& TextureCache() const { return *TexCache; }

	// Live stats for the debug overlay, filled by the build. The geometry counts are actors adopted
	// from the baked level, not surfaces built at runtime.
	int32 WorldSurfaceCount = 0;
	int32 SkySurfaceCount = 0;
	int32 WorldLightCount = 0;
	int32 PropInstanceCount = 0;
	int32 PropModelCount = 0;
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
	UPROPERTY() TObjectPtr<UElysiumLightRig> LightRig;

	// `<map>.env`: the sky name and orientation convention, and the map's TWO fog sets
	// (`worldspawn`'s for the world, `sky_camera`'s for the miniature). Kept past load so
	// `elysium.Fog` can re-stamp the primitives live.
	FElysiumEnvDef EnvDef;

	// Adopted from the baked level (not owned): the map's ambience.
	UPROPERTY() TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> HeightFog;
	// C3/D3 — the map's unbound PostProcessVolume, where a per-map Lumen art-direction value lives.
	// Baked neutral (nothing overridden), so it changes no pixel until someone puts a number on it;
	// `elysium.SkylightLeaking` / `elysium.LumenDiffuseBoost` are how C4/C5 try one without a rebuild.
	UPROPERTY() TObjectPtr<APostProcessVolume> PostProcess;

	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> WorldActors;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> SkyActors;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> PropActors;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> RuntimeWorldBrushes;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> RuntimeSkyBrushes;
	bool bPropsVisible = true;
	bool bSkyVisible = true;

	// One MID per unique baked material, standing in front of the level's own instances so the
	// look-tuning cvars reach real surfaces. The bake authors MaterialInstanceConstants; a constant
	// has no runtime setter, so without this every elysium.* material knob is dead on the path that
	// actually renders. Keyed by the baked material so a material shared by 40 components makes one
	// MID, not 40. Populated by ApplyMaterialOverrides at adopt; dropped with the component.
	UPROPERTY() TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInstanceDynamic>> MaterialOverrides;

	// Ropes: one Verlet UCableComponent per <map>.ropes segment (an overhead cable), kept
	// alive for the map's lifetime. MIDs off M_World_Opaque bound to the decoded RopeMaterial
	// texture; the cable's fixed endpoints and rest length come straight from the sidecar.
	UPROPERTY() TArray<TObjectPtr<UCableComponent>> Ropes;

	// This map's decoded-texture dedup index. A plain C++ object owned here, so its strong texture
	// refs drop when the component is torn down on unload and GC reclaims the textures — no
	// process-wide cache, no manual flush.
	TPimplPtr<FElysiumTextureCache> TexCache;
};
