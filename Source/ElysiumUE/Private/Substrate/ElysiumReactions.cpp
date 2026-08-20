#include "Substrate/ElysiumReactions.h"

#include "ElysiumLocomotionSample.h"   // ElysiumLocomotion::RelativeYaw — the one facing-frame rule

namespace ElysiumReactions
{

const TCHAR* FElysiumFlinch::Activity() const
{
	return bHead ? TEXT("ACT_HIT_HEAD") : TEXT("ACT_HIT_TORSO");
}

bool HitYawFrom(const FVector& AttackerOriginCm, const FVector& VictimOriginCm,
	float VictimUnrealYawDegrees, float& OutHitYawDegrees)
{
	OutHitYawDegrees = 0.0f;

	// Victim to attacker: where the blow came FROM, which is the half of the sign the fan's cell
	// names depend on (see the header).
	const FVector ToAttacker = AttackerOriginCm - VictimOriginCm;
	// Horizontal only — `hit_yaw` is a yaw fan, so the vertical component carries no cell.
	if (!(FVector2D(ToAttacker.X, ToAttacker.Y).SizeSquared() > UE_KINDA_SMALL_NUMBER))
	{
		return false;
	}

	const float BearingWorldYaw = static_cast<float>(
		FMath::RadiansToDegrees(FMath::Atan2(ToAttacker.Y, ToAttacker.X)));
	// The one facing-frame rule, shared with `move_yaw`: a world yaw expressed against a facing,
	// right-positive and normalized to (-180, 180].
	OutHitYawDegrees = ElysiumLocomotion::RelativeYaw(BearingWorldYaw, VictimUnrealYawDegrees);
	return true;
}

float FlinchJitter(FRandomStream& Rng)
{
	return Rng.FRandRange(-FlinchJitterDegrees, FlinchJitterDegrees);
}

bool FlinchPicksHead(FRandomStream& Rng)
{
	return Rng.GetFraction() < 0.5f;
}

bool BuildFlinch(const FVector& AttackerOriginCm, const FVector& VictimOriginCm,
	float VictimUnrealYawDegrees, FRandomStream& Rng, FElysiumFlinch& Out)
{
	Out = FElysiumFlinch();

	// The direction resolves BEFORE either draw, so a pair with no bearing between them leaves the
	// stream exactly where it was: a refused flinch is not a hit and consumes no randomness.
	float Bearing = 0.0f;
	if (!HitYawFrom(AttackerOriginCm, VictimOriginCm, VictimUnrealYawDegrees, Bearing))
	{
		return false;
	}

	Out.bHead = FlinchPicksHead(Rng);
	// Normalized after the jitter, not before: a hit from behind sits on the +-180 seam, and 175 + 20
	// has to fold back to -165 rather than steer a fan past its own last cell.
	Out.HitYawDegrees = FRotator::NormalizeAxis(Bearing + FlinchJitter(Rng));
	return true;
}

}
