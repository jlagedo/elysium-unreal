// Story 0019/8 (29e under the strict verdict), family **Damage19** -- the family's tests.
//
// Test names carry `Elysium.Substrate.NpcKernelDamage19.` and the retail address. One case per
// `rule` row at least, each assertion read off the listing (`vtmb_asm`) with the address beside it.
// The walked prose is `docs/vtmb/npc-ai/conditions-and-states.md` § "Story 8, family Damage19".
//
// Two standing facts shape the cases:
//   * `CBaseCombatCharacter::OnTakeDamage` (`0x1032ef60`) and `OnTakeDamage_Alive` (`0x103302e0`)
//     are hand bodies since story 8 wave 2 (`ElysiumCombatCharacter.cpp`): the slot-142 chains
//     answer what `0x1032ef60` answers, and `0x103302e0` answers its one value, 1, so the slot-390
//     chains run whole.
//   * The see/unseen split of `0x10265ed0` is decided by the NPC's own slot 363 / slot 201. A case
//     asks the same two virtuals first and asserts the arm that answer selects, rather than
//     assuming a headless trace outcome.
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
#include "ElysiumStub.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcZombie.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"
#include "Tests/ElysiumMeleeTestHelpers.h"

static constexpr EAutomationTestFlags GDamage19TestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	const TCHAR* const GDamage19TestFists = TEXT("item_w_fists");

	FElysiumItemTable MakeDamage19ItemTable()
	{
		FElysiumItemTable Table;
		FElysiumItemDef Fists;
		Fists.Classname = GDamage19TestFists;
		Fists.PrintName = TEXT("Fists");
		Fists.Type = EElysiumItemType::WeaponMelee;
		Fists.bHidden = true;
		FElysiumWeaponMode Mode;
		Mode.Tag = TEXT("Primary");
		Mode.TypeName = TEXT("Attack");
		Mode.Type = EElysiumWeaponModeType::Attack;
		Mode.Dmg = TEXT("2 Bashing Close_Combat_Brawl DMG_FIST");
		Mode.BaseLethality = 8;
		Mode.SkillRequirement = 1;
		Mode.AttackRate = 0.5f;
		Mode.Range = 0.f;
		Mode.AmmoCost = 0;
		Mode.AmmoFired = 1;
		Fists.Modes.Add(MoveTemp(Mode));
		Table.Items.Add(MoveTemp(Fists));
		Table.Reindex();
		return Table;
	}

	// The guard is stood as the retail class a case exercises; `other` is an ordinary NPC attacker
	// behind it, `enemy` a second NPC for the committed-enemy arms, `boom` a `point_explosion` and
	// `crate` a non-NPC entity. `damaged`/`half` count the guard's two outputs.
	struct FDamage19Fixture
	{
		FElysiumItemTable Items;
		bool bInstalled = false;
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumNpc* Enemy = nullptr;
		FElysiumPlayer* Player = nullptr;

		static FElysiumNpcWorldBuilder Build(const TCHAR* GuardClass)
		{
			FElysiumNpcWorldBuilder Builder(TEXT("damage19_kernel"), 0x44313930);
			Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
			Builder.AddNpcOfClass(TEXT("guard"), FVector::ZeroVector, GuardClass);
			Builder.AddNpc(TEXT("other"), FVector(-400.f * ElysiumMove::U, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
			Builder.AddNpc(TEXT("enemy"), FVector(0.f, 600.f * ElysiumMove::U, 0.f), TEXT("npc_VHumanCombatant"));
			Builder.AddEntity(TEXT("point_explosion"), TEXT("boom"), FVector(0.f, -300.f, 0.f));
			Builder.AddCounter(TEXT("damaged"));
			Builder.AddCounter(TEXT("half"));
			Builder.WireOutput(TEXT("guard"), TEXT("OnDamaged"), TEXT("damaged"));
			Builder.WireOutput(TEXT("guard"), TEXT("OnHalfHealth"), TEXT("half"));
			return Builder;
		}

		explicit FDamage19Fixture(const TCHAR* GuardClass = TEXT("CAI_BaseNPCTroika"), bool bArm = false)
			: Items(MakeDamage19ItemTable())
			, World(Build(GuardClass), [this, bArm](FElysiumRecordingServices&)
				{
					if (bArm)
					{
						ElysiumItems::Install(Items);
						bInstalled = true;
					}
				})
		{
			Guard = World.Npc(TEXT("guard"));
			Other = World.Npc(TEXT("other"));
			Enemy = World.Npc(TEXT("enemy"));
			Player = World.Player();
			if (Player != nullptr)
			{
				Player->Origin = FVector(0.0, 9000.0 * ElysiumMove::U, 0.0);
			}
			FElysiumNpcWorldFixture::Quiet({ Guard, Other, Enemy });
			if (Guard != nullptr)
			{
				FElysiumNpcWorldFixture::PrepareForKernelDrive(Guard, 100);
				Guard->Health = 100;
				// "No damage yet": the retail stamp is float curtime, and the clock stands at 0.
				Guard->BaseMemory.RepeatedDamageWindowStart = -1.0;
				Guard->BaseMemory.RepeatedDamageAccumulated = 0.f;
				// The type-0 stat list the species floors read: cap 20, no wounds.
				Guard->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 20);
				Guard->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 0);
				Guard->RecomputeSheet();
				Guard->MaxHealth = 100;
				Guard->Health = 100;
				// `DAMAGE_EVENTS_ONLY`: the cases exercise the NPC bodies ABOVE the combat character's
				// commit. Since story 8 wave 2 `0x103302e0` is a hand body, and on this setting it
				// commits nothing (`m_takedamage == 1`), so the health and ceiling a case sets stand;
				// every gate of `0x1032ef60` still passes (it refuses only `DAMAGE_NO`).
				Guard->TakeDamageMode = 1;
			}
		}

		~FDamage19Fixture()
		{
			if (bInstalled)
			{
				ElysiumItems::Uninstall(Items);
			}
			FElysiumNpc::ResetDeathThrowImpulse();
		}

		FDamage19Fixture(const FDamage19Fixture&) = delete;
		FDamage19Fixture& operator=(const FDamage19Fixture&) = delete;

		double Now() { return World.World.NowSeconds(); }

		// Deliver queued outputs (K11: producers enqueue, only queue service delivers).
		void Deliver() { World.World.Tick(Now()); }

		FElysiumEntity* Find(const TCHAR* Name) { return World.World.FindByName(Name); }

		template <class T>
		T* GuardAs() { return Guard != nullptr ? Guard->AsSpecies<T>() : nullptr; }
	};

	FElysiumNpcBase::FElysiumTakeDamageInfo Damage19Packet(const FElysiumEntity* Attacker, float Damage, uint32 Bits = 0)
	{
		FElysiumNpcBase::FElysiumTakeDamageInfo Info;
		Info.Attacker = Attacker != nullptr ? Attacker->Handle : FElysiumEntityHandle::Invalid();
		Info.Damage = Damage;
		Info.DamageBits = Bits;
		return Info;
	}

	int32 Damage19StubCount(const TCHAR* Surface)
	{
		TArray<ElysiumStub::FTally> Tally;
		ElysiumStub::CollectTally(Tally);
		for (const ElysiumStub::FTally& Row : Tally)
		{
			if (Row.Surface == Surface)
			{
				return Row.Count;
			}
		}
		return 0;
	}

	int32 Damage19SoundCount(FElysiumEntityWorld& World, const FElysiumEntityHandle& Source)
	{
		int32 Count = 0;
		for (const FElysiumGameSoundEvent& Event : World.GameSounds().Retained())
		{
			if (Event.Source == Source && Event.Category == ElysiumGameSounds::NpcTakeDamage())
			{
				++Count;
			}
		}
		return Count;
	}

	int32 Damage19Roll1To100Preview()
	{
		FRandomStream Probe = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
		return Probe.RandRange(1, 100);
	}
}

// =================================================================================================
// 0x10265e90 — `CAI_BaseNPC::OnTakeDamage`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19BaseOnTakeDamageTest,
	"Elysium.Arm.NpcKernelDamage19.BaseOnTakeDamage_10265e90", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19BaseOnTakeDamageTest::RunTest(const FString&)
{
	FDamage19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	N.LastTakeDamageInfo.Damage = -1.f;
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Damage19Packet(F.Other, 10.f);
	const int32 Result = N.FElysiumNpcBase::OnTakeDamage(&Info);
	// `0x1032ef60`'s alive arm dispatches slot 390; the Troika's `0x102beda0` caches the packet
	// (`0x102bedab`), which is the witness that the chain ran.
	TestEqual(TEXT("10265e99 CBaseCombatCharacter::OnTakeDamage runs first (its slot 390 cached the packet)"),
		N.LastTakeDamageInfo.Damage, 10.f);
	TestEqual(TEXT("10265eae returns what 0x1032ef60 answered (slot 390's 1, 0x1032f0bf)"), Result, 1);
	// 10265ea4 slot 459 runs unconditionally; outside NPC_STATE_SCRIPT 0x1026d7f0 writes nothing.
	TestTrue(TEXT("10265ea4 RemoveIgnoredConditions outside SCRIPT leaves conditions alone"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));
	// `DAMAGE_NO`: `0x1032ef60` answers 0 at its first test, and this body returns that 0.
	N.TakeDamageMode = 0;
	N.LastTakeDamageInfo.Damage = -1.f;
	TestEqual(TEXT("1032efa9 m_takedamage == DAMAGE_NO answers 0"), N.FElysiumNpcBase::OnTakeDamage(&Info), 0);
	TestEqual(TEXT("...and no slot 390 ran"), N.LastTakeDamageInfo.Damage, -1.f);
	return true;
}

// =================================================================================================
// 0x10265ed0 — `CAI_BaseNPC::OnTakeDamage_Alive`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19BaseAliveOutputsTest,
	"Elysium.Arm.NpcKernelDamage19.BaseOnTakeDamageAlive_10265ed0_Outputs", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19BaseAliveOutputsTest::RunTest(const FString&)
{
	FDamage19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("attacker"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.BaseScheduleHost.MemoryBits = 0x3u;
	N.Health = 100;
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Damage19Packet(F.Other, 5.f);
	TestEqual(TEXT("10266364 a full pass answers 1"), N.FElysiumNpcBase::OnTakeDamage_Alive(&Info), 1);
	TestEqual(TEXT("10265eea m_afMemory &= ~0x2 (INCOVER)"), N.BaseScheduleHost.MemoryBits, 0x1u);
	TestEqual(TEXT("10266310 m_flLastDamageTime = curtime"), N.BaseMemory.RepeatedDamageWindowStart, F.Now());
	// A second hit on the same tick: the stamp now equals curtime, so `0x10265f1a` skips m_OnDamaged.
	N.FElysiumNpcBase::OnTakeDamage_Alive(&Info);
	F.Deliver();
	TestEqual(TEXT("10265f26 m_OnDamaged fires once per tick"), F.World.Counter(TEXT("damaged")), 1.f);
	TestEqual(TEXT("10265f3e health above half: no m_OnHalfHealth"), F.World.Counter(TEXT("half")), 0.f);
	// Half health: `m_iHealth <= m_iMaxHealth / 2` (integer division).
	N.Health = 50;
	N.FElysiumNpcBase::OnTakeDamage_Alive(&Info);
	F.Deliver();
	TestEqual(TEXT("10265f4a m_OnHalfHealth at exactly half"), F.World.Counter(TEXT("half")), 1.f);
	TestEqual(TEXT("...and m_OnDamaged still gated on this tick"), F.World.Counter(TEXT("damaged")), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19BaseAliveConditionsTest,
	"Elysium.Arm.NpcKernelDamage19.BaseOnTakeDamageAlive_10265ed0_Conditions", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19BaseAliveConditionsTest::RunTest(const FString&)
{
	FDamage19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("attacker"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.Cognition.Conditions.Reset();
	// 20 is light and NOT heavy (0x10266660: `20.0 < damage`, strict).
	FElysiumNpcBase::FElysiumTakeDamageInfo Twenty = Damage19Packet(F.Other, 20.f);
	N.FElysiumNpcBase::OnTakeDamage_Alive(&Twenty);
	TestTrue(TEXT("10266239 LIGHT_DAMAGE on 20"), N.Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));
	TestFalse(TEXT("10266282 20 is not HEAVY_DAMAGE"), N.Cognition.Conditions.Has(EElysiumNpcCond::HeavyDamage));
	TestFalse(TEXT("1026631d 20 of 100 is not REPEATED_DAMAGE"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::RepeatedDamage));
	TestEqual(TEXT("102662ec the first hit RESETS the sum"), N.BaseMemory.RepeatedDamageAccumulated, 20.f);
	TestTrue(TEXT("102661de m_bCondTookDamage"), N.Cognition.bCondTookDamage);
	TestTrue(TEXT("102661cc m_hLastDamageEnt = the attacker"), N.BaseMemory.LastDamageAttacker == F.Other->Handle);
	// 21 on the same tick: heavy, and the sum 41 > 100 * 0.3.
	FElysiumNpcBase::FElysiumTakeDamageInfo TwentyOne = Damage19Packet(F.Other, 21.f);
	N.FElysiumNpcBase::OnTakeDamage_Alive(&TwentyOne);
	TestTrue(TEXT("10266293 HEAVY_DAMAGE on 21"), N.Cognition.Conditions.Has(EElysiumNpcCond::HeavyDamage));
	TestEqual(TEXT("102662c6 inside 1.0 s the sum ACCUMULATES"), N.BaseMemory.RepeatedDamageAccumulated, 41.f);
	TestTrue(TEXT("1026632e REPEATED_DAMAGE when the sum exceeds 30 % of m_iMaxHealth"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::RepeatedDamage));
	// A full second later the sum resets to the new hit.
	F.World.Advance(F.Now() + 1.0);
	N.Cognition.Conditions.Reset();
	FElysiumNpcBase::FElysiumTakeDamageInfo Three = Damage19Packet(F.Other, 3.f);
	N.FElysiumNpcBase::OnTakeDamage_Alive(&Three);
	TestEqual(TEXT("102662b3 curtime - m_flLastDamageTime >= 1.0 resets"), N.BaseMemory.RepeatedDamageAccumulated, 3.f);
	TestFalse(TEXT("...and 3 of 100 raises no REPEATED_DAMAGE"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::RepeatedDamage));
	// Zero damage is not light (0x10266630: `0.0 < damage`).
	N.Cognition.Conditions.Reset();
	FElysiumNpcBase::FElysiumTakeDamageInfo Zero = Damage19Packet(F.Other, 0.f);
	N.FElysiumNpcBase::OnTakeDamage_Alive(&Zero);
	TestFalse(TEXT("10266228 zero damage is not LIGHT_DAMAGE in the base"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19BaseAliveGatesTest,
	"Elysium.Arm.NpcKernelDamage19.BaseOnTakeDamageAlive_10265ed0_Gates", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19BaseAliveGatesTest::RunTest(const FString&)
{
	FDamage19Fixture F;
	FElysiumEntity* Boom = F.Find(TEXT("boom"));
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("a non-NPC entity"), Boom))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	const int32 SoundsBefore = Damage19SoundCount(F.World.World, N.Handle);

	// No attacker at +0x2c: 1, and nothing below — no record, no sound.
	N.Cognition.bCondTookDamage = false;
	FElysiumNpcBase::FElysiumTakeDamageInfo NoAttacker = Damage19Packet(nullptr, 30.f);
	TestEqual(TEXT("10265f64 no attacker answers 1"), N.FElysiumNpcBase::OnTakeDamage_Alive(&NoAttacker), 1);
	TestFalse(TEXT("...with m_bCondTookDamage untouched"), N.Cognition.bCondTookDamage);
	TestEqual(TEXT("...and no InsertSound (10265f64 jumps past 0x10266333)"),
		Damage19SoundCount(F.World.World, N.Handle), SoundsBefore);

	// An attacker with neither FL_CLIENT nor FL_NPC skips to the sound tail.
	FElysiumNpcBase::FElysiumTakeDamageInfo Crate = Damage19Packet(Boom, 30.f);
	TestEqual(TEXT("10265f74 a non-NPC attacker still answers 1"), N.FElysiumNpcBase::OnTakeDamage_Alive(&Crate), 1);
	TestFalse(TEXT("...skipping m_bCondTookDamage"), N.Cognition.bCondTookDamage);
	TestFalse(TEXT("...and every condition"), N.Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));
	TestEqual(TEXT("1026635b the tail still inserts the combat sound"),
		Damage19SoundCount(F.World.World, N.Handle), SoundsBefore + 1);

	// A null packet (named crash guard).
	TestEqual(TEXT("crash guard: a null packet answers 0"), N.FElysiumNpcBase::OnTakeDamage_Alive(nullptr), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19BaseAliveMemoryTest,
	"Elysium.Arm.NpcKernelDamage19.BaseOnTakeDamageAlive_10265ed0_Memory", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19BaseAliveMemoryTest::RunTest(const FString&)
{
	FDamage19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("attacker"), F.Other)
		|| !TestNotNull(TEXT("enemy"), F.Enemy))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	// The file-static death-throw direction the null-inflictor arm scales by 64.
	FElysiumNpcBase::DeathThrowImpulse() = FVector(1.f, 0.f, 0.f);
	const FVector Expected = N.GetAbsOrigin() + FVector(64.f * ElysiumMove::U, 0.f, 0.f);
	const bool bSeen = N.FInViewCone(F.Other) && N.FVisible(F.Other, 0x2804091, nullptr, 0);

	// Arm A: a live enemy, an unknown attacker, no SEE_ENEMY -> the ENEMY's memory takes the position.
	ElysiumNpcEnemy::SetEnemy(N, F.Enemy->Handle);
	N.Cognition.Conditions.Reset();
	const int32 MotorBefore = N.OnTakeDamageAliveMotorResets;
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Damage19Packet(F.Other, 4.f);
	N.FElysiumNpcBase::OnTakeDamage_Alive(&Info);
	TestTrue(TEXT("10265fde/1026606e no inflictor: origin + death-throw * 64"),
		N.BaseMemory.LastDamageAttackPosition.Equals(Expected, 0.01f));
	TestEqual(TEXT("10266186 the motor hold with a live enemy"), N.OnTakeDamageAliveMotorResets, MotorBefore + 1);
	TestEqual(TEXT("102661b9 the face-to-last-known call"), N.OnTakeDamageAliveMotorFaceCalls, MotorBefore + 1);
	const FElysiumNpcEnemyMemoryRecord* EnemyRecord = N.EnemyMemory.Find(F.Enemy->Handle);
	if (!bSeen)
	{
		TestTrue(TEXT("10266135 unseen: the CURRENT ENEMY's memory is refreshed at the attack position"),
			EnemyRecord != nullptr && EnemyRecord->LastPosition.Equals(Expected, 0.01f));
		TestNull(TEXT("...and the unknown attacker gets no record"), N.EnemyMemory.Find(F.Other->Handle));
	}
	else
	{
		TestNull(TEXT("10265fd9 seen: no memory call at all"), N.EnemyMemory.Find(F.Other->Handle));
	}

	// Arm B: SEE_ENEMY set -> slot 544 with NULL for an unknown attacker.
	N.Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
	N.FElysiumNpcBase::OnTakeDamage_Alive(&Info);
	TestNull(TEXT("1026616a SEE_ENEMY: an unknown attacker is passed as NULL"), N.EnemyMemory.Find(F.Other->Handle));

	// Arm C: an attacker already known -> slot 544 with the attacker.
	const FVector Seed = FVector(1.f, 2.f, 3.f);
	N.UpdateEnemyMemory(F.Other, Seed, nullptr);
	N.FElysiumNpcBase::OnTakeDamage_Alive(&Info);
	const FElysiumNpcEnemyMemoryRecord* AttackerRecord = N.EnemyMemory.Find(F.Other->Handle);
	if (!bSeen)
	{
		TestTrue(TEXT("10266164 a known attacker's record moves to the attack position"),
			AttackerRecord != nullptr && AttackerRecord->LastPosition.Equals(Expected, 0.01f));
	}
	else
	{
		TestTrue(TEXT("seen: the known attacker's record is left where it was"),
			AttackerRecord != nullptr && AttackerRecord->LastPosition.Equals(Seed, 0.01f));
	}

	// No enemy: no motor pair.
	ElysiumNpcEnemy::SetEnemy(N, FElysiumEntityHandle::Invalid());
	const int32 MotorNoEnemy = N.OnTakeDamageAliveMotorResets;
	N.FElysiumNpcBase::OnTakeDamage_Alive(&Info);
	TestEqual(TEXT("1026617e no enemy: no motor pair"), N.OnTakeDamageAliveMotorResets, MotorNoEnemy);
	return true;
}

// =================================================================================================
// 0x102bed30 — `CAI_BaseNPCTroika::OnTakeDamage` (corpus `CNPC_VVampire::OnTakeDamage`).
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19TroikaOnTakeDamageTest,
	"Elysium.Arm.NpcKernelDamage19.TroikaOnTakeDamage_102bed30", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19TroikaOnTakeDamageTest::RunTest(const FString&)
{
	FDamage19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Damage19Packet(F.Other, 10.f);

	// Not invincible: tail to CAI_BaseNPC::OnTakeDamage, whose answer is returned.
	N.LastTakeDamageInfo.Damage = -1.f;
	TestEqual(TEXT("102bed6d not invincible: the base's answer (0x1032ef60's slot-390 1)"), N.OnTakeDamage(&Info), 1);
	TestEqual(TEXT("...through 0x10265e90 -> 0x1032ef60 -> slot 390 (the packet cached, 0x102bedab)"),
		N.LastTakeDamageInfo.Damage, 10.f);
	F.Deliver();
	TestEqual(TEXT("...whose 0x10265ed0 fires m_OnDamaged (0x10265f26)"), F.World.Counter(TEXT("damaged")), 1.f);

	// Invincible: refused with 0, but m_OnDamaged fires at most once a tick and the stamp is written.
	// A later tick, so the stamp `0x10266310` just wrote differs from curtime.
	F.World.Advance(F.Now() + 1.0);
	N.LastTakeDamageInfo.Damage = -1.f;
	N.bInvincible = true;
	TestEqual(TEXT("102bed68 invincible answers 0"), N.OnTakeDamage(&Info), 0);
	TestEqual(TEXT("102bed56 m_flLastDamageTime = curtime"), N.BaseMemory.RepeatedDamageWindowStart, F.Now());
	N.OnTakeDamage(&Info);
	F.Deliver();
	TestEqual(TEXT("102bed63 m_OnDamaged once per tick however many packets land"),
		F.World.Counter(TEXT("damaged")), 2.f);
	TestEqual(TEXT("...and the chain never ran"), N.LastTakeDamageInfo.Damage, -1.f);
	return true;
}

// =================================================================================================
// 0x102beda0 — `CAI_BaseNPCTroika::OnTakeDamage_Alive` (slot 390).
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19TroikaAliveZeroTest,
	"Elysium.Arm.NpcKernelDamage19.TroikaOnTakeDamageAlive_102beda0_ZeroDamage", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19TroikaAliveZeroTest::RunTest(const FString&)
{
	FDamage19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("attacker"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.Cognition.Conditions.Reset();
	N.Cognition.bCondTookDamage = false;
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Damage19Packet(F.Other, 0.f, 0x80u);
	Info.AmmoType = 7;
	const int32 Result = N.OnTakeDamage_Alive(&Info);
	TestEqual(TEXT("102befb4 returns the base's answer"), Result, 1);
	TestEqual(TEXT("102bedab the packet is cached verbatim (+0x30)"), N.LastTakeDamageInfo.Damage, 0.f);
	TestEqual(TEXT("...(+0x38)"), N.LastTakeDamageInfo.DamageBits, 0x80u);
	TestEqual(TEXT("...(+0x40)"), N.LastTakeDamageInfo.AmmoType, 7);
	TestTrue(TEXT("102bef5c zero damage still raises LIGHT_DAMAGE"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));
	TestTrue(TEXT("102bef6f m_hLastDamageEnt = the attacker"), N.BaseMemory.LastDamageAttacker == F.Other->Handle);
	TestTrue(TEXT("102bef81 m_bCondTookDamage"), N.Cognition.bCondTookDamage);
	TestEqual(TEXT("102bee8e no expression on a zero hit"), N.AddExpressionForEventCalls, 0);
	TestTrue(TEXT("102bef97 the tail extends the FVisible override by 5 s (no enemy)"),
		FMath::IsNearlyEqual(N.Senses.Memory.StealthVisionOverrideUntil, F.Now() + 5.0, 1e-3));
	TestEqual(TEXT("crash guard: a null packet answers 0"), N.OnTakeDamage_Alive(nullptr), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19TroikaAlivePositiveTest,
	"Elysium.Arm.NpcKernelDamage19.TroikaOnTakeDamageAlive_102beda0_Positive", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19TroikaAlivePositiveTest::RunTest(const FString&)
{
	FDamage19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("attacker"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Damage19Packet(F.Other, 6.f);
	N.Senses.Memory.StealthVisionOverrideUntil = 0.0;
	N.OnTakeDamage_Alive(&Info);
	TestEqual(TEXT("102bee8e AddExpressionForEvent(0) on a positive hit"), N.AddExpressionForEventCalls, 1);
	TestEqual(TEXT("...event 0"), N.LastExpressionEvent, 0);
	TestTrue(TEXT("102bef88 no place, no ONE_HIT_KILL: the tail runs"),
		FMath::IsNearlyEqual(N.Senses.Memory.StealthVisionOverrideUntil, F.Now() + 5.0, 1e-3));

	// ONE_HIT_KILL (0x40000000): slot 144 and return, WITHOUT the tail.
	N.Senses.Memory.StealthVisionOverrideUntil = 0.0;
	N.NpcFlags.Set(EElysiumNpcFlag::ONE_HIT_KILL);
	N.OnTakeDamage_Alive(&Info);
	TestEqual(TEXT("102bee9d the expression still came first"), N.AddExpressionForEventCalls, 2);
	TestEqual(TEXT("102beeaf ONE_HIT_KILL returns before the tail"), N.Senses.Memory.StealthVisionOverrideUntil, 0.0);
	return true;
}

// =================================================================================================
// 0x1029fa50 — `CAI_BaseNPCTroika` slot 316, the melee-reaction hook.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19Slot316Test,
	"Elysium.Arm.NpcKernelDamage19.Slot316_1029fa50", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19Slot316Test::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CAI_BaseNPCTroika"), /*bArm=*/true);
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard) || !TestNotNull(TEXT("attacker"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;

	N.Cognition.Conditions.Reset();
	N.Slot316(nullptr, true, true);
	TestTrue(TEXT("1029fa63 BEING_ATTACKED before the null-target test"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::BeingAttacked));
	TestFalse(TEXT("1029fa6e a null target stops there"), N.Cognition.Conditions.Has(EElysiumNpcCond::ShouldDodge));

	// In range: 50 Source units, inside the 100-unit pad (0x1049e048).
	F.Other->Origin = N.GetAbsOrigin() + FVector(50.f * ElysiumMove::U, 0.f, 0.f);
	TestTrue(TEXT("10345760 50 units is within 100 + reach"), FElysiumNpc::MeleeSwingInRange(*F.Other, N.GetAbsOrigin()));
	const bool bMelee = FElysiumNpc::HoldingMeleeWeapon(N);
	const bool bInterrupt = N.OkToInterruptForMelee();
	const int32 SchedulesBefore = N.SetScheduleRetailCalls;
	N.Cognition.Conditions.Reset();
	N.Slot316(F.Other, true, true);
	TestEqual(TEXT("1029faba SHOULD_DODGE with range, a melee weapon and slot 325"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::ShouldDodge), bMelee && N.Slot325());
	TestEqual(TEXT("1029fb1f SHOULD_BLOCK with a melee weapon and slot 324"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::ShouldBlock), bMelee && N.Slot324());
	if (bMelee && N.Slot325() && bInterrupt)
	{
		TestEqual(TEXT("1029faea the dodge program 0xd5"), N.LastSetScheduleRetail, 0xd5);
		TestEqual(TEXT("...installed once"), N.SetScheduleRetailCalls, SchedulesBefore + 1);
	}
	else
	{
		TestEqual(TEXT("1029facb no program without slot 590"), N.SetScheduleRetailCalls, SchedulesBefore);
	}

	// Out of range: no dodge, the block arm has no range test.
	F.Other->Origin = N.GetAbsOrigin() + FVector(400.f * ElysiumMove::U, 0.f, 0.f);
	TestFalse(TEXT("10345837 400 units is out of range"), FElysiumNpc::MeleeSwingInRange(*F.Other, N.GetAbsOrigin()));
	N.Cognition.Conditions.Reset();
	N.Slot316(F.Other, true, true);
	TestFalse(TEXT("1029fa90 out of range: no SHOULD_DODGE"), N.Cognition.Conditions.Has(EElysiumNpcCond::ShouldDodge));
	TestEqual(TEXT("1029faf5 the block arm needs no range"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::ShouldBlock), bMelee && N.Slot324());

	// The two flags gate their own arms only.
	F.Other->Origin = N.GetAbsOrigin() + FVector(50.f * ElysiumMove::U, 0.f, 0.f);
	N.Cognition.Conditions.Reset();
	N.Slot316(F.Other, false, false);
	TestFalse(TEXT("1029fa7a bCheckDodge false: no SHOULD_DODGE"), N.Cognition.Conditions.Has(EElysiumNpcCond::ShouldDodge));
	TestFalse(TEXT("1029faf5 bCheckBlock false: no SHOULD_BLOCK"), N.Cognition.Conditions.Has(EElysiumNpcCond::ShouldBlock));
	TestTrue(TEXT("...BEING_ATTACKED regardless"), N.Cognition.Conditions.Has(EElysiumNpcCond::BeingAttacked));
	return true;
}

// =================================================================================================
// 0x1029fcf0 — `CAI_BaseNPCTroika::PlayerDefenderBlockReaction` (slot 318).
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19Slot318Test,
	"Elysium.Arm.NpcKernelDamage19.PlayerDefenderBlockReaction_1029fcf0", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19Slot318Test::RunTest(const FString&)
{
	FDamage19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	const int32 Before = N.SetScheduleRetailCalls;
	N.NextAttackTime = -1.0;
	TestFalse(TEXT("1029fcfa no roll record answers false"), N.PlayerDefenderBlockReaction(F.Other, nullptr, nullptr));
	TestEqual(TEXT("...with no program"), N.SetScheduleRetailCalls, Before);
	TestEqual(TEXT("...and m_flNextAttack untouched"), N.NextAttackTime, -1.0);

	FElysiumMeleeRoll Roll;
	Roll.Lethality = 5;
	Roll.Defense = 1;
	Roll.Soak = 1;
	const int32 Class = N.DefenderBlockReactionClass(Roll);
	const bool bOk = N.OkToDisturb();
	const bool bAnswer = N.PlayerDefenderBlockReaction(F.Other, &Roll, nullptr);
	TestEqual(TEXT("1029fd0c/1029fd6d the answer is OkToDisturb's"), bAnswer, bOk);
	if (bOk)
	{
		TestEqual(TEXT("1029fd2b class 3 takes 0xd8, any other 0xd7"), N.LastSetScheduleRetail,
			Class == 3 ? 0xd8 : 0xd7);
		TestFalse(TEXT("1029fd4d installed unforced"), N.bLastSetScheduleForce);
		TestTrue(TEXT("1029fd6f m_flNextAttack = curtime + 0.3 (the folded lerp's first cell)"),
			FMath::IsNearlyEqual(N.NextAttackTime, F.Now() + 0.3, 1e-4));
	}
	// Headless there is no `rules.txt`: the classifier is Unclassified, which is no retail band.
	TestEqual(TEXT("103498b0 without a Melee_Reactions table the class is -1 (not 3)"), Class, -1);

	// Integration (story 8 L09): with a stated `Melee_Reactions` table the class-3 arm is reached.
	// `0x103498b0` walks `_DAT_10739fa0..fac` (DodgeAttack -3, Dodge -2, Block -1, BlockStagger 0
	// in the melee fixture) with `<=`; margin 0 is class 3 and takes 0xd8 (`0x1029fd3e`/`0x1029fd48`),
	// margin -1 is class 2 and takes 0xd7 (`0x1029fd2d`/`0x1029fd37`).
	{
		const ElysiumMeleeTest::FRulesFixture Rules;
		FElysiumMeleeRoll Stagger;
		Stagger.Lethality = 2;
		Stagger.Defense = 1;
		Stagger.Soak = 1;   // margin 0
		TestEqual(TEXT("103498b0 margin 0 is class 3"), N.DefenderBlockReactionClass(Stagger), 3);
		FElysiumMeleeRoll Block = Stagger;
		Block.Soak = 2;     // margin -1
		TestEqual(TEXT("103498b0 margin -1 is class 2"), N.DefenderBlockReactionClass(Block), 2);
		if (N.OkToDisturb())
		{
			TestTrue(TEXT("1029fd6d class 3 answers true"), N.PlayerDefenderBlockReaction(F.Other, &Stagger, nullptr));
			TestEqual(TEXT("1029fd48 class 3 installs 0xd8"), N.LastSetScheduleRetail, 0xd8);
			TestTrue(TEXT("1029fd6d class 2 answers true"), N.PlayerDefenderBlockReaction(F.Other, &Block, nullptr));
			TestEqual(TEXT("1029fd37 class 2 installs 0xd7"), N.LastSetScheduleRetail, 0xd7);
		}
	}
	return true;
}

// =================================================================================================
// 0x102a01b0 — `CAI_BaseNPCTroika::PlayerKnockbackReaction` (slot 320).
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19Slot320Test,
	"Elysium.Arm.NpcKernelDamage19.PlayerKnockbackReaction_102a01b0", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19Slot320Test::RunTest(const FString&)
{
	FDamage19Fixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	TestFalse(TEXT("10344da0 0x8a is outside the push window"), FElysiumNpc::IsKnockbackPushActivity(0x8a));
	TestTrue(TEXT("10344da0 0x8b is inside"), FElysiumNpc::IsKnockbackPushActivity(0x8b));
	TestTrue(TEXT("10344da0 0x93 is inside"), FElysiumNpc::IsKnockbackPushActivity(0x93));
	TestFalse(TEXT("10344da0 0x94 is outside"), FElysiumNpc::IsKnockbackPushActivity(0x94));

	// In dialogue: refused, nothing written.
	N.KnockbackType = 77;
	const int32 Before = N.SetScheduleRetailCalls;
	N.Dialogue.bInDialog = true;
	TestFalse(TEXT("102a01ca IsInDialog refuses"), N.PlayerKnockbackReaction(F.Other, 0x8b));
	TestEqual(TEXT("...m_knockbackType untouched"), N.KnockbackType, 77);
	N.Dialogue.bInDialog = false;

	// `SelectHeaviestSequence` answers < 0 headless (the kernel's seam): refused, nothing written.
	int32 WeaponActivity = 0;
	const bool bSequence = N.SelectHeaviestSequence(N.TranslateActivityNumber(0x8b, WeaponActivity), -1) >= 0;
	const bool bAnswer = N.PlayerKnockbackReaction(F.Other, 0x8b);
	if (!bSequence)
	{
		TestFalse(TEXT("102a01ea no sequence: false"), bAnswer);
		TestEqual(TEXT("...m_knockbackType untouched"), N.KnockbackType, 77);
		TestEqual(TEXT("...no program"), N.SetScheduleRetailCalls, Before);
	}
	else
	{
		TestTrue(TEXT("102a0238 a push activity answers true"), bAnswer);
		TestEqual(TEXT("102a01f0 m_knockbackType = activity"), N.KnockbackType, 0x8b);
		TestEqual(TEXT("102a0232 program 0x14d"), N.LastSetScheduleRetail, 0x14d);
	}
	return true;
}

// =================================================================================================
// Slot 320 species — 0x10378d30 Gargoyle, 0x10380320 Hengeyokai, 0x103c43f0 TzimisceRunner.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19GargoyleKnockbackTest,
	"Elysium.Arm.NpcKernelDamage19.GargoylePlayerKnockbackReaction_10378d30", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19GargoyleKnockbackTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VGargoyle"));
	FElysiumNpcGargoyle* G = F.GuardAs<FElysiumNpcGargoyle>();
	if (!TestNotNull(TEXT("the gargoyle constructs"), G))
	{
		return false;
	}
	G->GargoyleCanKnockback = 0;
	TestFalse(TEXT("10378d3b m_iCanKnockback 0 refuses before the roll"), G->PlayerKnockbackReaction(F.Other, 0x8b));
	G->GargoyleCanKnockback = 1;
	for (int32 Pass = 0; Pass < 6; ++Pass)
	{
		const int32 Roll = Damage19Roll1To100Preview();
		TestEqual(TEXT("10378d4f a roll of 1..50 answers true, 51..100 false"),
			G->PlayerKnockbackReaction(F.Other, 0x8b), Roll <= 0x32);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19HengeyokaiKnockbackTest,
	"Elysium.Arm.NpcKernelDamage19.HengeyokaiPlayerKnockbackReaction_10380320", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19HengeyokaiKnockbackTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VHengeyokai"));
	FElysiumNpcHengeyokai* H = F.GuardAs<FElysiumNpcHengeyokai>();
	if (!TestNotNull(TEXT("the hengeyokai constructs"), H))
	{
		return false;
	}
	for (int32 Pass = 0; Pass < 6; ++Pass)
	{
		const int32 Roll = Damage19Roll1To100Preview();
		TestEqual(TEXT("10380335 ungated: a roll of 1..50 answers true"),
			H->PlayerKnockbackReaction(F.Other, 0x8b), Roll <= 0x32);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19RunnerKnockbackTest,
	"Elysium.Arm.NpcKernelDamage19.TzimisceRunnerPlayerKnockbackReaction_103c43f0", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19RunnerKnockbackTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VTzimisceRunner"));
	FElysiumNpcTzimisceRunner* R = F.GuardAs<FElysiumNpcTzimisceRunner>();
	if (!TestNotNull(TEXT("the runner constructs"), R))
	{
		return false;
	}
	int32 WeaponActivity = 0;
	const bool bSequence = R->SelectHeaviestSequence(R->TranslateActivityNumber(0x79, WeaponActivity), -1) >= 0;
	R->KnockbackType = 5;
	const bool bAnswer = R->PlayerKnockbackReaction(F.Other, 0x8b);
	TestEqual(TEXT("103c43f8 the Troika body's answer, tail-returned"), bAnswer, bSequence && R->OkToDisturb());
	if (bAnswer)
	{
		TestEqual(TEXT("103c43f0 the caller's activity is REPLACED by 0x79"), R->KnockbackType, 0x79);
	}
	else
	{
		TestEqual(TEXT("...refused: nothing written"), R->KnockbackType, 5);
	}
	return true;
}

// =================================================================================================
// Slot 313 species — 0x1037a5b0 Gargoyle, 0x10381b10 Hengeyokai, 0x103ab270 SabbatLeader.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19PresenceTest,
	"Elysium.Arm.NpcKernelDamage19.UpdatePresenceEffect_1037a5b0_10381b10_103ab270", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19PresenceTest::RunTest(const FString&)
{
	for (const TCHAR* Class : { TEXT("CNPC_VGargoyle"), TEXT("CNPC_VHengeyokai"), TEXT("CNPC_VSabbatLeader") })
	{
		FDamage19Fixture F(Class);
		if (!TestNotNull(TEXT("the species constructs"), F.Guard))
		{
			return false;
		}
		FElysiumNpc& N = *F.Guard;
		N.FriendPresenceEffect = 3;
		N.EnemyPresenceEffect = 4;
		N.EnemyPresencePercent = 0.5f;
		N.UpdatePresenceEffect();
		TestEqual(FString::Printf(TEXT("%s 0x101e3ff0 the presence effect is dismissed first"), Class),
			N.DismissPresenceEffectCalls, 1);
		TestEqual(FString::Printf(TEXT("%s +0xe80 m_iFriendPresenceEffect = 0"), Class), N.FriendPresenceEffect, 0);
		TestEqual(FString::Printf(TEXT("%s +0xe88 m_iEnemyPresenceEffect = 0"), Class), N.EnemyPresenceEffect, 0);
		TestEqual(FString::Printf(TEXT("%s +0xe84 m_flEnemeyPresencePercent = 0"), Class), N.EnemyPresencePercent, 0.f);
	}
	return true;
}

// =================================================================================================
// Slot 390 species.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19AndreiTest,
	"Elysium.Arm.NpcKernelDamage19.AndreiBloodOnTakeDamageAlive_1035e6d0", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19AndreiTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VAndreiBlood"));
	FElysiumNpcAndreiBlood* A = F.GuardAs<FElysiumNpcAndreiBlood>();
	if (!TestNotNull(TEXT("Andrei's blood constructs"), A))
	{
		return false;
	}
	// Not activated: ALWAYS neutered, the counter never moves — and the shared descriptor too.
	FElysiumDmg Dmg;
	Dmg.BaseDamage = 7;
	Dmg.ExtraInput = 3;
	Dmg.AppliedDamage = 0;
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Damage19Packet(nullptr, 9.f);
	Info.Dmg = &Dmg;
	A->bAndreiActivated = false;
	A->OnTakeDamage_Alive(&Info);
	TestEqual(TEXT("1035e82a not activated: no hit counted"), A->AndreiHitCounter, 0);
	TestEqual(TEXT("1035e859 the copy's +0x30 zeroed"), A->LastTakeDamageInfo.Damage, 0.f);
	TestEqual(TEXT("1035e865 m_iDiceAmt zeroed on the shared descriptor"), Dmg.BaseDamage, 0);
	TestEqual(TEXT("1035e870 m_iToHitSuccesses zeroed"), Dmg.ExtraInput, 0);
	TestEqual(TEXT("...the caller's own scalar is untouched"), Info.Damage, 9.f);

	// Activated, cap 20 > wounds 0 + 5: forwarded UNTOUCHED.
	A->bAndreiActivated = true;
	FElysiumNpcBase::FElysiumTakeDamageInfo Small = Damage19Packet(nullptr, 5.f);
	A->OnTakeDamage_Alive(&Small);
	TestEqual(TEXT("1035e83f m_iHitCounter increments"), A->AndreiHitCounter, 1);
	TestFalse(TEXT("1035e84c cap > wounds + amount: not dead"), A->bAndreiDead);
	TestEqual(TEXT("1035e877 the copy goes on untouched"), A->LastTakeDamageInfo.Damage, 5.f);

	// Activated, cap 20 <= 0 + 25: m_bDead and neutered.
	FElysiumNpcBase::FElysiumTakeDamageInfo Big = Damage19Packet(nullptr, 25.f);
	A->OnTakeDamage_Alive(&Big);
	TestEqual(TEXT("...the counter moves again"), A->AndreiHitCounter, 2);
	TestTrue(TEXT("1035e84e m_bDead on the killing blow"), A->bAndreiDead);
	TestEqual(TEXT("...and the killing blow is neutered"), A->LastTakeDamageInfo.Damage, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19AnimalTest,
	"Elysium.Arm.NpcKernelDamage19.AnimalOnTakeDamageAlive_103601a0", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19AnimalTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VAnimal"));
	FElysiumNpcAnimal* A = F.GuardAs<FElysiumNpcAnimal>();
	if (!TestNotNull(TEXT("the animal constructs"), A) || !TestNotNull(TEXT("the player"), F.Player))
	{
		return false;
	}
	FElysiumNpcBase::FElysiumTakeDamageInfo FromNpc = Damage19Packet(F.Other, 2.f);
	A->OnTakeDamage_Alive(&FromNpc);
	TestFalse(TEXT("103601b9 an NPC attacker latches nothing"), A->bPlayerAttackedMe);
	TestEqual(TEXT("103601c5 the packet goes on unchanged"), A->LastTakeDamageInfo.Damage, 2.f);
	FElysiumNpcBase::FElysiumTakeDamageInfo FromPlayer = Damage19Packet(F.Player, 3.f);
	A->OnTakeDamage_Alive(&FromPlayer);
	TestTrue(TEXT("103601bb a player attacker LATCHES m_bPlayerAttackedMe"), A->bPlayerAttackedMe);
	A->OnTakeDamage_Alive(&FromNpc);
	TestTrue(TEXT("...and nothing here clears it"), A->bPlayerAttackedMe);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19BachTest,
	"Elysium.Arm.NpcKernelDamage19.BachOnTakeDamageAlive_10363c70", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19BachTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VBach"));
	FElysiumNpcBach* B = F.GuardAs<FElysiumNpcBach>();
	if (!TestNotNull(TEXT("Bach constructs"), B))
	{
		return false;
	}
	FElysiumNpcBase::FElysiumTakeDamageInfo Bullet = Damage19Packet(nullptr, 8.f, 0x2u);
	B->LastTakeDamageInfo.Damage = -1.f;
	B->bCanFightYet = false;
	B->bBachShieldFlagB = false;
	TestEqual(TEXT("10363c84 m_bCanFightYet (+0x66a8) clear answers 0"), B->OnTakeDamage_Alive(&Bullet), 0);
	TestEqual(TEXT("...refused before the chain"), B->LastTakeDamageInfo.Damage, -1.f);
	TestFalse(TEXT("...and before +0x66a6"), B->bBachShieldFlagB);

	B->bCanFightYet = true;
	B->bBachShieldActive = true;
	B->OnTakeDamage_Alive(&Bullet);
	TestTrue(TEXT("10363ca9 +0x66a6 = 1 on every admitted packet"), B->bBachShieldFlagB);
	TestEqual(TEXT("10363ccc the shield eats a bullet: the copy's +0x30 is 0"), B->LastTakeDamageInfo.Damage, 0.f);
	TestEqual(TEXT("...the caller's packet is untouched"), Bullet.Damage, 8.f);

	B->bBachShieldActive = false;
	B->BachNextShieldTime = 99.0;
	B->OnTakeDamage_Alive(&Bullet);
	TestEqual(TEXT("10363ce9 shield down: m_flNextShieldTime (+0x6688) re-armed"), B->BachNextShieldTime, 0.0);
	TestEqual(TEXT("10363cf2 ...and the packet goes on intact"), B->LastTakeDamageInfo.Damage, 8.f);

	FElysiumNpcBase::FElysiumTakeDamageInfo Club = Damage19Packet(nullptr, 6.f, 0x80u);
	B->BachNextHolyLightTime = 99.0;
	B->BachNextShieldTime = 42.0;
	B->OnTakeDamage_Alive(&Club);
	TestEqual(TEXT("10363cff any other type re-arms the holy light (+0x6690)"), B->BachNextHolyLightTime, 0.0);
	TestEqual(TEXT("...leaving the shield timer"), B->BachNextShieldTime, 42.0);
	TestEqual(TEXT("10363d08 ...intact"), B->LastTakeDamageInfo.Damage, 6.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19GargoyleAliveTest,
	"Elysium.Arm.NpcKernelDamage19.GargoyleOnTakeDamageAlive_10378c10", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19GargoyleAliveTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VGargoyle"));
	FElysiumNpcGargoyle* G = F.GuardAs<FElysiumNpcGargoyle>();
	if (!TestNotNull(TEXT("the gargoyle constructs"), G) || !TestNotNull(TEXT("the player"), F.Player))
	{
		return false;
	}
	G->Senses.Memory.ClosestPlayer = F.Player->Handle;
	FElysiumNpcBase::FElysiumTakeDamageInfo Plain = Damage19Packet(nullptr, 2.f, 0x80u);
	G->GargoyleCanKnockback = 0;
	G->OnTakeDamage_Alive(&Plain);
	TestEqual(TEXT("10378c18 m_iCanKnockback = 1"), G->GargoyleCanKnockback, 1);
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	int32 Priority = 0;
	const bool bRow = G->Relationships.ResolveRow(F.Player->Handle, FString(), Value, Priority);
	TestTrue(TEXT("10378c7a AddEntityRelationship(closest player, D_HT, 10)"),
		bRow && Value == EElysiumRelationship::Hate && Priority == 10);
	TestEqual(TEXT("10378c82 the packet goes on"), G->LastTakeDamageInfo.Damage, 2.f);
	FElysiumNpcBase::FElysiumTakeDamageInfo Veto = Damage19Packet(nullptr, 2.f, 0x4000000u);
	G->OnTakeDamage_Alive(&Veto);
	TestEqual(TEXT("10378c3c bit 0x4000000 clears it again"), G->GargoyleCanKnockback, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19GhoulCroucherTest,
	"Elysium.Arm.NpcKernelDamage19.GhoulCroucherOnTakeDamageAlive_1037bc90", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19GhoulCroucherTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VGhoulCroucher"));
	FElysiumNpcGhoulCroucher* G = F.GuardAs<FElysiumNpcGhoulCroucher>();
	if (!TestNotNull(TEXT("the croucher constructs"), G))
	{
		return false;
	}
	G->bWasDisturbed = false;
	G->bUnawareExited = false;
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Damage19Packet(F.Other, 1.f);
	G->OnTakeDamage_Alive(&Info);
	TestTrue(TEXT("1037bd05 OnDisturbed(attacker) runs"), G->bWasDisturbed);
	TestTrue(TEXT("1037bd0d m_bUnawareExited = 1 after it"), G->bUnawareExited);
	TestEqual(TEXT("1037bd14 the packet goes on"), G->LastTakeDamageInfo.Damage, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19HengeyokaiAliveTest,
	"Elysium.Arm.NpcKernelDamage19.HengeyokaiOnTakeDamageAlive_103801d0", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19HengeyokaiAliveTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VHengeyokai"));
	FElysiumNpcHengeyokai* H = F.GuardAs<FElysiumNpcHengeyokai>();
	FElysiumEntity* Boom = F.Find(TEXT("boom"));
	if (!TestNotNull(TEXT("the hengeyokai constructs"), H) || !TestNotNull(TEXT("point_explosion"), Boom))
	{
		return false;
	}
	// `0x103830e0`'s `FadeToSkin(1)` (`0x10383110`) is the morph's witness: skin 0 -> 1.
	H->Skin = 0;
	FElysiumNpcBase::FElysiumTakeDamageInfo FromNpc = Damage19Packet(F.Other, 2.f);
	TestEqual(TEXT("10380262 returns the chain's answer"), H->OnTakeDamage_Alive(&FromNpc), 1);
	TestEqual(TEXT("10380259 not an explosion: no morph"), H->Skin, 0);
	TestEqual(TEXT("103801da the chain ran FIRST"), H->LastTakeDamageInfo.Damage, 2.f);
	FElysiumNpcBase::FElysiumTakeDamageInfo Blast = Damage19Packet(Boom, 3.f);
	H->OnTakeDamage_Alive(&Blast);
	TestEqual(TEXT("1038025d a point_explosion attacker runs 0x103830e0 (FadeToSkin(1))"), H->Skin, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19ManBatTest,
	"Elysium.Arm.NpcKernelDamage19.ManBatOnTakeDamageAlive_1038e880", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19ManBatTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VManBat"));
	FElysiumNpcManBat* M = F.GuardAs<FElysiumNpcManBat>();
	if (!TestNotNull(TEXT("the man-bat constructs"), M) || !TestNotNull(TEXT("the player"), F.Player))
	{
		return false;
	}
	M->LastTakeDamageInfo.Damage = -1.f;
	M->Health = 24;
	FElysiumNpcBase::FElysiumTakeDamageInfo FromNpc = Damage19Packet(F.Other, 4.f);
	TestEqual(TEXT("1038e8a3 under 25 health a non-player attacker is refused with 1"),
		M->OnTakeDamage_Alive(&FromNpc), 1);
	TestEqual(TEXT("...the chain never ran"), M->LastTakeDamageInfo.Damage, -1.f);
	FElysiumNpcBase::FElysiumTakeDamageInfo FromPlayer = Damage19Packet(F.Player, 5.f);
	M->OnTakeDamage_Alive(&FromPlayer);
	TestEqual(TEXT("1038e89c the player's hit chains"), M->LastTakeDamageInfo.Damage, 5.f);
	M->Health = 25;
	M->OnTakeDamage_Alive(&FromNpc);
	TestEqual(TEXT("1038e88d at 25 anyone's hit chains"), M->LastTakeDamageInfo.Damage, 4.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19MingXiaoTest,
	"Elysium.Arm.NpcKernelDamage19.MingXiaoOnTakeDamageAlive_10395ae0", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19MingXiaoTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VMingXiao"));
	FElysiumNpcMingXiao* M = F.GuardAs<FElysiumNpcMingXiao>();
	if (!TestNotNull(TEXT("Ming Xiao constructs"), M))
	{
		return false;
	}
	M->MingXiaoSeveredTentacleMask = 0;
	// 0x10395650's hitgroup table (no weapon).
	const int32 Table[][2] = { { 1, -2 }, { 4, 1 }, { 5, 0 }, { 6, 5 }, { 7, 4 }, { 8, 3 }, { 9, 2 }, { 2, -1 } };
	for (const int32* Row : Table)
	{
		M->LastHitGroup = Row[0];
		TestEqual(FString::Printf(TEXT("10395650 hitgroup %d -> tentacle %d"), Row[0], Row[1]),
			M->MingXiaoHitTentacleIndex(nullptr), Row[1]);
	}
	// 0x103952b0's melee map, with its fallthroughs.
	M->LastHitGroup = 4;
	TestEqual(TEXT("103952d7 hitgroup 4, tentacle 1 connected"), M->MingXiaoMeleeTentacleIndex(), 1);
	M->MingXiaoSeveredTentacleMask = 1u << 1;
	TestEqual(TEXT("103952f0 tentacle 1 severed falls into hitgroup 8's block"), M->MingXiaoMeleeTentacleIndex(), 3);
	M->MingXiaoSeveredTentacleMask = (1u << 1) | (1u << 3) | (1u << 5);
	TestEqual(TEXT("10395309 ...and to -1 with 5 severed too"), M->MingXiaoMeleeTentacleIndex(), -1);
	M->MingXiaoSeveredTentacleMask = 0;
	M->LastHitGroup = 2;
	// `0x1039539f`: `RandomInt(0, 99) < MeleeTentacleHitPercent` (Rules.txt, default 20,
	// `0x101e7405`): a pass walks 1,0,3,2,5,4 and answers limb 1 (all connected); a miss answers -1.
	const int32 Spread = M->MingXiaoMeleeTentacleIndex();
	TestTrue(TEXT("1039538d the spread draw answers limb 1 or -1"), Spread == 1 || Spread == -1);
	TestEqual(TEXT("103952b0 the draw's ceiling is the tuning record's +0x2c"), M->MingXiaoMeleeSpreadChance(), 20);

	// The head: the packet copy goes through the router, then the chain.
	M->MingXiaoTentacleId = INDEX_NONE;
	M->LastHitGroup = 4;
	for (float& Hp : M->MingXiaoHitPoints)
	{
		Hp = 100.f;
	}
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Damage19Packet(nullptr, 7.f);
	M->OnTakeDamage_Alive(&Info);
	TestEqual(TEXT("10395b3b the head runs 0x10395750 on the copy: limb 1 (10395b2d) takes the hit"),
		M->MingXiaoHitPoints[1], 93.f);
	TestEqual(TEXT("10395881 ...which zeroes the copy's damage before the chain (10395b47)"),
		M->LastTakeDamageInfo.Damage, 0.f);
	TestEqual(TEXT("...and the caller's packet is untouched"), Info.Damage, 7.f);
	// A proxy chains unmodified, with no router.
	M->MingXiaoTentacleId = 2;
	M->OnTakeDamage_Alive(&Info);
	TestEqual(TEXT("10395af6 a proxy skips the router"), M->MingXiaoHitPoints[1], 93.f);
	TestEqual(TEXT("10395af6 ...and chains the packet as is"), M->LastTakeDamageInfo.Damage, 7.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19TentacleTest,
	"Elysium.Arm.NpcKernelDamage19.MingXiaoTentacleOnTakeDamageAlive_1039e890", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19TentacleTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VMingXiaoTentacle"));
	FElysiumNpcMingXiaoTentacle* T = F.GuardAs<FElysiumNpcMingXiaoTentacle>();
	if (!TestNotNull(TEXT("the tentacle constructs"), T))
	{
		return false;
	}
	T->TentacleHideReadyTimer = 12.0;
	const int32 Before = T->SetScheduleRetailCalls;
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Damage19Packet(nullptr, 1.f);
	T->OnTakeDamage_Alive(&Info);
	TestEqual(TEXT("1039e89a m_flHideReadyTimer = 0"), T->TentacleHideReadyTimer, 0.0);
	TestEqual(TEXT("1039e8b8 SetSchedule(0x168) on every hit"), T->SetScheduleRetailCalls, Before + 1);
	TestEqual(TEXT("...program 0x168"), T->LastSetScheduleRetail, 0x168);
	TestFalse(TEXT("...unforced"), T->bLastSetScheduleForce);
	TestEqual(TEXT("1039e8c4 then the chain"), T->LastTakeDamageInfo.Damage, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19SabbatLeaderTest,
	"Elysium.Arm.NpcKernelDamage19.SabbatLeaderOnTakeDamageAlive_103aa480", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19SabbatLeaderTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VSabbatLeader"));
	FElysiumNpcSabbatLeader* S = F.GuardAs<FElysiumNpcSabbatLeader>();
	if (!TestNotNull(TEXT("the leader constructs"), S))
	{
		return false;
	}
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Damage19Packet(nullptr, 11.f);
	TestEqual(TEXT("103aa4d4 a pure forward answers the chain's 1"), S->OnTakeDamage_Alive(&Info), 1);
	TestEqual(TEXT("...the packet unchanged"), S->LastTakeDamageInfo.Damage, 11.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19SheriffTest,
	"Elysium.Arm.NpcKernelDamage19.SheriffManOnTakeDamageAlive_103b0e90", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19SheriffTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VSheriffMan"));
	FElysiumNpcSheriffMan* S = F.GuardAs<FElysiumNpcSheriffMan>();
	if (!TestNotNull(TEXT("the sheriff constructs"), S))
	{
		return false;
	}
	S->bSheriffTeleporting = false;
	S->bSheriffDead = false;
	S->TakeDamageMode = 2;
	FElysiumNpcBase::FElysiumTakeDamageInfo Small = Damage19Packet(nullptr, 5.f);
	S->OnTakeDamage_Alive(&Small);
	TestEqual(TEXT("103b0ff5 cap 20 > 0 + 5: the copy goes on untouched"), S->LastTakeDamageInfo.Damage, 5.f);
	TestFalse(TEXT("...not dead"), S->bSheriffDead);

	S->bSheriffTeleporting = true;
	S->OnTakeDamage_Alive(&Small);
	TestEqual(TEXT("103b1026 teleporting: the copy is neutered"), S->LastTakeDamageInfo.Damage, 0.f);
	TestFalse(TEXT("...and no death"), S->bSheriffDead);
	S->bSheriffTeleporting = false;

	FElysiumNpcBase::FElysiumTakeDamageInfo Lethal = Damage19Packet(nullptr, 25.f);
	S->OnTakeDamage_Alive(&Lethal);
	TestEqual(TEXT("103b0ff7 m_takedamage = 0"), S->TakeDamageMode, 0);
	TestTrue(TEXT("103b1010 species condition 0x79"),
		S->Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x79)));
	TestTrue(TEXT("103b1015 m_bDead"), S->bSheriffDead);
	TestEqual(TEXT("103b102c the killing packet is neutered"), S->LastTakeDamageInfo.Damage, 0.f);
	return true;
}

// =================================================================================================
// Slot 142 species — 0x103cccc0 Werewolf, 0x103e06d0 Zombie.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19WerewolfTest,
	"Elysium.Arm.NpcKernelDamage19.WerewolfOnTakeDamage_103cccc0", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19WerewolfTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VWerewolf"));
	FElysiumNpcWerewolf* W = F.GuardAs<FElysiumNpcWerewolf>();
	if (!TestNotNull(TEXT("the werewolf constructs"), W))
	{
		return false;
	}
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Damage19Packet(F.Other, 13.f, 0x80u);
	W->LastTakeDamageInfo.Damage = -1.f;
	TestEqual(TEXT("103ccd55 returns 0x102bed30's answer (the chain's slot-390 1)"), W->OnTakeDamage(&Info), 1);
	TestEqual(TEXT("...which reached the base chain (slot 390 cached the packet)"),
		W->LastTakeDamageInfo.Damage, 13.f);
	TestEqual(TEXT("103ccd4d the werewolf does NOT modify the damage"), Info.Damage, 13.f);
	TestEqual(TEXT("...or its bits"), Info.DamageBits, 0x80u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19ZombieTest,
	"Elysium.Arm.NpcKernelDamage19.ZombieOnTakeDamage_103e06d0", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19ZombieTest::RunTest(const FString&)
{
	FDamage19Fixture F(TEXT("CNPC_VZombie"));
	FElysiumNpcZombie* Z = F.GuardAs<FElysiumNpcZombie>();
	if (!TestNotNull(TEXT("the zombie constructs"), Z))
	{
		return false;
	}
	// Threshold = cvar 0 - wounds 0 + cap 20 = 20: 15 does not gib.
	Z->bZombieShouldGib = false;
	Z->bZombieShouldRagdoll = false;
	Z->bZombieHeadHit = false;
	// `DAMAGE_NO` makes `0x1032ef60` answer 0 at its first test, so the Troika chain's answer is 0 —
	// the arm these two cases exercise.
	Z->TakeDamageMode = 0;
	FElysiumNpcBase::FElysiumTakeDamageInfo Fifteen = Damage19Packet(nullptr, 15.f);
	const int32 Before = Z->SetScheduleRetailCalls;
	const int32 EmittersBefore = Z->EmitterCalls.Num();
	const int32 Result = Z->OnTakeDamage(&Fifteen);
	TestFalse(TEXT("103e07ff 15 is not above the head threshold 20"), Z->bZombieShouldGib);
	TestEqual(TEXT("103e080b the Troika chain's answer (0x1032ef60's DAMAGE_NO 0)"), Result, 0);
	TestEqual(TEXT("103e0857 a zero answer takes the fallback program"), Z->SetScheduleRetailCalls, Before + 1);
	TestEqual(TEXT("103e086d ...0x162"), Z->LastSetScheduleRetail, 0x162);
	TestTrue(TEXT("103e0861 ...FORCED"), Z->bLastSetScheduleForce);
	TestEqual(TEXT("103e088b no head hit: no emitter"), Z->EmitterCalls.Num(), EmittersBefore);

	FElysiumNpcBase::FElysiumTakeDamageInfo TwentyFive = Damage19Packet(nullptr, 25.f);
	Z->bZombieShouldRagdoll = true;
	Z->bZombieHeadHit = true;
	const int32 BeforeRagdoll = Z->SetScheduleRetailCalls;
	Z->OnTakeDamage(&TwentyFive);
	TestTrue(TEXT("103e0801 25 beats the threshold: m_bShouldGib"), Z->bZombieShouldGib);
	TestEqual(TEXT("103e085f m_bShouldRagdoll skips the program entirely"), Z->SetScheduleRetailCalls, BeforeRagdoll);
	TestTrue(TEXT("103e08ac a zero answer with a head hit: the DEATH emitter"),
		Z->EmitterCalls.Num() == EmittersBefore + 1
			&& Z->EmitterCalls.Last().Name == TEXT("zombie_headshot_death_emitter"));
	return true;
}

// =================================================================================================
// 0x1032ef60 / 0x103302e0 — `BeginVampHeal_HOT` and the Kindred frenzy arm (L13 review row 9).
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamage19CombatAliveFrenzyTest,
	"Elysium.Arm.NpcKernelDamage19.CombatAliveFrenzy_103302e0", GDamage19TestFlags)
bool FElysiumNpcKernelDamage19CombatAliveFrenzyTest::RunTest(const FString&)
{
	FDamage19Fixture F;
	if (!TestNotNull(TEXT("the guard stands"), F.Guard) || !TestNotNull(TEXT("the enemy stands"), F.Enemy))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	// A Kindred body that commits (`DAMAGE_YES`), with a ceiling no hit here reaches.
	N.bHasKindredTemplate = true;
	N.bKindredTemplate = true;
	N.TakeDamageMode = 2;
	N.Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 1000);
	N.RecomputeSheet();
	const TCHAR* const Hot = TEXT("CBaseCombatCharacter::BeginVampHeal_HOT");
	const TCHAR* const Frenzy = TEXT("CBaseCombatCharacter::FrenzyCheck");

	// 0x1032f17a: every alive packet asks the heal-over-time seam after the two stat reads.
	const int32 HotBefore = Damage19StubCount(Hot);
	const int32 FrenzyBefore = Damage19StubCount(Frenzy);
	FElysiumNpcBase::FElysiumTakeDamageInfo Small = Damage19Packet(F.Enemy, 10.f);
	N.OnTakeDamage(&Small);
	TestEqual(TEXT("0x1032f17a BeginVampHeal_HOT is asked on the alive arm"), Damage19StubCount(Hot), HotBefore + 1);
	TestEqual(TEXT("0x10330a50: 10 is below Dmg_Amount 0x39 and not aggravated: no FrenzyCheck"),
		Damage19StubCount(Frenzy), FrenzyBefore);

	// 0x10330a3a / 0x10330a50: at or above `VampFrenzy_Info/Dmg_Amount` (image default 0x39) the check runs.
	FElysiumNpcBase::FElysiumTakeDamageInfo Heavy = Damage19Packet(F.Enemy, 60.f);
	N.OnTakeDamage(&Heavy);
	TestEqual(TEXT("0x10330aa9 FrenzyCheck on a heavy hit from a live attacker"),
		Damage19StubCount(Frenzy), FrenzyBefore + 1);

	// 0x10330a78 / 0x10330a90: at or above AggrDmg_Amount (0x1d) only an aggravated hit checks.
	FElysiumNpcBase::FElysiumTakeDamageInfo Mid = Damage19Packet(F.Enemy, 30.f);
	N.OnTakeDamage(&Mid);
	TestEqual(TEXT("0x10330a9a: a plain 30 does not check"), Damage19StubCount(Frenzy), FrenzyBefore + 1);
	FElysiumNpcBase::FElysiumTakeDamageInfo MidAggravated = Damage19Packet(F.Enemy, 30.f, 0x8u);
	N.OnTakeDamage(&MidAggravated);
	TestEqual(TEXT("0x10330a90: an aggravated (0xc8000008) 30 checks"), Damage19StubCount(Frenzy), FrenzyBefore + 2);

	// 0x103309fb: no attacker, no check, however heavy.
	FElysiumNpcBase::FElysiumTakeDamageInfo Anonymous = Damage19Packet(nullptr, 60.f);
	N.OnTakeDamage(&Anonymous);
	TestEqual(TEXT("0x10330a00: an attacker-less packet never checks"), Damage19StubCount(Frenzy), FrenzyBefore + 2);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
