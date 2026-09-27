// Story 0019/8 (29e under the strict verdict), family **Conditions19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelConditions19.` and the retail address. No tests yet:
// the porter adds them with the bodies.
//
// Owns (Conditions19's `rule` rows): 0x10270b20 CAI_BaseNPC::GatherEnemyConditions, 0x1026ec30
// CAI_BaseNPC::GatherConditions, 0x102b27f0 CAI_BaseNPCTroika::GatherConditions, 0x1035d180
// CNPC_VAndreiBlood::GatherConditions, 0x10365a70 CNPC_VBach::GatherConditions, 0x1036b590
// CNPC_VChangBros::GatherConditions, 0x10374b00 CNPC_VDog::GatherConditions, 0x10375ed0
// CNPC_VFrenzyShadow::GatherConditions, 0x10378df0 CNPC_VGargoyle::GatherConditions, 0x1037b570
// CNPC_VGhoulCroucher::GatherConditions, 0x103803d0 CNPC_VHengeyokai::GatherConditions, 0x10394e40
// CNPC_VMingXiao::GatherConditions, 0x1039ec10 CNPC_VMingXiaoTentacle::GatherConditions, 0x103a2c30
// CNPC_VPedestrian::GatherConditions, 0x103a77f0 CNPC_VSabbatLeader::GatherConditions, 0x103ac500
// CNPC_VScurrying::GatherConditions, 0x103bce40 CNPC_VTzimisce::GatherConditions, 0x103c17f0
// CNPC_VTzimisceHeadClaw::GatherConditions, 0x103c35a0 CNPC_VTzimisceRunner::GatherConditions,
// 0x103d0410 CNPC_VWerewolf::GatherConditions.

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
