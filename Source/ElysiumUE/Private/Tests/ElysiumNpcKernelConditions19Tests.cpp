// Story 0019/8 (29e under the strict verdict), family **Conditions19** -- the family's tests.
//
// Test names carry `Elysium.Substrate.NpcKernelConditions19.` and the retail address. Every
// assertion is read off the listing (`vtmb_asm`) or the decompiled C of the body it names; the
// walked prose is `docs/vtmb/npc-ai/story8/Conditions19.md`. The bodies are driven directly (the
// virtual on the NPC), with the NPCs quiet so no think competes with the pass a case drives.
//
// Owns (Conditions19's `rule` rows): 0x10270b20 CAI_BaseNPC::GatherEnemyConditions, 0x1026ec30
// CAI_BaseNPC::GatherConditions, 0x102b27f0 CAI_BaseNPCTroika::GatherConditions, 0x1035d180
// CNPC_VAndreiBlood::GatherConditions, 0x10365a70 CNPC_VBach::GatherConditions, 0x1036b590
// CNPC_VChangBros::GatherConditions, 0x10374b00 CNPC_VDog::GatherConditions, 0x10375ed0
// CNPC_VFrenzyShadow::GatherConditions, 0x10378df0 CNPC_VGargoyle::GatherConditions, 0x1037b570
// CNPC_VGhoulCroucher::GatherConditions, 0x103803d0 CNPC_VHengeyokai::GatherConditions, 0x10394e40
// CNPC_VMingXiao::GatherConditions, 0x1039ec10 CNPC_VMingXiaoTentacle::GatherConditions, 0x103a2c30
// CNPC_VPedestrian::GatherConditions, 0x103a77f0 CNPC_VSabbatLeader::GatherConditions, 0x103ac500
// CNPC_VScurrying::GatherConditions, 0x103bce40 CNPC_VTzimisce::GatherConditions, 0x103c17f0
// CNPC_VTzimisceHeadClaw::GatherConditions, 0x103c35a0 CNPC_VTzimisceRunner::GatherConditions,
// 0x103d0410 CNPC_VWerewolf::GatherConditions.

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
#include "Substrate/ElysiumNpcZombie.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcFrenzyShadow.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "ElysiumEntityWorld.h"

static constexpr EAutomationTestFlags GCond19Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	struct FCond19Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumPlayer* Player = nullptr;

		// `GuardClass` is the retail class the guard is built as; `CAI_BaseNPCTroika` stands the bare
		// Troika line. The fixture ends on the first think, so the guard is IDLE (retail 1).
		// `HintType`, when given, stands an `info_node_hint` of that type at the origin ("hint").
		explicit FCond19Fixture(const TCHAR* GuardClass = TEXT("CNPC_VHumanCombatant"),
			const TCHAR* HintType = nullptr)
			: World([GuardClass, HintType]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("conditions19_kernel"), 1919);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpcOfClass(TEXT("guard"), FVector::ZeroVector, GuardClass);
					Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
					if (HintType != nullptr)
					{
						FElysiumEntityDef& Hint = Builder.AddEntity(TEXT("info_node_hint"), TEXT("hint"),
							FVector::ZeroVector);
						Hint.Keys.Add(TEXT("hinttype"), HintType);
					}
					Builder.WireOutput(TEXT("guard"), TEXT("OnFoundEnemy"), TEXT("c19_foundenemy"));
					Builder.WireOutput(TEXT("guard"), TEXT("OnFoundPlayer"), TEXT("c19_foundplayer"));
					Builder.WireOutput(TEXT("guard"), TEXT("OnLostEnemyLOS"), TEXT("c19_lostenemylos"));
					Builder.WireOutput(TEXT("guard"), TEXT("OnLostPlayerLOS"), TEXT("c19_lostplayerlos"));
					for (const TCHAR* Name : { TEXT("c19_foundenemy"), TEXT("c19_foundplayer"),
						TEXT("c19_lostenemylos"), TEXT("c19_lostplayerlos") })
					{
						Builder.AddCounter(Name);
					}
					return Builder;
				}())
		{
			Guard = World.Npc(TEXT("guard"));
			if (Guard == nullptr)
			{
				// A class whose `Spawn` renames it (the player-controller line's
				// `SetName("playercontroller")`, `0x103a4510`) is found by its class.
				Guard = World.NpcOfClass(GuardClass);
			}
			Other = World.Npc(TEXT("other"));
			Player = World.Player();
			FElysiumNpcWorldFixture::Quiet({ Guard, Other });
			if (Guard != nullptr)
			{
				// Slot 594's range block (`102b4808`) reads `m_flSeekDistInspection`; a wide one keeps
				// "the other" (400 cm away) inside it for every case.
				Guard->Senses.Perception.bResolved = true;
				Guard->Senses.Perception.VisionDistanceCm = 100000.f;
			}
		}

		double Now() const { return World.World.NowSeconds(); }

		// Deliver the queued outputs without letting any think re-run a pass.
		void Flush()
		{
			FElysiumNpcWorldFixture::Quiet({ Guard, Other });
			World.World.Tick(World.World.NowSeconds());
		}

		float Counter(const TCHAR* Name) { return World.Counter(Name); }

		// Move the clock to `To` without letting any think run a pass (the fixture stands at 0.0).
		void AdvanceQuiet(double To)
		{
			FElysiumNpcWorldFixture::Quiet({ Guard, Other });
			World.World.Tick(To);
		}

		// `Dominate_BrainWipe` on the guard: slot 201 (`0x102b4630`, `102b469c`) answers false.
		void BlindGuard()
		{
			FElysiumActiveDisciplineEffect BrainWipe;
			BrainWipe.Record = TEXT("Dominate_BrainWipe");
			Guard->Disciplines.TargetEffects.Add(BrainWipe);
		}

		// Take the player out of every sense: `IsInert()` refuses it, so `0x101d1800`'s stand-in
		// answers "no client in my PVS" and the sense pass sees nothing.
		void HidePlayer()
		{
			if (Player != nullptr)
			{
				Player->bHidden = true;
			}
		}
	};

	bool Cond19Has(const FElysiumNpcBase& Npc, EElysiumNpcCond Cond)
	{
		return Npc.Cognition.Conditions.Has(Cond);
	}

	bool Cond19HasOrdinal(const FElysiumNpcBase& Npc, int32 Ordinal)
	{
		return Npc.Cognition.Conditions.HasOrdinal(Ordinal);
	}
}

// =================================================================================================
// 0x1026ec30 CAI_BaseNPC::GatherConditions
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19BaseGatherStateSkipTest,
	"Elysium.Substrate.NpcKernelConditions19.BaseGatherConditions.StateSkipAndFlush", GCond19Flags)
bool FCond19BaseGatherStateSkipTest::RunTest(const FString&)
{
	// 0x1026ec30: `+0x5ca4 = 1`, then `0x102cc760` on `+0x1a9c`, then `m_NPCState` 0 or 7 skips the
	// whole body (`1026ecce` / `1026ecd7`).
	for (const int32 SkipState : { 0, 7 })
	{
		FCond19Fixture F;
		if (!TestNotNull(TEXT("the guard constructs"), F.Guard))
		{
			return false;
		}
		FElysiumNpc& N = *F.Guard;
		const double Now = F.Now();
		N.WriteNpcStateRetail(SkipState);
		N.Cognition.Conditions.Reset();
		N.Cognition.Conditions.Set(EElysiumNpcCond::SeeHate);
		N.Cognition.GatheredAt = -1.0;
		N.Cognition.DelayedConditions.Reset();
		N.Cognition.DelayedConditions.Add(TPair<int32, double>(0x47, Now - 1.0));   // due
		N.Cognition.DelayedConditions.Add(TPair<int32, double>(0x2f, Now + 10.0));  // waiting
		N.Cognition.DelayedConditions.Add(TPair<int32, double>(0x0b, Now));         // due (equal)
		N.CapabilityWord |= FElysiumNpcBase::Cond19CapWeaponSearch;
		N.NextWeaponSearchTime = -1.0;

		N.GatherConditions();

		TestEqual(TEXT("1026eca9 m_bConditionsGathered is written before the state test"),
			N.Cognition.GatheredAt, Now);
		TestTrue(TEXT("102cc760 promotes an entry stamped before curtime"),
			Cond19Has(N, EElysiumNpcCond::LostEnemy));
		TestTrue(TEXT("102cc760 promotes an entry stamped AT curtime (the swapped-in last entry)"),
			Cond19Has(N, EElysiumNpcCond::DetectedAttack));
		TestFalse(TEXT("102cc760 leaves a later entry waiting"),
			Cond19Has(N, EElysiumNpcCond::WaitingAttackTime));
		TestEqual(TEXT("102cc730 removes exactly the promoted entries"),
			N.Cognition.DelayedConditions.Num(), 1);
		TestTrue(TEXT("1026ecce/1026ecd7: no ClearSenseConditions on the skip"),
			Cond19Has(N, EElysiumNpcCond::SeeHate));
		TestEqual(TEXT("...and no better-weapon search either"), N.NextWeaponSearchTime, -1.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19BaseGatherSenseGateTest,
	"Elysium.Substrate.NpcKernelConditions19.BaseGatherConditions.SenseGate", GCond19Flags)
bool FCond19BaseGatherSenseGateTest::RunTest(const FString&)
{
	// `1026ece8` spawnflag 0x400 || `1026ecfb` 0x101d1800 || `1026ed03` m_bfNPCStateFlags bit 0;
	// none of them -> slot 477 ClearSenseConditions (`1026ed09`) and the block is skipped.
	{
		FCond19Fixture F;
		if (!TestNotNull(TEXT("the guard constructs"), F.Guard))
		{
			return false;
		}
		FElysiumNpc& N = *F.Guard;
		F.HidePlayer();
		N.SpawnFlags &= ~static_cast<int32>(FElysiumNpcBase::Cond19SpawnflagSenseAlways);
		N.WriteNpcStateRetail(4);   // SCRIPT: `0x1026e3e0` gives 0x08, bit 0 clear
		N.Cognition.Conditions.Reset();
		N.Cognition.Conditions.Set(EElysiumNpcCond::SeeHate);
		N.Cognition.Conditions.Set(EElysiumNpcCond::HearCombat);
		N.Cognition.Conditions.Set(EElysiumNpcCond::HearBulletImpact);
		N.CapabilityWord |= FElysiumNpcBase::Cond19CapWeaponSearch;
		N.NextWeaponSearchTime = -1.0;

		N.GatherConditions();

		TestFalse(TEXT("1026ed09 slot 477 clears SEE_HATE"), Cond19Has(N, EElysiumNpcCond::SeeHate));
		TestFalse(TEXT("...and HEAR_COMBAT"), Cond19Has(N, EElysiumNpcCond::HearCombat));
		TestTrue(TEXT("...but HEAR_BULLET_IMPACT is not in 0x105c97dc"),
			Cond19Has(N, EElysiumNpcCond::HearBulletImpact));
		TestEqual(TEXT("the block (and 0x1026fb40) did not run"), N.NextWeaponSearchTime, -1.0);
	}
	{
		FCond19Fixture F;
		if (!TestNotNull(TEXT("the guard constructs"), F.Guard))
		{
			return false;
		}
		FElysiumNpc& N = *F.Guard;
		F.HidePlayer();
		N.SpawnFlags |= static_cast<int32>(FElysiumNpcBase::Cond19SpawnflagSenseAlways);
		N.WriteNpcStateRetail(4);
		N.Cognition.Conditions.Reset();
		N.Cognition.Conditions.Set(EElysiumNpcCond::HearBulletImpact);
		N.CapabilityWord |= FElysiumNpcBase::Cond19CapWeaponSearch;
		N.NextWeaponSearchTime = -1.0;
		const double Now = F.Now();

		N.GatherConditions();

		TestEqual(TEXT("1026ece8 spawnflag 0x400 runs the block: 1026fb76 re-arms +0x5da0"),
			N.NextWeaponSearchTime, Now + FElysiumNpcBase::Cond19WeaponSearchInterval);
		TestEqual(TEXT("...and 1026fba4 Weapon_FindUsable was asked once"),
			N.Conditions19WeaponFindUsableCalls, 1);
		TestFalse(TEXT("1026ee3b: no usable weapon, no BETTER_WEAPON_AVAILABLE"),
			Cond19Has(N, FElysiumNpcBase::Cond19BetterWeaponAvailable));
		TestFalse(TEXT("1026e56a PerformSensing's OnListened clears the 0x105c97b4 table"),
			Cond19Has(N, EElysiumNpcCond::HearBulletImpact));
	}
	{
		// `1026ecfb`: a live client (the fixture's player) in an IDLE body runs the block too.
		FCond19Fixture F;
		if (!TestNotNull(TEXT("the guard constructs"), F.Guard))
		{
			return false;
		}
		FElysiumNpc& N = *F.Guard;
		N.SpawnFlags &= ~static_cast<int32>(FElysiumNpcBase::Cond19SpawnflagSenseAlways);
		N.WriteNpcStateRetail(4);
		N.CapabilityWord |= FElysiumNpcBase::Cond19CapWeaponSearch;
		N.NextWeaponSearchTime = -1.0;
		N.GatherConditions();
		TestEqual(TEXT("1026ecfb the PVS client runs the block"), N.Conditions19WeaponFindUsableCalls, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19BetterWeaponTest,
	"Elysium.Substrate.NpcKernelConditions19.BaseGatherConditions.BetterWeapon1026fb40", GCond19Flags)
bool FCond19BetterWeaponTest::RunTest(const FString&)
{
	FCond19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	const double Now = F.Now();

	N.CapabilityWord &= ~FElysiumNpcBase::Cond19CapWeaponSearch;
	N.NextWeaponSearchTime = -1.0;
	TestFalse(TEXT("1026fb53 without capability 0x200000"), N.Conditions19BetterWeaponAvailable(Now));
	TestEqual(TEXT("...and +0x5da0 untouched"), N.NextWeaponSearchTime, -1.0);

	N.CapabilityWord |= FElysiumNpcBase::Cond19CapWeaponSearch;
	N.NextWeaponSearchTime = Now;
	TestFalse(TEXT("1026fb69 a stamp AT curtime is not before it"), N.Conditions19BetterWeaponAvailable(Now));
	TestEqual(TEXT("...and is not re-armed"), N.NextWeaponSearchTime, Now);

	N.NextWeaponSearchTime = Now - 0.5;
	const int32 CallsBefore = N.Conditions19WeaponFindUsableCalls;
	TestFalse(TEXT("1026fbab the seam finds nothing usable"), N.Conditions19BetterWeaponAvailable(Now));
	TestEqual(TEXT("1026fb76 re-arm is curtime + 2.0"), N.NextWeaponSearchTime,
		Now + FElysiumNpcBase::Cond19WeaponSearchInterval);
	TestEqual(TEXT("1026fba4 asked Weapon_FindUsable"), N.Conditions19WeaponFindUsableCalls,
		CallsBefore + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19BaseGatherIdleSoundTest,
	"Elysium.Substrate.NpcKernelConditions19.BaseGatherConditions.IdleSoundCascade", GCond19Flags)
bool FCond19BaseGatherIdleSoundTest::RunTest(const FString&)
{
	// `1026ed27` slot 509 rolls; the cascade is `1026ed51` state-flags bit 1 -> slot 499,
	// `1026ed6f` flags2 0x10000 -> slot 504, `1026ed9a` local schedule 0x12f -> slot 503, else slot
	// 490. The roll is `RandomInt(0, 999) == 0`, so each case gathers until one speak lands.
	auto FirstIdleSpeak = [](FElysiumNpc& N) -> FString
	{
		static const TCHAR* const IdleConcepts[] = {
			TEXT("Idle_Calm"), TEXT("Idle_Agitated"), TEXT("Upset"), TEXT("Comfort") };
		for (int32 Attempt = 0; Attempt < 20000; ++Attempt)
		{
			N.VSoundSpeakCalls.Reset();
			N.GatherConditions();
			for (const FElysiumNpc::FVSoundSpeak& Speak : N.VSoundSpeakCalls)
			{
				for (const TCHAR* Concept : IdleConcepts)
				{
					if (Speak.Concept != nullptr && FCString::Strcmp(Speak.Concept, Concept) == 0)
					{
						return FString(Concept);
					}
				}
			}
		}
		return FString();
	};
	{
		FCond19Fixture F;
		if (!TestNotNull(TEXT("the guard constructs"), F.Guard))
		{
			return false;
		}
		F.HidePlayer();
		F.Guard->NpcFlags.Clear(EElysiumNpcFlag2::D_CALM);
		TestEqual(TEXT("1026edac slot 490 IdleSound in an idle body"), FirstIdleSpeak(*F.Guard),
			FString(TEXT("Idle_Calm")));
	}
	{
		FCond19Fixture F;
		if (!TestNotNull(TEXT("the guard constructs"), F.Guard))
		{
			return false;
		}
		F.HidePlayer();
		F.Guard->NpcFlags.Set(EElysiumNpcFlag2::D_CALM);
		TestEqual(TEXT("1026ed75 flags2 0x10000 takes slot 504 UpsetSound"), FirstIdleSpeak(*F.Guard),
			FString(TEXT("Upset")));
	}
	{
		// The zombie's slot 509 (`0x103e0fa0`) has no state test, so a COMBAT zombie (state flags
		// 0x8f, bit 1 set) reaches the cascade's first arm.
		FCond19Fixture F(TEXT("CNPC_VZombie"));
		if (!TestNotNull(TEXT("the zombie constructs"), F.Guard))
		{
			return false;
		}
		F.HidePlayer();
		F.Guard->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
		F.Guard->NpcFlags.Set(EElysiumNpcFlag2::D_CALM);
		TestEqual(TEXT("1026ed57 state-flags bit 1 wins over D_CALM: slot 499 IdleAgitatedSound"),
			FirstIdleSpeak(*F.Guard), FString(TEXT("Idle_Agitated")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19BaseGatherTailTest,
	"Elysium.Substrate.NpcKernelConditions19.BaseGatherConditions.TargetAndTroikaTail", GCond19Flags)
bool FCond19BaseGatherTailTest::RunTest(const FString&)
{
	// `1026ee6a..1026eebc` a live m_hTargetEnt reaches CheckTarget; `1026ef48..1026ef9d` the handle at
	// +0x6240 reaches 0x1028e980; `1026eed8..1026ef43` a live best-see-unknown reaches 0x1028e480.
	FCond19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	F.HidePlayer();
	N.SetTarget(FElysiumEntityHandle::Invalid());
	N.ScheduleHost.MoveTarget = FElysiumEntityHandle::Invalid();
	N.Senses.Memory.BestSeeUnknown = FElysiumEntityHandle::Invalid();
	// `CheckTarget` `0x10271d10` ends in `UpdateTargetPos` `0x10271b10` unconditionally
	// (`0x10271db7`), so its counter witnesses the call.
	const int32 UpdateTargetPosBefore = N.UpdateTargetPosCalls;
	N.GatherConditions();
	TestEqual(TEXT("1026ee6a no target, no CheckTarget"), N.UpdateTargetPosCalls, UpdateTargetPosBefore);
	TestEqual(TEXT("1026ef51 no move target, no 0x1028e980"), N.Conditions19RefreshGoalCalls, 0);
	TestEqual(TEXT("1026eee3 no see-unknown, no 0x1028e480"), N.Conditions19ApproachGoalCalls, 0);

	N.SetTarget(F.Other->Handle);
	N.ScheduleHost.MoveTarget = F.Other->Handle;
	N.GatherConditions();
	TestEqual(TEXT("1026eebc CheckTarget(target)"), N.UpdateTargetPosCalls, UpdateTargetPosBefore + 1);
	TestEqual(TEXT("1026ef9d 0x1028e980(move target)"), N.Conditions19RefreshGoalCalls, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19OcclusionReportTest,
	"Elysium.Substrate.NpcKernelConditions19.BaseGatherConditions.OcclusionReport1028e790", GCond19Flags)
bool FCond19OcclusionReportTest::RunTest(const FString&)
{
	FCond19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	const double Now = F.Now();
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	N.OccludedDelay = 2.f;
	N.OccludedReportTimeE = 0.0;
	N.Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);

	N.Conditions19OcclusionReportUpkeep();
	TestEqual(TEXT("1028e72a a zero stamp arms to curtime + m_flOccludedDelay"), N.OccludedReportTimeE,
		Now + 2.0);
	TestFalse(TEXT("1028e74a ENEMY_OCCLUDED is withheld until the delay has run"),
		Cond19Has(N, EElysiumNpcCond::EnemyOccluded));

	N.Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	N.OccludedReportTimeE = Now - 0.25;
	N.Conditions19OcclusionReportUpkeep();
	TestTrue(TEXT("1028e745 past the stamp the condition stands"),
		Cond19Has(N, EElysiumNpcCond::EnemyOccluded));
	TestEqual(TEXT("...and the stamp is kept"), N.OccludedReportTimeE, Now - 0.25);

	N.Cognition.Conditions.Clear(EElysiumNpcCond::EnemyOccluded);
	N.BaseMemory.bEnemyWentOccluded = true;
	N.Conditions19OcclusionReportUpkeep();
	TestEqual(TEXT("1028e754 without the condition the stamp is zeroed"), N.OccludedReportTimeE, 0.0);
	TestFalse(TEXT("1028e7cb 0x10270180(enemy, false) clears the went-occluded edge"),
		N.BaseMemory.bEnemyWentOccluded);
	TestEqual(TEXT("...and snapshots the enemy's origin"), N.BaseMemory.EnemyWentOccludedPosition,
		F.Other->Origin);

	N.SetTarget(F.Other->Handle);
	N.OccludedReportTimeT = 0.0;
	N.Cognition.Conditions.Set(FElysiumNpcBase::Cond19TargetOccluded);
	N.Conditions19OcclusionReportUpkeep();
	TestEqual(TEXT("1028e807 the target lane arms +0x62d0"), N.OccludedReportTimeT, Now + 2.0);
	TestFalse(TEXT("...and withholds TARGET_OCCLUDED"), Cond19Has(N, FElysiumNpcBase::Cond19TargetOccluded));
	return true;
}

// =================================================================================================
// 0x10270b20 CAI_BaseNPC::GatherEnemyConditions
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19EnemyLosFoundTest,
	"Elysium.Substrate.NpcKernelConditions19.GatherEnemyConditions.LosAndFoundOutputs", GCond19Flags)
bool FCond19EnemyLosFoundTest::RunTest(const FString&)
{
	// 0x10270b20: the four clears (`10270b4a..10270b65`), slot 201 zeroes `+0x5b98`
	// (`10270bb1`), below ten `HAVE_ENEMY_LOS` (`10270cc9`), and the found outputs on the
	// `m_afMemory & 0x20000` edge only (`10270d1e` / `10270e4c`).
	FCond19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.BaseMemory.EnemyOccludedCheck = 10;
	N.BaseScheduleHost.MemoryBits &= ~FElysiumNpcBase::Cond19MemoryEnemyInSight;
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(FElysiumNpcBase::Cond19EnemyFacingMe);
	N.Cognition.Conditions.Set(FElysiumNpcBase::Cond19BehindEnemy);
	N.Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	N.BaseMemory.EnemyOccluder = F.Other->Handle;

	N.GatherEnemyConditions(F.Other);
	F.Flush();

	TestEqual(TEXT("10270bb1 a visible enemy zeroes m_eEnemyOccludedCheck"), N.BaseMemory.EnemyOccludedCheck, 0);
	TestTrue(TEXT("10270cc9 HAVE_ENEMY_LOS"), Cond19Has(N, EElysiumNpcCond::HaveEnemyLos));
	TestFalse(TEXT("10270b65 ENEMY_OCCLUDED cleared"), Cond19Has(N, EElysiumNpcCond::EnemyOccluded));
	TestFalse(TEXT("10270b4a ENEMY_FACING_ME cleared (no SEE_ENEMY, so not re-raised)"),
		Cond19Has(N, FElysiumNpcBase::Cond19EnemyFacingMe));
	TestFalse(TEXT("10270b53 BEHIND_ENEMY cleared"), Cond19Has(N, FElysiumNpcBase::Cond19BehindEnemy));
	TestFalse(TEXT("10270b71 m_hEnemyOccluder reset"), N.BaseMemory.EnemyOccluder.IsSet());
	TestTrue(TEXT("10270e4c m_afMemory |= 0x20000"),
		(N.BaseScheduleHost.MemoryBits & FElysiumNpcBase::Cond19MemoryEnemyInSight) != 0);
	TestEqual(TEXT("10270df7 OnFoundEnemy on the edge"), F.Counter(TEXT("c19_foundenemy")), 1.f);
	TestEqual(TEXT("10270d75 no OnFoundPlayer for an NPC enemy"), F.Counter(TEXT("c19_foundplayer")), 0.f);

	N.GatherEnemyConditions(F.Other);
	F.Flush();
	TestEqual(TEXT("10270d1e the bit stands: no second found edge"), F.Counter(TEXT("c19_foundenemy")), 1.f);
	TestEqual(TEXT("10271202 UpdateEnemyPos runs on every live pass"), N.Conditions19UpdateEnemyPosCalls, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19EnemyOccludedTest,
	"Elysium.Substrate.NpcKernelConditions19.GatherEnemyConditions.OccludedAndLostOutputs", GCond19Flags)
bool FCond19EnemyOccludedTest::RunTest(const FString&)
{
	// A blind slot 201: `+0x5b98` counts up to ten (`10270ba8`), below ten keeps HAVE_ENEMY_LOS; AT
	// ten ENEMY_OCCLUDED (`10270bfc`), the lost outputs on the bit (`10270c46`), the bit cleared on
	// every at-limit pass (`10270c5d`).
	FCond19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	F.BlindGuard();
	N.BaseMemory.EnemyOccludedCheck = 8;
	N.BaseScheduleHost.MemoryBits |= FElysiumNpcBase::Cond19MemoryEnemyInSight;

	N.GatherEnemyConditions(F.Other);
	TestEqual(TEXT("10270ba8 the miss counts"), N.BaseMemory.EnemyOccludedCheck, 9);
	TestTrue(TEXT("below ten HAVE_ENEMY_LOS still stands"), Cond19Has(N, EElysiumNpcCond::HaveEnemyLos));
	TestFalse(TEXT("...and ENEMY_OCCLUDED does not"), Cond19Has(N, EElysiumNpcCond::EnemyOccluded));

	N.GatherEnemyConditions(F.Other);
	F.Flush();
	TestEqual(TEXT("10270ba6 the count saturates at ten"), N.BaseMemory.EnemyOccludedCheck, 10);
	TestTrue(TEXT("10270bfc ENEMY_OCCLUDED"), Cond19Has(N, EElysiumNpcCond::EnemyOccluded));
	TestFalse(TEXT("10270b5c HAVE_ENEMY_LOS cleared"), Cond19Has(N, EElysiumNpcCond::HaveEnemyLos));
	TestEqual(TEXT("10270c4c OnLostEnemyLOS on the bit"), F.Counter(TEXT("c19_lostenemylos")), 1.f);
	TestEqual(TEXT("10270c36 no OnLostPlayerLOS for an NPC"), F.Counter(TEXT("c19_lostplayerlos")), 0.f);
	TestTrue(TEXT("10270c5d the bit is cleared"),
		(N.BaseScheduleHost.MemoryBits & FElysiumNpcBase::Cond19MemoryEnemyInSight) == 0);

	N.GatherEnemyConditions(F.Other);
	F.Flush();
	TestEqual(TEXT("10270ba6 an eleventh miss stays at ten"), N.BaseMemory.EnemyOccludedCheck, 10);
	TestEqual(TEXT("10270c1f no second lost edge"), F.Counter(TEXT("c19_lostenemylos")), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19EnemyDeadTest,
	"Elysium.Substrate.NpcKernelConditions19.GatherEnemyConditions.DeadEnemyReturn", GCond19Flags)
bool FCond19EnemyDeadTest::RunTest(const FString&)
{
	// `10270e62`: slot 158 false -> ENEMY_DEAD, clear SEE_ENEMY and ENEMY_OCCLUDED, RETURN before
	// the distance, too-far, attack, UpdateEnemyPos and eluded blocks.
	FCond19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	// (Story 8 wave 2: slot 158 reads the life-state word itself, `AnimEventLifeStateWord`; `Event_Killed` `0x1032b9b0` writes 1 LIFE_DYING.)
	F.Other->AnimEventLifeStateWord = 1;
	N.Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
	N.Cognition.Conditions.Set(EElysiumNpcCond::EnemyTooFar);
	N.GatherEnemyConditions(F.Other);
	TestTrue(TEXT("10270e77 ENEMY_DEAD"), Cond19Has(N, EElysiumNpcCond::EnemyDead));
	TestFalse(TEXT("10270e80 SEE_ENEMY cleared"), Cond19Has(N, EElysiumNpcCond::SeeEnemy));
	TestFalse(TEXT("10270e89 ENEMY_OCCLUDED cleared"), Cond19Has(N, EElysiumNpcCond::EnemyOccluded));
	TestTrue(TEXT("10270eec returns before the too-far test"), Cond19Has(N, EElysiumNpcCond::EnemyTooFar));
	TestEqual(TEXT("...and before UpdateEnemyPos"), N.Conditions19UpdateEnemyPosCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19EnemySeeBlockTest,
	"Elysium.Substrate.NpcKernelConditions19.GatherEnemyConditions.SeeEnemyBlockAndTooFar", GCond19Flags)
bool FCond19EnemySeeBlockTest::RunTest(const FString&)
{
	// Under SEE_ENEMY (0x46 is not cleared by this body, so OnLooked's bit is what it reads):
	// slot 544 with the origin for a still enemy (`10271047`) or `origin - r * velocity` with r in
	// [-0.05, 0] (`10270fac..1027103f`); the enemy's own slot 363 picks ENEMY_FACING_ME or
	// BEHIND_ENEMY (`1027106c..102710b4`). Then `m_flDistTooFar` against 0x10270890 (`102711ad`).
	FCond19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	F.Other->Velocity = FVector::ZeroVector;
	F.Other->Angles = FVector::ZeroVector;   // facing +X, away from the guard at the origin
	N.Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
	N.DistTooFar = 10000.f;
	N.GatherEnemyConditions(F.Other);
	const FElysiumNpcEnemyMemoryRecord* Record = N.EnemyMemory.Find(F.Other->Handle);
	if (TestNotNull(TEXT("10271047 slot 544 wrote a record"), Record))
	{
		TestEqual(TEXT("...at the still enemy's origin"), Record->LastPosition, F.Other->Origin);
	}
	TestTrue(TEXT("102710b4 an enemy facing away: BEHIND_ENEMY"), Cond19Has(N, FElysiumNpcBase::Cond19BehindEnemy));
	TestFalse(TEXT("102710a0 ...and not ENEMY_FACING_ME"), Cond19Has(N, FElysiumNpcBase::Cond19EnemyFacingMe));
	TestFalse(TEXT("102711c9 inside m_flDistTooFar clears ENEMY_TOO_FAR"), Cond19Has(N, EElysiumNpcCond::EnemyTooFar));

	F.Other->Angles = FVector(0.0, 180.0, 0.0);   // turned to face the guard
	F.Other->Velocity = FVector(1000.0, 0.0, 0.0);
	N.Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
	N.DistTooFar = 10.f;
	N.GatherEnemyConditions(F.Other);
	Record = N.EnemyMemory.Find(F.Other->Handle);
	if (TestNotNull(TEXT("1027103f slot 544 again"), Record))
	{
		TestTrue(TEXT("10270ff6 the lead is origin - r * velocity, r in [-0.05, 0]"),
			Record->LastPosition.X >= F.Other->Origin.X - 0.001
				&& Record->LastPosition.X <= F.Other->Origin.X + 50.001);
	}
	TestTrue(TEXT("1027108c the enemy's view cone holds the guard: ENEMY_FACING_ME"),
		Cond19Has(N, FElysiumNpcBase::Cond19EnemyFacingMe));
	TestFalse(TEXT("10271095 ...and BEHIND_ENEMY cleared"), Cond19Has(N, FElysiumNpcBase::Cond19BehindEnemy));
	TestTrue(TEXT("102711be at or beyond m_flDistTooFar raises ENEMY_TOO_FAR"), Cond19Has(N, EElysiumNpcCond::EnemyTooFar));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19EnemyEludedTest,
	"Elysium.Substrate.NpcKernelConditions19.GatherEnemyConditions.EludedTail", GCond19Flags)
bool FCond19EnemyEludedTest::RunTest(const FString&)
{
	// `1027123f..10271272`: `curtime - LastTimeSeen > 8.0`; not already eluded; no SEE_ENEMY; the
	// Troika arm (`+0x98` is the NPC itself) marks eluded only on `DONE_EXTRAPOLATING`, clearing it.
	FCond19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	const double Now = F.Now();
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	N.UpdateEnemyMemory(F.Other, F.Other->Origin, nullptr);
	FElysiumNpcEnemyMemoryRecord* Record = N.EnemyMemory.FindMutable(F.Other->Handle);
	if (!TestNotNull(TEXT("the record stands"), Record))
	{
		return false;
	}

	Record->LastSeenTime = Now - 8.0;
	N.NpcFlags.Set(EElysiumNpcFlag::DONE_EXTRAPOLATING);
	N.Cognition.Conditions.Clear(EElysiumNpcCond::SeeEnemy);
	N.GatherEnemyConditions(F.Other);
	TestFalse(TEXT("10271272 exactly 8 s is not past the window"), N.EnemyMemory.IsEluded(F.Other->Handle));
	TestTrue(TEXT("...and the flag is untouched"), N.NpcFlags.Has(EElysiumNpcFlag::DONE_EXTRAPOLATING));

	Record = N.EnemyMemory.FindMutable(F.Other->Handle);
	if (!TestNotNull(TEXT("the record still stands"), Record))
	{
		return false;
	}
	Record->LastSeenTime = Now - 9.0;
	N.Cognition.Conditions.Clear(EElysiumNpcCond::SeeEnemy);
	N.GatherEnemyConditions(F.Other);
	TestTrue(TEXT("10271399 DONE_EXTRAPOLATING marks the enemy eluded"), N.EnemyMemory.IsEluded(F.Other->Handle));
	TestFalse(TEXT("10271319 ...and is cleared"), N.NpcFlags.Has(EElysiumNpcFlag::DONE_EXTRAPOLATING));
	return true;
}

// =================================================================================================
// 0x102b27f0 CAI_BaseNPCTroika::GatherConditions
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19TroikaClearsTest,
	"Elysium.Substrate.NpcKernelConditions19.TroikaGatherConditions.TopClearsAndEnemyDead", GCond19Flags)
bool FCond19TroikaClearsTest::RunTest(const FString&)
{
	// `102b2863..102b28bd` eleven clears before the base; `102b28dd` a dead slot-168 enemy raises
	// ENEMY_DEAD and clears SEE_ENEMY / ENEMY_OCCLUDED.
	FCond19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	F.HidePlayer();
	const EElysiumNpcCond TopClears[] = { EElysiumNpcCond::InvestigateSight, EElysiumNpcCond::Comfort,
		EElysiumNpcCond::LostUnknown, EElysiumNpcCond::IgnoreUnknown, EElysiumNpcCond::UnknownRunTimer,
		EElysiumNpcCond::UnknownAdvancing, EElysiumNpcCond::UnknownHolding,
		EElysiumNpcCond::UnknownRetreating, EElysiumNpcCond::OnFire, EElysiumNpcCond::DetectedAttack };
	for (const EElysiumNpcCond Cond : TopClears)
	{
		N.Cognition.Conditions.Set(Cond);
	}
	N.Cognition.Conditions.Set(EElysiumNpcCond::SquadSeeEnemy);
	N.GatherConditions();
	for (const EElysiumNpcCond Cond : TopClears)
	{
		TestFalse(FString::Printf(TEXT("102b2863..102b28bd clear %s"), ElysiumNpcCondName(Cond)),
			Cond19Has(N, Cond));
	}
	TestTrue(TEXT("0x102b2730 clears neither squad condition"), Cond19Has(N, EElysiumNpcCond::SquadSeeEnemy));

	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	// (Story 8 wave 2: slot 158 reads the life-state word itself, `AnimEventLifeStateWord`; `Event_Killed` `0x1032b9b0` writes 1 LIFE_DYING.)
	F.Other->AnimEventLifeStateWord = 1;
	N.GatherConditions();
	TestTrue(TEXT("102b28f6 a dead slot-168 enemy: ENEMY_DEAD"), Cond19Has(N, EElysiumNpcCond::EnemyDead));
	TestFalse(TEXT("102b28ff SEE_ENEMY cleared"), Cond19Has(N, EElysiumNpcCond::SeeEnemy));
	TestFalse(TEXT("102b2908 ENEMY_OCCLUDED cleared"), Cond19Has(N, EElysiumNpcCond::EnemyOccluded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19TroikaInterruptDistTest,
	"Elysium.Substrate.NpcKernelConditions19.TroikaGatherConditions.InterruptDistancesAndTime", GCond19Flags)
bool FCond19TroikaInterruptDistTest::RunTest(const FString&)
{
	// `102b291f..102b294c` the six clears; the `+0x6324` / `+0x6328` arms against the enemy's
	// squared SOURCE distance (400 cm = 157.5 units, 24800 squared); `+0x632c` at or before curtime
	// raises INTERRUPT_TIME (`102b2bd2`).
	FCond19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	F.HidePlayer();
	// The fixture stands at curtime 0.0, and `102b2b97..102b2baa` admits only `m_flInterruptTime >
	// 0.0`: a stamp "at curtime" needs a positive clock (the lane's first cut stamped 0.0).
	F.AdvanceQuiet(1.0);
	const double Now = F.Now();
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	N.Cognition.Conditions.Set(FElysiumNpcBase::Cond19InsideInterruptDist);
	N.Cognition.Conditions.Set(FElysiumNpcBase::Cond19OutsideInterruptDistF);
	N.ScheduleHost.InsideInterruptDistanceSqr = 30000.f;
	N.ScheduleHost.OutsideInterruptDistanceSqr = 20000.f;
	N.ScheduleHost.InterruptTime = Now;
	N.GatherConditions();
	TestFalse(TEXT("102b291f the stale INSIDE_INTERRUPT_DIST is cleared (the navigator seam is idle)"),
		Cond19Has(N, FElysiumNpcBase::Cond19InsideInterruptDist));
	TestFalse(TEXT("102b294c OUTSIDE_INTERRUPT_DIST_F cleared (no follower boss)"),
		Cond19Has(N, FElysiumNpcBase::Cond19OutsideInterruptDistF));
	TestTrue(TEXT("102b2a00 the enemy inside 30000: INSIDE_INTERRUPT_DIST_E"),
		Cond19Has(N, FElysiumNpcBase::Cond19InsideInterruptDistE));
	TestTrue(TEXT("102b2b24 the enemy outside 20000: OUTSIDE_INTERRUPT_DIST_E"),
		Cond19Has(N, FElysiumNpcBase::Cond19OutsideInterruptDistE));
	TestTrue(TEXT("102b2bd2 m_flInterruptTime reached: INTERRUPT_TIME"), Cond19Has(N, EElysiumNpcCond::InterruptTime));

	N.Cognition.Conditions.Clear(EElysiumNpcCond::InterruptTime);
	N.ScheduleHost.InsideInterruptDistanceSqr = 0.f;
	N.ScheduleHost.OutsideInterruptDistanceSqr = 0.f;
	N.ScheduleHost.InterruptTime = Now + 10.0;
	N.GatherConditions();
	TestFalse(TEXT("102b2964 a zero inside threshold skips the arm"), Cond19Has(N, FElysiumNpcBase::Cond19InsideInterruptDistE));
	TestFalse(TEXT("102b2a84 a zero outside threshold skips the arm"), Cond19Has(N, FElysiumNpcBase::Cond19OutsideInterruptDistE));
	TestFalse(TEXT("102b2bc1 a future m_flInterruptTime does not raise"), Cond19Has(N, EElysiumNpcCond::InterruptTime));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19TroikaStopBackupTest,
	"Elysium.Substrate.NpcKernelConditions19.TroikaGatherConditions.StopBackup", GCond19Flags)
bool FCond19TroikaStopBackupTest::RunTest(const FString&)
{
	// `102b2bf0` the running program's mask lists 0x2c -> clear, then raise when the XY dot of the
	// enemy direction with the forward is AT OR BELOW 0.707 (`102b2c8e`, pass R's correction);
	// without the mask the bit is cleared (`102b2ca6`).
	FCond19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	F.HidePlayer();
	N.Angles = FVector::ZeroVector;   // forward +X
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	N.Cognition.Conditions.Set(EElysiumNpcCond::StopBackup);
	N.GatherConditions();
	TestFalse(TEXT("102b2ca6 no program mask: STOP_BACKUP cleared"), Cond19Has(N, EElysiumNpcCond::StopBackup));

	FElysiumNpcConditions Mask;
	Mask.Set(EElysiumNpcCond::StopBackup);
	const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Mask);
	TestTrue(TEXT("the carrier program installs"), ElysiumSchedule::Start(N.Schedule, ElysiumSched::IDLE_STAND, N));
	N.GatherConditions();
	TestFalse(TEXT("102b2c8e an enemy dead ahead (dot 1.0) skips the set"), Cond19Has(N, EElysiumNpcCond::StopBackup));
	F.Other->Origin = FVector(-400.0, 0.0, 0.0);
	N.GatherConditions();
	TestTrue(TEXT("102b2c9f an enemy behind (dot -1.0) raises STOP_BACKUP"), Cond19Has(N, EElysiumNpcCond::StopBackup));
	N.Schedule.Clear();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19TroikaTailTest,
	"Elysium.Substrate.NpcKernelConditions19.TroikaGatherConditions.CorpseDoorWallDamage", GCond19Flags)
bool FCond19TroikaTailTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	F.HidePlayer();
	const double Now = F.Now();

	// `0x1028fa50`: due -> re-armed in [2.0, 2.5], both corpse conditions cleared (the corpse query
	// seam finds none).
	N.CorpseConditionTime = 0.0;
	N.Cognition.Conditions.Set(FElysiumNpcBase::Cond19SeeCorpse);
	N.Cognition.Conditions.Set(EElysiumNpcCond::SeeCorpseFriend);
	// `102b2dfa`: due -> re-armed +3.0, the seam's clear ray clears WEAPON_THROUGH_WALL.
	N.WeaponThroughWallTime = 0.0;
	N.Cognition.Conditions.Set(EElysiumNpcCond::WeaponThroughWall);
	// `102b2d1e..102b2d67`: a live blocked door with neither SEE_ENEMY nor NEW_ENEMY stays.
	N.BlockedDoor = F.Other->Handle;
	// `102b2fdf..102b302f`: LIGHT_DAMAGE standing latches m_bCondTookDamage.
	N.Cognition.bCondTookDamage = false;
	N.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	N.GatherConditions();
	TestTrue(TEXT("1028fa93 the corpse timer re-arms 2.0..2.5 s out"),
		N.CorpseConditionTime >= Now + 2.0 && N.CorpseConditionTime <= Now + 2.5);
	TestFalse(TEXT("1028fa99 SEE_CORPSE cleared"), Cond19Has(N, FElysiumNpcBase::Cond19SeeCorpse));
	TestFalse(TEXT("1028faa2 SEE_CORPSE_FRIEND cleared"), Cond19Has(N, EElysiumNpcCond::SeeCorpseFriend));
	TestEqual(TEXT("102b2e28 the wall trace re-arms +3.0"), N.WeaponThroughWallTime, Now + 3.0);
	TestFalse(TEXT("102b2fd6 a clear ray clears WEAPON_THROUGH_WALL"), Cond19Has(N, EElysiumNpcCond::WeaponThroughWall));
	TestTrue(TEXT("102b2d51/102b2d5e the door survives without SEE_ENEMY or NEW_ENEMY"), N.BlockedDoor == F.Other->Handle);
	TestTrue(TEXT("102b302f a standing damage condition latches m_bCondTookDamage"), N.Cognition.bCondTookDamage);

	// Not due: the corpse and wall stamps hold their words; the latch re-raises LIGHT_DAMAGE.
	N.Cognition.Conditions.Set(FElysiumNpcBase::Cond19SeeCorpse);
	N.Cognition.Conditions.Set(EElysiumNpcCond::WeaponThroughWall);
	N.Cognition.Conditions.Clear(EElysiumNpcCond::LightDamage);
	N.Cognition.Conditions.Clear(EElysiumNpcCond::HeavyDamage);
	N.Cognition.Conditions.Clear(EElysiumNpcCond::RepeatedDamage);
	N.GatherConditions();
	TestTrue(TEXT("1028fa6c not due: SEE_CORPSE untouched"), Cond19Has(N, FElysiumNpcBase::Cond19SeeCorpse));
	TestTrue(TEXT("102b2e10 not due: WEAPON_THROUGH_WALL untouched"), Cond19Has(N, EElysiumNpcCond::WeaponThroughWall));
	TestTrue(TEXT("102b301b the latch raises LIGHT_DAMAGE"), Cond19Has(N, EElysiumNpcCond::LightDamage));
	TestTrue(TEXT("...and stays latched"), N.Cognition.bCondTookDamage);

	// `102b2d67`: NEW_ENEMY standing resets the door. `SetEnemy` (`0x10279a50`) does not raise
	// NEW_ENEMY (`ChooseEnemy`'s change work does), so the case raises it; the sticky committed
	// enemy leaves `ChooseEnemy` idle and nothing in the pass clears it.
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	N.Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
	N.BlockedDoor = F.Other->Handle;
	N.GatherConditions();
	TestFalse(TEXT("102b2d67 SEE_ENEMY or NEW_ENEMY resets m_hBlockedDoor"), N.BlockedDoor.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19TroikaDetectedAttackTest,
	"Elysium.Substrate.NpcKernelConditions19.TroikaGatherConditions.DelayedDetectedAttack", GCond19Flags)
bool FCond19TroikaDetectedAttackTest::RunTest(const FString&)
{
	// `102b2d75..102b2df4`: the program masks DETECTED_ATTACK, the notice is unexpired and its
	// attacker resolves -> a delayed 0x0b at RandomFloat(0.9, 1.3) (`0x102cc6c0`) and `+0x65c4`
	// restamped to curtime (in the port's stamp form, the notice moved back by the retention).
	FCond19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	F.HidePlayer();
	const double Now = F.Now();
	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::DetectedAttack);
		const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Mask);
		TestTrue(TEXT("the carrier program installs"), ElysiumSchedule::Start(N.Schedule, ElysiumSched::IDLE_STAND, N));
		N.Senses.Memory.DetectedAttackAttacker = F.Other->Handle;
		N.Senses.Memory.DetectedAttackTime = Now;
		N.Cognition.DelayedConditions.Reset();
		N.GatherConditions();
		TestEqual(TEXT("102b2de6 one delayed entry"), N.Cognition.DelayedConditions.Num(), 1);
		if (N.Cognition.DelayedConditions.Num() == 1)
		{
			const TPair<int32, double>& Entry = N.Cognition.DelayedConditions[0];
			TestEqual(TEXT("...for DETECTED_ATTACK"), Entry.Key, static_cast<int32>(EElysiumNpcCond::DetectedAttack));
			TestTrue(TEXT("...promoted 0.9..1.3 s out"), Entry.Value >= Now + 0.9 - 1e-4 && Entry.Value <= Now + 1.3 + 1e-4);
		}
		TestEqual(TEXT("102b2df4 +0x65c4 := curtime"), N.Senses.Memory.DetectedAttackTime,
			Now - ElysiumNpcCond::DetectedAttackRetentionSeconds);
		TestFalse(TEXT("102b28bd the bit itself is cleared this pass"), Cond19Has(N, EElysiumNpcCond::DetectedAttack));

		N.GatherConditions();
		TestEqual(TEXT("102b2d91 the expired notice pushes nothing more"), N.Cognition.DelayedConditions.Num(), 1);
		N.Schedule.Clear();
	}

	// `0x102cc6c0`'s own rules: a second push of the same condition keeps the EARLIER stamp; a full
	// list of eight drops the push.
	N.Cognition.DelayedConditions.Reset();
	N.Conditions19PushDelayedCondition(0x0b, 1.0f, Now);
	N.Conditions19PushDelayedCondition(0x0b, 2.0f, Now);
	TestEqual(TEXT("102cc590 a later stamp does not replace"), N.Cognition.DelayedConditions[0].Value, Now + 1.0);
	N.Conditions19PushDelayedCondition(0x0b, 0.5f, Now);
	TestEqual(TEXT("102cc590 an earlier one does"), N.Cognition.DelayedConditions[0].Value, Now + 0.5);
	for (int32 Cond = 0x40; Cond < 0x47; ++Cond)
	{
		N.Conditions19PushDelayedCondition(Cond, 1.0f, Now);
	}
	N.Conditions19PushDelayedCondition(0x50, 1.0f, Now);
	TestEqual(TEXT("102cc6c0 CMP 8 / JGE: the ninth is dropped"), N.Cognition.DelayedConditions.Num(), 8);
	return true;
}

// =================================================================================================
// The species overrides
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19AndreiBloodTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.AndreiBlood1035d180", GCond19Flags)
bool FCond19AndreiBloodTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CNPC_VAndreiBlood"));
	if (!TestNotNull(TEXT("Andrei constructs"), F.Guard))
	{
		return false;
	}
	F.HidePlayer();
	F.Guard->Cognition.GatheredAt = -1.0;
	F.Guard->Cognition.Conditions.Set(FElysiumNpcBase::Cond19AndreiTimeToTeleport);
	F.Guard->GatherConditions();
	TestFalse(TEXT("1035d18c the Troika gather, then ClearCondition(0x79)"),
		Cond19Has(*F.Guard, FElysiumNpcBase::Cond19AndreiTimeToTeleport));
	TestTrue(TEXT("1035d183 the Troika body ran first (the gathered stamp)"), F.Guard->Cognition.GatheredAt >= 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19BachTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.Bach10365a70", GCond19Flags)
bool FCond19BachTest::RunTest(const FString&)
{
	// 0x10365a70 then its tail 0x10365a90: ten blind probes -> the occlusion bookkeeping; the >4 s
	// re-check latches the camper flag inside 200 units and warns (`bach_camp_warn`); GrenadeActive
	// 10 forces the latch and the grenade warning; a re-seen enemy while camping installs 0x15f.
	FCond19Fixture F(TEXT("CNPC_VBach"));
	FElysiumNpcBach* Bach = F.Guard != nullptr ? ElysiumTestAsSpecies<FElysiumNpcBach>(F.Guard) : nullptr;
	if (!TestNotNull(TEXT("Bach constructs"), Bach) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	F.HidePlayer();
	const double Now = F.Now();
	ElysiumNpcEnemy::SetEnemy(*Bach, F.Other->Handle);
	F.BlindGuard();
	Bach->BachLastOccludeOriginUnits = F.Other->Origin / ElysiumMove::U;   // L11's word, SOURCE units
	Bach->bBachCamperFlag = false;
	Bach->NamedWavEmits.Reset();

	Bach->GatherConditions();
	TestEqual(TEXT("10365afb m_iWasOccluded := 1"), Bach->BachWasOccluded, 1);
	TestEqual(TEXT("10365b10 m_flOccludeEnterTime := curtime"), Bach->BachOccludeEnterTime, Now);
	TestEqual(TEXT("10365b4f a zero area counts a reuse"), Bach->BachReusedOccludeCount, 1);
	TestFalse(TEXT("10365b61 one reuse does not latch"), Bach->bBachCamperFlag);

	Bach->BachOccludeEnterTime = Now - 5.0;
	Bach->GatherConditions();
	TestTrue(TEXT("10365c2a an enemy within 200 units after 4 s latches the camper flag"), Bach->bBachCamperFlag);
	TestTrue(TEXT("10365e6e the new latch warns with bach_camp_warn"),
		Bach->NamedWavEmits.Num() == 1
			&& Bach->NamedWavEmits[0].Wav == FString(TEXT("Character/Boss/Bach/bach_camp_warn.wav")));

	Bach->BachGrenadeActive = 10;
	Bach->bBachInStartingPosition = false;
	Bach->BachTeleportState = 0;
	Bach->GatherConditions();
	TestTrue(TEXT("10365c7f GrenadeActive 10 forces a fresh latch: bach_grenade"),
		Bach->NamedWavEmits.Num() == 2
			&& Bach->NamedWavEmits[1].Wav == FString(TEXT("Character/Boss/Bach/bach_grenade.wav")));

	Bach->BachGrenadeActive = 0;
	Bach->Disciplines.TargetEffects.Reset();
	const int32 CallsBefore = Bach->SetScheduleRetailCalls;
	Bach->GatherConditions();
	TestEqual(TEXT("10365f7a a re-seen enemy clears m_iWasOccluded"), Bach->BachWasOccluded, 0);
	TestEqual(TEXT("10365fa5 ...and, camping, installs schedule 0x15f"), Bach->SetScheduleRetailCalls, CallsBefore + 1);
	TestEqual(TEXT("...by that number"), Bach->LastSetScheduleRetail, 0x15f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19ChangBrosTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.ChangBros1036b590", GCond19Flags)
bool FCond19ChangBrosTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CNPC_VChangBrosBlade"));
	FElysiumNpcChangBros* Chang = F.Guard != nullptr ? ElysiumTestAsSpecies<FElysiumNpcChangBros>(F.Guard) : nullptr;
	if (!TestNotNull(TEXT("a Chang brother constructs"), Chang))
	{
		return false;
	}
	F.HidePlayer();
	Chang->Cognition.Conditions.Set(FElysiumNpcBase::Cond19ChangTimeToJumpAttack);
	Chang->Cognition.Conditions.Set(FElysiumNpcBase::Cond19ChangTimeToUnitedAttack);
	Chang->Cognition.Conditions.Set(FElysiumNpcBase::Cond19ChangTimeToTeleport);
	Chang->GatherConditions();
	const bool bJump = Chang->CheckForJumpAttack();
	const bool bUnited = Chang->CheckForUnited();
	TestEqual(TEXT("1036b5f3 / 1036b61b 0x79 is CheckForJumpAttack's answer"),
		Cond19Has(*Chang, FElysiumNpcBase::Cond19ChangTimeToJumpAttack), bJump);
	TestEqual(TEXT("1036b5fc / 1036b659 0x7c is CheckForUnited's answer"),
		Cond19Has(*Chang, FElysiumNpcBase::Cond19ChangTimeToUnitedAttack), bUnited);
	TestTrue(TEXT("1036b63a TIME_TO_TELEPORT is never cleared here: it latches"),
		Cond19Has(*Chang, FElysiumNpcBase::Cond19ChangTimeToTeleport));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19DogTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.Dog10374b00", GCond19Flags)
bool FCond19DogTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CNPC_VDog"));
	FElysiumNpcDog* Dog = F.Guard != nullptr ? ElysiumTestAsSpecies<FElysiumNpcDog>(F.Guard) : nullptr;
	if (!TestNotNull(TEXT("the dog constructs"), Dog))
	{
		return false;
	}
	F.HidePlayer();
	// `10374b13` / `10374b30`: PLAYER_ATTACKED, then friendship level 6 returns.
	Dog->bPlayerAttackedMe = true;
	Dog->AnimalFriendshipLevel = 6;
	Dog->ActivityNumber = 0x6e;
	Dog->Cognition.Conditions.Clear(FElysiumNpcBase::Cond19DogPlayerMovedAway);
	Dog->GatherConditions();
	TestTrue(TEXT("10374b24 m_bPlayerAttackedMe raises 0x7e"), Cond19Has(*Dog, FElysiumNpcBase::Cond19DogPlayerAttacked));
	TestFalse(TEXT("10374b30 friendship 6 returns before the snarl arm"),
		Cond19Has(*Dog, FElysiumNpcBase::Cond19DogPlayerMovedAway));

	// `10374b41..10374b75`: neither heard nor seen, snarling -> PLAYER_MOVEDAWAY and return.
	Dog->bPlayerAttackedMe = false;
	Dog->AnimalFriendshipLevel = 0;
	Dog->ActivityNumber = 0x6e;
	Dog->DogSnarlSourceWord = -1.f;
	Dog->GatherConditions();
	TestTrue(TEXT("10374b75 an unheard, unseen player while snarling: 0x7c"),
		Cond19Has(*Dog, FElysiumNpcBase::Cond19DogPlayerMovedAway));
	TestEqual(TEXT("...and the body returned before the distance word"), Dog->DogSnarlSourceWord, -1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19FrenzyShadowTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.FrenzyShadow10375ed0", GCond19Flags)
bool FCond19FrenzyShadowTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CNPC_VFrenzyShadow"));
	FElysiumNpcFrenzyShadow* Shadow = F.Guard != nullptr ? ElysiumTestAsSpecies<FElysiumNpcFrenzyShadow>(F.Guard) : nullptr;
	if (!TestNotNull(TEXT("the shadow constructs"), Shadow))
	{
		return false;
	}
	F.HidePlayer();
	// `ChooseEnemy` inside the base gather reaches slot 478 `BestEnemy` (`0x103766d0`), whose rescan
	// zeroes and rebuilds `+0x6664` (`103767a2`). A committed, living enemy with no SEE_HATE family
	// keeps the choice sticky, so the count this body reads is the one the case wrote (the lane's
	// first cut let the rescan zero it).
	ElysiumNpcEnemy::SetEnemy(*Shadow, F.Other->Handle);
	Shadow->HostileEnemyCount = 2;
	Shadow->GatherConditions();
	TestTrue(TEXT("10375ef0 more than one hostile: 0x79"), Cond19HasOrdinal(*Shadow, 0x79));
	Shadow->HostileEnemyCount = 1;
	Shadow->GatherConditions();
	TestFalse(TEXT("10375efb one hostile (JLE, signed): 0x79 cleared"), Cond19HasOrdinal(*Shadow, 0x79));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19GargoyleTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.Gargoyle10378df0", GCond19Flags)
bool FCond19GargoyleTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CNPC_VGargoyle"));
	if (!TestNotNull(TEXT("the gargoyle constructs"), F.Guard) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	F.HidePlayer();
	N.Cognition.Conditions.Set(EElysiumNpcCond::TooCloseToAttack);
	N.Cognition.Conditions.Set(EElysiumNpcCond::ShouldDodge);
	N.GatherConditions();
	TestFalse(TEXT("10378e06 SHOULD_DODGE cleared"), Cond19Has(N, EElysiumNpcCond::ShouldDodge));
	TestTrue(TEXT("10378e22 no enemy returns before the distance test"), Cond19Has(N, EElysiumNpcCond::TooCloseToAttack));

	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	F.Other->Origin = FVector(50.0 * ElysiumMove::U, 0.0, 0.0);   // exactly 50 units in 2-D
	N.GatherConditions();
	TestFalse(TEXT("10378e72 at 50.0 (<= 50) TOO_CLOSE_TO_ATTACK is cleared"), Cond19Has(N, EElysiumNpcCond::TooCloseToAttack));
	TestFalse(TEXT("10378e7b ...and SHOULD_STEPBACK"), Cond19Has(N, EElysiumNpcCond::ShouldStepback));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19GhoulCroucherTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.GhoulCroucher1037b570", GCond19Flags)
bool FCond19GhoulCroucherTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CNPC_VGhoulCroucher"));
	FElysiumNpcGhoulCroucher* Ghoul = F.Guard != nullptr ? ElysiumTestAsSpecies<FElysiumNpcGhoulCroucher>(F.Guard) : nullptr;
	if (!TestNotNull(TEXT("the croucher constructs"), Ghoul))
	{
		return false;
	}
	F.HidePlayer();
	const double Now = F.Now();
	Ghoul->bWasDisturbed = false;
	Ghoul->Cognition.Conditions.Clear(EElysiumNpcCond::NewEnemy);
	Ghoul->Cognition.GatheredAt = -1.0;
	Ghoul->GatherConditions();
	TestTrue(TEXT("1037b682 undisturbed with no NEW_ENEMY: UNAWARE 0x79"), Cond19Has(*Ghoul, FElysiumNpcBase::Cond19CroucherUnaware));
	TestEqual(TEXT("...and the Troika body did not run"), Ghoul->Cognition.GatheredAt, -1.0);

	Ghoul->Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
	Ghoul->GatherConditions();
	TestTrue(TEXT("1037b5f4 a standing NEW_ENEMY disturbs it"), Ghoul->bWasDisturbed);
	TestEqual(TEXT("1037b601 ...and returns without the base"), Ghoul->Cognition.GatheredAt, -1.0);

	Ghoul->GatherConditions();
	TestEqual(TEXT("1037b60e disturbed: the Troika body runs"), Ghoul->Cognition.GatheredAt, Now);
	TestEqual(TEXT("1037b628 no squad: SquadNewEnemy is not reached"), Ghoul->SelectIdealStateSquadNewEnemyCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19HengeyokaiTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.Hengeyokai103803d0", GCond19Flags)
bool FCond19HengeyokaiTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CNPC_VHengeyokai"));
	FElysiumNpcHengeyokai* Hengeyokai = F.Guard != nullptr ? ElysiumTestAsSpecies<FElysiumNpcHengeyokai>(F.Guard) : nullptr;
	if (!TestNotNull(TEXT("the hengeyokai constructs"), Hengeyokai) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	F.HidePlayer();
	Hengeyokai->Cognition.Conditions.Set(FElysiumNpcBase::Cond19HaveEnemyThrowLos);
	Hengeyokai->Cognition.Conditions.Set(EElysiumNpcCond::ShouldBlock);
	Hengeyokai->NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);
	Hengeyokai->GatherConditions();
	TestFalse(TEXT("1038043b not carrying: HAVE_ENEMY_THROW_LOS cleared"), Cond19Has(*Hengeyokai, FElysiumNpcBase::Cond19HaveEnemyThrowLos));
	TestFalse(TEXT("103803ee SHOULD_BLOCK cleared"), Cond19Has(*Hengeyokai, EElysiumNpcCond::ShouldBlock));

	ElysiumNpcEnemy::SetEnemy(*Hengeyokai, F.Other->Handle);
	Hengeyokai->NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
	Hengeyokai->GatherConditions();
	TestEqual(TEXT("10380430 carrying, HAVE_ENEMY_LOS and a clear throw line: 0x1b"),
		Cond19Has(*Hengeyokai, FElysiumNpcBase::Cond19HaveEnemyThrowLos),
		Cond19Has(*Hengeyokai, EElysiumNpcCond::HaveEnemyLos));
	TestFalse(TEXT("10382031 the throw test refuses a null enemy"), Hengeyokai->HengeyokaiThrowLosTest(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19MingXiaoTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.MingXiao10394e40", GCond19Flags)
bool FCond19MingXiaoTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CNPC_VMingXiao"));
	FElysiumNpcMingXiao* Ming = F.Guard != nullptr ? ElysiumTestAsSpecies<FElysiumNpcMingXiao>(F.Guard) : nullptr;
	if (!TestNotNull(TEXT("Ming Xiao constructs"), Ming))
	{
		return false;
	}
	F.HidePlayer();
	for (int32 Ordinal = 0x77; Ordinal <= 0x7e; ++Ordinal)
	{
		Ming->Cognition.Conditions.SetOrdinal(Ordinal);
	}
	Ming->WriteNpcStateRetail(1);
	Ming->GatherConditions();
	for (int32 Ordinal = 0x77; Ordinal <= 0x7d; ++Ordinal)
	{
		TestFalse(FString::Printf(TEXT("10394e4a..10394e80 clears 0x%x"), Ordinal), Cond19HasOrdinal(*Ming, Ordinal));
	}
	TestTrue(TEXT("0x7e is not in the clear list: it latches"), Cond19HasOrdinal(*Ming, 0x7e));

	// Out of idle, with the ranged weapon unset: the spit arm closes at `10394f50`.
	Ming->WriteNpcStateRetail(3);
	Ming->Cognition.Conditions.Clear(FElysiumNpcBase::Cond19MingXiaoMeleeHelpless);
	Ming->GatherConditions();
	int32 UnusedSchedule = 0;
	bool bAny = false;
	for (int32 Slot = 0; Slot < 6; ++Slot)
	{
		bAny |= Ming->FUN_10398030(Slot, false, UnusedSchedule);
	}
	TestEqual(TEXT("10394ef0 MELEE_HELPLESS is raised exactly when no slot admits"),
		Cond19Has(*Ming, FElysiumNpcBase::Cond19MingXiaoMeleeHelpless), !bAny);
	TestFalse(TEXT("10394f50 no m_hRangedWeapon: no CAN_ATTACK_SPIT"), Cond19Has(*Ming, FElysiumNpcBase::Cond19MingXiaoCanAttackSpit));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19MingXiaoTentacleTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.MingXiaoTentacle1039ec10", GCond19Flags)
bool FCond19MingXiaoTentacleTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CNPC_VMingXiaoTentacle"));
	FElysiumNpcMingXiaoTentacle* Tentacle = F.Guard != nullptr ? ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(F.Guard) : nullptr;
	if (!TestNotNull(TEXT("the tentacle constructs"), Tentacle) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	F.HidePlayer();
	const double Now = F.Now();
	ElysiumNpcEnemy::SetEnemy(*Tentacle, F.Other->Handle);
	Tentacle->MingXiaoTentacleFailedEvadeTimer = 0.0;
	Tentacle->ScheduleHost.EnemyDistUnits = 256.f;
	Tentacle->MingXiaoTentaclePhaseExpireTimer = Now + 10.0;
	Tentacle->GatherConditions();
	TestTrue(TEXT("1039ec72 an enemy at 256 (<=) with the evade timer run out: FLEE"), Cond19Has(*Tentacle, FElysiumNpcBase::Cond19TentacleFlee));
	TestFalse(TEXT("1039ec8c a future phase timer: no PHASE_EXPIRED"), Cond19Has(*Tentacle, FElysiumNpcBase::Cond19TentaclePhaseExpired));

	Tentacle->ScheduleHost.EnemyDistUnits = 257.f;
	Tentacle->MingXiaoTentaclePhaseExpireTimer = Now;
	Tentacle->GatherConditions();
	TestFalse(TEXT("1039ec15 cleared, and 257 does not re-raise FLEE"), Cond19Has(*Tentacle, FElysiumNpcBase::Cond19TentacleFlee));
	TestTrue(TEXT("1039ec9d the phase timer reached: PHASE_EXPIRED"), Cond19Has(*Tentacle, FElysiumNpcBase::Cond19TentaclePhaseExpired));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19PedestrianTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.Pedestrian103a2c30", GCond19Flags)
bool FCond19PedestrianTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CNPC_VPedestrian"));
	if (!TestNotNull(TEXT("the pedestrian constructs"), F.Guard))
	{
		return false;
	}
	F.HidePlayer();
	F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::PassOut);
	const int32 ReportsBefore = F.Guard->PedestrianCrimeReports;
	F.Guard->GatherConditions();
	TestFalse(TEXT("103a2c3a PASS_OUT is cleared before the Troika body"), Cond19Has(*F.Guard, EElysiumNpcCond::PassOut));
	TestEqual(TEXT("103a2c51 / 103a2d70 no witness record (no source, or the weapon's crime level -1)"),
		F.Guard->PedestrianCrimeReports, ReportsBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19SabbatLeaderTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.SabbatLeader103a77f0", GCond19Flags)
bool FCond19SabbatLeaderTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CNPC_VSabbatLeader"));
	FElysiumNpcSabbatLeader* Leader = F.Guard != nullptr ? ElysiumTestAsSpecies<FElysiumNpcSabbatLeader>(F.Guard) : nullptr;
	if (!TestNotNull(TEXT("the leader constructs"), Leader))
	{
		return false;
	}
	F.HidePlayer();
	for (const EElysiumNpcCond Cond : { EElysiumNpcCond::ShouldKick, EElysiumNpcCond::ShouldDodge, EElysiumNpcCond::ShouldStepback })
	{
		Leader->Cognition.Conditions.Set(Cond);
	}
	Leader->Cognition.Conditions.Clear(FElysiumNpcBase::Cond19SabbatTimeToJump);
	Leader->GatherConditions();
	TestFalse(TEXT("103a7868 SHOULD_KICK cleared"), Cond19Has(*Leader, EElysiumNpcCond::ShouldKick));
	TestFalse(TEXT("103a7871 SHOULD_DODGE cleared"), Cond19Has(*Leader, EElysiumNpcCond::ShouldDodge));
	TestFalse(TEXT("103a787a SHOULD_STEPBACK cleared"), Cond19Has(*Leader, EElysiumNpcCond::ShouldStepback));
	TestEqual(TEXT("103a785f TIME_TO_JUMP follows CheckForJumpCondition"),
		Cond19Has(*Leader, FElysiumNpcBase::Cond19SabbatTimeToJump), Leader->CheckForJumpCondition());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19ScurryingTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.Scurrying103ac500", GCond19Flags)
bool FCond19ScurryingTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CNPC_VRat"));
	FElysiumNpcScurrying* Rat = F.Guard != nullptr ? ElysiumTestAsSpecies<FElysiumNpcScurrying>(F.Guard) : nullptr;
	if (!TestNotNull(TEXT("the rat constructs"), Rat) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	const double Now = F.Now();
	// The gate closed (`103ac526`): the stored handle is not re-evaluated and raises 0x78.
	Rat->ScurryingDetectGateTime = Now + 10.0;
	Rat->ScurryingDetected = F.Other->Handle;
	Rat->GatherConditions();
	TestTrue(TEXT("103ac5c4 a resolving +0x667c raises PLAYER_TOOCLOSE"), Cond19Has(*Rat, FElysiumNpcBase::Cond19ScurryingPlayerTooClose));
	TestTrue(TEXT("103ac526 a closed gate keeps the handle"), Rat->ScurryingDetected == F.Other->Handle);

	// The gate open: an undetectable stored target is replaced by the detect probe's answer.
	Rat->ScurryingDetectGateTime = 0.0;
	Rat->GatherConditions();
	FElysiumEntity* const Expected = Rat->ScurryingShouldDetect(F.Other) ? static_cast<FElysiumEntity*>(F.Other)
		: Rat->ScurryingFindDetectablePlayer();
	TestTrue(TEXT("103ac57e +0x667c is the kept target or the probe's player"),
		Expected != nullptr ? Rat->ScurryingDetected == Expected->Handle : !Rat->ScurryingDetected.IsSet());
	TestEqual(TEXT("103ac505 / 103ac5c4 PLAYER_TOOCLOSE follows the handle"),
		Cond19Has(*Rat, FElysiumNpcBase::Cond19ScurryingPlayerTooClose), Rat->ScurryingDetected.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19TzimisceTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.Tzimisce103bce40", GCond19Flags)
bool FCond19TzimisceTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CNPC_VTzimisce"), TEXT("14000"));
	FElysiumNpcTzimisce* Tz = F.Guard != nullptr ? ElysiumTestAsSpecies<FElysiumNpcTzimisce>(F.Guard) : nullptr;
	const FElysiumEntity* const Hint = F.World.World.FindByName(TEXT("hint"));
	if (!TestNotNull(TEXT("the Tzimisce constructs"), Tz) || !TestNotNull(TEXT("the other"), F.Other)
		|| !TestNotNull(TEXT("the claw hint stands"), Hint))
	{
		return false;
	}
	F.HidePlayer();
	const double Now = F.Now();
	for (const EElysiumNpcCond Cond : { FElysiumNpcBase::Cond19TzimisceShouldDropBody,
		FElysiumNpcBase::Cond19HaveEnemyThrowLos, FElysiumNpcBase::Cond19TzimisceForceThrowBody })
	{
		Tz->Cognition.Conditions.Set(Cond);
	}
	Tz->NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);
	Tz->BaseScheduleHost.HintNode = Hint->Handle.Index;
	Tz->TzimiscePounceCheckTimer = Now - 1.0;
	Tz->Cognition.Conditions.Set(FElysiumNpcBase::Cond19CanPounce);
	Tz->TzimisceShunnedBodyTimer = Now - 1.0;
	Tz->TzimisceShunnedFindBody = 3;
	Tz->GatherConditions();
	TestFalse(TEXT("103bce50 SHOULD_DROP_BODY cleared"), Cond19Has(*Tz, FElysiumNpcBase::Cond19TzimisceShouldDropBody));
	TestFalse(TEXT("103bce59 HAVE_ENEMY_THROW_LOS cleared"), Cond19Has(*Tz, FElysiumNpcBase::Cond19HaveEnemyThrowLos));
	TestFalse(TEXT("103bce62 FORCE_THROW_BODY cleared"), Cond19Has(*Tz, FElysiumNpcBase::Cond19TzimisceForceThrowBody));
	TestTrue(TEXT("103bd07f a claw hint and no enemy: CLAW_HINT_INVALID"), Cond19Has(*Tz, FElysiumNpcBase::Cond19ClawHintInvalid));
	TestTrue(TEXT("103bd093 ...and CLAW_HINT_SPECIAL_INVALID"), Cond19Has(*Tz, FElysiumNpcBase::Cond19ClawHintSpecialInvalid));
	TestEqual(TEXT("103bd0c1 the pounce timer re-arms +2.5"), Tz->TzimiscePounceCheckTimer, Now + 2.5);
	TestFalse(TEXT("103bd145 no masked pounce: CAN_POUNCE cleared"), Cond19Has(*Tz, FElysiumNpcBase::Cond19CanPounce));
	TestEqual(TEXT("103bd16b m_iShunnedFindBody := 0"), Tz->TzimisceShunnedFindBody, 0);
	TestEqual(TEXT("103bd175 the shunned timer re-arms +10"), Tz->TzimisceShunnedBodyTimer, Now + 10.0);

	// An enemy 400 cm (157 units, 24800 squared) from the hint: inside [10000, 40000] the seam's
	// unusable hint raises 0x1c and CLEARS 0x1d; at 800 cm (99200) 0x1d is raised.
	ElysiumNpcEnemy::SetEnemy(*Tz, F.Other->Handle);
	Tz->GatherConditions();
	TestTrue(TEXT("103bd000 an unusable hint: CLAW_HINT_INVALID"), Cond19Has(*Tz, FElysiumNpcBase::Cond19ClawHintInvalid));
	TestFalse(TEXT("103bd065 inside the band: CLAW_HINT_SPECIAL_INVALID cleared"), Cond19Has(*Tz, FElysiumNpcBase::Cond19ClawHintSpecialInvalid));
	F.Other->Origin = FVector(800.0, 0.0, 0.0);
	Tz->GatherConditions();
	TestTrue(TEXT("103bd05f beyond 40000: CLAW_HINT_SPECIAL_INVALID"), Cond19Has(*Tz, FElysiumNpcBase::Cond19ClawHintSpecialInvalid));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19TzimisceClawRunnerTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.TzimisceHeadClaw103c17f0AndRunner103c35a0", GCond19Flags)
bool FCond19TzimisceClawRunnerTest::RunTest(const FString&)
{
	for (const TCHAR* ClassName : { TEXT("CNPC_VTzimisceHeadClaw"), TEXT("CNPC_VTzimisceRunner") })
	{
		FCond19Fixture F(ClassName);
		if (!TestNotNull(TEXT("the claw/runner constructs"), F.Guard))
		{
			return false;
		}
		F.HidePlayer();
		F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::ShouldKick);
		F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::ShouldStepback);
		F.Guard->GatherConditions();
		TestFalse(FString::Printf(TEXT("%s: SHOULD_KICK cleared (103c17fc / 103c35ac)"), ClassName),
			Cond19Has(*F.Guard, EElysiumNpcCond::ShouldKick));
		TestFalse(FString::Printf(TEXT("%s: SHOULD_STEPBACK cleared (103c1805 / 103c35b5)"), ClassName),
			Cond19Has(*F.Guard, EElysiumNpcCond::ShouldStepback));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCond19WerewolfTest,
	"Elysium.Substrate.NpcKernelConditions19.Species.Werewolf103d0410", GCond19Flags)
bool FCond19WerewolfTest::RunTest(const FString&)
{
	FCond19Fixture F(TEXT("CNPC_VWerewolf"));
	FElysiumNpcWerewolf* Wolf = F.Guard != nullptr ? ElysiumTestAsSpecies<FElysiumNpcWerewolf>(F.Guard) : nullptr;
	if (!TestNotNull(TEXT("the werewolf constructs"), Wolf) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	F.HidePlayer();
	Wolf->MoveHintNode = INDEX_NONE;
	Wolf->Cognition.Conditions.Clear(FElysiumNpcBase::Cond19WerewolfCanSpecialMove);
	// Witness of the three Werewolf19 updaters: `0x103cc450` CLEARS `0x7b` first thing (`0x103cc4c2`)
	// and, with `+0x66e8` bit 2 clear, sets nothing back; `0x103cc320` sets exactly one of `0x59` /
	// `0x79` (`0x103cc3a1` / `0x103cc3c9`).
	const EElysiumNpcCond ShouldBreakHint = static_cast<EElysiumNpcCond>(0x7b);
	const EElysiumNpcCond EnemyReachable = static_cast<EElysiumNpcCond>(0x79);
	Wolf->WerewolfHintFlags = 0;
	Wolf->Cognition.Conditions.Set(ShouldBreakHint);
	Wolf->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyUnreachable);
	Wolf->Cognition.Conditions.Clear(EnemyReachable);
	Wolf->GatherConditions();
	TestTrue(TEXT("103d04ff no enemy: 0x103cc450 does not run (0x7b kept)"), Cond19Has(*Wolf, ShouldBreakHint));
	TestFalse(TEXT("103d04ff no enemy: 0x103cc320 does not run (no 0x79)"), Cond19Has(*Wolf, EnemyReachable));
	TestEqual(TEXT("103d052b..103d0564 TOO_FAR_FOR_MELEE and TOO_FAR_TO_ATTACK agree afterwards"),
		Cond19Has(*Wolf, EElysiumNpcCond::TooFarForMelee), Cond19Has(*Wolf, EElysiumNpcCond::TooFarToAttack));

	ElysiumNpcEnemy::SetEnemy(*Wolf, F.Other->Handle);
	Wolf->MoveHintNode = 7;
	// With `0x103cc5c0` now live (lane L12), a held move hint with no path is cleared at
	// `0x103cc797` before `0x103d0569` reads `m_pMoveHint`. `m_fEffects & 0x40` returns from
	// `0x103cc5c0` at its second gate (`0x103cc6a4` / `0x103cc6af`), which keeps the hint for the
	// `0x103d0582` arm this case pins.
	Wolf->EffectsWord |= 0x40u;
	Wolf->GatherConditions();
	TestFalse(TEXT("103d0522 0x103cc450 ran: 0x7b cleared"), Cond19Has(*Wolf, ShouldBreakHint));
	TestTrue(TEXT("103d050d 0x103cc320 ran: one of 0x59 / 0x79 set"),
		Cond19Has(*Wolf, EElysiumNpcCond::EnemyUnreachable) != Cond19Has(*Wolf, EnemyReachable));
	TestNotNull(TEXT("103d04cc slot 544 remembered the enemy before the base"), Wolf->EnemyMemory.Find(F.Other->Handle));
	TestTrue(TEXT("103d0582 m_pMoveHint: CAN_SPECIAL_MOVE"), Cond19Has(*Wolf, FElysiumNpcBase::Cond19WerewolfCanSpecialMove));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
