#include "ElysiumMovementComponent.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

UElysiumMovementComponent::UElysiumMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Step 5 of the frame: the map actor makes this a tick prerequisite of its gameplay pass, so the
	// body moves against the positions this frame's thinks produced (11.1).
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

float UElysiumMovementComponent::GetMaxSpeed() const
{
	if (bNoclip)
	{
		return ElysiumMove::NoclipSpeed * (PendingCmd.IsDown(EElysiumButton::Speed) ? ElysiumMove::NoclipBoost : 1.0f);
	}
	if (!bOnGround)
	{
		return ElysiumMove::JumpMaxSpeed;
	}
	// `+speed` selects the *slow* gait: the run is the default, holding the key walks.
	return PendingCmd.IsDown(EElysiumButton::Speed) ? ElysiumMove::WalkSpeed : ElysiumMove::RunSpeed;
}

void UElysiumMovementComponent::SetNoclip(bool bEnable)
{
	bNoclip = bEnable;
	Velocity = FVector::ZeroVector;
	if (!bNoclip)
	{
		// Re-ask the world where the floor is rather than trusting whatever was true before the fly.
		bOnGround = false;
	}
}

void UElysiumMovementComponent::SetFrozen(bool bInFrozen)
{
	bFrozen = bInFrozen;
	if (bFrozen)
	{
		Velocity = FVector::ZeroVector;
	}
}

FVector UElysiumMovementComponent::WishDirection(const FElysiumUserCmd& Cmd, float& OutScale) const
{
	const AController* C = PawnOwner ? PawnOwner->GetController() : nullptr;
	const FRotator ViewRot = C ? C->GetControlRotation() : (PawnOwner ? PawnOwner->GetActorRotation() : FRotator::ZeroRotator);

	// Walking is on the ground plane; noclip flies along the aim, pitch included.
	const FRotator Frame = bNoclip ? ViewRot : FRotator(0.0f, ViewRot.Yaw, 0.0f);
	const FRotationMatrix Basis(Frame);

	FVector Wish = Basis.GetUnitAxis(EAxis::X) * Cmd.Move.X + Basis.GetUnitAxis(EAxis::Y) * Cmd.Move.Y;
	if (bNoclip)
	{
		Wish += FVector::UpVector * Cmd.Up;
	}

	// Source scales wishspeed by the length of the analog input and clamps it to maxspeed; a
	// diagonal on the keyboard is length sqrt(2), which is why it normalizes rather than summing.
	OutScale = FMath::Min(Wish.Size(), 1.0f);
	return Wish.GetSafeNormal();
}

void UElysiumMovementComponent::CategorizePosition()
{
	if (!UpdatedComponent)
	{
		bOnGround = false;
		return;
	}
	// Rising: never on the ground. Source's own gate, and what keeps a jump from re-grounding on
	// the frame it leaves.
	if (Velocity.Z > ElysiumMove::JumpSpeed * 0.5f)
	{
		bOnGround = false;
		return;
	}

	// A 2u down-trace. VtMB has no StayOnGround, so descending a step briefly leaves the ground and
	// this is what re-detects the floor.
	const FVector Start = UpdatedComponent->GetComponentLocation();
	const FVector End = Start - FVector(0.0f, 0.0f, 2.0f * ElysiumMove::U + ElysiumMove::DistEpsilon);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ElysiumCategorizePosition), /*bTraceComplex*/ false, PawnOwner);
	Params.AddIgnoredActor(PawnOwner);
	const bool bHit = UpdatedPrimitive && GetWorld()->SweepSingleByChannel(Hit, Start, End,
		UpdatedComponent->GetComponentQuat(), UpdatedPrimitive->GetCollisionObjectType(),
		UpdatedPrimitive->GetCollisionShape(), Params);

	bOnGround = bHit && Hit.ImpactNormal.Z >= ElysiumMove::StandableZ;
	SurfaceFriction = 1.0f;   // 4.7 reads the surface's own value
	if (bOnGround && Velocity.Z < 0.0f)
	{
		Velocity.Z = 0.0f;
	}
}

void UElysiumMovementComponent::ApplyFriction(float DeltaTime)
{
	// Scales all three components, not just the horizontal ones — that is what the decompile does.
	const float Speed = Velocity.Size();
	if (Speed < 0.1f)
	{
		return;
	}
	const float Control = FMath::Max(Speed, ElysiumMove::StopSpeed);
	const float Drop = Control * ElysiumMove::Friction * SurfaceFriction * DeltaTime;
	Velocity *= FMath::Max(0.0f, Speed - Drop) / Speed;
}

void UElysiumMovementComponent::ApplyAccelerate(const FVector& WishDir, float WishSpeed, float DeltaTime)
{
	const float AddSpeed = WishSpeed - FVector::DotProduct(Velocity, WishDir);
	if (AddSpeed <= 0.0f)
	{
		return;
	}
	const float AccelSpeed =
		FMath::Min(ElysiumMove::Accelerate * DeltaTime * WishSpeed * SurfaceFriction, AddSpeed);
	Velocity += AccelSpeed * WishDir;
}

void UElysiumMovementComponent::ApplyAirAccelerate(const FVector& WishDir, float WishSpeed, float DeltaTime)
{
	// The cap applies to the *target* while the uncapped wishspeed still drives accelspeed. That
	// asymmetry is the whole of Source's air-strafing behaviour, so it is reproduced exactly.
	const float Capped = FMath::Min(WishSpeed, ElysiumMove::AirSpeedCap);
	const float AddSpeed = Capped - FVector::DotProduct(Velocity, WishDir);
	if (AddSpeed <= 0.0f)
	{
		return;
	}
	const float AccelSpeed =
		FMath::Min(ElysiumMove::AirAccel * WishSpeed * DeltaTime * SurfaceFriction, AddSpeed);
	Velocity += AccelSpeed * WishDir;
}

void UElysiumMovementComponent::TryPlayerMove(const FVector& Delta)
{
	FVector Remaining = Delta;
	// Four bumps is Source's own iteration count; past that the move is abandoned rather than
	// resolved, which is what stops a wedged player from tunnelling.
	for (int32 Bump = 0; Bump < 4 && !Remaining.IsNearlyZero(); ++Bump)
	{
		FHitResult Hit;
		SafeMoveUpdatedComponent(Remaining, UpdatedComponent->GetComponentQuat(), /*bSweep*/ true, Hit);
		if (!Hit.IsValidBlockingHit())
		{
			return;
		}
		// ClipVelocity: project both the velocity and what is left of the move onto the plane.
		Velocity = FVector::VectorPlaneProject(Velocity, Hit.Normal);
		Remaining = FVector::VectorPlaneProject(Remaining * (1.0f - Hit.Time), Hit.Normal);
	}
}

void UElysiumMovementComponent::WalkMove(float DeltaTime)
{
	if (!UpdatedComponent)
	{
		return;
	}
	const FVector Start = UpdatedComponent->GetComponentLocation();
	const FVector StartVelocity = Velocity;
	const FVector Delta = Velocity * DeltaTime;

	// Attempt 1 — the flat move.
	TryPlayerMove(Delta);
	const FVector FlatPos = UpdatedComponent->GetComponentLocation();
	const FVector FlatVel = Velocity;

	// Completed? That is the move. (Cheap test: it went where it was asked to.)
	if (FVector::DistSquared2D(FlatPos, Start + Delta) < FMath::Square(ElysiumMove::DistEpsilon))
	{
		return;
	}
	if (!bOnGround)
	{
		return;   // StepMove is a ground behaviour
	}

	// Attempt 2 — raised. Reset, trace up a step, slide forward, trace back down.
	Velocity = StartVelocity;
	UpdatedComponent->SetWorldLocation(Start, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);

	const FVector Up(0.0f, 0.0f, ElysiumMove::StepSize + ElysiumMove::DistEpsilon);
	FHitResult UpHit;
	SafeMoveUpdatedComponent(Up, UpdatedComponent->GetComponentQuat(), true, UpHit);
	TryPlayerMove(FVector(Delta.X, Delta.Y, 0.0f));
	FHitResult DownHit;
	SafeMoveUpdatedComponent(-Up, UpdatedComponent->GetComponentQuat(), true, DownHit);

	const bool bStandable = DownHit.IsValidBlockingHit() && DownHit.ImpactNormal.Z >= ElysiumMove::StandableZ;
	const FVector StepPos = UpdatedComponent->GetComponentLocation();

	// Keep whichever attempt covered more ground horizontally.
	const float FlatDist = FVector::DistSquared2D(FlatPos, Start);
	const float StepDist = FVector::DistSquared2D(StepPos, Start);
	if (!bStandable || FlatDist >= StepDist)
	{
		UpdatedComponent->SetWorldLocation(FlatPos, false, nullptr, ETeleportType::TeleportPhysics);
		Velocity = FlatVel;
		return;
	}
	// The raised attempt won; its Z velocity still comes from the flat one.
	Velocity.Z = FlatVel.Z;
}

void UElysiumMovementComponent::NoclipMove(float DeltaTime)
{
	float Scale = 0.0f;
	const FVector WishDir = WishDirection(PendingCmd, Scale);
	Velocity = WishDir * GetMaxSpeed() * Scale;
	// No sweep: the point of noclip is to pass through geometry, and the pawn has already dropped
	// its collision.
	MoveUpdatedComponent(Velocity * DeltaTime, UpdatedComponent->GetComponentQuat(), /*bSweep*/ false);
}

void UElysiumMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ShouldSkipUpdate(DeltaTime) || !PawnOwner || !UpdatedComponent || DeltaTime <= 0.0f)
	{
		return;
	}
	if (bFrozen)
	{
		Velocity = FVector::ZeroVector;
		PrevCmd = PendingCmd;
		return;
	}

	if (bNoclip)
	{
		NoclipMove(DeltaTime);
		PrevCmd = PendingCmd;
		UpdateComponentVelocity();
		return;
	}

	CategorizePosition();

	float Scale = 0.0f;
	const FVector WishDir = WishDirection(PendingCmd, Scale);
	const float WishSpeed = GetMaxSpeed() * Scale;

	if (bOnGround)
	{
		// The jump edge, not the key's level: a held jump does not re-fire.
		if (PendingCmd.JustPressed(EElysiumButton::Jump, PrevCmd))
		{
			Velocity.Z = ElysiumMove::JumpSpeed;
			bOnGround = false;
		}
	}

	if (bOnGround)
	{
		ApplyFriction(DeltaTime);
		ApplyAccelerate(WishDir, WishSpeed, DeltaTime);
		Velocity.Z = 0.0f;
	}
	else
	{
		// 4.7 owns Source's half-before/half-after gravity split; one full step is the shell.
		Velocity.Z -= ElysiumMove::Gravity * DeltaTime;
		ApplyAirAccelerate(WishDir, WishSpeed, DeltaTime);
	}

	Velocity = Velocity.GetClampedToMaxSize(ElysiumMove::MaxVelocity);
	WalkMove(DeltaTime);

	PrevCmd = PendingCmd;
	UpdateComponentVelocity();
}
