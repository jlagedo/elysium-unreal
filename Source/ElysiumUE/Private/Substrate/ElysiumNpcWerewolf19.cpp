// Story 0019/8 (29e under the strict verdict), family **Werewolf19** -- `CAI_BaseNPCTroika`'s
// bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcWerewolf19.inl` (included inside `class FElysiumNpc`) or generated
// in `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated stub moved
// here unchanged (its overlay row reads `hand:`); the porter replaces the body and drops the
// marker.
//
// Owns (Werewolf19's `rule` rows): 0x102c44e0 SetFollowerBoss, 0x103cac20 FUN_103cac20, 0x102c4430
// FUN_102c4430, 0x10397380 FUN_10397380.

#include "Substrate/ElysiumNpc.h"
