#include "Visual/ElysiumNpcBody.h"

#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DetourCrowdAIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Visual/ElysiumAnimationDriver.h"
#include "Visual/ElysiumAnimGraph.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGroundSurface.h"    // the surfaceprop under the feet, this body's own trace
#include "ElysiumMapActor.h"
#include "ElysiumMoveSolve.h"        // ElysiumMove::U -- the Source unit the trace depth is in
#include "ElysiumPawn.h"             // the player's hull, the one toucher `NotifyHit` records
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcGait.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#if ENABLE_VISUAL_LOG
#include "Substrate/ElysiumNpc.h"    // the snapshot's DebugString()
#include "VisualLogger/VisualLoggerTypes.h"
#if !UE_BUILD_SHIPPING && WITH_GAMEPLAY_DEBUGGER
#include "Debug/ElysiumNpcDebugData.h"
#endif
#endif
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"

void FElysiumNpcAnimTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType,
	ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Target && IsValidChecked(Target) && !Target->IsUnreachable())
	{
		FScopeCycleCounterUObject ActorScope(Target);
		Target->AnimTick(DeltaTime);
	}
}

FString FElysiumNpcAnimTickFunction::DiagnosticMessage()
{
	return GetFullNameSafe(Target) + TEXT("[AElysiumNpcBody::AnimTick]");
}

FName FElysiumNpcAnimTickFunction::DiagnosticContext(bool bDetailed)
{
	if (bDetailed)
	{
		return FName(*FString::Printf(TEXT("ElysiumNpcBodyAnim/%s"), *GetFullNameSafe(Target)));
	}
	return FName(TEXT("ElysiumNpcBodyAnim"));
}

AElysiumNpcBody::AElysiumNpcBody(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The animation pass runs after this body's own movement has produced the frame's final
	// velocity, which the tick group is what guarantees. It stops when the world is held, because a
	// held body is not moving and re-classifying it every frame would only churn the record.
	AnimTickFunction.bCanEverTick = true;
	AnimTickFunction.bStartWithTickEnabled = true;
	AnimTickFunction.TickGroup = TG_PostPhysics;
	AnimTickFunction.bTickEvenWhenPaused = false;

	AIControllerClass = ADetourCrowdAIController::StaticClass();
	// A standing NPC needs a body, not a crowd agent. Spawn the controller on the first accepted
	// MoveTo, after the runtime Recast graph has crossed the activation barrier.
	AutoPossessAI = EAutoPossessAI::Disabled;
	bUseControllerRotationYaw = false;

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(34.0f, 88.0f);
	Capsule->SetCollisionProfileName(TEXT("Pawn"));
	Capsule->SetCanEverAffectNavigation(false);

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0f, 360.0f, 0.0f);
	Movement->MaxWalkSpeed = 254.0f; // retail speed_walk: 100 Source inches/s, expressed in cm
	Movement->BrakingDecelerationWalking = 768.0f;
	Movement->SetCanEverAffectNavigation(false);
	Movement->SetAutoActivate(false);

	// The ACharacter mesh is unused; the runtime-loaded glTF component is attached by the map actor.
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AElysiumNpcBody::InitializeAtFeet(const FVector& FeetOrigin, float YawDegrees)
{
	Teleport(FeetOrigin, YawDegrees);
	// The spawn is where retail clears its cached `surfacedata_t` (`CAI_BaseNPC +0x5b90`):
	// `NPCInit 0x10273390` and `OnRestore 0x1027bf50`, both of which this body's construction stands
	// for. Nothing else clears it — `MoveEnact 0x102ef870` overwrites it on the next move step.
	GroundSurface = FName();
	ApplyEnabledState();
}

void AElysiumNpcBody::SetRuntimeReady(bool bReady)
{
	bRuntimeReady = bReady;
	ApplyEnabledState();
}

void AElysiumNpcBody::SetModelStem(const FString& InStem, USkeletalMeshComponent* InVisual,
	int32 InVariant)
{
	ModelStem = InStem;
	Visual = InVisual;
	AnimVariant = FMath::Max(0, InVariant);
	// The hold is the body's state, not the mesh's: a model swapped under a silent think stays
	// held until the think that releases it runs.
	if (bAnimationHeld && InVisual != nullptr)
	{
		InVisual->GlobalAnimRateScale = 0.0f;
	}
	EnsureAnimDriver();
	// A model swap is a new body: the latch, the last request and the previous model's tables all go.
	AnimDriver->Reset();

	// A clip already playing on the visual when this driver is built — `BuildNpcVisual` arms the
	// disposition idle before the motor exists, so that arm could not claim a slot that was not
	// there. The body therefore enters arbitration holding the ambient claim its clip stands for;
	// without it, the first standing publish would own the base and replace the ambient cast's
	// stance vocabulary with the resolver's generic idle.
	if (UElysiumBipedAnimInstance* Inst = InVisual
			? Cast<UElysiumBipedAnimInstance>(InVisual->GetAnimInstance()) : nullptr)
	{
		if (Inst->GetCurrentActiveMontage() != nullptr || Inst->GetPlayingClip() != nullptr)
		{
			FElysiumAnimationRequest Adopted;
			Adopted.Source = EElysiumAnimSource::Npc;
			Adopted.Channel = EElysiumAnimChannel::Base;
			Adopted.Priority = EElysiumAnimPriority::Ambient;
			Adopted.Label = TEXT("adopted stand");
			AnimDriver->SubmitRequest(Adopted);
		}
	}

	// The body's authored speeds, resolved the moment it knows which model it wears rather than at
	// its first animation pass. A patrol or a scripted beat can issue its first travel request in the
	// same frame this body is built, and that request reads the tables through `GaitSpeed`.
	UElysiumAnimSubsystem* Anims = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	AnimDriver->RefreshGaitSpeeds(Anims);
}

uint32 AElysiumNpcBody::SubmitAnimRequest(const FElysiumAnimationRequest& Request)
{
	// Built on demand: a scripted beat can claim the body in the same frame the motor is built,
	// ahead of its first animation pass, and a dropped claim there would let the first publish end
	// the beat's clip.
	EnsureAnimDriver();
	return AnimDriver->SubmitRequest(Request);
}

bool AElysiumNpcBody::ReleaseAnimRequest(uint32 Handle)
{
	return AnimDriver.IsValid() && AnimDriver->ReleaseRequest(Handle);
}

int32 AElysiumNpcBody::OverlaySlotForHandle(uint32 Handle) const
{
	return AnimDriver.IsValid() ? AnimDriver->Overlay.FindByHandle(Handle) : INDEX_NONE;
}

int32 AElysiumNpcBody::ReleaseAllAnimRequests()
{
	// Deliberately NOT `EnsureAnimDriver`: a body whose driver was never built has never granted a
	// claim, and building one here would hand a corpse a locomotion publisher it never had.
	if (!AnimDriver.IsValid())
	{
		return 0;
	}
	bool bDroppedSlotLayer = false;
	const int32 Released = AnimDriver->ReleaseAllRequests(&bDroppedSlotLayer);
	// **A released claim does not take a pose down; the driver's next publish would, and this body
	// may never publish again.** The one caller is the death transaction, which freezes the corpse
	// straight after — so an overlay layer dropped here without this keeps composing the last frame
	// and weight of a shot the character died mid-way through. The driver serves bodies with no anim
	// instance at all and cannot reach one, so the take-down belongs here.
	if (bDroppedSlotLayer)
	{
		UElysiumBipedAnimInstance::StopSlotLayerOn(Visual.Get());
	}
	return Released;
}

const FElysiumAnimationRequest* AElysiumNpcBody::ActiveAnimRequest(
	EElysiumAnimChannel Channel) const
{
	return AnimDriver.IsValid() ? AnimDriver->ActiveRequest(Channel) : nullptr;
}

void AElysiumNpcBody::SetOwningEntity(AElysiumMapActor* InMap, const FElysiumEntityHandle& InOwner)
{
	OwningMap = InMap;
	OwningEntity = InOwner;
}

void AElysiumNpcBody::EnsureAnimDriver()
{
	if (!AnimDriver.IsValid())
	{
		AnimDriver = MakePimpl<FElysiumAnimationDriver>();
		AnimDriver->Source = EElysiumAnimSource::Npc;
		AnimDriver->BodyKind = EElysiumAnimBodyKind::Cast;
	}
	AnimDriver->Stem = ModelStem;
	AnimDriver->Variant = AnimVariant;
}

float AElysiumNpcBody::GaitSpeed(EElysiumNpcGaitKind Gait, float MoveYawDegrees) const
{
	if (!AnimDriver.IsValid())
	{
		return 0.f;   // no driver yet: unanswerable, and the caller falls back
	}
	const FElysiumGaitSpeeds& Speeds = AnimDriver->GaitSpeeds;
	const FElysiumGaitSpeedTable* Table = nullptr;
	switch (Gait)
	{
	case EElysiumNpcGaitKind::Run:   Table = &Speeds.Run;   break;
	case EElysiumNpcGaitKind::Sneak: Table = &Speeds.Sneak; break;
	default:                         Table = &Speeds.Walk;  break;
	}
	// The cell at the direction asked for. `bOrientRotationToMovement` settles a path-following body
	// onto zero, but it is not there while it turns — a patrol turnaround plays several strafe cells
	// on its way round, and commanding the forward one through them is the slide.
	return Table->SpeedAt(MoveYawDegrees);
}

float AElysiumNpcBody::CommandedTravelSpeed() const
{
	// The published record is the authority. `GaitSpeedForSelection` already read this body's own
	// fan for the projected graph state at the realized `move_yaw`, so re-reading a fan here — from
	// the travel order's gait kind, which is a different key — is what let a body play a walk cycle
	// at a run speed on a turnaround that dropped the run fan's rear cell under the split.
	const FElysiumAnimationSelection& Sel = AnimDriver->Selection;
	EElysiumNpcGaitKind Projected = EElysiumNpcGaitKind::Walk;
	if (ElysiumNpcGait::GaitKindForState(Sel.GraphState, Projected))
	{
		if (FMath::IsFinite(Sel.GroundSpeedCmPerSecond) && Sel.GroundSpeedCmPerSecond > 0.f)
		{
			return Sel.GroundSpeedCmPerSecond;
		}
		// The record projected to a gait and published no number for it: the body resolves no fan
		// for that gait and no clip speed either, so the stated constant is all there is. That is
		// the one residual place the command and the record are two numbers, and it is reported —
		// once per gait per body, because the answer does not come back on its own.
		const uint8 Bit = static_cast<uint8>(1u << static_cast<uint8>(Projected));
		if ((WarnedSpeedFallback & Bit) == 0)
		{
			WarnedSpeedFallback |= Bit;
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("NPC body '%s' published graph state %s with no cell speed: its %s fan resolved ")
				TEXT("nothing and the mover falls back to the stated constant, so what it travels at ")
				TEXT("is not what its record names"),
				*ModelStem, ElysiumAnimGraph::StateName(Sel.GraphState),
				Projected == EElysiumNpcGaitKind::Run ? TEXT("run")
					: (Projected == EElysiumNpcGaitKind::Sneak ? TEXT("sneak") : TEXT("walk")));
		}
		return ElysiumNpcGait::TravelSpeed(this, Projected, Sel.MoveYaw);
	}
	// Not a gait at all — the opening frames of a leg, where the body is still standing in its idle
	// and the record has no cell to command. The order's own kind is what it was given, so it is
	// what carries the body until the classifier catches up.
	return ElysiumNpcGait::TravelSpeed(this, *RequestedGaitKind, Sel.MoveYaw);
}

const FElysiumLocomotionSample& AElysiumNpcBody::GetAnimSample() const
{
	static const FElysiumLocomotionSample Empty;
	return AnimDriver.IsValid() ? AnimDriver->Sample : Empty;
}

void AElysiumNpcBody::GetAnimTranslationContext(FString& OutActorClassname,
	FString& OutWeaponClassname, EElysiumNpcState& OutActorState) const
{
	if (AnimDriver.IsValid())
	{
		OutActorClassname = AnimDriver->ActorClassname;
		OutWeaponClassname = AnimDriver->WeaponClassname;
		OutActorState = AnimDriver->ActorState;
		return;
	}
	// A body with no driver has keyed nothing yet, which is a real state rather than empty hands: the
	// caller is told the same empty answer either way, and the frame count is what says which.
	OutActorClassname.Reset();
	OutWeaponClassname.Reset();
	OutActorState = EElysiumNpcState::Idle;
}

void AElysiumNpcBody::RegisterActorTickFunctions(bool bRegister)
{
	Super::RegisterActorTickFunctions(bRegister);

	if (bRegister && AnimTickFunction.bCanEverTick)
	{
		AnimTickFunction.Target = this;
		AnimTickFunction.SetTickFunctionEnable(AnimTickFunction.bStartWithTickEnabled);
		AnimTickFunction.RegisterTickFunction(GetLevel());
		// The tick group already separates the two; saying so in the graph as well keeps the
		// dependency if anyone re-groups either end.
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			AnimTickFunction.AddPrerequisite(Movement, Movement->PrimaryComponentTick);
		}
	}
	else if (!bRegister)
	{
		AnimTickFunction.UnRegisterTickFunction();
	}
}

void AElysiumNpcBody::AnimTick(float DeltaSeconds)
{
	EnsureAnimDriver();

	USkeletalMeshComponent* Body = Visual.Get();
	UElysiumAnimSubsystem* Anims = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	UElysiumBipedAnimInstance* Graph = Body
		? Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()) : nullptr;

	// Read BEFORE the tick, on the same generation gate the player's producer uses: the report
	// describes the request published last frame, and the latch is about to decide whether that
	// request is over. A report stamped with a generation the driver has moved past describes a
	// clip that is no longer playing, and letting it answer would end the request that replaced it.
	static const FElysiumOneShotReport NoReport;
	const FElysiumOneShotReport& Report = Graph ? Graph->GetOneShotReport() : NoReport;
	const EElysiumOneShotState OneShot = ElysiumAnimGraph::OneShotStateFor(
		/*bHasAsset*/ AnimDriver->Assets.Sequence != nullptr
			|| AnimDriver->Assets.Space != nullptr,
		/*bGenerationMatches*/ Graph != nullptr
			&& Report.Generation == AnimDriver->Selection.Generation,
		Report.bInOneShotState, Report.bComplete);

	// What this body's own classname and drawn weapon do to every request it makes. Refreshed here
	// rather than on the equip: the driver keys its own re-resolve on the value changing, so a
	// loadout swap costs one frame of latency and no per-equip wiring, and a body whose entity has
	// gone reads empty hands rather than keeping what it last held.
	const AElysiumMapActor* Map = OwningMap.Get();
	FElysiumEntityWorld* Entities = Map ? Map->GetEntityWorld() : nullptr;
	FElysiumEntity* OwnerEntity = Entities ? Entities->Resolve(OwningEntity) : nullptr;
	FElysiumCombatCharacter* Character = OwnerEntity ? OwnerEntity->AsCombatCharacter() : nullptr;
	// A body standing for an entity it cannot reach keys every request it makes on no classname and
	// empty hands — the class bodies and the weapon ladders are then simply never walked, and the
	// record says "resolved" the whole time. Once per body, because the answer does not come back.
	if (Character == nullptr && OwningEntity.IsSet() && !bWarnedNoTranslationContext)
	{
		bWarnedNoTranslationContext = true;
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("NPC body '%s' cannot reach the character it stands for (%s): every request it ")
			TEXT("makes will be keyed with no classname and empty hands"),
			*ModelStem, Map == nullptr ? TEXT("no owning map actor")
				: (Entities == nullptr ? TEXT("no entity world")
					: (OwnerEntity == nullptr ? TEXT("the entity does not resolve")
						: TEXT("the entity is not a combat character"))));
	}
	AnimDriver->SetTranslationContext(Character);

	AnimDriver->Tick(DeltaSeconds, SampleLocomotion(), Anims,
		Body ? Body->GetSkeletalMeshAsset() : nullptr, OneShot);
	// From here on the driver's own `Sample` is the frame: the getter above recomputes from live
	// component state, and calling it twice in one frame is how a reader comes to describe a frame
	// the record does not.

	// **The speed authority's push, this body's half** (mirroring the player's push in
	// `AElysiumMapActor::TickPlayerAnimation`). The mover is commanded with the cell the record it
	// just published names — literally that number, not a second reading keyed on the travel order's
	// own gait. Per frame rather than on a generation change, because the direction moves every
	// frame while the tables move almost never — and an equip mid-leg, which is what the generation
	// gate existed for, is then just one more frame's answer.
	//
	// Only a leg whose speed came FROM a fan is re-derived — a caller-authored speed (the scripted
	// Walk/Custom gaits) keeps exactly the number it was handed, which is the trap this must not
	// fall into.
	if (bMoveRequested && RequestedGaitKind.IsSet())
	{
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			// The same floor `MoveTo` applies: the motor treats zero as a stall, so a fan that
			// resolves nothing must not be able to park a body mid-leg.
			Movement->MaxWalkSpeed = FMath::Max(1.0f, CommandedTravelSpeed());
		}
	}

	// Hand the settled record to this body's own graph, the same push `AElysiumMapActor` makes for
	// the player. Both producers fill one contract (`FElysiumAnimationDriver`), so a cast member's
	// locomotion and the player's cannot become two systems that happen to play the same files —
	// and a driver that resolved without publishing leaves the graph with no selection at all, which
	// the component answers with its reference pose.
	//
	// A body whose graph package is missing is not a biped instance and is skipped; it poses through
	// its own clip player instead, with no locomotion behind it.
	if (Graph != nullptr)
	{
		Graph->PublishSelection(AnimDriver->Selection, AnimDriver->Assets);
	}
}

const FElysiumAnimationSelection& AElysiumNpcBody::GetAnimSelection() const
{
	// A body with no driver yet still has a chain, and a readout that reported the record's default
	// would name every cast body a player one. Built once so the accessor stays a reference return.
	static const FElysiumAnimationSelection Empty = []
	{
		FElysiumAnimationSelection Blank;
		Blank.BodyKind = EElysiumAnimBodyKind::Cast;
		return Blank;
	}();
	return AnimDriver.IsValid() ? AnimDriver->Selection : Empty;
}

FVector AElysiumNpcBody::FeetLocation() const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	return GetActorLocation() - FVector(0.0f, 0.0f, Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.0f);
}

const FElysiumNpc* AElysiumNpcBody::ResolveOwningNpc(const FElysiumEntityWorld*& OutWorld) const
{
	const AElysiumMapActor* Map = OwningMap.Get();
	OutWorld = Map ? Map->GetEntityWorld() : nullptr;
	const FElysiumEntity* Entity = OutWorld ? OutWorld->Resolve(OwningEntity) : nullptr;
	return Entity ? Entity->AsNpc() : nullptr;
}

#if ENABLE_VISUAL_LOG

void AElysiumNpcBody::GrabDebugSnapshot(FVisualLogEntry* Snapshot) const
{
	if (Snapshot == nullptr)
	{
		return;
	}
	Snapshot->Location = GetActorLocation();

	FVisualLogStatusCategory Status(TEXT("Elysium NPC"));
	const FElysiumEntityWorld* EntityWorld = nullptr;
	const FElysiumNpc* Npc = ResolveOwningNpc(EntityWorld);
	if (Npc == nullptr)
	{
		Status.Add(TEXT("State"), TEXT("stale or unavailable entity owner"));
		Snapshot->Status.Add(Status);
		return;
	}

	Status.Add(TEXT("Entity"), Npc->DebugString());
	Status.Add(TEXT("Epoch"), FString::FromInt(static_cast<int32>(EntityWorld->GetEpoch())));
#if !UE_BUILD_SHIPPING && WITH_GAMEPLAY_DEBUGGER
	// The snapshot is grabbed once per Visual Logger entry, so the row arrays stay out of it.
	FElysiumNpcDebugData Data;
	Data.Build(*Npc, *EntityWorld, EElysiumNpcDebugRows::SummaryOnly);
	Status.Add(TEXT("Mind"), FString::Printf(TEXT("%s / %s / %s"),
		*Data.State, *Data.IdealState, *Data.BodyOwner));
	Status.Add(TEXT("Schedule"), FString::Printf(TEXT("%s task %d/%d %s"),
		*Data.ScheduleName, Data.TaskIndex + 1, Data.TaskCount, *Data.CurrentTask));
	Status.Add(TEXT("Conditions"), Data.Conditions);
	Status.Add(TEXT("Interrupt hits"), Data.InterruptHits);
	Status.Add(TEXT("Enemy"), FString::Printf(TEXT("%s failures=%d%s"), *Data.Enemy,
		Data.EnemyLosFailures, Data.bEnemyOccluded ? TEXT(" occluded") : TEXT("")));
	Status.Add(TEXT("Memory records"), FString::FromInt(Data.EnemyMemoryCount));
	Status.Add(TEXT("Trace"), Data.Trace.Num() > 0 ? Data.Trace.Last() : TEXT("(none)"));
#else
	Status.Add(TEXT("State"), TEXT("debug projection unavailable in this build"));
#endif
	Snapshot->Status.Add(Status);
}

#endif // ENABLE_VISUAL_LOG

void AElysiumNpcBody::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ServiceNavigationJump();
	// A held body is a body whose think is not running: no move step (so no ground refresh, as
	// retail's `MoveEnact` never runs) and no turn-in-place. `bFaceRequested` survives the hold
	// and the turn resumes on release, as the retail motor's ideal yaw does.
	if (bHeld)
	{
		return;
	}
	// Retail refreshes the NPC's ground surface once per move step and nowhere else
	// (`CAI_Navigator::MoveEnact 0x102ef870` -> `0x10270290` -> `+0x5b90`), so a body that is not
	// travelling pays for no trace and keeps the answer its last leg left — which is also what
	// makes the cache a cache rather than a per-frame query.
	if (bMoveRequested)
	{
		RefreshGroundSurface();
	}
	if (!bFaceRequested)
	{
		return;
	}
	// The turn-in-place. CharacterMovement is asleep here (a face request never travels), so its
	// own bOrientRotationToMovement never sees this and there is nothing to fight over the yaw.
	const float CurrentYaw = GetActorRotation().Yaw;
	const float RotationRate = GetCharacterMovement() ? GetCharacterMovement()->RotationRate.Yaw : 360.0f;
	const float NewYaw = FMath::FixedTurn(CurrentYaw, RequestedYaw, RotationRate * DeltaSeconds);
	SetActorRotation(FRotator(0.0f, NewYaw, 0.0f));
	if (FMath::Abs(FRotator::NormalizeAxis(RequestedYaw - NewYaw)) <= 1.0f)
	{
		bFaceRequested = false;
	}
}

bool AElysiumNpcBody::MoveTo(const FVector& FeetDestination, float AcceptanceRadiusCm,
	float SpeedCmPerSecond, bool bAllowPartialPath, TOptional<EElysiumNpcGaitKind> GaitKind)
{
	bFaceRequested = false;
	if (!bRuntimeReady || !bRequestedEnabled || bFrozen || bNavigationJumpInProgress)
	{
		return false;   // a scene owns this body; it does not take travel requests
	}
	if (!GetController())
	{
		SpawnDefaultController();
		ApplyCrowdState();   // the agent exists only now; re-apply what the beat already asked for
	}
	AAIController* AI = Cast<AAIController>(GetController());
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!AI || !Movement)
	{
		Stop();
		return false;
	}

	Movement->Activate();
	bNavigationJumpFailed = false;
	bRequestAlreadyAtGoal = false;
	Movement->MaxWalkSpeed = FMath::Max(1.0f, SpeedCmPerSecond);
	RequestedFeet = FeetDestination;
	RequestedAcceptanceCm = FMath::Max(1.0f, AcceptanceRadiusCm);
	RequestedGaitKind = GaitKind;
	UPathFollowingComponent* Following = AI->GetPathFollowingComponent();
	// Bound BEFORE the request: the finishes this exists to catch happen inside `MoveToLocation`.
	BindMoveFinished(Following);
	const EPathFollowingRequestResult::Type Result = AI->MoveToLocation(
		FeetDestination, RequestedAcceptanceCm, /*bStopOnOverlap=*/false,
		/*bUsePathfinding=*/true, /*bProjectDestinationToNavigation=*/true,
		/*bCanStrafe=*/false, nullptr, bAllowPartialPath);
	if (Result == EPathFollowingRequestResult::Failed)
	{
		// `RequestMoveWithImmediateFinish(Invalid)` already wrote `Invalid[]` above; the engine
		// never says which of its three refusals it was, so ask the navmesh directly.
		const FString Why = DescribeRefusedRoute(*AI, FeetDestination);
		LastMoveResult += TEXT(" -- ") + Why;
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: move request to %s refused: %s"),
			*GetName(), *FeetDestination.ToString(), *Why);
		Stop();
		return false;
	}
	if (Result == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		// `AAIController::MoveTo` finished this one itself (`RequestMoveWithImmediateFinish(Success)`,
		// `AIController.cpp`): the follower is Idle and there is nothing to pause or resume. The
		// body stands inside the goal's reach, so the request is answered as arrived, not as lost.
		bMoveRequested = true;
		bRequestAlreadyAtGoal = true;
		return true;
	}
	// `RequestSuccessful` is the request's ID being valid, not the request being alive. The crowd
	// follower can end it synchronously inside `RequestMove` -- `UCrowdFollowingComponent::
	// SetMoveSegment` calls `OnPathFinished(Aborted, InvalidPath)` on an empty corridor, a missing
	// crowd manager or a nav-data mismatch -- and hands back an Idle follower under a successful
	// result. Reading that as "in flight" is how a route died within a frame with its whole
	// distance left and nothing said why; a dead request is a refused one, and the caller decides
	// what a refusal means (the beat's fallback today, a `TaskFail 0xc` under the schedule).
	if (Following != nullptr && Following->GetStatus() == EPathFollowingStatus::Idle)
	{
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("%s: move request to %s ended inside the request (%s); refused"),
			*GetName(), *FeetDestination.ToString(),
			LastMoveResult.IsEmpty() ? TEXT("no result reported") : *LastMoveResult);
		Stop();
		return false;
	}
	bMoveRequested = true;
	if (bHeld)
	{
		// A request made on a held body is accepted and parked, as a retail route laid on a body
		// whose think never reaches `PerformMovement`: it starts travelling on release.
		PauseFollowing();
	}
	return true;
}

FString AElysiumNpcBody::DescribeRefusedRoute(const AAIController& AI, const FVector& FeetDestination) const
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (NavSys == nullptr)
	{
		return TEXT("no navigation system");
	}
	const FNavAgentProperties& Agent = AI.GetNavAgentPropertiesRef();
	const ANavigationData* NavData = NavSys->GetNavDataForProps(Agent, AI.GetNavAgentLocation());
	if (NavData == nullptr)
	{
		return TEXT("no nav data for this agent");
	}
	FNavLocation GoalOnMesh;
	if (!NavSys->ProjectPointToNavigation(FeetDestination, GoalOnMesh, INVALID_NAVEXTENT, &Agent))
	{
		return TEXT("the goal is off the navmesh");
	}
	FNavLocation SelfOnMesh;
	if (!NavSys->ProjectPointToNavigation(AI.GetNavAgentLocation(), SelfOnMesh, INVALID_NAVEXTENT, &Agent))
	{
		return TEXT("this body is off the navmesh");
	}
	FPathFindingQuery Query(&AI, *NavData, SelfOnMesh.Location, GoalOnMesh.Location);
	Query.SetAllowPartialPaths(true);
	const FPathFindingResult Found = NavSys->FindPathSync(Agent, Query);
	if (!Found.IsSuccessful() || !Found.Path.IsValid())
	{
		return FString::Printf(TEXT("no path at all (query result %d)"), static_cast<int32>(Found.Result));
	}
	const FNavigationPath& Path = *Found.Path;
	if (Path.IsPartial())
	{
		const FVector End = Path.GetPathPoints().Num() > 0 ? Path.GetPathPoints().Last().Location : SelfOnMesh.Location;
		return FString::Printf(TEXT("the navmesh does not connect them: a partial path of %.0f cm ends %.0f cm from the goal"),
			Path.GetLength(), FVector::Dist(End, GoalOnMesh.Location));
	}
	return FString::Printf(TEXT("a full path of %.0f cm exists; the refusal came from elsewhere"), Path.GetLength());
}

void AElysiumNpcBody::BindMoveFinished(UPathFollowingComponent* Following)
{
	if (bMoveFinishedBound || Following == nullptr)
	{
		return;
	}
	Following->OnRequestFinished.AddUObject(this, &AElysiumNpcBody::OnMoveRequestFinished);
	bMoveFinishedBound = true;
}

void AElysiumNpcBody::OnMoveRequestFinished(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	const UWorld* World = GetWorld();
	LastMoveResult = FString::Printf(TEXT("%s (request %u, world time %.2fs)"), *Result.ToString(),
		RequestID.GetID(), World ? World->GetTimeSeconds() : 0.0f);
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: move request finished: %s"), *GetName(),
		*LastMoveResult);
}

void AElysiumNpcBody::Face(float YawDegrees)
{
	if (!bRuntimeReady || !bRequestedEnabled)
	{
		return;   // the caller reads Idle back from Sample and treats the facing as already settled
	}
	RequestedYaw = FRotator::ClampAxis(YawDegrees);
	bFaceRequested = true;
}

void AElysiumNpcBody::Stop()
{
	ResetNavigationJump();
	NavigationType = EElysiumNpcNavType::Ground;
	bNavigationJumpFailed = false;
	// A launch ends here too. Freeze, teleport and the disable path all funnel through
	// `Stop()`, so clearing the recording flag once here covers every one of them.
	EndLaunchRecording();
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->Deactivate();
	}
	bMoveRequested = false;
	bRequestAlreadyAtGoal = false;
	bFaceRequested = false;
	// The leg is over; there is nothing left to re-derive against a later fan change.
	RequestedGaitKind.Reset();
	// `GroundSurface` is deliberately NOT cleared here. Retail's `+0x5b90` survives an arrival, a
	// freeze and a teleport — it is cleared only by `NPCInit 0x10273390` and `OnRestore 0x1027bf50`
	// (`InitializeAtFeet`) and rewritten by the next move step — so a footfall record landing on a
	// body in its blend-out still sounds on the floor it stopped on.
}

void AElysiumNpcBody::Teleport(const FVector& FeetOrigin, float YawDegrees)
{
	Stop();
	const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0f;
	SetActorLocationAndRotation(FeetOrigin + FVector(0.0f, 0.0f, HalfHeight),
		FRotator(0.0f, YawDegrees, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
}

void AElysiumNpcBody::SetEnabled(bool bEnabled)
{
	bRequestedEnabled = bEnabled;
	ApplyEnabledState();
}

void AElysiumNpcBody::SetHeld(bool bInHeld)
{
	if (bHeld == bInHeld)
	{
		return;
	}
	bHeld = bInHeld;
	if (bHeld)
	{
		PauseFollowing();
	}
	else
	{
		ResumeFollowing();
	}
}

void AElysiumNpcBody::PauseFollowing()
{
	// The request is KEPT: `PauseMove` parks the follower on its current path segment (and the
	// crowd component parks the Detour agent with it, so a held pedestrian stays an obstacle the
	// others path around) and `ResumeMove` picks the same request up where it stood. `Stop()`
	// would abort it, which is the scene's freeze, not the think's silence.
	AAIController* AI = Cast<AAIController>(GetController());
	UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	if (Following == nullptr)
	{
		return;
	}
	const EPathFollowingStatus::Type Status = Following->GetStatus();
	if (Status != EPathFollowingStatus::Moving && Status != EPathFollowingStatus::Waiting)
	{
		return;
	}
	// Retail's body simply does not move on the frame its think declines, so the velocity is
	// zeroed outright -- through `PauseMove(Reset)`, whose `StopMovementKeepPathing` is the ONE
	// way to zero a character's velocity without losing the request. `StopMovementImmediately`
	// is not: it runs `UNavMovementComponent::StopActiveMovement`, which under
	// `bStopMovementAbortPaths` (default true) aborts the outstanding request with
	// `MovementStop`, and a paused request is not Idle, so it dies (`Aborted[UserAbort
	// MovementStop]` in the follower's own report; sp_tutorial_1's thug_1 under `AIEnable 0`,
	// 2026-09-12 -- the walk-out failure the old sp_theatre run showed). Only on the ground: a
	// launched or jumping body is airborne on engine physics that retail's think does not own
	// either, and zeroing it mid-arc would hang it in the air, so its velocity is kept. It lands,
	// and `FinishNavigationJump` re-pauses a resumed follower.
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const bool bOnGround = Movement != nullptr && Movement->IsMovingOnGround();
	Following->PauseMove(FAIRequestID::CurrentRequest,
		bOnGround ? EPathFollowingVelocityMode::Reset : EPathFollowingVelocityMode::Keep);
}

void AElysiumNpcBody::ResumeFollowing()
{
	AAIController* AI = Cast<AAIController>(GetController());
	UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	if (Following != nullptr && Following->GetStatus() == EPathFollowingStatus::Paused)
	{
		// The body did not move while held, so it is still on its path and the follower resumes
		// the same request rather than re-planning one.
		Following->ResumeMove(FAIRequestID::CurrentRequest);
	}
}

void AElysiumNpcBody::SetAnimationHeld(bool bInHeld)
{
	if (bAnimationHeld == bInHeld)
	{
		return;
	}
	bAnimationHeld = bInHeld;
	// The clock, not the evaluation. `GlobalAnimRateScale` scales the delta `UpdateAnimation`
	// hands the graph and its montages, and a zero delta still runs the update -- so the cinematic
	// proxy's explicit seeks (`FElysiumBipedAnimProxy::Seek`/`PlayDirect`, which a choreo scene and
	// a dialogue drive per frame on a cast that IS AI-disabled) keep landing. `bPauseAnims`, the
	// corpse's `HoldBodyFinalPose`, would stop the evaluation itself and break that path. A
	// one-shot under a stopped clock reports "still playing" and completes on release, which is
	// retail's own answer for a sequence the think stopped advancing.
	if (USkeletalMeshComponent* Body = Visual.Get())
	{
		Body->GlobalAnimRateScale = bAnimationHeld ? 0.0f : 1.0f;
	}
}

void AElysiumNpcBody::SetFrozen(bool bInFrozen)
{
	if (bFrozen == bInFrozen)
	{
		return;
	}
	bFrozen = bInFrozen;
	if (bFrozen)
	{
		Stop();   // drop the outstanding request before the body stops simulating
	}
	ApplyEnabledState();
	// After the stop, not before: the crowd register only accepts a state change from an idle
	// agent, and the abort above is what makes this one idle.
	ApplyCrowdState();
}

void AElysiumNpcBody::SetIgnoreCharacterCollision(bool bIgnore)
{
	if (bIgnoreCharacterCollision == bIgnore)
	{
		return;
	}
	bIgnoreCharacterCollision = bIgnore;
	ApplyCollisionState();
	ApplyCrowdState();
}

void AElysiumNpcBody::ApplyCrowdState()
{
	// Blocking is only half of it: a crowd agent steers around its neighbours long before it
	// touches them, which is the jam the flag exists to remove. Separation is per-agent state on
	// the follower, so it is set here and not through the movement component. The controller is
	// spawned lazily by the first accepted MoveTo, so this runs from there as well as from the
	// setter — whichever happens second is the one that sticks.
	const AAIController* AI = Cast<AAIController>(GetController());
	UCrowdFollowingComponent* Crowd = AI
		? Cast<UCrowdFollowingComponent>(AI->GetPathFollowingComponent()) : nullptr;
	if (!Crowd)
	{
		return;
	}
	Crowd->SetCrowdSeparation(!bIgnoreCharacterCollision);
	Crowd->SetCrowdCollisionQueryRange(bIgnoreCharacterCollision ? 0.0f : 200.0f);
	// A frozen body is SOLID_NONE — a character walks through it — so it must not steer other
	// agents around it either. Detour avoidance is a separate register from collision, and an
	// immobilised body left in it is an invisible obstacle every neighbour paths around.
	Crowd->SetCrowdSimulationState(bFrozen
		? ECrowdSimulationState::Disabled : ECrowdSimulationState::Enabled);
}

void AElysiumNpcBody::ApplyCollisionState()
{
	const bool bEnabled = bRuntimeReady && bRequestedEnabled;
	// A frozen body is non-solid outright — VtMB's SOLID_NONE + FSOLID_NOT_SOLID.
	SetActorEnableCollision(bEnabled && !bFrozen);
	// An ignoring body keeps its world collision and stops answering only the pawn channel, which
	// is what CBaseAnimating::IsIgnoreCollisionEntity returning true for NPCs and the player means.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn,
			bIgnoreCharacterCollision ? ECR_Ignore : ECR_Block);
	}
}

void AElysiumNpcBody::ApplyEnabledState()
{
	const bool bEnabled = bRuntimeReady && bRequestedEnabled;
	// Frozen deliberately does NOT hide: a scene's cast is immobilised while staying on camera.
	SetActorHiddenInGame(!bEnabled);
	ApplyCollisionState();
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		if (!bEnabled || bFrozen)
		{
			Stop();
		}
		else if (bMoveRequested || bNavigationJumpInProgress)
		{
			Movement->Activate();
		}
		else
		{
			// Authored NPC origins are approximate feet positions. Sleeping idle movement leaves
			// those raw positions visibly above or below collision. The entity's enabled transition
			// occurs just after the
			// map activation barrier, when collision is ready and this capsule has become queryable.
			// Perform CharacterMovement's own capsule-aware floor query and swept height adjustment
			// once here, then leave it asleep without spawning a controller or crowd agent.
			FFindFloorResult Floor;
			Movement->FindFloor(GetActorLocation(), Floor, /*bCanUseCachedLocation=*/false);
			if (Floor.IsWalkableFloor())
			{
				Movement->CurrentFloor = Floor;
				Movement->AdjustFloorHeight();
				Movement->StopMovementImmediately();
				// And the mode that says so. `MovementMode` is zero-initialised to `MOVE_None` and only
				// ever becomes `MOVE_Walking` when a controller possesses the character — which happens
				// on the body's FIRST accepted travel request and never for a body that only ever
				// stands. `IsMovingOnGround()` reads the mode alone, so without this a body standing on
				// the floor the query above just proved reports itself airborne for its whole life, and
				// the animation latch pins it at `ACT_FALLING`. The floor result is the licence: a body
				// with nothing walkable under it keeps `MOVE_None` and honestly reads as not grounded.
				Movement->SetMovementMode(MOVE_Walking);
			}
			Movement->Deactivate();
		}
	}
}

void AElysiumNpcBody::RefreshGroundSurface()
{
	const UWorld* World = GetWorld();
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (World == nullptr || Capsule == nullptr)
	{
		GroundSurface = FName();
		return;
	}

	// From the capsule's centre, past its bottom, by the same 2u the player's ground trace reaches
	// below its hull — CharacterMovement floats the capsule a contact offset above the floor, so a
	// segment that stopped at the capsule bottom would end in the gap. The capsule wears the `Pawn`
	// profile and this channel's default response is Ignore, so the body cannot trace itself; the
	// actor passed to the query is belt and braces for the visual and anything parented to it.
	const float DepthCm = Capsule->GetScaledCapsuleHalfHeight()
		+ 2.0f * ElysiumMove::U + ElysiumMove::DistEpsilon;
	const FName Rendered = ElysiumGroundSurface::TraceBelow(World, GetActorLocation(), DepthCm, this);
	if (!Rendered.IsNone())
	{
		GroundSurface = Rendered;
		return;
	}

	// Nothing DRAWN under the body. That is not the same as nothing under it: a brush entity's
	// hull, a lift platform and a test slab are all standable and none of them is on the
	// render-surface channel. Ask the movement component for the floor itself — fresh, because a
	// teleported body's `CurrentFloor` and `MovementMode` still describe where it came from — and
	// answer index 0 for a floor with no drawn half, `NAME_None` only for a body standing on
	// nothing, which is retail's null `surfacedata_t` and a silent step.
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Movement == nullptr)
	{
		GroundSurface = FName();
		return;
	}
	FFindFloorResult Floor;
	Movement->FindFloor(GetActorLocation(), Floor, /*bCanUseCachedLocation=*/false);
	GroundSurface = Floor.IsWalkableFloor() ? ElysiumGroundSurface::DefaultSurface() : FName();
}

FElysiumLocomotionSample AElysiumNpcBody::SampleLocomotion() const
{
	// An NPC faces where its actor is turned — `bOrientRotationToMovement` keeps that pointed along
	// the path — where a player body faces where the view points.
	FElysiumLocomotionSample Out = ElysiumLocomotion::FromCharacterMovement(*this,
		static_cast<float>(GetActorRotation().Yaw));

	// **What this body was commanded.** A cast body's command stream is its travel order:
	// the motor is handed one number per leg and re-handed the cell it is about to play every frame
	// after that, and `MaxWalkSpeed` is where that number lives. Publishing it makes the cast's gait
	// test the same disjunction the player's is — retail tests the realized speed *or* the commanded
	// one — so a body ordered to run is running on its first moving frame rather than after it has
	// accelerated past the split, which is what an order-driven producer means by "run".
	//
	// Only while a leg is in flight: `MaxWalkSpeed` keeps its last value after a stop, and a body
	// standing still has commanded nothing.
	//
	// And not while held: the request is parked, and a commanded speed on a body that is not
	// travelling would have the driver classify it as moving and publish a gait over the idle the
	// refused think just asked for.
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	Out.CommandedSpeed = (bMoveRequested && !bHeld && Movement != nullptr)
		? Movement->MaxWalkSpeed : 0.0f;

	// **The cache, not a trace.** `FromCharacterMovement` cannot fill this — the engine's floor
	// sweeps run without `bReturnPhysicalMaterial` — and this call is a getter that a think and an
	// animation pass may both make on the same frame, so the world query lives on the once-per-move
	// `RefreshGroundSurface` above and this only publishes what it left. `NAME_None` on a body that
	// has never travelled is retail's own answer: `+0x5b90` is written by `MoveEnact` and by
	// nothing else.
	Out.GroundSurface = GroundSurface;
	return Out;
}

void AElysiumNpcBody::SampleTransform(FVector& OutFeetOrigin, float& OutYawDegrees) const
{
	OutFeetOrigin = FeetLocation();
	OutYawDegrees = GetActorRotation().Yaw;
}

EElysiumNpcMoveStatus AElysiumNpcBody::Sample(FVector& OutFeetOrigin, float& OutYawDegrees)
{
	OutFeetOrigin = FeetLocation();
	OutYawDegrees = GetActorRotation().Yaw;
	if (bNavigationJumpFailed)
	{
		Stop();
		return EElysiumNpcMoveStatus::Failed;
	}
	// Crossing the final goal's XY tolerance while airborne is not arrival. The smart link
	// must first finish its capsule flight and return control to the normal path follower.
	if (bNavigationJumpInProgress) return EElysiumNpcMoveStatus::Moving;
	if (!bMoveRequested)
	{
		return bFaceRequested ? EElysiumNpcMoveStatus::Moving : EElysiumNpcMoveStatus::Idle;
	}
	if (bRequestAlreadyAtGoal)
	{
		// The engine finished this request inside `MoveToLocation`; the body was already there.
		Stop();
		return EElysiumNpcMoveStatus::Reached;
	}

	const float Horizontal = FVector::Dist2D(OutFeetOrigin, RequestedFeet);
	const float Vertical = FMath::Abs(OutFeetOrigin.Z - RequestedFeet.Z);
	if (Horizontal <= RequestedAcceptanceCm + 4.0f && Vertical <= 96.0f)
	{
		Stop();
		return EElysiumNpcMoveStatus::Reached;
	}

	const AAIController* AI = Cast<AAIController>(GetController());
	const UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	if (!Following)
	{
		Stop();
		return EElysiumNpcMoveStatus::Unavailable;
	}
	const EPathFollowingStatus::Type Status = Following->GetStatus();
	if (Status == EPathFollowingStatus::Moving || Status == EPathFollowingStatus::Waiting
		|| Status == EPathFollowingStatus::Paused)
	{
		return EElysiumNpcMoveStatus::Moving;
	}
	Stop();
	return EElysiumNpcMoveStatus::Failed;
}

bool AElysiumNpcBody::Launch(const FVector& VelocityCmPerSecond)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!bRuntimeReady || !bRequestedEnabled || bFrozen || Movement == nullptr)
	{
		// A body that is not standing in the world cannot be thrown across it. The caller ends its
		// chain on false rather than waiting for a landing.
		return false;
	}

	// Whatever this body was doing, it is not doing it any more: a launched body follows no path.
	// `Stop()` also deactivates the component and ends any recording launch, so the order matters —
	// reactivate and re-arm after it.
	Stop();

	// **The component is `SetAutoActivate(false)` and `Stop()` deactivates it**, so a launch has to
	// wake it explicitly. Without this the velocity below is assigned to a component that never
	// integrates it and the body stands still while its cell plays.
	Movement->Activate();
	// And the mode has to be set outright. `MovementMode` only ever becomes `MOVE_Walking` through
	// `ApplyEnabledState`'s floor settle, and `IsMovingOnGround()` reads the mode alone — so a body
	// left in `MOVE_Walking` would be integrated as a walker and report itself grounded for the
	// whole flight, which is exactly the terminator's own condition.
	Movement->SetMovementMode(MOVE_Falling);
	// The assignment itself. Not `AddImpulse`, not `LaunchCharacter`'s additive form: retail assigns
	// the velocity outright and whatever the body carried is discarded.
	Movement->Velocity = VelocityCmPerSecond;

	bLaunched = true;
	bBallisticContacted = false;
	BallisticContactNormal = FVector::ZeroVector;
	return true;
}

bool AElysiumNpcBody::SampleBallistic(FElysiumBallisticSample& Out) const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Movement == nullptr)
	{
		return false;
	}
	Out = FElysiumBallisticSample();
	Out.bGrounded = Movement->IsMovingOnGround();
	Out.bFalling = Movement->MovementMode == MOVE_Falling;
	Out.VelocityCmPerSecond = Movement->Velocity;
	// Consumed on read: the chain asks once per think, and a normal left standing would divert it
	// again on a wall it has already rebounded from.
	Out.bContacted = bBallisticContacted;
	Out.ContactNormal = BallisticContactNormal;
	bBallisticContacted = false;
	BallisticContactNormal = FVector::ZeroVector;
	return true;
}

FElysiumNpcNavigationSample AElysiumNpcBody::SampleNavigation() const
{
	FElysiumNpcNavigationSample Sample;
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	Sample.bActiveGoal = bMoveRequested;
	Sample.Type = NavigationType;
	if (Movement)
	{
		Sample.bGrounded = Movement->IsMovingOnGround();
		Sample.VelocityCmPerSecond = Movement->Velocity;
	}
	// A launched reaction has no navigation goal; only an authored AIN link supplies Jump.
	return Sample;
}

void AElysiumNpcBody::ClearNavigationGoal()
{
	if (NavigationType != EElysiumNpcNavType::Jump && NavigationType != EElysiumNpcNavType::Climb)
	{
		Stop();
		return;
	}
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		if (UPathFollowingComponent* Following = AI->GetPathFollowingComponent())
			Following->AbortMove(*this, FPathFollowingResultFlags::ForcedScript,
				FAIRequestID::CurrentRequest, EPathFollowingVelocityMode::Keep);
	}
	bMoveRequested = false;
	bFaceRequested = false;
	RequestedGaitKind.Reset();
}

void AElysiumNpcBody::ResetSteering()
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->RotationRate.Yaw = 180.f; // Troika TaskFail -> motor 0x102e0a60(180)
	}
}


void AElysiumNpcBody::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	// ACharacter::Landed is called before CharacterMovement changes its falling mode. Defer
	// resuming the follower until the next motor service, after the floor/mode are settled.
	bNavigationJumpLanded = bNavigationJumpInProgress;
	// The flight is over as far as the ENGINE is concerned. Whether the CHAIN is over is the
	// substrate's rule, read off the next `SampleBallistic`; all this does is stop recording
	// contacts for a launch that is no longer in the air.
	EndLaunchRecording();
}

void AElysiumNpcBody::EndLaunchRecording()
{
	// **Every path that ends a flight clears this, not just a landing.** A chain can end without a
	// `Landed` — the body is stopped, teleported, frozen or disabled — and a `bLaunched` left set
	// would have `NotifyHit` recording wall normals off ordinary walking contacts, which is exactly
	// the backchannel the flag exists to prevent.
	bLaunched = false;
	bBallisticContacted = false;
	BallisticContactNormal = FVector::ZeroVector;
}

void AElysiumNpcBody::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other,
	UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal,
	FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse,
		Hit);
	// Only while carrying a launch. An ordinary walking body brushes geometry constantly, and a
	// normal recorded from one of those would divert the next chain that ran on this body.
	if (bLaunched)
	{
		bBallisticContacted = true;
		BallisticContactNormal = HitNormal;
	}
	// The player's touch handler `0x10147690`'s input, recorded and polled rather than pushed.
	// `DispatchBlockingHit` reaches this for a swept blocking hit in BOTH directions -- the
	// player's hull moving into this capsule (`bSelfMoved` false) and this body moving into the
	// hull (`bSelfMoved` true) -- which is also both directions retail's `PhysicsImpact` touches.
	// Ungated: retail touches whether or not the body is launched.
	if (Other != nullptr && Other->IsA<AElysiumPawn>())
	{
		bPlayerContactPending = true;
	}
}

bool AElysiumNpcBody::ConsumePlayerContact()
{
	const bool bPending = bPlayerContactPending;
	bPlayerContactPending = false;
	return bPending;
}

bool AElysiumNpcBody::ProjectToNavigable(const FVector& PointCm, FVector& OutProjectedCm) const
{
	// The same Recast graph `MoveTo` paths over, asked the narrower question. Extent and nav data
	// are left at the navigation system's own defaults — which is exactly what
	// `MoveToLocation`'s `bProjectDestinationToNavigation` uses — so a point this refuses is a point
	// the move request would have refused too.
	const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
	if (Nav == nullptr)
	{
		return false;   // no navigation behind this body: unprojectable, and the caller must fail
	}
	FNavLocation Projected;
	if (!Nav->ProjectPointToNavigation(PointCm, Projected))
	{
		return false;
	}
	OutProjectedCm = Projected.Location;
	return true;
}
