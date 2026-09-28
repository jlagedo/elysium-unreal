#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"
#include "Substrate/ElysiumNpcConditions.h"

class FElysiumEntity;
class FElysiumNpc;
class FElysiumNpcBase;
struct FElysiumDmg;

// The enemy transaction: the recovered `GatherConditions` order, the stickiness test, the schedule
// interrupt-interest gate, candidate arbitration and the `SetEnemy` effects.
//
// Free functions over `FElysiumNpc&` rather than a class, because none of this owns state — every
// byte it reads or writes already lives on the NPC's memory, relationship table and cognition
// block. That is what makes a whole acquisition pass drivable headless from a test.
//
// The recovered order (`docs/vtmb/npc-ai/social.md` -> "Enemy acquisition and
// replacement"), and this file's five steps are exactly it:
//
//     sense / hear / take damage / script relation
//       -> update category conditions and enemy-memory records
//       -> schedule interrupt-interest gate
//       -> ShouldChooseNewEnemy
//       -> BestEnemy eligibility and arbitration
//       -> SetEnemy plus last-enemy, condition, slot and lost-output effects
//       -> gather range, LOS, facing and attack conditions for that committed enemy
//
// The gate before the search is the load-bearing part: even a better candidate does not pre-empt
// behaviour in progress, and a schedule that is uninterested in the relevant condition keeps
// ownership. That is the starvation rule a one-frame global target scorer would miss.

namespace ElysiumNpcEnemy
{
	/**
	 * `ShouldChooseNewEnemy` (`0x10279d00`).
	 *
	 * Searches when there is no current enemy, when that actor is dead, when its enemy-memory record
	 * is marked eluded, or when `SEE_HATE`, `SEE_DISLIKE`, `SEE_NEMESIS` or `ENEMY_DEAD` is present.
	 *
	 * `SEE_FEAR` is deliberately NOT in that list. The retail body does not test it, although a
	 * remembered `D_FR` candidate is eligible in `BestEnemy` — so a fear relation can be *chosen*
	 * but cannot by itself *trigger* a choice. Adding it here would make frightened bystanders
	 * re-target on sight, which retail does not do.
	 *
	 * The internal declining bit at `+0x14bc` (`0x10000`) is not reproduced: it is unnamed, has no
	 * recovered writer, and gating on an invented flag would silently disable selection.
	 */
	bool ShouldChooseNewEnemy(const FElysiumNpcBase& Npc, const FElysiumNpcConditions& Cond);

	/**
	 * `BestEnemy` (`0x102743c0`).
	 *
	 * Eligibility: a handle resolving to a living actor other than self, with relation `D_HT` or
	 * `D_FR`, not carrying the eluded marker. Arbitration, in order:
	 *
	 *   1. a reachable candidate beats an unreachable one;
	 *   2. at the same reachability class, larger `IRelationPriority` wins;
	 *   3. at equal priority, smaller integer distance normally wins;
	 *   4. visibility modifies (3): a visible candidate can displace a farther unseen incumbent,
	 *      while a closer unseen candidate displaces only an unseen incumbent.
	 *
	 * Returns an invalid handle when nothing is eligible.
	 */
	FElysiumEntityHandle BestEnemy(const FElysiumNpc& Npc);

	/**
	 * `SetEnemy` (`0x10279a50`), retail's body (story 8 L11). On a change of the resolved enemy: the
	 * LIVE old enemy goes to `m_hLastEnemy` (`0x10279b70`) and slot 560 `ClearAttackConditions`
	 * runs; then `m_hEnemy` is written, and a non-null write runs the discipline break-on-notice
	 * sweep (`0x101e3d70`, a counted seam).
	 */
	void SetEnemy(FElysiumNpcBase& Npc, const FElysiumEntityHandle& NewEnemy);

	/**
	 * `CAI_BaseNPC::ChooseEnemy` (`0x10279dd0`), retail's body (story 8 L11): the interrupt gate
	 * (with the went-null fall-through), slot 480, slot 478 through the summoner redirect, and the
	 * change work on the NPC's own condition set and `m_afMemory`. Returns whether an enemy is now
	 * held — retail's answer, not "the enemy changed".
	 */
	bool ChooseEnemy(FElysiumNpcBase& Npc);

	// `IRelationPriority` for one candidate, joined through the NPC's own table.
	int32 RelationPriority(const FElysiumNpc& Npc, const FElysiumEntity& Candidate);
}
