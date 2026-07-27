#include "ElysiumPawn.h"

#include "Components/BoxComponent.h"
#include "ElysiumCameraComponent.h"
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
	Hull->InitBoxExtent(FVector(ElysiumMove::HullHalfWidth, ElysiumMove::HullHalfWidth,
		ElysiumMove::StandHeight * 0.5f));
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

	Camera = CreateDefaultSubobject<UElysiumCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Hull);
	// Eye at 64u above the feet; the box centre is 36u up, so the offset is +28u. In third person the
	// boom hangs off this same point — there is one camera and one weight, never a second actor.
	Camera->SetRelativeLocation(FVector(0.0f, 0.0f,
		ElysiumMove::StandViewZ - ElysiumMove::StandHeight * 0.5f));
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
	return Hull ? Hull->GetUnscaledBoxExtent().Z : ElysiumMove::StandHeight * 0.5f;
}

void AElysiumPawn::SetHullHeight(float HeightCm, float EyeAboveFeetCm, bool bAnchorFeet)
{
	if (!Hull)
	{
		return;
	}

	const float OldHalf = Hull->GetUnscaledBoxExtent().Z;
	const float NewHalf = HeightCm * 0.5f;
	if (FMath::IsNearlyEqual(OldHalf, NewHalf))
	{
		return;
	}

	// Source's player origin sits at the **feet** (its hull mins are 0); ours is the box centre, so
	// resizing has to move the actor or the body would grow/shrink about its middle. On the ground
	// the feet are what must stay planted; in the air it is the head, or an unduck would drive the
	// body down through whatever is beneath it.
	const float Shift = bAnchorFeet ? (NewHalf - OldHalf) : (OldHalf - NewHalf);

	Hull->SetBoxExtent(FVector(ElysiumMove::HullHalfWidth, ElysiumMove::HullHalfWidth, NewHalf),
		/*bUpdateOverlaps*/ true);
	AddActorWorldOffset(FVector(0.0f, 0.0f, Shift), /*bSweep*/ false, nullptr,
		ETeleportType::TeleportPhysics);

	if (Camera)
	{
		// The view offset is measured from the feet, so it re-bases onto the new centre.
		Camera->SetRelativeLocation(FVector(0.0f, 0.0f, EyeAboveFeetCm - NewHalf));
	}
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
	if (Camera)
	{
		// The orbit and dolly pairs are latches in the same frame's command; nothing polls a key.
		Camera->SetUserCmd(Cmd);
	}
}

void AElysiumPawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (UElysiumCameraComponent::CalcCameraFor(Camera, DeltaTime, OutResult))
	{
		return;
	}
	Super::CalcCamera(DeltaTime, OutResult);
}
