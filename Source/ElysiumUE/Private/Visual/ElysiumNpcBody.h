#pragma once

#include "CoreMinimal.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumWorldServices.h"
#include "GameFramework/Character.h"
#include "Templates/PimplPtr.h"
#include "ElysiumNpcBody.generated.h"

class AElysiumMapActor;
class AElysiumNpcBody;
class ANavLinkProxy;

// The cast's animation pass, in TG_PostPhysics. A SECOND tick function rather than the
// actor's own tick, because the engine wires no prerequisite between an actor's tick and its own
// CharacterMovement: a selection read from `Tick` would be reading whichever of the two happened to
// register first. Declaring it as a class default is also what lets `Elysium.Substrate.FrameOrder`
// assert the ordering, which a runtime `AddPrerequisite` could not. The turn-in-place stays in
// pre-physics where it was.
USTRUCT()
struct FElysiumNpcAnimTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

	AElysiumNpcBody* Target = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread,
		const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
};

template <>
struct TStructOpsTypeTraits<FElysiumNpcAnimTickFunction>
	: public TStructOpsTypeTraitsBase2<FElysiumNpcAnimTickFunction>
{
	enum { WithCopy = false };
};

// The engine half of one mobile NPC. The imported skeletal component remains the visible body and
// is attached at feet-relative offset; this actor supplies the capsule, CharacterMovement, Detour
// crowd following, and the small engine-neutral IElysiumNpcMotor surface.
UCLASS(Transient, NotBlueprintable)
class AElysiumNpcBody final : public ACharacter, public IElysiumNpcMotor
{
	GENERATED_BODY()

public:
	AElysiumNpcBody(const FObjectInitializer& ObjectInitializer);

	void InitializeAtFeet(const FVector& FeetOrigin, float YawDegrees);
	void SetRuntimeReady(bool bReady);
	// The plain-C++ NPC this engine body embodies, and the map actor that owns the world it lives
	// in. Collision ingress uses the entity identity so an ACharacter overlap remains an NPC toucher
	// instead of being collapsed to !player.
	//
	// The map is held rather than read back off `GetOwner()` because an `APawn`'s owner is not
	// permanent: `AController::Possess` sets it to the controller, and this body spawns its
	// controller lazily the first time it is asked to travel. Reading the map off the actor owner
	// therefore answers correctly for a body that has never moved and null for every body that has —
	// which silently keys every request a MOVING body makes on no classname and empty hands.
	void SetOwningEntity(AElysiumMapActor* InMap, const FElysiumEntityHandle& InOwner);
	FElysiumEntityHandle GetOwningEntity() const { return OwningEntity; }
	// The model this body wears and the repeatable token its weighted picks ride on. Set once when
	// the motor is built, because that is the one place that knows both.
	void SetModelStem(const FString& InStem, USkeletalMeshComponent* InVisual, int32 InVariant);

	virtual void Tick(float DeltaSeconds) override;
	virtual void RegisterActorTickFunctions(bool bRegister) override;

	// This body's own animation pass, driven from the tick function below.
	void AnimTick(float DeltaSeconds);
	// The frame's selection record. Never null, for the same reason the player's is not.
	const FElysiumAnimationSelection& GetAnimSelection() const;
	// The sample that record was classified from — the driver's own, not a fresh `SampleLocomotion`.
	// The two are published together and read together; a producer that re-samples describes a
	// different frame than the record beside it.
	const FElysiumLocomotionSample& GetAnimSample() const;
	// What the driver keyed that record on: the actor's classname, the classname of the weapon
	// actually in its hands, and the state the alert/relaxed branch read. Read-only, and read off the
	// driver rather than off the entity, because the entity's authored loadout and what the body is
	// holding are different facts — an armed course that proved only the first proved nothing about
	// the ladder.
	void GetAnimTranslationContext(FString& OutActorClassname, FString& OutWeaponClassname,
		EElysiumNpcState& OutActorState) const;

	// The channel arbitration slot on this body's driver. An action family claims a channel
	// here; the driver's next anim pass ranks the claim against the locomotion publish and the
	// record carries the verdict. Builds the driver when the claim arrives ahead of the first anim
	// pass, so a same-frame scripted beat is not dropped. Handle contract as on the driver: 0 is a
	// refused claim, and releasing a handle that already lapsed answers false rather than failing.
	uint32 SubmitAnimRequest(const FElysiumAnimationRequest& Request);
	// Which overlay slot a granted `UpperBody` handle landed in, or `INDEX_NONE`. The driver's stack
	// allocates the slot, and the arm seam has to write the pins of THAT slot rather than a fixed one
	// — a shot fired during a reload composes on the higher index.
	int32 OverlaySlotForHandle(uint32 Handle) const;
	bool ReleaseAnimRequest(uint32 Handle);
	// Every standing claim at once, for the death transaction. A driver that was never built
	// holds nothing, so this does not build one.
	int32 ReleaseAllAnimRequests();
	// The claim standing on one channel, or null. Read-only, and deliberately not building a
	// driver either, for the same reason: a body that has never been claimed on holds nothing.
	const FElysiumAnimationRequest* ActiveAnimRequest(EElysiumAnimChannel Channel) const;

	// Public so a test can read the declared frame order off the class default.
	UPROPERTY()
	FElysiumNpcAnimTickFunction AnimTickFunction;

	virtual bool MoveTo(const FVector& FeetDestination, float AcceptanceRadiusCm,
		float SpeedCmPerSecond, bool bAllowPartialPath = false,
		TOptional<EElysiumNpcGaitKind> GaitKind = TOptional<EElysiumNpcGaitKind>()) override;
	virtual void Face(float YawDegrees) override;
	virtual void Stop() override;
	virtual void Teleport(const FVector& FeetOrigin, float YawDegrees) override;
	virtual void SetEnabled(bool bEnabled) override;
	virtual void SetFrozen(bool bFrozen) override;
	virtual void SetIgnoreCharacterCollision(bool bIgnore) override;
	virtual EElysiumNpcMoveStatus Sample(FVector& OutFeetOrigin, float& OutYawDegrees) override;
	virtual FElysiumLocomotionSample SampleLocomotion() const override;
	virtual bool ProjectToNavigable(const FVector& PointCm, FVector& OutProjectedCm) const override;
	virtual float GaitSpeed(EElysiumNpcGaitKind Gait, float MoveYawDegrees) const override;
	virtual bool Launch(const FVector& VelocityCmPerSecond) override;
	virtual bool SampleBallistic(FElysiumBallisticSample& Out) const override;
	virtual FElysiumNpcNavigationSample SampleNavigation() const override;
	virtual void ClearNavigationGoal() override;
	virtual void SetNavigationType(EElysiumNpcNavType Type) override;
	// Entered by the baked smart link, without aborting its suspended path follower.
	bool BeginNavigationJump(ANavLinkProxy* Link, const FVector& DestinationFeet);
	virtual void ResetSteering() override;

	// Unreal's own landing and blocking-hit notifications. They exist ONLY to record what
	// `SampleBallistic` reports: nothing is decided here, and neither one calls into the substrate.
	// That direction is the whole point — a body that pushed a landing into the entity layer would
	// be a presentation backchannel, so the chain polls on the think it already has.
	virtual void Landed(const FHitResult& Hit) override;
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
		bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse,
		const FHitResult& Hit) override;

	// Re-read the surfaceprop under this body's feet into the cache `SampleLocomotion` publishes.
	//
	// **The body owns this trace because `UCharacterMovementComponent` does not ask for it**: its
	// floor sweeps run without `bReturnPhysicalMaterial`, so `CurrentFloor.HitResult.PhysMaterial`
	// is null on every one of them (`CharacterMovementComponent.cpp`, `ComputeFloorDist`). The
	// player's mover asks inside the ground trace it was already running; a cast body has to run
	// one of its own.
	//
	// Called from `Tick` while a move request is in flight, which is where retail writes its own
	// cache: `CAI_Navigator::MoveEnact 0x102ef870` is the ONLY writer of `CAI_BaseNPC +0x5b90`
	// (through the setter `0x10270290`), so a standing body never pays for a trace and never
	// changes its answer. Public so an engine-tier test can drive it without a navmesh, a
	// controller and a crowd agent standing between it and one trace.
	void RefreshGroundSurface();

private:
	void ServiceNavigationJump();
	void FinishNavigationJump(bool bSucceeded);
	void ResetNavigationJump();
	EElysiumNpcNavType NavigationType = EElysiumNpcNavType::Ground;
	TWeakObjectPtr<ANavLinkProxy> NavigationJumpLink;
	FVector NavigationJumpDestination = FVector::ZeroVector;
	bool bNavigationJumpInProgress = false;
	bool bNavigationJumpLanded = false;
	bool bNavigationJumpFailed = false;

	// Clear every trace of a recording launch. Called from `Landed` and from `Stop()`, which is the
	// funnel freeze, teleport and disable all reach.
	void EndLaunchRecording();

	// The most recent blocking contact since the last `SampleBallistic`, recorded by the two
	// notifications above and consumed by it. A contact is reported ONCE: the chain asks per think,
	// and a normal left standing would divert it again on a wall it already rebounded from.
	mutable bool bBallisticContacted = false;
	mutable FVector BallisticContactNormal = FVector::ZeroVector;
	// Whether this body is carrying a launch. It gates the contact recording, so an ordinary walking
	// body brushing a doorframe never leaves a normal for a chain that is not running.
	bool bLaunched = false;

	// Build the driver if it does not exist yet and re-point it at the model this body wears.
	// The driver holds the body's gait tables, and a travel request wants them before the first
	// animation pass has run.
	void EnsureAnimDriver();
	FVector FeetLocation() const;
	void ApplyEnabledState();
	// Solidity is three independent decisions — enabled, frozen, and character-ignoring — so it is
	// resolved in one place and re-applied from every one of them.
	void ApplyCollisionState();
	void ApplyCrowdState();
	// **The one speed number**: what this body's mover is commanded with while a leg driven
	// by one of its own fans is in flight, cm/s.
	//
	// It is the cell the record just published, read back off the published record rather than
	// re-derived from the motor's requested gait kind — those are two keys (the order's gait and the
	// projection of the classified activity) and they can name different fans on the same frame, at
	// which point the body plays one cycle and travels at another. Only where the record projects to
	// no gait at all — the opening frames of a leg, before the body has left its idle — does the
	// order's own kind answer, because there is no published cell yet to command.
	float CommandedTravelSpeed() const;
	FVector RequestedFeet = FVector::ZeroVector;
	float RequestedAcceptanceCm = 20.0f;
	float RequestedYaw = 0.0f;
	// Which of this body's own authored fans the in-flight move's speed came from — unset for a
	// caller-authored speed, which `AnimTick` must never overwrite (the equip-mid-leg fix).
	TOptional<EElysiumNpcGaitKind> RequestedGaitKind;
	bool bMoveRequested = false;
	bool bFaceRequested = false;
	bool bRequestedEnabled = true;
	bool bRuntimeReady = false;
	// Borrowed by a cutscene and given back: a frozen body cannot move or be touched but stays on
	// screen; an ignoring body still collides with the world, just not with other characters.
	bool bFrozen = false;
	bool bIgnoreCharacterCollision = false;
	// Reported once: a body that cannot reach its character keys every request on no classname and
	// empty hands, and nothing downstream of that is wrong enough to notice.
	bool bWarnedNoTranslationContext = false;
	// And once per body per gait: the record projected to a gait whose fan published nothing, so the
	// commanded number falls back to the stated constant while the record names a different one.
	// That is the one place the speed authority is still two numbers, and it says so.
	mutable uint8 WarnedSpeedFallback = 0;
	// The surfaceprop under this body's feet — `CAI_BaseNPC +0x5b90`, the cached `surfacedata_t*`
	// the NPC footfall arm at `0x1026d460` returns silently on when it is null. Written only by
	// `RefreshGroundSurface` (per move step, as `CAI_Navigator::MoveEnact 0x102ef870` writes the
	// retail one) and cleared only at spawn (`InitializeAtFeet`, which is where `NPCInit 0x10273390`
	// and `OnRestore 0x1027bf50` clear retail's). It SURVIVES `Stop()` — an arrival, a freeze, a
	// disable and a teleport — exactly as retail's does: a footfall record landing on a body in its
	// blend-out still sounds on the floor it stopped on, and a teleported body answers the floor it
	// left until its first move step rewrites the cache, which is retail's own behaviour too.
	FName GroundSurface;
	FElysiumEntityHandle OwningEntity;
	TWeakObjectPtr<AElysiumMapActor> OwningMap;

	// The same driver the player body runs, on the same contract. Held by value: it is plain
	// C++ with no UObject in it, and it dies with the body.
	TPimplPtr<struct FElysiumAnimationDriver> AnimDriver;
	FString ModelStem;
	int32 AnimVariant = 0;
	TWeakObjectPtr<USkeletalMeshComponent> Visual;
};
