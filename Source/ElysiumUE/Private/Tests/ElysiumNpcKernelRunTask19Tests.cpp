// Story 0019/8 (29e under the strict verdict), family **RunTask19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelRunTask19.` and the retail address. No tests yet:
// the porter adds them with the bodies.
//
// Owns (RunTask19's `rule` rows): 0x10288780 CAI_BaseNPC::RunTask, 0x102aacf0
// CAI_BaseNPCTroika::RunTask, 0x1038d130 CNPC_VManBat::RunTask, 0x1035f940 CNPC_VAnimal::RunTask,
// 0x10384ab0 CNPC_VHuman::RunTask, 0x10393930 CNPC_VMingXiao::RunTask, 0x1039d750
// CNPC_VMingXiaoTentacle::RunTask, 0x103bb1e0 CNPC_VTzimisce::RunTask, 0x103c3870
// CNPC_VTzimisceRunner::RunTask, 0x103cdfb0 CNPC_VWerewolf::RunTask, 0x103652b0
// CNPC_VBach::RunTask, 0x10374a20 CNPC_VDog::RunTask, 0x103793e0 CNPC_VGargoyle::RunTask,
// 0x1037b9f0 CNPC_VGhoulCroucher::RunTask, 0x10380cb0 CNPC_VHengeyokai::RunTask, 0x103b38a0
// CNPC_VTaxiDriver::RunTask, 0x103c5f40 CNPC_VVampireBoss::RunTask, 0x103e01d0
// CNPC_VZombie::RunTask, 0x1035d8b0 CNPC_VAndreiBlood::RunTask, 0x103612e0
// CNPC_VAsianVampire::RunTask, 0x1036bfc0 CNPC_VChangBros::RunTask, 0x103a8990
// CNPC_VSabbatLeader::RunTask, 0x103af780 CNPC_VSheriffMan::RunTask.

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
