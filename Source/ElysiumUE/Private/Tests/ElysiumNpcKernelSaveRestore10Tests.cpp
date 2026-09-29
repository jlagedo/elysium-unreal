#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcNewscaster.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumNpcMakerZombie.h"
#include "Tests/ElysiumNpcDeadClasses.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

// Story 29d, family **SaveRestore10 + Lifecycle10**. Every expectation below is read off the
// decompiled C of the body it names and, where the decompiler aliased or mislabelled an argument,
// off the listing — this family corrected the checklist's walk in five places and each correction
// has a case that states the corrected fact.
//
// The suite is in four parts: the base layer's CRC32; the `AIExtendedSaveHeader_t` block of slot
// 126 `Save` (`0x1027bc60`, `ElysiumNpcBaseSaveRestore10.cpp`); slot 180 `UpdateOnRemove`; and the
// non-slot bodies — `RunAlternateAI` mode 4, the two maker helpers and the scripted sequence's
// `Activate`.
//
// Every species case proves the species body for its retail class AND the Troika body for a bare
// `CAI_BaseNPCTroika` control, whose `RetailClass()` is null. Each species body is reached on a
// fresh NPC built as that class (`AddNpcOfClass`), never by reclassing a live one. The control was a
// plain `npc_VCop` until story 5 step 2 made that classname build `CNPC_VCop`; it is the bare
// Troika line now (`AddTroikaNpc`), which is what the control always stood for.

static constexpr EAutomationTestFlags GSaveRestore10TestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One NPC of retail class `SpeciesClass` that a case's arm is driven through, plus the bare
	// `CAI_BaseNPCTroika` control.
	struct FSaveRestore10Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Species = nullptr;
		FElysiumNpc* Troika = nullptr;

		explicit FSaveRestore10Fixture(const TCHAR* SpeciesClass = TEXT("CNPC_VHumanCombatant"))
			: World(Build(SpeciesClass))
		{
			Species = World.Npc(TEXT("species"));
			Troika = World.Npc(TEXT("troika"));
			FElysiumNpcWorldFixture::Quiet({ Species, Troika });
		}

		static FElysiumNpcWorldBuilder Build(const TCHAR* SpeciesClass)
		{
			FElysiumNpcWorldBuilder Builder(TEXT("saverestore10"), 20260914);
			Builder.AddNpcOfClass(TEXT("species"), FVector(100.0, 0.0, 0.0), SpeciesClass);
			Builder.AddTroikaNpc(TEXT("troika"), FVector(200.0, 0.0, 0.0));
			return Builder;
		}
	};
}

// -------------------------------------------------------------------------------------------------
// The CRC32. The sentinel codec beside it (`0x101cf250` / `0x101cf2f0`) guarded retail's time
// re-base around slots 126 / 127 and went with them in 0019/6 (`FElysiumSaveArchive`).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10Crc32Test,
	"Elysium.Substrate.NpcKernelSaveRestore10.Crc32", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10Crc32Test::RunTest(const FString&)
{
	// `0x1023f040` / `0x1023f0c0` / `0x1023f060` are CRC32, not the bit-vector copy the checklist's
	// walk calls them: init `0xffffffff`, a table-driven byte loop over `DAT_10496f58`, final
	// complement. Pinned against the published reflected CRC-32 check value.
	const char Check[] = "123456789";
	uint32 Crc = FElysiumNpcBase::SaveCrc32Init();
	TestEqual(TEXT("the init word is 0xffffffff"), Crc, 0xffffffffu);
	Crc = FElysiumNpcBase::SaveCrc32Update(Crc, reinterpret_cast<const uint8*>(Check), 9);
	Crc = FElysiumNpcBase::SaveCrc32Final(Crc);
	TestEqual(TEXT("\"123456789\" checksums to 0xcbf43926"), Crc, 0xcbf43926u);

	// Zero bytes: `~0xffffffff == 0`, which is the literal `0` retail's own no-schedule arm writes
	// into the header — the two arms agree on the checksum and differ only in the name.
	uint32 Empty = FElysiumNpcBase::SaveCrc32Final(
		FElysiumNpcBase::SaveCrc32Update(FElysiumNpcBase::SaveCrc32Init(), nullptr, 0));
	TestEqual(TEXT("an empty run checksums to 0"), Empty, 0u);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 126 `Save`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10BaseSaveTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.BaseSave", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10BaseSaveTest::RunTest(const FString&)
{
	FSaveRestore10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;

	// `CAI_BaseNPC::Save` `0x1027bc60`'s one hand block, `AIExtendedSaveHeader_t`, is built by
	// `BuildExtendedSaveHeader` for the NPC record (`SerializeExtendedHeader`) and read back by
	// `OnRestore` `0x1027bf50`. Its sentinel encodes and pointer fix-ups closed at
	// `FElysiumSaveArchive` in 0019/6, with their cases.
	using FHeader = FElysiumNpcBase::FAiExtendedSaveHeader;
	FHeader H = N.BuildExtendedSaveHeader();
	TestEqual(TEXT("the header's version is the literal 1"), static_cast<int32>(H.Version), 1);

	// The three flag bits, in the order `0x1027bc60` ORs them.
	TestEqual(TEXT("a quiet NPC sets no flag bit"), H.Flags, 0u);
	N.BaseMemory.Enemy = N.Handle;   // slot 0x29c `GetEnemy()` is non-null
	H = N.BuildExtendedSaveHeader();
	TestEqual(TEXT("bit 0x1 is the committed enemy"), H.Flags, 1u);
	N.SetTarget(N.Handle);              // `m_hTargetEnt` resolves onto a live entity
	H = N.BuildExtendedSaveHeader();
	TestEqual(TEXT("bit 0x2 is m_hTargetEnt, and both stand together"), H.Flags, 3u);
	// Bit 0x4 is the navigator goal, whose seam answers nothing (`NavigatorGoalIsActive`).
	TestFalse(TEXT("the navigator-goal seam answers nothing"), N.NavigatorGoalIsActive());

	// No running schedule: the name is cleared and the CRC is 0, which is what retail's own else arm
	// writes (`1027bd76 MOV byte ptr [ESP+0x1c],0x0` and `MOV dword ptr [ESP+0x9c],0x0`).
	TestTrue(TEXT("a scheduleless NPC writes an empty schedule name"), H.ScheduleName.IsEmpty());
	TestEqual(TEXT("and a zero task checksum"), H.ScheduleCrc, 0u);

	return true;
}

// Slots 126 / 127 on the Troika line and the species `Save` / `Restore` twins were deleted in
// 0019/6 (the generated SAVE walk carries the fields; load-side logic moved to `OnPostRestore`,
// covered through `ElysiumRoundTripSnapshot` in `ElysiumNpcKernelLifecycle2Tests.cpp`).

// -------------------------------------------------------------------------------------------------
// Slot 180 `UpdateOnRemove` and slot 106 `PostConstructor`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10UpdateOnRemoveTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.UpdateOnRemove", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10UpdateOnRemoveTest::RunTest(const FString&)
{
	FSaveRestore10Fixture Fix;
	if (!TestNotNull(TEXT("the control spawned"), Fix.Troika))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Troika;

	// `CAI_BaseNPCTroika::UpdateOnRemove` `0x1028d6e0`, step by step.
	//
	// Step 2 is gated on `m_pAttackCoordinator` (`+0x65e8`) being non-zero.
	N.AttackCoordinator = 0;
	N.TalkingUntil = -1.0;
	N.Dialogue.bInDialog = false;
	{ const int32 HuntIds[] = { 0, -1 }; N.BuildPatrolPath(&N.PatrolPathHuntCell, 0, 0, 0, HuntIds, FElysiumNpc::EPatrolPathBuild::Replace); }
	const int32 DialogStopsBefore = N.DialogStopScheduleRequests;
	const int32 ReleasesBefore = N.InterestingPlaceReleases;
	N.UpdateOnRemove();
	TestEqual(TEXT("the interesting-place release runs unconditionally, as retail's call site does"),
		N.InterestingPlaceReleases, ReleasesBefore + 1);
	TestEqual(TEXT("a quiet NPC's dialogue arm does not run"),
		N.DialogStopScheduleRequests, DialogStopsBefore);
	TestNull(TEXT("the hunt patrol path is released (0x1029f5d0)"), N.PatrolPathHuntCell.Path);

	// Step 4: `IsInDialog()` is the four-term gate, and a talking body takes the arm.
	N.TalkingUntil = N.World != nullptr ? N.World->NowSeconds() + 10.0 : 10.0;
	N.UpdateOnRemove();
	TestEqual(TEXT("a talking body runs the dialogue stop"),
		N.DialogStopScheduleRequests, DialogStopsBefore + 1);
	// Story 29d, family Social10 landed `CAI_BaseNPCTroika::FinishTalking` (`0x102c0ca0`) and this
	// arm now calls it, so the probe reads the real body's output: `102c0e85` STAMPS `+0x64cc` with
	// `curtime` rather than clearing it, and `102c0e73` clears `m_bIsTalking` (`+0x64c0`). The
	// window is still over — `CAI_BaseNPCTroika::IsTalking` tests `curtime < m_flTalkEnd` strictly.
	TestEqual(TEXT("FinishTalking stamps the talk end with curtime"),
		N.TalkingUntil, N.World != nullptr ? N.World->NowSeconds() : 0.0);
	TestFalse(TEXT("and clears the m_bIsTalking latch"), N.bIsTalking);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10RemoveSpeciesTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.RemoveSpecies", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10RemoveSpeciesTest::RunTest(const FString&)
{
	// `CNPC_VCop::UpdateOnRemove` `0x10371a90` — both census decrements, read off the listing. The
	// two counters are PROCESS-WIDE, so the case sets and reads them explicitly. The subject is a
	// real `npc_VCop`, which is `CNPC_VCop`.
	{
		FSaveRestore10Fixture Fix(TEXT("CNPC_VCop"));
		if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
		{
			return false;
		}
		FElysiumNpcCop& N = *ElysiumTestAsSpecies<FElysiumNpcCop>(Fix.Species);
		FElysiumNpcCop::CopAliveCensus() = 3;
		FElysiumNpcCop::CopSecondCensus() = 2;
		N.bCopCountedAlive = true;
		N.bCopCountedSecond = true;
		N.UpdateOnRemove();
		TestEqual(TEXT("a counted cop decrements the live census"), FElysiumNpcCop::CopAliveCensus(), 2);
		TestEqual(TEXT("and the second census"), FElysiumNpcCop::CopSecondCensus(), 1);
		TestFalse(TEXT("m_bCountedAlive is cleared"), N.bCopCountedAlive);
		TestFalse(TEXT("and so is its twin at +0x6672"), N.bCopCountedSecond);
		// The guard is what keeps a second removal from driving the census negative.
		N.UpdateOnRemove();
		TestEqual(TEXT("an uncounted cop decrements nothing"), FElysiumNpcCop::CopAliveCensus(), 2);
		TestEqual(TEXT("nor the second census"), FElysiumNpcCop::CopSecondCensus(), 1);
		// Only the first byte set: the two arms are independent.
		N.bCopCountedAlive = true;
		N.UpdateOnRemove();
		TestEqual(TEXT("only the live census moves"), FElysiumNpcCop::CopAliveCensus(), 1);
		TestEqual(TEXT("the second is untouched"), FElysiumNpcCop::CopSecondCensus(), 1);
	}
	// Zeroed after the cop's world is torn down, so nothing its teardown does survives the case.
	FElysiumNpcCop::CopAliveCensus() = 0;
	FElysiumNpcCop::CopSecondCensus() = 0;

	// `CNPC_VMingXiao::UpdateOnRemove` `0x10391230` — the throwable drop, gated on the mode.
	{
		FSaveRestore10Fixture Fix(TEXT("CNPC_VMingXiao"));
		if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
		{
			return false;
		}
		FElysiumNpcMingXiao& N = *ElysiumTestAsSpecies<FElysiumNpcMingXiao>(Fix.Species);
		N.MingXiaoThrowableObjectMode = 2;
		const int32 ChainBefore = N.InterestingPlaceReleases;
		N.UpdateOnRemove();
		TestEqual(TEXT("the carried throwable is dropped, which zeroes the mode"),
			N.MingXiaoThrowableObjectMode, 0);
		TestEqual(TEXT("and the Troika body ran after it"),
			N.InterestingPlaceReleases, ChainBefore + 1);
	}

	// `CNPC_VNewscaster::UpdateOnRemove` `0x103a03a0` — the story-queue teardown, then the Troika
	// body. The teardown's own assertions are family Species'; what this case states is the CHAIN.
	{
		FSaveRestore10Fixture Fix(TEXT("CNPC_VNewscaster"));
		if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
		{
			return false;
		}
		FElysiumNpcNewscaster& N = *ElysiumTestAsSpecies<FElysiumNpcNewscaster>(Fix.Species);
		const int32 ChainBefore = N.InterestingPlaceReleases;
		N.UpdateOnRemove();
		TestEqual(TEXT("the newscaster arm chains the Troika body"),
			N.InterestingPlaceReleases, ChainBefore + 1);
	}

	// The control: a bare Troika NPC has a null `RetailClass()`, so it takes no species arm.
	{
		FSaveRestore10Fixture Fix;
		if (!TestNotNull(TEXT("the control spawned"), Fix.Troika))
		{
			return false;
		}
		FElysiumNpcCop::CopAliveCensus() = 5;
		Fix.Troika->UpdateOnRemove();
		TestEqual(TEXT("the bare Troika line takes the Troika body, not the CNPC_VCop arm"),
			FElysiumNpcCop::CopAliveCensus(), 5);
	}
	FElysiumNpcCop::CopAliveCensus() = 0;
	return true;
}

// Slot 106 `PostConstructor` is closed at UE component construction since 0019/6 (its slot-424 dispatch is
// Unreal component construction); its case went with it.

// -------------------------------------------------------------------------------------------------
// `FUN_10290350` — `RunAlternateAI` mode 4.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10DoorMode4Test,
	"Elysium.Substrate.NpcKernelSaveRestore10.DoorMode4", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10DoorMode4Test::RunTest(const FString&)
{
	FSaveRestore10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;

	// Step 1: the latch is UP across `MaintainActivity` and down on either side of it.
	N.AlternateAi = 4;
	N.AlternateAiExpireTime = 100.0;
	const int32 SweepsBefore = N.AlternateAiDoorSweeps;
	TestTrue(TEXT("mode 4 always answers true, so the transaction keeps the body"),
		N.RunAlternateAiDoorMode4(/*Now=*/10.0));
	TestTrue(TEXT("m_bForceMaintainActivity was up while MaintainActivity ran"),
		N.bForceMaintainActivitySeenByLastMaintain);
	TestFalse(TEXT("and is down again afterwards"), N.bForceMaintainActivity);
	TestEqual(TEXT("the forward sweep was asked for"), N.AlternateAiDoorSweeps, SweepsBefore + 1);
	TestEqual(TEXT("an unexpired transaction keeps its mode"), N.AlternateAi, 4);

	// Step 4: `curtime < timer` keeps it; the ELSE arm fires, so an EQUAL stamp has expired.
	N.AlternateAi = 4;
	N.bOpeningDoorWait = true;
	N.AlternateAiExpireTime = 10.0;
	N.BaseScheduleHost.FailureReason = 0;
	N.RunAlternateAiDoorMode4(/*Now=*/10.0);
	TestEqual(TEXT("an expiry stamp equal to curtime expires"), N.AlternateAi, 0);
	TestFalse(TEXT("m_bOpeningDoorWait is cleared with it"), N.bOpeningDoorWait);
	// `TaskFail` (`0x1029adb0`) writes the reason into `m_failureReason` and ZEROES the pending
	// one, so the landed reason is `BaseScheduleHost.FailureReason` and not `TaskFailureReason()`.
	TestEqual(TEXT("and TaskFail is raised with reason 0xe"), N.BaseScheduleHost.FailureReason, 0xe);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The species arm tables cover every override row the census holds.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10ArmCoverageTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.ArmCoverage", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10ArmCoverageTest::RunTest(const FString&)
{
	// Every class the census gives an override of 180 must take a named arm — which is
	// what stops a class this family does not know from silently falling through to the Troika
	// body. The probe: stand as that class and run the slot; a claimed arm leaves a fingerprint the
	// Troika body does not, and an unclaimed one is caught by the dispatcher's own fall-through
	// returning false. The check here is that the census holds no override row this file has not
	// listed. Classes with no instance (`CScriptedTarget`'s `0x1034e320` / `0x1034e370`) carry no
	// arm and are skipped; every other class still fails loudly on an unlisted row.
	// Slots 126 / 127 carry no species arm since 0019/6 (the SAVE walk is the persistence; the
	// species twins' load-side logic is each class's `OnPostRestore`), so only slot 180 is probed.
	const TCHAR* const KnownRemove[] = {
		TEXT("0x10371a90"), TEXT("0x10391230"), TEXT("0x103a03a0"),
		// The three cine classes share `0x101a7140`, `FElysiumScriptedSequence::UpdateOnRemove`
		// (story 5 fold A3), which releases the NPC through `ScriptEntityCancel`.
		TEXT("0x101a7140") };

	int32 ClassCount = 0;
	int32 DeadSkipped = 0;
	for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
	{
		++ClassCount;
		if (ElysiumNpcDeadClasses::Contains(Row.Name))
		{
			++DeadSkipped;
			continue;
		}
		struct FSlotProbe { int32 Slot; const TCHAR* const* Known; int32 Count; };
		const FSlotProbe Probes[] = {
			{ 180, KnownRemove,  UE_ARRAY_COUNT(KnownRemove) },
		};
		for (const FSlotProbe& Probe : Probes)
		{
			const FElysiumNpcClassSlot* Override =
				ElysiumNpcTestCensus::OverrideOf(&Row, Probe.Slot);
			if (Override == nullptr)
			{
				continue;
			}
			bool bListed = false;
			for (int32 Index = 0; Index < Probe.Count; ++Index)
			{
				if (FCString::Strcmp(Probe.Known[Index], Override->Address) == 0)
				{
					bListed = true;
					break;
				}
			}
			TestTrue(FString::Printf(TEXT("%s#%d (%s) takes a named arm"),
				Row.Name, Probe.Slot, Override->Address), bListed);
		}
	}
	TestTrue(TEXT("the census was walked"), ClassCount > 0);
	TestEqual(TEXT("and every class with no instance was skipped, keeping its census row"),
		DeadSkipped, static_cast<int32>(UE_ARRAY_COUNT(ElysiumNpcDeadClasses::Names)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The maker: `MakeNPC`'s child inheritance and `CNPCMaker_Zombie`'s two slots (story 5 fold A4:
// both are NPCs of their own classes, and the inherited words are the maker's OWN Troika words).
// -------------------------------------------------------------------------------------------------

namespace
{
	struct FSaveRestore10MakerFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpcMaker* Maker = nullptr;
		FElysiumNpcMakerZombie* ZombieMaker = nullptr;

		FSaveRestore10MakerFixture()
			: World(Build())
		{
			FElysiumEntity* Entity = World.World.FindByName(TEXT("maker"));
			FElysiumNpc* Npc = Entity != nullptr ? Entity->AsNpc() : nullptr;
			Maker = Npc != nullptr ? Npc->AsSpecies<FElysiumNpcMaker>() : nullptr;
			Entity = World.World.FindByName(TEXT("zmaker"));
			Npc = Entity != nullptr ? Entity->AsNpc() : nullptr;
			ZombieMaker = Npc != nullptr ? Npc->AsSpecies<FElysiumNpcMakerZombie>() : nullptr;
		}

		static FElysiumNpcWorldBuilder Build()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("saverestore10maker"), 20260914);
			// Far from the player the fixture spawns at the origin, so the zombie maker's MAXIMUM
			// player distance has a distance to refuse on.
			FElysiumEntityDef& Def =
				Builder.AddEntity(TEXT("npc_maker"), TEXT("maker"), FVector(50000.0, 0.0, 0.0));
			// `CNPCMaker::Spawn` (`0x1034afe0`) dispatches slot 104 `Precache`, whose missing-model arm
			// `UTIL_Remove`s the maker, so the maker authors a model.
			Def.Keys.Add(TEXT("model"), TEXT("models/maker.mdl"));
			Def.Keys.Add(TEXT("NPCType"), TEXT("npc_VHumanCombatant"));
			Def.Keys.Add(TEXT("MaxNPCCount"), TEXT("10"));
			Def.Keys.Add(TEXT("MaxLiveChildren"), TEXT("10"));
			Def.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
			// `sp_tutorial_1`'s `thug_maker`, key for key: the Troika words it authors ON THE MAKER
			// (the witness for D8, story 5 fold A4).
			Def.Keys.Add(TEXT("vision"), TEXT("540"));
			Def.Keys.Add(TEXT("hearing"), TEXT("1.00"));
			Def.Keys.Add(TEXT("npc_perception"), TEXT("3"));
			Def.Keys.Add(TEXT("use_interesting"), TEXT("1"));
			Def.Keys.Add(TEXT("percent_occluded_wait"), TEXT("10"));
			Def.Keys.Add(TEXT("percent_occluded_cover"), TEXT("30"));
			Def.Keys.Add(TEXT("allow_alert_lookaround"), TEXT("1"));
			Def.Keys.Add(TEXT("allow_kick_hint_use"), TEXT("1"));
			FElysiumEntityDef& Zombie =
				Builder.AddEntity(TEXT("npc_maker_zombie"), TEXT("zmaker"), FVector(50000.0, 0.0, 0.0));
			Zombie.Keys.Add(TEXT("model"), TEXT("models/zombiemaker.mdl"));
			Zombie.Keys.Add(TEXT("NPCType"), TEXT("npc_VZombie"));
			Zombie.Keys.Add(TEXT("MaxNPCCount"), TEXT("10"));
			Zombie.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
			Zombie.Keys.Add(TEXT("Flag_ZombieAIType"), TEXT("1"));
			Zombie.Keys.Add(TEXT("remove_distance"), TEXT("1500.5"));
			return Builder;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10MakerTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.Maker", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10MakerTest::RunTest(const FString&)
{
	FSaveRestore10MakerFixture Fix;
	if (!TestNotNull(TEXT("the maker spawned"), Fix.Maker)
		|| !TestNotNull(TEXT("the zombie maker spawned"), Fix.ZombieMaker))
	{
		return false;
	}
	FElysiumNpcMaker& M = *Fix.Maker;

	// D8: the authored keys bind on the maker's OWN Troika words (a maker IS a Troika NPC).
	TestEqual(TEXT("the maker carries its authored vision"), M.AuthoredVision, 540.f);
	TestEqual(TEXT("and its npc_perception"), M.AuthoredPerception, 3);

	// `CNPCMaker::MakeNPC` `0x1034b7b0`, slot 617: `child.X = this->X`, nine words and the triple.
	M.ChildTargetName = TEXT("spawned");
	const int32 DeriveBefore = M.PerceptionRecomputes;
	FElysiumNpc* Child = M.MakeNPC(/*bBypass=*/true);
	if (!TestNotNull(TEXT("the maker spawned a child, and slot 617 returns it"), Child))
	{
		return false;
	}
	TestEqual(TEXT("D8: the child keeps thug_maker's vision 540, not a default"), Child->AuthoredVision, 540.f);
	TestEqual(TEXT("hearing 1.00"), Child->AuthoredHearing, 1.0f);
	TestEqual(TEXT("npc_perception 3"), Child->AuthoredPerception, 3);
	TestTrue(TEXT("use_interesting 1"), Child->bUseInteresting);
	// The copy (`0x1034ba0f..0x1034ba45`) precedes `DispatchSpawn` (`0x1034ba7a`), so the child's Troika
	// `Spawn` renormalises the copied 10 / 30 / 0 / 0 / 0 into its cumulative ladder (`0x10299120..
	// 0x10299185`, sum 40, truncating IDIV): wait 25, cover 100. (Integrator correction, story 8 L08:
	// the raw copy was asserted before the Troika body ran at spawn.)
	TestEqual(TEXT("percent_occluded_wait 10 -> 25 by the spawn ladder"), Child->PercentOccludedWait, 25);
	TestEqual(TEXT("percent_occluded_cover 30 -> 100 by the spawn ladder"), Child->PercentOccludedCover, 100);
	TestTrue(TEXT("allow_alert_lookaround 1"), Child->bAllowAlertLookaround);
	TestTrue(TEXT("allow_kick_hint_use 1"), Child->ScheduleHost.bAllowKickHintUse);
	TestTrue(TEXT("+0x65f4 m_bCameFromSpawner is the last write of the body"), Child->bCameFromSpawner);
	TestEqual(TEXT("InitPerceptionDistances and 0x1028fc90 run on the MAKER, retail's oddity"),
		M.PerceptionRecomputes, DeriveBefore + 1);
	TestTrue(TEXT("the child is named NPCTargetname"), Child->TargetName == TEXT("spawned"));
	// D10: the child is built from the maker's own block, keys it does not read included.
	TestTrue(TEXT("the replay carries the maker's own block (MaxNPCCount rides along)"),
		Child->Def != nullptr && Child->Def->Keys.Contains(TEXT("MaxNPCCount")));

	// The copy is `this->X` at the moment of the call: change the maker's words, spawn again.
	M.AuthoredPerception = 7;
	M.AuthoredVision = 512.f;
	M.AuthoredHearing = 256.f;
	M.PercentOccludedWait = 11;
	M.PercentOccludedCover = 22;
	M.PercentOccludedWalk = 33;
	M.PercentOccludedFlank = 44;
	M.PercentOccludedChase = 55;
	M.bUseInteresting = false;
	M.bAllowAlertLookaround = false;
	M.bStayEntrenched = true;
	M.ScheduleHost.bAllowKickHintUse = false;
	FElysiumNpc* Second = M.MakeNPC(/*bBypass=*/true);
	if (TestNotNull(TEXT("a second child"), Second))
	{
		TestEqual(TEXT("+0x63b0 follows the maker"), Second->AuthoredPerception, 7);
		TestEqual(TEXT("+0x63b4"), Second->AuthoredVision, 512.f);
		TestEqual(TEXT("+0x63bc"), Second->AuthoredHearing, 256.f);
		TestFalse(TEXT("+0x63d9"), Second->bUseInteresting);
		// 11 / 22 / 33 / 44 / 55 copied, then the spawn ladder (sum 165, chase forced to 100 first):
		// 1100/165 = 6, +2200/165 = 19, +3300/165 = 39, 4400/165 + 39 = 65.
		TestEqual(TEXT("+0x6420"), Second->PercentOccludedWait, 6);
		TestEqual(TEXT("+0x6424"), Second->PercentOccludedCover, 19);
		TestEqual(TEXT("+0x6428"), Second->PercentOccludedWalk, 39);
		TestEqual(TEXT("+0x642c"), Second->PercentOccludedFlank, 65);
		TestEqual(TEXT("+0x6430"), Second->PercentOccludedChase, 100);
		TestFalse(TEXT("+0x6434"), Second->bAllowAlertLookaround);
		TestTrue(TEXT("+0x6435"), Second->bStayEntrenched);
		TestFalse(TEXT("+0x6436"), Second->ScheduleHost.bAllowKickHintUse);
	}

	// `CNPCMaker_Zombie::CanMakeNPC` `0x1034d0a0`, slot 618, through a `CNPCMaker` reference.
	FElysiumNpcMakerZombie& Z = *Fix.ZombieMaker;
	FElysiumNpcMaker& AsMaker = Z;
	//   D9: `remove_distance` binds the ONE float word `+0x76d8`.
	TestEqual(TEXT("remove_distance is the float +0x76d8"), Z.RemoveDistance, 1500.5f);
	TestEqual(TEXT("Flag_ZombieAIType is +0x76d0 m_iZombieAISpawnType"), Z.ZombieAiSpawnType, 1);
	//   1. A bypass answers yes before anything else.
	TestTrue(TEXT("a bypass admits before any other arm"), AsMaker.CanMakeNPC(/*bBypass=*/true));
	//   2. `m_flRemoveDist < 0.9 * Manhattan` refuses: 50000 units away is too FAR.
	TestFalse(TEXT("a player 50000 units away is beyond 0.9 x Manhattan"), AsMaker.CanMakeNPC(false));
	TestEqual(TEXT("on the distance arm"), FString(FElysiumNpcMaker::AttemptName(Z.LastAttempt)),
		FString(TEXT("distance")));
	Z.RemoveDistance = 100000.f;
	AsMaker.CanMakeNPC(/*bBypass=*/false);
	TestNotEqual(TEXT("a generous maximum falls through to the base"),
		FString(FElysiumNpcMaker::AttemptName(Z.LastAttempt)), FString(TEXT("distance")));
	// The comparison is the float against the scaled sum, strictly: at 0.9 x 50000 = 45000 exactly
	// the maker admits past this arm.
	Z.RemoveDistance = 45000.f;
	AsMaker.CanMakeNPC(/*bBypass=*/false);
	TestNotEqual(TEXT("equality admits (m_flRemoveDist < scaled refuses, not <=)"),
		FString(FElysiumNpcMaker::AttemptName(Z.LastAttempt)), FString(TEXT("distance")));

	// `CNPCMaker_Zombie::MakeNPC` `0x1034d140`, slot 617 — the missing-item arm first (forced: the
	// class registry is process-wide, so an earlier suite could have installed the catalogue).
	Z.SetZombieFistsItemForTests(false);
	TestFalse(TEXT("the missing-item arm sees no catalogue"), Z.ZombieFistsItemExists());
	const int32 EquipsBefore = Z.ZombieFistsEquips;
	TestNull(TEXT("a missing item definition answers null"), AsMaker.MakeNPC(/*bBypass=*/true));
	TestEqual(TEXT("and hands nothing over"), Z.ZombieFistsEquips, EquipsBefore);

	// The SUCCESS arm.
	Z.SetZombieFistsItemForTests(true);
	FElysiumNpc* Zombie = AsMaker.MakeNPC(/*bBypass=*/true);
	if (TestNotNull(TEXT("the zombie maker spawned a child"), Zombie))
	{
		TestEqual(TEXT("the fists were handed over"), Z.ZombieFistsEquips, EquipsBefore + 1);
		TestEqual(TEXT("the item it looked up"), Z.LastZombieFistsItem, FString(TEXT("item_w_zombie_fists")));
		TestEqual(TEXT("the spawn emitter was requested once"), Z.ZombieSpawnEmitters.Num(), 1);
		if (Z.ZombieSpawnEmitters.Num() > 0)
		{
			TestEqual(TEXT("at the maker's own origin"), Z.ZombieSpawnEmitters[0].Origin, Z.Origin);
			TestEqual(TEXT("with retail's 15.0-second life"), Z.ZombieSpawnEmitters[0].LifetimeSeconds, 15.0f);
		}
		TestFalse(TEXT("SetDisableAI(false) overrides the maker's own copy"), Zombie->IsAiDisabled());
		// The RTTI cast to `CNPC_VZombie` (`0x1062567c`, `.?AVCNPC_VZombie@@`) hits, so
		// `SetZombieAIType(child, m_iZombieAISpawnType)` runs: 1 is stored as-is.
		TestEqual(TEXT("an npc_VZombie child takes the maker's AI spawn type"), Zombie->ZombieAiType, 1);
	}
	Z.ClearZombieFistsItemForTests();
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CCineAISchedule::FUN_101a8de0` — slot 113 `Activate` on the scripted sequence.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10SequenceActivateTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.SequenceActivate", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10SequenceActivateTest::RunTest(const FString&)
{
	// `0x101a8de0`: the actor search, the two diagnostics, the three sequence precaches in the order
	// pre-idle / post-idle / play, and the next-script resolution that CLEARS `m_iszNextScript` when
	// the named beat does not resolve.
	FElysiumNpcWorldBuilder Builder(TEXT("saverestore10seq"), 20260914);
	Builder.AddNpc(TEXT("actor"), FVector(100.0, 0.0, 0.0));
	FElysiumEntityDef& Found = Builder.AddEntity(TEXT("scripted_sequence"), TEXT("beat_found"));
	Found.Keys.Add(TEXT("m_iszEntity"), TEXT("actor"));
	Found.Keys.Add(TEXT("m_iszIdle"), TEXT("wait_idle"));
	Found.Keys.Add(TEXT("m_iszPostIdle"), TEXT("after_idle"));
	Found.Keys.Add(TEXT("m_iszPlay"), TEXT("the_action"));
	Found.Keys.Add(TEXT("m_iszNextScript"), TEXT("beat_missing"));
	FElysiumEntityDef& Lost = Builder.AddEntity(TEXT("scripted_sequence"), TEXT("beat_lost"));
	Lost.Keys.Add(TEXT("m_iszEntity"), TEXT("nobody_here"));
	Lost.Keys.Add(TEXT("m_iszNextScript"), TEXT("beat_found"));

	FElysiumNpcWorldFixture Fix(MoveTemp(Builder));
	const FElysiumEntity* FoundBeat = Fix.World.FindByName(TEXT("beat_found"));
	const FElysiumEntity* LostBeat = Fix.World.FindByName(TEXT("beat_lost"));
	if (!TestNotNull(TEXT("the found beat spawned"), FoundBeat)
		|| !TestNotNull(TEXT("the lost beat spawned"), LostBeat))
	{
		return false;
	}

	// A headless fixture's NPC authors no model, but its Troika `Spawn` runs `Precache` (`0x10298d9c` ->
	// `0x10298ad0`), which names an unset model `models/error/error.mdl` (`SetModelName`) before the
	// beat activates. So the actor resolves WITH a model and takes the precache arm: no diagnostic,
	// and the sequence precaches. (Integrator correction, story 8 L08: the lane's Troika `Spawn` is
	// live at spawn now. Whether retail's `GetModelPtr` resolves `error.mdl` is unrecovered; this
	// runtime's seam asks for a non-empty model name.)
	TestEqual(TEXT("the error-model actor prints no diagnostic"),
		FElysiumNpcWorldFixture::Debug(FoundBeat, TEXT("Activate diagnostics")), FString(TEXT("0")));
	TestFalse(TEXT("and issues the sequence precaches"),
		FElysiumNpcWorldFixture::Debug(FoundBeat, TEXT("Activate precaches")).IsEmpty());

	// An actor that resolves to nothing takes the "could not find" arm, which also prints three
	// lines and SKIPS the precache.
	TestEqual(TEXT("the missing-actor arm prints the bracketed diagnostic too"),
		FElysiumNpcWorldFixture::Debug(LostBeat, TEXT("Activate diagnostics")), FString(TEXT("3")));

	// The next-script half: a name that does not resolve is CLEARED, and one that does survives.
	TestTrue(TEXT("an unresolvable m_iszNextScript is cleared"),
		FElysiumNpcWorldFixture::Debug(FoundBeat, TEXT("Next script")).IsEmpty());
	TestEqual(TEXT("and a resolvable one survives"),
		FElysiumNpcWorldFixture::Debug(LostBeat, TEXT("Next script")), FString(TEXT("beat_found")));
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
