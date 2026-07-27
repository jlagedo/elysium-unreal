#include "ElysiumCapsulePawn.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "ElysiumUserCmd.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

AElysiumCapsulePawn::AElysiumCapsulePawn()
{
	PrimaryActorTick.bCanEverTick = false;

	// Source player hull ~ 32u wide x 72u tall -> radius 40.6cm, half-height 91.4cm.
	GetCapsuleComponent()->InitCapsuleSize(40.6f, 91.4f);
	GetCapsuleComponent()->SetGenerateOverlapEvents(true);

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	// Eye at 64u above the feet; capsule centre is 91.4cm up, so offset +71.2cm.
	Camera->SetRelativeLocation(FVector(0.0f, 0.0f, 71.2f));
	Camera->bUsePawnControlRotation = true;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = RunSpeed;
	Move->MaxAcceleration = 8192.0f;
	Move->BrakingDecelerationWalking = 8192.0f;
	Move->GroundFriction = 8.0f;
	Move->MaxStepHeight = 46.0f;                 // Source sv_stepsize 18u
	Move->SetWalkableFloorAngle(50.0f);
	Move->JumpZVelocity = 340.0f;
	Move->AirControl = 0.6f;
	Move->GravityScale = 1.0f;
	Move->MaxFlySpeed = NoclipSpeed;
	Move->BrakingDecelerationFlying = 8192.0f;
}

void AElysiumCapsulePawn::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->ViewPitchMin = -89.0f;
			PC->PlayerCameraManager->ViewPitchMax = 89.0f;
		}
	}
}

void AElysiumCapsulePawn::ApplyGait(bool bWalk)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (bNoclip)
	{
		Move->MaxFlySpeed = NoclipSpeed * (bWalk ? NoclipBoost : 1.0f);
	}
	else
	{
		Move->MaxWalkSpeed = bWalk ? WalkSpeed : RunSpeed;
	}
}

void AElysiumCapsulePawn::SetNoclip(bool bEnable)
{
	bNoclip = bEnable;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	SetActorEnableCollision(!bNoclip);
	Move->SetMovementMode(bNoclip ? MOVE_Flying : MOVE_Walking);
	Move->Velocity = FVector::ZeroVector;
	ApplyGait(false);
}

void AElysiumCapsulePawn::SetMovementFrozen(bool bFrozen)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->Velocity = FVector::ZeroVector;
	Move->SetMovementMode(bFrozen ? MOVE_None : (bNoclip ? MOVE_Flying : MOVE_Walking));
}

void AElysiumCapsulePawn::ApplyUserCmd(const FElysiumUserCmd& Cmd)
{
	// `+speed` selects the slow gait; the run is the default.
	ApplyGait(Cmd.IsDown(EElysiumButton::Speed));

	const FRotator ViewRot = GetControlRotation();
	const FRotator Frame = bNoclip ? ViewRot : FRotator(0.0f, ViewRot.Yaw, 0.0f);
	const FRotationMatrix Basis(Frame);

	if (Cmd.Move.X != 0.0f)
	{
		AddMovementInput(Basis.GetUnitAxis(EAxis::X), Cmd.Move.X);
	}
	if (Cmd.Move.Y != 0.0f)
	{
		AddMovementInput(Basis.GetUnitAxis(EAxis::Y), Cmd.Move.Y);
	}
	// Vertical strafe only exists in noclip: on the ground `+moveup` is a swim/ladder verb with
	// nothing to act on.
	if (bNoclip && Cmd.Up != 0.0f)
	{
		AddMovementInput(FVector::UpVector, Cmd.Up);
	}

	const bool bJumpDown = Cmd.IsDown(EElysiumButton::Jump);
	if (bJumpDown && !bWasJumpDown && !bNoclip)
	{
		Jump();
	}
	else if (!bJumpDown && bWasJumpDown)
	{
		StopJumping();
	}
	bWasJumpDown = bJumpDown;
}
