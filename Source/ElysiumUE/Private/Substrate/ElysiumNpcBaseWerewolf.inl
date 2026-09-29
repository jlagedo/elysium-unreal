// Story 0019/8 (29e under the strict verdict), family **Werewolf19** -- `CAI_BaseNPC`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpcBase` by `Substrate/ElysiumNpcBase.h`; the definitions are in
// `ElysiumNpcBaseWerewolf.cpp`, or generated in the slot files for a slot body.
//
// Owns (Werewolf19's `rule` rows): 0x10271d10 CAI_BaseNPC::CheckTarget.

// --- Story 8, lane L12 -------------------------------------------------------------------------

/** `CAI_BaseNPC::CheckTarget(CBaseEntity*)` (`0x10271d10`): clear `COND 0x4b` HAVE_TARGET_LOS and
 *  `0x49` TARGET_OCCLUDED, ask slot 201 `FVisible(target, 0x2804091, NULL, 0)` once, set `0x4b` on a
 *  clear line or `0x49` otherwise, then `UpdateTargetPos` unconditionally. No debounce. */
void CheckTarget(FElysiumEntity* Target);

/** SEAM for `CAI_BaseNPC::UpdateTargetPos` (`0x10271b10`): re-aim or re-path the navigator's
 *  target goal at `m_hTargetEnt` (`+0x5ce4`). It acts only when the navigator's nav type (`+0x5d34`
 *  `+0x18`, `NavGetType`) is neither 3 nor 1 AND `GetGoalType` (`0x102ee620`, `NavGoalState()`)
 *  answers 1. Both gates are read from the navigator; the body past them is not ported. Counted. */
void UpdateTargetPos();
int32 UpdateTargetPosCalls = 0;
