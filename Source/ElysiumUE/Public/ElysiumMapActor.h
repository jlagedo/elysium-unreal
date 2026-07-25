#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/PimplPtr.h"
#include "ElysiumMapActor.generated.h"

class FElysiumEntityWorld;
class FElysiumSoundSchemeManager;
struct FElysiumTextureCache;
class UAnimSequence;
class UDecalComponent;
class UDirectionalLightComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UElysiumLightRig;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UPostProcessComponent;
class UProceduralMeshComponent;
class USceneComponent;
class USkyLightComponent;
class UStaticMesh;

// One loaded VtMB map, built at runtime from the shared Python-pipeline intermediates
// (no imported .uasset content): the exported OBJ as one procedural-mesh section per
// material with its albedo texture, the 3D skybox, placeholder lighting, and the player
// spawn. Everything map-scoped is a component of (or outer'd to) this actor, so destroying
// it unloads the map. Spawned only by UElysiumMapSubsystem (deferred, MapName set before
// FinishSpawning); BeginPlay builds the map.
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
	USkeletalMeshComponent* BuildNpcVisual(const FString& Stem, const FVector& Location, const FRotator& Rotation);

	// 8.3 — build one dynamic-prop body: parse props/<Stem>.obj (cached per stem, built once
	// through FElysiumStaticMeshBuilder like LoadProps) and stand a movable UStaticMeshComponent on
	// this map actor at the given transform. Non-solid (visual parity first; prop_physics collision
	// is 8.4). Returns the component, or null on an empty stem / missing/failed OBJ. The FElysiumProp
	// leaf calls this from Spawn() and registers the result with the entity world for teardown. The
	// Rotation is the exporter's pre-converted Unreal-space model_quat, read verbatim.
	UStaticMeshComponent* BuildPropVisual(const FString& Stem, const FVector& Location, const FQuat& Rotation);

	// 8.4 — build one physics-prop body: like BuildPropVisual but the mesh is cooked with convex
	// collision (the decomposed props/<Stem>.hulls sidecar, one FKConvexElem per part, or a single
	// whole-model hull when absent), the collision-cooked mesh cached under a distinct key so a model
	// shared with a non-solid prop_dynamic does not clash. The returned component carries the
	// PhysicsActor profile with collision enabled but is NOT yet simulating — the FElysiumPhysProp
	// leaf drives SetSimulatePhysics / mass / the elysium.PhysicsProps gate. Registered for teardown
	// (RegisterPropBody) like a dynamic prop.
	UStaticMeshComponent* BuildPhysPropVisual(const FString& Stem, const FVector& Location, const FQuat& Rotation);

	// Live stats for the debug overlay, filled by LoadMap.
	FString LoadedMap;
	int32 WorldSurfaceCount = 0;
	int32 SkySurfaceCount = 0;
	int32 WorldLightCount = 0;
	int32 PropInstanceCount = 0;
	int32 PropModelCount = 0;
	// Brush collision (.hulls/.dispcol): convex-hull count, displacement-triangle count, and
	// whether brush collision is the active world collider (vs. the render-mesh trimesh).
	int32 HullCount = 0;
	int32 DispTriCount = 0;
	bool bBrushCollision = false;
	// Entity substrate: number of `.ents` records the world spawned (0 if the map has no sidecar),
	// and how many of them got a P1.5 brush body (convex collision / trigger overlap volume).
	int32 EntityCount = 0;
	int32 BrushBodyCount = 0;
	// Decals (7.2): number of deferred UDecalComponents built from <map>.decals (0 if the map has
	// no decals or elysium.Decals is off).
	int32 DecalCount = 0;
	// Ropes (8.7): number of UCableComponents built from <map>.ropes (0 if the map has no ropes or
	// elysium.Ropes is off).
	int32 RopeCount = 0;

	// P4.6 — the `info_landmark` this map load entered through (a landmark transition / direct
	// landmark Travel), or empty for a plain info_player_start spawn. Shown in the Maps Cog window.
	FString EntryLandmark;

#if !UE_BUILD_SHIPPING
	// --- P2.6 click-pick surface -------------------------------------------------------------
	// The debug pick (ElysiumPick.h) ray-casts the mesh data on the CPU, because physics cannot
	// answer what it needs: the world render mesh carries no collision under the default
	// elysium.BrushCollision 1, and a solid prop's cooked collision is one convex hull of the
	// whole model. These accessors hand it the CPU-side geometry; all of it is non-Shipping.

	// One unique prop model's triangle soup, retained past LoadProps for triangle-exact picking
	// (~3 MB across the tutorial's 161 models). Positions are model-local; Tris indexes them.
	struct FPropPickSoup
	{
		FString Model;
		TArray<FVector> Positions;
		TArray<int32> Tris;
	};

	UProceduralMeshComponent* GetWorldMesh() const { return WorldMesh; }
	UProceduralMeshComponent* GetSkyMesh() const { return SkyMesh; }
	const TArray<TObjectPtr<UInstancedStaticMeshComponent>>& GetPropComponents() const { return PropComponents; }
	const TArray<FPropPickSoup>& GetPropPickSoups() const { return PropPickSoups; }
	// The soup index backing an ISM's model, or INDEX_NONE. Both solidity buckets of one model
	// map to the same soup.
	int32 GetPropSoupIndex(const UInstancedStaticMeshComponent* Ism) const;
	// The OBJ group key ("<material>@<cubemap>") behind a mesh section, or empty. The section
	// index is the one CreateMeshSection was called with, so it indexes these 1:1.
	const FString& GetSectionMaterialName(const UProceduralMeshComponent* Mesh, int32 Section) const;
#endif

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
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> WorldMesh;
	// Collision-only world colliders built from the pipeline's brush sidecars, preferred over
	// WorldMesh's render-trimesh: HullCollision holds one convex element per solid world brush
	// (.hulls, invisible clip brushes included); DispCollision is the displacement terrain
	// trimesh (.dispcol). Both invisible.
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> HullCollision;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> DispCollision;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> SkyMesh;
	// The 2D six-face skybox backdrop: a large inward box sampling the sky cubemap through
	// M_Sky. Distinct from SkyMesh (the 3D skybox miniature geometry).
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> SkyDomeMesh;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> SunLight;
	UPROPERTY() TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY() TObjectPtr<UElysiumLightRig> LightRig;
	UPROPERTY() TObjectPtr<UPostProcessComponent> PostProcess;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> HeightFog;

	// Static props: one ISM per (unique model, solidity) bucket, over runtime-built meshes.
	// Both arrays keep the objects alive for the map's lifetime (freed on map unload).
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> PropComponents;
	UPROPERTY() TArray<TObjectPtr<UStaticMesh>> PropMeshes;
	bool bPropsVisible = true;

	// Decals (7.2): one deferred UDecalComponent per <map>.decals line, kept alive for the map's
	// lifetime (freed on unload). MIDs off M_Decal, projected onto the world by their box transform.
	UPROPERTY() TArray<TObjectPtr<UDecalComponent>> Decals;

	// Ropes (8.7): one Verlet UCableComponent per <map>.ropes segment (an overhead cable), kept
	// alive for the map's lifetime (freed on unload). MIDs off M_World_Opaque bound to the decoded
	// RopeMaterial texture; the cable's fixed endpoints and slack come straight from the sidecar.
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

#if !UE_BUILD_SHIPPING
	// P2.6 click-pick CPU geometry, filled during the build and freed with the actor.
	TArray<FPropPickSoup> PropPickSoups;
	TMap<const UInstancedStaticMeshComponent*, int32> PropSoupByComponent;
	TArray<FString> WorldSectionNames;   // WorldMesh section index -> OBJ group key
	TArray<FString> SkySectionNames;     // SkyMesh section index   -> OBJ group key
#endif

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
	void LoadProps();
	// Build convex world collision from <map>.hulls (one FKConvexElem per solid brush) onto
	// HullCollision. Returns true when at least one hull loaded — the caller then drops the
	// render-mesh trimesh, making the brushes (with their invisible clip volumes) the walkable
	// surface. False (sidecar missing/empty) leaves the trimesh fallback in place.
	bool LoadHulls();
	// Build the displacement terrain trimesh from <map>.dispcol onto DispCollision. Only meaningful
	// alongside brush collision; no-op when the sidecar is absent (map has no displacements).
	void LoadDispCol();
	int32 BuildMeshFromObj(const FString& ObjPath, UProceduralMeshComponent* Mesh, bool bCollision);
	// Build the deferred decals from <map>.decals (7.2): one UDecalComponent per line, oriented so
	// its -X projects into the wall along the decal's room normal, sized to the on-surface extents,
	// with a MID off M_Decal. No-op when the sidecar is absent or elysium.Decals is 0.
	void BuildDecals();
	// Build the overhead cables from <map>.ropes (8.7): one Verlet UCableComponent per segment,
	// fixed at both endpoints, rest length = straight distance + slack (so it hangs), width/texture
	// from the sidecar, material a MID off M_World_Opaque. No-op when the sidecar is absent or
	// elysium.Ropes is 0.
	void BuildRopes();
	void ApplySkyTransform();
	// Per-map colour grade (.cube), sky IBL ambient + height fog (.env). Sets the SkyLight
	// cubemap but leaves RecaptureSky to the caller.
	void ApplyEnvironment();
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
