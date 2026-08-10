#include "ElysiumLocomotionSample.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace ElysiumLocomotion
{

float RelativeYaw(float WorldYaw, float FacingYaw)
{
	return FRotator::NormalizeAxis(WorldYaw - FacingYaw);
}

float AdvanceMoveYaw(FElysiumMoveYawFilter& Filter, float TargetYawDegrees, float Speed2D,
	float DeltaSeconds)
{
	// Retail's re-arm window and slew rate, both function-local in the ordinary player selector.
	constexpr float ReArmSeconds = 0.3f;
	constexpr float DegreesPerSecond = 4.0f * 180.0f;

	const float Delta = FMath::Max(DeltaSeconds, 0.0f);
	if (!(Speed2D > UE_KINDA_SMALL_NUMBER))
	{
		// Not moving: nothing is written, so the parameter holds and the idle window runs. This is
		// where the snap below is earned.
		Filter.IdleSeconds += Delta;
		return Filter.Value;
	}

	if (Filter.IdleSeconds <= ReArmSeconds)
	{
		// `FixedTurn` takes the short way round the wrap, which is the whole reason it is used here —
		// but it answers in [0, 360), so the result is normalized back into the descriptor's own
		// (-180, 180]. Left unnormalized a backpedalling body reads +184 where the fan wants -176,
		// and the grid resolves the wrong cell only near the seam.
		Filter.Value = FRotator::NormalizeAxis(
			FMath::FixedTurn(Filter.Value, TargetYawDegrees, DegreesPerSecond * Delta));
	}
	else
	{
		Filter.Value = FRotator::NormalizeAxis(TargetYawDegrees);
	}
	Filter.IdleSeconds = 0.0f;
	return Filter.Value;
}

FElysiumLocomotionSample FromCharacterMovement(const ACharacter& Body, float FacingYawDegrees)
{
	FElysiumLocomotionSample Out;

	const UCharacterMovementComponent* Move = Body.GetCharacterMovement();
	const FVector Velocity = Body.GetVelocity();
	Out.FacingYaw = FacingYawDegrees;
	Out.bOnGround = Move ? Move->IsMovingOnGround() : false;
	// CharacterMovement carries its own crouch, and it is the same two-state distinction the box
	// body's `bDucked` makes — the ramp is the box body's alone.
	Out.Stance = Move && Move->IsCrouching() ? EElysiumStance::Ducked : EElysiumStance::Standing;

	// Into the facing frame. A yaw-only rotation, so the vertical component passes through.
	const FVector Planar = FRotator(0.0f, -Out.FacingYaw, 0.0f).RotateVector(Velocity);
	Out.LocalVelocity = FVector(Planar.X, Planar.Y, Velocity.Z);

	if (Out.Speed2D() > UE_KINDA_SMALL_NUMBER)
	{
		Out.MoveYawVelocity = RelativeYaw(
			static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X))),
			Out.FacingYaw);
	}
	// Seeded unfiltered. A producer is a getter and may be called more than once a frame, so it
	// cannot advance a rate; the body's driver replaces this with the slewed value once per frame.
	Out.MoveYawPose = Out.MoveYawVelocity;

	// The steering request, which is this path's analogue of the mover's wish direction: the path
	// follower drives it through AddInputVector and it survives the move, where the input vector
	// itself is consumed during it.
	if (Move)
	{
		const FVector Accel = Move->GetCurrentAcceleration();
		const double Planar2D = FVector2D(Accel.X, Accel.Y).Size();
		if (Planar2D > UE_KINDA_SMALL_NUMBER)
		{
			Out.MoveYawWish = RelativeYaw(
				static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Accel.Y, Accel.X))),
				Out.FacingYaw);
			const float MaxAccel = Move->GetMaxAcceleration();
			Out.WishScale = MaxAccel > 0.0f
				? FMath::Clamp(static_cast<float>(Planar2D) / MaxAccel, 0.0f, 1.0f) : 1.0f;
		}
	}

	return Out;
}

} // namespace ElysiumLocomotion
