// Story 0019/8 (29e under the strict verdict), family **Boss19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcBoss19.inl` (included inside `class FElysiumNpc`) or generated in
// `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated stub moved here
// unchanged (its overlay row reads `hand:`); the porter replaces the body and drops the marker.
//
// Owns (Boss19's `rule` rows): 0x102b52a0 FUN_102b52a0, 0x102c51a0 DoPossession, 0x102c5310
// DoFrenzy, 0x103830e0 FUN_103830e0, 0x10395c70 FUN_10395c70, 0x1039e970 FUN_1039e970, 0x10397410
// FUN_10397410, 0x10397e90 FUN_10397e90, 0x10397f00 FUN_10397f00, 0x10395750 FUN_10395750.

#include "Substrate/ElysiumNpc.h"
