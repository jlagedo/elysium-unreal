#pragma once

#include "CoreMinimal.h"

#include "Substrate/ElysiumSchedule.h"

class FElysiumNpc;

// The combat schedule families and the two selectors that choose among them.
//
// The programs are registered from this file rather than from the kernel because a program and the
// policy that picks it are one decision: `SCHED_TROIKA_MELEE_ATTACK1`'s transfer to the swing only
// makes sense beside the `CAN_MELEE_ATTACK1` branch that selects it. `ElysiumSchedule::Register` is
// the growth point; the kernel keeps the task vocabulary and nothing about fighting.
//
// The recovered material: `docs/vtmb/npc-ai-reverse-engineering.md` -> "Ordinary humanoid combat
// selection" for both selector orders, the decoded chase/attack/dodge/block programs and the
// interrupt census; "Incapacitation, feeding, grapple, and death" for the three damage schedules;
// `docs/vtmb/combat-and-damage.md` -> "Target acquisition, sequence commit and recovery" for the
// announce notice `TASK_ANNOUNCE_ATTACK` sends.
//
// Every program below whose contents the survey does not decode is marked CHOSEN, NOT RECOVERED at
// its construction, with the recovered INTENT quoted beside it. A marked program is composed from
// the recovered task vocabulary and nothing else — it never carries a task this runtime invented.

// The per-NPC state the melee selector retains between ticks.
//
// Recovered, and the reason this struct exists at all: "The selector retains timer state, so
// repeated ticks do not independently reroll every action." A selector that redrew its coin on
// every think would make an NPC alternate kick and step-back at think frequency, which is a
// different behaviour from the one the sentence describes.
struct FElysiumNpcCombatSelector
{
	enum class EMeleeReaction : uint8 { None, Kick, Stepback };

	// The kick-versus-step-back binary draw, held until it is consumed or expires.
	EMeleeReaction DrawnReaction = EMeleeReaction::None;
	double DrawnReactionExpiresAt = -1.0;

	// CHOSEN, NOT RECOVERED: how long a drawn decision is retained. The survey states THAT the
	// selector retains timer state and names no interval for any of the per-NPC timers it mentions.
	// Two seconds is picked because it outlasts an ordinary think cadence (0.05-0.25 s) by an order
	// of magnitude — which is what "repeated ticks do not independently reroll" requires — while
	// staying under the shortest recovered attack recovery, so a stale draw cannot survive the
	// exchange it was made for. Replace the constant, not the retention.
	static constexpr double ReactionRetentionSeconds = 2.0;

	void Reset()
	{
		DrawnReaction = EMeleeReaction::None;
		DrawnReactionExpiresAt = -1.0;
	}
};

namespace ElysiumNpcCombat
{
	/**
	 * `CNPC_VHuman`'s melee selector (`0x10385e40`), in its recovered order.
	 *
	 * Returns `None` when the policy declines, which is the recovered composition rule: "A selector
	 * returning zero falls through to `CAI_BaseNPCTroika::SelectSchedule`, so the weapon policy
	 * composes with damage, door, fear and base state reactions rather than replacing them."
	 */
	EElysiumScheduleId SelectMeleeSchedule(FElysiumNpc& Npc, double Now);

	/** The ranged selector (`0x10386560`), same contract. */
	EElysiumScheduleId SelectRangedSchedule(FElysiumNpc& Npc, double Now);
}
