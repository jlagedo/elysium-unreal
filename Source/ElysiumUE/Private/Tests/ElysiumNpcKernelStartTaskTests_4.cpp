// Story 0019/8, family **StartTask19**, lane L04 -- the species `StartTask` overrides (slot 442).
//
// One case per `rule` row, named by retail class and address, under
// `Elysium.Substrate.NpcKernelStartTask19.Species`. Every arm is asserted through what it writes:
// the task status (`fTaskStatus`, `+0x5c44`), the failure word (`+0x5c50`) and `COND_TASK_FAILED`,
// the species words, and the recording seams. The trace stamp a `TaskFail` arm writes (`+0x1b44`
// file / `+0x1b48` line) is the `StartTask fail trace <file>:<line>` row in the mind's trace.
//
// The guard is stood as the retail class a case exercises (`AddNpcOfClass`); nothing reclasses a
// live instance. A task is handed to the body the way the runner hands it: the class-LOCAL id
// translated forward through the class's own task space (slot 580's `+0x18` sub-space), since every
// body translates it back through slot 450 before it switches.
//
// Arms that retail ends by falling into the parent body (`CAI_BaseNPCTroika::StartTask`,
// `CNPC_VHuman::StartTask`, ...) assert only the writes the species arm makes before the call; the
// parent's own effects belong to its lane's suite.
//
// Owns (StartTask19's `rule` rows, lane L04): 0x1035f650 CNPC_VAnimal, 0x103847f0 CNPC_VHuman,
// 0x10392d80 CNPC_VMingXiao, 0x1039c4c0 CNPC_VMingXiaoTentacle, 0x103ba7c0 CNPC_VTzimisce, 0x103c1820
// CNPC_VTzimisceHeadClaw, 0x103c35d0 CNPC_VTzimisceRunner, 0x103ccda0 CNPC_VWerewolf, 0x103645a0
// CNPC_VBach, 0x10374940 CNPC_VDog, 0x103790d0 CNPC_VGargoyle, 0x1037b8b0 CNPC_VGhoulCroucher,
// 0x103805d0 CNPC_VHengeyokai, 0x1038c390 CNPC_VManBat, 0x103a5650 CNPC_VSabbatGunman, 0x103ac740
// CNPC_VScurrying, 0x103b36d0 CNPC_VTaxiDriver, 0x103c5ac0 CNPC_VVampireBoss, 0x103dfd80 CNPC_VZombie,
// 0x1035d1b0 CNPC_VAndreiBlood, 0x103611a0 CNPC_VAsianVampire, 0x1036b750 CNPC_VChangBros, 0x103a78c0
// CNPC_VSabbatLeader, 0x103aec70 CNPC_VSheriffMan, 0x10371b70 CNPC_VCop (Damaged19), and the verified
// 0x10375f50 CNPC_VFrenzyShadow.

#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumLocalIdSpace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFrenzyShadow.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcHuman.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcSabbatGunman.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTaxiDriver.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcZombie.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Substrate/ElysiumScheduleText.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

static constexpr EAutomationTestFlags GStartTask19SpeciesFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One world per case: the guard as `GuardClass`, a combatant `other` (a live entity to aim at),
	// an NPC named `Cop` (the ManBat's fly-by search), and five `info_hint`s -- a plain one (type
	// 10100) and the werewolf's chain: `wwfence` (0x3aa0, the fence leap) naming `wwnext` (15000),
	// `wwcheat` (0x3aa9) and `wwbad` (0x3aa0) naming the plain hint.
	struct FStartTask19SpeciesFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumPlayer* Player = nullptr;

		explicit FStartTask19SpeciesFixture(const TCHAR* GuardClass)
			: World([GuardClass]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("starttask19_species"), 4419);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpcOfClass(TEXT("guard"), FVector::ZeroVector, GuardClass);
					Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
					Builder.AddNpc(TEXT("Cop"), FVector(800.f, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
					Builder.AddEntity(TEXT("info_hint"), TEXT("hint"), FVector(256.f, 128.f, 0.f))
						.Keys.Add(TEXT("hinttype"), TEXT("10100"));
					{
						FElysiumEntityDef& Def = Builder.AddEntity(TEXT("info_hint"), TEXT("wwfence"),
							FVector(300.f, 50.f, 0.f));
						Def.Keys.Add(TEXT("hinttype"), TEXT("15008"));
						Def.Keys.Add(TEXT("target_name"), TEXT("wwnext"));
					}
					Builder.AddEntity(TEXT("info_hint"), TEXT("wwnext"), FVector(500.f, 0.f, 0.f))
						.Keys.Add(TEXT("hinttype"), TEXT("15000"));
					Builder.AddEntity(TEXT("info_hint"), TEXT("wwcheat"), FVector(600.f, 0.f, 0.f))
						.Keys.Add(TEXT("hinttype"), TEXT("15017"));
					{
						FElysiumEntityDef& Def = Builder.AddEntity(TEXT("info_hint"), TEXT("wwbad"),
							FVector(700.f, 0.f, 0.f));
						Def.Keys.Add(TEXT("hinttype"), TEXT("15008"));
						Def.Keys.Add(TEXT("target_name"), TEXT("hint"));
					}
					return Builder;
				}())
		{
			Guard = World.Npc(TEXT("guard"));
			if (Guard == nullptr)
			{
				// A `Spawn` that renames the body (the player-controller line's `SetName`,
				// `0x103a4510`, FrenzyShadow among it) is found by its class instead.
				Guard = World.NpcOfClass(GuardClass);
			}
			Other = World.Npc(TEXT("other"));
			Player = World.Player();
			FElysiumNpcWorldFixture::Quiet({ Guard, Other, World.Npc(TEXT("Cop")) });
		}

		template <class T>
		T* As() const
		{
			return Guard != nullptr ? Guard->AsSpecies<T>() : nullptr;
		}

		// The class-LOCAL task as the GLOBAL id a schedule step carries.
		int32 GlobalTask(int32 LocalTask) const
		{
			const FElysiumLocalIdSpace* Space = Guard->IdSpace(EElysiumIdCategory::Task);
			return Space != nullptr ? Space->LocalToGlobal(LocalTask) : LocalTask;
		}

		// A fresh task state (status NEW, no failure, `COND_TASK_FAILED` clear), then the body.
		void Reset()
		{
			Guard->Schedule.TaskStatus = EElysiumTaskStatus::New;
			Guard->BaseScheduleHost.FailureReason = 0;
			Guard->Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
		}

		int32 Start(int32 LocalTask, float Data = 0.f)
		{
			Reset();
			FElysiumScheduleStep Step;
			Step.TaskId = GlobalTask(LocalTask);
			Step.Data = Data;
			return Guard->StartTaskSlot442(&Step);
		}

		FElysiumScheduleStep StepOf(int32 LocalTask, float Data = 0.f) const
		{
			FElysiumScheduleStep Step;
			Step.TaskId = GlobalTask(LocalTask);
			Step.Data = Data;
			return Step;
		}

		bool Completed() const { return Guard->Schedule.TaskStatus == EElysiumTaskStatus::Complete; }
		bool Running() const { return Guard->Schedule.TaskStatus == EElysiumTaskStatus::New; }

		// The failure the arm raised, 0 when none.
		int32 Failure() const
		{
			return Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed)
				? Guard->BaseScheduleHost.FailureReason : 0;
		}

		// The `+0x1b44`/`+0x1b48` stamp a traced `TaskFail` arm leaves.
		bool Traced(const TCHAR* File, int32 Line) const
		{
			const FString Needle = FString::Printf(TEXT("StartTask fail trace %s:%d"), File, Line);
			return Guard->GetMind().Trace().ContainsByPredicate([&Needle](const FString& Row)
			{
				return Row.Contains(Needle);
			});
		}

		double Now() const { return World.World.NowSeconds(); }

		int32 HintIndex(const TCHAR* Name)
		{
			FElysiumEntity* Hint = World.World.FindByName(Name);
			return Hint != nullptr ? Hint->Handle.Index : INDEX_NONE;
		}

		FElysiumEntity* Entity(const TCHAR* Name) { return World.World.FindByName(Name); }

		// The two weapons the cases hand out, as a case-local item table (the automation process loads
		// no vdata item table; `ElysiumItems::Install` keeps the table by reference, so the fixture owns
		// it and uninstalls it with itself, as the BlockReaction suite does). Bach's rifle keeps retail's
		// classname because `0x103645a0` compares it (`0x105c1c70`); the blade is any melee weapon.
		FElysiumItemTable Items;
		bool bItemsInstalled = false;

		void InstallItems()
		{
			if (bItemsInstalled)
			{
				return;
			}
			bItemsInstalled = true;
			FElysiumItemDef Blade;
			Blade.Classname = TEXT("item_w_st19_blade");
			Blade.PrintName = Blade.Classname;
			Blade.Type = EElysiumItemType::WeaponMelee;
			Blade.bWieldable = true;
			Items.Items.Add(MoveTemp(Blade));
			FElysiumItemDef Rifle;
			Rifle.Classname = TEXT("item_w_rem_m_700_bach");
			Rifle.PrintName = Rifle.Classname;
			Rifle.Type = EElysiumItemType::WeaponFirearm;
			Rifle.bWieldable = true;
			Rifle.AmmoType = TEXT("StartTask19TestRound");
			Rifle.MagazineSize = 5;
			Rifle.DefaultAmmo = 5;
			Items.Items.Add(MoveTemp(Rifle));
			Items.Reindex();
			ElysiumItems::Install(Items);
		}

		~FStartTask19SpeciesFixture()
		{
			if (bItemsInstalled)
			{
				ElysiumItems::Uninstall(Items);
			}
		}

		// The active weapon after `GiveNamedItem(classname)` and, when it did not become active, slot
		// 388 `Weapon_Switch`.
		FElysiumEntity* GiveWeapon(const TCHAR* Classname)
		{
			InstallItems();
			const FElysiumEntityHandle Handle = Guard->Inventory.GiveNamedItem(*Guard, FString(Classname));
			FElysiumEntity* Item = World.World.Resolve(Handle);
			if (Item != nullptr && Guard->ActiveWeaponEntity() != Item)
			{
				Guard->Weapon_Switch(Item, 0);
			}
			return Guard->ActiveWeaponEntity();
		}
	};

	// The `NAI_Hull::Width` a case expects (`0x102d61b0`: maxs.y - mins.y of the full box).
	float StartTask19SpeciesHullWidth(const FElysiumNpcBase& Npc, int32 Hull)
	{
		FVector Mins = FVector::ZeroVector;
		FVector Maxs = FVector::ZeroVector;
		Npc.RetailHullExtents(Hull, FElysiumNpcBase::EElysiumHullExtents::Full, Mins, Maxs);
		return static_cast<float>(Maxs.Y - Mins.Y);
	}
}

// =================================================================================================
// CNPC_VTzimisceHeadClaw -- 0x103c1820
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesHeadClawTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.TzimisceHeadClaw_0x103c1820", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesHeadClawTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VTzimisceHeadClaw"));
	FElysiumNpcTzimisceHeadClaw* Claw = F.As<FElysiumNpcTzimisceHeadClaw>();
	if (!TestNotNull(TEXT("the head claw"), Claw))
	{
		return false;
	}
	// 0x36/0x37: slot 618 -- the exertion (channel 4, 1.0, 0.8, pitch 100) from the fat guy's table.
	for (const int32 Task : { 0x36, 0x37 })
	{
		const int32 Before = Claw->NamedWavEmits.Num();
		F.Start(Task);
		if (TestEqual(FString::Printf(TEXT("0x%x: one exertion"), Task), Claw->NamedWavEmits.Num(), Before + 1))
		{
			const FElysiumNpc::FNamedWavEmit& Emit = Claw->NamedWavEmits.Last();
			TestTrue(TEXT("from TC_FatGuy's exerts"), Emit.Wav.StartsWith(TEXT("character/monster/TC_FatGuy/Exert_Heavy_")));
			TestEqual(TEXT("channel 4"), Emit.Channel, 4);
			TestEqual(TEXT("volume 1.0"), Emit.Volume, 1.f);
			TestEqual(TEXT("attenuation 0.8"), Emit.Attenuation, 0.8f);
			TestEqual(TEXT("pitch 100"), Emit.Pitch, 100);
		}
	}
	// 0x122..0x124: the task is REPLACED by `{0x4b, 1.0}` and handed to the Troika body; the head claw
	// itself emits nothing.
	for (const int32 Task : { 0x122, 0x123, 0x124 })
	{
		const int32 Before = Claw->NamedWavEmits.Num();
		F.Start(Task);
		TestEqual(FString::Printf(TEXT("0x%x: no exertion"), Task), Claw->NamedWavEmits.Num(), Before);
	}
	return true;
}

// =================================================================================================
// CNPC_VTzimisceRunner -- 0x103c35d0
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesRunnerTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.TzimisceRunner_0x103c35d0", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesRunnerTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VTzimisceRunner"));
	FElysiumNpcTzimisceRunner* Runner = F.As<FElysiumNpcTzimisceRunner>();
	if (!TestNotNull(TEXT("the runner"), Runner) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	// 0x122..0x124: `m_flWaitFinished = m_flWaitFinishedDelta + curtime`, no base, no completion.
	Runner->ScheduleHost.WaitFinishedDelta = 2.5f;
	for (const int32 Task : { 0x122, 0x123, 0x124 })
	{
		Runner->BaseScheduleHost.WaitFinished = -1.0;
		F.Start(Task);
		TestEqual(FString::Printf(TEXT("0x%x: the wait is curtime + delta"), Task),
			Runner->BaseScheduleHost.WaitFinished, F.Now() + 2.5);
		TestTrue(FString::Printf(TEXT("0x%x: still running"), Task), F.Running());
	}
	// 0x130: a live potential enemy's origin into `m_vSavePosition` (then the base).
	Runner->RunnerPotentialEnemy = F.Other->Handle;
	Runner->SavePosition = FVector(1.f, 2.f, 3.f);
	F.Start(0x130);
	TestEqual(TEXT("0x130: the save position is the potential enemy's origin"), Runner->SavePosition, F.Other->Origin);
	// 0x36/0x37: the runner's exertion.
	const int32 Before = Runner->NamedWavEmits.Num();
	F.Start(0x36);
	if (TestEqual(TEXT("0x36: one exertion"), Runner->NamedWavEmits.Num(), Before + 1))
	{
		TestTrue(TEXT("from TC_Runner's exerts"),
			Runner->NamedWavEmits.Last().Wav.StartsWith(TEXT("character/monster/TC_Runner/Exert_Heavy_")));
	}
	return true;
}

// =================================================================================================
// CNPC_VTaxiDriver -- 0x103b36d0
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesTaxiTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.TaxiDriver_0x103b36d0", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesTaxiTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VTaxiDriver"));
	FElysiumNpcTaxiDriver* Taxi = F.As<FElysiumNpcTaxiDriver>();
	if (!TestNotNull(TEXT("the taxi driver"), Taxi))
	{
		return false;
	}
	// The sixteen suppressed ids complete at once and never reach the base.
	for (const int32 Task : { 0x2b, 0x2e, 0x2f, 0x31, 0xb2, 0xba, 0xbb, 0xbc, 0xbd, 0xf8, 0xf9, 0xfb, 0xfc, 0xfd,
		0x11b, 0x11d })
	{
		F.Start(Task);
		TestTrue(FString::Printf(TEXT("0x%x completes at once"), Task), F.Completed());
	}
	// 0xb9 outside dialogue: the upkeep answers -1 and the task completes.
	TestFalse(TEXT("the driver is not in dialogue"), Taxi->IsInDialog());
	TestEqual(TEXT("the upkeep's out-of-dialogue answer"), Taxi->RunDialogActivity(), INDEX_NONE);   // 0x102c1400
	F.Start(0xb9);
	TestTrue(TEXT("0xb9 out of dialogue completes"), F.Completed());
	return true;
}

// =================================================================================================
// CNPC_VSabbatGunman -- 0x103a5650
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesGunmanTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.SabbatGunman_0x103a5650", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesGunmanTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VSabbatGunman"));
	FElysiumNpcSabbatGunman* Gunman = F.As<FElysiumNpcSabbatGunman>();
	if (!TestNotNull(TEXT("the gunman"), Gunman))
	{
		return false;
	}
	// `m_flWaitFinishedDelta *= 1 / sabbat_gunman_speed_scalar` (shipped "3.0") before the base.
	const float Scalar = ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::SabbatGunmanSpeedScalar);
	for (const int32 Task : { 0x11b, 0x11c, 0x122, 0x123, 0x124 })
	{
		Gunman->ScheduleHost.WaitFinishedDelta = 6.f;
		F.Start(Task);
		TestEqual(FString::Printf(TEXT("0x%x scales the wait delta"), Task),
			Gunman->ScheduleHost.WaitFinishedDelta, (1.f / Scalar) * 6.f);
	}
	return true;
}

// =================================================================================================
// CNPC_VCop -- 0x10371b70
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesCopTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.Cop_0x10371b70", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesCopTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VCop"));
	FElysiumNpcCop* Cop = F.As<FElysiumNpcCop>();
	if (!TestNotNull(TEXT("the cop"), Cop) || !TestNotNull(TEXT("the player"), F.Player))
	{
		return false;
	}
	F.Start(0x107);
	TestTrue(TEXT("0x107 completes (tail jump)"), F.Completed());
	// 0x14a: the closest player handed to slot 598 by tail jump; no completion of its own.
	Cop->Senses.Memory.ClosestPlayer = F.Player->Handle;
	F.Start(0x14a);
	TestTrue(TEXT("0x14a leaves the task running"), F.Running());
	TestEqual(TEXT("0x14a raises no failure"), F.Failure(), 0);
	return true;
}

// =================================================================================================
// CNPC_VDog -- 0x10374940
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesDogTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.Dog_0x10374940", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesDogTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VDog"));
	FElysiumNpcDog* Dog = F.As<FElysiumNpcDog>();
	if (!TestNotNull(TEXT("the dog"), Dog))
	{
		return false;
	}
	// 0x36: the attack -- completes only when slot 251 `IsActivityFinished` answers true.
	Dog->bSequenceFinished = false;
	F.Start(0x36);
	TestTrue(TEXT("0x36 with the sequence playing stays running"), F.Running());
	Dog->bSequenceFinished = true;
	Dog->IdealSequence = Dog->SequenceNumber;
	F.Start(0x36);
	TestTrue(TEXT("0x36 with the sequence finished completes"), F.Completed());
	// 0xbc: `SetActivity(3)` then the Animal body.
	TestEqual(TEXT("0xbc returns through the Animal body"), F.Start(0xbc), 0);
	return true;
}

// =================================================================================================
// CNPC_VGhoulCroucher -- 0x1037b8b0
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesGhoulTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.GhoulCroucher_0x1037b8b0", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesGhoulTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VGhoulCroucher"));
	FElysiumNpcGhoulCroucher* Ghoul = F.As<FElysiumNpcGhoulCroucher>();
	if (!TestNotNull(TEXT("the ghoul"), Ghoul))
	{
		return false;
	}
	// 0x14a: `RestartIdealActivity(table A)`, and a failure 0x15 when it did not take. The restart
	// (`0x10289ee0` -> `SetIdealActivity 0x10272650`) stores `m_IdealActivity` for every non-zero id,
	// and both tables (`0x1063abcc` / `0x1063abdc`: 0x1059.. / 0x105a..) hold no zero, so the 0x15
	// arm is unreachable here: whatever the ideal was, it is the table's after the restart.
	Ghoul->IdealActivityNumber = Ghoul->UnawareTableA() + 1;
	F.Start(0x14a);
	TestEqual(TEXT("0x14a: the restart stores the ideal"), Ghoul->IdealActivityNumber, Ghoul->UnawareTableA());
	TestEqual(TEXT("0x14a: an ideal that took does not fail"), F.Failure(), 0);
	TestTrue(TEXT("0x14a never completes"), F.Running());
	// 0x14b: the same over table B; the refusal arm (which also sets `m_bUnawareExited` +0x6667,
	// `0x1037b95f`) is likewise unreachable for a non-zero entry.
	Ghoul->bUnawareExited = false;
	Ghoul->IdealActivityNumber = Ghoul->UnawareTableB() + 1;
	F.Start(0x14b);
	TestEqual(TEXT("0x14b: the restart stores the ideal"), Ghoul->IdealActivityNumber, Ghoul->UnawareTableB());
	TestEqual(TEXT("0x14b: no failure"), F.Failure(), 0);
	TestFalse(TEXT("0x14b: the exit flag stays clear"), Ghoul->bUnawareExited);
	return true;
}

// =================================================================================================
// CNPC_VHuman -- 0x103847f0
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesHumanTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.Human_0x103847f0", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesHumanTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VHuman"));
	FElysiumNpcHuman* Human = F.As<FElysiumNpcHuman>();
	if (!TestNotNull(TEXT("the human"), Human))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	// 0x89/0x8a: the attack stamp, `RestartIdealActivity(0x4b)`, no completion.
	for (const int32 Task : { 0x89, 0x8a })
	{
		Human->LastAttackTime = -1.0;
		F.Start(Task);
		TestEqual(FString::Printf(TEXT("0x%x stamps m_flLastAttackTime"), Task), Human->LastAttackTime, F.Now());
		TestTrue(FString::Printf(TEXT("0x%x keeps running"), Task), F.Running());
	}
	// 0x8b (the downed enemy's paired activity, or 0x51), 0x8d/0x8e/0x8f/0x90/0x91: an activity each,
	// no completion, no failure.
	for (const int32 Task : { 0x8b, 0x8d, 0x8e, 0x8f, 0x90, 0x91 })
	{
		F.Start(Task);
		TestTrue(FString::Printf(TEXT("0x%x keeps running"), Task), F.Running());
		TestEqual(FString::Printf(TEXT("0x%x raises nothing"), Task), F.Failure(), 0);
	}
	// 0x9f with no weapon: line 0x138, `TaskFail(3)`.
	F.Start(0x9f, 1.f);
	TestEqual(TEXT("0x9f without a weapon fails 3"), F.Failure(), 3);
	TestTrue(TEXT("stamped at line 0x138"), F.Traced(TEXT("NPC_VHuman.cpp"), 0x138));
	// 0x9f with one: `weapon+0x8c0 * data + 2 * Width(m_eHull)` — a `weapon_melee` record is a
	// `CWeaponMelee`, whose constructor (`0x103e9ac0`) writes 50 into `+0x8c0` (0018 story 8).
	if (TestNotNull(TEXT("a weapon is active"), F.GiveWeapon(TEXT("item_w_st19_blade"))))
	{
		F.Start(0x9f, 1.f);
		const float Width = StartTask19SpeciesHullWidth(*Human, Human->HullKind);
		// `0x1038485b` thunk `0x1001402e` -> `0x102ee1c0` on `m_pNavigator`: the PATH tolerance
		// (`path+0x28`), not `m_flGoalTolerance` (+0x6320).
		TestEqual(TEXT("0x9f: the path tolerance is the melee max word plus twice the hull width"),
			Human->Navigator.GoalToleranceCm, (50.f * 1.f + (Width + Width)) * ElysiumMove::U);
		TestTrue(TEXT("0x9f with a weapon completes"), F.Completed());
	}
	return true;
}

// =================================================================================================
// CNPC_VAnimal -- 0x1035f650
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesAnimalTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.Animal_0x1035f650", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesAnimalTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VAnimal"));
	FElysiumNpcAnimal* Animal = F.As<FElysiumNpcAnimal>();
	if (!TestNotNull(TEXT("the animal"), Animal))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	for (const int32 Task : { 0x89, 0x8a })
	{
		Animal->LastAttackTime = -1.0;
		F.Start(Task);
		TestEqual(FString::Printf(TEXT("0x%x stamps m_flLastAttackTime"), Task), Animal->LastAttackTime, F.Now());
		TestTrue(FString::Printf(TEXT("0x%x keeps running"), Task), F.Running());
	}
	for (const int32 Task : { 0x8b, 0x8e })
	{
		F.Start(Task);
		TestTrue(FString::Printf(TEXT("0x%x keeps running"), Task), F.Running());
	}
	F.Start(0x9f, 1.f);
	TestEqual(TEXT("0x9f without a weapon fails 3"), F.Failure(), 3);
	TestTrue(TEXT("stamped at line 0x165"), F.Traced(TEXT("NPC_VAnimal.cpp"), 0x165));
	// 0xa5 with no interesting place: line 0x19e, `TaskFail(0x22)`.
	TestNull(TEXT("no interesting place"), Animal->CurrentAmbientSpot());
	F.Start(0xa5);
	TestEqual(TEXT("0xa5 without a place fails 0x22"), F.Failure(), 0x22);
	TestTrue(TEXT("stamped at line 0x19e"), F.Traced(TEXT("NPC_VAnimal.cpp"), 0x19e));
	return true;
}

// =================================================================================================
// CNPC_VAndreiBlood -- 0x1035d1b0
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesAndreiTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.AndreiBlood_0x1035d1b0", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesAndreiTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VAndreiBlood"));
	FElysiumNpcAndreiBlood* Andrei = F.As<FElysiumNpcAndreiBlood>();
	if (!TestNotNull(TEXT("Andrei"), Andrei))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	// 0x14b with no hint: line 0x130, `TaskFail(4)`.
	Andrei->BaseScheduleHost.HintNode = INDEX_NONE;
	F.Start(0x14b);
	TestEqual(TEXT("0x14b without a hint fails 4"), F.Failure(), 4);
	TestTrue(TEXT("stamped at line 0x130"), F.Traced(TEXT("NPC_VAndreiBlood.cpp"), 0x130));
	// With one: onto the hint, visible, solid, `m_bTriggerUnhide`, `m_fEffects |= 0x10`, complete.
	Andrei->BaseScheduleHost.HintNode = F.HintIndex(TEXT("hint"));
	Andrei->EffectsWord = 0x20u;
	Andrei->SolidFlagsWord = 0x4u;
	Andrei->bAndreiTriggerUnhide = false;
	F.Start(0x14b);
	TestEqual(TEXT("0x14b: at the hint"), Andrei->Origin, F.Entity(TEXT("hint"))->Origin);
	TestEqual(TEXT("0x14b: EF_NODRAW cleared, 0x10 set"), Andrei->EffectsWord, 0x10u);
	TestEqual(TEXT("0x14b: FSOLID_NOT_SOLID cleared"), Andrei->SolidFlagsWord & 0x4u, 0u);
	TestTrue(TEXT("0x14b: m_bTriggerUnhide"), Andrei->bAndreiTriggerUnhide);
	TestTrue(TEXT("0x14b completes"), F.Completed());
	// 0x150: consumed, nothing.
	F.Start(0x150);
	TestTrue(TEXT("0x150 keeps running"), F.Running());
	// 0x151/0x152: the teleport sounds on channel 2, no completion.
	F.Start(0x151);
	TestEqual(TEXT("0x151: the teleport-out wav"), Andrei->NamedWavEmits.Last().Wav,
		FString(TEXT("Character/Boss/Andrei/TeleportOut.wav")));
	TestEqual(TEXT("0x151: channel 2"), Andrei->NamedWavEmits.Last().Channel, 2);
	TestTrue(TEXT("0x151 keeps running"), F.Running());
	F.Start(0x152);
	TestEqual(TEXT("0x152: the teleport-in wav"), Andrei->NamedWavEmits.Last().Wav,
		FString(TEXT("Character/Boss/Andrei/TeleportIn.wav")));
	TestTrue(TEXT("0x152 keeps running"), F.Running());
	// 0x153: the teleport node into `m_pHintNode`, complete.
	F.Start(0x153);
	TestTrue(TEXT("0x153: the node the selector answered (a hint or none)"),
		Andrei->BaseScheduleHost.HintNode == INDEX_NONE || Andrei->BaseScheduleHost.HintNode >= 0);
	TestTrue(TEXT("0x153 completes"), F.Completed());
	// 0x154: the summon sound, no completion.
	F.Start(0x154);
	TestEqual(TEXT("0x154: the summon wav"), Andrei->NamedWavEmits.Last().Wav,
		FString(TEXT("Character/Boss/Andrei/Summon.wav")));
	TestTrue(TEXT("0x154 keeps running"), F.Running());
	// 0x156: the wait stamp.
	Andrei->AndreiTeleportWaitStartTime = -1.0;
	F.Start(0x156);
	TestEqual(TEXT("0x156 stamps +0x66d0"), Andrei->AndreiTeleportWaitStartTime, F.Now());
	// Any other id: the vampire-boss body, whose prologue stamps +0x669c on every task.
	Andrei->VampireBossTaskStartTime = -1.0;
	F.Start(0x14d);
	TestEqual(TEXT("default: the boss body ran"), Andrei->VampireBossTaskStartTime, F.Now());
	// 0x155 last: `m_OnDeath` then `UTIL_Remove(this)`.
	const int32 Removes = Andrei->UtilRemoveCalls;
	F.Start(0x155);
	TestEqual(TEXT("0x155 removes itself"), Andrei->UtilRemoveCalls, Removes + 1);
	return true;
}

// =================================================================================================
// CNPC_VAsianVampire -- 0x103611a0
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesAsianTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.AsianVampire_0x103611a0", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesAsianTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VAsianVampire"));
	FElysiumNpcAsianVampire* Asian = F.As<FElysiumNpcAsianVampire>();
	if (!TestNotNull(TEXT("the asian vampire"), Asian))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	F.Start(0x150);
	TestTrue(TEXT("0x150 keeps running"), F.Running());
	// 0x151/0x152: the node into `m_pHintNode`; a null one stamps its line and fails 1 -- and BOTH
	// paths reach the shared `TaskComplete(0)`, which a raised failure then refuses.
	const struct { int32 Task; int32 Line; } Rows[] = { { 0x151, 0x110 }, { 0x152, 0x118 } };
	for (const auto& Row : Rows)
	{
		F.Start(Row.Task);
		if (Asian->BaseScheduleHost.HintNode == INDEX_NONE)
		{
			TestEqual(FString::Printf(TEXT("0x%x: no node fails 1"), Row.Task), F.Failure(), 1);
			TestTrue(FString::Printf(TEXT("0x%x: stamped"), Row.Task), F.Traced(TEXT("NPC_VAsianVampire.cpp"), Row.Line));
			TestTrue(FString::Printf(TEXT("0x%x: the completion is refused"), Row.Task), F.Running());
		}
		else
		{
			TestTrue(FString::Printf(TEXT("0x%x: a node completes"), Row.Task), F.Completed());
		}
	}
	Asian->VampireBossTaskStartTime = -1.0;
	F.Start(0x14d);
	TestEqual(TEXT("default: the boss body ran"), Asian->VampireBossTaskStartTime, F.Now());
	return true;
}

// =================================================================================================
// CNPC_VBach -- 0x103645a0
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesBachTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.Bach_0x103645a0", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesBachTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VBach"));
	FElysiumNpcBach* Bach = F.As<FElysiumNpcBach>();
	if (!TestNotNull(TEXT("Bach"), Bach))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	// 0xb0/0xb1 with no weapon: complete.
	for (const int32 Task : { 0xb0, 0xb1 })
	{
		F.Start(Task);
		TestTrue(FString::Printf(TEXT("0x%x without a weapon completes"), Task), F.Completed());
	}
	// 0x34/0x35 with no weapon: `+0x66a4` clear completes; set, the human base runs and then clears it.
	for (const int32 Task : { 0x34, 0x35 })
	{
		Bach->bBachSniperWait = false;
		F.Start(Task);
		TestTrue(FString::Printf(TEXT("0x%x without the sniper wait completes"), Task), F.Completed());
		Bach->bBachSniperWait = true;
		F.Start(Task);
		TestFalse(FString::Printf(TEXT("0x%x: the base ran, then +0x66a4 cleared"), Task), Bach->bBachSniperWait);
	}
	// 0xba..0xbd complete at once.
	for (const int32 Task : { 0xba, 0xbb, 0xbc, 0xbd })
	{
		F.Start(Task);
		TestTrue(FString::Printf(TEXT("0x%x completes"), Task), F.Completed());
	}
	// 0x14a: the rifle (none owned here), `+0x66a4 = 0`, the weapon-switch time curtime + 0.5, complete.
	Bach->bBachSniperWait = true;
	F.Start(0x14a);
	TestFalse(TEXT("0x14a clears +0x66a4"), Bach->bBachSniperWait);
	TestEqual(TEXT("0x14a: +0x668c"), Bach->BachNextWeaponSwitchTime, F.Now() + 0.5);
	TestTrue(TEXT("0x14a completes"), F.Completed());
	Bach->BachNextWeaponSwitchTime = -1.0;
	F.Start(0x14b);
	TestEqual(TEXT("0x14b: +0x668c"), Bach->BachNextWeaponSwitchTime, F.Now() + 0.5);
	TestTrue(TEXT("0x14b completes"), F.Completed());
	// 0x14c: the holy light (0x103656a0), its wav, `+0x66a1 = 0`, complete.
	const int32 HolyBefore = Bach->Fun103656a0Calls;
	Bach->bBachInStartingPosition = true;
	F.Start(0x14c);
	TestEqual(TEXT("0x14c: the holy-light equip"), Bach->Fun103656a0Calls, HolyBefore + 1);
	TestEqual(TEXT("0x14c: the holy-light wav"), Bach->NamedWavEmits.Last().Wav,
		FString(TEXT("Character/Boss/Bach/bach_holy_light.wav")));
	TestFalse(TEXT("0x14c clears +0x66a1"), Bach->bBachInStartingPosition);
	TestTrue(TEXT("0x14c completes"), F.Completed());
	// 0x14d with no weapon: no stat write, complete.
	Bach->BachType3StatWrites.Reset();
	F.Start(0x14d);
	TestEqual(TEXT("0x14d without a weapon writes no stat"), Bach->BachType3StatWrites.Num(), 0);
	TestTrue(TEXT("0x14d completes"), F.Completed());
	// 0x14e: an out-of-range state stamps line 0x350 and fails 4.
	Bach->BachTeleportState = 4;
	F.Start(0x14e);
	TestEqual(TEXT("0x14e: state 4 fails 4"), F.Failure(), 4);
	TestTrue(TEXT("stamped at line 0x350"), F.Traced(TEXT("NPC_VBach.cpp"), 0x350));
	// 0x14f with no hint: line 0x355, `TaskFail(4)`.
	Bach->BaseScheduleHost.HintNode = INDEX_NONE;
	F.Start(0x14f);
	TestEqual(TEXT("0x14f without a hint fails 4"), F.Failure(), 4);
	TestTrue(TEXT("stamped at line 0x355"), F.Traced(TEXT("NPC_VBach.cpp"), 0x355));
	// With one: the teleport, the state advanced (3 wraps to 0), stat 0xd = 5, the shield.
	Bach->BaseScheduleHost.HintNode = F.HintIndex(TEXT("hint"));
	Bach->BachTeleportState = 3;
	Bach->CapabilityWord |= 0x1;
	Bach->bBachMovementSpot = true;
	Bach->BachNextShieldTime = 10.0;
	Bach->BachType3StatWrites.Reset();
	F.Start(0x14f);
	TestEqual(TEXT("0x14f: at the hint"), Bach->Origin, F.Entity(TEXT("hint"))->Origin);
	TestEqual(TEXT("0x14f: capability 1 removed"), Bach->CapabilityWord & 0x1, 0);
	TestFalse(TEXT("0x14f: +0x66a7 cleared"), Bach->bBachMovementSpot);
	TestEqual(TEXT("0x14f: state 3 wraps to 0"), Bach->BachTeleportState, 0);
	TestTrue(TEXT("0x14f: stat 0xd set to 5"), Bach->BachType3StatWrites.Contains(FIntPoint(0xd, 5)));
	TestEqual(TEXT("0x14f: next shield +6"), Bach->BachNextShieldTime, 16.0);
	TestTrue(TEXT("0x14f: shield up"), Bach->bBachShieldActive);
	TestEqual(TEXT("0x14f: shield time curtime + 3"), Bach->BachShieldTime, F.Now() + 3.0);
	TestEqual(TEXT("0x14f: the shield wav"), Bach->NamedWavEmits.Last().Wav,
		FString(TEXT("Character/Boss/Bach/bach_shield.wav")));
	TestTrue(TEXT("0x14f completes"), F.Completed());
	// 0x150: state 1 -> capability, movement spot, hint 0x426c; state 0 -> capability, hint 0x426d;
	// state 2 -> skip-to-warning (1.2) and fail 4 at 0x387; state 3 -> movement spot + the same.
	Bach->BachTeleportState = 1;
	Bach->CapabilityWord &= ~0x1;
	Bach->bBachMovementSpot = false;
	F.Start(0x150);
	TestEqual(TEXT("0x150 state 1: capability 1"), Bach->CapabilityWord & 0x1, 0x1);
	TestTrue(TEXT("0x150 state 1: movement spot"), Bach->bBachMovementSpot);
	Bach->BachTeleportState = 0;
	Bach->CapabilityWord &= ~0x1;
	Bach->bBachMovementSpot = false;
	F.Start(0x150);
	TestEqual(TEXT("0x150 state 0: capability 1"), Bach->CapabilityWord & 0x1, 0x1);
	TestFalse(TEXT("0x150 state 0: no movement spot"), Bach->bBachMovementSpot);
	for (const int32 State : { 2, 3 })
	{
		Bach->BachTeleportState = State;
		Bach->bBachSkipToWarning = false;
		Bach->bBachMovementSpot = false;
		F.Start(0x150);
		TestTrue(FString::Printf(TEXT("0x150 state %d: skip to the warning"), State), Bach->bBachSkipToWarning);
		TestEqual(FString::Printf(TEXT("0x150 state %d: the skip time"), State), Bach->BachSkipToWarningTime, 1.2f);
		TestEqual(FString::Printf(TEXT("0x150 state %d: fails 4"), State), F.Failure(), 4);
		TestTrue(TEXT("stamped at line 0x387"), F.Traced(TEXT("NPC_VBach.cpp"), 0x387));
		TestEqual(FString::Printf(TEXT("0x150 state %d: movement spot only on 3"), State), Bach->bBachMovementSpot, State == 3);
	}
	// 0x151: no movement spot drops capability 1; the rifle; `+0x66a4 = 0`; skip-to-warning at the
	// state's time; complete.
	Bach->BachTeleportState = 1;
	Bach->bBachMovementSpot = false;
	Bach->CapabilityWord |= 0x1;
	Bach->bBachSniperWait = true;
	Bach->bBachSkipToWarning = false;
	F.Start(0x151);
	TestEqual(TEXT("0x151: capability 1 removed"), Bach->CapabilityWord & 0x1, 0);
	TestFalse(TEXT("0x151: +0x66a4 cleared"), Bach->bBachSniperWait);
	TestTrue(TEXT("0x151: skip to the warning"), Bach->bBachSkipToWarning);
	TestEqual(TEXT("0x151: state 1's skip time"), Bach->BachSkipToWarningTime, 1.0f);
	TestTrue(TEXT("0x151 completes"), F.Completed());
	// The rifle arms of 0xb0/0xb1: camper + occluded, camper clear, the aim wait, the skip.
	if (TestNotNull(TEXT("the rifle is active"), F.GiveWeapon(TEXT("item_w_rem_m_700_bach"))))
	{
		Bach->bBachCamperFlag = true;
		Bach->BachWasOccluded = 1;
		F.Start(0xb0);
		TestEqual(TEXT("0xb0 camper occluded: the wait is curtime + 1e9"), Bach->BaseScheduleHost.WaitFinished,
			F.Now() + 1000000000.0);
		TestEqual(TEXT("0xb0 camper occluded: the warning with it"), Bach->BachWarningTime, Bach->BaseScheduleHost.WaitFinished);
		Bach->bBachCamperFlag = true;
		Bach->BachWasOccluded = 0;
		Bach->BachReusedOccludeCount = 3;
		F.Start(0xb0);
		TestEqual(TEXT("0xb0 camper clear: curtime + 0.15"), Bach->BaseScheduleHost.WaitFinished,
			F.Now() + static_cast<double>(0.15f));
		TestFalse(TEXT("0xb0 camper clear: the flag drops"), Bach->bBachCamperFlag);
		TestEqual(TEXT("0xb0 camper clear: the reuse count drops"), Bach->BachReusedOccludeCount, 0);
		Bach->bBachSkipToWarning = false;
		F.Start(0xb1);
		// No enemy: stat 0xc reads its default 1; both feats read 0.
		const double Aim = F.Now() + static_cast<double>(0.2f) + static_cast<double>(1.f * 0.08f);
		TestEqual(TEXT("0xb1 aim: the warning"), Bach->BachWarningTime, Aim);
		TestEqual(TEXT("0xb1 aim: the wait is the warning + 1.5"), Bach->BaseScheduleHost.WaitFinished,
			Aim + static_cast<double>(1.5f));
		Bach->bBachSkipToWarning = true;
		Bach->BachSkipToWarningTime = 1.3f;
		F.Start(0xb1);
		TestEqual(TEXT("0xb1 skip: the warning at curtime + the skip time"), Bach->BachWarningTime,
			F.Now() + static_cast<double>(1.3f));
		TestFalse(TEXT("0xb1 skip: the skip flag drops"), Bach->bBachSkipToWarning);
		TestEqual(TEXT("0xb1 skip: the skip time is spent"), Bach->BachSkipToWarningTime, 0.f);
		// 0x34 holding the rifle with `+0x66a4` clear only completes.
		Bach->bBachSniperWait = false;
		F.Start(0x34);
		TestTrue(TEXT("0x34 aimed: completes"), F.Completed());
		// 0x14d with a weapon: stat 0xf = 1.
		Bach->BachType3StatWrites.Reset();
		F.Start(0x14d);
		TestTrue(TEXT("0x14d with a weapon writes stat 0xf = 1"), Bach->BachType3StatWrites.Contains(FIntPoint(0xf, 1)));
	}
	return true;
}

// =================================================================================================
// CNPC_VChangBros -- 0x1036b750
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesChangTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.ChangBros_0x1036b750", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesChangTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VChangBros"));
	FElysiumNpcChangBros* Chang = F.As<FElysiumNpcChangBros>();
	if (!TestNotNull(TEXT("the Chang"), Chang))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	// 0x13b: sector 3 stamps `+0x66cc`; any other sector leaves it.
	Chang->LastJumpTime = -1.0;
	F.Start(0x13b);
	TestEqual(TEXT("0x13b: +0x66cc only in sector 3"), Chang->LastJumpTime,
		Chang->GetSector(Chang->Origin) == 3 ? F.Now() : -1.0);
	// 0x150: not solid, then falls into 0x151: the teleport-in emitter and `+0x66d0`.
	Chang->SolidFlagsWord = 0u;
	Chang->FacingTime = -1.0;
	int32 Emitters = Chang->ChangEmitterPlacements.Num();
	F.Start(0x150);
	TestEqual(TEXT("0x150: FSOLID_NOT_SOLID"), Chang->SolidFlagsWord & 0x4u, 0x4u);
	TestEqual(TEXT("0x150 falls into 0x151: one emitter"), Chang->ChangEmitterPlacements.Num(), Emitters + 1);
	TestEqual(TEXT("0x150: the teleport-in emitter"), Chang->ChangEmitterPlacements.Last().Name,
		FString(TEXT("chang_teleport_in_emitter")));
	TestEqual(TEXT("0x150: +0x66d0"), Chang->FacingTime, F.Now());
	TestTrue(TEXT("0x150 keeps running"), F.Running());
	Chang->SolidFlagsWord = 0u;
	F.Start(0x151);
	TestEqual(TEXT("0x151 alone leaves the solid flags"), Chang->SolidFlagsWord, 0u);
	// 0x152: the teleport node, complete.
	F.Start(0x152);
	TestTrue(TEXT("0x152: the node the selector answered (a hint or none)"),
		Chang->BaseScheduleHost.HintNode == INDEX_NONE || Chang->BaseScheduleHost.HintNode >= 0);
	TestTrue(TEXT("0x152 completes"), F.Completed());
	// 0x153: ground nav, `m_bJumping = 0`, flags2 & ~0x80000002, the teleport-out emitter.
	Chang->bJumping = true;
	Chang->NpcFlags.SetRawWord2Bits(0x80000002u);
	F.Start(0x153);
	TestFalse(TEXT("0x153: m_bJumping cleared"), Chang->bJumping);
	TestFalse(TEXT("0x153: flags2 0x80000002 cleared"), Chang->NpcFlags.HasRawWord2Bits(0x80000002u));
	TestEqual(TEXT("0x153: the teleport-out emitter"), Chang->ChangEmitterPlacements.Last().Name,
		FString(TEXT("chang_teleport_out_emitter")));
	TestTrue(TEXT("0x153 keeps running"), F.Running());
	// 0x154: the ledge node; none stamps 0x1aa and fails 1, and the shared completion is refused.
	F.Start(0x154);
	if (Chang->BaseScheduleHost.HintNode == INDEX_NONE)
	{
		TestEqual(TEXT("0x154: no ledge fails 1"), F.Failure(), 1);
		TestTrue(TEXT("stamped at line 0x1aa"), F.Traced(TEXT("NPC_VChangBros.cpp"), 0x1aa));
	}
	else
	{
		TestTrue(TEXT("0x154: a ledge completes"), F.Completed());
	}
	F.Start(0x155);
	TestTrue(TEXT("0x155 completes"), F.Completed());
	// 0x156: the charge, `+0x66f0 = curtime + 1.5`.
	Chang->ChangEnergyChargeTime = -1.0;
	F.Start(0x156);
	TestEqual(TEXT("0x156: +0x66f0"), Chang->ChangEnergyChargeTime, F.Now() + 1.5);
	TestTrue(TEXT("0x156 keeps running"), F.Running());
	// 0x157: `+0x66d8 = 0`, the attack stamp.
	Chang->bChangEnergyBallSpawned = true;
	Chang->LastAttackTime = -1.0;
	F.Start(0x157);
	TestFalse(TEXT("0x157: +0x66d8 cleared"), Chang->bChangEnergyBallSpawned);
	TestEqual(TEXT("0x157: m_flLastAttackTime"), Chang->LastAttackTime, F.Now());
	// 0x158 with no hint: line 0x1ce, `TaskFail(1)`.
	Chang->BaseScheduleHost.HintNode = INDEX_NONE;
	F.Start(0x158);
	TestEqual(TEXT("0x158 without a hint fails 1"), F.Failure(), 1);
	TestTrue(TEXT("stamped at line 0x1ce"), F.Traced(TEXT("NPC_VChangBros.cpp"), 0x1ce));
	// 0x159: the united node, complete.
	F.Start(0x159);
	const FElysiumEntity* United = Chang->SelectUnitedNode();
	TestEqual(TEXT("0x159: the united node"), Chang->BaseScheduleHost.HintNode,
		United != nullptr ? United->Handle.Index : INDEX_NONE);
	TestTrue(TEXT("0x159 completes"), F.Completed());
	F.Start(0x15a);
	TestTrue(TEXT("0x15a keeps running"), F.Running());
	// 0x15b: not solid, `+0x66d4 = curtime + 4`.
	Chang->SolidFlagsWord = 0u;
	F.Start(0x15b);
	TestEqual(TEXT("0x15b: FSOLID_NOT_SOLID"), Chang->SolidFlagsWord & 0x4u, 0x4u);
	TestEqual(TEXT("0x15b: +0x66d4"), Chang->ChangUnitedTime, F.Now() + 4.0);
	F.Start(0x15c);
	TestTrue(TEXT("0x15c keeps running"), F.Running());
	// 0x15d: the stamp, solid again; a non-zero `m_ChangType` stops before the blast.
	Chang->ChangType = 1;
	Chang->SolidFlagsWord = 0x4u;
	Emitters = Chang->ChangEmitterPlacements.Num();
	F.Start(0x15d);
	TestEqual(TEXT("0x15d: +0x66ec"), Chang->ChangLastUnitedAttackTime, F.Now());
	TestEqual(TEXT("0x15d: solid"), Chang->SolidFlagsWord & 0x4u, 0u);
	TestEqual(TEXT("0x15d type 1: no blast emitter"), Chang->ChangEmitterPlacements.Num(), Emitters);
	Chang->ChangType = 0;
	Chang->ChangArenaCenter = FVector(100.f, 200.f, 0.f);
	F.Start(0x15d);
	if (TestEqual(TEXT("0x15d type 0: the blast emitter"), Chang->ChangEmitterPlacements.Num(), Emitters + 1))
	{
		const FElysiumNpc::FTeleportEmitterPlacement& Blast = Chang->ChangEmitterPlacements.Last();
		TestEqual(TEXT("0x15d: named"), Blast.Name, FString(TEXT("chang_blast_emitter")));
		TestEqual(TEXT("0x15d: at the arena centre lifted 50"), Blast.PositionUnits,
			FVector(100.f, 200.f, 0.f) / ElysiumMove::U + FVector(0.f, 0.f, 50.f));
	}
	Chang->VampireBossTaskStartTime = -1.0;
	F.Start(0x14d);
	TestEqual(TEXT("default: the boss body ran"), Chang->VampireBossTaskStartTime, F.Now());
	return true;
}

// =================================================================================================
// CNPC_VGargoyle -- 0x103790d0
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesGargoyleTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.Gargoyle_0x103790d0", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesGargoyleTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VGargoyle"));
	FElysiumNpcGargoyle* Gargoyle = F.As<FElysiumNpcGargoyle>();
	if (!TestNotNull(TEXT("the gargoyle"), Gargoyle) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	F.Start(0x12f);
	TestTrue(TEXT("0x12f keeps running"), F.Running());
	// 0x130: no pillar -- nothing written, running; a pillar -- its origin saved, complete.
	Gargoyle->GargoylePillarTarget = FElysiumEntityHandle::Invalid();
	Gargoyle->SavePosition = FVector(1.f, 2.f, 3.f);
	F.Start(0x130);
	TestEqual(TEXT("0x130 without a pillar writes nothing"), Gargoyle->SavePosition, FVector(1.f, 2.f, 3.f));
	TestTrue(TEXT("0x130 without a pillar keeps running"), F.Running());
	Gargoyle->GargoylePillarTarget = F.Other->Handle;
	F.Start(0x130);
	TestEqual(TEXT("0x130: the pillar's origin"), Gargoyle->SavePosition, F.Other->Origin);
	TestTrue(TEXT("0x130 completes"), F.Completed());
	// 0x31: completes only when already facing the ideal.
	F.Start(0x31);
	TestEqual(TEXT("0x31 completes iff FacingIdeal"), F.Completed(), Gargoyle->FacingIdeal());
	// 0xe9/0xea: `m_iDoingGibDeath = 1` first.
	for (const int32 Task : { 0xe9, 0xea })
	{
		Gargoyle->GargoyleDoingGibDeath = 0;
		F.Start(Task);
		TestEqual(FString::Printf(TEXT("0x%x: +0x6684 = 1"), Task), Gargoyle->GargoyleDoingGibDeath, 1);
	}
	// 0x36/0x37: the gargoyle's exertion.
	const int32 Before = Gargoyle->NamedWavEmits.Num();
	F.Start(0x37);
	if (TestEqual(TEXT("0x37: one exertion"), Gargoyle->NamedWavEmits.Num(), Before + 1))
	{
		TestTrue(TEXT("from the gargoyle's exerts"),
			Gargoyle->NamedWavEmits.Last().Wav.StartsWith(TEXT("character/monster/gargoyle/exert_heavy_")));
	}
	return true;
}

// =================================================================================================
// CNPC_VHengeyokai -- 0x103805d0
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesHengeyokaiTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.Hengeyokai_0x103805d0", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesHengeyokaiTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VHengeyokai"));
	FElysiumNpcHengeyokai* Henge = F.As<FElysiumNpcHengeyokai>();
	if (!TestNotNull(TEXT("the hengeyokai"), Henge) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	int32 Before = Henge->Fun103828a0Calls;
	F.Start(0x135);
	TestEqual(TEXT("0x135: the carried-body drop"), Henge->Fun103828a0Calls, Before + 1);
	TestTrue(TEXT("0x135 completes"), F.Completed());
	Before = Henge->HengeyokaiThawCalls;
	F.Start(0x136);
	TestEqual(TEXT("0x136: the thaw"), Henge->HengeyokaiThawCalls, Before + 1);
	TestTrue(TEXT("0x136 completes"), F.Completed());
	Before = Henge->Fun103831c0Calls;
	F.Start(0x14a);
	TestEqual(TEXT("0x14a: the shark form"), Henge->Fun103831c0Calls, Before + 1);
	TestTrue(TEXT("0x14a completes"), F.Completed());
	F.Start(0x14b);
	TestTrue(TEXT("0x14b keeps running"), F.Running());
	// 0x14c: the model swap: `m_fEffects |= 0x10`, hull 0x12, complete.
	Henge->EffectsWord = 0u;
	F.Start(0x14c);
	TestEqual(TEXT("0x14c: 0x10"), Henge->EffectsWord & 0x10u, 0x10u);
	TestEqual(TEXT("0x14c: hull 0x12"), Henge->HullKind, 0x12);
	TestTrue(TEXT("0x14c completes"), F.Completed());
	// 0x14d: `SUB_Remove` think at curtime + 0.01, complete.
	F.Start(0x14d);
	TestEqual(TEXT("0x14d: the next think"), Henge->NextThink, static_cast<float>(F.Now() + static_cast<double>(0.01f)));
	TestTrue(TEXT("0x14d completes"), F.Completed());
	// 0x14e: flags2 |= 0x80000800, complete.
	Henge->NpcFlags.ClearRawWord2Bits(0x80000800u);
	F.Start(0x14e);
	TestTrue(TEXT("0x14e: flags2 0x800"), Henge->NpcFlags.HasRawWord2Bits(0x800u));
	TestTrue(TEXT("0x14e: flags2 bit 31"), Henge->NpcFlags.HasRawWord2Bits(0x80000000u));
	TestTrue(TEXT("0x14e completes"), F.Completed());
	F.Start(0x134);
	TestTrue(TEXT("0x134 keeps running"), F.Running());
	// 0x36/0x37: the exertion.
	Before = Henge->NamedWavEmits.Num();
	F.Start(0x36);
	if (TestEqual(TEXT("0x36: one exertion"), Henge->NamedWavEmits.Num(), Before + 1))
	{
		TestTrue(TEXT("from the hengeyokai's exerts"),
			Henge->NamedWavEmits.Last().Wav.StartsWith(TEXT("character/monster/hengeyokai/exert_heavy_")));
	}
	// 0xc8: completes only once facing.
	F.Start(0xc8);
	TestEqual(TEXT("0xc8 completes iff FacingIdeal"), F.Completed(), Henge->FacingIdeal());
	// 0xc9 without `COND 0x1b`: face, arm the fail timer.
	Henge->Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x1b));
	Henge->HengeyokaiTaskFailTimer = -1.0;
	F.Start(0xc9);
	TestEqual(TEXT("0xc9: +0x6674 = curtime + 1"), Henge->HengeyokaiTaskFailTimer, F.Now() + 1.0);
	TestTrue(TEXT("0xc9 keeps running"), F.Running());
	// 0xca: no fail timer on either path.
	Henge->HengeyokaiTaskFailTimer = -1.0;
	F.Start(0xca);
	TestEqual(TEXT("0xca arms no fail timer"), Henge->HengeyokaiTaskFailTimer, -1.0);
	// 0x130: `m_ePathMode = 2`; a dead target fails 1 at 0x3b3; a live one is saved and completes.
	Henge->HengeyokaiPathMode = 0;
	Henge->HengeyokaiPickupTarget = FElysiumEntityHandle::Invalid();
	F.Start(0x130);
	TestEqual(TEXT("0x130: path mode 2"), Henge->HengeyokaiPathMode, 2);
	TestEqual(TEXT("0x130 without a target fails 1"), F.Failure(), 1);
	TestTrue(TEXT("stamped at line 0x3b3"), F.Traced(TEXT("NPC_VHengeyokai.cpp"), 0x3b3));
	Henge->HengeyokaiPickupTarget = F.Other->Handle;
	F.Start(0x130);
	TestEqual(TEXT("0x130: the target's origin"), Henge->SavePosition, F.Other->Origin);
	TestTrue(TEXT("0x130 completes"), F.Completed());
	// 0x132/0x133: the carry facing, no completion.
	for (const int32 Task : { 0x132, 0x133 })
	{
		Before = Henge->Fun10382bb0Calls;
		F.Start(Task);
		TestEqual(FString::Printf(TEXT("0x%x: the carry facing"), Task), Henge->Fun10382bb0Calls, Before + 1);
		TestTrue(FString::Printf(TEXT("0x%x keeps running"), Task), F.Running());
	}
	return true;
}

// =================================================================================================
// CNPC_VManBat -- 0x1038c390
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesManBatTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.ManBat_0x1038c390", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesManBatTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VManBat"));
	FElysiumNpcManBat* Bat = F.As<FElysiumNpcManBat>();
	if (!TestNotNull(TEXT("the manbat"), Bat) || !TestNotNull(TEXT("the player"), F.Player))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	F.Start(0x14a);
	TestEqual(TEXT("0x14a: ideal 0x28"), Bat->IdealActivityNumber, 0x28);
	int32 Flaps = Bat->ManBatFlapSelectorCalls.Num();
	F.Start(0x14b);
	TestEqual(TEXT("0x14b: steer, then the flap selector"), Bat->ManBatFlapSelectorCalls.Num(), Flaps + 1);
	// 0x14c: release, ground mode 2, mode 5, ideal 0x2f.
	const int32 Releases = Bat->Fun1038f660Calls;
	F.Start(0x14c);
	TestEqual(TEXT("0x14c: the release"), Bat->Fun1038f660Calls, Releases + 1);
	TestEqual(TEXT("0x14c: flight switch 2"), Bat->ManBatFlightSwitchCalls.Last(), 2);
	TestEqual(TEXT("0x14c: mode 5"), Bat->ManBatMoveGoalNodeMode, 5);
	TestEqual(TEXT("0x14c: ideal 0x2f"), Bat->IdealActivityNumber, 0x2f);
	// 0x14d: speed 500 on the horizontal, a climb of 0.1..0.5 of it.
	Flaps = Bat->ManBatFlapSelectorCalls.Num();
	F.Start(0x14d);
	const FVector Velocity = Bat->Velocity / ElysiumMove::U;
	TestTrue(TEXT("0x14d: horizontal speed 500"), FMath::IsNearlyEqual(static_cast<float>(Velocity.Size2D()), 500.f, 0.5f));
	TestTrue(TEXT("0x14d: climb 50..250"), Velocity.Z >= 50.0 - 0.01 && Velocity.Z <= 250.0 + 0.01);
	TestEqual(TEXT("0x14d: the flap selector"), Bat->ManBatFlapSelectorCalls.Num(), Flaps + 1);
	// The fly-node searches answer null here (family Bosses' seam): each arm's mode and its fail 4.
	const struct { int32 Task; int32 Mode; } Searches[] = { { 0x14e, 1 }, { 0x151, 2 }, { 0x152, 3 }, { 0x155, 4 } };
	for (const auto& Row : Searches)
	{
		F.Start(Row.Task);
		TestEqual(FString::Printf(TEXT("0x%x: mode %d"), Row.Task, Row.Mode), Bat->ManBatMoveGoalNodeMode, Row.Mode);
		TestEqual(FString::Printf(TEXT("0x%x: no fly node fails 4"), Row.Task), F.Failure(), 4);
	}
	TestTrue(TEXT("0x151: node id 1..3"), Bat->ManBatMoveGoalNodeId >= 1 && Bat->ManBatMoveGoalNodeId <= 3);
	// 0x14f with no fly node: mode 0, node id 1, still none -> fail 4.
	Bat->ManBatFlyNode = FElysiumEntityHandle::Invalid();
	F.Start(0x14f);
	TestEqual(TEXT("0x14f: mode 0"), Bat->ManBatMoveGoalNodeMode, 0);
	TestEqual(TEXT("0x14f: node id 1"), Bat->ManBatMoveGoalNodeId, 1);
	TestEqual(TEXT("0x14f: fails 4"), F.Failure(), 4);
	// 0x14f with a fly node: the next node id, complete.
	Bat->ManBatFlyNode = F.Other->Handle;
	Bat->ManBatMoveGoalNodeId = 2;
	Bat->bManBatReachedMoveGoal = true;
	F.Start(0x14f);
	TestEqual(TEXT("0x14f: node id + 1"), Bat->ManBatMoveGoalNodeId, 3);
	TestFalse(TEXT("0x14f: +0x6664 cleared"), Bat->bManBatReachedMoveGoal);
	TestTrue(TEXT("0x14f completes"), F.Completed());
	F.Start(0x150);
	TestEqual(TEXT("0x150: ideal 0x30"), Bat->IdealActivityNumber, 0x30);
	TestEqual(TEXT("0x150: velocity zero"), Bat->Velocity, FVector::ZeroVector);
	// 0x153: the throw; the port's `ThrowModel` answers the arm.
	F.Start(0x153);
	TestTrue(TEXT("0x153 completes or fails 1"), F.Completed() || F.Failure() == 1);
	// 0x154: a node id 1..3 different from the last, fail 4 with no node.
	Bat->ManBatMoveGoalNodeId = 2;
	F.Start(0x154);
	TestTrue(TEXT("0x154: a different node id"), Bat->ManBatMoveGoalNodeId != 2 && Bat->ManBatMoveGoalNodeId >= 1
		&& Bat->ManBatMoveGoalNodeId <= 3);
	TestEqual(TEXT("0x154 fails 4"), F.Failure(), 4);
	// 0x156: face the closest player (pitch 0), ideal 0xb0.
	Bat->Senses.Memory.ClosestPlayer = F.Player->Handle;
	F.Start(0x156);
	TestEqual(TEXT("0x156: pitch 0"), Bat->Angles.X, 0.0);
	TestEqual(TEXT("0x156: ideal 0xb0"), Bat->IdealActivityNumber, 0xb0);
	// 0x157: mode 6, no fly node, running.
	Bat->ManBatFlyNode = F.Other->Handle;
	F.Start(0x157);
	TestEqual(TEXT("0x157: mode 6"), Bat->ManBatMoveGoalNodeMode, 6);
	TestFalse(TEXT("0x157: fly node cleared"), Bat->ManBatFlyNode.IsSet());
	TestTrue(TEXT("0x157 keeps running"), F.Running());
	F.Start(0x158);
	TestEqual(TEXT("0x158: ideal 0x1170"), Bat->IdealActivityNumber, 0x1170);
	for (const int32 Task : { 0x159, 0x162 })
	{
		Bat->ManBatFlyByTarget = FElysiumEntityHandle::Invalid();
		F.Start(Task);
		TestEqual(FString::Printf(TEXT("0x%x: mode 7"), Task), Bat->ManBatMoveGoalNodeMode, 7);
		TestTrue(FString::Printf(TEXT("0x%x: the fly-by target is the closest player"), Task),
			Bat->ManBatFlyByTarget == F.Player->Handle);
	}
	for (const int32 Task : { 0x15a, 0x161 })
	{
		F.Start(Task);
		TestEqual(FString::Printf(TEXT("0x%x: ideal 0x4b"), Task), Bat->IdealActivityNumber, 0x4b);
	}
	F.Start(0x15b);
	TestEqual(TEXT("0x15b: ideal 0x1054"), Bat->IdealActivityNumber, 0x1054);
	// 0x15c: mode 8, node 1, no node -> fail 4, then FALLS INTO 0x15d (lift 10, completion refused).
	FVector Before = Bat->Origin;
	F.Start(0x15c);
	TestEqual(TEXT("0x15c: mode 8"), Bat->ManBatMoveGoalNodeMode, 8);
	TestEqual(TEXT("0x15c: fails 4"), F.Failure(), 4);
	TestEqual(TEXT("0x15c falls into 0x15d: lifted 10"), Bat->Origin.Z, Before.Z + 10.0 * ElysiumMove::U, 0.01);
	TestTrue(TEXT("0x15c: the completion is refused"), F.Running());
	Before = Bat->Origin;
	F.Start(0x15d);
	TestEqual(TEXT("0x15d: lifted 10"), Bat->Origin.Z, Before.Z + 10.0 * ElysiumMove::U, 0.01);
	TestTrue(TEXT("0x15d completes"), F.Completed());
	int32 Count = Bat->Fun1038fc80Calls;
	F.Start(0x15e);
	TestEqual(TEXT("0x15e: 0x1038fc80"), Bat->Fun1038fc80Calls, Count + 1);
	TestTrue(TEXT("0x15e completes"), F.Completed());
	Count = Bat->Fun1038fd40Calls;
	F.Start(0x15f);
	TestEqual(TEXT("0x15f: 0x1038fd40"), Bat->Fun1038fd40Calls, Count + 1);
	TestTrue(TEXT("0x15f completes"), F.Completed());
	// 0x160: the nearest entity named "Cop".
	F.Start(0x160);
	TestTrue(TEXT("0x160: the fly-by target is Cop"), Bat->ManBatFlyByTarget == F.Entity(TEXT("Cop"))->Handle);
	TestEqual(TEXT("0x160: mode 7"), Bat->ManBatMoveGoalNodeMode, 7);
	TestTrue(TEXT("0x160 keeps running"), F.Running());
	// 0x163: mode 9, the coast timer curtime + 1.
	F.Start(0x163);
	TestEqual(TEXT("0x163: mode 9"), Bat->ManBatMoveGoalNodeMode, 9);
	TestEqual(TEXT("0x163: +0x66b4"), Bat->ManBatCoastTimer, F.Now() + 1.0);
	Bat->bHasPlayedFlyBySound = true;
	F.Start(0x164);
	TestFalse(TEXT("0x164: the fly-by sound flag cleared"), Bat->bHasPlayedFlyBySound);
	TestTrue(TEXT("0x164 completes"), F.Completed());
	return true;
}

// =================================================================================================
// CNPC_VMingXiao -- 0x10392d80
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesMingXiaoTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.MingXiao_0x10392d80", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesMingXiaoTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VMingXiao"));
	FElysiumNpcMingXiao* Ming = F.As<FElysiumNpcMingXiao>();
	if (!TestNotNull(TEXT("Ming Xiao"), Ming))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	for (const int32 Task : { 0x89, 0x8a })
	{
		Ming->LastAttackTime = -1.0;
		F.Start(Task);
		TestEqual(FString::Printf(TEXT("0x%x stamps m_flLastAttackTime"), Task), Ming->LastAttackTime, F.Now());
	}
	for (const int32 Task : { 0x8b, 0x8e, 0x14b, 0x153, 0x154, 0x158, 0x15a, 0x15b, 0x15c })
	{
		F.Start(Task);
		TestTrue(FString::Printf(TEXT("0x%x keeps running"), Task), F.Running());
	}
	F.Start(0x9f, 1.f);
	TestEqual(TEXT("0x9f without a weapon fails 3"), F.Failure(), 3);
	TestTrue(TEXT("stamped at line 0x296"), F.Traced(TEXT("NPC_VMingXiao.cpp"), 0x296));
	int32 Before = Ming->Fun1039a750Calls;
	F.Start(0x14a);
	TestEqual(TEXT("0x14a: the transform"), Ming->Fun1039a750Calls, Before + 1);
	TestTrue(TEXT("0x14a completes"), F.Completed());
	Ming->bMingXiaoHasTransformed = false;
	Ming->EffectsWord = 0u;
	F.Start(0x14c);
	TestEqual(TEXT("0x14c: hull 0xf"), Ming->HullKind, 0xf);
	TestTrue(TEXT("0x14c: +0x6678"), Ming->bMingXiaoHasTransformed);
	TestEqual(TEXT("0x14c: 0x10"), Ming->EffectsWord & 0x10u, 0x10u);
	TestTrue(TEXT("0x14c completes"), F.Completed());
	F.Start(0x14d);
	TestEqual(TEXT("0x14d: the next think"), Ming->NextThink, static_cast<float>(F.Now() + static_cast<double>(0.01f)));
	TestTrue(TEXT("0x14d completes"), F.Completed());
	// `0x10395ce0` `BeginDefeatSequenceOnce` (lane L12): the latch `+0x6744` gates `0x10395c70` once.
	Ming->bMingXiaoPlayedDeathAnim = false;
	F.Start(0x14e);
	TestTrue(TEXT("0x14e: the defeat sequence latches"), Ming->bMingXiaoPlayedDeathAnim);
	TestTrue(TEXT("0x14e completes"), F.Completed());
	for (const int32 Task : { 0x14f, 0x150, 0x151, 0x152 })
	{
		TestEqual(FString::Printf(TEXT("0x%x: the throw attack"), Task), F.Start(Task), 0);
	}
	// 0x155: the spit timer, no completion.
	Ming->MingXiaoSpitAttackTimer = -1.0;
	F.Start(0x155);
	TestTrue(TEXT("0x155: the spit timer is at or after curtime"), Ming->MingXiaoSpitAttackTimer >= F.Now());
	TestTrue(TEXT("0x155 keeps running"), F.Running());
	// 0x156: the object mode from the data word (0..4, anything else 0), complete.
	F.Start(0x156, 2.f);
	TestTrue(TEXT("0x156 completes"), F.Completed());
	F.Start(0x156, 7.f);
	TestTrue(TEXT("0x156 out of range completes"), F.Completed());
	// 0x157: the goal's answer is the return; never a completion.
	F.Start(0x157);
	TestTrue(TEXT("0x157 never completes"), F.Running());
	Ming->MingXiaoThrowableObjectMode = 0;
	F.Start(0x159);
	TestTrue(TEXT("0x159 completes"), F.Completed());
	// 0x15d..0x15f: the emitters.
	F.Start(0x15d);
	TestEqual(TEXT("0x15d: the damage emitter"), Ming->MingXiaoEmitters.Last(),
		FString(TEXT("Ming_xiao_tentacle_damage_emitter")));
	TestTrue(TEXT("0x15d completes"), F.Completed());
	F.Start(0x15e);
	TestEqual(TEXT("0x15e: the second death emitter at Bip01 Spine6"), Ming->MingXiaoEmitters.Last(),
		FString(TEXT("Ming_xiao_death_emitter2@Bip01 Spine6")));
	TestTrue(TEXT("0x15e completes"), F.Completed());
	F.Start(0x15f);
	TestEqual(TEXT("0x15f: the second proxy emitter"), Ming->MingXiaoEmitters.Last(),
		FString(TEXT("Ming_xiao_death_proxy_emitter2@Bip01 Spine6")));
	TestTrue(TEXT("0x15f completes"), F.Completed());
	return true;
}

// =================================================================================================
// CNPC_VMingXiaoTentacle -- 0x1039c4c0
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesTentacleTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.MingXiaoTentacle_0x1039c4c0", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesTentacleTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VMingXiaoTentacle"));
	FElysiumNpcMingXiaoTentacle* Tentacle = F.As<FElysiumNpcMingXiaoTentacle>();
	if (!TestNotNull(TEXT("the tentacle"), Tentacle) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	for (const int32 Task : { 0x8b, 0x8e, 0x14c, 0x150, 0x154 })
	{
		F.Start(Task);
		TestTrue(FString::Printf(TEXT("0x%x keeps running"), Task), F.Running());
	}
	F.Start(0x9f, 1.f);
	TestEqual(TEXT("0x9f without a weapon fails 3"), F.Failure(), 3);
	TestTrue(TEXT("stamped at line 0x181"), F.Traced(TEXT("NPC_VMingXiaoTentacle.cpp"), 0x181));
	// 0x14a: the phase machine.
	Tentacle->TentaclePhase = 0;
	int32 Teardowns = Tentacle->Fun1039f310Calls;
	Tentacle->MingXiaoTentaclePhaseExpireTimer = 5.0;
	F.Start(0x14a, 1.f);
	TestEqual(TEXT("0x14a 1: the teardown"), Tentacle->Fun1039f310Calls, Teardowns + 1);
	TestEqual(TEXT("0x14a 1: phase 1"), Tentacle->TentaclePhase, 1);
	TestTrue(TEXT("0x14a 1: invincible"), Tentacle->bInvincible);
	TestEqual(TEXT("0x14a 1: the timer cleared"), Tentacle->MingXiaoTentaclePhaseExpireTimer, 0.0);
	TestTrue(TEXT("0x14a completes"), F.Completed());
	F.Start(0x14a, 1.f);
	TestEqual(TEXT("0x14a same phase: no teardown"), Tentacle->Fun1039f310Calls, Teardowns + 1);
	TestTrue(TEXT("0x14a same phase completes"), F.Completed());
	F.Start(0x14a, 2.f);
	TestEqual(TEXT("0x14a 2: phase 2"), Tentacle->TentaclePhase, 2);
	TestFalse(TEXT("0x14a 2: vulnerable"), Tentacle->bInvincible);
	F.Start(0x14a, 3.f);
	TestEqual(TEXT("0x14a 3: phase 3"), Tentacle->TentaclePhase, 3);
	TestTrue(TEXT("0x14a 3: invincible"), Tentacle->bInvincible);
	F.Start(0x14a, 7.f);
	TestEqual(TEXT("0x14a other: phase 0"), Tentacle->TentaclePhase, 0);
	TestTrue(TEXT("0x14a other: invincible"), Tentacle->bInvincible);
	// 0x14b: the three forms.
	int32 Emitters = Tentacle->TentacleEmitterPlacements.Num();
	F.Start(0x14b, 2.f);
	TestEqual(TEXT("0x14b 2: hull 0x11"), Tentacle->HullKind, 0x11);
	TestEqual(TEXT("0x14b 2: pathing hull 0x11"), Tentacle->PathingHullKind, 0x11);
	TestEqual(TEXT("0x14b 2: no emitter"), Tentacle->TentacleEmitterPlacements.Num(), Emitters);
	TestTrue(TEXT("0x14b 2 completes"), F.Completed());
	F.Start(0x14b, 3.f);
	TestEqual(TEXT("0x14b 3: hull 0xf"), Tentacle->HullKind, 0xf);
	TestEqual(TEXT("0x14b 3: the baby-transform emitter"), Tentacle->TentacleEmitterPlacements.Last().Name,
		FString(TEXT("Ming_xiao_baby_transform_emitter")));
	F.Start(0x14b, 0.f);
	TestEqual(TEXT("0x14b other: hull 0x11"), Tentacle->HullKind, 0x11);
	TestEqual(TEXT("0x14b other: the tentacle-transform emitter"), Tentacle->TentacleEmitterPlacements.Last().Name,
		FString(TEXT("Ming_xiao_tentacle_transform_emitter")));
	TestEqual(TEXT("0x14b: at this body's origin"), Tentacle->TentacleEmitterPlacements.Last().PositionUnits,
		Tentacle->Origin / ElysiumMove::U);
	F.Start(0x14d);
	TestTrue(TEXT("0x14d completes"), F.Completed());
	int32 Count = Tentacle->Fun1039ef10Calls;
	F.Start(0x14e);
	TestEqual(TEXT("0x14e: 0x1039ef10"), Tentacle->Fun1039ef10Calls, Count + 1);
	TestTrue(TEXT("0x14e completes"), F.Completed());
	// 0x14f: no enemy -> fail 6 at 0x200; an enemy but no node -> fail 7 at 0x20b.
	ElysiumNpcEnemy::SetEnemy(*Tentacle, FElysiumEntityHandle::Invalid());
	F.Start(0x14f);
	TestEqual(TEXT("0x14f without an enemy fails 6"), F.Failure(), 6);
	TestTrue(TEXT("stamped at line 0x200"), F.Traced(TEXT("NPC_VMingXiaoTentacle.cpp"), 0x200));
	ElysiumNpcEnemy::SetEnemy(*Tentacle, F.Other->Handle);
	F.Start(0x14f);
	TestEqual(TEXT("0x14f without a node fails 7"), F.Failure(), 7);
	TestTrue(TEXT("stamped at line 0x20b"), F.Traced(TEXT("NPC_VMingXiaoTentacle.cpp"), 0x20b));
	// 0x151: no companion -> fail 1 at 0x2a0.
	F.Start(0x151);
	TestEqual(TEXT("0x151 without a companion fails 1"), F.Failure(), 1);
	TestTrue(TEXT("stamped at line 0x2a0"), F.Traced(TEXT("NPC_VMingXiaoTentacle.cpp"), 0x2a0));
	// 0x152: no node near the scatter centre -> fail 7 at 0x2c6.
	F.Start(0x152);
	TestEqual(TEXT("0x152 without a node fails 7"), F.Failure(), 7);
	TestTrue(TEXT("stamped at line 0x2c6"), F.Traced(TEXT("NPC_VMingXiaoTentacle.cpp"), 0x2c6));
	F.Start(0x153);
	TestTrue(TEXT("0x153 completes"), F.Completed());
	Emitters = Tentacle->TentacleEmitterPlacements.Num();
	F.Start(0x155);
	TestEqual(TEXT("0x155: the death emitter"), Tentacle->TentacleEmitterPlacements.Last().Name,
		FString(TEXT("Ming_xiao_baby_death_emitter")));
	TestEqual(TEXT("0x155: one emitter"), Tentacle->TentacleEmitterPlacements.Num(), Emitters + 1);
	TestTrue(TEXT("0x155 completes"), F.Completed());
	// `0x1039ea60` `BeginTentacleDefeatOnce` (lane L12): the latch gates the death entry once.
	Tentacle->bTentaclePlayedDeathAnim = false;
	F.Start(0x156);
	TestTrue(TEXT("0x156: 0x1039ea60 latches"), Tentacle->bTentaclePlayedDeathAnim);
	TestTrue(TEXT("0x156 completes"), F.Completed());
	return true;
}

// =================================================================================================
// CNPC_VSabbatLeader -- 0x103a78c0
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesSabbatLeaderTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.SabbatLeader_0x103a78c0", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesSabbatLeaderTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VSabbatLeader"));
	FElysiumNpcSabbatLeader* Leader = F.As<FElysiumNpcSabbatLeader>();
	if (!TestNotNull(TEXT("the leader"), Leader))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	// The prologue: a spawned nova particle outlives only 0x161/0x162.
	Leader->bSabbatLeaderParticleSpawned = true;
	F.Start(0x155);
	TestFalse(TEXT("prologue: any other task kills the particle"), Leader->bSabbatLeaderParticleSpawned);
	// 0x14e: activated, a boss monster, the attack stamp (then the base).
	Leader->bSabbatLeaderActivated = false;
	Leader->bIsBossMonster = false;
	F.Start(0x14e);
	TestTrue(TEXT("0x14e: +0x66b8"), Leader->bSabbatLeaderActivated);
	TestTrue(TEXT("0x14e: m_bIsBossMonster"), Leader->bIsBossMonster);
	// 0x154: the dive-in point; none fails 1 at 0x25a.
	F.Start(0x154);
	if (Leader->BaseScheduleHost.HintNode == INDEX_NONE)
	{
		TestEqual(TEXT("0x154: no point fails 1"), F.Failure(), 1);
		TestTrue(TEXT("stamped at line 0x25a"), F.Traced(TEXT("NPC_VSabbatLeader.cpp"), 0x25a));
	}
	else
	{
		TestTrue(TEXT("0x154: a point completes"), F.Completed());
	}
	for (const int32 Task : { 0x155, 0x156 })
	{
		F.Start(Task);
		TestTrue(FString::Printf(TEXT("0x%x completes"), Task), F.Completed());
	}
	F.Start(0x157);
	TestEqual(TEXT("0x157: the ambient run loop"), Leader->NamedWavEmits.Last().Wav,
		FString(TEXT("Character/Monster/Andrei_Transformed/ambient_run.wav")));
	TestTrue(TEXT("0x157 completes"), F.Completed());
	F.Start(0x158);
	TestEqual(TEXT("0x158: the loop stopped on channel 2"), Leader->SabbatStoppedSounds.Last(),
		FString(TEXT("2:Character/Monster/Andrei_Transformed/ambient_run.wav")));
	TestTrue(TEXT("0x158 completes"), F.Completed());
	// 0x15a: the dive-jump set-up at the hint.
	Leader->BaseScheduleHost.HintNode = F.HintIndex(TEXT("hint"));
	Leader->bSabbatLeaderTrackPlayer = true;
	F.Start(0x15a);
	TestEqual(TEXT("0x15a: the jump origin"), Leader->JumpOrigin, Leader->Origin / ElysiumMove::U);
	TestEqual(TEXT("0x15a: the jump target"), Leader->JumpTarget, F.Entity(TEXT("hint"))->Origin / ElysiumMove::U);
	TestEqual(TEXT("0x15a: height 200"), Leader->JumpHeight, 200.f);
	TestFalse(TEXT("0x15a: not tracking"), Leader->bSabbatLeaderTrackPlayer);
	TestTrue(TEXT("0x15a completes"), F.Completed());
	// 0x15b with no closest player: fail 1 at 0x28d.
	Leader->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	F.Start(0x15b);
	TestEqual(TEXT("0x15b without a player fails 1"), F.Failure(), 1);
	TestTrue(TEXT("stamped at line 0x28d"), F.Traced(TEXT("NPC_VSabbatLeader.cpp"), 0x28d));
	// 0x15c: the dive in.
	Leader->EffectsWord = 0u;
	Leader->SolidFlagsWord = 0u;
	F.Start(0x15c);
	TestEqual(TEXT("0x15c: gravity 0.1"), Leader->Gravity, 0.1f);
	TestTrue(TEXT("0x15c: diving"), Leader->bSabbatDiving);
	TestEqual(TEXT("0x15c: EF_NODRAW"), Leader->EffectsWord & 0x20u, 0x20u);
	TestEqual(TEXT("0x15c: not solid"), Leader->SolidFlagsWord & 0x4u, 0x4u);
	TestTrue(TEXT("0x15c: jumping"), Leader->bJumping);
	TestTrue(TEXT("0x15c keeps running"), F.Running());
	// 0x15d: the dive out.
	F.Start(0x15d);
	TestFalse(TEXT("0x15d: not diving"), Leader->bSabbatDiving);
	TestEqual(TEXT("0x15d: 0x10"), Leader->EffectsWord & 0x10u, 0x10u);
	TestTrue(TEXT("0x15d keeps running"), F.Running());
	// 0x15e: onto the hint, the warning. 0x15b's failure released the hint (Troika `TaskFail
	// 0x1029adb0` -> `ClearScheduleHint`), so it is stood again.
	Leader->BaseScheduleHost.HintNode = F.HintIndex(TEXT("hint"));
	F.Start(0x15e);
	TestEqual(TEXT("0x15e: at the hint"), Leader->Origin, F.Entity(TEXT("hint"))->Origin);
	TestEqual(TEXT("0x15e: +0x66dc"), Leader->SabbatWarningFinishTime, F.Now() + 1.0);
	TestEqual(TEXT("0x15e: the warning wav"), Leader->NamedWavEmits.Last().Wav,
		FString(TEXT("Character/Monster/Andrei_Transformed/splash_warning.wav")));
	TestTrue(TEXT("0x15e keeps running"), F.Running());
	Leader->NpcFlags.ClearRawWord2Bits(0x80000800u);
	F.Start(0x15f);
	TestTrue(TEXT("0x15f: flags2 0x800"), Leader->NpcFlags.HasRawWord2Bits(0x800u));
	TestTrue(TEXT("0x15f completes"), F.Completed());
	F.Start(0x160);
	TestEqual(TEXT("0x160: the roar"), Leader->NamedWavEmits.Last().Wav,
		FString(TEXT("Character/Monster/Andrei_Transformed/roar_1.wav")));
	TestTrue(TEXT("0x160 keeps running"), F.Running());
	// 0x161/0x162: the nova; the particle survives 0x162's prologue.
	F.Start(0x161);
	TestTrue(TEXT("0x161: the last attack was a nova"), Leader->bSabbatLeaderLastAttackWasNova);
	TestTrue(TEXT("0x161: the particle is up"), Leader->bSabbatLeaderParticleSpawned);
	F.Start(0x162);
	TestTrue(TEXT("0x162 keeps the particle"), Leader->bSabbatLeaderParticleSpawned);
	// 0x163: the blast emitter 80 above, the attack stamp; the prologue killed the particle.
	Leader->LastAttackTime = -1.0;
	F.Start(0x163);
	TestFalse(TEXT("0x163's prologue kills the particle"), Leader->bSabbatLeaderParticleSpawned);
	if (TestTrue(TEXT("0x163: an emitter"), Leader->SabbatEmitterPlacements.Num() > 0))
	{
		TestEqual(TEXT("0x163: the blast emitter"), Leader->SabbatEmitterPlacements.Last().Name,
			FString(TEXT("Andrei_blast_emitter")));
		TestEqual(TEXT("0x163: 80 above"), Leader->SabbatEmitterPlacements.Last().PositionUnits,
			Leader->Origin / ElysiumMove::U + FVector(0.f, 0.f, 80.f));
	}
	TestEqual(TEXT("0x163: m_flLastAttackTime"), Leader->LastAttackTime, F.Now());
	return true;
}

// =================================================================================================
// CNPC_VScurrying -- 0x103ac740
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesScurryingTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.Scurrying_0x103ac740", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesScurryingTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VScurrying"));
	FElysiumNpcScurrying* Rat = F.As<FElysiumNpcScurrying>();
	if (!TestNotNull(TEXT("the scurrying"), Rat))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	F.Start(0xbc);
	TestTrue(TEXT("0xbc keeps running"), F.Running());
	F.Start(0x14b);
	TestTrue(TEXT("0x14b completes"), F.Completed());
	// 0x14a: no scarer and a spent scare: fail 6 at 0xe9.
	Rat->ScurryingScarer = FElysiumEntityHandle::Invalid();
	Rat->ScurryingScareExpiry = F.Now() - 1.0;
	F.Start(0x14a);
	TestEqual(TEXT("0x14a with no scare fails 6"), F.Failure(), 6);
	TestTrue(TEXT("stamped at line 0xe9"), F.Traced(TEXT("NPC_VScurrying.cpp"), 0xe9));
	// A live scare stamp proceeds to the flee search: no destination fails 7 at 0xfa, a refused goal
	// 0xc at 0x107, an accepted one completes.
	Rat->ScurryingScareExpiry = F.Now() + 5.0;
	F.Start(0x14a);
	TestNotEqual(TEXT("0x14a with a live scare does not fail 6"), F.Failure(), 6);
	if (F.Failure() == 7)
	{
		TestTrue(TEXT("stamped at line 0xfa"), F.Traced(TEXT("NPC_VScurrying.cpp"), 0xfa));
	}
	else if (F.Failure() == 0xc)
	{
		TestTrue(TEXT("stamped at line 0x107"), F.Traced(TEXT("NPC_VScurrying.cpp"), 0x107));
	}
	else
	{
		TestTrue(TEXT("0x14a with a goal completes"), F.Completed());
	}
	return true;
}

// =================================================================================================
// CNPC_VSheriffMan -- 0x103aec70
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesSheriffTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.SheriffMan_0x103aec70", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesSheriffTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VSheriffMan"));
	FElysiumNpcSheriffMan* Sheriff = F.As<FElysiumNpcSheriffMan>();
	if (!TestNotNull(TEXT("the sheriff"), Sheriff))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	// 0x13b: the land-blast emitter at this body's origin (then the base).
	F.Start(0x13b);
	if (TestTrue(TEXT("0x13b: an emitter"), Sheriff->SheriffEmitterPlacements.Num() > 0))
	{
		TestEqual(TEXT("0x13b: the land-blast emitter"), Sheriff->SheriffEmitterPlacements.Last().Name,
			FString(TEXT("sheriff_landblast_emitter")));
	}
	// 0x150: teleport out.
	Sheriff->bSheriffTeleporting = false;
	Sheriff->EffectsWord = 0u;
	Sheriff->SolidFlagsWord = 0u;
	F.Start(0x150);
	TestTrue(TEXT("0x150: teleporting"), Sheriff->bSheriffTeleporting);
	TestEqual(TEXT("0x150: the teleport emitter"), Sheriff->SheriffEmitterPlacements.Last().Name,
		FString(TEXT("sheriff_teleport_emitter")));
	TestEqual(TEXT("0x150: EF_NODRAW"), Sheriff->EffectsWord & 0x20u, 0x20u);
	TestEqual(TEXT("0x150: not solid"), Sheriff->SolidFlagsWord & 0x4u, 0x4u);
	TestTrue(TEXT("0x150 keeps running"), F.Running());
	// 0x151/0x155/0x156/0x157: a selected node, or its line and fail 1.
	const struct { int32 Task; int32 Line; } Selects[] = { { 0x151, 0x192 }, { 0x155, 0x1ac }, { 0x156, 0x1b4 }, { 0x157, 0x1bc } };
	for (const auto& Row : Selects)
	{
		F.Start(Row.Task);
		if (Sheriff->BaseScheduleHost.HintNode == INDEX_NONE)
		{
			TestEqual(FString::Printf(TEXT("0x%x: no node fails 1"), Row.Task), F.Failure(), 1);
			TestTrue(FString::Printf(TEXT("0x%x: stamped"), Row.Task), F.Traced(TEXT("npc_vsheriffman.cpp"), Row.Line));
		}
		else
		{
			TestTrue(FString::Printf(TEXT("0x%x: a node completes"), Row.Task), F.Completed());
		}
	}
	// 0x152 with a node already held: nothing, running.
	Sheriff->BaseScheduleHost.HintNode = F.HintIndex(TEXT("hint"));
	F.Start(0x152);
	TestEqual(TEXT("0x152 keeps the held node"), Sheriff->BaseScheduleHost.HintNode, F.HintIndex(TEXT("hint")));
	TestTrue(TEXT("0x152 keeps running"), F.Running());
	// 0x153: teleport in onto the hint.
	const int32 Melee = Sheriff->ChooseBestMeleeWeaponCalls;
	Sheriff->bSheriffTeleporting = true;
	Sheriff->EffectsWord = 0x20u;
	Sheriff->SolidFlagsWord = 0x4u;
	Sheriff->LastAttackTime = -1.0;
	F.Start(0x153);
	TestEqual(TEXT("0x153: at the hint"), Sheriff->Origin, F.Entity(TEXT("hint"))->Origin);
	TestEqual(TEXT("0x153: EF_NODRAW cleared"), Sheriff->EffectsWord & 0x20u, 0u);
	TestEqual(TEXT("0x153: 0x10 set"), Sheriff->EffectsWord & 0x10u, 0x10u);
	TestEqual(TEXT("0x153: solid"), Sheriff->SolidFlagsWord & 0x4u, 0u);
	TestEqual(TEXT("0x153: the melee pick"), Sheriff->ChooseBestMeleeWeaponCalls, Melee + 1);
	TestTrue(TEXT("0x153: snapped to bip01"), Sheriff->SheriffMatchOriginAnglesCalls.Num() > 0);
	TestFalse(TEXT("0x153: done teleporting"), Sheriff->bSheriffTeleporting);
	TestEqual(TEXT("0x153: m_flLastAttackTime"), Sheriff->LastAttackTime, F.Now());
	TestTrue(TEXT("0x153 keeps running"), F.Running());
	F.Start(0x154);
	TestTrue(TEXT("0x154 keeps running"), F.Running());
	// 0x158: a clear stand trace: the jump, the stamp, complete.
	Sheriff->LastAttackTime = -1.0;
	F.Start(0x158);
	TestEqual(TEXT("0x158: no failure on a clear stand"), F.Failure(), 0);
	TestEqual(TEXT("0x158: m_flLastAttackTime"), Sheriff->LastAttackTime, F.Now());
	TestTrue(TEXT("0x158 completes"), F.Completed());
	return true;
}

// =================================================================================================
// CNPC_VTzimisce -- 0x103ba7c0
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesTzimisceTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.Tzimisce_0x103ba7c0", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesTzimisceTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VTzimisce"));
	FElysiumNpcTzimisce* Tzim = F.As<FElysiumNpcTzimisce>();
	if (!TestNotNull(TEXT("the Tzimisce"), Tzim) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	F.Start(0x3);
	TestTrue(TEXT("0x3 completes"), F.Completed());
	Tzim->PathMode = 0;
	F.Start(0xf);
	TestEqual(TEXT("0xf: path mode 1"), Tzim->PathMode, 1);
	for (const int32 Task : { 0x89, 0x8a })
	{
		Tzim->LastAttackTime = -1.0;
		F.Start(Task);
		TestEqual(FString::Printf(TEXT("0x%x stamps m_flLastAttackTime"), Task), Tzim->LastAttackTime, F.Now());
	}
	for (const int32 Task : { 0x8b, 0x8e, 0xa7, 0xc1 })
	{
		F.Start(Task);
		TestTrue(FString::Printf(TEXT("0x%x keeps running"), Task), F.Running());
	}
	for (const int32 Task : { 0xbf, 0xc0 })
	{
		const int32 Before = Tzim->Fun103bf440Calls;
		F.Start(Task);
		TestEqual(FString::Printf(TEXT("0x%x: the carry facing"), Task), Tzim->Fun103bf440Calls, Before + 1);
	}
	F.Start(0xc2);
	TestTrue(TEXT("0xc2 completes"), F.Completed());
	// 0xc3: path mode 2; no target -> fail 1 at 0x722; a target -> complete.
	Tzim->PickupTarget = FElysiumEntityHandle::Invalid();
	F.Start(0xc3);
	TestEqual(TEXT("0xc3: path mode 2"), Tzim->PathMode, 2);
	TestEqual(TEXT("0xc3 without a target fails 1"), F.Failure(), 1);
	TestTrue(TEXT("stamped at line 0x722"), F.Traced(TEXT("NPC_VTzimisce.cpp"), 0x722));
	Tzim->PickupTarget = F.Other->Handle;
	F.Start(0xc3);
	TestTrue(TEXT("0xc3 with a target completes"), F.Completed());
	F.Start(0xc8);
	TestEqual(TEXT("0xc8 completes iff FacingIdeal"), F.Completed(), Tzim->FacingIdeal());
	Tzim->Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x1b));
	Tzim->TzimisceTaskFailTimer = -1.0;
	F.Start(0xc9);
	TestEqual(TEXT("0xc9: +0x66a8 = curtime + 1"), Tzim->TzimisceTaskFailTimer, F.Now() + 1.0);
	Tzim->TzimisceTaskFailTimer = -1.0;
	F.Start(0xca);
	TestEqual(TEXT("0xca arms no fail timer"), Tzim->TzimisceTaskFailTimer, -1.0);
	// 0xcb: the stamp; completes iff the activity is finished.
	Tzim->bSequenceFinished = false;
	Tzim->LastAttackTime = -1.0;
	F.Start(0xcb);
	TestEqual(TEXT("0xcb stamps m_flLastAttackTime"), Tzim->LastAttackTime, F.Now());
	TestTrue(TEXT("0xcb unfinished keeps running"), F.Running());
	Tzim->bSequenceFinished = true;
	Tzim->IdealSequence = Tzim->SequenceNumber;
	F.Start(0xcb);
	TestTrue(TEXT("0xcb finished completes"), F.Completed());
	Tzim->bSequenceFinished = false;
	// 0xcc/0xcd with no enemy: fail 6 at their lines.
	ElysiumNpcEnemy::SetEnemy(*Tzim, FElysiumEntityHandle::Invalid());
	F.Start(0xcc);
	TestEqual(TEXT("0xcc without an enemy fails 6"), F.Failure(), 6);
	TestTrue(TEXT("stamped at line 0x78c"), F.Traced(TEXT("NPC_VTzimisce.cpp"), 0x78c));
	F.Start(0xcd);
	TestEqual(TEXT("0xcd without an enemy fails 6"), F.Failure(), 6);
	TestTrue(TEXT("stamped at line 0x7b5"), F.Traced(TEXT("NPC_VTzimisce.cpp"), 0x7b5));
	// 0xcc with one: the pounce check `0x103bf660` (family Conditions19's `TzimiscePounceTest`). Here it
	// refuses -- the enemy has no memory record, so its LKP is `vec3_origin` (`0x102dfed0`), inside the
	// `d < 40000` floor (`0x103bf740`) -- and the arm fails 0x1a at line 0x7ab (`0x103bad57`) without
	// stamping the attack time.
	ElysiumNpcEnemy::SetEnemy(*Tzim, F.Other->Handle);
	const int32 Pounces = Tzim->Fun103bf660Calls;
	Tzim->LastAttackTime = -1.0;
	F.Start(0xcc);
	TestEqual(TEXT("0xcc: the pounce check"), Tzim->Fun103bf660Calls, Pounces + 1);
	TestEqual(TEXT("0x103bad6b the refused pounce fails 0x1a"), F.Failure(), 0x1a);
	TestTrue(TEXT("stamped at line 0x7ab"), F.Traced(TEXT("NPC_VTzimisce.cpp"), 0x7ab));
	TestEqual(TEXT("0xcc refused: no m_flLastAttackTime stamp"), Tzim->LastAttackTime, -1.0);
	F.Start(0xce);
	TestTrue(TEXT("0xce unfinished keeps running"), F.Running());
	// 0xd1: `(ResolveTaskDistance(data) + 150)^2`, complete.
	const float Pad = Tzim->ResolveTaskDistance(64.f) + 150.f;
	F.Start(0xd1, 64.f);
	TestEqual(TEXT("0xd1: the inside-interrupt distance"), Tzim->ScheduleHost.InsideInterruptDistanceSqr, Pad * Pad);
	TestTrue(TEXT("0xd1 completes"), F.Completed());
	F.Start(0xd2, 5.f);
	TestEqual(TEXT("0xd2: the data word as the ideal"), Tzim->IdealActivityNumber, 5);
	return true;
}

// =================================================================================================
// CNPC_VVampireBoss -- 0x103c5ac0
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesVampireBossTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.VampireBoss_0x103c5ac0", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesVampireBossTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VVampireBoss"));
	FElysiumNpcVampireBoss* Boss = F.As<FElysiumNpcVampireBoss>();
	if (!TestNotNull(TEXT("the boss"), Boss))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	// The prologue on every task.
	Boss->VampireBossTaskStartTime = -1.0;
	F.Start(0x2f);
	TestEqual(TEXT("prologue: +0x669c = curtime"), Boss->VampireBossTaskStartTime, F.Now());
	TestTrue(TEXT("0x2f keeps running"), F.Running());
	// 0x14a: no hint -> fail 4 at 0xee; a hint -> hidden, EF_NODRAW, complete.
	Boss->BaseScheduleHost.HintNode = INDEX_NONE;
	F.Start(0x14a);
	TestEqual(TEXT("0x14a without a hint fails 4"), F.Failure(), 4);
	TestTrue(TEXT("stamped at line 0xee"), F.Traced(TEXT("npc_VVampireBoss.cpp"), 0xee));
	F.Start(0x14b);
	TestEqual(TEXT("0x14b without a hint fails 4"), F.Failure(), 4);
	TestTrue(TEXT("stamped at line 0xfd"), F.Traced(TEXT("npc_VVampireBoss.cpp"), 0xfd));
	Boss->BaseScheduleHost.HintNode = F.HintIndex(TEXT("hint"));
	Boss->EffectsWord = 0u;
	F.Start(0x14a);
	TestEqual(TEXT("0x14a: EF_NODRAW"), Boss->EffectsWord & 0x20u, 0x20u);
	TestTrue(TEXT("0x14a completes"), F.Completed());
	// 0x14b: onto the hint, visible, solid, snapped, 0x10, complete.
	Boss->SolidFlagsWord = 0x4u;
	const int32 Snaps = Boss->VampireBossMatchOriginAnglesCalls.Num();
	F.Start(0x14b);
	TestEqual(TEXT("0x14b: at the hint"), Boss->Origin, F.Entity(TEXT("hint"))->Origin);
	TestEqual(TEXT("0x14b: EF_NODRAW cleared"), Boss->EffectsWord & 0x20u, 0u);
	TestEqual(TEXT("0x14b: 0x10 set"), Boss->EffectsWord & 0x10u, 0x10u);
	TestEqual(TEXT("0x14b: solid"), Boss->SolidFlagsWord & 0x4u, 0u);
	TestEqual(TEXT("0x14b: snapped to bip01"), Boss->VampireBossMatchOriginAnglesCalls.Num(), Snaps + 1);
	TestTrue(TEXT("0x14b completes"), F.Completed());
	const int32 Transforms = Boss->TransformationStartCalls;
	F.Start(0x14c);
	TestEqual(TEXT("0x14c: slot 618"), Boss->TransformationStartCalls, Transforms + 1);
	TestTrue(TEXT("0x14c completes"), F.Completed());
	F.Start(0x14d);
	TestTrue(TEXT("0x14d keeps running"), F.Running());
	// 0x14e: no monster model -> fail 4 at 0x131; a model -> hull 0, complete.
	Boss->VampireBossMonsterModelName.Reset();
	F.Start(0x14e);
	TestEqual(TEXT("0x14e without a model fails 4"), F.Failure(), 4);
	TestTrue(TEXT("stamped at line 0x131"), F.Traced(TEXT("npc_VVampireBoss.cpp"), 0x131));
	Boss->VampireBossMonsterModelName = TEXT("models/character/monster/test/test.mdl");
	Boss->HullKind = 5;
	Boss->PathingHullKind = 5;
	F.Start(0x14e);
	TestEqual(TEXT("0x14e: hull 0"), Boss->HullKind, 0);
	TestEqual(TEXT("0x14e: pathing hull 0"), Boss->PathingHullKind, 0);
	TestTrue(TEXT("0x14e completes"), F.Completed());
	F.Start(0x14f);
	TestEqual(TEXT("0x14f: the next think"), Boss->NextThink, static_cast<float>(F.Now() + static_cast<double>(0.01f)));
	TestTrue(TEXT("0x14f completes"), F.Completed());
	return true;
}

// =================================================================================================
// CNPC_VWerewolf -- 0x103ccda0
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesWerewolfTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.Werewolf_0x103ccda0", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesWerewolfTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VWerewolf"));
	FElysiumNpcWerewolf* Wolf = F.As<FElysiumNpcWerewolf>();
	if (!TestNotNull(TEXT("the werewolf"), Wolf))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	const int32 Fence = F.HintIndex(TEXT("wwfence"));
	const int32 Next = F.HintIndex(TEXT("wwnext"));
	const int32 Cheat = F.HintIndex(TEXT("wwcheat"));
	const int32 Bad = F.HintIndex(TEXT("wwbad"));
	const int32 Plain = F.HintIndex(TEXT("hint"));
	if (!TestTrue(TEXT("the werewolf hints stand"), Fence != INDEX_NONE && Next != INDEX_NONE && Cheat != INDEX_NONE
		&& Bad != INDEX_NONE && Plain != INDEX_NONE))
	{
		return false;
	}
	// Task 2 inside schedule 0x158: `m_flWaitFinished = curtime + werewolf_teleport_in_time` (2.0).
	Wolf->Schedule.Current = Wolf->ResolveScheduleId(Wolf->TranslateScheduleRetail(0x158));
	Wolf->BaseScheduleHost.WaitFinished = -1.0;
	F.Start(2);
	if (Wolf->Schedule.Current != ElysiumScheduleId::None)
	{
		TestEqual(TEXT("2 in 0x158: the wait"), Wolf->BaseScheduleHost.WaitFinished, F.Now() + 2.0);
		TestTrue(TEXT("2 in 0x158 keeps running"), F.Running());
	}
	Wolf->Schedule.Current = ElysiumScheduleId::None;
	// 0x4e: `ResolveTaskDistance(data) + (+0x66d0) + (+0x66cc) + 5.0`, complete.
	Wolf->WerewolfTeleportDistanceA = 10.f;
	Wolf->WerewolfTeleportDistanceB = 20.f;
	const double Expected = static_cast<double>(Wolf->ResolveTaskDistance(32.f)) + 20.0 + 10.0 + 5.0;
	F.Start(0x4e, 32.f);
	TestEqual(TEXT("0x4e: the tolerance"), Wolf->ScheduleHost.GoalToleranceCm,
		static_cast<float>(Expected) * ElysiumMove::U);
	// `0x103ccece` `0x102ee1c0` and `0x103ccee0` `0x102f2fe0`: the same value into the navigator's path.
	TestEqual(TEXT("0x4e: the path tolerance"), Wolf->Navigator.GoalToleranceCm, static_cast<float>(Expected) * ElysiumMove::U);
	TestEqual(TEXT("0x4e: path +0x20"), Wolf->NavPathScalar20, static_cast<float>(Expected));
	TestTrue(TEXT("0x4e completes"), F.Completed());
	// 0x100: the base first, then m_bfAINPCFlags &= ~0x10000.
	Wolf->NpcFlags.Set(EElysiumNpcFlag::FORCE_RELAXED_ANIMS);
	F.Start(0x100);
	TestFalse(TEXT("0x100 clears 0x10000"), Wolf->NpcFlags.Has(EElysiumNpcFlag::FORCE_RELAXED_ANIMS));
	// 0x14a: no enemy -> the closest player becomes the enemy; complete either way.
	ElysiumNpcEnemy::SetEnemy(*Wolf, FElysiumEntityHandle::Invalid());
	F.Start(0x14a);
	if (Wolf->Senses.Memory.ClosestPlayer.IsSet() && F.Player != nullptr)
	{
		TestTrue(TEXT("0x14a: the player is the enemy"),
			static_cast<const FElysiumNpc*>(Wolf)->GetEnemy() == static_cast<FElysiumEntity*>(F.Player));
	}
	TestTrue(TEXT("0x14a completes"), F.Completed());
	// 0x14b: an activity and a facing, no completion.
	F.Start(0x14b);
	TestTrue(TEXT("0x14b keeps running"), F.Running());
	// 0x14c: COND 0x77 completes; otherwise (no navigator goal) the string failure.
	Wolf->Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x77));
	F.Start(0x14c);
	TestTrue(TEXT("0x14c under 0x77 completes"), F.Completed());
	Wolf->Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x77));
	Wolf->BaseScheduleHost.bShouldMove = true;
	F.Start(0x14c);
	TestEqual(TEXT("0x14c: the pointer-valued failure"), F.Failure(), 0x10661dac);
	TestEqual(TEXT("0x14c: its text"), Wolf->BaseScheduleHost.FailText, FString(TEXT("Did not path out of player's sight")));
	TestFalse(TEXT("0x14c: m_bShouldMove cleared"), Wolf->BaseScheduleHost.bShouldMove);
	F.Start(0x14d);
	TestTrue(TEXT("0x14d keeps running"), F.Running());
	F.Start(0x14e);
	TestTrue(TEXT("0x14e completes"), F.Completed());
	// 0x14f: seen within the second -> fail 0x1a; unseen for 1.0+ -> teleport out, complete.
	Wolf->WerewolfLastSeenTime = F.Now();
	F.Start(0x14f);
	TestEqual(TEXT("0x14f seen fails 0x1a"), F.Failure(), 0x1a);
	Wolf->WerewolfLastSeenTime = F.Now() - 5.0;
	F.Start(0x14f);
	TestTrue(TEXT("0x14f unseen completes"), F.Completed());
	// 0x150: no hint -> 4; the cheat hint completes; a playable hint runs; an unplayable one -> 0x15.
	Wolf->TeleportHintNode = INDEX_NONE;
	F.Start(0x150);
	TestEqual(TEXT("0x150 without a hint fails 4"), F.Failure(), 4);
	Wolf->TeleportHintNode = Cheat;
	F.Start(0x150);
	TestTrue(TEXT("0x150 on 0x3aa9 completes"), F.Completed());
	Wolf->TeleportHintNode = Fence;
	F.Start(0x150);
	TestTrue(TEXT("0x150 on a playable hint runs"), F.Running() && F.Failure() == 0);
	Wolf->TeleportHintNode = Plain;
	F.Start(0x150);
	TestEqual(TEXT("0x150 on an unplayable hint fails 0x15"), F.Failure(), 0x15);
	// 0x151: no hint completes; a hint is blacklisted 5 s, remembered, and chained by name.
	Wolf->TeleportHintNode = INDEX_NONE;
	F.Start(0x151);
	TestTrue(TEXT("0x151 without a hint completes"), F.Completed());
	Wolf->BossBlacklist.Reset();
	Wolf->TeleportHintNode = Fence;
	F.Start(0x151);
	if (TestEqual(TEXT("0x151: one blacklist row"), Wolf->BossBlacklist.Num(), 1))
	{
		TestTrue(TEXT("0x151: the fence is listed"), Wolf->BossBlacklist[0].Entity == F.Entity(TEXT("wwfence"))->Handle);
		TestEqual(TEXT("0x151: for 5 s"), Wolf->BossBlacklist[0].ExpiresAt, F.Now() + 5.0);
	}
	TestEqual(TEXT("0x151: the last used teleport hint"), Wolf->WerewolfLastUsedTeleportHint, Fence);
	TestEqual(TEXT("0x151: chained to wwnext"), Wolf->TeleportHintNode, Next);
	TestTrue(TEXT("0x151 on a playable next hint runs"), F.Running());
	// 0x152: zone flags 0, blacklist unless listed, clear, the seen stamp, complete.
	Wolf->BossBlacklist.Reset();
	Wolf->TeleportHintNode = Fence;
	Wolf->WerewolfHintFlags = 5u;
	F.Start(0x152);
	TestEqual(TEXT("0x152: zone flags cleared"), Wolf->WerewolfHintFlags, 0u);
	TestEqual(TEXT("0x152: listed once"), Wolf->BossBlacklist.Num(), 1);
	TestEqual(TEXT("0x152: the teleport hint cleared"), Wolf->TeleportHintNode, INDEX_NONE);
	TestEqual(TEXT("0x152: +0x66ec"), Wolf->WerewolfLastSeenTime, F.Now());
	TestTrue(TEXT("0x152 completes"), F.Completed());
	Wolf->TeleportHintNode = Fence;
	F.Start(0x152);
	TestEqual(TEXT("0x152: a live row is not re-added"), Wolf->BossBlacklist.Num(), 1);
	// 0x153: no move hint -> 4; with one, the groundpoint into m_vSavePosition, complete.
	Wolf->MoveHintNode = INDEX_NONE;
	F.Start(0x153);
	TestEqual(TEXT("0x153 without a move hint fails 4"), F.Failure(), 4);
	Wolf->MoveHintNode = Fence;
	F.Start(0x153);
	TestTrue(TEXT("0x153 completes"), F.Completed());
	// 0x154: no move hint -> 4; with one, a facing and no completion.
	Wolf->MoveHintNode = INDEX_NONE;
	F.Start(0x154);
	TestEqual(TEXT("0x154 without a move hint fails 4"), F.Failure(), 4);
	Wolf->MoveHintNode = Fence;
	F.Start(0x154);
	TestTrue(TEXT("0x154 with a move hint keeps running"), F.Running() && F.Failure() == 0);
	// 0x155: unplayable -> 0x15; the fence leap re-added ONLY when already at index 0 (retail bug).
	Wolf->MoveHintNode = Plain;
	F.Start(0x155);
	TestEqual(TEXT("0x155 unplayable fails 0x15"), F.Failure(), 0x15);
	Wolf->BossBlacklist.Reset();
	Wolf->MoveHintNode = Fence;
	F.Start(0x155);
	TestEqual(TEXT("0x155: an absent fence leap is NOT added (index -1)"), Wolf->BossBlacklist.Num(), 0);
	TestTrue(TEXT("0x155 keeps running"), F.Running());
	Wolf->FUN_103662d0(F.Entity(TEXT("wwfence"))->Handle, 100.f);
	F.Start(0x155);
	TestEqual(TEXT("0x155: a fence leap at index 0 is added again"), Wolf->BossBlacklist.Num(), 2);
	// 0x156: no move hint completes; the chain; the fail-then-complete defect.
	Wolf->MoveHintNode = INDEX_NONE;
	F.Start(0x156);
	TestTrue(TEXT("0x156 without a move hint completes"), F.Completed());
	Wolf->BossBlacklist.Reset();
	Wolf->MoveHintNode = Fence;
	F.Start(0x156);
	TestEqual(TEXT("0x156: the last used move hint"), Wolf->WerewolfLastUsedMoveHint, Fence);
	TestEqual(TEXT("0x156: chained to wwnext"), Wolf->MoveHintNode, Next);
	TestEqual(TEXT("0x156: an absent hint is not blacklisted"), Wolf->BossBlacklist.Num(), 0);
	TestTrue(TEXT("0x156 on a playable next hint runs"), F.Running());
	Wolf->MoveHintNode = Bad;
	F.Start(0x156);
	TestEqual(TEXT("0x156 defect: an unplayable next hint fails 0x15"), F.Failure(), 0x15);
	TestTrue(TEXT("0x156 defect: the fall-through completion is refused by the failure"), F.Running());
	// 0x157: blacklist unless listed, zone 0, clear, complete.
	Wolf->BossBlacklist.Reset();
	Wolf->MoveHintNode = Fence;
	Wolf->WerewolfHintFlags = 3u;
	F.Start(0x157);
	TestEqual(TEXT("0x157: listed"), Wolf->BossBlacklist.Num(), 1);
	TestEqual(TEXT("0x157: zone flags cleared"), Wolf->WerewolfHintFlags, 0u);
	TestEqual(TEXT("0x157: the move hint cleared"), Wolf->MoveHintNode, INDEX_NONE);
	TestTrue(TEXT("0x157 completes"), F.Completed());
	// 0x158: the break-hint search answers none (seam) -> 4.
	F.Start(0x158);
	TestEqual(TEXT("0x158 without a break hint fails 4"), F.Failure(), 4);
	// 0x159: no break hint -> 0x15; playable runs; unplayable -> 0x15.
	Wolf->WerewolfBreakHintNode = INDEX_NONE;
	F.Start(0x159);
	TestEqual(TEXT("0x159 without a break hint fails 0x15"), F.Failure(), 0x15);
	Wolf->WerewolfBreakHintNode = Fence;
	F.Start(0x159);
	TestTrue(TEXT("0x159 playable runs"), F.Running() && F.Failure() == 0);
	Wolf->WerewolfBreakHintNode = Plain;
	F.Start(0x159);
	TestEqual(TEXT("0x159 unplayable fails 0x15"), F.Failure(), 0x15);
	// 0x15a: no move hint -> 4; the leap words.
	Wolf->MoveHintNode = INDEX_NONE;
	F.Start(0x15a);
	TestEqual(TEXT("0x15a without a move hint fails 4"), F.Failure(), 4);
	Wolf->MoveHintNode = Fence;
	FElysiumNpcBase::FHintWords FenceWords;
	Wolf->HintWords(Fence, FenceWords);
	const FVector SelfUnits = Wolf->Origin / ElysiumMove::U;
	const FVector EndUnits = Wolf->GetHintEndpoint(&FenceWords) / ElysiumMove::U;
	const float DeltaZ = static_cast<float>(EndUnits.Z) - static_cast<float>(SelfUnits.Z);
	const float Extra = DeltaZ > 0.f ? DeltaZ + 100.f
		: (FMath::Abs(static_cast<float>(SelfUnits.Y) - static_cast<float>(EndUnits.Y))
			+ FMath::Abs(static_cast<float>(SelfUnits.X) - static_cast<float>(EndUnits.X))) * 0.25f;
	F.Start(0x15a);
	TestEqual(TEXT("0x15a: the jump origin"), Wolf->JumpOrigin, SelfUnits);
	TestEqual(TEXT("0x15a: the jump target"), Wolf->JumpTarget, EndUnits);
	TestEqual(TEXT("0x15a: gravity 2"), Wolf->JumpGravity, 2.f);
	TestEqual(TEXT("0x15a: height (no user data)"), Wolf->JumpHeight, Extra);
	TestTrue(TEXT("0x15a completes"), F.Completed());
	// 0x15b: the landing: gravity 1, zone 0, blacklist 15 s UNCONDITIONALLY, clear, +0x66a8 = 0.
	Wolf->BossBlacklist.Reset();
	Wolf->FUN_103662d0(F.Entity(TEXT("wwfence"))->Handle, 100.f);
	Wolf->MoveHintNode = Fence;
	Wolf->WerewolfHintFlags = 1u;
	Wolf->WerewolfSnapWordA = 1;
	F.Start(0x15b);
	TestEqual(TEXT("0x15b: gravity 1"), Wolf->JumpGravity, 1.f);
	TestEqual(TEXT("0x15b: zone flags cleared"), Wolf->WerewolfHintFlags, 0u);
	TestEqual(TEXT("0x15b: added even though listed"), Wolf->BossBlacklist.Num(), 2);
	TestEqual(TEXT("0x15b: the move hint cleared"), Wolf->MoveHintNode, INDEX_NONE);
	TestEqual(TEXT("0x15b: +0x66a8 cleared"), Wolf->WerewolfSnapWordA, 0);
	TestTrue(TEXT("0x15b completes"), F.Completed());
	// 0x15c..0x161: the ideal must take or the task fails 0x15; 0x15f also goes non-solid. The restart
	// (`0x10289ee0` -> `0x10272650`) stores every non-zero id, so the 0x15 arm is unreachable for
	// these constants: a preset ideal is overwritten and the task runs.
	const struct { int32 Task; int32 Activity; } Plays[] = {
		{ 0x15c, 0x11c }, { 0x15d, 0x11d }, { 0x15e, 0x11f }, { 0x15f, 0x11e }, { 0x160, 0x120 }, { 0x161, 0x121 } };
	for (const auto& Row : Plays)
	{
		Wolf->MoveHintNode = Fence;
		Wolf->IdealActivityNumber = Row.Activity + 0x100;
		F.Start(Row.Task);
		TestEqual(FString::Printf(TEXT("0x%x: the restart stores the ideal"), Row.Task), Wolf->IdealActivityNumber, Row.Activity);
		TestEqual(FString::Printf(TEXT("0x%x: so no 0x15"), Row.Task), F.Failure(), 0);
		Wolf->IdealActivityNumber = Row.Activity;
		Wolf->SolidFlagsWord = 0u;
		F.Start(Row.Task);
		TestEqual(FString::Printf(TEXT("0x%x: a taken ideal runs"), Row.Task), F.Failure(), 0);
		TestEqual(FString::Printf(TEXT("0x%x: non-solid only on 0x15f"), Row.Task),
			(Wolf->SolidFlagsWord & 0x4u) != 0u, Row.Task == 0x15f);
	}
	return true;
}

// =================================================================================================
// CNPC_VZombie -- 0x103dfd80
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesZombieTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.Zombie_0x103dfd80", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesZombieTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VZombie"));
	FElysiumNpcZombie* Zombie = F.As<FElysiumNpcZombie>();
	if (!TestNotNull(TEXT("the zombie"), Zombie) || !TestNotNull(TEXT("the player"), F.Player))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	// 0x151: should move, the navigator activity 0x1014, a lunge wait after curtime.
	Zombie->BaseScheduleHost.bShouldMove = false;
	F.Start(0x151);
	TestTrue(TEXT("0x151: m_bShouldMove"), Zombie->BaseScheduleHost.bShouldMove);
	TestEqual(TEXT("0x151: movement activity 0x1014"), Zombie->Navigator.MovementActivity, 0x1014);
	TestTrue(TEXT("0x151: the wait is after curtime"), Zombie->BaseScheduleHost.WaitFinished >= F.Now());
	TestTrue(TEXT("0x151 keeps running"), F.Running());
	// 0x152: the player feeds the zombie when it is the closest player; complete either way.
	Zombie->Senses.Memory.ClosestPlayer = F.Player->Handle;
	const int32 Feeds = Zombie->ZombieBeFedOnCalls;
	F.Start(0x152);
	TestEqual(TEXT("0x152: the player's slot 425"), Zombie->ZombieBeFedOnCalls, Feeds + 1);
	TestTrue(TEXT("0x152 completes"), F.Completed());
	Zombie->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	F.Start(0x152);
	TestEqual(TEXT("0x152 with no player calls nothing"), Zombie->ZombieBeFedOnCalls, Feeds + 1);
	TestTrue(TEXT("0x152 with no player completes"), F.Completed());
	// 0x153: the variant's activity must take -- and the restart (`0x10289ee0` -> `0x10272650`)
	// stores every non-zero id, so for variants 1..3 the 0x15 arm is unreachable.
	const struct { int32 Variant; int32 Activity; } Feeds3[] = { { 1, 0x1098 }, { 2, 0x109b }, { 3, 0x109e } };
	for (const auto& Row : Feeds3)
	{
		Zombie->ZombieFeedVariant = Row.Variant;
		Zombie->IdealActivityNumber = Row.Activity + 1;
		F.Start(0x153);
		TestEqual(FString::Printf(TEXT("0x153 variant %d: the restart stores the ideal"), Row.Variant),
			Zombie->IdealActivityNumber, Row.Activity);
		TestEqual(FString::Printf(TEXT("0x153 variant %d: no 0x15"), Row.Variant), F.Failure(), 0);
		Zombie->IdealActivityNumber = Row.Activity;
		F.Start(0x153);
		TestEqual(FString::Printf(TEXT("0x153 variant %d taken runs"), Row.Variant), F.Failure(), 0);
	}
	Zombie->ZombieFeedVariant = 0;
	Zombie->IdealActivityNumber = Zombie->ActivityNumber;
	F.Start(0x153);
	TestEqual(TEXT("0x153 variant 0 keeps m_Activity"), F.Failure(), 0);
	// 0x150: no nearest node -> 0x18.
	F.Start(0x150);
	TestTrue(TEXT("0x150: no node fails 0x18, a node submits a goal and runs"), F.Failure() == 0x18 || F.Running());
	F.Start(0x14f);
	TestEqual(TEXT("0x14f: ideal 0x4a"), Zombie->IdealActivityNumber, 0x4a);
	F.Start(0x14e);
	TestEqual(TEXT("0x14e: ideal 0x1081"), Zombie->IdealActivityNumber, 0x1081);
	Zombie->bSequenceFinished = false;
	F.Start(0x36);
	TestTrue(TEXT("0x36 unfinished keeps running"), F.Running());
	Zombie->bSequenceFinished = true;
	Zombie->IdealSequence = Zombie->SequenceNumber;
	F.Start(0x36);
	TestTrue(TEXT("0x36 finished completes"), F.Completed());
	Zombie->bZombieNeedsCrawlOutOfGround = true;
	F.Start(0x14c);
	TestFalse(TEXT("0x14c clears the crawl-out"), Zombie->bZombieNeedsCrawlOutOfGround);
	TestTrue(TEXT("0x14c keeps running"), F.Running());
	return true;
}

// =================================================================================================
// CNPC_VFrenzyShadow -- 0x10375f50 (verified; its body lives in ElysiumNpcFrenzyShadow.cpp)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SpeciesFrenzyShadowTest,
	"Elysium.Arm.NpcKernelStartTask19.Species.FrenzyShadow_0x10375f50", GStartTask19SpeciesFlags)
bool FElysiumNpcKernelStartTask19SpeciesFrenzyShadowTest::RunTest(const FString&)
{
	FStartTask19SpeciesFixture F(TEXT("CNPC_VFrenzyShadow"));
	FElysiumNpcFrenzyShadow* Shadow = F.As<FElysiumNpcFrenzyShadow>();
	if (!TestNotNull(TEXT("the shadow"), Shadow))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);
	// 0x4e/0x9f: `NAI_Hull::Width(0) * 0.2 + ResolveTaskDistance(data)` -- hull 0 is HUMAN_HULL (26).
	for (const int32 Task : { 0x4e, 0x9f })
	{
		const float Expected = Shadow->ResolveTaskDistance(40.f)
			+ static_cast<float>(static_cast<double>(StartTask19SpeciesHullWidth(*Shadow, 0)) * 0.2);
		F.Start(Task, 40.f);
		TestEqual(FString::Printf(TEXT("0x%x: the tolerance carries the hull term"), Task),
			Shadow->ScheduleHost.GoalToleranceCm, Expected * ElysiumMove::U);
		// `0x10375fec` `0x102ee1c0` / `0x10375ffe` `0x102f2fe0`: the same word into the navigator's path.
		TestEqual(FString::Printf(TEXT("0x%x: the path tolerance"), Task), Shadow->Navigator.GoalToleranceCm,
			Expected * ElysiumMove::U);
		TestTrue(FString::Printf(TEXT("0x%x completes"), Task), F.Completed());
	}
	TestEqual(TEXT("hull 0 is 26 wide"), StartTask19SpeciesHullWidth(*Shadow, 0), 26.f);
	// The attack tasks without melee capability fail 0x1f -- reached through the LOCAL id now.
	for (const int32 Task : { 0x34, 0x37 })
	{
		F.Start(Task);
		TestEqual(FString::Printf(TEXT("0x%x without melee capability fails 0x1f"), Task), F.Failure(), 0x1f);
	}
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
