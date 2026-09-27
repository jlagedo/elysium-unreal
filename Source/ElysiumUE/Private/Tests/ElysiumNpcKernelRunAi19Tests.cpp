// Story 0019/8 (29e under the strict verdict), family **RunAi19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelRunAi19.` and the retail address. No tests yet: the
// porter adds them with the bodies.
//
// Owns (RunAi19's `rule` rows): 0x1028fd80 CAI_BaseNPCTroika::RunAlternateAI, 0x1026f110
// CAI_BaseNPC::RunAI, 0x1028fcc0 CAI_BaseNPCTroika::RunAI, 0x1039e3d0
// CNPC_VMingXiaoTentacle::RunAI, 0x103bdef0 CNPC_VTzimisce::vfunc432, 0x103c1d20
// CNPC_VTzimisceHeadClaw::vfunc432, 0x1035e980 CNPC_VAndreiBlood::vfunc432, 0x10361110
// CNPC_VAsianVampire::RunAI, 0x10363b60 CNPC_VBach::vfunc432, 0x103747e0 CNPC_VDog::vfunc432,
// 0x10378b80 CNPC_VGargoyle::vfunc432, 0x10380120 CNPC_VHengeyokai::vfunc432, 0x1038e990
// CNPC_VManBat::vfunc432, 0x103a3670 CNPC_VPedestrian::vfunc432, 0x103a75c0
// CNPC_VSabbatLeader::RunAI, 0x103aebd0 CNPC_VSheriffMan::RunAI, 0x103df850 CNPC_VZombie::vfunc432.

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
