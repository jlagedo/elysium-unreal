// Story 0019/8 (29e under the strict verdict), family **Spawn19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelSpawn19.` and the retail address. No tests yet: the
// porter adds them with the bodies.
//
// Owns (Spawn19's `rule` rows): 0x10265ad0 CAI_BaseNPC::Event_Killed, 0x103c60a0
// CNPC_VVampireBoss::TransformationStart, 0x103c75f0 CNPC_VVampireBoss::InputTransformModel,
// 0x103dfbb0 CNPC_VZombie::CreateCorpse, 0x102bf340 CAI_BaseNPCTroika::Event_Killed, 0x103ab310
// CNPC_VSabbatLeader::TransformationStart, 0x10273200 CAI_BaseNPC::Spawn, 0x10378da0
// CNPC_VGargoyle::Event_Killed, 0x10380390 CNPC_VHengeyokai::Event_Killed, 0x1038e8c0
// CNPC_VManBat::Event_Killed, 0x1039e900 CNPC_VMingXiaoTentacle::Event_Killed, 0x103be010
// CNPC_VTzimisce::Event_Killed, 0x103c1d50 CNPC_VTzimisceHeadClaw::Event_Killed, 0x10368b70
// CNPC_VCamera::Spawn, 0x10395ba0 CNPC_VMingXiao::Event_Killed, 0x10298d30
// CAI_BaseNPCTroika::Spawn, 0x101aa9c0 CPayphone::Spawn, 0x1035f510 CNPC_VAnimal::Spawn, 0x10384690
// CNPC_VHuman::Spawn, 0x103927a0 CNPC_VMingXiao::Spawn, 0x1039c380 CNPC_VMingXiaoTentacle::Spawn,
// 0x103b9060 CNPC_VTzimisce::Spawn, 0x103c1b90 CNPC_VTzimisceHeadClaw::Spawn, 0x103c3b30
// CNPC_VTzimisceRunner::Spawn, 0x103caa30 CNPC_VWerewolf::Spawn, 0x10374000 CNPC_VDog::Spawn,
// 0x1037cda0 CNPC_VGuard1::Spawn, 0x10387110 CNPC_VHumanCombatant::Spawn, 0x103a2540
// CNPC_VPedestrian::Spawn, 0x103ac430 CNPC_VScurrying::Spawn, 0x103c4ef0 CNPC_VVampire::Spawn,
// 0x103df170 CNPC_VZombie::Spawn, 0x1035cc20 CNPC_VAndreiBlood::Spawn, 0x10360c50
// CNPC_VAsianVampire::Spawn, 0x10363850 CNPC_VBach::Spawn, 0x1036afc0 CNPC_VChangBros::Spawn,
// 0x10371a20 CNPC_VCop::Spawn, 0x1037b040 CNPC_VGhoulCroucher::Spawn, 0x1037fa00
// CNPC_VHengeyokai::Spawn, 0x103887a0 CNPC_VHunter::Spawn, 0x10389390 CNPC_VLasombra::Spawn,
// 0x1038b030 CNPC_VManBat::Spawn, 0x103a4510 CNPC_VPlayerController::Spawn, 0x103a6c80
// CNPC_VSabbatLeader::Spawn, 0x103ad630 CNPC_VRat::Spawn, 0x103ae630 CNPC_VSheriffMan::Spawn,
// 0x103dd620 CNPC_VYukie::Spawn, 0x10375c50 CNPC_VFrenzyShadow::Spawn.

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
