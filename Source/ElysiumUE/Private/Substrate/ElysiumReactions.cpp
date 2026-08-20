#include "Substrate/ElysiumReactions.h"

#include "ElysiumLocomotionSample.h"   // ElysiumLocomotion::RelativeYaw — the one facing-frame rule
#include "Substrate/ElysiumWeaponClasses.h"   // EElysiumMeleeDefenderReaction — the classifier's own enum

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

const TCHAR* BlockActivityFor(EElysiumMeleeDefenderReaction Reaction)
{
	switch (Reaction)
	{
	case EElysiumMeleeDefenderReaction::BlockStagger:
		return TEXT("ACT_BLOCK_HEAVY");
	case EElysiumMeleeDefenderReaction::Block:
	case EElysiumMeleeDefenderReaction::Dodge:
	case EElysiumMeleeDefenderReaction::DodgeAttack:
		return TEXT("ACT_BLOCK");
	default:
		// `HitKnockback` and `Unclassified` — neither is a blocked class, so neither names one.
		return nullptr;
	}
}

bool IsFrontalContact(const FVector& AttackerOriginCm, const FVector& VictimOriginCm,
	float VictimUnrealYawDegrees)
{
	float Bearing = 0.0f;
	if (!HitYawFrom(AttackerOriginCm, VictimOriginCm, VictimUnrealYawDegrees, Bearing))
	{
		return false;
	}
	// `HitYawFrom` normalizes to (-180, 180], so the absolute value IS the angle off the facing.
	return FMath::Abs(Bearing) <= BlockFrontalHalfAngleDegrees;
}

// --- The grounded knockback family --------------------------------------------------------------

const TCHAR* FElysiumKnockback::Activity() const
{
	return bFallbackCell ? FallbackGroundedKnockbackActivity
		: KnockbackActivity(Size, Height, Direction);
}

const TCHAR* KnockbackActivity(EKnockbackSize Size, EKnockbackHeight Height,
	EKnockbackDirection Direction)
{
	if (Height == EKnockbackHeight::Low)
	{
		// Two cells, both of them BACK. Any other direction names no authored clip.
		if (Direction != EKnockbackDirection::Back)
		{
			return nullptr;
		}
		return Size == EKnockbackSize::Small
			? TEXT("ACT_KNOCKBACK_SMALL_LOW_BACK") : TEXT("ACT_KNOCKBACK_NORMAL_LOW_BACK");
	}

	switch (Direction)
	{
	case EKnockbackDirection::Forward:
		return Size == EKnockbackSize::Small
			? TEXT("ACT_KNOCKBACK_SMALL_HIGH_FORWARD") : TEXT("ACT_KNOCKBACK_NORMAL_HIGH_FORWARD");
	case EKnockbackDirection::Left:
		return Size == EKnockbackSize::Small
			? TEXT("ACT_KNOCKBACK_SMALL_HIGH_LEFT") : TEXT("ACT_KNOCKBACK_NORMAL_HIGH_LEFT");
	case EKnockbackDirection::Right:
		return Size == EKnockbackSize::Small
			? TEXT("ACT_KNOCKBACK_SMALL_HIGH_RIGHT") : TEXT("ACT_KNOCKBACK_NORMAL_HIGH_RIGHT");
	default:
		return Size == EKnockbackSize::Small
			? TEXT("ACT_KNOCKBACK_SMALL_HIGH_BACK") : TEXT("ACT_KNOCKBACK_NORMAL_HIGH_BACK");
	}
}

bool IsKnockbackAllowed(bool bVictimAlive, bool bTemplateDisallowsKnockbacks)
{
	// The two recovered terms only. The two omitted ones are named on the declaration.
	return bVictimAlive && !bTemplateDisallowsKnockbacks;
}

FVector KnockbackAwayFrom(const FVector& AttackerOriginCm, const FVector& VictimOriginCm)
{
	FVector Away = VictimOriginCm - AttackerOriginCm;
	Away.Z = 0.0f;
	return Away;
}

FVector KnockbackAwayWithoutAttacker(float VictimUnrealYawDegrees)
{
	// The negated forward of the victim's own facing: nothing to be thrown away from, so straight
	// backwards. The z is zeroed like the origin-pair form's — only the yaw is ever read.
	const FVector Forward = FRotator(0.0f, VictimUnrealYawDegrees, 0.0f).Vector();
	return FVector(-Forward.X, -Forward.Y, 0.0f);
}

bool KnockbackRelativeYaw(const FVector& AwayCm, float VictimUnrealYawDegrees,
	float& OutAwayWorldYawDegrees, float& OutRelativeYawDegrees)
{
	OutAwayWorldYawDegrees = 0.0f;
	OutRelativeYawDegrees = 0.0f;

	const FVector2D Horizontal(AwayCm.X, AwayCm.Y);
	if (Horizontal.Size() < KnockbackDegenerateLength)
	{
		return false;
	}

	const float AwayWorldYaw = static_cast<float>(
		FMath::RadiansToDegrees(FMath::Atan2(Horizontal.Y, Horizontal.X)));
	OutAwayWorldYawDegrees = FRotator::ClampAxis(AwayWorldYaw);
	// `away` MINUS the facing — the negation Unreal's mirrored frame puts on retail's own
	// `victimYaw - awayYaw` (see the header). `ClampAxis` is `AngleMod`: [0, 360).
	OutRelativeYawDegrees = FRotator::ClampAxis(AwayWorldYaw - VictimUnrealYawDegrees);
	return true;
}

EKnockbackDirection KnockbackDirectionForRelativeYaw(float RelativeYawDegrees)
{
	const float Rel = FRotator::ClampAxis(RelativeYawDegrees);
	// Retail's cascade, in retail's own order. The first band wraps the seam, which is why it is two
	// comparisons; the four widths that fall out are 89 front, 90 right, 90 rear and 91 left.
	if (Rel > 316.0f || Rel <= 45.0f)
	{
		return EKnockbackDirection::Forward;
	}
	if (Rel <= 135.0f)
	{
		return EKnockbackDirection::Right;
	}
	if (Rel <= 225.0f)
	{
		return EKnockbackDirection::Back;
	}
	return EKnockbackDirection::Left;
}

float KnockbackSnapYaw(float AwayWorldYawDegrees, EKnockbackDirection Direction)
{
	// Indexed by the bucket number the enum carries, so this table and retail's are the same table.
	// The values are retail's negated, because Unreal yaw is Source yaw negated.
	static constexpr float Offsets[] = { 180.0f, 90.0f, 0.0f, 270.0f };
	const int32 Bucket = static_cast<int32>(Direction);
	checkf(Bucket >= 0 && Bucket < UE_ARRAY_COUNT(Offsets),
		TEXT("knockback direction %d is outside the four recovered buckets"), Bucket);
	return FRotator::ClampAxis(AwayWorldYawDegrees + Offsets[Bucket]);
}

void BuildKnockback(const FVector& AwayCm, float VictimUnrealYawDegrees, FElysiumKnockback& Out)
{
	Out = FElysiumKnockback();

	float AwayWorldYaw = 0.0f;
	float RelativeYaw = 0.0f;
	// A degenerate `away` is NOT a refusal. Retail's classifier answers bucket 0 off a zeroed vector
	// and its yaw snap runs off a zero `away` yaw, and the cell comes from the no-list fallback
	// instead of the bucket — so the two deliberately disagree on this one path (bucket `Back`
	// against a `..._FORWARD` cell). That is retail's own shape, recorded rather than smoothed.
	Out.bFallbackCell = !KnockbackRelativeYaw(AwayCm, VictimUnrealYawDegrees, AwayWorldYaw,
		RelativeYaw);

	Out.AwayWorldYawDegrees = AwayWorldYaw;
	Out.RelativeYawDegrees = RelativeYaw;
	// Bucket 0 on the degenerate path, stated rather than reached through the bands: a zeroed
	// relative yaw would fall in the FRONT band, and retail's classifier returns 0 there instead.
	Out.Direction = Out.bFallbackCell ? EKnockbackDirection::Back
		: KnockbackDirectionForRelativeYaw(RelativeYaw);
	Out.SnapYawDegrees = KnockbackSnapYaw(AwayWorldYaw, Out.Direction);
	Out.Size = StandInKnockbackSize;
	Out.Height = StandInKnockbackHeight;
}

}
