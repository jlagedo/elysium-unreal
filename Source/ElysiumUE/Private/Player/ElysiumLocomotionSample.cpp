#include "ElysiumLocomotionSample.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace ElysiumLocomotion
{

float RelativeYaw(float WorldYaw, float FacingYaw)
{
	return FRotator::NormalizeAxis(WorldYaw - FacingYaw);
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
