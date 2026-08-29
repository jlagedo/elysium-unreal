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

// The grounded knockback family.

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

bool IsKnockbackAllowed(bool bVictimAlive, bool bTemplateDisallowsKnockbacks,
	bool bBuildupAdmits, bool bClassBypass)
{
	// Retail's own order, and the bypass really is FIRST: the class virtual skips both terms below
	// it, so a `CNPC_VTzimisceRunner` is knocked back even wearing `Disallow_Knockbacks` and even
	// when its buildup counter has drained.
	if (bClassBypass)
	{
		return true;
	}
	// The dead-victim refusal, which retail spells `Health == Max_Health` over damage-taken. The
	// health commit runs BEFORE the knockback entry on the same blow, so a killing blow arrives here
	// already dead and is refused: the body dies where it stands rather than being thrown first.
	// Ordering that is the producer's job; answering it is this one's.
	return bVictimAlive && !bTemplateDisallowsKnockbacks && bBuildupAdmits;
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
}

int32 KnockbackBucketFor(int32 RotationByte, EKnockbackDirection Direction)
{
	if (RotationByte < 0 || RotationByte >= KnockbackDirectionCount)
	{
		// `0xFF` on the 639 records filling fewer than four buckets, and the byte is decoded
		// unsigned, so this catches both that and anything else outside the cycle.
		return INDEX_NONE;
	}
	// Bucket `k` answers `(RotationByte + k) mod 4`, so the bucket answering a given direction is
	// that relation inverted. The `+ Count` keeps the modulus non-negative on a C++ `%`.
	return (static_cast<int32>(Direction) - RotationByte + KnockbackDirectionCount)
		% KnockbackDirectionCount;
}

bool SelectKnockbackActivity(const FElysiumSwingRecord& Record, EKnockbackDirection Direction,
	FRandomStream& Rng, FString& OutActivity)
{
	OutActivity.Reset();

	int32 Bucket = KnockbackBucketFor(Record.B8, Direction);
	if (Bucket == INDEX_NONE)
	{
		return false;
	}
	// **An empty bucket re-reads bucket 0**, which is retail's own first fallback rather than a
	// refusal: `if (counts[bucket] < 1) bucket = 0`. A direction the attack authors no candidate for
	// therefore answers whatever bucket 0 holds, and only a record whose bucket 0 is ALSO empty
	// reaches the caller's fallback. Refusing here instead would skip the shipped answer entirely.
	if (!Record.KnockbackNames.IsValidIndex(Bucket) || Record.KnockbackNames[Bucket].IsEmpty())
	{
		Bucket = 0;
	}
	if (!Record.KnockbackNames.IsValidIndex(Bucket))
	{
		return false;
	}
	const TArray<FString>& Candidates = Record.KnockbackNames[Bucket];
	if (Candidates.IsEmpty())
	{
		// Retail reaches a per-class default activity by direction here (a virtual on the victim).
		// Nothing in this runtime carries one, so the caller's own fallback cell stands in — named
		// at `FallbackGroundedKnockbackActivity`, which is a DIFFERENT fallback from this one.
		return false;
	}
	// **The draw is unconditional, including over a single candidate.** Retail calls
	// `RandomInt(0, count - 1)` whenever the bucket holds anything, so a one-candidate bucket still
	// spends a draw. Skipping it would answer the same activity and leave the stream one position
	// behind, which every later reaction in the run would then read differently.
	const int32 Pick = Rng.RandRange(0, Candidates.Num() - 1);
	OutActivity = Candidates[Pick];
	return !OutActivity.IsEmpty();
}
}
