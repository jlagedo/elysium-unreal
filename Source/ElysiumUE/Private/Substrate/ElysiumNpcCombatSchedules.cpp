#include "Substrate/ElysiumNpcCombatSchedules.h"

#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumScheduleId.h"

// The two pre-kernel selectors, which are now one line each -- and that is 0019/3's effect here.
//
// This file used to be 686 lines: eighteen hand-typed combat PROGRAMS, a `KernelAnswerFirst` fold
// from a retail schedule number to one of the port's 29 typed identities, and a CHOSEN, NOT
// RECOVERED approximation of each selector's arms underneath it. The approximations existed for one
// reason, stated at the time as family Schedule's standing fact two: the recovered slot bodies
// (`SelectScheduleMeleeCombat` `0x102b6c30` and `SelectScheduleRangedCombat` `0x102b7fc0`, with
// their five species arms each) answer raw retail numbers, and MOST of those numbers named no
// program this runtime carried -- `0xe9`, `0xe4`, `0xe7`, `0x98`, `0xc2`-`0xc6`, `0x8c`-`0x8f`,
// `0x158`-`0x15f` and a dozen more -- so returning them would have left a fighting NPC with nothing
// to run.
//
// The corpus registers all 691 texts. Every one of those numbers is a loaded program, so the fold
// has nothing to fold and the approximations have nothing to stand in for. What is left is the
// recovered body's answer and the recovered composition rule: zero means the selector declines and
// `CAI_BaseNPCTroika::SelectSchedule` gets its turn.
//
// The kick-versus-step-back binary draw went with them, and so did the enemy stamp into
// `m_vSavePosition` that the step-back arm performed. Both were inventions:
// `SCHED_TROIKA_RUN_AWAY_FROM_ENEMY`'s own text runs `TASK_STORE_ENEMY_POSITION_IN_SAVEPOSITION`,
// so the stamp is a TASK in the game rather than a selector's side effect.

int32 ElysiumNpcCombat::SelectMeleeSchedule(FElysiumNpc& Npc, double Now)
{
	(void)Now;
	// Slot 604 `0x102b6c30` and its five species arms (`ElysiumNpcKernelSchedule.cpp`).
	return Npc.SelectScheduleMeleeCombat(0);
}

int32 ElysiumNpcCombat::SelectRangedSchedule(FElysiumNpc& Npc, double Now)
{
	(void)Now;
	// Slot 605 `0x102b7fc0` and its five species arms (`ElysiumNpcKernelCombat10_2.cpp`).
	return Npc.SelectScheduleRangedCombat(0);
}
