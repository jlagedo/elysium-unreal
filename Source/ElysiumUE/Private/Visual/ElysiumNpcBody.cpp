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
#include "ElysiumMapActor.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcGait.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Visual/ElysiumBipedAnimInstance.h"
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
	// CCC4 — the animation pass runs after this body's own movement has produced the frame's final
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
	EnsureAnimDriver();
	// A model swap is a new body: the latch, the last request and the previous model's tables all go.
	AnimDriver->Reset();

	// A clip already playing on the visual predates this driver — `BuildNpcVisual` arms the
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

	// **The speed authority's push, this body's half** (LIFE3, mirroring the player's push in
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
	static const FElysiumAnimationSelection Empty;
	return AnimDriver.IsValid() ? AnimDriver->Selection : Empty;
}

FVector AElysiumNpcBody::FeetLocation() const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	return GetActorLocation() - FVector(0.0f, 0.0f, Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.0f);
}

void AElysiumNpcBody::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
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
	if (!bRuntimeReady || !bRequestedEnabled || bFrozen)
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
	Movement->MaxWalkSpeed = FMath::Max(1.0f, SpeedCmPerSecond);
	RequestedFeet = FeetDestination;
	RequestedAcceptanceCm = FMath::Max(1.0f, AcceptanceRadiusCm);
	RequestedGaitKind = GaitKind;
	const EPathFollowingRequestResult::Type Result = AI->MoveToLocation(
		FeetDestination, RequestedAcceptanceCm, /*bStopOnOverlap=*/false,
		/*bUsePathfinding=*/true, /*bProjectDestinationToNavigation=*/true,
		/*bCanStrafe=*/false, nullptr, bAllowPartialPath);
	bMoveRequested = Result != EPathFollowingRequestResult::Failed;
	if (!bMoveRequested)
	{
		Stop();
	}
	return bMoveRequested;
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
	bFaceRequested = false;
	// The leg is over; there is nothing left to re-derive against a later fan change.
	RequestedGaitKind.Reset();
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
		else if (bMoveRequested)
		{
			Movement->Activate();
		}
		else
		{
			// Authored NPC origins are approximate feet positions. Active CharacterMovement used to
			// settle every body under gravity, but sleeping idle movement leaves those raw positions
			// visibly above or below collision. The entity's enabled transition occurs just after the
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

FElysiumLocomotionSample AElysiumNpcBody::SampleLocomotion() const
{
	// An NPC faces where its actor is turned — `bOrientRotationToMovement` keeps that pointed along
	// the path — where a player body faces where the view points.
	FElysiumLocomotionSample Out = ElysiumLocomotion::FromCharacterMovement(*this,
		static_cast<float>(GetActorRotation().Yaw));

	// **What this body was commanded** (LIFE3). A cast body's command stream is its travel order:
	// the motor is handed one number per leg and re-handed the cell it is about to play every frame
	// after that, and `MaxWalkSpeed` is where that number lives. Publishing it makes the cast's gait
	// test the same disjunction the player's is — retail tests the realized speed *or* the commanded
	// one — so a body ordered to run is running on its first moving frame rather than after it has
	// accelerated past the split, which is what an order-driven producer means by "run".
	//
	// Only while a leg is in flight: `MaxWalkSpeed` keeps its last value after a stop, and a body
	// standing still has commanded nothing.
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	Out.CommandedSpeed = (bMoveRequested && Movement != nullptr) ? Movement->MaxWalkSpeed : 0.0f;
	return Out;
}

EElysiumNpcMoveStatus AElysiumNpcBody::Sample(FVector& OutFeetOrigin, float& OutYawDegrees)
{
	OutFeetOrigin = FeetLocation();
	OutYawDegrees = GetActorRotation().Yaw;
	if (!bMoveRequested)
	{
		return bFaceRequested ? EElysiumNpcMoveStatus::Moving : EElysiumNpcMoveStatus::Idle;
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
