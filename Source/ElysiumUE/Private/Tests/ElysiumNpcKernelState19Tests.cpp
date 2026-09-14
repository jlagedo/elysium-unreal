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
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29e, family **State19**. Every assertion is read off the decompiled C or the listing
// (story 29e's banked reading). The central premise: Troika `SelectIdealState` answers nothing with
// no program installed, because every arm but the four flee arms and case 0xe's damage uses
// `HasInterruptCondition`.

static constexpr EAutomationTestFlags GState19Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	struct FState19Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumPlayer* Player = nullptr;

		FState19Fixture()
			: World([]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("state19_kernel"), 919);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpc(TEXT("guard"), FVector::ZeroVector, TEXT("npc_VHumanCombatant"));
					Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
					return Builder;
				}())
		{
			Guard = World.Npc(TEXT("guard"));
			Other = World.Npc(TEXT("other"));
			Player = World.Player();
			FElysiumNpcWorldFixture::Quiet({ Guard, Other });
		}
	};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelState19SetStateTest,
	"Elysium.Substrate.NpcKernelState19.SetState", GState19Flags)
bool FElysiumNpcKernelState19SetStateTest::RunTest(const FString&)
{
	FState19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	
	TestEqual(TEXT("1026e340 admission idle is retail 1"), N.NpcStateRetail(), 1);
	N.SetState(2);
	TestEqual(TEXT("1026e340 writes +0x5cc0"), N.NpcStateRetail(), 2);
	TestEqual(TEXT("1026e340 writes +0x5cc4 too"), N.IdealStateRetail(), 2);
	TestEqual(TEXT("and maps COMBAT onto the typed mind"),
		static_cast<int32>(N.GetMind().State()), static_cast<int32>(EElysiumNpcState::Combat));

	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	TestNotNull(TEXT("the enemy is live before the idle strip"), N.GetEnemy());
	N.SetState(1);
	TestNull(TEXT("1026e37e SetEnemy(NULL) on idle with a live enemy"), N.GetEnemy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelState19TroikaSelectIdealStateTest,
	"Elysium.Substrate.NpcKernelState19.TroikaSelectIdealState", GState19Flags)
bool FElysiumNpcKernelState19TroikaSelectIdealStateTest::RunTest(const FString&)
{
	FState19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
		N.SetRetailClassForTests(TEXT("CAI_BaseNPCTroika"));

	// `10269d30` opens `if (*(int *)(this + 0x5c38) == 0) return 0;` — no program, nothing.
	N.Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	TestEqual(TEXT("102ad660 with no schedule answers the current idle"),
		N.SelectIdealStateRetail(), 1);

	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	TestEqual(TEXT("102ad660 idle damage with no schedule does not promote"),
		N.SelectIdealStateRetail(), 1);

	// Interrupt NEW_ENEMY with a program that lists it.
	N.Cognition.Conditions.Reset();
	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::NewEnemy);
		const ElysiumSchedule::FInterruptMaskScope Scope(EElysiumScheduleId::IdleStand, Mask);
		TestTrue(TEXT("the interrupt carrier installs"),
			ElysiumSchedule::Start(N.Schedule, EElysiumScheduleId::IdleStand, N));
		N.Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
		TestEqual(TEXT("102ad660 interrupt NEW_ENEMY from idle is COMBAT"),
			N.SelectIdealStateRetail(), 2);
	}
	N.Schedule.Clear();
	N.Cognition.Conditions.Reset();

	// Bare flee arms: no schedule required.
	N.Cognition.Conditions.Set(EElysiumNpcCond::SupernaturalFleeLevel);
	TestEqual(TEXT("102ad660 idle 0x21 flee uses bare HasCondition"),
		N.SelectIdealStateRetail(), 8);
	TestTrue(TEXT("and ORs INITIAL_FLEE 0x100"),
		N.NpcFlags.Has(EElysiumNpcFlag::INITIAL_FLEE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelState19BaseSelectIdealStateTest,
	"Elysium.Substrate.NpcKernelState19.BaseSelectIdealState", GState19Flags)
bool FElysiumNpcKernelState19BaseSelectIdealStateTest::RunTest(const FString&)
{
	FState19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	
	N.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	TestEqual(TEXT("1026f660 idle damage with no schedule does not promote"),
		N.BaseSelectIdealState(), 1);

	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::LightDamage);
		const ElysiumSchedule::FInterruptMaskScope Scope(EElysiumScheduleId::IdleStand, Mask);
		ElysiumSchedule::Start(N.Schedule, EElysiumScheduleId::IdleStand, N);
		N.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
		N.bNoAlertState = true;
		TestEqual(TEXT("1026f660 base idle LIGHT_DAMAGE promotes with no m_bNoAlertState test"),
			N.BaseSelectIdealState(), 3);
		TestFalse(TEXT("and clears m_bCondTookDamage"), N.Cognition.bCondTookDamage);
	}
	N.Schedule.Clear();
	N.Cognition.Conditions.Reset();
	N.WriteNpcStateRetail(2);
	TestEqual(TEXT("1026f660 combat with no enemy falls to ALERT"),
		N.BaseSelectIdealState(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelState19FifteenByteTest,
	"Elysium.Substrate.NpcKernelState19.FifteenByteSpecies", GState19Flags)
bool FElysiumNpcKernelState19FifteenByteTest::RunTest(const FString&)
{
	FState19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	
	struct FRow { const TCHAR* Cls; const TCHAR* Addr; int32 Tag; };
	const FRow Rows[] = {
		{ TEXT("CNPC_VAsianVampire"), TEXT("0x10361060"), 6 },
		{ TEXT("CNPC_VBach"),         TEXT("0x10363b40"), 0xe },
		{ TEXT("CNPC_VBatSwarm"),     TEXT("0x103674a0"), 7 },
		{ TEXT("CNPC_VChangBros"),    TEXT("0x1036b500"), 0xa },
		{ TEXT("CNPC_VCombatman"),    TEXT("0x10370320"), 0xb },
		{ TEXT("CNPC_VGargoyle"),     TEXT("0x10378b60"), 0x11 },
		{ TEXT("CNPC_VHengeyokai"),   TEXT("0x10380100"), 0x13 },
		{ TEXT("CNPC_VMoleman"),      TEXT("0x1039fe10"), 0x1b },
		{ TEXT("CNPC_VSheriffMan"),   TEXT("0x103aeac0"), 0x21 },
		{ TEXT("CNPC_VSheriffSwarm"), TEXT("0x103b2450"), 0x22 },
		{ TEXT("CNPC_VHunter"),       TEXT("0x10388ab0"), 0x17 },
		{ TEXT("CNPC_VYukie"),        TEXT("0x103dd780"), 0x2a },
	};
	for (const FRow& Row : Rows)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(Row.Cls);
		const FElysiumNpcClassSlot* Slot = ElysiumNpcKernelClass::OverrideOf(Cls, 461);
		if (!TestNotNull(FString::Printf(TEXT("%s has a slot-461 override"), Row.Cls), Slot))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s's body is %s"), Row.Cls, Row.Addr),
			FString(Slot->Address), FString(Row.Addr));
		N.SetRetailClassForTests(Row.Cls);
		N.SelectIdealStateSelector = 0;
		N.SelectIdealStateRetail();
		TestTrue(FString::Printf(TEXT("%s wrote a selector (tag %d then the chain)"), Row.Cls, Row.Tag),
			N.SelectIdealStateSelector != 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelState19BachOnStateChangeTest,
	"Elysium.Substrate.NpcKernelState19.BachOnStateChange", GState19Flags)
bool FElysiumNpcKernelState19BachOnStateChangeTest::RunTest(const FString&)
{
	FState19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
		N.SetRetailClassForTests(TEXT("CNPC_VBach"));
	N.bCanFightYet = false;
	N.SetState(2);
	TestEqual(TEXT("103639b0 snaps COMBAT back to idle while m_bCanFightYet is 0"),
		N.NpcStateRetail(), 1);
	N.bCanFightYet = true;
	N.SetState(2);
	TestEqual(TEXT("103639b0 chains when m_bCanFightYet is set"),
		N.NpcStateRetail(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelState19SabbatLeaderTest,
	"Elysium.Substrate.NpcKernelState19.SabbatLeaderSelectIdealState", GState19Flags)
bool FElysiumNpcKernelState19SabbatLeaderTest::RunTest(const FString&)
{
	FState19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
		N.SetRetailClassForTests(TEXT("CNPC_VSabbatLeader"));
	N.bSabbatLeaderActivated = false;
	TestEqual(TEXT("103a7450 unactivated leader is IDLE"),
		N.SelectIdealStateRetail(), 1);
	N.bSabbatLeaderActivated = true;
	N.WriteNpcStateRetail(1);
	N.Senses.Memory.ClosestPlayer = F.Player->Handle;
	TestEqual(TEXT("103a7450 activated idle adopts the closest player as COMBAT"),
		N.SelectIdealStateRetail(), 2);
	TestNotNull(TEXT("and SetEnemy took the player"), N.GetEnemy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelState19HumanHuntTest,
	"Elysium.Substrate.NpcKernelState19.HumanHuntConVar", GState19Flags)
bool FElysiumNpcKernelState19HumanHuntTest::RunTest(const FString&)
{
	FState19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
		N.SetRetailClassForTests(TEXT("CNPC_VHuman"));
	N.WriteNpcStateRetail(2);
	N.HuntConVarIsCommand = false;
	N.HuntConVarRawWord = 1.f;
	TestEqual(TEXT("103851e0 no-enemy combat with DAT_1092447c[0xb] != 0 is HUNT"),
		N.SelectIdealStateRetail(), 0xb);
	N.HuntConVarIsCommand = true;
	TestEqual(TEXT("103851e0 IsCommand() refuses the hunt arm"),
		N.SelectIdealStateRetail(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelState19CameraPreSelectTest,
	"Elysium.Substrate.NpcKernelState19.CameraPreSelectIdealState", GState19Flags)
bool FElysiumNpcKernelState19CameraPreSelectTest::RunTest(const FString&)
{
	FState19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
		N.SetRetailClassForTests(TEXT("CNPC_VCamera"));
	TestEqual(TEXT("10368f80 unconditionally writes ALERT"),
		N.PreSelectIdealStateRetail(), 3);
	TestEqual(TEXT("and the selector tag is 9"), N.SelectIdealStateSelector, 9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelState19NoAlertWithScheduleTest,
	"Elysium.Substrate.NpcKernelState19.NoAlertStateBaseTail", GState19Flags)
bool FElysiumNpcKernelState19NoAlertWithScheduleTest::RunTest(const FString&)
{
	FState19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
		N.SetRetailClassForTests(TEXT("CAI_BaseNPCTroika"));
	N.bNoAlertState = true;
	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::LightDamage);
		const ElysiumSchedule::FInterruptMaskScope Scope(EElysiumScheduleId::IdleStand, Mask);
		ElysiumSchedule::Start(N.Schedule, EElysiumScheduleId::IdleStand, N);
		N.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
		TestEqual(TEXT("no_alert_state skips Troika damage then the base tail still promotes"),
			N.SelectIdealStateRetail(), 3);
	}
	return true;
}

// --- The four species ladders story 29e wave 1 left partial ---------------------------------------

namespace
{
	// Every arm of Guard1's and Cop's law ladders, and Tzimisce's and Pedestrian's own states,
	// needs an INTERRUPT mask because retail reaches them through `thunk_FUN_10269d30`.
	int32 State19TestsIdealUnderMask(FElysiumNpc& N, EElysiumNpcCond Cond)
	{
		FElysiumNpcConditions Mask;
		Mask.Set(Cond);
		const ElysiumSchedule::FInterruptMaskScope Scope(EElysiumScheduleId::IdleStand, Mask);
		ElysiumSchedule::Start(N.Schedule, EElysiumScheduleId::IdleStand, N);
		N.Cognition.Conditions.Set(Cond);
		const int32 Answer = N.SelectIdealStateRetail();
		N.Schedule.Clear();
		N.Cognition.Conditions.Reset();
		return Answer;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelState19Guard1Test,
	"Elysium.Substrate.NpcKernelState19.Guard1SelectIdealState", GState19Flags)
bool FElysiumNpcKernelState19Guard1Test::RunTest(const FString&)
{
	FState19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard)
		|| !TestNotNull(TEXT("the player constructs"), F.Player))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.SetRetailClassForTests(TEXT("CNPC_VGuard1"));
	N.Senses.Memory.ClosestPlayer = F.Player->Handle;

	// `1037d2a4`: the arm needs the closest player to BE that channel's offender. With no offender
	// recorded the compare fails and the body chains `CNPC_VHuman`. SUPERNATURAL_ATTACK is the
	// discriminator to use for that, because the FLEE conditions have a bare arm of their own in
	// `0x102ad660` further down the chain and would answer 8 either way.
	N.Witness.Channel(ElysiumNpcWitness::EChannel::Supernatural).Offender =
		FElysiumEntityHandle::Invalid();
	TestEqual(TEXT("1037d290 the law arm refuses when the offender is not the closest player"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::SupernaturalAttackLevel), 1);

	N.Witness.Channel(ElysiumNpcWitness::EChannel::Supernatural).Offender = F.Player->Handle;
	N.Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).Offender = F.Player->Handle;
	TestEqual(TEXT("1037d290 0x215 idle SUPERNATURAL_FLEE -> FLEE"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::SupernaturalFleeLevel), 8);
	TestTrue(TEXT("...and latches the guard's hate (0x1037e2d0)"), N.bGuard1HatesPlayer);
	N.bGuard1HatesPlayer = false;
	N.WriteNpcStateRetail(1);
	TestEqual(TEXT("1037d290 0x222 idle CRIMINAL_FLEE -> FLEE"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::CriminalFleeLevel), 8);
	N.WriteNpcStateRetail(1);
	TestEqual(TEXT("1037d290 0x22f idle SUPERNATURAL_ATTACK -> COMBAT"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::SupernaturalAttackLevel), 2);
	N.WriteNpcStateRetail(1);
	TestEqual(TEXT("1037d290 0x23c idle CRIMINAL_ATTACK -> COMBAT"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::CriminalAttackLevel), 2);
	// `1037d5d5`: INVESTIGATE_LEVEL has no offender compare and answers the guard's own state 0xc.
	N.WriteNpcStateRetail(1);
	TestEqual(TEXT("1037d290 0x243 idle INVESTIGATE_LEVEL -> 0xc"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::InvestigateLevel), 0xc);

	// `1037d8e7`: alert's hear arm answers HUNT 0xb while `m_fHatesPlayer` stands, ALERT otherwise.
	N.WriteNpcStateRetail(3);
	N.bGuard1HatesPlayer = true;
	TestEqual(TEXT("1037d290 0x291 alert hear with m_fHatesPlayer -> HUNT"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::HearCombat), 0xb);
	N.WriteNpcStateRetail(3);
	N.bGuard1HatesPlayer = false;
	TestEqual(TEXT("1037d290 0x296 alert hear without it -> ALERT"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::HearCombat), 3);

	// `1037da3c`: the hunt state leaves on SEE_PLAYER only while the guard hates the player. With
	// no hate the body chains `CNPC_VHuman`, whose 0xb ladder reads neither SEE_PLAYER nor
	// `ShouldGoToIdleState`, so the ideal is answered untouched.
	N.WriteNpcStateRetail(0xb);
	N.WriteIdealStateRetail(0xb);
	N.bGuard1HatesPlayer = false;
	TestEqual(TEXT("1037d290 0x2b1 hunt SEE_PLAYER without hate falls to CNPC_VHuman"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::SeePlayer), 0xb);
	N.WriteNpcStateRetail(0xb);
	N.bGuard1HatesPlayer = true;
	TestEqual(TEXT("1037d290 0x2b1 hunt SEE_PLAYER with hate -> COMBAT"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::SeePlayer), 2);

	// `1037dd4d`: state 0xc ends in IDLE unconditionally and never reaches the chain.
	N.WriteNpcStateRetail(0xc);
	N.WriteIdealStateRetail(0xc);
	N.Cognition.Conditions.Reset();
	TestEqual(TEXT("1037d290 0x2ee state 0xc with nothing standing -> IDLE"),
		N.SelectIdealStateRetail(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelState19CopTest,
	"Elysium.Substrate.NpcKernelState19.CopSelectIdealState", GState19Flags)
bool FElysiumNpcKernelState19CopTest::RunTest(const FString&)
{
	FState19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard)
		|| !TestNotNull(TEXT("the player constructs"), F.Player))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.SetRetailClassForTests(TEXT("CNPC_VCop"));
	N.Senses.Memory.ClosestPlayer = F.Player->Handle;

	// `103723f0`: every test is the BARE form, so the pre-pass answers with no program installed.
	// INVESTIGATE_SOUND alone is not enough — BEING_ATTACKED must stand too.
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::InvestigateSound);
	N.Cognition.Conditions.Set(EElysiumNpcCond::HearCombat);
	N.Senses.Memory.LastSoundCombat.Source = F.Player->Handle;
	TestEqual(TEXT("103723f0 INVESTIGATE_SOUND without BEING_ATTACKED refuses"),
		N.CopSelectIdealStatePrePass(), 0);
	N.Cognition.Conditions.Set(EElysiumNpcCond::BeingAttacked);
	TestEqual(TEXT("103723f0 a combat sound owned by the closest player answers COMBAT"),
		N.CopSelectIdealStatePrePass(), 2);

	// `1037252b`: `m_bCondTookDamage` is cleared whenever damage stands, even when the attacker is
	// not the closest player and the pre-pass refuses.
	N.Cognition.Conditions.Reset();
	N.Cognition.bCondTookDamage = true;
	N.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	N.Senses.Memory.LastDamageAttacker = FElysiumEntityHandle::Invalid();
	TestEqual(TEXT("103723f0 damage from someone else refuses"), N.CopSelectIdealStatePrePass(), 0);
	TestFalse(TEXT("...but m_bCondTookDamage is cleared anyway"), N.Cognition.bCondTookDamage);
	N.Senses.Memory.LastDamageAttacker = F.Player->Handle;
	N.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	TestEqual(TEXT("103723f0 damage from the closest player answers COMBAT"),
		N.CopSelectIdealStatePrePass(), 2);

	// `103726c0`: idle runs the pre-pass and writes its answer as the ideal state.
	N.WriteNpcStateRetail(1);
	N.WriteIdealStateRetail(1);
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	TestEqual(TEXT("103726c0 idle with the pre-pass taking -> COMBAT"),
		N.SelectIdealStateRetail(), 2);
	TestEqual(TEXT("...and the answer is the ideal state"), N.IdealStateRetail(), 2);

	// `103726f4`: the crazy state's four law arms, then its unconditional IDLE tail.
	N.WriteNpcStateRetail(0xc);
	TestEqual(TEXT("103726c0 0x5e1 crazy SUPERNATURAL_FLEE -> FLEE"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::SupernaturalFleeLevel), 8);
	N.WriteNpcStateRetail(0xc);
	TestEqual(TEXT("103726c0 0x5f1 crazy CRIMINAL_FLEE -> FLEE"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::CriminalFleeLevel), 8);
	N.WriteNpcStateRetail(0xc);
	TestEqual(TEXT("103726c0 0x601 crazy SUPERNATURAL_ATTACK -> COMBAT"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::SupernaturalAttackLevel), 2);
	N.WriteNpcStateRetail(0xc);
	TestEqual(TEXT("103726c0 0x611 crazy CRIMINAL_ATTACK -> COMBAT"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::CriminalAttackLevel), 2);
	N.WriteNpcStateRetail(0xc);
	N.WriteIdealStateRetail(0xc);
	N.Cognition.Conditions.Reset();
	TestEqual(TEXT("103726c0 0x616 crazy with nothing standing -> IDLE"),
		N.SelectIdealStateRetail(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelState19TzimisceTest,
	"Elysium.Substrate.NpcKernelState19.TzimisceSelectIdealState", GState19Flags)
bool FElysiumNpcKernelState19TzimisceTest::RunTest(const FString&)
{
	FState19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.SetRetailClassForTests(TEXT("CNPC_VTzimisce"));

	// `103bd6c4`: NEW_ENEMY alone from idle — the base pairs it with SEE_ENEMY, this body does not.
	TestEqual(TEXT("103bd690 0xd42 idle NEW_ENEMY -> COMBAT"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::NewEnemy), 2);
	N.WriteNpcStateRetail(1);
	TestEqual(TEXT("103bd690 0xd4c idle LIGHT_DAMAGE -> ALERT"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::LightDamage), 3);

	// `103bd8ba`: this class accepts sound type **4** where the base accepts only 1 / 8 / 0x10.
	N.WriteNpcStateRetail(1);
	N.Senses.Memory.BestSound.TypeMask = ElysiumGameSounds::Player;
	TestEqual(TEXT("103bd690 0xd84 idle hear with sound type 4 -> ALERT"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::HearPlayer), 3);
	// A type outside the set refuses THIS body's arm and chains Troika — whose own `0x453e` arm
	// reads HEAR_PLAYER with no sound-type gate at all, so the answer is still ALERT but by a
	// different route. The discriminating half is the trace, and the Tzimisce's is `0xd84`.
	N.WriteNpcStateRetail(1);
	N.Senses.Memory.BestSound.TypeMask = ElysiumGameSounds::Carcass;
	TestEqual(TEXT("...and a type outside {1, 4, 8, 0x10} falls to Troika, which has no type gate"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::HearPlayer), 3);
	TestEqual(TEXT("...and the selector tag is Troika's 2, not the Tzimisce's 0x26"),
		N.SelectIdealStateSelector, 2);
	N.Senses.Memory.BestSound.TypeMask = 0;

	// `103bd95c`: combat with no enemy falls to HUNT 0xb, not ALERT — unless SEE_ENEMY stands.
	AddExpectedError(TEXT("Combat state with no enemy"), EAutomationExpectedErrorFlags::Contains, 0);
	N.WriteNpcStateRetail(2);
	N.WriteIdealStateRetail(2);
	N.Cognition.Conditions.Reset();
	ElysiumNpcEnemy::SetEnemy(N, FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("103bd690 0xdd2 combat with no enemy -> HUNT"), N.SelectIdealStateRetail(), 0xb);
	N.WriteNpcStateRetail(2);
	N.WriteIdealStateRetail(2);
	N.Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
	TestEqual(TEXT("103bd690 0xdcd ...but SEE_ENEMY makes it ALERT"), N.SelectIdealStateRetail(), 3);

	// `103bda2e`: alert's hear arm answers HUNT, not ALERT.
	N.WriteNpcStateRetail(3);
	N.Cognition.Conditions.Reset();
	TestEqual(TEXT("103bd690 0xda2 alert hear -> HUNT"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::HearCombat), 0xb);

	// `103bddf2`: the hunt state's own ladder.
	N.WriteNpcStateRetail(0xb);
	TestEqual(TEXT("103bd690 0xdf2 hunt NEW_ENEMY -> COMBAT"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::NewEnemy), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelState19PedestrianTest,
	"Elysium.Substrate.NpcKernelState19.PedestrianSelectIdealState", GState19Flags)
bool FElysiumNpcKernelState19PedestrianTest::RunTest(const FString&)
{
	FState19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard)
		|| !TestNotNull(TEXT("the player constructs"), F.Player))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.SetRetailClassForTests(TEXT("CNPC_VPedestrian"));
	N.Senses.Memory.ClosestPlayer = F.Player->Handle;

	// `103a3450`: the flee state re-states itself with no test at all.
	N.WriteNpcStateRetail(8);
	N.WriteIdealStateRetail(1);
	TestEqual(TEXT("103a2e30 0x375 an already-fleeing pedestrian stays fleeing"),
		N.SelectIdealStateRetail(), 8);

	// `103a2e6b`: the three damage interrupts all answer FLEE, and the arm hands slot 596 the
	// ATTACKER, not the closest player.
	N.WriteNpcStateRetail(1);
	N.Senses.Memory.LastDamageAttacker = F.Player->Handle;
	TestEqual(TEXT("103a2e30 0x308 idle LIGHT_DAMAGE -> FLEE"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::LightDamage), 8);
	N.WriteNpcStateRetail(3);
	TestEqual(TEXT("103a2e30 0x308 alert HEAVY_DAMAGE -> FLEE"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::HeavyDamage), 8);
	N.WriteNpcStateRetail(1);
	TestEqual(TEXT("103a2e30 0x308 idle REPEATED_DAMAGE -> FLEE"),
		State19TestsIdealUnderMask(N, EElysiumNpcCond::RepeatedDamage), 8);

	// `103a338d`: the bullet-impact tail is the BARE condition and needs the impact's owner to be
	// the closest player AND `m_flPlayerDist` under 512 units (`_DAT_10483aac`).
	N.WriteNpcStateRetail(1);
	N.WriteIdealStateRetail(1);
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::HearBulletImpact);
	N.Senses.Memory.LastSoundBulletImpact.Source = F.Player->Handle;
	N.Senses.Memory.ClosestPlayerDistanceCm = 600.f * static_cast<float>(ElysiumMove::U);
	TestEqual(TEXT("103a2e30 a bullet impact beyond 512 units falls to CNPC_VHuman"),
		N.SelectIdealStateRetail(), 1);
	return true;
}

#endif
