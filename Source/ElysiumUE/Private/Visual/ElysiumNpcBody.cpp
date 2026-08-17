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

	// The body's authored speeds, resolved the moment it knows which model it wears rather than at
	// its first animation pass. A patrol or a scripted beat can issue its first travel request in the
	// same frame this body is built, and that request reads the tables through `GaitSpeed`.
	UElysiumAnimSubsystem* Anims = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	AnimDriver->RefreshGaitSpeeds(Anims, FString(), FString());
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

float AElysiumNpcBody::GaitSpeed(EElysiumNpcGaitKind Gait) const
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
	// The forward cell: `bOrientRotationToMovement` keeps a path-following body pointed along its
	// path, so its `move_yaw` is zero and no other cell can be the one it travels at.
	return Table->Forward();
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

	AnimDriver->Tick(DeltaSeconds, SampleLocomotion(), Anims,
		Body ? Body->GetSkeletalMeshAsset() : nullptr, OneShot);

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
	float SpeedCmPerSecond, bool bAllowPartialPath)
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
	return ElysiumLocomotion::FromCharacterMovement(*this,
		static_cast<float>(GetActorRotation().Yaw));
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
