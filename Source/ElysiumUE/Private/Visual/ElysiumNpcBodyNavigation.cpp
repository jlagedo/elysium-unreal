#include "Visual/ElysiumNpcBody.h"

#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/GameplayStaticsTypes.h"
#include "Navigation/NavLinkProxy.h"
#include "Navigation/PathFollowingComponent.h"
#include "Substrate/ElysiumNpcLog.h"

namespace
{
bool CapsuleArcIsClear(AElysiumNpcBody* Body, const FVector& Start, const FVector& Destination,
	const FVector& Velocity, float FlightTime)
{
	const UCharacterMovementComponent* Movement = Body->GetCharacterMovement();
	const UCapsuleComponent* Capsule = Body->GetCapsuleComponent();
	FPredictProjectilePathParams Params(0.f, Start, Velocity, FlightTime);
	Params.OverrideGravityZ = Movement->GetGravityZ();
	Params.SimFrequency = 30.f;
	FPredictProjectilePathResult Prediction;
	UGameplayStatics::PredictProjectilePath(Body, Params, Prediction);
	if (Prediction.PathData.Num() < 2) return false;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(ElysiumNavJumpProbe), false, Body);
	const FCollisionResponseParams Response(Capsule->GetCollisionResponseToChannels());
	for (int32 Point = 1; Point < Prediction.PathData.Num(); ++Point)
	{
		FHitResult Hit;
		if (!Body->GetWorld()->SweepSingleByChannel(Hit, Prediction.PathData[Point - 1].Location,
			Prediction.PathData[Point].Location, FQuat::Identity, Capsule->GetCollisionObjectType(),
			Capsule->GetCollisionShape(), Query, Response)) continue;
		// A floor contact at the landing point is the intended result of a jump. A wall,
		// ceiling, occupied departure, or earlier floor contact refuses it before nav Jump.
		const float Tolerance = Capsule->GetScaledCapsuleRadius() * 2.f + 4.f;
		return !Hit.bStartPenetrating && Hit.Normal.Z >= Movement->GetWalkableFloorZ()
			&& Prediction.PathData[Point].Velocity.Z <= 0.f
			&& FVector::Dist2D(Hit.Location, Destination) <= Tolerance
			&& FMath::Abs(Hit.Location.Z - Destination.Z) <= Movement->MaxStepHeight + 4.f;
	}
	return true;
}
}

bool AElysiumNpcBody::BeginNavigationJump(ANavLinkProxy* Link, const FVector& DestinationFeet)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!IsValid(Link) || !Movement || !bRuntimeReady || !bRequestedEnabled || bFrozen
		|| bLaunched || bNavigationJumpInProgress || !Movement->IsMovingOnGround()
		|| DestinationFeet.ContainsNaN() || Movement->GetGravityZ() >= -UE_SMALL_NUMBER)
		return false;

	const FVector Start = GetActorLocation();
	const FVector Destination = DestinationFeet + FVector(0, 0,
		GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	FVector Velocity;
	// Navigation/flight service modernization authorized by spec 0005: Unreal's own arc
	// solver and CharacterMovement integrate the capsule. No entity timer or task is advanced.
	if (!UGameplayStatics::SuggestProjectileVelocity_CustomArc(this, Velocity, Start,
		Destination, Movement->GetGravityZ()) || Velocity.ContainsNaN()
		|| Velocity.Size2D() <= UE_SMALL_NUMBER)
		return false;
	const float FlightTime = float(FVector::Dist2D(Start, Destination) / Velocity.Size2D());
	// Retail 0x102eece0 performs its directional move probe before SetNavType(Jump).
	// Unreal supplies the trajectory points and capsule queries at this service boundary.
	if (!CapsuleArcIsClear(this, Start, Destination, Velocity, FlightTime)) return false;

	NavigationJumpLink = Link;
	NavigationJumpDestination = DestinationFeet;
	bNavigationJumpInProgress = true;
	bNavigationJumpLanded = false;
	bNavigationJumpFailed = false;
	NavigationType = EElysiumNpcNavType::Jump; // retail MoveJump 0x102eece0 -> 0x102eeba0(1)
	Movement->Activate();
	// Discard the walker's last requested acceleration without aborting the smart link.
	// Detour's default bAffectFallingVelocity=false then leaves this flight to the capsule.
	Movement->StopMovementKeepPathing();
	Movement->ClearAccumulatedForces();
	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = Velocity;
	return true;
}

void AElysiumNpcBody::ResetNavigationJump()
{
	bNavigationJumpInProgress = false;
	bNavigationJumpLanded = false;
	NavigationJumpLink.Reset();
}

void AElysiumNpcBody::SetNavigationType(EElysiumNpcNavType Type)
{
	// STOP_MOVING's grounded/stuck arms explicitly set Ground before completion/TaskFail.
	// ClearNavigationGoal has already released the follower while retaining flight velocity.
	if (Type != EElysiumNpcNavType::Jump && bNavigationJumpInProgress)
		FinishNavigationJump(false);
	NavigationType = Type;
}

void AElysiumNpcBody::FinishNavigationJump(bool bSucceeded)
{
	ANavLinkProxy* Link = NavigationJumpLink.Get();
	ResetNavigationJump();
	NavigationType = EElysiumNpcNavType::Ground;
	AAIController* NavController = Cast<AAIController>(GetController());
	UPathFollowingComponent* Following = NavController ? NavController->GetPathFollowingComponent() : nullptr;
	if (!bSucceeded && bMoveRequested)
	{
		// Keep the request observable until Sample consumes Failed; clearing it here would
		// report Idle and turn a blocked jump into a successful movement task.
		bNavigationJumpFailed = true;
		if (Following) Following->AbortMove(*this, FPathFollowingResultFlags::InvalidPath,
			FAIRequestID::CurrentRequest, EPathFollowingVelocityMode::Keep);
	}
	else if (Link)
	{
		Link->ResumePathFollowing(this);
		if (bHeld)
		{
			// The landing resumed a follower on a body whose think is still silent; park it again
			// where it landed, as the hold would have on the ground.
			PauseFollowing();
		}
	}
}

void AElysiumNpcBody::ServiceNavigationJump()
{
	if (!bNavigationJumpInProgress) return;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!NavigationJumpLink.IsValid() || !Movement || !Movement->IsActive() || Movement->MovementMode == MOVE_None)
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("NPC '%s' lost its AIN jump link or movement service"), *GetName());
		FinishNavigationJump(false);
		return;
	}
	const AAIController* NavController = Cast<AAIController>(GetController());
	const UPathFollowingComponent* Following = NavController ? NavController->GetPathFollowingComponent() : nullptr;
	if (bMoveRequested && (!Following || Following->GetStatus() == EPathFollowingStatus::Idle))
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("NPC '%s' lost its path request during an AIN jump"), *GetName());
		FinishNavigationJump(false);
		return;
	}
	if (bNavigationJumpLanded || Movement->IsMovingOnGround())
	{
		const float Tolerance = GetCapsuleComponent()->GetScaledCapsuleRadius() * 2.f + 4.f;
		const FVector Delta = FeetLocation() - NavigationJumpDestination;
		const bool bReached = Delta.Size2D() <= Tolerance
			&& FMath::Abs(Delta.Z) <= Movement->MaxStepHeight + 4.f;
		// A STOP_MOVING task cleared its goal; landing anywhere now completes that stop.
		if (!bReached && bMoveRequested)
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("NPC '%s' landed short of its AIN jump endpoint by %s"),
				*GetName(), *Delta.ToCompactString());
		FinishNavigationJump(bReached || !bMoveRequested);
	}
	// There is no flight watchdog or zero-velocity terminator here. STOP_MOVING owns its
	// stuck-on-top arm and must observe airborne Jump + zero velocity on its own think.
}
