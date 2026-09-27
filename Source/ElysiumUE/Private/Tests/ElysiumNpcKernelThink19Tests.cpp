// Story 0019/8 (29e under the strict verdict), family **Think19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelThink19.` and the retail address. No tests yet: the
// porter adds them with the bodies.
//
// Owns (Think19's `rule` rows): 0x10298070 CAI_BaseNPCTroika::UpdateCharacter, 0x1026ca80
// CAI_BaseNPC::NPCThink, 0x10292de0 CAI_BaseNPCTroika::NPCThink, 0x10369120 CNPC_VCamera::NPCThink,
// 0x1037b3f0 CNPC_VGhoulCroucher::NPCThink, 0x10394990 CNPC_VMingXiao::NPCThink, 0x103a05b0
// CNPC_VNewscaster::NPCThink, 0x103b9040 CNPC_VTzimisce::NPCThink, 0x103c6000
// CNPC_VVampireBoss::NPCThink, 0x103dfa20 CNPC_VZombie::NPCThink, 0x1035db20
// CNPC_VAndreiBlood::NPCThink, 0x10361490 CNPC_VAsianVampire::NPCThink, 0x1036c6c0
// CNPC_VChangBros::NPCThink, 0x10375e50 CNPC_VFrenzyShadow::NPCThink, 0x103af830
// CNPC_VSheriffMan::NPCThink.

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
