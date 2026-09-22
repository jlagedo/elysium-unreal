#pragma once

#include "CoreMinimal.h"

#include "Substrate/ElysiumScheduleId.h"

class FElysiumNpc;

// The two pre-kernel combat selectors.
//
// Both are now one line: the recovered slot body's answer. The eighteen hand-typed combat PROGRAMS
// that used to live here are VtMB's own texts in the corpus, and the CHOSEN, NOT RECOVERED selector
// arms that stood under them are gone with the fold that made them necessary -- see the `.cpp`.
//
// The recovered material: `docs/vtmb/npc-ai/programs.md` -> "Ordinary humanoid combat selection"
// for both selector orders.

namespace ElysiumNpcCombat
{
	/**
	 * `CNPC_VHuman`'s melee selector (`0x10385e40`), which this runtime answers through slot 604.
	 *
	 * Answers a class-LOCAL retail schedule number, or `ElysiumScheduleId::None` when the policy
	 * declines -- the recovered composition rule: "A selector returning zero falls through to
	 * `CAI_BaseNPCTroika::SelectSchedule`, so the weapon policy composes with damage, door, fear and
	 * base state reactions rather than replacing them."
	 */
	int32 SelectMeleeSchedule(FElysiumNpc& Npc, double Now);

	/** The ranged selector (`0x10386560`), same contract. */
	int32 SelectRangedSchedule(FElysiumNpc& Npc, double Now);
}
