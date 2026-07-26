#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/PimplPtr.h"
#include "ElysiumEnvironment.h"    // FElysiumSkyDef — a plain by-value member
#include "ElysiumWorldServices.h"  // the four interfaces this actor implements
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
class UglTFRuntimeAsset;
class UProceduralMeshComponent;
class USceneComponent;
class USkyLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class AElysiumMapActor;

// S2 — the map's post-move tick (runtime-architecture.md §3, step 7). A second tick function on
// the same actor, in TG_PostPhysics, carrying the work that must see the frame's FINAL positions:
// the `+use` look cursor traces against where a door actually ended up this frame, not where it
// was before its swept move and the pawn's. Two tick functions on one actor is the engine's own
// answer to work that straddles physics — splitting into two actors would reintroduce the
// ordering question tick groups solve.
USTRUCT()
struct FElysiumPostMoveTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

	AElysiumMapActor* Target = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread,
		const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
};

template <>
struct TStructOpsTypeTraits<FElysiumPostMoveTickFunction> : public TStructOpsTypeTraitsBase2<FElysiumPostMoveTickFunction>
{
	enum { WithCopy = false };
};

// One loaded VtMB map. The map's *look* — world and 3D-skybox geometry, materials, textures,
// static props, lights, fog — is baked offline into real .uasset content and a real .umap
// (tools/bake_map.py), which UElysiumMapSubsystem::Travel opens; this actor is spawned into that
// level and adopts its actors. What it still builds at load time is everything the bake cannot
// hold: brush collision from .hulls/.dispcol (the walkable surface — baked geometry carries none),
// the .ropes cables, the sky cubemap and backdrop, the Track-B entity substrate and every
// entity-driven body. Everything it builds is a component of (or outer'd to) this actor, so
// destroying it unloads that half. Spawned only by UElysiumMapSubsystem (deferred, MapName set
// before FinishSpawning); BeginPlay builds the map.
//
// It is also the substrate's engine side (11.2): it implements three of the four
// FElysiumWorldServices interfaces and hands the bundle to FElysiumEntityWorld at construction, so
// the plain-C++ half below it never casts back up here. IElysiumPresenter is the fourth and has no
// production implementation until 11.8 — the world's fade/sign/dialogue state is still polled by
// AElysiumHUD.
UCLASS()
class AElysiumMapActor : public AActor,
	public IElysiumEmbodiment,
	public IElysiumAudio,
	public IElysiumTravel
{
	GENERATED_BODY()

public:
	AElysiumMapActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void RegisterActorTickFunctions(bool bRegister) override;

	// S2 — the frame's gameplay pass (TG_PrePhysics, steps 2-4): advance the one clock, run the
	// substrate think-first, then the audio/scheme pass. Runs before physics and before the pawn's
	// move, so a mover's swept move for this frame is issued before anything is moved against it.
	virtual void Tick(float DeltaSeconds) override;

	// S2 — the frame's post-move pass (TG_PostPhysics, step 7), driven by PostMoveTickFunction.
	void PostMoveTick(float DeltaSeconds);

	// Step 7's tick function. Public so a test can read the declared frame order off the class.
	UPROPERTY()
	FElysiumPostMoveTickFunction PostMoveTickFunction;

	// Map name under FElysiumContentPaths::Root() (a folder holding <MapName>.obj).
	UPROPERTY(EditAnywhere, Category = "Elysium")
	FString MapName = TEXT("sp_tutorial_1");

	// B7 — the uniform scale a body built for this def takes: the 3D-skybox miniature's scale for
	// a sky-scope entity (its origin and hulls are already carried through the transform by the
	// def parser, but a mesh's own size is not a point), 1 for everything else.
	virtual float BodyScaleFor(const struct FElysiumEntityDef& Def) const override;

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

	// B3/8.5 — build one NPC skeletal body: load (cached per stem) out/npc/<Stem>.glb through
	// glTFRuntime and stand a movable USkeletalMeshComponent on this map actor at the given
	// transform, playing the standing idle its disposition selects (reference pose when nothing
	// resolves). The idle usually lives in a **shared animation bank**, not the NPC's own glb, and
	// is retargeted onto this skeleton by bone name — UElysiumNpcAnimSubsystem owns that resolution
	// and the session-lifetime bank cache. Returns the component, or null on a missing/failed glb or
	// empty stem. The FElysiumNpc leaf calls this from its Spawn() and registers the result with the
	// entity world for teardown. Meshes/anims are cached on this actor (freed on unload) so a model
	// shared by several NPCs (three Sabbat share shovelhead) loads once.
	virtual USkeletalMeshComponent* BuildNpcVisual(const FString& Stem, const FVector& Location,
		const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant) override;

	// Re-run the default-idle policy on a live body and crossfade to the result. The seam a
	// disposition change reaches animation through: 9.9's `SetDisposition` is 2,510 calls, 2,467
	// of them a .dlg line's action, so an NPC's stance follows the conversation.
	virtual bool RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Disposition, int32 IdleVariant) override;

	// Crossfade a live NPC body to a named clip, resolved through the manifest. Returns false when
	// the name resolves nothing. The seam `SetAnimation` / `SetGesture` / `m_iszPlay` use.
	// OutSeconds receives the clip's authored length — what a `scripted_sequence` schedules its
	// `OnEndSequence` off, since the action animation is what gives the beat its duration.
	virtual bool PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem, const FString& ClipName,
		bool bLoop, float* OutSeconds) override;

	// Retarget one named clip onto an already-built NPC model's skeleton, cached per (stem, clip).
	// The clip may live in the NPC's own glb or in any shared bank — the manifest says which, and
	// the bank is loaded once per session. Null when the stem has no body yet or the name resolves
	// nothing. This is the seam the script surface reaches animation through (`SetAnimation`,
	// `SetGesture`, `scripted_sequence.m_iszPlay`).
	UAnimSequence* ResolveNpcClip(const FString& Stem, const FString& ClipName);

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
	virtual UStaticMeshComponent* BuildPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) override;

	// 8.4 — build one physics-prop body: the same baked mesh BuildPropVisual stands, which for a
	// physics model carries VtMB's own convex collision (one shape per `.phy` ledge, from the
	// props/<Stem>.phys sidecar) and its authored mass on the body setup, under
	// CTF_UseSimpleAndComplex so a Chaos body can simulate against the simple shapes while the debug
	// pick still gets a per-poly face index. The returned component carries the PhysicsActor profile
	// with collision enabled but is NOT yet simulating — the FElysiumPhysProp leaf drives
	// SetSimulatePhysics / mass / the elysium.PhysicsProps gate. Registered for teardown
	// (RegisterPropBody) like a dynamic prop.
	virtual UStaticMeshComponent* BuildPhysPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) override;

	// Repaint a prop body to one of its model's alternate skin families (VtMB's `skin` keyfield /
	// `Skin` input -- a material remap over the model's own slots, applied instantly). Family 0 and
	// any family the model does not carry restore the authored materials, which is what Source does
	// with an out-of-range skin. Safe on any prop body from either build path. `elysium.PropSkins 0`
	// disables the whole pass.
	virtual void ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family) override;

	// --- IElysiumEmbodiment: the player's body ----------------------------------------------
	// All five resolve the pawn through this world's first player controller and report false /
	// no-op when there is none (the menu backdrop seats no pawn). They are the substrate's only
	// route to the player until 11.4 makes the player an entity, at which point they become
	// ordinary entity operations and these overrides shrink to the pawn's own transform.
	virtual bool GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const override;
	virtual bool GetPlayerOrigin(FVector& OutLocation, float& OutYaw) const override;
	virtual void TeleportPlayer(const FVector& FeetOrigin, float Yaw) override;
	virtual void DamagePlayer(float Amount) override;
	virtual FElysiumEntityHandle TraceUseCursor(const FVector& Start, const FVector& End) const override;

	// --- IElysiumAudio ----------------------------------------------------------------------
	// Voices forward to the GI-scoped UElysiumAudioSubsystem; the scheme calls drive this map's own
	// FElysiumSoundSchemeManager. All no-op safely with no subsystem / no scheme manager.
	virtual FElysiumAudioVoiceHandle PlayVoice(const FString& Rel, const FElysiumPlayParams& Params) override;
	virtual void StopVoice(FElysiumAudioVoiceHandle Handle, float FadeSeconds) override;
	virtual void SetVoiceVolume(FElysiumAudioVoiceHandle Handle, float Volume) override;
	virtual bool IsVoicePlaying(FElysiumAudioVoiceHandle Handle) const override;
	virtual void FadeInScheme(const FString& SchemeRel, const FVector& Anchor, float FadeSeconds) override;
	virtual void FadeOutScheme(const FString& SchemeRel, float FadeSeconds) override;
	virtual FString ActiveSchemeRel() const override;

	// --- IElysiumTravel ---------------------------------------------------------------------
	// Both forward to the GI-scoped UElysiumMapSubsystem, which owns when the travel happens.
	virtual void RequestLandmarkTravel(const FString& Map, const FString& Landmark,
		const FVector& Offset, float Yaw) override;
	virtual void ChangeMap(const FString& Map) override;

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
	// Service plumbing: the objects the four interfaces forward to. Each may be absent (no pawn on
	// the menu backdrop, no subsystem in a bare world), which is what makes every service call
	// safely no-op rather than conditional at the call site.
	class APawn* ResolvePlayerPawn() const;
	class UElysiumAudioSubsystem* GetAudioSubsystem() const;
	class UElysiumMapSubsystem* GetMapSubsystem() const;

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
	// B8/B8b — `<map>.env`: the sky name and orientation convention, and the map's TWO fog sets
	// (`worldspawn`'s for the world, `sky_camera`'s for the miniature). Kept past load so
	// `elysium.Fog` can re-stamp the primitives live.
	FElysiumEnvDef EnvDef;
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

	// One MID per unique baked material, standing in front of the level's own instances so the
	// look-tuning cvars reach real surfaces. The bake authors MaterialInstanceConstants; a
	// constant has no runtime setter, so without this every elysium.* material knob is dead on
	// the path that actually renders. Keyed by the baked material so a material shared by 40
	// components makes one MID, not 40 — the same one-MID-per-material shape the runtime-built
	// path had. Populated by ApplyMaterialOverrides at adopt; dropped with the actor on unload.
	UPROPERTY() TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInstanceDynamic>> MaterialOverrides;

	// Ropes (8.7): one Verlet UCableComponent per <map>.ropes segment (an overhead cable), kept
	// alive for the map's lifetime (freed on unload). MIDs off M_World_Opaque bound to the decoded
	// RopeMaterial texture; the cable's fixed endpoints and rest length come straight from the sidecar.
	UPROPERTY() TArray<TObjectPtr<class UCableComponent>> Ropes;

	// B3/8.5 NPC skeletal bodies: per-stem mesh cache and a per-(stem, clip) animation cache,
	// GC-rooted here so a model shared by several NPCs loads once and survives until unload. The
	// USkeletalMeshComponents themselves are components of this actor (rooted via
	// AddInstanceComponent), freed with it.
	//
	// The animation cache is keyed `<stem>|<clip>` and lives HERE rather than on the GI-scoped
	// UElysiumNpcAnimSubsystem, because glTFRuntime binds every UAnimSequence it builds to one
	// USkeletalMesh's USkeleton — and meshes are per-map-epoch. The subsystem caches what is
	// skeleton-independent: the parsed bank glbs and the clip vocabularies. An entry may be null
	// (nothing resolved → reference pose); it is still cached, so a miss is not retried per NPC.
	UPROPERTY() TMap<FString, TObjectPtr<USkeletalMesh>> NpcMeshCache;
	UPROPERTY() TMap<FString, TObjectPtr<UAnimSequence>> NpcAnimCache;

	// The NPC's own parsed glb, kept for the epoch so a clip it owns itself (its dialogue anims)
	// can still be retargeted after the mesh is cached — the bank path does not go through it.
	UPROPERTY() TMap<FString, TObjectPtr<UglTFRuntimeAsset>> NpcAssetCache;

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
	// Stand a UMaterialInstanceDynamic in front of every unique baked material on the world,
	// sky and prop components, so the look-tuning cvars can reach them (a baked
	// MaterialInstanceConstant has no runtime setter). Builds the MID set on the first call and
	// re-applies the current cvar values on every call, so a cvar callback is just a re-run.
	// No-op under elysium.MaterialOverrides 0, which leaves the baked values exactly as authored.
	void ApplyMaterialOverrides();
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
	// B8b — stamp each adopted primitive with the fog set that owns it: `worldspawn`'s on the
	// world and its props, the `sky_camera`'s on the 3D-skybox miniature. Custom primitive data,
	// because the two share screen depth and a deferred fog pass cannot scope by anything else
	// (ElysiumFog.h). Re-run by elysium.Fog, which stamps zeros instead.
	void ApplySceneFog();
	bool ReadSpawn(FVector& OutLocation, float& OutYaw) const;
	// P4.6 — if this load is a landmark transition (the map subsystem has a queued landmark spawn),
	// override the info_player_start placement: resolve the destination `info_landmark` in the just-
	// built entity world and seat the player at landmark origin + the carried offset. Fires the
	// landmark's OnEnterMapHere. No-op (keeps the .spawn placement) for a plain load or a missing
	// landmark. Called by LoadMap after the entity world is built.
	void ResolveLandmarkSpawn();

	// S2 — declare the frame order rather than observe it: the gameplay tick runs after the player
	// controller's input sample (step 1), and the pawn's movement component runs after the gameplay
	// tick (step 5), so the pawn is moved against the positions this frame's thinks produced. Both
	// ends appear later than BeginPlay (no controller yet on a fresh world, no pawn at all on the
	// menu backdrop), so this re-checks each gameplay tick until each end is bound, and rebinds if
	// the pawn is replaced.
	void EnsureTickPrerequisites();
	TWeakObjectPtr<class APlayerController> PrereqController;
	TWeakObjectPtr<class UPawnMovementComponent> PrereqMovement;

	// The player pawn may not exist yet in BeginPlay, so the teleport is deferred to Tick;
	// the pawn is then held frozen until the async collision cook yields ground beneath it.
	bool bSpawnPending = false;
	bool bSpawnPlaced = false;
	bool bSpawnDone = false;
	float SpawnHoldSeconds = 0.f;
	FVector PendingSpawnLoc = FVector::ZeroVector;
	float PendingSpawnYaw = 0.f;
};
