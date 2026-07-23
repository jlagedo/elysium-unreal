#include "ElysiumPawn.h"

#include "ElysiumHUD.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

AElysiumPawn::AElysiumPawn()
{
	PrimaryActorTick.bCanEverTick = true;

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

void AElysiumPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Shift is the slow gait (run is default), matching VtMB's +speed binding.
	bool bShift = false;
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		bShift = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
	}

	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (bNoclip)
	{
		Move->MaxFlySpeed = NoclipSpeed * (bShift ? NoclipBoost : 1.f);
	}
	else
	{
		Move->MaxWalkSpeed = bShift ? WalkSpeed : RunSpeed;
	}
}

void AElysiumPawn::SetupPlayerInputComponent(UInputComponent* Input)
{
	Super::SetupPlayerInputComponent(Input);

	Input->BindAxis(TEXT("MoveForward"), this, &AElysiumPawn::MoveForward);
	Input->BindAxis(TEXT("MoveRight"), this, &AElysiumPawn::MoveRight);
	Input->BindAxis(TEXT("MoveUp"), this, &AElysiumPawn::MoveUp);
	Input->BindAxis(TEXT("Turn"), this, &AElysiumPawn::Turn);
	Input->BindAxis(TEXT("LookUp"), this, &AElysiumPawn::LookUp);

	Input->BindAction(TEXT("Jump"), IE_Pressed, this, &AElysiumPawn::OnJumpPressed);
	Input->BindAction(TEXT("Jump"), IE_Released, this, &AElysiumPawn::OnJumpReleased);
	Input->BindAction(TEXT("ToggleNoclip"), IE_Pressed, this, &AElysiumPawn::ToggleNoclip);
	Input->BindAction(TEXT("ToggleSky"), IE_Pressed, this, &AElysiumPawn::ToggleSky);
	Input->BindAction(TEXT("ToggleDebug"), IE_Pressed, this, &AElysiumPawn::ToggleDebug);
	Input->BindAction(TEXT("Use"), IE_Pressed, this, &AElysiumPawn::OnUsePressed);
}

void AElysiumPawn::OnUsePressed()
{
	// Route +use to the current map's entity world (P4.2). E doubles as noclip-ascend, but MoveUp
	// only acts while noclipping, so in normal play E is the use key. PlayerUse no-ops when the
	// look-cursor is on nothing.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumMapSubsystem* Maps = GI->GetSubsystem<UElysiumMapSubsystem>())
		{
			if (AElysiumMapActor* Map = Maps->GetCurrentMap())
			{
				if (FElysiumEntityWorld* World = Map->GetEntityWorld())
				{
					World->PlayerUse();
				}
			}
		}
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
}

void AElysiumPawn::ToggleSky()
{
	if (const UElysiumMapSubsystem* Maps = GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>())
	{
		if (AElysiumMapActor* Map = Maps->GetCurrentMap())
		{
			Map->ToggleSkybox();
		}
	}
}

void AElysiumPawn::ToggleDebug()
{
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (AElysiumHUD* HUD = Cast<AElysiumHUD>(PC->GetHUD()))
		{
			HUD->ToggleDebug();
		}
	}
}
