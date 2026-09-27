// Story 0019/8 (29e under the strict verdict), family **Misc19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelMisc19.` and the retail address. No tests yet: the
// porter adds them with the bodies.
//
// Owns (Misc19's `rule` rows): 0x10279a50 SetEnemy, 0x10279dd0 CAI_BaseNPC::ChooseEnemy, 0x102b4f60
// CAI_BaseNPCTroika::FUN_102b4f60, 0x102b4fe0 CAI_BaseNPCTroika::FUN_102b4fe0, 0x1035dc30
// CNPC_VAndreiBlood::Activate, 0x10365a90 FUN_10365a90, 0x103cfc50
// CNPC_VWerewolf::CheckAllMoveHints, 0x1026cdc0 CAI_BaseNPC::EnterGrappleState, 0x1026cec0
// CAI_BaseNPC::FUN_1026cec0, 0x102b4cc0 CAI_BaseNPCTroika::FUN_102b4cc0, 0x10372c50
// CNPC_VCop::vfunc596, 0x10372dd0 CNPC_VCop::vfunc598, 0x10395ce0 FUN_10395ce0, 0x1039ea60
// FUN_1039ea60, 0x103a3850 CNPC_VPedestrian::vfunc27, 0x101a98c0 CCineAISchedule::vfunc586,
// 0x101aade0 CPayphone::EnterGrappleState, 0x102b5c00 CAI_BaseNPCTroika::EnterGrappleState,
// 0x1037b500 CNPC_VGhoulCroucher::EnterGrappleState, 0x1017f4a0 PlayerSupernaturalIncident,
// 0x10274e30 CAI_BaseNPC::HandleAnimEvent, 0x1029b290 CAI_BaseNPCTroika::HandleAnimEvent,
// 0x10374280 CNPC_VDog::HandleAnimEvent, 0x103786c0 CNPC_VGargoyle::HandleAnimEvent, 0x1037fb60
// CNPC_VHengeyokai::HandleAnimEvent, 0x1038e000 CNPC_VManBat::HandleAnimEvent, 0x10392a70
// CNPC_VMingXiao::HandleAnimEvent, 0x103a7000 CNPC_VSabbatLeader::HandleAnimEvent, 0x103ba410
// CNPC_VTzimisce::HandleAnimEvent, 0x103c1540 CNPC_VTzimisceHeadClaw::HandleAnimEvent, 0x103d88e0
// CNPC_VWerewolf::HandleAnimEvent.

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
