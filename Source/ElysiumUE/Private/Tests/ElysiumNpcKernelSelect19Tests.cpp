// Story 0019/8 (29e under the strict verdict), family **Select19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelSelect19.` and the retail address. No tests yet: the
// porter adds them with the bodies.
//
// Owns (Select19's `rule` rows): 0x1028a380 CAI_BaseNPC::SelectSchedule, 0x10394120
// CNPC_VMingXiao::PreSelectSchedule, 0x102ae920 CAI_BaseNPCTroika::PreSelectSchedule, 0x102af660
// CAI_BaseNPCTroika::SelectSchedule, 0x1035fb50 CNPC_VAnimal::SelectSchedule, 0x10384ee0
// CNPC_VHuman::SelectSchedule, 0x103941e0 CNPC_VMingXiao::SelectSchedule, 0x103a46b0
// CNPC_VPlayerController::PreSelectSchedule, 0x103aa510 CNPC_VSabbatLeader::PreSelectSchedule,
// 0x103bb7c0 CNPC_VTzimisce::SelectSchedule, 0x103c1610 CNPC_VTzimisceHeadClaw::SelectSchedule,
// 0x103c3310 CNPC_VTzimisceRunner::SelectSchedule, 0x10360eb0 CNPC_VAsianVampire::SelectSchedule,
// 0x1036b250 CNPC_VChangBros::SelectSchedule, 0x103742d0 CNPC_VDog::SelectSchedule, 0x10375d90
// CNPC_VFrenzyShadow::SelectSchedule, 0x103788d0 CNPC_VGargoyle::SelectSchedule, 0x1037d130
// CNPC_VGuard1::SelectSchedule, 0x1037fca0 CNPC_VHengeyokai::SelectSchedule, 0x103872d0
// CNPC_VHumanCombatant::SelectSchedule, 0x103a29f0 CNPC_VPedestrian::SelectSchedule, 0x103a70c0
// CNPC_VSabbatLeader::SelectSchedule, 0x103ac610 CNPC_VScurrying::SelectSchedule, 0x103ae8c0
// CNPC_VSheriffMan::SelectSchedule, 0x103cee70 CNPC_VWerewolf::SelectSchedule, 0x103dceb0
// CNPC_VWolfMorph::SelectSchedule, 0x103df2e0 CNPC_VZombie::SelectSchedule, 0x10371ee0
// CNPC_VCop::SelectSchedule, 0x1037bd60 CNPC_VGhoulCroucher::SelectSchedule, 0x10387d20
// CNPC_VHumanCombatPatrol::SelectSchedule, 0x103dd6b0 CNPC_VYukie::SelectSchedule.

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
