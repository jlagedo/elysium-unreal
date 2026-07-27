#include "ElysiumPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

AElysiumPawn::AElysiumPawn()
{
	// Nothing to tick: the gait is a latch the bindings set, and the entity that holds the player's
	// state is sampled by the entity world's own tick (11.4).
	PrimaryActorTick.bCanEverTick = false;

	// Source player hull ~ 32u wide x 72u tall -> radius 40.6cm, half-height 91.4cm.
	GetCapsuleComponent()->InitCapsuleSize(40.6f, 91.4f);
	// The capsule must raise overlaps so trigger brush bodies (P1.5) see the player begin/end
	// touch. Its Block-of-WorldDynamic is not a mutual block against a trigger's overlap response,
	// so the player passes through and the overlap fires rather than being stopped.
	GetCapsuleComponent()->SetGenerateOverlapEvents(true);

	// Body yaws with the controller; pitch is applied to the camera only.
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	// Eye at 64u above the feet; capsule centre is 91.4cm up, so offset +71.2cm.
	Camera->SetRelativeLocation(FVector(0.f, 0.f, 71.2f));
	Camera->bUsePawnControlRotation = true;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = RunSpeed;
	Move->MaxAcceleration = 8192.f;
	Move->BrakingDecelerationWalking = 8192.f;
	Move->GroundFriction = 8.f;
	Move->MaxStepHeight = 46.f;                 // Source sv_stepsize 18u
	Move->SetWalkableFloorAngle(50.f);
	Move->JumpZVelocity = 340.f;
	Move->AirControl = 0.6f;
	Move->GravityScale = 1.f;
	Move->MaxFlySpeed = NoclipSpeed;
	Move->BrakingDecelerationFlying = 8192.f;
}

void AElysiumPawn::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->ViewPitchMin = -89.f;
			PC->PlayerCameraManager->ViewPitchMax = 89.f;
		}
	}
}

void AElysiumPawn::SetupPlayerInputComponent(UInputComponent* Input)
{
	Super::SetupPlayerInputComponent(Input);

	// Movement only. +use, the sign dismissal and the skybox toggle are the player controller's
	// (11.4) — a body does not decide what a key means.
	Input->BindAxis(TEXT("MoveForward"), this, &AElysiumPawn::MoveForward);
	Input->BindAxis(TEXT("MoveRight"), this, &AElysiumPawn::MoveRight);
	Input->BindAxis(TEXT("MoveUp"), this, &AElysiumPawn::MoveUp);
	Input->BindAxis(TEXT("Turn"), this, &AElysiumPawn::Turn);
	Input->BindAxis(TEXT("LookUp"), this, &AElysiumPawn::LookUp);

	Input->BindAction(TEXT("Jump"), IE_Pressed, this, &AElysiumPawn::OnJumpPressed);
	Input->BindAction(TEXT("Jump"), IE_Released, this, &AElysiumPawn::OnJumpReleased);
	Input->BindAction(TEXT("ToggleNoclip"), IE_Pressed, this, &AElysiumPawn::ToggleNoclip);

	// VtMB's `+speed` / `-speed` pair. Bound to the keys directly (the legacy mapping set has no
	// such action) so the gait is a latch rather than a per-frame IsInputKeyDown poll.
	for (const FKey& Shift : { EKeys::LeftShift, EKeys::RightShift })
	{
		Input->BindKey(Shift, IE_Pressed, this, &AElysiumPawn::OnWalkPressed);
		Input->BindKey(Shift, IE_Released, this, &AElysiumPawn::OnWalkReleased);
	}
}

void AElysiumPawn::OnWalkPressed()  { bWalkGait = true;  ApplyGait(); }
void AElysiumPawn::OnWalkReleased() { bWalkGait = false; ApplyGait(); }

void AElysiumPawn::ApplyGait()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (bNoclip)
	{
		Move->MaxFlySpeed = NoclipSpeed * (bWalkGait ? NoclipBoost : 1.f);
	}
	else
	{
		Move->MaxWalkSpeed = bWalkGait ? WalkSpeed : RunSpeed;
	}
}

void AElysiumPawn::MoveForward(float Value)
{
	if (Value == 0.f)
	{
		return;
	}
	// Noclip flies along the aim (includes pitch); walking moves on the ground plane.
	const FVector Dir = bNoclip
		? Camera->GetForwardVector()
		: FRotationMatrix(FRotator(0.f, GetControlRotation().Yaw, 0.f)).GetUnitAxis(EAxis::X);
	AddMovementInput(Dir, Value);
}

void AElysiumPawn::MoveRight(float Value)
{
	if (Value == 0.f)
	{
		return;
	}
	const FVector Dir = bNoclip
		? Camera->GetRightVector()
		: FRotationMatrix(FRotator(0.f, GetControlRotation().Yaw, 0.f)).GetUnitAxis(EAxis::Y);
	AddMovementInput(Dir, Value);
}

void AElysiumPawn::MoveUp(float Value)
{
	// Vertical strafe only exists in noclip (Space/E up, Ctrl/Q down).
	if (bNoclip && Value != 0.f)
	{
		AddMovementInput(FVector::UpVector, Value);
	}
}

void AElysiumPawn::Turn(float Value)
{
	AddControllerYawInput(Value);
}

void AElysiumPawn::LookUp(float Value)
{
	AddControllerPitchInput(Value);
}

void AElysiumPawn::OnJumpPressed()
{
	if (!bNoclip)
	{
		Jump();
	}
}

void AElysiumPawn::OnJumpReleased()
{
	StopJumping();
}

void AElysiumPawn::ToggleNoclip()
{
	SetNoclip(!bNoclip);
}

void AElysiumPawn::SetNoclip(bool bEnable)
{
	bNoclip = bEnable;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	SetActorEnableCollision(!bNoclip);
	Move->SetMovementMode(bNoclip ? MOVE_Flying : MOVE_Walking);
	Move->Velocity = FVector::ZeroVector;
	ApplyGait();   // the same latch means "walk" on the ground and "boost" in the air
}
