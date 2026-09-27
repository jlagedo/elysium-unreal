// Story 0019/8 (29e under the strict verdict), family **Werewolf19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelWerewolf19.` and the retail address. No tests yet:
// the porter adds them with the bodies.
//
// Owns (Werewolf19's `rule` rows): 0x103cc450 CNPC_VWerewolf::UpdateConditionShouldBreakHint,
// 0x103d0ec0 CNPC_VWerewolf::FindBreakHint, 0x103d1200 CNPC_VWerewolf::FindEgressHint, 0x103d2070
// CNPC_VWerewolf::IsImperativeMoveHint, 0x103d3c20 CNPC_VWerewolf::FindTeleportHint, 0x103da0a0
// CNPC_VWerewolf::IsEnemyUnreachable, 0x102c44e0 SetFollowerBoss, 0x103cac20 FUN_103cac20,
// 0x103d2810 CNPC_VWerewolf::IsImperativeRandomMoveHint, 0x103d2a10 CNPC_VWerewolf::FindMoveHint,
// 0x102c4430 FUN_102c4430, 0x10397380 FUN_10397380, 0x103cc320
// CNPC_VWerewolf::UpdateConditionEnemyUnreachable, 0x103cf770
// CNPC_VWerewolf::CheckAllRandomMoveHints, 0x103d14f0 CNPC_VWerewolf::FindRandomMoveHint,
// 0x10271d10 CAI_BaseNPC::CheckTarget, 0x103cc5c0 CNPC_VWerewolf::UpdateConditionCanSpecialMove.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

#endif  // WITH_DEV_AUTOMATION_TESTS
