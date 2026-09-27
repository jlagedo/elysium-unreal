// Story 0019/8 (29e under the strict verdict), family **Damaged19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelDamaged19.` and the retail address. No tests yet:
// the porter adds them with the bodies.
//
// Owns (Damaged19's `rule` rows): 0x102c1ce0 CAI_BaseNPCTroika::ScriptHide ‼, 0x103c43b0
// CNPC_VTzimisceRunner::vfunc330 ‼, 0x1037e240 CNPC_VGuard1::NPCInit ‼, 0x10387140
// CNPC_VHumanCombatant::NPCInit ‼, 0x103dd800 CNPC_VYukie::NPCInit ‼, 0x103c32c0
// CNPC_VTzimisceRunner::HandleAnimEvent ‼, 0x103a4700 CNPC_VPlayerController::NPCThink ‼,
// 0x103cb590 CNPC_VWerewolf::NPCThink ‼, 0x10371b70 CNPC_VCop::StartTask ‼.

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
