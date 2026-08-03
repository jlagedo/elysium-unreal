#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/PimplPtr.h"
#include "ElysiumEnvironment.h"    // FElysiumSkyDef — a plain by-value member
#include "ElysiumWorldServices.h"  // the four interfaces this actor implements
#include "ElysiumMapActor.generated.h"

class FElysiumEntityWorld;
class FElysiumSoundSchemeManager;
class UElysiumEntityBodies;
class UElysiumMapCollision;
class UElysiumMapVisuals;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UMaterialParameterCollection;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UNiagaraSystem;
class UTexture2D;
class ANavMeshBoundsVolume;
class AElysiumNpcBody;
class AElysiumMapActor;

// Elysium's map lifecycle, distinct from Unreal's package/actor lifecycle. BeginPlay only starts
// Building; authored gameplay is admitted exactly once at the Activating -> Active transaction.
enum class EElysiumMapRuntimePhase : uint8
{
	Building,
	WaitingForPrerequisites,
	Activating,
	Active,
	Failed,
};

enum class EElysiumMapReadinessResult : uint8
{
	Waiting,
	Ready,
	Failed,
};

// Pure snapshot of the independently completing activation prerequisites. The map actor builds
// one from current engine state each poll; keeping the decision engine-neutral makes completion
// order, backdrop rules and fail-closed timeout behaviour directly testable.
struct FElysiumMapRuntimePrerequisites
{
	static constexpr double WatchdogSeconds = 8.0;

	bool bConstructionComplete = false;
	bool bEntityWorldReady = false;
	bool bAudioCatalogReady = true;
	bool bMenuBackdrop = false;
	bool bCollisionReady = false;   // Ready or intentionally Disabled
	bool bCollisionFailed = false;
	bool bNavigationRequired = false;
	bool bNavigationReady = false;
	bool bNavigationFailed = false;
	bool bSpawnTransformReady = false;
	bool bPlayerEntityReady = false;
	bool bPossessedPawnReady = false;
	bool bPlayerBodyReady = false;
	bool bFinalPlacementReady = false;
	bool bTickPrerequisitesReady = false;

	FString Missing() const;
	EElysiumMapReadinessResult Evaluate(double WaitSeconds, FString& OutFailure) const;
};

const TCHAR* ElysiumMapRuntimePhaseName(EElysiumMapRuntimePhase Phase);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnElysiumMapRuntimeReady, AElysiumMapActor*);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnElysiumMapRuntimeFailed, AElysiumMapActor*, const FString&);

// S2 — the map's pre-move tick (runtime-architecture.md §3, steps 2-3). The first of the actor's
// four tick functions, in TG_PrePhysics, carrying everything that must be settled BEFORE the pawn
// moves: the frame order's own wiring, the one clock advance, and the player entity's own think.
// Retail runs the whole player move out of the `clc_move` drain, ahead of `GameFrame`, with the
// player's think inside it — so the clock has to be at this frame's `now` and the body has to be
// placed and held before the mover component gets its turn.
USTRUCT()
struct FElysiumPreMoveTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

	AElysiumMapActor* Target = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread,
		const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
};

template <>
struct TStructOpsTypeTraits<FElysiumPreMoveTickFunction> : public TStructOpsTypeTraitsBase2<FElysiumPreMoveTickFunction>
{
	enum { WithCopy = false };
};

// S2 — GameFrame after every player/NPC movement tick (runtime-architecture.md §3, steps 5-6).
// This cannot be AElysiumMapActor::PrimaryActorTick: CharacterMovement automatically depends on
// the primary tick of the actor owning its floor, and the runtime world collision is map-owned.
// A separate tick can depend on those movement ticks without forming the reverse edge.
USTRUCT()
struct FElysiumGameplayTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

	AElysiumMapActor* Target = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread,
		const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
};

template <>
struct TStructOpsTypeTraits<FElysiumGameplayTickFunction> : public TStructOpsTypeTraitsBase2<FElysiumGameplayTickFunction>
{
	enum { WithCopy = false };
};

// S2 — the map's post-move tick (runtime-architecture.md §3, step 8). A fourth tick function on
// the same actor, in TG_PostPhysics, carrying the work that must see the frame's FINAL positions:
// the `+use` look cursor traces against where a door actually ended up this frame, not where it
// was before its swept move and the pawn's. Four tick functions on one actor is the engine's own
// answer to work that straddles physics — splitting into separate actors would reintroduce the
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
// (pipeline/unreal/bake_map.py), which UElysiumMapSubsystem::Travel opens; this actor is spawned into that
// level. Spawned only by UElysiumMapSubsystem (deferred, MapName set before FinishSpawning);
// BeginPlay builds the map.
//
// **What this actor is** is the map's ORCHESTRATOR and the substrate's engine side (11.2): it owns
// the load order and the ordered frame passes, seats the player, holds the entity world / scheme manager
// / camera director, and implements three of the four FElysiumWorldServices interfaces so the
// plain-C++ half below it never casts back up here. The fourth, IElysiumPresenter, is the
// world-scoped UElysiumPresentationSubsystem (11.8), which this actor looks up and threads in.
//
// **What it is not** is the map's renderer. Three components carry the work that has nothing to do
// with entity logic, and the actor holds no piece of their state:
//
//   UElysiumMapVisuals    the LOOK — adopting the baked level, the material-override MIDs, the sky
//                         cube and backdrop, the sky light's level, the two fog sets, the Lumen
//                         knobs on the map's PPV, the light rig, the cables, the visibility A/Bs
//   UElysiumMapCollision  the WALKABLE SURFACE — `<map>.hulls` convex + `<map>.dispcol` trimesh,
//                         which is the only world collider (baked geometry carries none)
//   UElysiumEntityBodies  the BODY FACTORY behind IElysiumEmbodiment's mesh half — NPC skeletal and
//                         prop static bodies, their skins, and the per-map asset caches
//
// Everything any of them builds is a component of (or outer'd to) this actor, so destroying it
// unloads the whole map.
UCLASS()
class AElysiumMapActor : public AActor,
	public IElysiumEmbodiment,
	public IElysiumAudio,
	public IElysiumTravel,
	public IElysiumWeather
{
	GENERATED_BODY()

public:
	AElysiumMapActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void RegisterActorTickFunctions(bool bRegister) override;

	// S2 — the frame's pre-move pass (TG_PrePhysics, steps 2-3), driven by PreMoveTickFunction:
	// wire the frame order, advance the one clock, place and hold a freshly spawned pawn, then run
	// the player entity's own think. Everything here happens before the pawn's move, which is where
	// retail puts it.
	void PreMoveTick(float DeltaSeconds);

	// The native actor tick is the map-owned collision's movement-base barrier. It stays between the
	// player move and NPC moves; GameFrame itself runs from GameplayTickFunction after both.
	virtual void Tick(float DeltaSeconds) override;

	// S2 — the frame's gameplay pass (TG_PrePhysics, steps 5-6): substrate think-first, then the
	// audio/scheme pass, after every movement component has produced this frame's final feet/yaw.
	void GameplayTick(float DeltaSeconds);

	// S2 — the frame's post-move pass (TG_PostPhysics, step 8), driven by PostMoveTickFunction.
	void PostMoveTick(float DeltaSeconds);

	// Steps 2-3's tick function. Public so a test can read the declared frame order off the class.
	UPROPERTY()
	FElysiumPreMoveTickFunction PreMoveTickFunction;

	// Steps 5-6's tick function. Public so a test can read the declared frame order off the class.
	UPROPERTY()
	FElysiumGameplayTickFunction GameplayTickFunction;

	// Step 8's tick function. Public so a test can read the declared frame order off the class.
	UPROPERTY()
	FElysiumPostMoveTickFunction PostMoveTickFunction;

	// Map name under FElysiumContentPaths::Root() (a folder holding <MapName>.obj).
	UPROPERTY(EditAnywhere, Category = "Elysium")
	FString MapName = TEXT("sp_tutorial_1");

	// --- The three halves this actor is not ---------------------------------------------------
	// Never null after construction. Anything asking the map what it LOOKS like (the Lights and
	// Maps Cog windows, the light probe, elysium.togglesky) goes through GetVisuals(); anything
	// asking what it is SOLID against goes through GetCollision(). The actor deliberately carries
	// no forwarders for either — a façade over these would rebuild the god object the split removed.
	UElysiumMapVisuals* GetVisuals() const { return Visuals; }
	UElysiumMapCollision* GetCollision() const { return Collision; }
	UElysiumEntityBodies* GetBodies() const { return Bodies; }

	// B7 — the uniform scale a body built for this def takes: the 3D-skybox miniature's scale for
	// a sky-scope entity (its origin and hulls are already carried through the transform by the
	// def parser, but a mesh's own size is not a point), 1 for everything else.
	virtual float BodyScaleFor(const struct FElysiumEntityDef& Def) const override;

	// True only after the activation transaction has completed. The headless profiler waits on
	// this before capturing, so it cannot observe a partially-built runtime world.
	bool IsSpawnDone() const { return bSpawnDone; }
	EElysiumMapRuntimePhase GetRuntimePhase() const { return RuntimePhase; }
	bool IsRuntimeActive() const { return RuntimePhase == EElysiumMapRuntimePhase::Active; }
	double GetRuntimeWaitSeconds() const;
	FString GetMissingRuntimePrerequisites() const;
	const FString& GetRuntimeFailureReason() const { return RuntimeFailureReason; }
	FOnElysiumMapRuntimeReady& OnRuntimeReady() { return RuntimeReady; }
	FOnElysiumMapRuntimeFailed& OnRuntimeFailed() { return RuntimeFailed; }

	// The live Track-B entity world (P1.4), or null if the map has no `.ents`. Owned by this
	// actor, so it dies on map unload. The `elysium.world*` verbs reach it through here.
	FElysiumEntityWorld* GetEntityWorld() const { return EntityWorld.Get(); }

	// The P6.3 SoundScheme playback manager (ambient bed + music state machine + random scheduler)
	// for this map, or null if the map has no entity world. Owned by this actor (dies on unload); the
	// ambient_soundscheme entities and the Cog Sound Schemes window reach it through here.
	FElysiumSoundSchemeManager* GetSchemeManager() const { return SchemeManager.Get(); }

	// --- IElysiumEmbodiment: entity bodies ----------------------------------------------------
	// All five forward to UElysiumEntityBodies, which owns the meshes, the animation resolution and
	// the per-map asset caches. They stay declared here because this actor is the substrate's one
	// engine seam (ElysiumWorldServices.h) — the substrate never learns that a body factory exists.
	virtual USkeletalMeshComponent* BuildNpcVisual(const FString& Stem, const FVector& Location,
		const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant) override;
	virtual IElysiumNpcMotor* BuildNpcMotor(USkeletalMeshComponent* Body,
		const FVector& FeetOrigin, float YawDegrees) override;
	virtual void DestroyNpcMotor(IElysiumNpcMotor* Motor) override;
	virtual bool RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Disposition, int32 IdleVariant) override;
	virtual bool PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem, const FString& ClipName,
		bool bLoop, float* OutSeconds) override;
	virtual bool PlayNpcActivity(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Activity, int32 Variant, bool bLoop, float* OutSeconds) override;
	virtual bool PlayCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName,
		bool bLoop, float* OutSeconds) override;
	virtual bool SeekCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds) override;
	virtual void StopCinematicClip(USkeletalMeshComponent* Body) override;
	virtual int32 SetFlexControllers(USkeletalMeshComponent* Body,
		TArrayView<const FElysiumFlexWrite> Writes, TArray<FString>* OutMissing) override;
	virtual bool SetMouthOpen(USkeletalMeshComponent* Body, float Open) override;
	virtual FString AnimatedPropStemForModel(const FString& ModelPath) const override;
	virtual USkeletalMeshComponent* BuildAnimatedPropVisual(const FString& Stem,
		const FVector& Location, const FQuat& Rotation, float UniformScale) override;
	virtual bool PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& ClipName, bool bLoop, float* OutSeconds) override;
	virtual void ApplyAnimatedPropSkin(USkeletalMeshComponent* Comp,
		const FString& StaticStem, int32 Family) override;
	virtual UStaticMeshComponent* BuildBrushVisual(const FString& Stem,
		USceneComponent* ParentBody, float UniformScale, bool bSky) override;
	virtual UStaticMeshComponent* BuildPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) override;
	virtual UStaticMeshComponent* BuildPhysPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) override;
	virtual void ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family) override;
	virtual USkeletalMeshComponent* BuildPlayerVisual(const FString& Stem,
		const FString& Disposition, int32 IdleVariant) override;
	virtual void ClearPlayerVisual() override;

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
	// 11.7 — the scripted-shot channel. The director resolves a `vdata/camerashots/` file against this
	// map's entities and bodies and hands the values to the pawn's camera; the camera itself never
	// learns what an entity is.
	virtual int32 PushCameraShot(const FString& ShotFile, const FElysiumEntityHandle& Subject) override;
	virtual int32 PushCameraShotValue(const FElysiumCameraShot& Shot) override;
	virtual bool UpdateCameraShotValue(int32 ShotId, const FElysiumCameraShot& Shot) override;
	virtual bool PopCameraShot(int32 ShotId, float BlendOutSeconds = -1.0f) override;

	// --- IElysiumAudio ----------------------------------------------------------------------
	// Voices forward to the GI-scoped UElysiumAudioSubsystem; the scheme calls drive this map's own
	// FElysiumSoundSchemeManager. All no-op safely with no subsystem / no scheme manager.
	virtual FElysiumVoiceHandle Submit(FElysiumAudioRequest Request) override;
	virtual void Prefetch(const FElysiumAudioSource& Source) override;
	virtual void PauseVoice(FElysiumVoiceHandle Handle, bool bPaused) override;
	virtual void SeekVoice(FElysiumVoiceHandle Handle, float MediaOffsetSeconds) override;
	virtual void SetVoicePitch(FElysiumVoiceHandle Handle, float Pitch) override;
	virtual void CancelAudioOwner(FElysiumAudioOwner AudioOwner, float FadeSeconds = 0.f) override;
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

	// --- IElysiumWeather --------------------------------------------------------------------
	virtual void ApplyWetness(const FElysiumWeatherTransition& Transition) override;
	virtual void ApplyEmitter(const FElysiumWeatherEmitterState& Emitter) override;
	virtual void RemoveEmitter(const FElysiumEntityHandle& Entity) override;
	void FireWeatherTimer(bool bRainOn);
	FString GetWeatherDebugSummary() const;
	const FElysiumWeatherTransition& GetWetnessTransition() const { return WetnessTransition; }
	float GetPresentedWetness() const { return PresentedWetness; }
	float GetPresentedWetnessScale() const { return PresentedWetnessScale; }
	bool IsEnvironmentWetnessOverridden() const { return bEnvironmentWetnessOverride; }

	// Live stats for the debug overlay, filled by LoadMap. The look and collider counts live on
	// UElysiumMapVisuals / UElysiumMapCollision alongside the things they count.
	FString LoadedMap;
	// Entity substrate: number of `.ents` records the world spawned (0 if the map has no sidecar),
	// and how many of them got a P1.5 brush body (convex collision / trigger overlap volume).
	int32 EntityCount = 0;
	int32 BrushBodyCount = 0;

	// P4.6 — the `info_landmark` this map load entered through (a landmark transition / direct
	// landmark Travel), or empty for a plain info_player_start spawn. Shown in the Maps Cog window.
	FString EntryLandmark;

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
	UPROPERTY() TObjectPtr<UElysiumMapVisuals> Visuals;
	UPROPERTY() TObjectPtr<UElysiumMapCollision> Collision;
	UPROPERTY() TObjectPtr<UElysiumEntityBodies> Bodies;
	UPROPERTY(Transient) TObjectPtr<ANavMeshBoundsVolume> NavigationBounds;
	UPROPERTY(Transient) TArray<TObjectPtr<AElysiumNpcBody>> NpcMotors;
	UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> EnvironmentParameters;
	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> RainSystem;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> RainHeightTexture;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> RainMaterial;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> RainLayerMaterials;
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UNiagaraComponent>> RainComponents;
	TMap<int32, FElysiumWeatherEmitterState> RainEmitterStates;
	FElysiumWeatherTransition WetnessTransition;
	float PresentedWetness = 0.0f;
	float PresentedWetnessScale = 1.0f;
	bool bEnvironmentWetnessOverride = false;

	// B7 — the 3D-skybox miniature's placement (`<map>.sky`), or the identity on the 65 maps
	// with no `sky_camera`. Read at map load and used twice: the def parser carries sky-scope
	// entities through it, and a miniature body takes its uniform mesh scale from it.
	FElysiumSkyDef SkyDef;

	// The Track-B entity substrate for this map (P1.4): parsed defs, live entities, the event
	// queue, and the debug sinks. A plain C++ object (no UObject) held type-erased so the header
	// needs only a forward declaration; destroyed with the actor on map unload.
	TPimplPtr<FElysiumEntityWorld> EntityWorld;

	// The P6.3 SoundScheme manager (plain C++, owned here). Constructed alongside EntityWorld so the
	// ambient_soundscheme entities can reach it during their spawn pass; ticked from Tick with the
	// player location; its voices are stopped on unload (EndPlay).
	TPimplPtr<FElysiumSoundSchemeManager> SchemeManager;
	// A start_enabled ambient_soundscheme asks for its bed during the construction Spawn pass. Keep
	// that request as data until the activation transaction reaches its audio step.
	bool bHasDeferredSchemeFadeIn = false;
	FString DeferredSchemeRel;
	FVector DeferredSchemeAnchor = FVector::ZeroVector;
	float DeferredSchemeFadeSeconds = 0.0f;

	// 11.7 — the live scripted camera shots and their entity bindings. Owned here because resolving a
	// shot's anchors needs the entity world and the bodies standing in it; refreshed in the post-move
	// pass so a shot following an NPC sees where that NPC ended the frame.
	TPimplPtr<class FElysiumCameraDirector> CameraDirector;
	// The pawn's camera, or null (a backdrop map seats no pawn).
	class UElysiumCameraComponent* PlayerCamera() const;

	void LoadMap();
	bool ReadSpawn(FVector& OutLocation, float& OutYaw) const;
	// P4.6 — if this load is a landmark transition (the map subsystem has a queued landmark spawn),
	// override the info_player_start placement: resolve the destination `info_landmark` in the just-
	// built entity world and seat the player at landmark origin + the carried offset. Fires the
	// landmark's OnEnterMapHere. No-op (keeps the .spawn placement) for a plain load or a missing
	// landmark. Called by LoadMap after the entity world is built.
	void ResolveLandmarkSpawn();
	// 11.9 — if this load is a save restore, the World block's absolute player pose outranks both
	// info_player_start and a landmark offset. Run right after ResolveLandmarkSpawn.
	void ResolveRestorePlacement();

	// S2 — declare the frame order rather than observe it. Three edges are wired here, all of them
	// late-binding: pre-move follows the player controller's input sample (step 1), player movement
	// follows pre-move (step 4), and the map-floor barrier follows player movement so this frame's
	// gameplay pass ultimately sees where the pawn actually ended up. Each end appears after BeginPlay
	// (no controller yet on a fresh world, no pawn at all on the menu backdrop), so this re-checks
	// each pre-move tick until all are bound, and rebinds if the pawn is replaced. The unconditional
	// pre-move -> floor barrier -> gameplay -> post-move chain is wired once at registration because
	// those functions always exist and must hold on a map that never seats a pawn.
	void EnsureTickPrerequisites();
	TWeakObjectPtr<class APlayerController> PrereqController;
	TWeakObjectPtr<class UPawnMovementComponent> PrereqMovement;

	// Lifecycle polling is the only work admitted before Active. It places/freezes the pawn at its
	// final transform, observes asynchronous collision completion, and opens the atomic transaction
	// only when every independent prerequisite is satisfied.
	void PollRuntimeActivation();
	void EnsureRuntimeNavigation();
	bool IsRuntimeNavigationReady() const;
	FElysiumMapRuntimePrerequisites CollectRuntimePrerequisites() const;
	void ActivateRuntime();
	void FailRuntime(const FString& Reason);
	void TickAudio(float DeltaSeconds);
	void TickWeatherPresentation();
	void ApplyWeatherTuning();
	// UE can retain an already-overlapping pair across a non-swept/zero-distance teleport without
	// emitting a fresh begin edge. Refresh the pawn's overlap cache, then reconcile every runtime
	// brush currently containing it into the deduplicating entity touch bus.
	void ReconcilePlayerBrushTouches(APawn* Pawn);

	// The player pawn may not exist yet in BeginPlay, so final placement is performed by the
	// readiness poll. It stays frozen through the activation transaction.
	bool bSpawnPending = false;
	bool bSpawnPlaced = false;
	bool bSpawnDone = false;
	FVector PendingSpawnLoc = FVector::ZeroVector;
	float PendingSpawnYaw = 0.f;

	EElysiumMapRuntimePhase RuntimePhase = EElysiumMapRuntimePhase::Building;
	bool bRuntimeConstructionComplete = false;
	bool bMenuBackdrop = false;
	bool bNavigationBuildRequested = false;
	bool bNavigationBuildFailed = false;
	// Set in EndPlay. After it, a DestroyNpcMotor call is the entity world's own teardown running
	// against actors the engine has already destroyed, and must do nothing.
	bool bMotorsRetired = false;
	double RuntimeWaitStartSeconds = 0.0;
	double RuntimeWaitDurationSeconds = 0.0;
	uint64 AudioMapEpoch = 0;
	FString RuntimeFailureReason;
	FOnElysiumMapRuntimeReady RuntimeReady;
	FOnElysiumMapRuntimeFailed RuntimeFailed;
};
