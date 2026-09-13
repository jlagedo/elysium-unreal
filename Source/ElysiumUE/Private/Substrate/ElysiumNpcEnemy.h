#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"
#include "Substrate/ElysiumNpcConditions.h"

class FElysiumEntity;
class FElysiumNpc;
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
	 * The damage half of `CAI_BaseNPC::OnTakeDamageAlive` (`0x10265ed0`): the target's attacker is
	 * inserted into CAI_Memory through slot 544. It does not add a D_HT relationship. The Troika
	 * override's separate five-second `m_flStealthVisionOverrideTime` write is a later sensing seam.
	 * Returns true only when the attacker gains its first record.
	 */
	bool RememberDamage(FElysiumNpc& Npc, const FElysiumDmg& Dmg, double Now);

	/**
	 * `CAI_BaseNPC::GatherConditions` (`0x1026ec30`), in its recovered five-step order. This is the
	 * whole decision-pass input side: it rebuilds `Npc.Cognition.Conditions` from scratch, runs the
	 * enemy transaction in the middle of it, and finishes with the committed enemy's own conditions
	 * so the schedule selection that follows reads the enemy this pass chose, not last pass's.
	 */
	void GatherConditions(FElysiumNpc& Npc, double Now);

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
	bool ShouldChooseNewEnemy(const FElysiumNpc& Npc, const FElysiumNpcConditions& Cond);

	// The exceptional interrupt relevant to a pending replacement: `LOST_ENEMY` for an eluded or
	// went-null enemy, `ENEMY_DEAD` for a dead one, `NEW_ENEMY` for ordinary replacement. Retail's
	// active-schedule gate accepts NEW_ENEMY OR the exceptional bit; IsScheduleInterested owns that
	// composition.
	EElysiumNpcCond RequiredInterrupt(const FElysiumNpc& Npc);

	// Does the active schedule admit `Required`? An NPC running NO schedule is interested in
	// everything — see the note at the definition.
	bool IsScheduleInterested(const FElysiumNpc& Npc, EElysiumNpcCond Required);

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
	 * `SetEnemy` (`0x10279a50`). Transfers the old handle through the last-enemy path, writes the
	 * new one, and forgets the previous LOS claim — which is what lets `OnFoundEnemy` fire again for
	 * the new target rather than being swallowed by the previous episode's latch.
	 */
	void SetEnemy(FElysiumNpc& Npc, const FElysiumEntityHandle& NewEnemy);

	/**
	 * `ChooseEnemy` (`0x10279dd0`) plus its effects. Runs the interrupt gate, the stickiness test,
	 * the search and the transition outputs. Returns true when the committed enemy changed.
	 */
	bool ChooseEnemy(FElysiumNpc& Npc, FElysiumNpcConditions& Cond, double Now);

	// `IRelationPriority` for one candidate, joined through the NPC's own table.
	int32 RelationPriority(const FElysiumNpc& Npc, const FElysiumEntity& Candidate);
}
