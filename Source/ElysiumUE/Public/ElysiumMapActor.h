#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/PimplPtr.h"
#include "ElysiumAnimationIntent.h" // FElysiumAnimationSelection — returned by value reference
#include "ElysiumClipMovement.h"   // FElysiumBaseClipCycle / FElysiumClipMovementPath — by value
#include "ElysiumEnvironment.h"    // FElysiumSkyDef — a plain by-value member
#include "ElysiumWorldServices.h"  // the four interfaces this actor implements
#include "ElysiumMapActor.generated.h"

class FElysiumEntityWorld;
class FElysiumSoundSchemeManager;
class UElysiumEntityBodies;
class UElysiumMapCollision;
class UElysiumMapVisuals;
class UElysiumMovementComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialParameterCollection;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UNiagaraSystem;
class UTexture2D;
class ANavMeshBoundsVolume;
class APawn;
class AElysiumNpcBody;
class AElysiumMapActor;
class AElysiumEffectActor;
class UElysiumParticleTrees;
struct FElysiumEffectAttachment;

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
	bool bAnimationPreloadReady = false;
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

// The map's pre-move tick (`docs/architecture/runtime-architecture.md` §3, steps 2-3). The first of the actor's
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

// GameFrame after every player/NPC movement tick (`docs/architecture/runtime-architecture.md` §3, steps 5-6).
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

// The map's post-move tick (`docs/architecture/runtime-architecture.md` §3, step 8). A fourth tick function on
// the same actor, in TG_PostPhysics, carrying the work that must see the frame's FINAL positions:
// the `+use` camera/body query sees where a door actually ended up this frame, not where it was
// before its swept move and the pawn's. Four tick functions on one actor is the engine's own
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
// **What this actor is** is the map's ORCHESTRATOR and the substrate's engine side: it owns
// the load order and the ordered frame passes, seats the player, holds the entity world / scheme manager
// / camera director, and implements three of the four FElysiumWorldServices interfaces so the
// plain-C++ half below it never casts back up here. The fourth, IElysiumPresenter, is the
// world-scoped UElysiumPresentationSubsystem, which this actor looks up and threads in.
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

	// The frame's pre-move pass (TG_PrePhysics, steps 2-3), driven by PreMoveTickFunction:
	// wire the frame order, advance the one clock, place and hold a freshly spawned pawn, then run
	// the player entity's own think. Everything here happens before the pawn's move, which is where
	// retail puts it.
	void PreMoveTick(float DeltaSeconds);

	// The native actor tick is the map-owned collision's movement-base barrier. It stays between the
	// player move and NPC moves; GameFrame itself runs from GameplayTickFunction after both.
	virtual void Tick(float DeltaSeconds) override;

	// The frame's gameplay pass (TG_PrePhysics, steps 5-6): substrate think-first, then the
	// audio/scheme pass, after every movement component has produced this frame's final feet/yaw.
	void GameplayTick(float DeltaSeconds);

	// The frame's post-move pass (TG_PostPhysics, step 8), driven by PostMoveTickFunction.
	void PostMoveTick(float DeltaSeconds);

	// Run the gaze cascade for every drawn character and publish each answer to its body.
	// Called from PostMoveTick, where the head-bone pose it measures against is settled.
	void TickGaze(float DeltaSeconds);

	// Steps 2-3's tick function. Public so a test can read the declared frame order off the class.
	UPROPERTY()
	FElysiumPreMoveTickFunction PreMoveTickFunction;

	// Steps 5-6's tick function. Public so a test can read the declared frame order off the class.
	UPROPERTY()
	FElysiumGameplayTickFunction GameplayTickFunction;

	// Step 8's tick function. Public so a test can read the declared frame order off the class.
	UPROPERTY()
	FElysiumPostMoveTickFunction PostMoveTickFunction;

	// Map name under FElysiumContentPaths::Root() (a folder holding <MapName>.obj). Empty on a
	// stage world, which has no map to name.
	UPROPERTY(EditAnywhere, Category = "Elysium")
	FString MapName = TEXT("sp_tutorial_1");

	// The green room's world: this actor carries the body factory, the camera director and the
	// substrate seam over an empty level, with no VtMB map behind any of them. Set by
	// UElysiumMapSubsystem::EnterGreenRoom before FinishSpawning, same as MapName.
	UPROPERTY()
	bool bStageOnly = false;

	bool IsStageOnly() const { return bStageOnly; }

	// The three halves this actor is not.
	// Never null after construction. Anything asking the map what it LOOKS like (the Lights and
	// Maps Cog windows, the light probe, elysium.togglesky) goes through GetVisuals(); anything
	// asking what it is SOLID against goes through GetCollision(). The actor deliberately carries
	// no forwarders for either — a façade over these would rebuild a god object.
	UElysiumMapVisuals* GetVisuals() const { return Visuals; }
	UElysiumMapCollision* GetCollision() const { return Collision; }
	UElysiumEntityBodies* GetBodies() const { return Bodies; }

	// The player body's frame selection: which activity was classified, which label and owning
	// bank it resolved through, and why if it did not. Never null: a body with no vocabulary answers a
	// default record whose outcome says exactly that, so a reader has nothing to test.
	const FElysiumAnimationSelection& GetPlayerAnimSelection() const;
	// The sample that record was classified from, with the driver's own filtered pose parameter on
	// it. Published as one pair with the record above and read as one: a reader that re-samples the
	// mover instead is describing a frame the record never saw.
	const FElysiumLocomotionSample& GetPlayerAnimSample() const;

	// The player half of the channel arbitration slot: claim a channel of the player body's
	// driver, or give a claim back. Same handle contract as `AElysiumNpcBody`'s pair; the driver is
	// built on demand so a claim ahead of the first player anim pass is not dropped.
	uint32 SubmitPlayerAnimRequest(const struct FElysiumAnimationRequest& Request);
	// Which overlay slot a granted `UpperBody` handle landed in, or `INDEX_NONE`. The stack allocates
	// the slot, so the arm seam writes the pins of THAT slot rather than a fixed one.
	int32 PlayerOverlaySlotForHandle(uint32 Handle) const;
	bool ReleasePlayerAnimRequest(uint32 Handle);
	// Every standing claim on the player driver at once. A driver that was never built holds
	// nothing, so this does not build one.
	int32 ReleaseAllPlayerAnimRequests();
	// The claim standing on one channel of the player driver, or null. Read-only, and it does
	// not build a driver: a body that has never been claimed on holds nothing.
	const struct FElysiumAnimationRequest* ActivePlayerAnimRequest(
		EElysiumAnimChannel Channel) const;
	// Whether this component is the player pawn's own visual — the body whose channel claims route
	// to the player driver rather than to an NPC motor's.
	bool IsPlayerVisual(const USkeletalMeshComponent* Body) const;

	// The uniform scale a body built for this def takes: the 3D-skybox miniature's scale for
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

	// The live Track-B entity world, or null if the map has no `.ents`. Owned by this
	// actor, so it dies on map unload. The `elysium.world*` verbs reach it through here.
	FElysiumEntityWorld* GetEntityWorld() const { return EntityWorld.Get(); }
	// Engine overlap ingress from UElysiumBrushComponent. Runtime teleports suppress the callbacks
	// Unreal emits inside SetActorLocation and replace them with one post-movement containment diff.
	void RouteBrushTouch(const FElysiumEntityHandle& Brush,
		const FElysiumEntityHandle& Activator, bool bBegin);

	// The SoundScheme playback manager (ambient bed + music state machine + random scheduler)
	// for this map, or null if the map has no entity world. Owned by this actor (dies on unload); the
	// ambient_soundscheme entities and the Cog Sound Schemes window reach it through here.
	FElysiumSoundSchemeManager* GetSchemeManager() const { return SchemeManager.Get(); }

	// IElysiumEmbodiment: entity bodies.
	// Every override in this block forwards to UElysiumEntityBodies, which owns the meshes, the
	// animation resolution and the per-map asset caches. They stay declared here because this actor
	// is the substrate's one engine seam (ElysiumWorldServices.h) — the substrate never learns that
	// a body factory exists.
	virtual USkeletalMeshComponent* BuildNpcVisual(const FString& Stem, const FVector& Location,
		const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant) override;
	virtual IElysiumNpcMotor* BuildNpcMotor(USkeletalMeshComponent* Body,
		const FElysiumEntityHandle& EntityOwner, const FVector& FeetOrigin, float YawDegrees,
		const FString& Stem, int32 Variant) override;
	virtual void DestroyNpcMotor(IElysiumNpcMotor* Motor) override;
	virtual bool RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Disposition, int32 DispositionLevel, int32 IdleVariant) override;
	virtual void UpdateNpcDisposition(USkeletalMeshComponent* Body, const FString& Disposition,
		int32 DispositionLevel) override;
	virtual bool ResolveStanceClips(const FString& Stem, const FString& AnimName,
		FElysiumStanceClips& OutClips) override;
	virtual bool ResolveDisposition(const FString& Disposition, int32 DispositionLevel,
		FElysiumDisposition& OutRow) override;
	virtual bool IsNpcBodyVisible(USkeletalMeshComponent* Body) override;
	virtual bool PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FElysiumClipSegment& Segment, float* OutSeconds) override;
	virtual void ReleaseNpcSegment(USkeletalMeshComponent* Body) override;
	virtual bool PreloadNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& ClipName) override;
	virtual bool PreloadNpcClipForModel(const FString& Stem, bool bPlayerMaterial,
		const FString& ClipName) override;
	virtual bool ResolveNpcActivityClip(const FElysiumActivityClipRequest& Request,
		FElysiumActivityClip& Out) override;
	virtual bool PlayNpcOneShot(USkeletalMeshComponent* Body,
		const FElysiumOneShotClipRequest& Request, float* OutSeconds) override;
	virtual void ReleaseNpcReaction(USkeletalMeshComponent* Body) override;
	virtual EElysiumHeldReactionState QueryNpcReactionHold(
		USkeletalMeshComponent* Body) const override;
	virtual void ReleaseBodyAnimClaims(USkeletalMeshComponent* Body) override;
	virtual bool StartBodyRagdoll(USkeletalMeshComponent* Body) override;
	virtual void HoldBodyFinalPose(USkeletalMeshComponent* Body) override;
	virtual bool ResolveNpcSequenceClip(const FString& Stem, const FString& ClipName,
		EElysiumAnimBodyKind BodyKind, FString& OutAnimName,
		float& OutGroundSpeedCmPerSecond) override;
	virtual bool HasNpcClip(const FString& Stem, const FString& ClipName) override;
	virtual FString NpcClipBlockedReaction(const FString& Stem, const FString& ClipLabel) override;
	virtual const TArray<FElysiumSwingRecord>* NpcClipSwings(const FString& Stem,
		const FString& ClipLabel) override;
	virtual const FElysiumComboChain* NpcClipCombo(const FString& Stem,
		const FString& ClipLabel) override;
	virtual FString NpcClipOwner(const FString& Stem, const FString& ClipLabel) override;
	virtual bool GetBodyBoneTransform(USkeletalMeshComponent* Body, const FString& BoneName,
		FTransform& OutWorld) const override;
	virtual void QuerySwingContacts(const FElysiumSwingSweep& Sweep,
		TArray<FElysiumEntityHandle>& OutHits) const override;
	virtual bool GetBodyClipPhase(USkeletalMeshComponent* Body, EElysiumAnimChannel Channel,
		FElysiumClipPhase& Out) override;
	virtual const TArray<FElysiumAnimEvent>* GetNpcEventTimeline(const FString& OwnerStem,
		const FString& Label) override;
	virtual bool PlayCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName,
		bool bLoop, float* OutSeconds) override;
	virtual bool PreloadCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName) override;
	virtual bool PreloadCinematicClipForModel(const FString& Stem, bool bPlayerMaterial,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName) override;
	virtual bool SeekCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds) override;
	virtual void StopCinematicClip(USkeletalMeshComponent* Body) override;
	virtual void ReleaseCinematicClaim(USkeletalMeshComponent* Body) override;
	virtual bool GetCinematicClipPosition(USkeletalMeshComponent* Body, float& OutSeconds) const override;
	virtual bool ResyncCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds) override;
	virtual int32 SetFlexControllers(USkeletalMeshComponent* Body,
		TArrayView<const FElysiumFlexWrite> Writes, TArray<FString>* OutMissing) override;
	virtual bool SetMouthOpen(USkeletalMeshComponent* Body, float Open) override;
	virtual bool PlayAttachedEffect(USkeletalMeshComponent* Body, const FString& Definition,
		FName Attachment) override;
	// R7.3 (`effects-architecture.md` §5.9): the producer entry point -- one transient effect actor
	// on the floor (or its family override), the tree from `DA_ElysiumParticleTrees`.
	virtual FElysiumEffectHandle SpawnParticleRoot(const FString& Root,
		const FElysiumEntityHandle* Parent, int32 AttachMode, FName AttachName, int32 AttachPoint,
		const FVector& OriginCm, const FRotator& Angles) override;
	virtual void StopParticleRoot(const FElysiumEffectHandle& Handle) override;
	virtual void KillParticleRoot(const FElysiumEffectHandle& Handle) override;
	virtual bool GetPhonemeFilter(USkeletalMeshComponent* Body, float& OutMin,
		float& OutMax) const override;
	virtual bool SetViewTarget(USkeletalMeshComponent* Body, const FVector& WorldTarget) override;
	virtual bool GetHeadFrame(USkeletalMeshComponent* Body, FVector& OutPosition,
		FVector& OutForward) const override;
	virtual FString AnimatedPropStemForModel(const FString& ModelPath) const override;
	virtual FElysiumPlacedModelBody BuildPlacedModelBody(
		const FElysiumPlacedModelRequest& Request) override;
	virtual bool HasPlacedModelCatalogue() const override;
	virtual USkeletalMeshComponent* BuildAnimatedPropVisual(const FString& Stem,
		const FVector& Location, const FQuat& Rotation, float UniformScale,
		int32 PlacementToken = 0) override;
	virtual bool PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& ClipName, bool bLoop, float* OutSeconds) override;
	virtual int32 PreloadAnimatedPropClips(USkeletalMeshComponent* Body,
		const FString& Stem) override;
	virtual int32 FinishAnimationPreload() override;
	virtual void ApplyAnimatedPropSkin(USkeletalMeshComponent* Comp,
		const FString& StaticStem, int32 Family) override;
	virtual FString AnimatedPropRestClip(const FString& Stem,
		int32 PlacementToken = 0) const override;
	virtual bool FindAnimatedPropClip(const FString& Stem, const FString& ClipName,
		bool& bOutLoops) const override;
	virtual UStaticMeshComponent* BuildBrushVisual(const FString& Stem,
		USceneComponent* ParentBody, float UniformScale, bool bSky) override;
	virtual UStaticMeshComponent* BuildPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) override;
	virtual EElysiumItemGroundModelState ItemGroundModelState(
		const FString& ModelPath) override;
	virtual UStaticMeshComponent* BuildPhysPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) override;
	virtual void ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family) override;
	virtual USkeletalMeshComponent* BuildPlayerVisual(const FString& Stem,
		const FString& Disposition, int32 IdleVariant) override;
	virtual void ClearPlayerVisual() override;
	virtual void SetPlayerBodyEntityHidden(bool bInHidden) override;

	// IElysiumEmbodiment: the player's body.
	// Every override in this block resolves the pawn through this world's first player controller
	// and reports false / no-ops when there is none (the menu backdrop seats no pawn). They are the
	// substrate's route to the player pawn.
	virtual bool GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const override;
	virtual bool GetPlayerUseOrigin(FVector& OutLocation) const override;
	virtual bool GetPlayerFeetTransform(FVector& OutFeetOrigin, FRotator& OutViewRotation) const override;
	virtual bool GetPlayerCapsuleTransform(FVector& OutCapsuleCenter, FRotator& OutViewRotation) const override;
	virtual void TeleportPlayer(const FVector& FeetOrigin, const FRotator& ViewRotation) override;
	virtual void DamagePlayer(float Amount) override;
	virtual void RegisterUseAnchor(UPrimitiveComponent* Source,
		const FElysiumEntityHandle& Owner) override;
	virtual void SetUseAnchorEnabled(const FElysiumEntityHandle& Owner, bool bEnabled) override;
	virtual void ClearUseAnchors() override;
	// Presentation-only lookup for the rendered component that supplied an entity's use anchor.
	// Query proxies remain private; callers receive the physical mesh/brush, never the proxy box.
	UPrimitiveComponent* FindUseVisual(const FElysiumEntityHandle& Owner) const;
	virtual void RegisterTouchAnchor(UPrimitiveComponent* Source,
		const FElysiumEntityHandle& Owner) override;
	virtual void SetTouchAnchorEnabled(const FElysiumEntityHandle& Owner, bool bEnabled) override;
	virtual void ClearTouchAnchors() override;
	virtual FElysiumUseQueryResult QueryPlayerUse(
		const FElysiumEntityHandle& CurrentFocus) const override;
	virtual FElysiumEntityHandle QueryFeedTarget() const override;
	// The ranged shot's aim query. Geometry only, and the zero-spread case of retail's cone.
	virtual FElysiumEntityHandle QueryAimTarget(float MaxRangeCm) const override;
	// The two perception queries. Geometry only; every threshold stays substrate.
	virtual bool QueryLineOfSight(const FVector& FromCm, const FVector& ToCm) const override;
	virtual float QueryLightAtPoint(const FVector& PointCm) const override;
	// R6.2: the lightstyle pattern table and the runtime `light_dynamic` source, both on the rig.
	virtual void SetLightStylePattern(int32 Style, const FString& Pattern) override;
	virtual FString LightStylePattern(int32 Style) const override;
	virtual ULightComponent* BuildDynamicLight(const FElysiumDynamicLightSpec& Spec,
		USceneComponent* Parent) override;
	virtual void DestroyDynamicLight(ULightComponent* Light) override;
	// R6.1: the baked sprite actor's hidden state, through the visuals' entity-index bucket.
	virtual void SetBakedSpriteVisible(int32 EntityIndex, bool bVisible) override;
	virtual bool IsPlayerSneaking() const override;
	virtual bool IsPlayerOnGround() const override;
	virtual FString GetPlayerBaseActivity() const override;
	// The forced-sequence record the melee stop's rule is evaluated over, and the stop
	// itself. Both are the player body's, so both live on this actor beside the driver that owns it.
	virtual void StopPlayerBody() override;
	virtual float ResolveNpcMakerGroundZ(const FVector& MakerOriginCm,
		float TraceDepthCm) const override;
	virtual bool IsNpcMakerVisibleFromPlayer(const FVector& MakerOriginCm) const override;
	virtual bool IsNpcMakerInPlayerViewCone(const FVector& MakerOriginCm) const override;
	virtual bool IsNpcMakerSpawnAreaOccupied(const FVector& GroundOriginCm,
		float HalfExtentCm) const override;
	// R7.2: the ranged shot's forward world trace and the decal it leaves, through the world's
	// UElysiumDecalSubsystem.
	virtual bool LayShotImpactDecal(const FVector& FromCm, const FVector& Direction, float RangeCm,
		int32 Variation) override;
	// The scripted-shot channel. The director resolves a `vdata/camerashots/` file against this
	// map's entities and bodies and hands the values to the pawn's camera; the camera itself never
	// learns what an entity is.
	virtual int32 PushCameraShot(const FString& ShotFile, const FElysiumEntityHandle& Subject) override;
	virtual int32 PushCameraShotValue(const FElysiumCameraShot& Shot) override;
	virtual bool UpdateCameraShotValue(int32 ShotId, const FElysiumCameraShot& Shot) override;
	virtual void SetEquippedCameraClass(int32 CameraClass) override;
	virtual bool PopCameraShot(int32 ShotId, float BlendOutSeconds = -1.0f) override;

	// IElysiumAudio.
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
	virtual float OutputLeadSeconds() const override;

	// IElysiumTravel.
	// Both forward to the GI-scoped UElysiumMapSubsystem, which owns when the travel happens.
	virtual void RequestLandmarkTravel(const FString& Map, const FString& Landmark,
		const FVector& Offset, float Yaw) override;
	virtual void ChangeMap(const FString& Map) override;

	// IElysiumWeather.
	virtual void ApplyWetness(const FElysiumWeatherTransition& Transition) override;
	virtual void ApplyEmitter(const FElysiumWeatherEmitterState& Emitter) override;
	virtual void RemoveEmitter(const FElysiumEntityHandle& Entity) override;
	// R7.3: the three Valve classes' publishes, each to its bake-placed actor by entity index.
	virtual void ApplyDust(const FElysiumDustState& Dust) override;
	virtual void ApplySteam(const FElysiumSteamState& Steam) override;
	virtual void ApplyBeam(const FElysiumBeamState& Beam) override;
	void FireWeatherTimer(bool bRainOn);
	FString GetWeatherDebugSummary() const;
	const FElysiumWeatherTransition& GetWetnessTransition() const { return WetnessTransition; }
	const TMap<int32, FElysiumWeatherEmitterState>& GetWeatherEmitters() const
	{
		return RainEmitterStates;
	}
	bool IsFollowRainActive() const;
	FVector GetFollowRainLocation() const;
	float GetPresentedWetness() const { return PresentedWetness; }
	float GetPresentedWetnessScale() const { return PresentedWetnessScale; }
	bool IsEnvironmentWetnessOverridden() const { return bEnvironmentWetnessOverride; }

	// Live stats for the debug overlay, filled by LoadMap. The look and collider counts live on
	// UElysiumMapVisuals / UElysiumMapCollision alongside the things they count.
	FString LoadedMap;
	// Entity substrate: number of `.ents` records the world spawned (0 if the map has no sidecar),
	// and how many of them got a brush body (convex collision / trigger overlap volume).
	int32 EntityCount = 0;
	int32 BrushBodyCount = 0;

	// The `info_landmark` this map load entered through (a landmark transition / direct
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
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> RainFollowComponent;
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UNiagaraComponent>> RainComponents;
	// Runtime-only target bounds. Brush entities register their existing body; model props receive
	// a query-only box component and remain ordinary components of this one map actor.
	UPROPERTY(Transient) TArray<TObjectPtr<UPrimitiveComponent>> OwnedUseAnchorComponents;
	struct FUseAnchorRecord
	{
		TWeakObjectPtr<UPrimitiveComponent> Component;
		TWeakObjectPtr<UPrimitiveComponent> Visual;
		FElysiumEntityHandle Owner;
		bool bEnabled = true;
	};
	TArray<FUseAnchorRecord> UseAnchors;
	UPROPERTY(Transient) TArray<TObjectPtr<UPrimitiveComponent>> OwnedTouchAnchorComponents;
	struct FTouchAnchorRecord
	{
		TWeakObjectPtr<UPrimitiveComponent> Component;
		FElysiumEntityHandle Owner;
		bool bEnabled = true;
	};
	TArray<FTouchAnchorRecord> TouchAnchors;

	// R6.1 diagnostic: entity indices whose `SetBakedSpriteVisible` found no baked billboard, so
	// the miss is reported once per sprite instead of once per input. A map that bakes no sprites
	// at all is the ruled legacy-lane case and says nothing here.
	TSet<int32> SpriteMissWarned;

	UFUNCTION()
	void HandleTouchAnchorBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	/** Apply the recovered origin/tree/point attachment rule to one emitter component. */
	void AttachEmitter(const struct FElysiumWeatherEmitterState& Emitter, UNiagaraComponent* Component);
	// R7.3 (`ElysiumMapActorEffects.cpp`): the retarget for a `MapsOnV2Models` map -- drive the
	// bake-placed AElysiumEffectActor by entity index instead of creating a component.
	void ApplyEmitterToPlacedActor(const struct FElysiumWeatherEmitterState& Emitter);
	// Resolve what an attach mode needs off the parent entity (§5.7): its body, its skeletal
	// mesh, the socket the bone names, the reconstructed world location of a tree attach. False
	// when the mode needs a parent it cannot have; the actor then keeps its placed origin.
	bool ResolveEffectAttachment(FElysiumEntity* ParentEntity, int32 AttachType,
		const FString& Bone, const FVector& AuthoredLocationCm, int32 EntityIndex,
		const FString& Label, FElysiumEffectAttachment& Out);
	// The last publish per placed effect, for the once-logged "no actor" warning and the
	// weather debug summary.
	TMap<int32, FElysiumWeatherEmitterState> EffectEmitterStates;
	// §5.9's transient roots, by handle id.
	TMap<int32, TWeakObjectPtr<AElysiumEffectActor>> TransientEffects;
	int32 NextEffectId = 0;
	UPROPERTY(Transient) TObjectPtr<UElysiumParticleTrees> ParticleTrees;
	void RefreshFollowRain();
	void UpdateFollowRainLocation();
	TMap<int32, FElysiumWeatherEmitterState> RainEmitterStates;
	/** Failure identities already reported by the presentation adapter; avoids per-tick warning spam. */
	TSet<FString> ReportedEmitterFailures;
	/** Warn once per failure identity: on the first sighting of Key, record it in
	    ReportedEmitterFailures and log the produced message as a LogElysium warning; on every later
	    sighting do nothing, so a failure path that re-runs per tick cannot spam the log. The message
	    is a producer rather than a string so a suppressed repeat pays no formatting cost. */
	void WarnEmitterOnce(const FString& Key, TFunctionRef<FString()> Message);
	FElysiumWeatherTransition WetnessTransition;
	float PresentedWetness = 0.0f;
	float PresentedWetnessScale = 1.0f;
	bool bEnvironmentWetnessOverride = false;

	// The 3D-skybox miniature's placement (`<map>.sky`), or the identity on the 65 maps
	// with no `sky_camera`. Read at map load and used twice: the def parser carries sky-scope
	// entities through it, and a miniature body takes its uniform mesh scale from it.
	FElysiumSkyDef SkyDef;

	// The Track-B entity substrate for this map: parsed defs, live entities, the event
	// queue, and the debug sinks. A plain C++ object (no UObject) held type-erased so the header
	// needs only a forward declaration; destroyed with the actor on map unload.
	TPimplPtr<FElysiumEntityWorld> EntityWorld;

	// The SoundScheme manager (plain C++, owned here). Constructed alongside EntityWorld so the
	// ambient_soundscheme entities can reach it during their spawn pass; ticked from Tick with the
	// player location; its voices are stopped on unload (EndPlay).
	TPimplPtr<FElysiumSoundSchemeManager> SchemeManager;
	// A start_enabled ambient_soundscheme asks for its bed during the construction Spawn pass. Keep
	// that request as data until the activation transaction reaches its audio step.
	bool bHasDeferredSchemeFadeIn = false;
	FString DeferredSchemeRel;
	FVector DeferredSchemeAnchor = FVector::ZeroVector;
	float DeferredSchemeFadeSeconds = 0.0f;

	// The live scripted camera shots and their entity bindings. Owned here because resolving a
	// shot's anchors needs the entity world and the bodies standing in it; refreshed in the post-move
	// pass so a shot following an NPC sees where that NPC ended the frame.
	TPimplPtr<class FElysiumCameraDirector> CameraDirector;
	// The pawn's camera, or null (a backdrop map seats no pawn).
	class UElysiumCameraComponent* PlayerCamera() const;

	// The player body's animation selection, driven from the post-move pass. Owned here rather
	// than on a pawn because the resolution needs the entity world and the model stem, and because
	// `IElysiumPlayerBody` is what serves both movement implementations through one interface. It dies
	// with the map epoch, which is also what resets the jump latch across a travel.
	TPimplPtr<struct FElysiumAnimationDriver> PlayerAnimDriver;
	// The stem the player visual was built from, kept so the driver can name its catalog without
	// re-deriving it from the entity record every frame.
	FString PlayerVisualStem;
	// The driver's gait-table generation as last handed to the mover. The push is on change
	// rather than per frame, and the mover keeps the tables across a teleport because they belong to
	// the body — so this is the only thing that has to remember whether it happened.
	uint32 PushedGaitGeneration = 0;
	void TickPlayerAnimation(float DeltaSeconds);

	// Animation-driven movement.
	// Where the base channel's own clip stands, read off the pose layer and pushed onto the driver
	// before it ticks. The driver never reaches for an anim instance — it also serves bodies that
	// have none — so this is the one place the two are joined.
	//
	// It answers only for the clip the base-channel CLAIM named: a body posing something else there
	// is not standing on the forced sequence, and reading that clip's cycle would time the lock off
	// a stranger.
	FElysiumBaseClipCycle ReadPlayerBaseClipCycle(USkeletalMeshComponent* Visual) const;
	// Hand the mover the swing that owns its command, after the driver has settled the predicate.
	void PushPlayerAnimMovementLock(class APawn* Pawn);
	// Take it back. The lock is an input the mover keeps until it is handed a new one, so every path
	// that stops pushing — the predicate releasing, and this actor leaving its `Active` phase
	// mid-swing — goes through here rather than letting a frozen window stand.
	void ClearPlayerAnimMovementLock(class APawn* Pawn);
	// The clip the pushed path belongs to (`owner|label`), so the shared path is rebuilt when the
	// swing changes clips and shared by pointer on every other frame.
	FString PushedLockClip;
	TSharedPtr<const FElysiumClipMovementPath> PushedLockPath;
	// Owning stems whose blend sidecar cannot answer the movement question — either it carries no
	// `movement_fields` at all or it states a schema this build cannot address. Reported once each,
	// with the cause named: both are real gaps and NEITHER is the same absence as a clip the file
	// states no records for.
	TSet<FString> ReportedMovementGaps;
	// Latched so a body that cannot be stopped reports once rather than on every frame of a swing.
	bool bReportedNoStoppableBody = false;
	// StopPlayerBody's cached mover: the stop is asked fresh every frame of a melee tail window,
	// and the component set on a pawn cannot change under it — only the pawn itself can be
	// replaced, which is what invalidates the pair.
	TWeakObjectPtr<APawn> StopBodyPawn;
	TWeakObjectPtr<UElysiumMovementComponent> StopBodyMove;

	void LoadMap();
	// LoadMap's stage-world half: the substrate scaffolding a green room needs and nothing else —
	// an entity world with a player in it, a spawn transform for the pawn, and the body factory.
	// No baked level, no collision, no sidecars, so the activation barrier's world-content inputs
	// are satisfied by their own "intentionally absent" states rather than skipped.
	void BuildStageWorld();
	// If this load is a landmark transition (the map subsystem has a queued landmark spawn),
	// override the info_player_start placement: resolve the destination `info_landmark` in the just-
	// built entity world and seat the player at landmark origin + the carried offset. Fires the
	// landmark's OnEnterMapHere. No-op (keeps the .spawn placement) for a plain load or a missing
	// landmark. Called by LoadMap after the entity world is built.
	void ResolveLandmarkSpawn();
	// If this load is a save restore, the World block's absolute player pose outranks both
	// info_player_start and a landmark offset. Run right after ResolveLandmarkSpawn.
	void ResolveRestorePlacement();

	// Declare the frame order rather than observe it. Three edges are wired here, all of them
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
	bool bSuppressPlayerTouchIngress = false;
	bool bPlayerTouchReconcilePending = false;

	// The player pawn may not exist yet in BeginPlay, so final placement is performed by the
	// readiness poll. It stays frozen through the activation transaction.
	bool bSpawnPending = false;
	bool bSpawnPlaced = false;
	bool bSpawnDone = false;
	FVector PendingSpawnLoc = FVector::ZeroVector;
	float PendingSpawnYaw = 0.f;
	EElysiumPlayerPlacementSpace PendingSpawnSpace = EElysiumPlayerPlacementSpace::Feet;

	EElysiumMapRuntimePhase RuntimePhase = EElysiumMapRuntimePhase::Building;
	bool bRuntimeConstructionComplete = false;
	bool bAnimationPreloadReady = false;
	bool bMenuBackdrop = false;
	bool bNavigationBuildRequested = false;
	bool bNavigationBuildFailed = false;
	// Set in EndPlay. After it, a DestroyNpcMotor call is the entity world's own teardown running
	// against actors the engine has already destroyed, and must do nothing.
	bool bMotorsRetired = false;
	double RuntimeWaitStartSeconds = 0.0;
	double RuntimeWaitDurationSeconds = 0.0;
	// This map's epoch, minted by UElysiumMapSubsystem at BeginPlay and retired at EndPlay.
	// Everything an application-lifetime object holds on this map's behalf is keyed by it. 0 in a
	// bare world with no map subsystem, which owns nothing across a boundary that never fires.
	uint64 MapEpoch = 0;
	FString RuntimeFailureReason;
	FOnElysiumMapRuntimeReady RuntimeReady;
	FOnElysiumMapRuntimeFailed RuntimeFailed;
};
