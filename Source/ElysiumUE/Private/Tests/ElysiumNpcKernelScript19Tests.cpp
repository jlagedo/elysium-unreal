// Story 0019/8 (29e under the strict verdict), family **Script19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelScript19.` and the retail address. No tests yet: the
// porter adds them with the bodies.
//
// Owns (Script19's `rule` rows): 0x101a8c30 FUN_101a8c30, 0x101a7140 CCineNPC::UpdateOnRemove,
// 0x101a8640 FUN_101a8640, 0x1027d0a0 FUN_1027d0a0, 0x1029f460 FUN_1029f460, 0x1038b1a0
// FUN_1038b1a0, 0x101a7880 CCineNPC::vfunc583, 0x101a8460 SequenceDone, 0x101a8890 FUN_101a8890,
// 0x101a9080 CCineAI::vfunc583, 0x10278220 FUN_10278220, 0x102800c0 ScheduledMoveToGoalEntity,
// 0x102801e0 ScheduledFollowPath, 0x102aa640 FUN_102aa640, 0x102aa860 FUN_102aa860, 0x1037c1c0
// CNPC_VGhoulCroucher::ScriptHide, 0x1038b120 CNPC_VManBat::OverrideMove, 0x101a82d0
// CCineAISchedule::FUN_101a82d0, 0x101a9510 CCineAI::vfunc584, 0x101a9790
// CCineAISchedule::vfunc583.

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
