#pragma once

#include "CoreMinimal.h"

// One authored attack envelope on a melee attack sequence — the box the cast-arm melee selector
// tests an enemy against (`docs/vtmb/combat-and-damage.md` → "The cast arm").
//
// **The axes are not Cartesian and the corners are not positions.** Before scoring its candidates
// `CBaseCombatCharacter::ChooseMeleeAttackSequence` reduces the enemy to three scalars against the
// attacker's own origin — the XY-only distance to the enemy's AABB centre (the vertical component
// is deliberately outside the square root), the signed height difference, and the enemy's own
// half-extents — and builds
//
//     min = ( mag − half.x , −half.y , diff.z − half.z )
//     max = ( mag + half.x , +half.y , diff.z + half.z )
//
// so the three axes mean **reach distance, lateral tolerance and vertical offset**, and the lateral
// one carries no positional term at all: it is symmetric about zero. Retail applies no rotation and
// no basis transform anywhere on this path. That is what makes a 459-record `andrei` lunge and a
// hand-length fist swing comparable to the same test.
//
// The consequence for anything that touches these numbers: they take a scale and nothing else. Put
// through the ordinary position projection they would pick up the Y reflection, mirroring an axis
// that is symmetric — so the mirrored record would compare equal against every symmetric enemy box
// and disagree with nothing that could report it.
//
// The array is declared by `numenvelopes`@700 / `envelopeindex`@704 and is a DIFFERENT array from
// the 188-byte swing-contact records at 708/712, with a different job and no parallelism: 574
// descriptors declare both, and `andrei`'s `JumpFromBlood_Attack` states 459 envelopes against 17
// contact records for the same clip.
//
// It lives in `Public/` for the same reason `ElysiumSwingRecord.h` does: it crosses the outbound
// service seam.
struct FElysiumMeleeEnvelope
{
	// The two corners, in Unreal centimetres — a scale off the file's inches, no reflection. Named
	// `Min`/`Max` because that is what the file states them as; they are not a world-space box.
	FVector Min = FVector::ZeroVector;
	FVector Max = FVector::ZeroVector;

	// Whether a derived query box overlaps this envelope, per-axis and inclusive at both ends —
	// which is how retail's reach test reads and how the selector's own bit is scored.
	bool Overlaps(const FVector& QueryMin, const FVector& QueryMax) const
	{
		return QueryMin.X <= Max.X && QueryMax.X >= Min.X
			&& QueryMin.Y <= Max.Y && QueryMax.Y >= Min.Y
			&& QueryMin.Z <= Max.Z && QueryMax.Z >= Min.Z;
	}
};
