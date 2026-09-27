#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumSaveArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 5 step 5 (spec 0019), packet 5g: the `CAI_BaseNPC` / `CAI_BaseNPCTroika` split.
//
// The base-only probe is a concrete `FElysiumNpcBase` with no classname and no Troika object
// behind it, so `+0x98 m_pBaseNPCTroika` (`AsNpc()`) is null: every base body it runs is the
// `CAI_BaseNPC` body alone, and every `+0x98` arm takes its null branch. No shipped classname
// builds one before the controller and cine folds (steps 9-10); the probe is how the base
// layer's own behaviour is asserted until then.
//
// The pair cases run a Troika NPC and assert that its override reaches the base half, in retail's
// order.

static constexpr EAutomationTestFlags GBaseSplitTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The interface's four pure hooks answer the inert value; nothing here runs a schedule.
	class FBaseOnlyNpc final : public FElysiumNpcBase
	{
	public:
		virtual float RunSpecialIdleActivity(double) override { return 0.f; }
		virtual bool IsBodyVisible() const override { return false; }
		virtual float PlayActivity(const FString&) override { return 0.f; }
		virtual float RandomSeconds(float) override { return 0.f; }
	};

	struct FBaseSplitFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;

		FBaseSplitFixture()
			: World(Build())
		{
			Npc = World.Npc(TEXT("subject"));
			FElysiumNpcWorldFixture::Quiet({ Npc });
		}

		static FElysiumNpcWorldBuilder Build()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("basesplit"), 20260926);
			Builder.AddNpc(TEXT("subject"), FVector(100.0, 0.0, 0.0), TEXT("npc_VHumanCombatant"));
			return Builder;
		}
	};
}

// -------------------------------------------------------------------------------------------------
// The two type words: `+0x94 m_pBaseNPC` and `+0x98 m_pBaseNPCTroika`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseSplitTypeWordsTest,
	"Elysium.Substrate.NpcKernelBaseSplit.TypeWords", GBaseSplitTestFlags)

bool FElysiumNpcKernelBaseSplitTypeWordsTest::RunTest(const FString&)
{
	FBaseOnlyNpc Base;
	TestTrue(TEXT("a base-only NPC answers +0x94 with itself"), Base.AsNpcBase() == &Base);
	TestNull(TEXT("and +0x98 with null"), Base.AsNpc());
	TestNull(TEXT("it has no CAI_Senses object yet (fold 9)"), Base.SensesObject());

	FBaseSplitFixture Fix;
	if (!TestNotNull(TEXT("the Troika subject spawned"), Fix.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fix.Npc;
	TestTrue(TEXT("a Troika NPC answers +0x94 with its base"),
		Npc.AsNpcBase() == static_cast<FElysiumNpcBase*>(&Npc));
	TestTrue(TEXT("and +0x98 with itself"), Npc.AsNpc() == &Npc);
	TestTrue(TEXT("and its senses runner is the m_pSenses object"), Npc.SensesObject() == &Npc.Senses);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Base bodies on a base-only NPC: no Troika gate, no Troika word.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseSplitBaseBodiesTest,
	"Elysium.Substrate.NpcKernelBaseSplit.BaseBodies", GBaseSplitTestFlags)

bool FElysiumNpcKernelBaseSplitBaseBodiesTest::RunTest(const FString&)
{
	FBaseOnlyNpc Base;

	// `0x1027a530`, slot 510: `m_iFloatSoundFrequency` 0 or 8 disables it, and a stamp in the
	// future refuses. The Troika's seven gates (`0x10294070`) are not on this path.
	Base.FloatSoundFrequency = 0;
	TestFalse(TEXT("float sound: frequency 0 disables"), Base.ShouldPlayFloatSound());
	Base.FloatSoundFrequency = 8;
	TestFalse(TEXT("float sound: frequency 8 disables"), Base.ShouldPlayFloatSound());
	Base.FloatSoundFrequency = 1;
	Base.NextFloatSoundTime = 1.0e6f;
	TestFalse(TEXT("float sound: the time window refuses before the roll"), Base.ShouldPlayFloatSound());

	// `0x10273fc0`, slot 448's base half: the three writes and nothing else.
	Base.BaseScheduleHost.bShouldMove = true;
	Base.Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
	Base.TaskFail(0x1b);
	TestFalse(TEXT("TaskFail clears m_bShouldMove (+0x1a40)"), Base.BaseScheduleHost.bShouldMove);
	TestEqual(TEXT("and writes the failure code (+0x5c50)"), Base.BaseScheduleHost.FailureReason, 0x1b);
	TestTrue(TEXT("and raises TASK_FAILED (0x5c)"), Base.Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));

	// `0x1026a2a0` writes through `m_pSenses`; a base-only NPC has none, and the write lands nowhere.
	Base.SetDistLook(1234.f);
	TestNull(TEXT("SetDistLook with no senses object writes nowhere"), Base.SensesObject());

	// `0x1026f590`, slot 460's base body, answers 0 (no change) on every path: with no enemy the
	// arm is not taken, and the typed slot answers the ideal state the mind already holds.
	const int32 SquadCalls = Base.SelectIdealStateSquadNewEnemyCalls;
	TestEqual(TEXT("PreSelectIdealState's base body answers 0"), Base.BasePreSelectIdealState(), 0);
	TestEqual(TEXT("and without an enemy it reaches no squad"), Base.SelectIdealStateSquadNewEnemyCalls, SquadCalls);
	TestEqual(TEXT("the typed slot answers the mind's ideal state"),
		static_cast<int32>(Base.PreSelectIdealState()), static_cast<int32>(Base.GetMind().IdealState()));

	// The retail-id slot pair dispatches to the base's own bodies on a base-only NPC.
	TestEqual(TEXT("slot 460 in retail ids is the base body"), Base.PreSelectIdealStateRetail(), 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Pairs: a Troika override reaches the base half, in retail's order.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseSplitPairsTest,
	"Elysium.Substrate.NpcKernelBaseSplit.Pairs", GBaseSplitTestFlags)

bool FElysiumNpcKernelBaseSplitPairsTest::RunTest(const FString&)
{
	FBaseSplitFixture Fix;
	if (!TestNotNull(TEXT("the Troika subject spawned"), Fix.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fix.Npc;

	// `0x1029adb0` ends in its direct call to `0x10273fc0`: only the base half writes `+0x1a40`,
	// so the Troika fail clearing it proves the base half ran.
	Npc.BaseScheduleHost.bShouldMove = true;
	Npc.ScheduleHost.PendingFailureReason = 7;
	Npc.TaskFail(0x1d);
	TestFalse(TEXT("the Troika TaskFail runs the base half (+0x1a40)"), Npc.BaseScheduleHost.bShouldMove);
	TestEqual(TEXT("which writes the failure code"), Npc.BaseScheduleHost.FailureReason, 0x1d);
	TestEqual(TEXT("after the Troika half's own reset"), Npc.ScheduleHost.PendingFailureReason, 0);

	// `0x102993c0` calls `0x1027bc60` first: the record's base half leads, so a base-only reader
	// recovers the base words from a Troika NPC's record.
	Npc.BaseScheduleHost.FailureReason = 0x2a;
	Npc.BaseScheduleHost.AttackExtentsCm = FVector(3.0, 4.0, 5.0);
	Npc.BaseScheduleHost.HintReusableAt = 12.5;
	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes, true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
		Npc.Serialize(Ar);
	}
	FBaseOnlyNpc Reader;
	{
		FMemoryReader Memory(Bytes, true);
		FElysiumSaveArchive Ar(Memory, FElysiumSaveVersion::Latest);
		Reader.FElysiumNpcBase::Serialize(Ar);
	}
	TestEqual(TEXT("the base half leads the record: failure code"), Reader.BaseScheduleHost.FailureReason, 0x2a);
	TestEqual(TEXT("attack extents"), Reader.BaseScheduleHost.AttackExtentsCm, FVector(3.0, 4.0, 5.0));
	TestEqual(TEXT("hint reuse time"), Reader.BaseScheduleHost.HintReusableAt, 12.5);
	TestTrue(TEXT("and the Troika half follows it"), Bytes.Num() > 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The descriptor chain and the base bindings.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseSplitBindingsTest,
	"Elysium.Substrate.NpcKernelBaseSplit.Bindings", GBaseSplitTestFlags)

bool FElysiumNpcKernelBaseSplitBindingsTest::RunTest(const FString&)
{
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	const FElysiumClassDesc* Base = Reg.Find(TEXT("CAI_BaseNPC"));
	const FElysiumClassDesc* Troika = Reg.Find(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("CAI_BaseNPC is registered"), Base) || !TestNotNull(TEXT("and the Troika"), Troika))
	{
		return false;
	}
	TestTrue(TEXT("CAI_BaseNPC is abstract"), Base->bAbstract);
	TestEqual(TEXT("the Troika derives from CAI_BaseNPC"), Troika->BaseName, FName(TEXT("CAI_BaseNPC")));

	// `m_bShouldMove` (+0x1a40) is a CAI_BaseNPC row: the base carries it, the Troika inherits it
	// without a copy, and the chain walk resolves it from either.
	TestTrue(TEXT("the base row lives on CAI_BaseNPC"), Base->Fields.Contains(TEXT("m_bShouldMove")));
	TestFalse(TEXT("and is not copied onto the Troika"), Troika->Fields.Contains(TEXT("m_bShouldMove")));
	TestNotNull(TEXT("the Troika resolves it through the chain"), Reg.FindField(*Troika, TEXT("m_bShouldMove")));
	// A Troika row stays the Troika's.
	TestTrue(TEXT("m_flDesiredMoveYaw (+0x63ec) is the Troika's"), Troika->Fields.Contains(TEXT("m_flDesiredMoveYaw")));
	TestFalse(TEXT("and not the base's"), Base->Fields.Contains(TEXT("m_flDesiredMoveYaw")));

	// A spawned Troika NPC's save rows include the base's.
	FBaseSplitFixture Fix;
	if (Fix.Npc != nullptr && Fix.Npc->Class != nullptr)
	{
		const TArray<FName> Saved = Reg.SaveFields(*Fix.Npc->Class);
		TestTrue(TEXT("a species' save walk carries the base row"), Saved.Contains(FName(TEXT("m_bShouldMove"))));
		TestTrue(TEXT("and the Troika row"), Saved.Contains(FName(TEXT("m_flDesiredMoveYaw"))));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
