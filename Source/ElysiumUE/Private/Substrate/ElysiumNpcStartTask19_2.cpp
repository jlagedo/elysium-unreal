// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- `CAI_BaseNPCTroika`'s
// bodies, second part.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// The cut follows the packet's chunk boundaries: one dispatch, retail's default arm once, no case
// body shared across the cut. Unity-build names here are prefixed `StartTask19_2`.
//
// Owns (StartTask19's `rule` rows): 0x102a1910 CAI_BaseNPCTroika::StartTask (the arms past the cut;
// the first part is `ElysiumNpcStartTask19.cpp`).

#include "Substrate/ElysiumNpc.h"
