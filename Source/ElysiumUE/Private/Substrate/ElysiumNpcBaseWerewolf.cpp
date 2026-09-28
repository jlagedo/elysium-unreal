// Story 0019/8 (29e under the strict verdict), family **Werewolf19** -- `CAI_BaseNPC`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcBaseWerewolf.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated
// stub moved here unchanged (its overlay row reads `hand:`); the porter replaces the body and drops
// the marker.
//
// Owns (Werewolf19's `rule` rows): 0x10271d10 CAI_BaseNPC::CheckTarget.

#include "Substrate/ElysiumNpcBase.h"

#include "Substrate/ElysiumNpcConditions.h"

// Story 8, lane L12. Walked prose in
// `docs/vtmb/npc-ai/conditions-and-states.md` § "Story 8, family Werewolf19".

namespace NpcKernelBaseWerewolf19
{
	// `COND 0x4b` HAVE_TARGET_LOS and `COND 0x49` TARGET_OCCLUDED: the base registrar's numbers, with
	// no `EElysiumNpcCond` enumerator (a hot header), so they are cast.
	constexpr int32 GBaseWerewolf19CondHaveTargetLos = 0x4b;
	constexpr int32 GBaseWerewolf19CondTargetOccluded = 0x49;
	// The trace mask slot 201 is asked under (`0x10271d86 PUSH 0x2804091`), `MASK_BLOCKLOS`-shaped.
	constexpr int32 GBaseWerewolf19TargetLosMask = 0x2804091;
}

// -------------------------------------------------------------------------------------------------
// 0x10271d10 CAI_BaseNPC::CheckTarget
// -------------------------------------------------------------------------------------------------

void FElysiumNpcBase::CheckTarget(FElysiumEntity* TargetEntity)
{
	using namespace NpcKernelBaseWerewolf19;
	// The `CAI_Memory_CheckTarget` VProf scope around the body (`0x10271d15`..`0x10271d63` and the 0x(sites 0x10271d25 0x10271d2f 0x10271d3d 0x10271d4b 0x10271d5d 0x10271dca 0x10271dd4 0x10271de7 0x10271df2 0x10271e66 0x10271e6b 0x10271e70 0x10271e74 0x10271e82)
	// tail from `0x10271dbc`) is profiler bookkeeping with no reader; it is absent.
	Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(GBaseWerewolf19CondHaveTargetLos));    // 0x10271d6e
	Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(GBaseWerewolf19CondTargetOccluded));   // 0x10271d77
	const bool bVisible = FVisible(TargetEntity, GBaseWerewolf19TargetLosMask, nullptr, 0);   // 0x10271d8e slot 201
	// 0x10271da0 / 0x10271da9: slot 1 on `ent_trace_conditions` on either arm, result discarded.
	Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(bVisible            // 0x10271d9c
		? GBaseWerewolf19CondHaveTargetLos                                     // 0x10271dac
		: GBaseWerewolf19CondTargetOccluded));                                 // 0x10271da3 / 0x10271db0
	UpdateTargetPos();                                                         // 0x10271db7 0x10271b10
}

void FElysiumNpcBase::UpdateTargetPos()
{
	// SEAM for `0x10271b10`; see the declaration. Retail's first gate reads the navigator's goal type
	// (`m_pNavigator (+0x5d34) + 0x18`) and `GetGoalType` (`0x102ee620`); with no goal object the gate
	// answers "not a target goal" and the body returns without touching the path.
	++UpdateTargetPosCalls;
	// `navigator+0x18` has no port word; `0x102ee620` is family Motor's `NavGoalState()` (answers -1
	// with no goal object): not a target goal, so the body returns here.
	if (NavGoalState() != 1)
	{
		return;
	}
}
