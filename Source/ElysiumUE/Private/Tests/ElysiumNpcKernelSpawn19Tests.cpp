// Story 0019/8 (29e under the strict verdict), family **Spawn19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelSpawn19.` and the retail address. Every case stands
// the NPC as the retail class it exercises (`AddNpcOfClass`) and drives the body directly -- the
// virtual on the NPC, or the qualified base where the row is the base body -- then asserts the words
// the listing writes. A spawn case re-runs `Spawn()` on the already-standing NPC after clearing the
// words it asserts, because the world's own spawn pass is followed by `Activate`'s `NPCInit`, which
// rewrites several of them (the ordering the integrator settles; see the L08 report).
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
#include "ElysiumEntityWorld.h"
#include "ElysiumNpcFlags.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcCamera.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcFrenzyShadow.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcGuard1.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcHuman.h"
#include "Substrate/ElysiumNpcHumanCombatant.h"
#include "Substrate/ElysiumNpcHunter.h"
#include "Substrate/ElysiumNpcLasombra.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcPayphone.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcPlayerController.h"
#include "Substrate/ElysiumNpcRat.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTestHull.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcVampire.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumNpcYukie.h"
#include "Substrate/ElysiumNpcZombie.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

static constexpr EAutomationTestFlags GSpawn19TestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One world: the subject NPC as `RetailClass`, a second combatant, a `point_target` prop the
	// cases that need a live side entity point a handle at, and a `CAI_TestHull` for the base-only
	// arm of `CAI_BaseNPC::Spawn`.
	struct FSpawn19Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumEntity* Prop = nullptr;
		FElysiumNpcBase* Hull = nullptr;
		FElysiumPlayer* Player = nullptr;

		explicit FSpawn19Fixture(const TCHAR* RetailClass)
			: World([RetailClass]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("spawn19_kernel"), 1919);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpcOfClass(TEXT("subject"), FVector::ZeroVector, RetailClass);
					Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
					Builder.AddEntity(TEXT("point_target"), TEXT("prop"), FVector(0.f, 400.f, 0.f));
					FElysiumEntityDef& HullDef = Builder.AddEntity(TEXT("CAI_TestHull"), TEXT("hull"),
						FVector(0.f, -400.f, 0.f));
					HullDef.InternalFactory = []() -> TUniquePtr<FElysiumEntity>
					{
						return MakeUnique<FElysiumNpcTestHull>();
					};
					return Builder;
				}())
		{
			Npc = World.Npc(TEXT("subject"));
			if (Npc == nullptr)
			{
				// A controller-line body renames itself `playercontroller` (`0x103a4510`).
				Npc = World.NpcOfClass(RetailClass);
			}
			Other = World.Npc(TEXT("other"));
			Prop = World.World.FindByName(TEXT("prop"));
			FElysiumEntity* const HullEntity = World.World.FindByName(TEXT("hull"));
			Hull = HullEntity != nullptr ? HullEntity->AsNpcBase() : nullptr;
			Player = World.Player();
			FElysiumNpcWorldFixture::Quiet({ Npc, Other });
		}

		template <class T>
		T* As() const
		{
			return Npc != nullptr ? Npc->AsSpecies<T>() : nullptr;
		}

		double Now() const { return World.World.NowSeconds(); }
	};

	// A damage packet with the player as attacker (`CTakeDamageInfo +0x2c`).
	FElysiumNpcBase::FElysiumTakeDamageInfo Spawn19Info(const FSpawn19Fixture& F)
	{
		FElysiumNpcBase::FElysiumTakeDamageInfo Info;
		Info.Attacker = F.Player != nullptr ? F.Player->Handle : FElysiumEntityHandle::Invalid();
		Info.Damage = 25.f;
		return Info;
	}

	bool Spawn19HasCaps(const FElysiumNpcBase& Npc, int32 Mask)
	{
		return (Npc.CapabilityWord & Mask) == Mask;
	}
}

// =================================================================================================
// 0x10265ad0 CAI_BaseNPC::Event_Killed
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19BaseEventKilledTest,
	"Elysium.Substrate.NpcKernelSpawn19.BaseEventKilled_0x10265ad0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19BaseEventKilledTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VHumanCombatant"));
	if (!TestNotNull(TEXT("the subject stands"), F.Npc) || !TestNotNull(TEXT("the player stands"), F.Player))
	{
		return false;
	}
	F.Npc->Flags |= FElysiumNpcBase::Spawn19BecomeDeadFlag;
	F.Npc->MaxHealth = 100;
	F.Npc->SpawnFlags &= ~0x200;
	F.Npc->Cognition.bCondTookDamage = false;
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Spawn19Info(F);

	F.Npc->FElysiumNpcBase::Event_Killed(&Info);

	// 0x10265d06 / 0x10265dba: the ideal and the current state are both DEAD.
	TestEqual(TEXT("m_IdealNPCState = 7 (0x10265db0)"), F.Npc->IdealStateRetail(), 7);
	TestEqual(TEXT("SetState(7) (0x10265dba)"), F.Npc->NpcStateRetail(), 7);
	// 0x10265d32: `m_hLastDamageEnt` is the attacker's handle. 0x10265d46 writes `m_bCondTookDamage`
	// (+0x5b80) = 1, but the tail's `SetState(7)` (0x10265dba -> 0x1026e340) changes the state and so
	// dispatches slot 463, whose Troika body `0x102ae140` clears +0x5b80 in its unconditional tail:
	// on a Troika NPC the word ends 0. (Integrator correction: the lane asserted 1.)
	TestTrue(TEXT("m_hLastDamageEnt = the attacker"), F.Npc->BaseMemory.LastDamageAttacker == F.Player->Handle);
	TestFalse(TEXT("m_bCondTookDamage: 1 at 0x10265d46, then 0 from OnStateChange 0x102ae140"),
		F.Npc->Cognition.bCondTookDamage);
	TestTrue(TEXT("SetCondition(0x4c LIGHT_DAMAGE) (0x10265d1f)"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));
	// 0x10265cc8 -> 0x10265a90: the one-shot OnDeath latch.
	TestTrue(TEXT("the +0x5bd4 latch is set"), F.Npc->HasReportedDeath());
	// 0x10265ce5 BecomeDead: health = maxhealth / 2, maxhealth = 5, MOVETYPE_FLYGRAVITY.
	TestEqual(TEXT("BecomeDead: m_iHealth = 100 / 2"), F.Npc->Health, 50);
	TestEqual(TEXT("BecomeDead: m_iMaxHealth = 5"), F.Npc->MaxHealth, 5);
	TestEqual(TEXT("BecomeDead: SetMoveType(6)"), F.Npc->RetailMoveType, 6);
	// 0x10265d70 -> 0x10265d90: no fade spawnflag, so the carcass sound, not the fade.
	TestEqual(TEXT("InsertSound(SOUND_CARCASS) once"), F.Npc->Spawn19CarcassSoundInserts, 1);
	TestEqual(TEXT("no SUB_StartFadeOut"), F.Npc->Spawn19FadeOutStarts, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19BaseEventKilledFreezeTest,
	"Elysium.Substrate.NpcKernelSpawn19.BaseEventKilledNpcFreezeRefuses_0x10265ad0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19BaseEventKilledFreezeTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VHumanCombatant"));
	if (!TestNotNull(TEXT("the subject stands"), F.Npc))
	{
		return false;
	}
	// 0x10265adf / 0x10265aee: an NPC already running `GetScheduleOfType(0x3a)` ignores the kill.
	F.Npc->Schedule.Current = F.Npc->Spawn19ScheduleOfType(FElysiumNpcBase::Spawn19SchedNpcFreeze);
	const int32 IdealBefore = F.Npc->IdealStateRetail();
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Spawn19Info(F);
	F.Npc->FElysiumNpcBase::Event_Killed(&Info);
	TestEqual(TEXT("the ideal state is untouched (0x10265dc4 return)"), F.Npc->IdealStateRetail(), IdealBefore);
	TestFalse(TEXT("no OnDeath latch"), F.Npc->HasReportedDeath());
	TestEqual(TEXT("no carcass sound"), F.Npc->Spawn19CarcassSoundInserts, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19BaseEventKilledFadeTest,
	"Elysium.Substrate.NpcKernelSpawn19.BaseEventKilledFadeArm_0x10265ad0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19BaseEventKilledFadeTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VHumanCombatant"));
	if (!TestNotNull(TEXT("the subject stands"), F.Npc))
	{
		return false;
	}
	// Slot 552 `0x1027a400` answers spawnflag bit 9: the fade arm (0x10265d72), no carcass sound.
	F.Npc->SpawnFlags |= 0x200;
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Spawn19Info(F);
	F.Npc->FElysiumNpcBase::Event_Killed(&Info);
	TestEqual(TEXT("SUB_StartFadeOut once"), F.Npc->Spawn19FadeOutStarts, 1);
	TestEqual(TEXT("no carcass sound"), F.Npc->Spawn19CarcassSoundInserts, 0);
	TestEqual(TEXT("still DEAD"), F.Npc->NpcStateRetail(), 7);
	return true;
}

// =================================================================================================
// 0x102bf340 CAI_BaseNPCTroika::Event_Killed
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19TroikaEventKilledTest,
	"Elysium.Substrate.NpcKernelSpawn19.TroikaEventKilled_0x102bf340", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19TroikaEventKilledTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("the subject stands"), F.Npc))
	{
		return false;
	}
	const int32 LawBefore = F.World.World.LawEvents().NumRetained();
	const int32 ReleasesBefore = F.Npc->InterestingPlaceReleases;
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Spawn19Info(F);
	F.Npc->Event_Killed(&Info);
	// 0x102bf3a0: the death stimulus, a criminal act of level 3.
	TestEqual(TEXT("one death stimulus"), F.Npc->Spawn19DeathStimulusBroadcasts, 1);
	TestEqual(TEXT("one law record"), F.World.World.LawEvents().NumRetained(), LawBefore + 1);
	if (F.World.World.LawEvents().NumRetained() > 0)
	{
		const ElysiumNpcWitness::FElysiumLawEvent& Record = F.World.World.LawEvents().Retained().Last();
		TestEqual(TEXT("level 3 (0x102bf372)"), Record.Severity, 3);
		TestTrue(TEXT("criminal channel (type 1)"), Record.Channel == ElysiumNpcWitness::EChannel::Criminal);
	}
	// 0x102bf3a8: the base body ran.
	TestEqual(TEXT("the base's DEAD"), F.Npc->NpcStateRetail(), 7);
	// 0x102bf3cd: the interesting-place release.
	TestEqual(TEXT("one interesting-place release"), F.Npc->InterestingPlaceReleases, ReleasesBefore + 1);
	// 0x102bf3ec..0x102bf3fa: the named NPC's MarkAsDead line.
	TestEqual(TEXT("one MarkAsDead line"), F.Npc->Spawn19MarkAsDeadLines, 1);
	TestEqual(TEXT("its text"), F.Npc->Spawn19LastMarkAsDeadLine, FString(TEXT("MarkAsDead(\"subject\")")));
	return true;
}

// =================================================================================================
// Species Event_Killed
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19GargoyleEventKilledTest,
	"Elysium.Substrate.NpcKernelSpawn19.GargoyleEventKilled_0x10378da0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19GargoyleEventKilledTest::RunTest(const FString&)
{
	{
		FSpawn19Fixture F(TEXT("CNPC_VGargoyle"));
		FElysiumNpcGargoyle* const Gargoyle = F.As<FElysiumNpcGargoyle>();
		if (!TestNotNull(TEXT("the gargoyle stands"), Gargoyle))
		{
			return false;
		}
		// 0x10378da8: `m_iDoingGibDeath == 0` installs the death PROGRAM and never reaches the base.
		Gargoyle->GargoyleDoingGibDeath = 0;
		FElysiumNpcBase::FElysiumTakeDamageInfo Info = Spawn19Info(F);
		Gargoyle->Event_Killed(&Info);
		TestFalse(TEXT("no base death on the program arm"), Gargoyle->HasReportedDeath());
	}
	{
		FSpawn19Fixture F(TEXT("CNPC_VGargoyle"));
		FElysiumNpcGargoyle* const Gargoyle = F.As<FElysiumNpcGargoyle>();
		if (!TestNotNull(TEXT("the gargoyle stands"), Gargoyle))
		{
			return false;
		}
		// 0x10378daa: a gibbing gargoyle takes the Troika body.
		Gargoyle->GargoyleDoingGibDeath = 1;
		FElysiumNpcBase::FElysiumTakeDamageInfo Info = Spawn19Info(F);
		Gargoyle->Event_Killed(&Info);
		TestTrue(TEXT("the base death on the gib arm"), Gargoyle->HasReportedDeath());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19HengeyokaiEventKilledTest,
	"Elysium.Substrate.NpcKernelSpawn19.HengeyokaiEventKilled_0x10380390", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19HengeyokaiEventKilledTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VHengeyokai"));
	FElysiumNpcHengeyokai* const Henge = F.As<FElysiumNpcHengeyokai>();
	if (!TestNotNull(TEXT("the hengeyokai stands"), Henge) || !TestNotNull(TEXT("the prop stands"), F.Prop))
	{
		return false;
	}
	// 0x1038039a: carrying (`+0x14b8` bit 5) -> `0x103828a0` drops the body first.
	Henge->NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
	Henge->HengeyokaiPickupTarget = F.Prop->Handle;
	const int32 FormBitsBefore = Henge->FormBitCalls;
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Spawn19Info(F);
	Henge->Event_Killed(&Info);
	TestFalse(TEXT("m_hPickupTarget = -1 (0x103828a0)"), Henge->HengeyokaiPickupTarget.IsSet());
	TestEqual(TEXT("FormBit(false) once"), Henge->FormBitCalls, FormBitsBefore + 1);
	TestTrue(TEXT("then the Troika death (0x103803aa)"), Henge->HasReportedDeath());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19ManBatEventKilledTest,
	"Elysium.Substrate.NpcKernelSpawn19.ManBatEventKilled_0x1038e8c0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19ManBatEventKilledTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VManBat"));
	FElysiumNpcManBat* const ManBat = F.As<FElysiumNpcManBat>();
	if (!TestNotNull(TEXT("the man-bat stands"), ManBat) || !TestNotNull(TEXT("the prop stands"), F.Prop))
	{
		return false;
	}
	ManBat->ManBatScreechCone = F.Prop->Handle;
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Spawn19Info(F);
	ManBat->Event_Killed(&Info);
	// 0x1038e8ce / 0x1038e8d5 / 0x1038e8dc, in that order.
	if (TestEqual(TEXT("three teardown calls"), ManBat->Spawn19DeathTeardown.Num(), 3))
	{
		TestEqual(TEXT("fly mode 2 first"), ManBat->Spawn19DeathTeardown[0], FString(TEXT("0x1038c170(2)")));
		TestEqual(TEXT("then the drop"), ManBat->Spawn19DeathTeardown[1], FString(TEXT("0x1038f660")));
		TestEqual(TEXT("then the minions"), ManBat->Spawn19DeathTeardown[2], FString(TEXT("0x1038fd40")));
	}
	// 0x1038e93d / 0x1038e945: the screech cone is DESTROYED and the handle cleared.
	TestTrue(TEXT("the cone entity is removed"), F.Prop->IsDead());
	TestFalse(TEXT("+0x66a4 = -1"), ManBat->ManBatScreechCone.IsSet());
	TestTrue(TEXT("then the Troika death (0x1038e956)"), ManBat->HasReportedDeath());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19TentacleEventKilledTest,
	"Elysium.Substrate.NpcKernelSpawn19.MingXiaoTentacleEventKilled_0x1039e900", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19TentacleEventKilledTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VMingXiaoTentacle"));
	FElysiumNpcMingXiaoTentacle* const Tentacle = F.As<FElysiumNpcMingXiaoTentacle>();
	if (!TestNotNull(TEXT("the tentacle stands"), Tentacle))
	{
		return false;
	}
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Spawn19Info(F);
	// First kill (0x1039e90b taken): LIFE_DYING and the death schedule, no base death.
	Tentacle->bTentaclePlayedDeathAnim = false;
	Tentacle->Event_Killed(&Info);
	TestEqual(TEXT("m_lifeState = 1 (0x1039e92e)"), Tentacle->AnimEventLifeStateWord, 1);
	// `0x1039e938 -> 0x1039e970` (Boss19's `MingXiaoTentacleEnterDeath`) raises `+0x6699`.
	TestTrue(TEXT("0x1039e970 ran: m_bPlayedDeathAnim (0x6699) raised"), Tentacle->bTentaclePlayedDeathAnim);
	TestFalse(TEXT("no base death on the first kill"), Tentacle->HasReportedDeath());
	// Second kill: the latch stands, the base runs.
	Tentacle->bTentaclePlayedDeathAnim = true;
	Tentacle->Event_Killed(&Info);
	TestTrue(TEXT("the Troika death on the second kill (0x1039e925)"), Tentacle->HasReportedDeath());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19MingXiaoEventKilledTest,
	"Elysium.Substrate.NpcKernelSpawn19.MingXiaoEventKilled_0x10395ba0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19MingXiaoEventKilledTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VMingXiao"));
	FElysiumNpcMingXiao* const Ming = F.As<FElysiumNpcMingXiao>();
	if (!TestNotNull(TEXT("Ming Xiao stands"), Ming))
	{
		return false;
	}
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Spawn19Info(F);
	Ming->bMingXiaoPlayedDeathAnim = false;
	Ming->Event_Killed(&Info);
	TestEqual(TEXT("m_lifeState = 1 (0x10395c29)"), Ming->AnimEventLifeStateWord, 1);
	// `0x10395c33 -> 0x10395c70` (Boss19's `MingXiaoEnterDeath`) raises `+0x6744`.
	TestTrue(TEXT("0x10395c70 ran: m_bPlayedDeathAnim (0x6744) raised"), Ming->bMingXiaoPlayedDeathAnim);
	TestFalse(TEXT("no base death on the first kill"), Ming->HasReportedDeath());
	// The HEAD (`m_iTentacleID == -1`) runs the two grub teardowns under `ming_xiao_grub_death` "1":
	// `0x10397e90` defeats every severed tentacle (`0x1039ea60`), `0x10397f00` every spawned body
	// (`0x10395ce0`). One of each stands for the sweep.
	FElysiumEntityDef TentacleDef;
	TentacleDef.Classname = TEXT("npc_VMingXiaoTentacle");
	const FElysiumEntityHandle TentacleHandle = F.World.World.SpawnRuntimeEntity(MoveTemp(TentacleDef));
	FElysiumEntityDef BodyDef;
	BodyDef.Classname = TEXT("npc_VMingXiao");
	const FElysiumEntityHandle BodyHandle = F.World.World.SpawnRuntimeEntity(MoveTemp(BodyDef));
	FElysiumEntity* const TentacleEntity = F.World.World.Resolve(TentacleHandle);
	FElysiumEntity* const BodyEntity = F.World.World.Resolve(BodyHandle);
	FElysiumNpcMingXiaoTentacle* const Severed = TentacleEntity != nullptr && TentacleEntity->AsNpc() != nullptr
		? TentacleEntity->AsNpc()->AsSpecies<FElysiumNpcMingXiaoTentacle>() : nullptr;
	FElysiumNpcMingXiao* const Spawned = BodyEntity != nullptr && BodyEntity->AsNpc() != nullptr
		? BodyEntity->AsNpc()->AsSpecies<FElysiumNpcMingXiao>() : nullptr;
	if (!TestNotNull(TEXT("a severed tentacle stands"), Severed) || !TestNotNull(TEXT("a spawned body stands"), Spawned))
	{
		return false;
	}
	Severed->bTentaclePlayedDeathAnim = false;
	Spawned->bMingXiaoPlayedDeathAnim = false;
	Ming->SeveredTentacles[0] = TentacleHandle;
	Ming->Proxies[0] = BodyHandle;
	Ming->bMingXiaoPlayedDeathAnim = true;
	Ming->MingXiaoTentacleId = INDEX_NONE;
	Ming->Event_Killed(&Info);
	TestTrue(TEXT("0x10395c0b -> 0x10397e90: the severed tentacle is defeated"), Severed->bTentaclePlayedDeathAnim);
	TestTrue(TEXT("0x10395c12 -> 0x10397f00: the spawned body is defeated"), Spawned->bMingXiaoPlayedDeathAnim);
	TestTrue(TEXT("then the Troika death (0x10395c1e)"), Ming->HasReportedDeath());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19TzimisceEventKilledTest,
	"Elysium.Substrate.NpcKernelSpawn19.TzimisceEventKilled_0x103be010", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19TzimisceEventKilledTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VTzimisce"));
	FElysiumNpcTzimisce* const Tzimisce = F.As<FElysiumNpcTzimisce>();
	if (!TestNotNull(TEXT("the tzimisce stands"), Tzimisce))
	{
		return false;
	}
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Spawn19Info(F);
	Tzimisce->Event_Killed(&Info);
	// 0x103be025: the nearby cleanup runs whether or not a body is carried.
	TestEqual(TEXT("0x103bdfc0 once"), Tzimisce->Spawn19NearbyRemovals, 1);
	TestTrue(TEXT("then the Troika death (0x103be031)"), Tzimisce->HasReportedDeath());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19HeadClawEventKilledTest,
	"Elysium.Substrate.NpcKernelSpawn19.TzimisceHeadClawEventKilled_0x103c1d50", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19HeadClawEventKilledTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VTzimisceHeadClaw"));
	FElysiumNpcTzimisceHeadClaw* const Claw = F.As<FElysiumNpcTzimisceHeadClaw>();
	if (!TestNotNull(TEXT("the head claw stands"), Claw))
	{
		return false;
	}
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Spawn19Info(F);
	Claw->Event_Killed(&Info);
	TestTrue(TEXT("EndSlow(true) then the Troika death (0x103c1d61)"), Claw->HasReportedDeath());
	return true;
}

// =================================================================================================
// 0x103dfbb0 CNPC_VZombie::CreateCorpse
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19ZombieCreateCorpseTest,
	"Elysium.Substrate.NpcKernelSpawn19.ZombieCreateCorpse_0x103dfbb0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19ZombieCreateCorpseTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VZombie"));
	FElysiumNpcZombie* const Zombie = F.As<FElysiumNpcZombie>();
	if (!TestNotNull(TEXT("the zombie stands"), Zombie))
	{
		return false;
	}
	FElysiumNpcBase::FElysiumTakeDamageInfo Info = Spawn19Info(F);
	const FVector Force(1.f, 2.f, 3.f);
	Zombie->CreateCorpse(Force, &Info);
	// COPY first (0x103dfbd7 / 0x103dfbe9), whatever follows.
	TestEqual(TEXT("m_vecDeathForceVector saved"), Zombie->ZombieDeathForceVector, Force);
	TestEqual(TEXT("m_DeathDamageInfo saved"), Zombie->ZombieDeathDamageInfo.Damage, Info.Damage);
	// No activity-0x21 sequence (the seam answers -1): 0x103dfc9b takes the gib arm and removes it.
	TestTrue(TEXT("CorpseGib then UTIL_Remove (0x103dfd09)"), Zombie->IsDead());
	return true;
}

// =================================================================================================
// Slots 617 / 618
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19InputTransformModelTest,
	"Elysium.Substrate.NpcKernelSpawn19.VampireBossInputTransformModel_0x103c75f0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19InputTransformModelTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VVampireBoss"));
	FElysiumNpcVampireBoss* const Boss = F.As<FElysiumNpcVampireBoss>();
	if (!TestNotNull(TEXT("the boss stands"), Boss))
	{
		return false;
	}
	FElysiumInputArgs Args;
	// An unauthored MorphModel: the classname is armed UNCONDITIONALLY, the model word is not.
	Boss->VampireBossMonsterClassname.Reset();
	Boss->VampireBossMonsterModelName = TEXT("before");
	Boss->VampireBossMorphModelName.Reset();
	Boss->InputTransformModel(Args);
	TestEqual(TEXT("+0x6694 = npc_VVampireBoss (0x103c763f)"), Boss->VampireBossMonsterClassname,
		FString(TEXT("npc_VVampireBoss")));
	TestEqual(TEXT("+0x6680 untouched on an empty morph (0x103c7659)"), Boss->VampireBossMonsterModelName,
		FString(TEXT("before")));
	// An authored one.
	Boss->VampireBossMorphModelName = TEXT("models/character/monster/monster.mdl");
	Boss->InputTransformModel(Args);
	TestEqual(TEXT("+0x6680 = the morph model (0x103c7662)"), Boss->VampireBossMonsterModelName,
		FString(TEXT("models/character/monster/monster.mdl")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19TransformationStartTest,
	"Elysium.Substrate.NpcKernelSpawn19.VampireBossTransformationStart_0x103c60a0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19TransformationStartTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VVampireBoss"));
	FElysiumNpcVampireBoss* const Boss = F.As<FElysiumNpcVampireBoss>();
	if (!TestNotNull(TEXT("the boss stands"), Boss))
	{
		return false;
	}
	Boss->VampireBossMonsterClassname = TEXT("npc_VVampireBoss");
	Boss->PlInvestigate = 3;
	Boss->PercentOccludedChase = 77;
	Boss->TransformationStart();
	FElysiumEntity* const NewEntity = F.World.World.Resolve(Boss->ProteanTransformOther);
	FElysiumNpcVampireBoss* const New = NewEntity != nullptr && NewEntity->AsNpc() != nullptr
		? NewEntity->AsNpc()->AsSpecies<FElysiumNpcVampireBoss>() : nullptr;
	if (!TestNotNull(TEXT("m_hProteanTransformOther names the new body (0x103c6304)"), New))
	{
		return false;
	}
	TestTrue(TEXT("the new body's +0x155c is us (0x103c61e0)"), New->ProteanTransformOther == Boss->Handle);
	TestTrue(TEXT("the new body's m_hTransformPartner is us (0x103c62e1)"), New->TransformPartner == Boss->Handle);
	TestEqual(TEXT("both protean stamps agree"), New->ProteanTransformStartTime, Boss->ProteanTransformStartTime);
	TestEqual(TEXT("new m_nRenderFX = 0x1f"), New->RenderFxWord, 0x1f);
	TestEqual(TEXT("our m_nRenderFX = 0x1e"), Boss->RenderFxWord, 0x1e);
	TestEqual(TEXT("our m_nRenderMode = 2"), Boss->RenderMode, 2);
	TestEqual(TEXT("the police level copied"), New->PlInvestigate, 3);
	TestEqual(TEXT("the occlusion percent copied"), New->PercentOccludedChase, 77);
	TestTrue(TEXT("spawnflag 4 on the new body (0x103c611b)"), (New->SpawnFlags & 4) != 0);
	TestTrue(TEXT("owned by us (slot 202)"), New->GetOwnerEntity() == Boss->Handle);
	TestEqual(TEXT("m_flSeekDistBase = 4096 (0x103c615e)"), New->AuthoredVision, 4096.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19TransformationStartMissTest,
	"Elysium.Substrate.NpcKernelSpawn19.VampireBossTransformationStartFactoryMiss_0x103c60a0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19TransformationStartMissTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VVampireBoss"));
	FElysiumNpcVampireBoss* const Boss = F.As<FElysiumNpcVampireBoss>();
	if (!TestNotNull(TEXT("the boss stands"), Boss))
	{
		return false;
	}
	// The factory miss retail faults on (0x103c6104 -> 0x103c6110): the named crash guard.
	Boss->VampireBossMonsterClassname = TEXT("no_such_classname");
	Boss->TransformationStart();
	TestEqual(TEXT("one factory miss counted"), Boss->Spawn19TransformCreateFailures, 1);
	TestFalse(TEXT("no partner written"), Boss->ProteanTransformOther.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19SabbatLeaderTransformationStartTest,
	"Elysium.Substrate.NpcKernelSpawn19.SabbatLeaderTransformationStart_0x103ab310", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19SabbatLeaderTransformationStartTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VSabbatLeader"));
	FElysiumNpcSabbatLeader* const Leader = F.As<FElysiumNpcSabbatLeader>();
	if (!TestNotNull(TEXT("the leader stands"), Leader))
	{
		return false;
	}
	Leader->VampireBossMonsterClassname = TEXT("npc_VVampireBoss");
	Leader->HullKind = 3;
	Leader->PathingHullKind = 3;
	Leader->TransformationStart();
	TestEqual(TEXT("the no-mouth model (0x103ab36a)"), Leader->Model,
		FString(TEXT("models/character/npc/unique/hollywood/andrei/andrei_no_mouth.mdl")));
	TestEqual(TEXT("m_eHull = 0 (0x103ab39d)"), Leader->HullKind, 0);
	TestEqual(TEXT("m_eDefaultHull = 0 (0x103ab3a3)"), Leader->PathingHullKind, 0);
	// Then the boss body (0x103ab3b9); it writes our render words last.
	TestTrue(TEXT("the boss swap ran"), Leader->ProteanTransformOther.IsSet());
	TestEqual(TEXT("the boss body's m_nRenderFX 0x1e wins over the leader's 0"), Leader->RenderFxWord, 0x1e);
	return true;
}

// =================================================================================================
// 0x10273200 CAI_BaseNPC::Spawn and 0x10298d30 CAI_BaseNPCTroika::Spawn
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19BaseSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.BaseSpawn_0x10273200", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19BaseSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VHumanCombatant"));
	if (!TestNotNull(TEXT("the subject stands"), F.Npc) || !TestNotNull(TEXT("the test hull stands"), F.Hull))
	{
		return false;
	}
	// A Troika NPC (`m_pBaseNPCTroika` set, 0x1027329b) never takes the equipment arm.
	F.Npc->CapabilityWord |= FElysiumNpcBase::Spawn19CapUseWeapons;
	F.Npc->AdditionalEquipment = TEXT("item_w_knife");
	// The map's spawn pass already ran the Troika body (`FElysiumNpc::Spawn` -> `0x10299006`), so
	// the counts are taken relative to it.
	const int32 WeaponRequestsBefore = F.Npc->Spawn19WeaponCreateRequests;
	const int32 CombatSpawnsBefore = F.Npc->Spawn19CombatCharacterSpawns;
	F.Npc->FElysiumNpcBase::Spawn();
	TestEqual(TEXT("no Weapon_Create on the Troika line"), F.Npc->Spawn19WeaponCreateRequests, WeaponRequestsBefore);
	TestEqual(TEXT("CBaseCombatCharacter::Spawn once (0x10273328)"), F.Npc->Spawn19CombatCharacterSpawns,
		CombatSpawnsBefore + 1);

	// A plain CAI_BaseNPC with the capability and a real classname takes it (0x10273306).
	F.Hull->CapabilityWord |= FElysiumNpcBase::Spawn19CapUseWeapons;
	F.Hull->AdditionalEquipment = TEXT("item_w_knife");
	F.Hull->FElysiumNpcBase::Spawn();
	TestEqual(TEXT("Weapon_Create once"), F.Hull->Spawn19WeaponCreateRequests, 1);
	TestEqual(TEXT("with m_spawnEquipment"), F.Hull->Spawn19LastWeaponCreate, FString(TEXT("item_w_knife")));
	// The two refused literals (0x102732d1, 0x102732f2).
	F.Hull->AdditionalEquipment = TEXT("0");
	F.Hull->FElysiumNpcBase::Spawn();
	F.Hull->AdditionalEquipment = TEXT("item_w_unarmed");
	F.Hull->FElysiumNpcBase::Spawn();
	TestEqual(TEXT("neither literal creates"), F.Hull->Spawn19WeaponCreateRequests, 1);
	// No capability (0x102732ac): no create.
	F.Hull->CapabilityWord &= ~FElysiumNpcBase::Spawn19CapUseWeapons;
	F.Hull->AdditionalEquipment = TEXT("item_w_knife");
	F.Hull->FElysiumNpcBase::Spawn();
	TestEqual(TEXT("no create without 0x200000"), F.Hull->Spawn19WeaponCreateRequests, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19TroikaSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.TroikaSpawn_0x10298d30", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19TroikaSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("the subject stands"), F.Npc))
	{
		return false;
	}
	F.Npc->CapabilityWord = 0;
	F.Npc->PlInvestigate = 0;
	F.Npc->PlCriminalAttack = -3;
	F.Npc->PlSupernaturalFlee = 4;
	F.Npc->PercentOccludedWait = 1;
	F.Npc->PercentOccludedCover = 1;
	F.Npc->PercentOccludedWalk = 1;
	F.Npc->PercentOccludedFlank = 1;
	F.Npc->PercentOccludedChase = 1;
	F.Npc->TroikaSpawnBody();
	TestTrue(TEXT("capabilities 1, 0x800000, 8"), Spawn19HasCaps(*F.Npc, 0x800009));
	TestEqual(TEXT("m_bloodColor = 0xf7"), F.Npc->BloodColorWord, 0xf7);
	TestEqual(TEXT("m_flFieldOfView = 0.2"), F.Npc->FieldOfViewDot, 0.2f);
	TestEqual(TEXT("m_HackedGunPos.z = 55"), F.Npc->HackedGunPosUnits.Z, 55.0);
	TestEqual(TEXT("SetSolid(SOLID_BBOX)"), F.Npc->RetailSolidType, 2);
	TestTrue(TEXT("solid flags 1 and 0x40"), (F.Npc->RetailSolidFlags & 0x41u) == 0x41u);
	TestEqual(TEXT("SetMoveType(4)"), F.Npc->RetailMoveType, 4);
	TestTrue(TEXT("AddFlag2(4)"), (F.Npc->EntityFlags2Word & 4u) != 0u);
	// The police-level repairs: below 1 -> 6, a valid level left alone.
	TestEqual(TEXT("pl_investigate 0 -> 6"), F.Npc->PlInvestigate, 6);
	TestEqual(TEXT("pl_criminal_attack -3 -> 6"), F.Npc->PlCriminalAttack, 6);
	TestEqual(TEXT("pl_supernatural_flee 4 kept"), F.Npc->PlSupernaturalFlee, 4);
	// The occluded ladder: five ones sum to 5, cumulative 20/40/60/80/100.
	TestEqual(TEXT("wait 20"), F.Npc->PercentOccludedWait, 20);
	TestEqual(TEXT("cover 40"), F.Npc->PercentOccludedCover, 40);
	TestEqual(TEXT("walk 60"), F.Npc->PercentOccludedWalk, 60);
	TestEqual(TEXT("flank 80"), F.Npc->PercentOccludedFlank, 80);
	TestEqual(TEXT("chase 100"), F.Npc->PercentOccludedChase, 100);
	// A zero sum with chase 0: the warning, then the default ladder 10/40/50/70/100.
	F.Npc->PercentOccludedWait = 0;
	F.Npc->PercentOccludedCover = 0;
	F.Npc->PercentOccludedWalk = 0;
	F.Npc->PercentOccludedFlank = 0;
	F.Npc->PercentOccludedChase = 0;
	F.Npc->TroikaSpawnBody();
	TestEqual(TEXT("default wait 10"), F.Npc->PercentOccludedWait, 10);
	TestEqual(TEXT("default cover 40"), F.Npc->PercentOccludedCover, 40);
	TestEqual(TEXT("default walk 50"), F.Npc->PercentOccludedWalk, 50);
	TestEqual(TEXT("default flank 70"), F.Npc->PercentOccludedFlank, 70);
	TestEqual(TEXT("default chase 100"), F.Npc->PercentOccludedChase, 100);
	return true;
}

// =================================================================================================
// Species Spawn
// =================================================================================================

// A capability-only row: clear the word, re-run the class's own Spawn, read the mask.
template <class TSpecies>
static bool Spawn19CapsCase(FAutomationTestBase& Test, const TCHAR* RetailClass, int32 Mask)
{
	FSpawn19Fixture F(RetailClass);
	TSpecies* const Subject = F.As<TSpecies>();
	if (!Test.TestNotNull(TEXT("the subject stands as its class"), Subject))
	{
		return false;
	}
	Subject->CapabilityWord = 0;
	Subject->Spawn();
	Test.TestTrue(TEXT("the listing's capability mask"), Spawn19HasCaps(*Subject, Mask));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19AnimalSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.AnimalSpawn_0x1035f510", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19AnimalSpawnTest::RunTest(const FString&)
{
	return Spawn19CapsCase<FElysiumNpcAnimal>(*this, TEXT("CNPC_VAnimal"), 0x4000000);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19HumanSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.HumanSpawn_0x10384690", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19HumanSpawnTest::RunTest(const FString&)
{
	return Spawn19CapsCase<FElysiumNpcHuman>(*this, TEXT("CNPC_VHuman"), 0xc200d00);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19HumanCombatantSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.HumanCombatantSpawn_0x10387110", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19HumanCombatantSpawnTest::RunTest(const FString&)
{
	return Spawn19CapsCase<FElysiumNpcHumanCombatant>(*this, TEXT("CNPC_VHumanCombatant"), 0xc200d40);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19PedestrianSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.PedestrianSpawn_0x103a2540", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19PedestrianSpawnTest::RunTest(const FString&)
{
	return Spawn19CapsCase<FElysiumNpcPedestrian>(*this, TEXT("CNPC_VPedestrian"), 0xc200d00);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19HunterSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.HunterSpawn_0x103887a0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19HunterSpawnTest::RunTest(const FString&)
{
	return Spawn19CapsCase<FElysiumNpcHunter>(*this, TEXT("CNPC_VHunter"), 0xc200d40);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19AsianVampireSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.AsianVampireSpawn_0x10360c50", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19AsianVampireSpawnTest::RunTest(const FString&)
{
	return Spawn19CapsCase<FElysiumNpcAsianVampire>(*this, TEXT("CNPC_VAsianVampire"), 0xc201d40);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19ChangBrosSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.ChangBrosSpawn_0x1036afc0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19ChangBrosSpawnTest::RunTest(const FString&)
{
	return Spawn19CapsCase<FElysiumNpcChangBros>(*this, TEXT("CNPC_VChangBros"), 0xc209d40);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19SheriffManSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.SheriffManSpawn_0x103ae630", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19SheriffManSpawnTest::RunTest(const FString&)
{
	return Spawn19CapsCase<FElysiumNpcSheriffMan>(*this, TEXT("CNPC_VSheriffMan"), 0xc201d40);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19VampireSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.VampireSpawn_0x103c4ef0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19VampireSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VVampire"));
	FElysiumNpcVampire* const Vampire = F.As<FElysiumNpcVampire>();
	if (!TestNotNull(TEXT("the vampire stands"), Vampire))
	{
		return false;
	}
	Vampire->CapabilityWord = 0;
	Vampire->Spawn();
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	int32 Priority = -1;
	const bool bRow = Vampire->Relationships.ResolvePersistentRow(FElysiumEntityHandle::Invalid(),
		TEXT("player"), Value, Priority);
	TestTrue(TEXT("AddClassRelationship(1, D_HT, 0) (0x103c4ef9)"), bRow && Value == EElysiumRelationship::Hate);
	TestTrue(TEXT("then the human caps and 0x40 (0x103c4f09)"), Spawn19HasCaps(*Vampire, 0xc200d40));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19Guard1SpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.Guard1Spawn_0x1037cda0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19Guard1SpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VGuard1"));
	FElysiumNpcGuard1* const Guard = F.As<FElysiumNpcGuard1>();
	if (!TestNotNull(TEXT("the guard stands"), Guard))
	{
		return false;
	}
	Guard->CapabilityWord = 0;
	Guard->bGuard1HatesPlayer = true;
	Guard->Spawn();
	TestFalse(TEXT("m_fHatesPlayer = 0 (0x1037cdad)"), Guard->bGuard1HatesPlayer);
	TestTrue(TEXT("0x200d00 plus the human caps"), Spawn19HasCaps(*Guard, 0xc200d00));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19YukieSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.YukieSpawn_0x103dd620", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19YukieSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VYukie"));
	FElysiumNpcYukie* const Yukie = F.As<FElysiumNpcYukie>();
	if (!TestNotNull(TEXT("Yukie stands"), Yukie))
	{
		return false;
	}
	Yukie->FieldOfViewDot = 0.5f;
	Yukie->Spawn();
	TestEqual(TEXT("m_flFieldOfView = -1.0 LAST (0x103dd634)"), Yukie->FieldOfViewDot, -1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19CopSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.CopSpawn_0x10371a20", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19CopSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VCop"));
	FElysiumNpcCop* const Cop = F.As<FElysiumNpcCop>();
	if (!TestNotNull(TEXT("the cop stands"), Cop))
	{
		return false;
	}
	const int32 CensusBefore = FElysiumNpcCop::CopAliveCensus();
	Cop->CopOldPlayerRelationType = 4;
	Cop->bCopCountedAlive = false;
	Cop->bCopCountedSecond = true;
	Cop->Spawn();
	TestEqual(TEXT("DAT_1093acac incremented (0x10371a44)"), FElysiumNpcCop::CopAliveCensus(), CensusBefore + 1);
	TestTrue(TEXT("m_bCountedAlive = 1 (0x10371a49)"), Cop->bCopCountedAlive);
	TestFalse(TEXT("+0x6672 = 0 (0x10371a50)"), Cop->bCopCountedSecond);
	TestEqual(TEXT("m_eOldPlayerRelationType = 0 (0x10371a2f)"), Cop->CopOldPlayerRelationType, 0);
	FElysiumNpcCop::CopAliveCensus() = CensusBefore;   // the process-wide static outlives the world
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19GhoulCroucherSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.GhoulCroucherSpawn_0x1037b040", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19GhoulCroucherSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VGhoulCroucher"));
	FElysiumNpcGhoulCroucher* const Ghoul = F.As<FElysiumNpcGhoulCroucher>();
	if (!TestNotNull(TEXT("the croucher stands"), Ghoul))
	{
		return false;
	}
	// Burning wins over disturbed (0x1037b07c before 0x1037b09a).
	Ghoul->bGhoulSpawnBurning = true;
	Ghoul->bGhoulSpawnDisturbed = true;
	Ghoul->Spawn();
	TestEqual(TEXT("MalkMansionStalkerBurning"), Ghoul->StatTemplate, FString(TEXT("MalkMansionStalkerBurning")));
	TestTrue(TEXT("disturbed: m_bWasDisturbed"), Ghoul->bWasDisturbed);
	TestTrue(TEXT("disturbed: the stalker model"), Ghoul->Model.EndsWith(TEXT("Stalker/stalker.mdl")));
	TestEqual(TEXT("police levels 999999"), Ghoul->PlInvestigate, 999999);
	TestTrue(TEXT("m_nUnawareType in 0..3"), Ghoul->UnawareType >= 0 && Ghoul->UnawareType <= 3);
	// Neither: the croucher and the female model.
	Ghoul->bGhoulSpawnBurning = false;
	Ghoul->bGhoulSpawnDisturbed = false;
	Ghoul->Spawn();
	TestEqual(TEXT("MalkMansionCroucher"), Ghoul->StatTemplate, FString(TEXT("MalkMansionCroucher")));
	TestFalse(TEXT("undisturbed: m_bWasDisturbed = 0"), Ghoul->bWasDisturbed);
	TestTrue(TEXT("undisturbed: the female model"), Ghoul->Model.EndsWith(TEXT("stalker_female.mdl")));
	// Disturbed only.
	Ghoul->bGhoulSpawnDisturbed = true;
	Ghoul->Spawn();
	TestEqual(TEXT("MalkMansionStalker"), Ghoul->StatTemplate, FString(TEXT("MalkMansionStalker")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19AndreiBloodSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.AndreiBloodSpawn_0x1035cc20", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19AndreiBloodSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VAndreiBlood"));
	FElysiumNpcAndreiBlood* const Andrei = F.As<FElysiumNpcAndreiBlood>();
	if (!TestNotNull(TEXT("Andrei stands"), Andrei))
	{
		return false;
	}
	Andrei->ActiveRunnerCount = 3;
	Andrei->bAndreiForceTeleport = false;
	Andrei->Spawn();
	TestEqual(TEXT("m_iActiveRunnerCount = 0"), Andrei->ActiveRunnerCount, 0);
	TestTrue(TEXT("m_bForceTeleport = 1 (0x1035ccb5)"), Andrei->bAndreiForceTeleport);
	TestEqual(TEXT("m_fTeleportWaitStartTime = curtime"), Andrei->AndreiTeleportWaitStartTime, F.Now());
	TestTrue(TEXT("m_iHitMax rolled (0x1035e950)"), Andrei->AndreiHitMax >= 2 && Andrei->AndreiHitMax <= 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19BachSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.BachSpawn_0x10363850", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19BachSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VBach"));
	FElysiumNpcBach* const Bach = F.As<FElysiumNpcBach>();
	if (!TestNotNull(TEXT("Bach stands"), Bach))
	{
		return false;
	}
	Bach->bBachInStartingPosition = false;
	Bach->BachNextShieldTime = 9.0;
	Bach->bCanFightYet = true;
	Bach->Spawn();
	TestTrue(TEXT("m_bBachInStartingPosition = 1 (0x1036386f)"), Bach->bBachInStartingPosition);
	TestEqual(TEXT("m_flNextShieldTime = 0"), Bach->BachNextShieldTime, 0.0);
	TestFalse(TEXT("m_bCanFightYet = 0 (0x10363900)"), Bach->bCanFightYet);
	TestTrue(TEXT("0x200000 added"), Spawn19HasCaps(*Bach, 0x200000));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19HengeyokaiSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.HengeyokaiSpawn_0x1037fa00", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19HengeyokaiSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VHengeyokai"));
	FElysiumNpcHengeyokai* const Henge = F.As<FElysiumNpcHengeyokai>();
	if (!TestNotNull(TEXT("the hengeyokai stands"), Henge))
	{
		return false;
	}
	Henge->EntityFlags2Word = 4u;
	Henge->Spawn();
	TestTrue(TEXT("RemoveFlag2(4) then AddFlag2(0x20)"), Henge->EntityFlags2Word == 0x20u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19LasombraSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.LasombraSpawn_0x10389390", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19LasombraSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VLasombra"));
	FElysiumNpcLasombra* const Lasombra = F.As<FElysiumNpcLasombra>();
	if (!TestNotNull(TEXT("the lasombra stands"), Lasombra))
	{
		return false;
	}
	Lasombra->LasombraCoverDisableOverride = 5.f;
	Lasombra->Spawn();
	TestEqual(TEXT("m_flCoverDisableOverride = 0 (0x10389398)"), Lasombra->LasombraCoverDisableOverride, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19ManBatSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.ManBatSpawn_0x1038b030", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19ManBatSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VManBat"));
	FElysiumNpcManBat* const ManBat = F.As<FElysiumNpcManBat>();
	if (!TestNotNull(TEXT("the man-bat stands"), ManBat))
	{
		return false;
	}
	ManBat->bForceFrequentThink = false;
	ManBat->Spawn();
	TestTrue(TEXT("SetForceFrequentThink(true) (0x1038b03e)"), ManBat->bForceFrequentThink);
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	int32 Priority = -1;
	ManBat->Relationships.ResolvePersistentRow(FElysiumEntityHandle::Invalid(), TEXT("player"), Value, Priority);
	TestEqual(TEXT("the class-1 row at priority 10 (0x1038b04c)"), Priority, 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19SabbatLeaderSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.SabbatLeaderSpawn_0x103a6c80", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19SabbatLeaderSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VSabbatLeader"));
	FElysiumNpcSabbatLeader* const Leader = F.As<FElysiumNpcSabbatLeader>();
	if (!TestNotNull(TEXT("the leader stands"), Leader))
	{
		return false;
	}
	Leader->CapabilityWord = 0;
	Leader->StatTemplate.Reset();
	Leader->Spawn();
	TestEqual(TEXT("m_statTemplate = VampireSabbatLeader (0x103a6ced)"), Leader->StatTemplate,
		FString(TEXT("VampireSabbatLeader")));
	TestTrue(TEXT("0x209000 (0x103a6cd8)"), Spawn19HasCaps(*Leader, 0x209000));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19DogSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.DogSpawn_0x10374000", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19DogSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VDog"));
	FElysiumNpcDog* const Dog = F.As<FElysiumNpcDog>();
	if (!TestNotNull(TEXT("the dog stands"), Dog))
	{
		return false;
	}
	Dog->CapabilityWord = 0;
	Dog->DogWord6688 = 7;
	Dog->Spawn();
	TestEqual(TEXT("+0x6688 = 0 (0x10374025)"), Dog->DogWord6688, 0);
	TestTrue(TEXT("+0x6674 = curtime + [0, 1] (0x1037402f)"),
		Dog->DogStamp6674 >= F.Now() && Dog->DogStamp6674 <= F.Now() + 1.0);
	TestTrue(TEXT("0x4000000 | 0x200000 | 0x8000"), Spawn19HasCaps(*Dog, 0x4208000));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19ScurryingSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.ScurryingSpawn_0x103ac430", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19ScurryingSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VScurrying"));
	FElysiumNpcScurrying* const Scurry = F.As<FElysiumNpcScurrying>();
	if (!TestNotNull(TEXT("the scurrying stands"), Scurry))
	{
		return false;
	}
	Scurry->FrenziedWord = 0u;
	Scurry->Spawn();
	TestTrue(TEXT("m_bfNPCFrenziedFlags |= 0x10000 (0x103ac443)"), (Scurry->FrenziedWord & 0x10000u) != 0u);
	TestEqual(TEXT("+0x668c = curtime (0x103ac451)"), Scurry->ScurryingSpawnStamp, F.Now());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19RatSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.RatSpawn_0x103ad630", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19RatSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VRat"));
	FElysiumNpcRat* const Rat = F.As<FElysiumNpcRat>();
	if (!TestNotNull(TEXT("the rat stands"), Rat))
	{
		return false;
	}
	Rat->StatTemplate.Reset();
	Rat->FrenziedWord = 0u;
	Rat->Spawn();
	TestEqual(TEXT("m_statTemplate = Rat (0x103ad63e)"), Rat->StatTemplate, FString(TEXT("Rat")));
	TestTrue(TEXT("then the scurrying body"), (Rat->FrenziedWord & 0x10000u) != 0u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19ZombieSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.ZombieSpawn_0x103df170", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19ZombieSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VZombie"));
	FElysiumNpcZombie* const Zombie = F.As<FElysiumNpcZombie>();
	if (!TestNotNull(TEXT("the zombie stands"), Zombie))
	{
		return false;
	}
	Zombie->AdditionalEquipment = TEXT("item_w_fists");
	Zombie->PlCriminalFlee = 3;
	Zombie->Spawn();
	TestTrue(TEXT("m_spawnEquipment = NULL (0x103df1a7)"), Zombie->AdditionalEquipment.IsEmpty());
	TestEqual(TEXT("the police levels 999999 (0x103df1b9)"), Zombie->PlCriminalFlee, 999999);
	TestTrue(TEXT("0x4000000 | 0x200000 | 0x8000"), Spawn19HasCaps(*Zombie, 0x4208000));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19WerewolfSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.WerewolfSpawn_0x103caa30", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19WerewolfSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VWerewolf"));
	FElysiumNpcWerewolf* const Werewolf = F.As<FElysiumNpcWerewolf>();
	if (!TestNotNull(TEXT("the werewolf stands"), Werewolf))
	{
		return false;
	}
	Werewolf->StatTemplate.Reset();
	Werewolf->WerewolfDoorState = 3;
	Werewolf->EntityFlags2Word = 4u;
	Werewolf->Spawn();
	TestEqual(TEXT("m_statTemplate = Werewolf (0x103caaaa)"), Werewolf->StatTemplate, FString(TEXT("Werewolf")));
	TestTrue(TEXT("AddFlag(0x2000)"), (Werewolf->Flags & 0x2000) != 0);
	TestTrue(TEXT("m_bIsBCCTargetable = 1"), Werewolf->bIsBccTargetable);
	TestEqual(TEXT("m_DoorState = 0"), Werewolf->WerewolfDoorState, 0);
	TestTrue(TEXT("RemoveFlag2(4), AddFlag2(0x10)"), Werewolf->EntityFlags2Word == 0x10u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19TzimisceSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.TzimisceSpawn_0x103b9060", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19TzimisceSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VTzimisce"));
	FElysiumNpcTzimisce* const Tzimisce = F.As<FElysiumNpcTzimisce>();
	if (!TestNotNull(TEXT("the tzimisce stands"), Tzimisce))
	{
		return false;
	}
	Tzimisce->HeadLocalForward = FVector(1.f, 2.f, 3.f);
	Tzimisce->bInMelee = false;
	Tzimisce->Spawn();
	TestEqual(TEXT("(x, y, z) -> (-y, x, z) (0x103b9095..0x103b90a9)"), Tzimisce->HeadLocalForward,
		FVector(-2.f, 1.f, 3.f));
	TestEqual(TEXT("m_flFieldOfView = -0.5"), Tzimisce->FieldOfViewDot, -0.5f);
	TestTrue(TEXT("m_bInMelee = 1 (0x103b90d8)"), Tzimisce->bInMelee);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19HeadClawSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.TzimisceHeadClawSpawn_0x103c1b90", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19HeadClawSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VTzimisceHeadClaw"));
	FElysiumNpcTzimisceHeadClaw* const Claw = F.As<FElysiumNpcTzimisceHeadClaw>();
	if (!TestNotNull(TEXT("the head claw stands"), Claw))
	{
		return false;
	}
	Claw->CapabilityWord = 0;
	Claw->FrenziedWord = 0u;
	Claw->Spawn();
	TestEqual(TEXT("TzimisceCreation2"), Claw->StatTemplate, FString(TEXT("TzimisceCreation2")));
	TestEqual(TEXT("D_HT 10"), Claw->PlayerReaction, FString(TEXT("D_HT 10")));
	TestEqual(TEXT("occluded wait 0"), Claw->PercentOccludedWait, 0);
	TestEqual(TEXT("occluded chase 100"), Claw->PercentOccludedChase, 100);
	TestTrue(TEXT("0x4000000 | 0x200000"), Spawn19HasCaps(*Claw, 0x4200000));
	TestTrue(TEXT("frenzied |= 0x80"), (Claw->FrenziedWord & 0x80u) != 0u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19RunnerSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.TzimisceRunnerSpawn_0x103c3b30", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19RunnerSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VTzimisceRunner"));
	FElysiumNpcTzimisceRunner* const Runner = F.As<FElysiumNpcTzimisceRunner>();
	if (!TestNotNull(TEXT("the runner stands"), Runner) || !TestNotNull(TEXT("the prop"), F.Prop))
	{
		return false;
	}
	Runner->CapabilityWord = 0;
	Runner->RunnerPotentialEnemy = F.Prop->Handle;
	Runner->Spawn();
	TestEqual(TEXT("TzimisceCreation3"), Runner->StatTemplate, FString(TEXT("TzimisceCreation3")));
	TestTrue(TEXT("0x4000000 only"), Spawn19HasCaps(*Runner, 0x4000000) && (Runner->CapabilityWord & 0x200000) == 0);
	TestTrue(TEXT("m_bAllowsInterpenetratingAttacks = 1 (0x103c3ba7)"), Runner->bAllowsInterpenetratingAttacks);
	TestFalse(TEXT("m_hPotentialEnemy = -1 (0x103c3bbb)"), Runner->RunnerPotentialEnemy.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19MingXiaoSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.MingXiaoSpawn_0x103927a0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19MingXiaoSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VMingXiao"));
	FElysiumNpcMingXiao* const Ming = F.As<FElysiumNpcMingXiao>();
	if (!TestNotNull(TEXT("Ming Xiao stands"), Ming))
	{
		return false;
	}
	Ming->MingXiaoConnectedTentacleCount = 2;
	Ming->bNeverMeleeOpponent = false;
	Ming->Spawn();
	TestEqual(TEXT("m_iConnectedTentacleCount = 6 (0x10392854)"), Ming->MingXiaoConnectedTentacleCount, 6);
	TestEqual(TEXT("m_iTentacleID = -1 (0x103927e9)"), Ming->MingXiaoTentacleId, static_cast<int32>(INDEX_NONE));
	TestFalse(TEXT("m_rhProxies[5] = -1"), Ming->Proxies[5].IsSet());
	TestEqual(TEXT("m_rflRegrowTimers[0] = FLT_MAX (0x10392842)"), Ming->MingXiaoRegrowTimers[0],
		static_cast<double>(TNumericLimits<float>::Max()));
	TestTrue(TEXT("m_bNeverMeleeOpponent = 1 (0x1039291c)"), Ming->bNeverMeleeOpponent);
	TestEqual(TEXT("m_flFieldOfView = -0.5"), Ming->FieldOfViewDot, -0.5f);
	TestTrue(TEXT("AddMiscFlag(0x80000)"), (Ming->MiscFlags & 0x80000u) != 0u);
	// `0x10392830..0x1039283f`: each limb's hit points = the tuning record's `+0x0` TentacleHPInitial.
	Ming->MingXiaoHitPoints[3] = -1.f;
	Ming->Spawn();
	TestEqual(TEXT("m_rflHitPoints[3] = TentacleHPInitial (0x1039283f)"), Ming->MingXiaoHitPoints[3],
		Ming->Select19MingXiaoTuningField(0));
	TestTrue(TEXT("...a positive record cell"), Ming->MingXiaoHitPoints[0] > 0.f);
	// `0x10392935 -> 0x103986b0`: the ideal range from the connected attack limbs.
	TestEqual(TEXT("m_flIdealRange = 0x103986b0's answer (0x1039293a)"), Ming->MingXiaoIdealRange,
		Ming->MingXiaoIdealRangeFromLimbs());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19TentacleSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.MingXiaoTentacleSpawn_0x1039c380", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19TentacleSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VMingXiaoTentacle"));
	FElysiumNpcMingXiaoTentacle* const Tentacle = F.As<FElysiumNpcMingXiaoTentacle>();
	if (!TestNotNull(TEXT("the tentacle stands"), Tentacle))
	{
		return false;
	}
	Tentacle->bInvincible = false;
	Tentacle->bIgnoreCollisionSpecies = false;
	Tentacle->bTentaclePlayedDeathAnim = true;
	Tentacle->Spawn();
	TestTrue(TEXT("m_bInvincible = 1 (0x1039c3b6)"), Tentacle->bInvincible);
	TestTrue(TEXT("m_bIgnoreCollision = 1 (0x1039c3e5)"), Tentacle->bIgnoreCollisionSpecies);
	TestFalse(TEXT("m_bPlayedDeathAnim = 0 (0x1039c3f3)"), Tentacle->bTentaclePlayedDeathAnim);
	TestEqual(TEXT("m_flIgnoreCollisionTimer = curtime + 5"), Tentacle->TentacleIgnoreCollisionTimer, F.Now() + 5.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19CameraSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.CameraSpawn_0x10368b70", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19CameraSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VCamera"));
	FElysiumNpcCamera* const Camera = F.As<FElysiumNpcCamera>();
	if (!TestNotNull(TEXT("the camera stands"), Camera))
	{
		return false;
	}
	Camera->PlCriminalFlee = 0;
	Camera->EntityFlags2Word = 0u;
	Camera->Spawn();
	TestEqual(TEXT("m_bloodColor = 0xf7 (0x10368bb1)"), Camera->BloodColorWord, 0xf7);
	TestEqual(TEXT("SetSolid(SOLID_NONE)"), Camera->RetailSolidType, 0);
	TestFalse(TEXT("m_hEyeLookTarget = -1 (0x10368d0b)"), Camera->EyeLookTargetHandle.IsSet());
	TestEqual(TEXT("pl_criminal_flee 0 -> 6"), Camera->PlCriminalFlee, 6);
	TestTrue(TEXT("AddFlag2(0x10) (0x10368db2)"), (Camera->EntityFlags2Word & 0x10u) != 0u);
	TestTrue(TEXT("bits_CAP_SQUAD"), Spawn19HasCaps(*Camera, 0x4000000));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19PayphoneSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.PayphoneSpawn_0x101aa9c0", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19PayphoneSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CPayphone"));
	FElysiumNpcPayphone* const Phone = F.As<FElysiumNpcPayphone>();
	if (!TestNotNull(TEXT("the payphone stands"), Phone))
	{
		return false;
	}
	Phone->RetailSolidFlags = 0u;
	Phone->Spawn();
	TestEqual(TEXT("m_iHealth = 80000 (0x101aaa67)"), Phone->Health, 80000);
	TestEqual(TEXT("m_takedamage = 0 (0x101aaa71)"), Phone->TakeDamageMode, 0);
	TestEqual(TEXT("SetSolid(SOLID_BBOX)"), Phone->RetailSolidType, 2);
	TestTrue(TEXT("AddSolidFlags(FSOLID_NOT_SOLID) (0x101aab04)"), (Phone->RetailSolidFlags & 4u) != 0u);
	TestEqual(TEXT("SetMoveType(MOVETYPE_NONE)"), Phone->RetailMoveType, 0);
	TestTrue(TEXT("AddFlag(0x10000)"), (Phone->Flags & 0x10000) != 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19PlayerControllerSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.PlayerControllerSpawn_0x103a4510", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19PlayerControllerSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VPlayerController"));
	FElysiumNpcPlayerController* const Controller = F.As<FElysiumNpcPlayerController>();
	if (!TestNotNull(TEXT("the controller stands"), Controller))
	{
		return false;
	}
	// The landed body (`ElysiumNpcPlayerController.cpp`), now over the retail vampire/human chain.
	TestEqual(TEXT("SetName(playercontroller) (0x103a4544)"), Controller->TargetName,
		FString(TEXT("playercontroller")));
	TestTrue(TEXT("m_bForceFrequentThink = 1 (0x103a4530)"), Controller->bForceFrequentThink);
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	int32 Priority = -1;
	Controller->Relationships.ResolvePersistentRow(FElysiumEntityHandle::Invalid(), TEXT("player"), Value, Priority);
	TestTrue(TEXT("the class-1 row rewritten to D_LI (0x103a4521)"), Value == EElysiumRelationship::Like);
	TestTrue(TEXT("the vampire chain's human caps and 0x40"), Spawn19HasCaps(*Controller, 0xc200d40));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpawn19FrenzyShadowSpawnTest,
	"Elysium.Substrate.NpcKernelSpawn19.FrenzyShadowSpawn_0x10375c50", GSpawn19TestFlags)
bool FElysiumNpcKernelSpawn19FrenzyShadowSpawnTest::RunTest(const FString&)
{
	FSpawn19Fixture F(TEXT("CNPC_VFrenzyShadow"));
	FElysiumNpcFrenzyShadow* const Shadow = F.As<FElysiumNpcFrenzyShadow>();
	if (!TestNotNull(TEXT("the shadow stands"), Shadow))
	{
		return false;
	}
	// `CapabilitiesAdd(0x200000)` BEFORE the controller body (the landed body `ElysiumNpcFrenzyShadow.cpp`).
	TestTrue(TEXT("0x200000 then the controller chain"), Spawn19HasCaps(*Shadow, 0xc200d40));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
