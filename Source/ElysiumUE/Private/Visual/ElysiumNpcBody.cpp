#include "Visual/ElysiumNpcBody.h"

#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DetourCrowdAIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"

AElysiumNpcBody::AElysiumNpcBody(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AIControllerClass = ADetourCrowdAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
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

	// The ACharacter mesh is unused; the runtime-loaded glTF component is attached by the map actor.
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AElysiumNpcBody::InitializeAtFeet(const FVector& FeetOrigin, float YawDegrees)
{
	Teleport(FeetOrigin, YawDegrees);
	SpawnDefaultController();
	ApplyEnabledState();
}

void AElysiumNpcBody::SetRuntimeReady(bool bReady)
{
	bRuntimeReady = bReady;
	ApplyEnabledState();
}

FVector AElysiumNpcBody::FeetLocation() const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	return GetActorLocation() - FVector(0.0f, 0.0f, Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.0f);
}

bool AElysiumNpcBody::MoveTo(const FVector& FeetDestination, float AcceptanceRadiusCm,
	float SpeedCmPerSecond)
{
	AAIController* AI = Cast<AAIController>(GetController());
	if (!AI || !GetCharacterMovement() || !GetCharacterMovement()->IsActive())
	{
		return false;
	}

	GetCharacterMovement()->MaxWalkSpeed = FMath::Max(1.0f, SpeedCmPerSecond);
	RequestedFeet = FeetDestination;
	RequestedAcceptanceCm = FMath::Max(1.0f, AcceptanceRadiusCm);
	const EPathFollowingRequestResult::Type Result = AI->MoveToLocation(
		FeetDestination, RequestedAcceptanceCm, /*bStopOnOverlap=*/false,
		/*bUsePathfinding=*/true, /*bProjectDestinationToNavigation=*/true,
		/*bCanStrafe=*/false, nullptr, /*bAllowPartialPath=*/false);
	bMoveRequested = Result != EPathFollowingRequestResult::Failed;
	return bMoveRequested;
}

void AElysiumNpcBody::Stop()
{
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
	}
	bMoveRequested = false;
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

void AElysiumNpcBody::ApplyEnabledState()
{
	const bool bEnabled = bRuntimeReady && bRequestedEnabled;
	SetActorHiddenInGame(!bEnabled);
	SetActorEnableCollision(bEnabled);
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		if (bEnabled)
		{
			Movement->Activate();
		}
		else
		{
			Stop();
			Movement->Deactivate();
		}
	}
}

EElysiumNpcMoveStatus AElysiumNpcBody::Sample(FVector& OutFeetOrigin, float& OutYawDegrees)
{
	OutFeetOrigin = FeetLocation();
	OutYawDegrees = GetActorRotation().Yaw;
	if (!bMoveRequested)
	{
		return EElysiumNpcMoveStatus::Idle;
	}

	const float Horizontal = FVector::Dist2D(OutFeetOrigin, RequestedFeet);
	const float Vertical = FMath::Abs(OutFeetOrigin.Z - RequestedFeet.Z);
	if (Horizontal <= RequestedAcceptanceCm + 4.0f && Vertical <= 96.0f)
	{
		bMoveRequested = false;
		return EElysiumNpcMoveStatus::Reached;
	}

	const AAIController* AI = Cast<AAIController>(GetController());
	const UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	if (!Following)
	{
		return EElysiumNpcMoveStatus::Unavailable;
	}
	const EPathFollowingStatus::Type Status = Following->GetStatus();
	if (Status == EPathFollowingStatus::Moving || Status == EPathFollowingStatus::Waiting
		|| Status == EPathFollowingStatus::Paused)
	{
		return EElysiumNpcMoveStatus::Moving;
	}
	bMoveRequested = false;
	return EElysiumNpcMoveStatus::Failed;
}
