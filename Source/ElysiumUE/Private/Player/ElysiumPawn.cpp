#include "ElysiumPawn.h"

#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
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
	// A moving pawn is an agent on the graph, never geometry that reshapes it. UShapeComponent
	// defaults this to true, and APawn does not propagate its actor-level navigation flag to an
	// arbitrary box root, so leaving it enabled dirties Recast tiles on every move and crouch.
	Hull->SetCanEverAffectNavigation(false);
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
	// resizing has to move the actor or the body would grow/shrink about its middle.
	//
	// On the ground the feet stay planted, so the centre travels the half-height change. Airborne,
	// `FinishDuck` moves the origin by **half** the height difference (`+18u` ducking, `-18u`
	// standing up) rather than the whole of it — the feet rise 18 and the head drops 18, which is
	// exactly a **fixed centre**. So the airborne case is a pure resize with no shift, and that half
	// — not the full 36 — is the crouch-jump's reach (`docs/vtmb/source_movement.md` → "Ducking").
	const float Shift = bAnchorFeet ? (NewHalf - OldHalf) : 0.0f;

	Hull->SetBoxExtent(FVector(ElysiumMove::HullHalfWidth, ElysiumMove::HullHalfWidth, NewHalf),
		/*bUpdateOverlaps*/ true);
	AddActorWorldOffset(FVector(0.0f, 0.0f, Shift), /*bSweep*/ false, nullptr,
		ETeleportType::TeleportPhysics);

	// **The body surface re-bases with the hull, for exactly the reason the eye does.** It hangs off
	// the box centre by one half-height so its authored feet sit on the hull's floor, and that
	// offset is a function of the *current* height rather than a constant: shrinking to the ducked
	// hull moves the centre down 18 while a surface still offset by the standing 36 keeps its feet
	// 18 below the floor — the model wades into the ground for as long as the crouch lasts.
	//
	// It is re-based here rather than where it is attached because this is the one place the height
	// changes. A crouch used to be a held key and the artefact flashed; it is a toggle now and it
	// simply stands.
	if (USkeletalMeshComponent* Visual = GetPlayerVisual())
	{
		Visual->SetRelativeLocation(FVector(0.0f, 0.0f, -NewHalf));
	}

	SetEyeHeight(EyeAboveFeetCm);
}

void AElysiumPawn::SetEyeHeight(float EyeAboveFeetCm)
{
	if (Camera && Hull)
	{
		// The view offset is measured from the feet, so it re-bases onto the current centre. The
		// mover drives this on its own during a duck transition, where the eye slides between the
		// two offsets while the hull is still the standing one.
		Camera->SetRelativeLocation(FVector(0.0f, 0.0f,
			EyeAboveFeetCm - Hull->GetUnscaledBoxExtent().Z));
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

FElysiumLocomotionSample AElysiumPawn::GetLocomotionSample() const
{
	// Handed straight back rather than recomputed: the mover published it at its tick tail, which is
	// the only point in the frame where the state it describes is settled.
	return Movement ? Movement->GetLocomotionSample() : FElysiumLocomotionSample();
}

void AElysiumPawn::SetPlayerVisual(USkeletalMeshComponent* InVisual)
{
	PlayerVisual = InVisual;
	ApplyPlayerModelAlpha(PlayerVisualAlpha);
}

void AElysiumPawn::ApplyPlayerModelAlpha(float Alpha)
{
	PlayerVisualAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	if (PlayerVisual)
	{
		PlayerVisual->SetScalarParameterValueOnMaterials(TEXT("ModelAlpha"), PlayerVisualAlpha);
		PlayerVisual->SetHiddenInGame(PlayerVisualAlpha <= KINDA_SMALL_NUMBER);
	}
}

void AElysiumPawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (UElysiumCameraComponent::CalcCameraFor(Camera, DeltaTime, OutResult))
	{
		ApplyPlayerModelAlpha(Camera->ModelAlpha());
		return;
	}
	Super::CalcCamera(DeltaTime, OutResult);
	ApplyPlayerModelAlpha(Camera ? Camera->ModelAlpha() : 0.0f);
}
