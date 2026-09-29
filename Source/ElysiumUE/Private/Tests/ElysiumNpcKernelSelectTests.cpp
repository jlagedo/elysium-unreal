// Story 0019/8 (29e under the strict verdict), family **Select19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelSelect19.` and the retail address. Every expected
// schedule is the listing's (`vtmb_asm`), named beside its number by the class's own corpus unit
// (`Content/ElysiumCorpus/ai/schedules/<unit>/space.json`). Bodies are driven directly (the virtual,
// or the qualified retail body) on an NPC stood as the retail class the case exercises; the AI is
// quieted so no think competes.
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
#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFrenzyShadow.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcGuard1.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcHuman.h"
#include "Substrate/ElysiumNpcHumanCombatPatrol.h"
#include "Substrate/ElysiumNpcHumanCombatant.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcPlayerController.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcWolfMorph.h"
#include "Substrate/ElysiumNpcYukie.h"
#include "Substrate/ElysiumNpcZombie.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleNumbers.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

static constexpr EAutomationTestFlags GSelect19Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One subject of the retail class the case exercises (`CAI_BaseNPCTroika` stands the bare
	// Troika line) and one ordinary combatant beside it to serve as an enemy.
	struct FSelect19Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumPlayer* Player = nullptr;

		explicit FSelect19Fixture(const TCHAR* RetailClass = TEXT("CAI_BaseNPCTroika"))
			: World([RetailClass]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("select19_kernel"), 1919);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpcOfClass(TEXT("subject"), FVector::ZeroVector, RetailClass);
					Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
					return Builder;
				}())
		{
			// The controller line renames itself on `Spawn`, so the subject is found by class.
			Npc = World.Npc(TEXT("subject"));
			if (Npc == nullptr && FCString::Strcmp(RetailClass, TEXT("CAI_BaseNPCTroika")) != 0)
			{
				Npc = World.NpcOfClass(RetailClass);
			}
			Other = World.Npc(TEXT("other"));
			Player = World.Player();
			FElysiumNpcWorldFixture::Quiet({ Npc, Other });
			FElysiumNpc::ResetSelect19ConVars();
			if (Npc != nullptr)
			{
				Npc->Schedule.Clear();
				Npc->Cognition.Conditions.Reset();
			}
		}

		~FSelect19Fixture()
		{
			FElysiumNpc::ResetSelect19ConVars();
		}

		template <class T>
		T* As() const
		{
			return Npc != nullptr ? Npc->AsSpecies<T>() : nullptr;
		}
	};

}

// =================================================================================================
// `CAI_BaseNPC::SelectSchedule` `0x1028a380`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19BaseIdleTest,
	"Elysium.Substrate.NpcKernelSelect19.BaseSelectSchedule.Idle", GSelect19Flags)
bool FElysiumNpcKernelSelect19BaseIdleTest::RunTest(const FString&)
{
	// 0x1028a380 case 1.
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(1);
	N.Cognition.Conditions.Set(EElysiumNpcCond::HearCombat);
	TestEqual(TEXT("0x1028a3e4 HEAR_COMBAT -> 6 ALERT_FACE"), N.BaseSelectSchedule(), 6);
	TestEqual(TEXT("0x1028a389 selector id 1"), N.SelectScheduleSelector, 1);
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::GiveWay);
	TestEqual(TEXT("0x1028a428 GIVE_WAY -> 0x38"), N.BaseSelectSchedule(), 0x38);
	N.Cognition.Conditions.Reset();
	N.Navigator.GoalType = 0;
	TestEqual(TEXT("0x1028a452 navigator goal type 0 -> 1 IDLE_STAND"), N.BaseSelectSchedule(), 1);
	N.Navigator.GoalType = 4;
	TestEqual(TEXT("0x1028a4ae otherwise 2 IDLE_WALK"), N.BaseSelectSchedule(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19BaseCombatTest,
	"Elysium.Substrate.NpcKernelSelect19.BaseSelectSchedule.Combat", GSelect19Flags)
bool FElysiumNpcKernelSelect19BaseCombatTest::RunTest(const FString&)
{
	// 0x1028a380 case 2, the ladder after the enemy-dead and flinch arms.
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(2);
	auto Ask = [&N](std::initializer_list<EElysiumNpcCond> Conds)
	{
		N.Cognition.Conditions.Reset();
		for (EElysiumNpcCond C : Conds)
		{
			N.Cognition.Conditions.Set(C);
		}
		return N.BaseSelectSchedule();
	};
	TestEqual(TEXT("0x1028a644 NEW_ENEMY -> 5 WAKE_ANGRY"), Ask({ EElysiumNpcCond::NewEnemy }), 5);
	TestEqual(TEXT("0x1028a79b no SEE_ENEMY -> 0xb COMBAT_FACE"), Ask({}), 0xb);
	TestEqual(TEXT("0x1028a79b ENEMY_OCCLUDED -> 0xf CHASE_ENEMY"), Ask({ EElysiumNpcCond::EnemyOccluded }), 0xf);
	TestEqual(TEXT("0x1028a7c8 TOO_CLOSE_TO_ATTACK -> 0x15"),
		Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::TooCloseToAttack }), 0x15);
	TestEqual(TEXT("0x1028a7f0 CAN_RANGE_ATTACK1 -> 0x21"),
		Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::CanRangeAttack1 }), 0x21);
	TestEqual(TEXT("0x1028a818 CAN_RANGE_ATTACK2 -> 0x22"),
		Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::CanRangeAttack2 }), 0x22);
	TestEqual(TEXT("0x1028a840 CAN_MELEE_ATTACK1 -> 0x1f"),
		Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::CanMeleeAttack1 }), 0x1f);
	TestEqual(TEXT("0x1028a868 CAN_MELEE_ATTACK2 -> 0x20"),
		Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::CanMeleeAttack2 }), 0x20);
	TestEqual(TEXT("0x1028a890 NOT_FACING_ATTACK -> 0xb"),
		Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::NotFacingAttack }), 0xb);
	TestEqual(TEXT("0x1028a8c5 nothing left -> 0xf"), Ask({ EElysiumNpcCond::SeeEnemy }), 0xf);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19BaseAlertTest,
	"Elysium.Substrate.NpcKernelSelect19.BaseSelectSchedule.Alert", GSelect19Flags)
bool FElysiumNpcKernelSelect19BaseAlertTest::RunTest(const FString&)
{
	// 0x1028a380 case 3.
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(3);
	TestEqual(TEXT("0x1028a579 nothing heard -> 9 ALERT_STAND"), N.BaseSelectSchedule(), 9);
	N.Cognition.Conditions.Set(EElysiumNpcCond::HearWorld);
	TestEqual(TEXT("0x1028a55f HEAR_WORLD -> 6 ALERT_FACE"), N.BaseSelectSchedule(), 6);
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	N.Cognition.bCondTookDamage = true;
	// DeltaIdealYaw 0 is below (1.0 - 0.2) * 60.0 = 48.
	TestEqual(TEXT("0x1028a5e0 damage facing the attack -> 0x19 TAKE_COVER_FROM_ORIGIN"),
		N.BaseSelectSchedule(), 0x19);
	TestFalse(TEXT("0x1028a5b7 clears m_bCondTookDamage"), N.Cognition.bCondTookDamage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19BaseOtherStatesTest,
	"Elysium.Substrate.NpcKernelSelect19.BaseSelectSchedule.OtherStates", GSelect19Flags)
bool FElysiumNpcKernelSelect19BaseOtherStatesTest::RunTest(const FString&)
{
	// 0x1028a380 cases 0, 5 (default), 6, 7 and 0xc.
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(6);
	TestEqual(TEXT("0x1028a3ad state 6 -> 1"), N.BaseSelectSchedule(), 1);
	N.WriteNpcStateRetail(0xc);
	TestEqual(TEXT("0x1028a9ac state 0xc -> 1"), N.BaseSelectSchedule(), 1);
	const int32 Before = N.SelectRagdollRequests;
	N.WriteNpcStateRetail(7);
	TestEqual(TEXT("0x1028a908 no client ragdoll -> 0x2b DIE"), N.BaseSelectSchedule(), ElysiumSched::DIE);
	TestEqual(TEXT("0x1028a8f7 asked BecomeClientRagdoll once"), N.SelectRagdollRequests, Before + 1);
	// The two DevWarnings below go to `LogElysiumNpcEnt` at Warning, as the landed kernel's do.
	N.WriteNpcStateRetail(0);
	TestEqual(TEXT("0x1028a3a3 NONE -> 0x43 FAIL"), N.BaseSelectSchedule(), ElysiumSched::FAIL);
	N.WriteNpcStateRetail(5);
	TestEqual(TEXT("0x1028a9c7 state 5 -> 0x43 FAIL"), N.BaseSelectSchedule(), ElysiumSched::FAIL);
	return true;
}

// =================================================================================================
// `CAI_BaseNPCTroika::PreSelectSchedule` `0x102ae920`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19PreSelectForcedTest,
	"Elysium.Substrate.NpcKernelSelect19.PreSelectSchedule.Forced", GSelect19Flags)
bool FElysiumNpcKernelSelect19PreSelectForcedTest::RunTest(const FString&)
{
	// 0x102ae920: the selector id, the investigate-sound reset, the forced schedule consumed.
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.Senses.Memory.InvestigateSound.Serial = 7;
	N.ScheduleHost.ForcedSchedule = 0x77;
	TestEqual(TEXT("0x102ae94f the forced schedule is returned"), N.PreSelectSchedule(), 0x77);
	TestEqual(TEXT("0x102ae943 and consumed"), N.ScheduleHost.ForcedSchedule, 0);
	TestEqual(TEXT("0x102ae92a selector id 2"), N.SelectScheduleSelector, 2);
	TestEqual(TEXT("0x102ae934 0x101b9880 reset m_InvestigateSound"),
		static_cast<int32>(N.Senses.Memory.InvestigateSound.Serial), 0);
	N.WriteNpcStateRetail(1);
	TestEqual(TEXT("0x102af39a nothing -> 0"), N.PreSelectSchedule(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19PreSelectCombatTest,
	"Elysium.Substrate.NpcKernelSelect19.PreSelectSchedule.Combat", GSelect19Flags)
bool FElysiumNpcKernelSelect19PreSelectCombatTest::RunTest(const FString&)
{
	// 0x102ae920 state 2: ON_FIRE, the ATTACK_UNKNOWN flag, NEW_ENEMY, and the no-enemy re-entry.
	FSelect19Fixture F(TEXT("CNPC_VHuman"));
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(2);
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	N.Cognition.Conditions.Set(EElysiumNpcCond::OnFire);
	TestEqual(TEXT("0x102aed1e ON_FIRE -> 0x151 SCHED_TROIKA_ONFIRE"), N.PreSelectSchedule(), 0x151);
	N.Cognition.Conditions.Reset();
	N.NpcFlags.Set(EElysiumNpcFlag::ATTACK_UNKNOWN);
	TestEqual(TEXT("0x102aedbc ATTACK_UNKNOWN -> 0x5b"), N.PreSelectSchedule(), 0x5b);
	TestFalse(TEXT("0x102aed99 and the flag is cleared"), N.NpcFlags.Has(EElysiumNpcFlag::ATTACK_UNKNOWN));
	N.Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
	TestEqual(TEXT("0x102aee01 NEW_ENEMY -> 0xea START_COMBAT"), N.PreSelectSchedule(), 0xea);
	N.Cognition.Conditions.Reset();
	ElysiumNpcEnemy::SetEnemy(N, FElysiumEntityHandle::Invalid());
	N.bNoAlertState = false;
	// No enemy: SetState(3), then 0x1028a260 re-enters; the alert case answers 0x4b ALERT_WAIT.
	TestEqual(TEXT("0x102aed79 no enemy -> SetState(3) and the re-entry"), N.PreSelectSchedule(), 0x4b);
	TestEqual(TEXT("0x102aed5c the state is ALERT"), N.NpcStateRetail(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19PreSelectPlayerOnHeadTest,
	"Elysium.Substrate.NpcKernelSelect19.PreSelectSchedule.PlayerOnHead", GSelect19Flags)
bool FElysiumNpcKernelSelect19PreSelectPlayerOnHeadTest::RunTest(const FString&)
{
	// 0x102aee2a..0x102aef3a: `debug_player_on_head` modes 0, 1, 2.
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(1);
	const EElysiumNpcCond OnHead = static_cast<EElysiumNpcCond>(0x3b);
	const int32 Expected[] = { 0x7b, 0x79, 0x7a };
	for (int32 Mode = 0; Mode < 3; ++Mode)
	{
		FElysiumNpc::SetSelect19ConVar(FElysiumNpc::ESelect19ConVar::DebugPlayerOnHead, Mode);
		N.Cognition.Conditions.Set(OnHead);
		TestEqual(FString::Printf(TEXT("0x102aee9e mode %d"), Mode), N.PreSelectSchedule(), Expected[Mode]);
		TestFalse(TEXT("0x102aee43 PLAYER_ON_HEAD cleared"), N.Cognition.Conditions.Has(OnHead));
	}
	FElysiumNpc::SetSelect19ConVar(FElysiumNpc::ESelect19ConVar::DebugPlayerOnHead, 4);
	N.Cognition.Conditions.Set(OnHead);
	TestEqual(TEXT("0x102aee98 an out-of-range mode falls through to 0"), N.PreSelectSchedule(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19PreSelectTailTest,
	"Elysium.Substrate.NpcKernelSelect19.PreSelectSchedule.Tail", GSelect19Flags)
bool FElysiumNpcKernelSelect19PreSelectTailTest::RunTest(const FString&)
{
	// 0x102af102..0x102af39e: the base arm, special nav, STARTLED, the idle ladder, RUN_TO_SAVED.
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(1);
	N.Cognition.Conditions.Set(EElysiumNpcCond::OnFire);
	TestEqual(TEXT("0x102af109 the base answer (ON_FIRE) is returned"), N.PreSelectSchedule(), 0x151);
	N.Cognition.Conditions.Reset();
	N.NpcFlags.Set(EElysiumNpcFlag2::FINISH_SPECIAL_NAV);
	N.NavSetType(3);
	TestEqual(TEXT("0x102af143 nav type 3 -> 0xfc FINISH_CLIMB"), N.PreSelectSchedule(), 0xfc);
	TestFalse(TEXT("0x102af135 FINISH_SPECIAL_NAV cleared"), N.NpcFlags.Has(EElysiumNpcFlag2::FINISH_SPECIAL_NAV));
	N.NpcFlags.Set(EElysiumNpcFlag2::FINISH_SPECIAL_NAV);
	N.NavSetType(1);
	TestEqual(TEXT("0x102af16b nav type 1 -> 0xfd FINISH_JUMP"), N.PreSelectSchedule(), 0xfd);
	N.NavSetType(0);
	N.NpcFlags.Set(EElysiumNpcFlag::DO_STARTLED);
	TestEqual(TEXT("0x102af1cf DO_STARTLED -> 0xf1"), N.PreSelectSchedule(), 0xf1);
	TestFalse(TEXT("0x102af1dd and cleared"), N.NpcFlags.Has(EElysiumNpcFlag::DO_STARTLED));
	N.Cognition.Conditions.Set(EElysiumNpcCond::Knockback);
	TestEqual(TEXT("0x102af278 idle KNOCKBACK -> 0x14c"), N.PreSelectSchedule(), 0x14c);
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::Comfort);
	TestEqual(TEXT("0x102af2a1 COMFORT -> 0x12f"), N.PreSelectSchedule(), 0x12f);
	N.Cognition.Conditions.Reset();
	N.NpcFlags.Set(EElysiumNpcFlag2::D_CALM);
	TestEqual(TEXT("0x102af2d3 D_CALM -> 0x130"), N.PreSelectSchedule(), 0x130);
	N.NpcFlags.Clear(EElysiumNpcFlag2::D_CALM);
	N.NpcFlags.Set(EElysiumNpcFlag2::D_FOLLOW);
	TestEqual(TEXT("0x102af2ff D_FOLLOW -> 0x131"), N.PreSelectSchedule(), 0x131);
	N.NpcFlags.Clear(EElysiumNpcFlag2::D_FOLLOW);
	N.NpcFlags.Set(EElysiumNpcFlag2::D_POSSESSED);
	TestEqual(TEXT("0x102af327 D_POSSESSED -> 0x131"), N.PreSelectSchedule(), 0x131);
	N.NpcFlags.Clear(EElysiumNpcFlag2::D_POSSESSED);
	N.WriteNpcStateRetail(2);
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	N.Cognition.Conditions.Reset();   // whatever the enemy commit raised (NEW_ENEMY) is not this arm's
	N.Cognition.Conditions.Set(EElysiumNpcCond::Knockback);
	TestEqual(TEXT("0x102af24f combat KNOCKBACK -> 0x14c"), N.PreSelectSchedule(), 0x14c);
	N.Cognition.Conditions.Reset();
	N.WriteNpcStateRetail(1);
	N.ScheduleHost.bSavePositionWalk = true;
	TestEqual(TEXT("0x102af21b m_fSavePositionWalk -> 0x89"), N.PreSelectSchedule(), 0x89);
	TestFalse(TEXT("0x102af221 and consumed"), N.ScheduleHost.bSavePositionWalk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19PreSelectCrimSuspicionTest,
	"Elysium.Substrate.NpcKernelSelect19.PreSelectSchedule.CrimSuspicion", GSelect19Flags)
bool FElysiumNpcKernelSelect19PreSelectCrimSuspicionTest::RunTest(const FString&)
{
	// 0x102aef42 state 0xe: ON_FIRE.
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(0xe);
	N.Cognition.Conditions.Set(EElysiumNpcCond::OnFire);
	TestEqual(TEXT("0x102af0a0 state 0xe ON_FIRE -> 0x151"), N.PreSelectSchedule(), 0x151);
	return true;
}

// =================================================================================================
// `CAI_BaseNPCTroika::SelectSchedule` `0x102af660` — one test per case.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19TroikaIdleTest,
	"Elysium.Substrate.NpcKernelSelect19.TroikaSelectSchedule.Case1Idle", GSelect19Flags)
bool FElysiumNpcKernelSelect19TroikaIdleTest::RunTest(const FString&)
{
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(1);
	TestEqual(TEXT("0x102af8dc nothing -> 0x6b IDLE_DISPOSITION"), N.TroikaSelectSchedule(), 0x6b);
	TestEqual(TEXT("0x102af66c selector id 2"), N.SelectScheduleSelector, 2);
	N.bReturnToInitialPos = true;
	TestEqual(TEXT("0x102af8b5 m_bReturnToInitialPos -> 0x45"), N.TroikaSelectSchedule(), 0x45);
	TestFalse(TEXT("0x102af8b7 and consumed"), N.bReturnToInitialPos);
	N.NpcFlags.Set(EElysiumNpcFlag::D_IS_BUSY);
	N.bReturnToInitialPos = true;
	TestEqual(TEXT("0x102af690 busy with a discipline -> 0x6b first"), N.TroikaSelectSchedule(), 0x6b);
	TestTrue(TEXT("and the return flag is untouched"), N.bReturnToInitialPos);
	N.NpcFlags.Clear(EElysiumNpcFlag::D_IS_BUSY);
	N.bReturnToInitialPos = false;
	// The patrol arm (0x102af6b6..0x102af758): a path object with a schedule draws `0x1029f650`
	// on the patrol cell (0x102af738), which resets `m_bPatrolPathUseHint` (+0x65a0) before its roll
	// -- no interest record stands here, so the flag ends cleared -- then answers the path's +0x4.
	{
		const int32 Ids[] = { 0, -1 };
		N.BuildPatrolPath(&N.PatrolPathCell, 0, 0, 0, Ids, FElysiumNpc::EPatrolPathBuild::Replace);
		if (TestNotNull(TEXT("the patrol cell holds a path"), N.PatrolPathCell.Path))
		{
			N.PatrolPathCell.Path->Schedule = 0x42;
			N.ScheduleHost.bPatrolPathUseHint = true;
			TestEqual(TEXT("0x102af758 the path's schedule word"), N.TroikaSelectSchedule(), 0x42);
			TestFalse(TEXT("0x102af738 0x1029f650 reset m_bPatrolPathUseHint (no interest record)"),
				N.ScheduleHost.bPatrolPathUseHint);
		}
		N.ReleasePatrolPath(&N.PatrolPathCell);
	}
	N.bUseInteresting = true;
	TestEqual(TEXT("0x102af711 use_interesting with no place -> 0xff"), N.TroikaSelectSchedule(), 0xff);
	return true;
}

// `0x102af660` case 1 orders the patrol arm (`0x102af6b6` / `0x102af6be` .. `0x102af743`) BEFORE the
// interest arm (`0x102af6f3`), so a `use_interesting` body that holds a patrol path gets the path's
// program back when a pass ends; it never reaches interesting-place selection. Pins both port sites
// that bypass the selector for a `use_interesting` body: the `ScheduleDone` latch and the
// `ThinkAutonomous` route.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19PatrolOutranksUseInterestingTest,
	"Elysium.Substrate.NpcKernelSelect19.PatrolOutranksUseInteresting_0x102af6b6", GSelect19Flags)
bool FElysiumNpcKernelSelect19PatrolOutranksUseInterestingTest::RunTest(const FString&)
{
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	FElysiumNpc::ResetPatrolPathPool();
	N.WriteNpcStateRetail(1);
	N.WriteIdealStateRetail(1);
	N.bUseInteresting = true;
	constexpr double Now = 5.0;

	// `SetupPatrolType(999 2 FOLLOW_PATROL_PATH_WALK)`'s shape: a type-2 three-node path whose
	// schedule `0x1029f460` installs (`0x1029f56b`).
	const int32 Ids[] = { 40, 41, 42, -1 };
	N.BuildPatrolPath(&N.PatrolPathCell, 999, 2, 0x67, Ids, FElysiumNpc::EPatrolPathBuild::Replace);
	if (!TestNotNull(TEXT("the patrol cell holds a path"), N.PatrolPathCell.Path))
	{
		return false;
	}
	const int32 Patrol = N.Schedule.Current;
	const FElysiumScheduleProgram* Program = ElysiumScheduleFor(Patrol);
	if (!TestNotNull(TEXT("0x1029f56b installed SCHED_TROIKA_FOLLOW_PATROL_PATH_WALK"), Program)
		|| !TestEqual(TEXT("its seven tasks (NEXT_PATROL_POINT is index 6)"), Program->Tasks.Num(), 7))
	{
		return false;
	}
	TestEqual(TEXT("0x10307c20 type 2 starts at index 0"), N.PatrolPathCell.Path->Current, 0);

	// Tasks 0..5 done: the pass starts task 6, `TASK_NEXT_PATROL_POINT` -> `0x102aa9e0` ->
	// `NextPoint 0x10307b80` (+1, in range) and `TaskComplete`.
	N.Schedule.TaskIndex = 6;
	N.Schedule.TaskStatus = EElysiumTaskStatus::New;
	N.MaintainSchedule(Now, /*bReduced=*/true);
	if (!TestNotNull(TEXT("0x102aa9fa an in-range step keeps the path"), N.PatrolPathCell.Path))
	{
		return false;
	}
	TestEqual(TEXT("0x10307b96 task 6 advanced the index 0 -> 1"), N.PatrolPathCell.Path->Current, 1);
	TestEqual(TEXT("0x102aaa10 task 6 completed"), N.Schedule.TaskStatus, EElysiumTaskStatus::Complete);

	// One more pass: the program is done (`COND_SCHEDULE_DONE`); `SelectSchedule` case 1 reaches the
	// patrol arm before `m_bUseInteresting` and answers the path's program (`0x102af743`).
	N.MaintainSchedule(Now, /*bReduced=*/true);
	TestEqual(TEXT("0x102af743 the path's program is selected again"), N.Schedule.Current, Patrol);
	TestTrue(TEXT("and it is running"), N.Schedule.IsRunning());
	TestEqual(TEXT("the path did not move again"), N.PatrolPathCell.Path->Current, 1);
	TestNull(TEXT("no interesting place is claimed (CurrentSpotIndex none)"), N.CurrentAmbientSpot());
	TestNotEqual(TEXT("and the body has no Ambient owner"), N.GetMind().Owner(), EElysiumBodyOwner::Ambient);

	// No program at all (the route's other door, `ThinkAutonomous`): a body holding a path still
	// goes to selection, which answers the path's program.
	N.Schedule.Clear();
	N.MaintainSchedule(Now, /*bReduced=*/true);
	TestEqual(TEXT("0x102af6b6 with no program the patrol arm still wins"), N.Schedule.Current, Patrol);
	TestNull(TEXT("still no interesting place"), N.CurrentAmbientSpot());
	TestNotEqual(TEXT("still no Ambient owner"), N.GetMind().Owner(), EElysiumBodyOwner::Ambient);

	// The path gone (`0x1029f5d0`): only now does the body reach the interest door (`0x102af6f3`),
	// which this runtime still serves with its executor (0002/11) -- it installs no program.
	N.ReleasePatrolPath(&N.PatrolPathCell);
	N.Schedule.Clear();
	N.MaintainSchedule(Now, /*bReduced=*/true);
	TestFalse(TEXT("0x102af6f3 without a path the use_interesting body leaves the interpreter"),
		N.Schedule.IsRunning());
	FElysiumNpc::ResetPatrolPathPool();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19TroikaCombatTest,
	"Elysium.Substrate.NpcKernelSelect19.TroikaSelectSchedule.Case2Combat", GSelect19Flags)
bool FElysiumNpcKernelSelect19TroikaCombatTest::RunTest(const FString&)
{
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(2);
	auto Ask = [&N](std::initializer_list<EElysiumNpcCond> Conds)
	{
		N.Cognition.Conditions.Reset();
		for (EElysiumNpcCond C : Conds)
		{
			N.Cognition.Conditions.Set(C);
		}
		return N.TroikaSelectSchedule();
	};
	N.Cognition.bCondTookDamage = true;
	TestEqual(TEXT("0x102afe5d no SEE_ENEMY -> 0xb COMBAT_FACE"), Ask({}), 0xb);
	TestFalse(TEXT("0x102afc28 clears m_bCondTookDamage"), N.Cognition.bCondTookDamage);
	TestEqual(TEXT("0x102afe71 ENEMY_OCCLUDED -> 0xb1"), Ask({ EElysiumNpcCond::EnemyOccluded }), 0xb1);
	// The fixture's loadout may or may not arm the subject; the weapon word decides the two arms that
	// read it (`TEST AH,0x60`).
	const bool bRanged = (N.SelectActiveWeaponWord() & 0x6000u) != 0;
	const int32 TooClose = Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::TooCloseToAttack });
	if (bRanged)
	{
		TestTrue(TEXT("0x102b01cc TOO_CLOSE with a ranged weapon -> 0xef / 0xf0"), TooClose == 0xef || TooClose == 0xf0);
	}
	else
	{
		TestEqual(TEXT("0x102b01f6 TOO_CLOSE with no ranged weapon -> 0xb8"), TooClose, 0xb8);
	}
	const int32 Range1 = Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::CanRangeAttack1 });
	TestTrue(TEXT("0x102afeb0 CAN_RANGE_ATTACK1 -> 0xef or 0xed"), Range1 == 0xef || Range1 == 0xed);
	TestEqual(TEXT("0x102aff5b CAN_RANGE_ATTACK2 -> 0xee"),
		Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::CanRangeAttack2 }), 0xee);
	TestEqual(TEXT("0x102aff86 CAN_MELEE_ATTACK1 -> 0xdc"),
		Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::CanMeleeAttack1 }), 0xdc);
	TestEqual(TEXT("0x102affb1 CAN_MELEE_ATTACK2 -> 0xdf"),
		Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::CanMeleeAttack2 }), 0xdf);
	TestEqual(TEXT("0x102affdc NOT_FACING_ATTACK -> 0xb"),
		Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::NotFacingAttack }), 0xb);
	TestEqual(TEXT("0x102b0007 TOO_FAR_TO_ATTACK -> 0xb1"),
		Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::TooFarToAttack }), 0xb1);
	TestEqual(TEXT("0x102b0043 WAITING_ATTACK_TIME -> 0xbc"),
		Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::WaitingAttackTime }), 0xbc);
	TestEqual(TEXT("0x102b0054 ENEMY_UNREACHABLE -> 0x17"),
		Ask({ EElysiumNpcCond::SeeEnemy, EElysiumNpcCond::EnemyUnreachable }), 0x17);
	TestEqual(TEXT("0x102b0142 nothing else -> 0xbf"), Ask({ EElysiumNpcCond::SeeEnemy }), 0xbf);
	// The `+0x6444` gate with no ranged weapon falls to the base body, whose combat ladder answers.
	N.ScheduleHost.ShootAtHintNode = 1;
	if (bRanged)
	{
		TestEqual(TEXT("0x102afe31 the shoot-at-hint gate with a ranged weapon -> 0xec"),
			Ask({ EElysiumNpcCond::SeeEnemy }), 0xec);
	}
	else
	{
		TestEqual(TEXT("0x102afe15 the shoot-at-hint gate falls to 0x1028a380"),
			Ask({ EElysiumNpcCond::SeeEnemy }), 0xf);
		TestEqual(TEXT("the base wrote its own selector id"), N.SelectScheduleSelector, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19TroikaAlertTest,
	"Elysium.Substrate.NpcKernelSelect19.TroikaSelectSchedule.Case3Alert", GSelect19Flags)
bool FElysiumNpcKernelSelect19TroikaAlertTest::RunTest(const FString&)
{
	// The settled alert selector (`conditions-and-states.md` § "guesses, settled" 1): 0x4b, never the
	// lookaround.
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(3);
	N.bAllowAlertLookaround = true;
	TestEqual(TEXT("0x102af979 nothing -> 0x4b ALERT_WAIT"), N.TroikaSelectSchedule(), 0x4b);
	TestTrue(TEXT("0x102af96d m_bGoToIdleState"), N.bGoToIdleState);
	TestTrue(TEXT("0x102af973 m_bForceStateChange"), N.GetMind().IsStateChangeForced());
	N.Cognition.Conditions.Set(EElysiumNpcCond::DetectedAttack);
	TestEqual(TEXT("0x102af923 DETECTED_ATTACK -> 0x56"), N.TroikaSelectSchedule(), 0x56);
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::InvestigateSight);
	TestEqual(TEXT("0x102af903 0x102b8a60 INVESTIGATE_SIGHT -> 0x59"), N.TroikaSelectSchedule(), 0x59);
	TestEqual(TEXT("0x102b8b53 alert level 3"), N.AlertLevel, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19TroikaFleeTest,
	"Elysium.Substrate.NpcKernelSelect19.TroikaSelectSchedule.Case8Flee", GSelect19Flags)
bool FElysiumNpcKernelSelect19TroikaFleeTest::RunTest(const FString&)
{
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(8);
	TestEqual(TEXT("0x102b0372 nothing -> 0x77 COWER"), N.TroikaSelectSchedule(), 0x77);
	N.Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x39));
	TestEqual(TEXT("0x102b025b COVER_FAILURE -> 0x73"), N.TroikaSelectSchedule(), 0x73);
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::DetectedAttack);
	TestEqual(TEXT("0x102b02fd DETECTED_ATTACK -> 0x56"), N.TroikaSelectSchedule(), 0x56);
	N.Cognition.Conditions.Reset();
	N.NpcFlags.Set(EElysiumNpcFlag::INITIAL_FLEE);
	N.Cognition.Conditions.Set(EElysiumNpcCond::InvestigateSound);
	TestEqual(TEXT("0x102b0328 INVESTIGATE_SOUND -> 0x48"), N.TroikaSelectSchedule(), 0x48);
	TestFalse(TEXT("0x102b0340 INITIAL_FLEE cleared"), N.NpcFlags.Has(EElysiumNpcFlag::INITIAL_FLEE));
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	TestEqual(TEXT("0x102b03ab no INITIAL_FLEE -> 0x73"), N.TroikaSelectSchedule(), 0x73);
	N.NpcFlags.Set(EElysiumNpcFlag::INITIAL_FLEE);
	TestEqual(TEXT("0x102b03f2 INITIAL_FLEE and damage -> 0x72 SCREAM"), N.TroikaSelectSchedule(), 0x72);
	TestFalse(TEXT("0x102b03b9 INITIAL_FLEE consumed"), N.NpcFlags.Has(EElysiumNpcFlag::INITIAL_FLEE));
	TestTrue(TEXT("0x102b03db m_flNextFleeSoundTime in [10, 20]"),
		N.Senses.Memory.NextFleeSoundTime >= 10.0 && N.Senses.Memory.NextFleeSoundTime <= 20.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19TroikaHuntTest,
	"Elysium.Substrate.NpcKernelSelect19.TroikaSelectSchedule.CaseBHunt", GSelect19Flags)
bool FElysiumNpcKernelSelect19TroikaHuntTest::RunTest(const FString&)
{
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(0xb);
	N.HuntExpireTime = 0.0;
	TestEqual(TEXT("0x102afae1 the hunt timer has run out -> 0x85 HUNT_FINISH"), N.TroikaSelectSchedule(), 0x85);
	N.HuntExpireTime = 1000.0;
	TestEqual(TEXT("0x102afb45 no enemy -> 0x7d HUNT_SETUP_NO_ENEMY"), N.TroikaSelectSchedule(), 0x7d);
	N.NpcFlags.Set(EElysiumNpcFlag::MADE_HUNT_PATH);
	TestEqual(TEXT("0x102afab2 no hunt path clears MADE_HUNT_PATH first"), N.TroikaSelectSchedule(), 0x7d);
	TestFalse(TEXT("0x102afab5"), N.NpcFlags.Has(EElysiumNpcFlag::MADE_HUNT_PATH));
	{ const int32 HuntIds[] = { 0, -1 }; N.BuildPatrolPath(&N.PatrolPathHuntCell, 0, 0, 0, HuntIds, FElysiumNpc::EPatrolPathBuild::Replace); }
	N.NpcFlags.Set(EElysiumNpcFlag::MADE_HUNT_PATH);
	TestEqual(TEXT("0x102afb63 a made hunt path -> 0x7e HUNT"), N.TroikaSelectSchedule(), 0x7e);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19TroikaFixedStatesTest,
	"Elysium.Substrate.NpcKernelSelect19.TroikaSelectSchedule.CaseCDandDefault", GSelect19Flags)
bool FElysiumNpcKernelSelect19TroikaFixedStatesTest::RunTest(const FString&)
{
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(0xc);
	TestEqual(TEXT("0x102b0214 state 0xc -> 0x6b"), N.TroikaSelectSchedule(), 0x6b);
	N.WriteNpcStateRetail(0xd);
	TestEqual(TEXT("0x102b0232 state 0xd -> 0x44"), N.TroikaSelectSchedule(), 0x44);
	N.WriteNpcStateRetail(6);
	TestEqual(TEXT("0x102b0be3 state 6 is the base body's (-> 1)"), N.TroikaSelectSchedule(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19TroikaCrimSuspicionTest,
	"Elysium.Substrate.NpcKernelSelect19.TroikaSelectSchedule.CaseECrimSuspicion", GSelect19Flags)
bool FElysiumNpcKernelSelect19TroikaCrimSuspicionTest::RunTest(const FString&)
{
	// 0x102b0abc: the `m_iSubState` walk 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 2.
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(0xe);
	N.SubState = 0;
	TestEqual(TEXT("0x102b0bbb default -> 0x116"), N.TroikaSelectSchedule(), 0x116);
	TestEqual(TEXT("sub-state 1"), N.SubState, 1);
	TestEqual(TEXT("0x102b0ad2 1 -> 0x117"), N.TroikaSelectSchedule(), 0x117);
	TestEqual(TEXT("0x102b0afa 2 -> 0x118"), N.TroikaSelectSchedule(), 0x118);
	TestEqual(TEXT("0x102b0b22 3 -> 0x119"), N.TroikaSelectSchedule(), 0x119);
	TestEqual(TEXT("0x102b0b7f 4 without SEE_ENEMY -> 0x11a"), N.TroikaSelectSchedule(), 0x11a);
	TestEqual(TEXT("0x102b0b93 5 -> 0x117"), N.TroikaSelectSchedule(), 0x117);
	TestEqual(TEXT("sub-state back to 2"), N.SubState, 2);
	N.SubState = 4;
	N.Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
	TestEqual(TEXT("0x102b0b6b 4 with SEE_ENEMY -> 0x11b"), N.TroikaSelectSchedule(), 0x11b);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19TroikaHelpersTest,
	"Elysium.Substrate.NpcKernelSelect19.TroikaSelectSchedule.AlertHelpers", GSelect19Flags)
bool FElysiumNpcKernelSelect19TroikaHelpersTest::RunTest(const FString&)
{
	// 0x102b8a60, 0x102b9060 and 0x102b8d20.
	FSelect19Fixture F;
	if (!TestNotNull(TEXT("the subject constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	TestEqual(TEXT("0x102b8b4b nothing -> 0"), N.SelectUnknownAlertSchedule(), 0);
	N.NpcFlags.Set(EElysiumNpcFlag::LOOKED_AT_UNKNOWN);
	TestEqual(TEXT("0x102b8b24 LOOKED_AT_UNKNOWN -> 0x5c"), N.SelectUnknownAlertSchedule(), 0x5c);
	N.NpcFlags.Clear(EElysiumNpcFlag::LOOKED_AT_UNKNOWN);
	N.BaseScheduleHost.MemoryBits |= 0x8000000u;
	N.Cognition.Conditions.Set(EElysiumNpcCond::InvestigateSight);
	TestEqual(TEXT("0x102b8ba7 investigating, INVESTIGATE_SIGHT -> 0x5a"), N.SelectUnknownAlertSchedule(), 0x5a);
	N.Cognition.Conditions.Reset();
	N.BaseScheduleHost.MemoryBits = 0;
	TestEqual(TEXT("0x102b9305 nothing heard -> 0"), N.SelectSoundAlertSchedule(), 0);
	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::HearFlinch);
		const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Mask);
		ElysiumSchedule::Start(N.Schedule, ElysiumSched::IDLE_STAND, N);
		N.Cognition.Conditions.Set(EElysiumNpcCond::HearFlinch);
		TestEqual(TEXT("0x102b92e1 HEAR_FLINCH interrupt -> 0x50"), N.SelectSoundAlertSchedule(), 0x50);
	}
	N.Schedule.Clear();
	N.Cognition.Conditions.Reset();
	const double Before = N.Senses.Memory.NextInvestigateSoundTime;
	TestEqual(TEXT("0x102b8d3f no sound source -> 0"), N.SelectSoundSourceSchedule(0x89, 0x73), 0);
	TestEqual(TEXT("and no re-arm"), N.Senses.Memory.NextInvestigateSoundTime, Before);
	return true;
}

// =================================================================================================
// The species bodies.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19AnimalTest,
	"Elysium.Substrate.NpcKernelSelect19.Species.Animal", GSelect19Flags)
bool FElysiumNpcKernelSelect19AnimalTest::RunTest(const FString&)
{
	// 0x1035fb50.
	FSelect19Fixture F(TEXT("CNPC_VAnimal"));
	if (!TestNotNull(TEXT("the animal constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(1);
	N.NpcFlags.Set(EElysiumNpcFlag::DO_STARTLED);
	TestEqual(TEXT("0x1035fb6b DO_STARTLED -> 0x15d"), N.SpeciesSelectSchedule(), 0x15d);
	TestEqual(TEXT("selector 5"), N.SelectScheduleSelector, 5);
	N.bUseInteresting = true;
	TestEqual(TEXT("0x1035fbe3 use_interesting, no place -> 0x156"), N.SpeciesSelectSchedule(), 0x156);
	N.bUseInteresting = false;
	TestEqual(TEXT("0x1035fc0a otherwise the Troika body"), N.SpeciesSelectSchedule(), 0x6b);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19DogTest,
	"Elysium.Substrate.NpcKernelSelect19.Species.Dog", GSelect19Flags)
bool FElysiumNpcKernelSelect19DogTest::RunTest(const FString&)
{
	// 0x103742d0.
	FSelect19Fixture F(TEXT("CNPC_VDog"));
	if (!TestNotNull(TEXT("the dog constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(1);
	const EElysiumNpcCond Snarl = static_cast<EElysiumNpcCond>(0x7b);
	N.Cognition.Conditions.Set(Snarl);
	TestEqual(TEXT("0x103742e6 SHOULD_SNARL -> 0x166"), N.SpeciesSelectSchedule(), 0x166);
	TestFalse(TEXT("and cleared"), N.Cognition.Conditions.Has(Snarl));
	N.bUseInteresting = false;
	TestEqual(TEXT("0x10374305 idle, no interesting place -> 0x164 LOITER"), N.SpeciesSelectSchedule(), 0x164);
	N.WriteNpcStateRetail(3);
	TestEqual(TEXT("0x10374322 alert, not befriended -> the animal, whose Troika alert is 0x4b"),
		N.SpeciesSelectSchedule(), 0x4b);
	N.WriteNpcStateRetail(0xc);
	TestEqual(TEXT("0x1037434c the animal's 0x6b becomes 0x164"), N.SpeciesSelectSchedule(), 0x164);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19ScurryingTest,
	"Elysium.Substrate.NpcKernelSelect19.Species.Scurrying", GSelect19Flags)
bool FElysiumNpcKernelSelect19ScurryingTest::RunTest(const FString&)
{
	// 0x103ac610.
	FSelect19Fixture F(TEXT("CNPC_VScurrying"));
	FElysiumNpcScurrying* S = F.As<FElysiumNpcScurrying>();
	if (!TestNotNull(TEXT("the scurrying constructs"), S))
	{
		return false;
	}
	S->ScurryingFrightDurationSeconds = 4.f;
	S->Senses.Memory.LastSoundBulletImpact.Position = FVector(10.f, 20.f, 0.f);
	S->Cognition.Conditions.Set(EElysiumNpcCond::HearBulletImpact);
	TestEqual(TEXT("0x103ac680 HEAR_BULLET_IMPACT -> 0x162 EVADE"), S->SpeciesSelectSchedule(), 0x162);
	TestEqual(TEXT("m_vecFrightOrigin"), S->ScurryingFrightOrigin, FVector(10.f, 20.f, 0.f));
	TestTrue(TEXT("m_flFrightEndTime = now + duration"), S->ScurryingFrightEndTime >= 4.0);
	S->Cognition.Conditions.Reset();
	TestEqual(TEXT("0x103ac630 inside the fright window -> 0x162"), S->SpeciesSelectSchedule(), 0x162);
	TestEqual(TEXT("selector 0x20"), S->SelectScheduleSelector, 0x20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19ZombieTest,
	"Elysium.Substrate.NpcKernelSelect19.Species.Zombie", GSelect19Flags)
bool FElysiumNpcKernelSelect19ZombieTest::RunTest(const FString&)
{
	// 0x103df2e0.
	FSelect19Fixture F(TEXT("CNPC_VZombie"));
	FElysiumNpcZombie* Z = F.As<FElysiumNpcZombie>();
	if (!TestNotNull(TEXT("the zombie constructs"), Z))
	{
		return false;
	}
	Z->bZombieNeedsCrawlOutOfGround = true;
	TestEqual(TEXT("0x103df2f6 crawl out -> 0x161"), Z->SpeciesSelectSchedule(), 0x161);
	Z->bZombieNeedsCrawlOutOfGround = false;
	Z->ZombieAiType = 7;
	TestEqual(TEXT("0x103df39f type 7 unprovoked -> 0x16b FEAR_SOMETHING"), Z->SpeciesSelectSchedule(), 0x16b);
	Z->Cognition.Conditions.Set(EElysiumNpcCond::HeavyDamage);
	Z->WriteNpcStateRetail(1);
	TestEqual(TEXT("0x103df3bb damage sets type 1; idle type 1 -> SetState(2), 0x169"),
		Z->SpeciesSelectSchedule(), 0x169);
	TestEqual(TEXT("type 1"), Z->ZombieAiType, 1);
	TestEqual(TEXT("0x103df457 COMBAT"), Z->NpcStateRetail(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19HumanTest,
	"Elysium.Substrate.NpcKernelSelect19.Species.Human", GSelect19Flags)
bool FElysiumNpcKernelSelect19HumanTest::RunTest(const FString&)
{
	// 0x10384ee0.
	FSelect19Fixture F(TEXT("CNPC_VHuman"));
	if (!TestNotNull(TEXT("the human constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.WriteNpcStateRetail(2);
	N.Cognition.bCondTookDamage = true;
	N.Cognition.Conditions.Set(EElysiumNpcCond::DetectedAttack);
	TestEqual(TEXT("0x10384f54 a detected attacker it has no memory of -> 0x56"), N.SpeciesSelectSchedule(), 0x56);
	TestEqual(TEXT("0x10384ee9 selector 0x14"), N.SelectScheduleSelector, 0x14);
	TestFalse(TEXT("0x10384efe clears m_bCondTookDamage"), N.Cognition.bCondTookDamage);
	N.WriteNpcStateRetail(1);
	N.Cognition.Conditions.Reset();
	TestEqual(TEXT("0x1038502f any other state is the Troika body"), N.SpeciesSelectSchedule(), 0x6b);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19HumanLineTest,
	"Elysium.Substrate.NpcKernelSelect19.Species.HumanCombatantYukiePatrolGhoul", GSelect19Flags)
bool FElysiumNpcKernelSelect19HumanLineTest::RunTest(const FString&)
{
	// 0x103872d0, 0x103dd6b0, 0x10387d20 idle all chain down to the Troika body; 0x1037bd60 answers
	// its unaware pair first.
	for (const TCHAR* Cls : { TEXT("CNPC_VHumanCombatant"), TEXT("CNPC_VYukie"), TEXT("CNPC_VHumanCombatPatrol") })
	{
		FSelect19Fixture F(Cls);
		if (!TestNotNull(Cls, F.Npc))
		{
			continue;
		}
		F.Npc->WriteNpcStateRetail(1);
		TestEqual(FString::Printf(TEXT("%s idle -> the Troika 0x6b"), Cls), F.Npc->SpeciesSelectSchedule(), 0x6b);
	}
	FSelect19Fixture G(TEXT("CNPC_VGhoulCroucher"));
	FElysiumNpcGhoulCroucher* Ghoul = G.As<FElysiumNpcGhoulCroucher>();
	if (TestNotNull(TEXT("the croucher constructs"), Ghoul))
	{
		Ghoul->bWasDisturbed = false;
		TestEqual(TEXT("0x1037bdd0 undisturbed -> 0x158"), Ghoul->SpeciesSelectSchedule(), 0x158);
		Ghoul->bWasDisturbed = true;
		Ghoul->bUnawareExited = false;
		TestEqual(TEXT("0x1037bdff not yet exited -> 0x159"), Ghoul->SpeciesSelectSchedule(), 0x159);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19Guard1Test,
	"Elysium.Substrate.NpcKernelSelect19.Species.Guard1", GSelect19Flags)
bool FElysiumNpcKernelSelect19Guard1Test::RunTest(const FString&)
{
	// 0x1037d130.
	FSelect19Fixture F(TEXT("CNPC_VGuard1"));
	if (!TestNotNull(TEXT("the guard constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.NpcFlags.Set(EElysiumNpcFlag::DO_STARTLED);
	TestEqual(TEXT("0x1037d14b DO_STARTLED -> 0xf1"), N.SpeciesSelectSchedule(), 0xf1);
	TestEqual(TEXT("selector 0x12"), N.SelectScheduleSelector, 0x12);
	N.WriteNpcStateRetail(0xc);
	TestEqual(TEXT("0x1037d1ee state 0xc without the interrupt -> SetState(1), then the chain"),
		N.SpeciesSelectSchedule(), 0x6b);
	TestEqual(TEXT("IDLE"), N.NpcStateRetail(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19CopTest,
	"Elysium.Substrate.NpcKernelSelect19.Species.Cop", GSelect19Flags)
bool FElysiumNpcKernelSelect19CopTest::RunTest(const FString&)
{
	// 0x10371ee0: the spawner-budget arm.
	FSelect19Fixture F(TEXT("CNPC_VCop"));
	FElysiumNpcCop* Cop = F.As<FElysiumNpcCop>();
	if (!TestNotNull(TEXT("the cop constructs"), Cop))
	{
		return false;
	}
	Cop->WriteNpcStateRetail(1);
	Cop->bCameFromSpawner = true;
	Cop->bCopCountedSecond = false;
	const int32 SavedAlive = FElysiumNpcCop::CopAliveCensus();
	const int32 SavedSecond = FElysiumNpcCop::CopSecondCensus();
	FElysiumNpcCop::CopAliveCensus() = 2;
	FElysiumNpcCop::CopSecondCensus() = 0;
	TestEqual(TEXT("0x10372070 a small census -> 0x170 WANDER_PATROL"), Cop->SpeciesSelectSchedule(), 0x170);
	FElysiumNpcCop::CopAliveCensus() = 9;
	TestEqual(TEXT("0x103720a5 a large census -> 0x16e WANDER_AND_VANISH"), Cop->SpeciesSelectSchedule(), 0x16e);
	TestTrue(TEXT("0x10372093 the claim byte"), Cop->bCopCountedSecond);
	TestEqual(TEXT("0x1037209a the second census counts it"), FElysiumNpcCop::CopSecondCensus(), 1);
	TestEqual(TEXT("selector 0xc (the chain did not run)"), Cop->SelectScheduleSelector, 0xc);
	FElysiumNpcCop::CopAliveCensus() = SavedAlive;
	FElysiumNpcCop::CopSecondCensus() = SavedSecond;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19PedestrianTest,
	"Elysium.Substrate.NpcKernelSelect19.Species.Pedestrian", GSelect19Flags)
bool FElysiumNpcKernelSelect19PedestrianTest::RunTest(const FString&)
{
	// 0x103a29f0.
	FSelect19Fixture F(TEXT("CNPC_VPedestrian"));
	FElysiumNpcPedestrian* P = F.As<FElysiumNpcPedestrian>();
	if (!TestNotNull(TEXT("the pedestrian constructs"), P))
	{
		return false;
	}
	P->bPedestrianFirstThink = true;
	TestEqual(TEXT("0x103a2a05 first think -> 0xfe"), P->SpeciesSelectSchedule(), 0xfe);
	TestFalse(TEXT("and consumed"), P->bPedestrianFirstThink);
	P->Cognition.Conditions.Set(EElysiumNpcCond::PassOut);
	TestEqual(TEXT("0x103a2a3f PASS_OUT -> 0xfa KNOCKOUT"), P->SpeciesSelectSchedule(), 0xfa);
	P->Cognition.Conditions.Reset();
	P->WriteNpcStateRetail(1);
	P->Cognition.Conditions.Set(EElysiumNpcCond::InvestigateSound);
	// Retail compares the SQUARED distance to the best sound against 256.0: a sound at the NPC's own
	// feet is inside it and turns without the roll.
	P->Senses.Memory.BestSound.Position = P->Origin;
	const int32 Answer = P->SpeciesSelectSchedule();
	TestTrue(TEXT("0x103a2b67 a near sound -> 0x157 TURN_TO_SOUND"), Answer == 0x157);
	TestTrue(TEXT("0x103a2b80 INITIAL_FLEE"), P->NpcFlags.Has(EElysiumNpcFlag::INITIAL_FLEE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19VampireBossLineTest,
	"Elysium.Substrate.NpcKernelSelect19.Species.AsianVampireChangBrosSheriffSabbat", GSelect19Flags)
bool FElysiumNpcKernelSelect19VampireBossLineTest::RunTest(const FString&)
{
	// 0x10360eb0, 0x1036b250, 0x103ae8c0, 0x103a70c0, 0x103aa510.
	{
		FSelect19Fixture F(TEXT("CNPC_VAsianVampire"));
		if (TestNotNull(TEXT("the asian vampire constructs"), F.Npc))
		{
			F.Npc->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
			FElysiumNpc::SetSelect19ConVar(FElysiumNpc::ESelect19ConVar::AsianVampForceJumpUp, 1);
			TestEqual(TEXT("0x10360f75 asianvamp_force_jump_up -> 0x15a"), F.Npc->SpeciesSelectSchedule(), 0x15a);
			TestEqual(TEXT("selector 6"), F.Npc->SelectScheduleSelector, 6);
		}
	}
	{
		FSelect19Fixture F(TEXT("CNPC_VChangBros"));
		if (TestNotNull(TEXT("the chang constructs"), F.Npc))
		{
			F.Npc->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
			F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyUnreachable);
			TestEqual(TEXT("0x1036b3bf ENEMY_UNREACHABLE -> 0x15d"), F.Npc->SpeciesSelectSchedule(), 0x15d);
			FElysiumNpc::SetSelect19ConVar(FElysiumNpc::ESelect19ConVar::ChangBrosForceUnitedAttack, 1);
			TestEqual(TEXT("0x1036b2e2 changbros_force_united_attack -> 0x15e"), F.Npc->SpeciesSelectSchedule(), 0x15e);
		}
	}
	{
		FSelect19Fixture F(TEXT("CNPC_VSheriffMan"));
		FElysiumNpcSheriffMan* S = F.As<FElysiumNpcSheriffMan>();
		if (TestNotNull(TEXT("the sheriff constructs"), S))
		{
			S->bSheriffActivated = false;
			S->WriteNpcStateRetail(1);
			TestEqual(TEXT("0x103ae977 unactivated -> the human chain"), S->SpeciesSelectSchedule(), 0x6b);
			S->bSheriffActivated = true;
			S->WriteNpcStateRetail(5);
			TestEqual(TEXT("0x103ae950 transforming -> 0x158"), S->SpeciesSelectSchedule(), 0x158);
		}
	}
	{
		FSelect19Fixture F(TEXT("CNPC_VSabbatLeader"));
		FElysiumNpcSabbatLeader* L = F.As<FElysiumNpcSabbatLeader>();
		if (TestNotNull(TEXT("the leader constructs"), L))
		{
			L->WriteNpcStateRetail(5);
			TestEqual(TEXT("0x103a7127 transforming -> 0x158"), L->SpeciesSelectSchedule(), 0x158);
			L->WriteNpcStateRetail(1);
			L->SabbatLeaderRoarAttackCount = 0;
			TestEqual(TEXT("0x103a71c5 no roars left -> 0x165 ROAR"), L->SpeciesSelectSchedule(), 0x165);
			TestEqual(TEXT("0x103a71c7 refilled to 3"), L->SabbatLeaderRoarAttackCount, 3);
			L->WriteNpcStateRetail(2);
			ElysiumNpcEnemy::SetEnemy(*L, FElysiumEntityHandle::Invalid());
			L->PreSelectSchedule();
			TestEqual(TEXT("0x103aa584 combat with no enemy is written IDLE, raw"), L->NpcStateRetail(), 1);
			TestEqual(TEXT("0x103aa58e and the ideal state"), L->IdealStateRetail(), 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19GargoyleHengeyokaiTest,
	"Elysium.Substrate.NpcKernelSelect19.Species.GargoyleHengeyokai", GSelect19Flags)
bool FElysiumNpcKernelSelect19GargoyleHengeyokaiTest::RunTest(const FString&)
{
	// 0x103788d0 and 0x1037fca0.
	{
		FSelect19Fixture F(TEXT("CNPC_VGargoyle"));
		FElysiumNpcGargoyle* G = F.As<FElysiumNpcGargoyle>();
		if (TestNotNull(TEXT("the gargoyle constructs"), G))
		{
			G->WriteNpcStateRetail(1);
			TestEqual(TEXT("0x103789cc not in combat -> the human chain"), G->SpeciesSelectSchedule(), 0x6b);
			G->WriteNpcStateRetail(2);
			G->GargoyleShunnedFindPillar = 0;
			G->SpeciesSelectSchedule();
			TestEqual(TEXT("0x10378f80 no enemy, no pillar: the shunned-pillar word"), G->GargoyleShunnedFindPillar, 2);
		}
	}
	{
		FSelect19Fixture F(TEXT("CNPC_VHengeyokai"));
		FElysiumNpcHengeyokai* H = F.As<FElysiumNpcHengeyokai>();
		if (TestNotNull(TEXT("the hengeyokai constructs"), H))
		{
			H->WriteNpcStateRetail(5);
			TestEqual(TEXT("0x1037fcbc transforming -> 0x16f"), H->SpeciesSelectSchedule(), 0x16f);
			H->WriteNpcStateRetail(2);
			H->NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
			H->Cognition.Conditions.Set(EElysiumNpcCond::EnemyDead);
			TestEqual(TEXT("0x1037fce7 enemy dead while carrying -> 0x15e DROP_FISH"), H->SpeciesSelectSchedule(), 0x15e);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19MingXiaoTest,
	"Elysium.Substrate.NpcKernelSelect19.Species.MingXiao", GSelect19Flags)
bool FElysiumNpcKernelSelect19MingXiaoTest::RunTest(const FString&)
{
	// 0x10394120 and 0x103941e0.
	FSelect19Fixture F(TEXT("CNPC_VMingXiao"));
	FElysiumNpcMingXiao* M = F.As<FElysiumNpcMingXiao>();
	if (!TestNotNull(TEXT("ming xiao constructs"), M))
	{
		return false;
	}
	M->WriteNpcStateRetail(5);
	TestEqual(TEXT("0x10394133 transforming -> 0x157"), M->PreSelectSchedule(), 0x157);
	TestEqual(TEXT("selector 0x19"), M->SelectScheduleSelector, 0x19);
	M->WriteNpcStateRetail(2);
	TestEqual(TEXT("0x103941a3 otherwise 0 after the weapon switch"), M->PreSelectSchedule(), 0);
	M->WriteNpcStateRetail(1);
	TestEqual(TEXT("0x10394485 idle -> 0x44"), M->SpeciesSelectSchedule(), 0x44);
	M->WriteNpcStateRetail(2);
	M->Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x78));
	TestEqual(TEXT("0x10394231 CAN_ATTACK_FRONT_LEFT -> 0x159"), M->SpeciesSelectSchedule(), 0x159);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19TzimisceLineTest,
	"Elysium.Substrate.NpcKernelSelect19.Species.TzimisceHeadClawRunner", GSelect19Flags)
bool FElysiumNpcKernelSelect19TzimisceLineTest::RunTest(const FString&)
{
	// 0x103bb7c0, 0x103c1610, 0x103c3310.
	{
		FSelect19Fixture F(TEXT("CNPC_VTzimisce"));
		FElysiumNpcTzimisce* T = F.As<FElysiumNpcTzimisce>();
		if (TestNotNull(TEXT("the tzimisce constructs"), T))
		{
			T->NpcFlags.Set(EElysiumNpcFlag::DO_STARTLED);
			TestEqual(TEXT("0x103bb7e0 DO_STARTLED -> 0x187"), T->SpeciesSelectSchedule(), 0x187);
			TestEqual(TEXT("selector 0x26"), T->SelectScheduleSelector, 0x26);
			T->WriteNpcStateRetail(0xb);
			T->NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
			TestEqual(TEXT("0x103bb850 hunting while carrying -> 0x198 DROP_BODY"), T->SpeciesSelectSchedule(), 0x198);
			T->NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);
			T->WriteNpcStateRetail(2);
			T->NpcFlags.Set(EElysiumNpcFlag::FINDING_BODY);
			TestEqual(TEXT("0x103bbca8 finding a body with no search -> 0x170 MELEE_IDLE"),
				T->SpeciesSelectSchedule(), 0x170);
			TestFalse(TEXT("0x103bbc3c FINDING_BODY cleared"), T->NpcFlags.Has(EElysiumNpcFlag::FINDING_BODY));
		}
	}
	{
		FSelect19Fixture F(TEXT("CNPC_VTzimisceHeadClaw"));
		if (TestNotNull(TEXT("the head claw constructs"), F.Npc))
		{
			F.Npc->WriteNpcStateRetail(2);
			F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::ShouldCharge);
			TestEqual(TEXT("0x103c1633 SHOULD_CHARGE -> 0x158"), F.Npc->SpeciesSelectSchedule(), 0x158);
		}
	}
	{
		FSelect19Fixture F(TEXT("CNPC_VTzimisceRunner"));
		if (TestNotNull(TEXT("the runner constructs"), F.Npc))
		{
			F.Npc->WriteNpcStateRetail(1);
			TestEqual(TEXT("0x103c3431 not in combat -> the Troika body"), F.Npc->SpeciesSelectSchedule(), 0x6b);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19WerewolfTest,
	"Elysium.Substrate.NpcKernelSelect19.Species.Werewolf", GSelect19Flags)
bool FElysiumNpcKernelSelect19WerewolfTest::RunTest(const FString&)
{
	// 0x103cee70.
	FSelect19Fixture F(TEXT("CNPC_VWerewolf"));
	if (!TestNotNull(TEXT("the werewolf constructs"), F.Npc))
	{
		return false;
	}
	FElysiumNpc::SetSelect19ConVar(FElysiumNpc::ESelect19ConVar::WerewolfForceTeleport, 1);
	TestEqual(TEXT("0x103cef07 werewolf_force_teleport -> 0x157"), F.Npc->SpeciesSelectSchedule(), 0x157);
	TestEqual(TEXT("selector 0x29"), F.Npc->SelectScheduleSelector, 0x29);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSelect19ControllerLineTest,
	"Elysium.Substrate.NpcKernelSelect19.Species.ControllerLine", GSelect19Flags)
bool FElysiumNpcKernelSelect19ControllerLineTest::RunTest(const FString&)
{
	// The three rows landed in story 5 fold A2: 0x103a46b0, 0x10375d90, 0x103dceb0.
	{
		FSelect19Fixture F(TEXT("CNPC_VPlayerController"));
		if (TestNotNull(TEXT("the controller constructs"), F.Npc))
		{
			F.Npc->WriteNpcStateRetail(1);
			TestEqual(TEXT("0x103a46b0 idle -> 0x6b"), F.Npc->PreSelectSchedule(), 0x6b);
			F.Npc->WriteNpcStateRetail(2);
			F.Npc->ScheduleHost.ForcedSchedule = 0x77;
			TestEqual(TEXT("0x103a46b0 otherwise the Troika pre-selector (forced schedule)"),
				F.Npc->PreSelectSchedule(), 0x77);
		}
	}
	{
		FSelect19Fixture F(TEXT("CNPC_VWolfMorph"));
		if (TestNotNull(TEXT("the wolf morph constructs"), F.Npc))
		{
			TestEqual(TEXT("0x103dceb0 not running the morph -> 0x158"), F.Npc->SpeciesSelectSchedule(), 0x158);
		}
	}
	{
		FSelect19Fixture F(TEXT("CNPC_VFrenzyShadow"));
		if (TestNotNull(TEXT("the frenzy shadow constructs"), F.Npc))
		{
			F.Npc->WriteNpcStateRetail(2);
			ElysiumNpcEnemy::SetEnemy(*F.Npc, F.Other->Handle);
			TestEqual(TEXT("0x10375d90 combat, one live enemy -> 0x161 FEED"), F.Npc->SpeciesSelectSchedule(), 0x161);
		}
	}
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
