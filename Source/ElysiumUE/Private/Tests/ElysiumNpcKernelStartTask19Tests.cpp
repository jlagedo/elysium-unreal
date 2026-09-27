// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelStartTask19.` and the retail address. No tests yet:
// the porter adds them with the bodies.
//
// Owns (StartTask19's `rule` rows): 0x102827f0 CAI_BaseNPC::StartTask, 0x102a1910
// CAI_BaseNPCTroika::StartTask, 0x1035f650 CNPC_VAnimal::StartTask, 0x103847f0
// CNPC_VHuman::StartTask, 0x10392d80 CNPC_VMingXiao::StartTask, 0x1039c4c0
// CNPC_VMingXiaoTentacle::StartTask, 0x103ba7c0 CNPC_VTzimisce::StartTask, 0x103c1820
// CNPC_VTzimisceHeadClaw::StartTask, 0x103c35d0 CNPC_VTzimisceRunner::StartTask, 0x103ccda0
// CNPC_VWerewolf::StartTask, 0x103645a0 CNPC_VBach::StartTask, 0x10374940 CNPC_VDog::StartTask,
// 0x10375f50 CNPC_VFrenzyShadow::StartTask, 0x103790d0 CNPC_VGargoyle::StartTask, 0x1037b8b0
// CNPC_VGhoulCroucher::StartTask, 0x103805d0 CNPC_VHengeyokai::StartTask, 0x1038c390
// CNPC_VManBat::StartTask, 0x103a5650 CNPC_VSabbatGunman::StartTask, 0x103ac740
// CNPC_VScurrying::StartTask, 0x103b36d0 CNPC_VTaxiDriver::StartTask, 0x103c5ac0
// CNPC_VVampireBoss::StartTask, 0x103dfd80 CNPC_VZombie::StartTask, 0x1035d1b0
// CNPC_VAndreiBlood::StartTask, 0x103611a0 CNPC_VAsianVampire::StartTask, 0x1036b750
// CNPC_VChangBros::StartTask, 0x103a78c0 CNPC_VSabbatLeader::StartTask, 0x103aec70
// CNPC_VSheriffMan::StartTask.

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
