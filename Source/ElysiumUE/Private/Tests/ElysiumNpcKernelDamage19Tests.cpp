// Story 0019/8 (29e under the strict verdict), family **Damage19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelDamage19.` and the retail address. No tests yet: the
// porter adds them with the bodies.
//
// Owns (Damage19's `rule` rows): 0x1029fa50 CAI_BaseNPCTroika::FUN_1029fa50, 0x1029fcf0
// CAI_BaseNPCTroika::PlayerDefenderBlockReaction, 0x102a01b0
// CAI_BaseNPCTroika::PlayerKnockbackReaction, 0x10378d30 CNPC_VGargoyle::PlayerKnockbackReaction,
// 0x1037a5b0 CNPC_VGargoyle::UpdatePresenceEffect, 0x10380320
// CNPC_VHengeyokai::PlayerKnockbackReaction, 0x10381b10 CNPC_VHengeyokai::UpdatePresenceEffect,
// 0x103ab270 CNPC_VSabbatLeader::UpdatePresenceEffect, 0x103c43f0
// CNPC_VTzimisceRunner::PlayerKnockbackReaction, 0x10265ed0 CAI_BaseNPC::OnTakeDamage_Alive,
// 0x10265e90 CAI_BaseNPC::OnTakeDamage, 0x102beda0 CAI_BaseNPCTroika::OnTakeDamage, 0x102bed30
// CNPC_VVampire::OnTakeDamage, 0x1035e6d0 CNPC_VAndreiBlood::OnTakeDamage_Alive, 0x103601a0
// CNPC_VAnimal::FUN_103601a0, 0x10363c70 CNPC_VBach::vfunc390, 0x10378c10 CNPC_VGargoyle::vfunc390,
// 0x1037bc90 CNPC_VGhoulCroucher::OnTakeDamage_Alive, 0x103801d0 CNPC_VHengeyokai::vfunc390,
// 0x1038e880 CNPC_VManBat::vfunc390, 0x10395ae0 CNPC_VMingXiao::vfunc390, 0x1039e890
// CNPC_VMingXiaoTentacle::vfunc390, 0x103aa480 CNPC_VSabbatLeader::OnTakeDamage_Alive, 0x103b0e90
// CNPC_VSheriffMan::OnTakeDamage_Alive, 0x103cccc0 CNPC_VWerewolf::OnTakeDamage, 0x103e06d0
// CNPC_VZombie::OnTakeDamage.

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
