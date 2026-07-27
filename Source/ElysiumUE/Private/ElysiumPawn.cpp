#include "ElysiumPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "ElysiumMovementComponent.h"
#include "ElysiumUserCmd.h"
#include "GameFramework/PlayerController.h"

AElysiumPawn::AElysiumPawn()
{
	// Nothing to tick: intent arrives as a value and the movement component is what runs on it.
	PrimaryActorTick.bCanEverTick = false;

	// Source's player hull, 32 x 32 x 72 units -> 81.28 x 81.28 x 182.88 cm. The box extent is half
	// of each, and the component's origin is the box centre, so the feet sit at -91.44.
	Hull = CreateDefaultSubobject<UBoxComponent>(TEXT("Hull"));
	Hull->InitBoxExtent(FVector(16.0f * ElysiumMove::U, 16.0f * ElysiumMove::U, 36.0f * ElysiumMove::U));
	Hull->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	// The hull must raise overlaps so trigger brush bodies see the player begin/end touch. Its
	// Block-of-WorldDynamic is not a mutual block against a trigger's overlap response, so the
	// player passes through and the overlap fires rather than being stopped.
	Hull->SetGenerateOverlapEvents(true);
	RootComponent = Hull;

	// Body yaws with the controller; pitch is applied to the camera only.
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Hull);
	// Eye at 64u above the feet; the box centre is 36u up, so the offset is +28u.
	Camera->SetRelativeLocation(FVector(0.0f, 0.0f, 28.0f * ElysiumMove::U));
	Camera->bUsePawnControlRotation = true;

	Movement = CreateDefaultSubobject<UElysiumMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Hull;
}

UPawnMovementComponent* AElysiumPawn::GetMovementComponent() const
{
	return Movement;
}

void AElysiumPawn::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			// cl_pitchup / cl_pitchdown.
			PC->PlayerCameraManager->ViewPitchMin = -89.0f;
			PC->PlayerCameraManager->ViewPitchMax = 89.0f;
		}
	}
}

bool AElysiumPawn::IsNoclip() const
{
	return Movement && Movement->IsNoclip();
}

void AElysiumPawn::SetNoclip(bool bEnable)
{
	SetActorEnableCollision(!bEnable);
	if (Movement)
	{
		Movement->SetNoclip(bEnable);
	}
}

float AElysiumPawn::GetBodyHalfHeight() const
{
	return Hull ? Hull->GetUnscaledBoxExtent().Z : 36.0f * ElysiumMove::U;
}

void AElysiumPawn::SetMovementFrozen(bool bFrozen)
{
	if (Movement)
	{
		Movement->SetFrozen(bFrozen);
	}
}

void AElysiumPawn::ApplyUserCmd(const FElysiumUserCmd& Cmd)
{
	if (Movement)
	{
		Movement->SetUserCmd(Cmd);
	}
}
