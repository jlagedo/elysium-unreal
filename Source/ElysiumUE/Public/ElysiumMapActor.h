#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/PimplPtr.h"
#include "ElysiumEnvironment.h"   // FElysiumSkyDef — a plain by-value member
#include "ElysiumMapActor.generated.h"

class AStaticMeshActor;
class FElysiumEntityWorld;
class FElysiumSoundSchemeManager;
struct FElysiumTextureCache;
class UAnimSequence;
class USkeletalMesh;
class USkeletalMeshComponent;
class UElysiumLightRig;
class UExponentialHeightFogComponent;
class UProceduralMeshComponent;
class USceneComponent;
class USkyLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

// One loaded VtMB map. The map's *look* — world and 3D-skybox geometry, materials, textures,
// static props, lights, fog — is baked offline into real .uasset content and a real .umap
// (tools/bake_map.py), which UElysiumMapSubsystem::Travel opens; this actor is spawned into that
// level and adopts its actors. What it still builds at load time is everything the bake cannot
// hold: brush collision from .hulls/.dispcol (the walkable surface — baked geometry carries none),
// the .ropes cables, the sky cubemap and backdrop, the Track-B entity substrate and every
// entity-driven body. Everything it builds is a component of (or outer'd to) this actor, so
// destroying it unloads that half. Spawned only by UElysiumMapSubsystem (deferred, MapName set
// before FinishSpawning); BeginPlay builds the map.
UCLASS()
class AElysiumMapActor : public AActor
{
	GENERATED_BODY()

public:
	AElysiumMapActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// Map name under FElysiumContentPaths::Root() (a folder holding <MapName>.obj).
	UPROPERTY(EditAnywhere, Category = "Elysium")
	FString MapName = TEXT("sp_tutorial_1");

	// B7 — the uniform scale a body built for this def takes: the 3D-skybox miniature's scale for
	// a sky-scope entity (its origin and hulls are already carried through the transform by the
	// def parser, but a mesh's own size is not a point), 1 for everything else.
	float BodyScaleFor(const struct FElysiumEntityDef& Def) const;

	// Show/hide the 3D skybox mesh (bound to the T key by the pawn).
	void ToggleSkybox();
	bool IsSkyboxVisible() const;

	// Show/hide the real-time light rig (bound to the L key / elysium.lights).
	void ToggleLights();
	bool AreLightsVisible() const;

	// Show/hide the static-prop instances (elysium.props).
	void ToggleProps();
	bool ArePropsVisible() const;

	// The real-time light rig for this map (the Lights Cog window's source list + live tuning), or
	// null before the map is built.
	UElysiumLightRig* GetLightRig() const { return LightRig; }

	// True once the pawn has been placed and its ground collision has finished cooking
	// (or the spawn-hold timed out). The headless profiler waits on this before capturing.
	bool IsSpawnDone() const { return bSpawnDone; }

	// The live Track-B entity world (P1.4), or null if the map has no `.ents`. Owned by this
	// actor, so it dies on map unload. The `elysium.world*` verbs reach it through here.
	FElysiumEntityWorld* GetEntityWorld() const { return EntityWorld.Get(); }

	// The P6.3 SoundScheme playback manager (ambient bed + music state machine + random scheduler)
	// for this map, or null if the map has no entity world. Owned by this actor (dies on unload); the
	// ambient_soundscheme entities and the Cog Sound Schemes window reach it through here.
	FElysiumSoundSchemeManager* GetSchemeManager() const { return SchemeManager.Get(); }

	// B3 — build one NPC skeletal body: load (cached per stem) out/npc/<Stem>.glb through glTFRuntime
	// and stand a movable USkeletalMeshComponent on this map actor at the given transform, playing its
	// idle clip when the glb carries one (else the reference pose). Returns the component, or null on a
	// missing/failed glb or empty stem. The FElysiumNpc leaf calls this from its Spawn() and registers
	// the result with the entity world for teardown. Meshes/anims are cached on this actor (freed on
	// unload) so a model shared by several NPCs (three Sabbat share shovelhead) loads once.
	USkeletalMeshComponent* BuildNpcVisual(const FString& Stem, const FVector& Location, const FRotator& Rotation,
		float UniformScale = 1.f);

	// The baked SM_<Stem> asset for a prop model, cached per stem (one load per model however many
	// entities place it). Null + a warning naming the bake command when the map has no such asset.
	// Shared by both prop build paths, so a model used by a dynamic and a physics prop loads once.
	UStaticMesh* ResolvePropMesh(const FString& Stem);

	// 8.3 — build one dynamic-prop body: stand a movable UStaticMeshComponent on this map actor at
	// the given transform, drawing the baked prop mesh. Non-solid — a prop_dynamic is dressing, and
	// the mesh's own collision belongs to the physics props that share it. Returns the component, or
	// null on an empty stem / unbaked model. The FElysiumProp leaf calls this from Spawn() and
	// registers the result with the entity world for teardown. The Rotation is the exporter's
	// pre-converted Unreal-space model_quat, read verbatim.
	UStaticMeshComponent* BuildPropVisual(const FString& Stem, const FVector& Location, const FQuat& Rotation,
		float UniformScale = 1.f);

	// 8.4 — build one physics-prop body: the same baked mesh BuildPropVisual stands, which for a
	// physics model carries VtMB's own convex collision (one shape per `.phy` ledge, from the
	// props/<Stem>.phys sidecar) and its authored mass on the body setup, under
	// CTF_UseSimpleAndComplex so a Chaos body can simulate against the simple shapes while the debug
	// pick still gets a per-poly face index. The returned component carries the PhysicsActor profile
	// with collision enabled but is NOT yet simulating — the FElysiumPhysProp leaf drives
	// SetSimulatePhysics / mass / the elysium.PhysicsProps gate. Registered for teardown
	// (RegisterPropBody) like a dynamic prop.
	UStaticMeshComponent* BuildPhysPropVisual(const FString& Stem, const FVector& Location, const FQuat& Rotation,
		float UniformScale = 1.f);

	// Repaint a prop body to one of its model's alternate skin families (VtMB's `skin` keyfield /
	// `Skin` input -- a material remap over the model's own slots, applied instantly). Family 0 and
	// any family the model does not carry restore the authored materials, which is what Source does
	// with an out-of-range skin. Safe on any prop body from either build path. `elysium.PropSkins 0`
	// disables the whole pass.
	void ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family);

	// Live stats for the debug overlay, filled by LoadMap. The geometry counts are actors adopted
	// from the baked level, not surfaces built at runtime.
	FString LoadedMap;
	int32 WorldSurfaceCount = 0;
	int32 SkySurfaceCount = 0;
	int32 WorldLightCount = 0;
	int32 PropInstanceCount = 0;
	int32 PropModelCount = 0;
	// Brush collision (.hulls/.dispcol): convex-hull count and displacement-triangle count. The
	// baked world meshes carry no gameplay collision, so these are the only world collider.
	int32 HullCount = 0;
	int32 DispTriCount = 0;
	bool bBrushCollision = false;
	// Entity substrate: number of `.ents` records the world spawned (0 if the map has no sidecar),
	// and how many of them got a P1.5 brush body (convex collision / trigger overlap volume).
	int32 EntityCount = 0;
	int32 BrushBodyCount = 0;
	// Decals (7.2): number of deferred decal actors adopted from the baked level.
	int32 DecalCount = 0;
	// Ropes (8.7): number of UCableComponents built from <map>.ropes (0 if the map has no ropes or
	// elysium.Ropes is off).
	int32 RopeCount = 0;

	// P4.6 — the `info_landmark` this map load entered through (a landmark transition / direct
	// landmark Travel), or empty for a plain info_player_start spawn. Shown in the Maps Cog window.
	FString EntryLandmark;

	// --- The baked level's actors ------------------------------------------------------------
	// The map's look is real .uasset content in the .umap this actor was spawned into
	// (tools/bake_map.py). AdoptBakedLevel buckets those actors by the tag the bake stamped on
	// them, so the runtime can address them: hide/show them, drive the lights, and let the debug
	// pick name what it hit. Empty until BeginPlay has run.
	const TArray<TObjectPtr<AStaticMeshActor>>& GetWorldActors() const { return WorldActors; }
	const TArray<TObjectPtr<AStaticMeshActor>>& GetSkyActors() const { return SkyActors; }
	const TArray<TObjectPtr<AStaticMeshActor>>& GetPropActors() const { return PropActors; }

	// The baked sky light and height fog, adopted from the level so the Lights Cog window can tune
	// the map's ambience live. Null if the bake did not place them.
	USkyLightComponent* GetSkyLight() const { return SkyLight; }
	UExponentialHeightFogComponent* GetHeightFog() const { return HeightFog; }

	// Per-phase load timings (milliseconds), filled by LoadMap in build order, for the Maps Cog
	// window. The last entry is always the "Total". Empty until the first load completes.
	struct FLoadPhase
	{
		FString Name;
		double Milliseconds = 0.0;
	};
	TArray<FLoadPhase> LoadPhases;

private:
	UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
	// Collision-only world colliders built from the pipeline's brush sidecars. The baked world
	// meshes carry no gameplay collision, so these ARE the walkable surface: HullCollision holds
	// one convex element per solid world brush (.hulls, invisible clip brushes included);
	// DispCollision is the displacement terrain trimesh (.dispcol). Both invisible.
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> HullCollision;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> DispCollision;
	// The 2D six-face skybox backdrop: a large inward box sampling the sky cubemap through M_Sky.
	// Built at runtime because its cubemap is assembled from the six exported face images, which
	// is also what feeds the SkyLight's IBL. Distinct from the baked 3D-skybox miniature geometry.
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> SkyDomeMesh;
	// The backdrop's own MID (off M_Sky), kept so elysium.SkyBrightness can re-apply live.
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SkyMid;
	// B7 — the 3D-skybox miniature's placement (`<map>.sky`), or the identity on the 65 maps
	// with no `sky_camera`. Read at map load and used twice: the def parser carries sky-scope
	// entities through it, and a miniature body takes its uniform mesh scale from it.
	FElysiumSkyDef SkyDef;
	UPROPERTY() TObjectPtr<UElysiumLightRig> LightRig;

	// Adopted from the baked level (not owned): the map's ambience. Their tuning fields are driven
	// by the light rig so the Lights Cog window reaches them.
	UPROPERTY() TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> HeightFog;
	// C3/D3 — the map's unbound PostProcessVolume, where a per-map Lumen art-direction value
	// lives. Baked neutral (nothing overridden), so it changes no pixel until someone puts a
	// number on it; `elysium.SkylightLeaking` / `elysium.IndirectIntensity` are how C4/C5 try
	// one without a rebuild.
	UPROPERTY() TObjectPtr<class APostProcessVolume> PostProcess;

	// The baked level's geometry actors, bucketed by the bake's tags. Not owned — they belong to
	// the level and die with it; these are handles for visibility toggles and the debug pick.
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> WorldActors;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> SkyActors;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> PropActors;
	bool bPropsVisible = true;
	bool bSkyVisible = true;

	// Ropes (8.7): one Verlet UCableComponent per <map>.ropes segment (an overhead cable), kept
	// alive for the map's lifetime (freed on unload). MIDs off M_World_Opaque bound to the decoded
	// RopeMaterial texture; the cable's fixed endpoints and rest length come straight from the sidecar.
	UPROPERTY() TArray<TObjectPtr<class UCableComponent>> Ropes;

	// B3 NPC skeletal bodies: per-stem mesh + idle-anim cache, GC-rooted here so a model shared by
	// several NPCs loads once and survives until unload. The USkeletalMeshComponents themselves are
	// components of this actor (rooted via AddInstanceComponent), freed with it. An idle entry may be
	// null (the glb has no idle clip → reference pose); the stem is still cached to avoid re-scanning.
	UPROPERTY() TMap<FString, TObjectPtr<USkeletalMesh>> NpcMeshCache;
	UPROPERTY() TMap<FString, TObjectPtr<UAnimSequence>> NpcIdleCache;

	// 8.3 dynamic-prop static meshes: per-stem cache, GC-rooted here so a model placed by several
	// prop entities builds once and survives until unload. The UStaticMeshComponents themselves are
	// components of this actor (AddInstanceComponent), gated/moved by their FElysiumProp leaf, and
	// torn down by the entity world (RegisterPropBody), mirroring the NPC bodies.
	UPROPERTY() TMap<FString, TObjectPtr<UStaticMesh>> PropMeshCache;
	// The map's baked prop skin table, loaded once on first use. bPropSkinsLoaded separates
	// "not looked for yet" from "this map has none" (most maps have none, and a miss must not
	// re-hit LoadObject per prop).
	UPROPERTY() TObjectPtr<class UElysiumPropSkinSet> PropSkins;
	bool bPropSkinsLoaded = false;

	// This map's decoded-texture dedup index (one UTexture2D per unique path, shared across the
	// map's material instances). A plain C++ object owned here, so its strong texture refs drop
	// when the actor is torn down on unload and GC reclaims the textures — no process-wide cache,
	// no manual flush. Created at the top of LoadMap, before any material is built.
	TPimplPtr<FElysiumTextureCache> TextureCache;

	// The Track-B entity substrate for this map (P1.4): parsed defs, live entities, the event
	// queue, and the debug sinks. A plain C++ object (no UObject) held type-erased so the header
	// needs only a forward declaration; destroyed with the actor on map unload.
	TPimplPtr<FElysiumEntityWorld> EntityWorld;

	// The P6.3 SoundScheme manager (plain C++, owned here). Constructed alongside EntityWorld so the
	// ambient_soundscheme entities can reach it during their spawn pass; ticked from Tick with the
	// player location; its voices are stopped on unload (EndPlay).
	TPimplPtr<FElysiumSoundSchemeManager> SchemeManager;

	void LoadMap();
	// Walk the baked level once and bucket its actors by the tag tools/bake_map.py stamped on them
	// (elysium.world / .sky / .prop / .light / .skylight / .fog), filling WorldActors, SkyActors,
	// PropActors, SkyLight and HeightFog and handing the light rig its sources to adopt. Returns
	// the number of tagged actors found; 0 means this world is not a baked level.
	int32 AdoptBakedLevel();
	// Build convex world collision from <map>.hulls (one FKConvexElem per solid brush) onto
	// HullCollision. Returns true when at least one hull loaded — the caller then drops the
	// render-mesh trimesh, making the brushes (with their invisible clip volumes) the walkable
	// surface. False (sidecar missing/empty) leaves the trimesh fallback in place.
	bool LoadHulls();
	// Build the displacement terrain trimesh from <map>.dispcol onto DispCollision. Only meaningful
	// alongside brush collision; no-op when the sidecar is absent (map has no displacements).
	void LoadDispCol();
	// Build the overhead cables from <map>.ropes (8.7): one Verlet UCableComponent per segment,
	// fixed at both endpoints, rest length straight off the sidecar (below the span for a taut cable,
	// above it for one that hangs), width/texture
	// from the sidecar, material a MID off M_World_Opaque. No-op when the sidecar is absent or
	// elysium.Ropes is 0.
	void BuildRopes();
	// Assemble the six exported sky faces into one UTextureCube and use it twice: as the visible
	// backdrop on SkyDomeMesh (through M_Sky), and as the adopted SkyLight's IBL source. A real
	// cubemap on the sky light is what gives Lumen sky occlusion — interiors then darken because
	// they cannot see the sky, instead of receiving a constant fill through solid walls.
	void ApplyEnvironment();
	// The SkyLight's intensity for this map, from the type-5 `emit_skyambient` magnitude and the
	// cube's own upper-hemisphere mean radiance (C1/C2, D2). Zero on the 83 maps with no sky
	// pair, zero where the pair authors a zero, and otherwise the factor that makes the cube
	// deliver VtMB's stated sky radiance. `CubeUpperMean` 0 means "no cube".
	float SkyAmbientIntensity(float CubeUpperMean) const;
	// Push the elysium.* art-direction cvars onto the adopted PPV. A negative value means
	// "leave it neutral" — the override is cleared, not set to a default — so the shipped state
	// and an experiment are distinguishable rather than merely equal-looking.
	void ApplyPostProcessKnobs();
	// Push elysium.SkyBrightness onto the live backdrop MID. The faithful value is 1 (D7): VtMB's
	// sky transfer is the identity, so this is an A/B knob, not a calibration. No-op with no sky.
	void ApplySkyBrightness();
	bool ReadSpawn(FVector& OutLocation, float& OutYaw) const;
	// P4.6 — if this load is a landmark transition (the map subsystem has a queued landmark spawn),
	// override the info_player_start placement: resolve the destination `info_landmark` in the just-
	// built entity world and seat the player at landmark origin + the carried offset. Fires the
	// landmark's OnEnterMapHere. No-op (keeps the .spawn placement) for a plain load or a missing
	// landmark. Called by LoadMap after the entity world is built.
	void ResolveLandmarkSpawn();

	// The player pawn may not exist yet in BeginPlay, so the teleport is deferred to Tick;
	// the pawn is then held frozen until the async collision cook yields ground beneath it.
	bool bSpawnPending = false;
	bool bSpawnPlaced = false;
	bool bSpawnDone = false;
	float SpawnHoldSeconds = 0.f;
	FVector PendingSpawnLoc = FVector::ZeroVector;
	float PendingSpawnYaw = 0.f;
};
