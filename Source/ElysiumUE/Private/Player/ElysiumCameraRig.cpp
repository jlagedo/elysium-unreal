#include "ElysiumCameraRig.h"

// =====================================================================================
// The damper
// =====================================================================================

namespace
{
	// The decay factor for one step. Expressed as a half-life so the result is independent of how
	// the elapsed time was subdivided: two steps of `Dt` compose to exactly one step of `2 * Dt`,
	// which is what `Pow` gives and a `K * Dt` Euler step does not.
	float DecayFactor(float HalfLifeSeconds, float Dt)
	{
		if (HalfLifeSeconds <= 0.0f || Dt <= 0.0f)
		{
			return HalfLifeSeconds <= 0.0f ? 0.0f : 1.0f;
		}
		return FMath::Pow(0.5f, Dt / HalfLifeSeconds);
	}
}

FVector ElysiumRig::DampToward(const FVector& Current, const FVector& Target, float HalfLifeSeconds,
	float Dt)
{
	const float Retained = DecayFactor(HalfLifeSeconds, Dt);
	return Target + (Current - Target) * Retained;
}

float ElysiumRig::DampToward(float Current, float Target, float HalfLifeSeconds, float Dt)
{
	const float Retained = DecayFactor(HalfLifeSeconds, Dt);
	return Target + (Current - Target) * Retained;
}

// =====================================================================================
// The boom
// =====================================================================================

FRotator ElysiumRig::BoomRotation(const FRotator& ViewRot, const FElysiumCameraRigTuning& Tuning)
{
	// The camera sits at `Pivot - Forward * Distance`, so lifting it above the eye line means
	// pitching the boom *down*: `Forward.Z < 0` puts `-Forward.Z` above the pivot. Hence the
	// subtraction, and it is the reason `PitchOffset` is documented as "degrees above the eye line"
	// rather than as a rotation — the sign of the rotation is the opposite of the sign of the lift.
	// **Normalize before clamping.** A control rotation arrives in Unreal's canonical [0, 360)
	// — `APlayerCameraManager::LimitViewPitch` ends with `FRotator::ClampAxis` — so looking down is
	// 271..359, not -89..0. Clamping that raw pins the boom at `PitchMax` for every downward view
	// and releases it only when the angle wraps back past 360, which reads as the camera snapping to
	// maximum-up and then recentring. Yaw has always been normalized here for the same reason.
	FRotator BoomRot;
	BoomRot.Pitch = FMath::Clamp(FRotator::NormalizeAxis(ViewRot.Pitch) - Tuning.PitchOffset,
		Tuning.PitchMin, Tuning.PitchMax);
	BoomRot.Yaw = FRotator::NormalizeAxis(ViewRot.Yaw);
	// A banked view must not roll the boom: the strafe bank is a view effect and rotating the arm
	// by it would swing the character across the frame.
	BoomRot.Roll = 0.0f;
	return BoomRot;
}

FVector ElysiumRig::BoomTarget(const FVector& Pivot, const FRotator& BoomRot, float Distance,
	const FElysiumCameraRigTuning& Tuning)
{
	const FRotationMatrix Basis(BoomRot);
	const FVector Forward = Basis.GetScaledAxis(EAxis::X);
	const FVector Right = Basis.GetScaledAxis(EAxis::Y);
	const FVector Up = Basis.GetScaledAxis(EAxis::Z);

	// The shoulder offset is applied in the boom's own frame rather than in world space, so it
	// stays on the same side of the character as the player turns.
	return Pivot
		- Forward * (Distance + Tuning.ShoulderOffset.X)
		+ Right * Tuning.ShoulderOffset.Y
		+ Up * Tuning.ShoulderOffset.Z;
}

float ElysiumRig::SolveBoomDistance(float Current, float Desired, bool bHit, float HitDistance,
	const FElysiumCameraRigTuning& Tuning, float Dt)
{
	const float Floor = FMath::Min(Tuning.MinBoomLength, Desired);

	if (bHit)
	{
		// Retract immediately. Any easing here puts geometry inside the near plane for the duration
		// of the ease, which reads as the wall clipping through frame rather than as smoothing.
		const float Allowed = FMath::Max(Floor, HitDistance - Tuning.WallPullIn);
		return FMath::Min(Current, Allowed);
	}

	// Clear: grow back toward the rest length, rate-limited. This is the half that is *not*
	// symmetric with the retract, and it is why the camera does not pop out of a doorway on the
	// first frame the sweep misses.
	if (Tuning.ReturnSpeed <= 0.0f || Dt <= 0.0f)
	{
		return Desired;
	}
	return FMath::Clamp(Current + Tuning.ReturnSpeed * Dt, Floor, Desired);
}
