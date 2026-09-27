// Story 0019/8 (29e under the strict verdict), family **Werewolf19** -- the species classes'
// bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (Werewolf19's `rule` rows): 0x103cc450 CNPC_VWerewolf::UpdateConditionShouldBreakHint,
// 0x103d0ec0 CNPC_VWerewolf::FindBreakHint, 0x103d1200 CNPC_VWerewolf::FindEgressHint, 0x103d2070
// CNPC_VWerewolf::IsImperativeMoveHint, 0x103d3c20 CNPC_VWerewolf::FindTeleportHint, 0x103da0a0
// CNPC_VWerewolf::IsEnemyUnreachable, 0x103d2810 CNPC_VWerewolf::IsImperativeRandomMoveHint,
// 0x103d2a10 CNPC_VWerewolf::FindMoveHint, 0x103cc320
// CNPC_VWerewolf::UpdateConditionEnemyUnreachable, 0x103cf770
// CNPC_VWerewolf::CheckAllRandomMoveHints, 0x103d14f0 CNPC_VWerewolf::FindRandomMoveHint,
// 0x103cc5c0 CNPC_VWerewolf::UpdateConditionCanSpecialMove.

#include "Substrate/ElysiumNpc.h"
