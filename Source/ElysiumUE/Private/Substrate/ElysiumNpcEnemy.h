#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"
#include "Substrate/ElysiumNpcConditions.h"

class FElysiumEntity;
class FElysiumNpc;

// The enemy transaction: the recovered `GatherConditions` order, the stickiness test, the schedule
// interrupt-interest gate, candidate arbitration and the `SetEnemy` effects.
//
// Free functions over `FElysiumNpc&` rather than a class, because none of this owns state — every
// byte it reads or writes already lives on the NPC's memory, relationship table and cognition
// block. That is what makes a whole acquisition pass drivable headless from a test.
//
// The recovered order (`docs/vtmb/npc-ai-reverse-engineering.md` -> "Enemy acquisition and
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
	// The priority the remembered attacker's `D_HT` row carries. `IRelationPriority` (`0x10333700`)
	// "returns the row's raw integer, and otherwise returns 5 for a non-null actor" — so 5 is the
	// value an NPC with no row already arbitrates at, and it is what the corpus's dominant hostile
	// `player_reaction D_HT 5` (96 rows) writes. It is the same value and the same reasoning as the
	// law lane's own attack arm (`Substrate/ElysiumNpcWitness.h` -> `AttackRelationPriority`);
	// choosing anything else would make a damage-driven hostility outrank or lose to an authored one
	// for no recovered reason.
	inline constexpr int32 AttackerRelationPriority = 5;

	/**
	 * How long a damage-derived hostility lasts.
	 *
	 * RECOVERED, and the reason this row expires at all: the Troika NPC override at `0x102beda0`
	 * "first saves the complete incoming damage packet at `+0x660c`, then composes the base
	 * transaction. A surviving positive hit remembers the attacker for FIVE SECONDS and notifies the
	 * active schedule" (`docs/vtmb/combat-and-damage.md` -> "NPC damage response and stagger
	 * boundaries"). It is the same five seconds the incoming-melee notice remembers its attacker for
	 * (`ElysiumNpcCond::DetectedAttackRetentionSeconds`) — two records of the same shape, and this
	 * one is the damage half.
	 *
	 * The window is absolute and re-stamped, not extended: every qualifying hit starts it again, so
	 * an NPC that keeps being hit never lets it lapse and one that is left alone forgets five
	 * seconds after the last hit lands.
	 */
	inline constexpr double DamageMemorySeconds = 5.0;

	/**
	 * Step 3 of the recovered NPC damage-to-AI transaction — "records the attack position and
	 * attacker, updates enemy memory" (`docs/vtmb/combat-and-damage.md` -> "NPC damage response and
	 * stagger boundaries", step 3; the same transaction in
	 * `docs/vtmb/npc-ai-reverse-engineering.md` -> "Incapacitation, feeding, grapple, and death").
	 * The attacker/position/time half is the leaf's own
	 * record (`FElysiumNpc::OnDamageCommitted`); this is the enemy-memory half, and it lives here
	 * because this file already owns the "take damage -> update ... enemy-memory records" step of the
	 * flow above.
	 *
	 * CHOSEN, NOT RECOVERED — the STORE. That damage makes its attacker a selectable enemy is
	 * recovered twice over: step 3 above, and "Damage, player criminal/supernatural state, class
	 * relations, scripted `SetRelationship`, encounter logic, and enemy assignment can all change the
	 * effective relation or behavior after spawn" (`docs/vtmb/npc-ai-reverse-engineering.md` ->
	 * "Perception and player-reaction distributions"). What is NOT recovered is where the record
	 * lives: retail keeps a per-actor enemy-memory component and `BestEnemy` walks it, while this
	 * runtime has no such component and `BestEnemy` walks the entity list gated on the relationship
	 * table. The eligibility surface a remembered attacker has to reach is therefore the relationship
	 * table, and a `D_HT` row toward the attacker is how it is reached. Nothing downstream changes:
	 * the gate, the stickiness test, the arbitration and the composed selector do the rest.
	 *
	 * RECOVERED — the LIFETIME. The row lasts `DamageMemorySeconds` and no longer, refreshed by every
	 * qualifying hit. It is a `FElysiumDerivedRelationship`, so it expires on the substrate clock and
	 * never enters a save; the persistent rows beside it — an authored `player_reaction`, a scripted
	 * `SetRelationship`, the law lane's own permanent attack row — keep their exact behaviour and
	 * their save path. `ExpireDamageMemory` is the clock half, run once per decision pass.
	 *
	 * The row can be REFUSED, exactly as the law lane's can: a persistent exact-entity row at a
	 * higher priority beats a derived one, so an authored `player_reaction D_LI 10` keeps its
	 * character friendly through a punch. That is an authored decision beating a derived one, and it
	 * is reported rather than silently dropped.
	 *
	 * Returns true only on the non-hostile -> hate EDGE, so a caller logs a hostility change once per
	 * attacker rather than once per hit. A hit that only renews an existing window returns false and
	 * says nothing.
	 */
	bool RememberAttacker(FElysiumNpc& Npc, const FElysiumEntityHandle& Attacker, double Now);

	/**
	 * The clock half of `RememberAttacker`: drop every damage memory whose five seconds have run out,
	 * and notice when the one that lapsed was holding up a fight.
	 *
	 * Run at the head of the decision pass, so everything that reads the relationship table later in
	 * the same pass — the sight categories, the law lane, `BestEnemy` — sees one consistent answer.
	 *
	 * A COMMITTED enemy whose derived row lapses with no persistent hostility behind it stops being
	 * an eligible enemy relation. That is the state retail reaches by dropping the actor's
	 * enemy-memory record, and the de-escalation it produces here is the ordinary transaction's:
	 * `ShouldChooseNewEnemy` says search, the interrupt gate decides whether this pass may (a swing
	 * in progress keeps ownership until it ends), `BestEnemy` finds the lapsed attacker ineligible,
	 * and `SetEnemy` clears the slot. No output fires — the actor was not lost, its hostility ran
	 * out — and the ideal-state pass then takes the enemy-less NPC out of combat.
	 */
	void ExpireDamageMemory(FElysiumNpc& Npc, double Now);

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
	 * It also searches on `FElysiumNpcCognition::bEnemyHostilityLapsed`, which is this runtime's
	 * stand-in store reaching the same state by its own route: retail's record for that actor is gone
	 * from enemy memory, so its own test finds nothing to keep. The flag is set only by
	 * `ExpireDamageMemory`, so an enemy committed any other way — a script's assignment, an authored
	 * `D_HT` — is unaffected by it.
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

	// Which interrupt condition the pending replacement needs to find in the active schedule's mask:
	// `LOST_ENEMY` for an eluded or went-null enemy, `ENEMY_DEAD` for a dead one, `NEW_ENEMY` for an
	// ordinary replacement.
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
