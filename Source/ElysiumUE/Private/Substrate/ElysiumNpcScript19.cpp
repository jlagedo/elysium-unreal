// Story 0019/8 (29e under the strict verdict), family **Script19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcScript19.inl` (included inside `class FElysiumNpc`) or generated
// in `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated stub moved
// here unchanged (its overlay row reads `hand:`); the porter replaces the body and drops the
// marker.
//
// Owns (Script19's `rule` rows): 0x101a8c30 FUN_101a8c30, 0x101a8640 FUN_101a8640, 0x1027d0a0
// FUN_1027d0a0, 0x1029f460 FUN_1029f460, 0x1038b1a0 FUN_1038b1a0, 0x101a8460 SequenceDone,
// 0x101a8890 FUN_101a8890, 0x10278220 FUN_10278220, 0x102800c0 ScheduledMoveToGoalEntity,
// 0x102801e0 ScheduledFollowPath, 0x102aa640 FUN_102aa640, 0x102aa860 FUN_102aa860.

#include "Substrate/ElysiumNpc.h"
