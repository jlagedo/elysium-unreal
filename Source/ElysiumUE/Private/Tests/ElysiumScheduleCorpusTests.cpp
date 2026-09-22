#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ElysiumContentPaths.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Substrate/ElysiumScheduleNumbers.h"
#include "Substrate/ElysiumTaskOps.h"

// `FElysiumScheduleCorpus`: the recipe the 56 init bodies perform, done once.
//
// Most of these cases run on a scratch corpus written into the intermediate directory, because the
// things that must be tested -- a missing parent, a cycle, one bad text costing one class its
// programs, a constant that disagrees with the corpus -- are all shapes the SHIPPED corpus does not
// have, and could not be tested against it at all. Two cases then run on the real corpus and skip
// when it is not deployed.

namespace
{
	/** Writes the deployed corpus's own layout into a throwaway directory. */
	struct FScratchCorpus
	{
		FString Root;

		FScratchCorpus()
		{
			Root = FPaths::ProjectIntermediateDir() / TEXT("ElysiumScheduleCorpusTests")
				/ FGuid::NewGuid().ToString(EGuidFormats::Digits);
			// The two squad slots retail seeds globally before any space initialises.
			Write(TEXT("vocabulary.json"), TEXT(
				"{\"squadSlots\":["
				"{\"name\":\"SQUAD_SLOT_ATTACK1\",\"globalId\":1000000000},"
				"{\"name\":\"SQUAD_SLOT_ATTACK2\",\"globalId\":1000000001}]}"));
		}

		~FScratchCorpus()
		{
			IFileManager::Get().DeleteDirectory(*Root, false, true);
		}

		void Write(const FString& Relative, const FString& Body) const
		{
			FFileHelper::SaveStringToFile(Body, *(Root / Relative));
		}

		/** One unit. `Schedules` and `Tasks` are `name=localId` pairs; `Texts` are whole `.sch`
		 *  bodies in feed order. */
		void Unit(const FString& Key, const FString& ClassName, const FString& ParentKey,
			const TArray<TPair<FString, int32>>& Schedules,
			const TArray<TPair<FString, int32>>& Tasks,
			const TArray<FString>& Texts) const
		{
			auto Rows = [](const TArray<TPair<FString, int32>>& Pairs)
			{
				FString Out;
				for (const TPair<FString, int32>& Pair : Pairs)
				{
					if (!Out.IsEmpty())
					{
						Out += TEXT(",");
					}
					Out += FString::Printf(TEXT("{\"name\":\"%s\",\"localId\":%d}"),
						*Pair.Key, Pair.Value);
				}
				return Out;
			};

			const FString Parent = ParentKey.IsEmpty()
				? FString(TEXT("null"))
				: FString::Printf(TEXT("\"%s\""), *ParentKey);
			FString Spaces;
			for (const TCHAR* Category :
				{ TEXT("schedule"), TEXT("task"), TEXT("condition"), TEXT("squadslot") })
			{
				if (!Spaces.IsEmpty())
				{
					Spaces += TEXT(",");
				}
				Spaces += FString::Printf(
					TEXT("\"%s\":{\"address\":\"0x0\",\"namespace\":\"0x0\",\"parent\":null,")
					TEXT("\"parentUnit\":%s,\"initVa\":\"0x0\"}"), Category, *Parent);
			}

			FString TextRows;
			for (int32 Index = 0; Index < Texts.Num(); ++Index)
			{
				const FString Leaf = FString::Printf(TEXT("text%d.sch"), Index);
				Write(Key / Leaf, Texts[Index]);
				if (!TextRows.IsEmpty())
				{
					TextRows += TEXT(",");
				}
				TextRows += FString::Printf(
					TEXT("{\"file\":\"%s\",\"name\":\"t%d\",\"order\":%d}"), *Leaf, Index, Index);
			}

			Write(Key / TEXT("space.json"), FString::Printf(TEXT(
				"{\"className\":\"%s\",\"classNames\":[\"%s\"],\"initBody\":\"0x0\","
				"\"spaces\":{%s},"
				"\"registrations\":{\"schedule\":[%s],\"task\":[%s],\"condition\":[],"
				"\"squadSlot\":[]},"
				"\"texts\":[%s]}"),
				*ClassName, *ClassName, *Spaces, *Rows(Schedules), *Rows(Tasks), *TextRows));
		}
	};

	/** The base unit every scratch case starts from: `NONE` at 0 first, as `0x102cadd0` does. */
	void WriteBase(const FScratchCorpus& Corpus, int32 FailLocalId = 0x43)
	{
		Corpus.Unit(TEXT("cai_basenpc"), TEXT("CAI_BaseNPC"), FString(),
			{ { TEXT("NONE"), ElysiumSched::NONE },
			  { TEXT("IDLE_STAND"), ElysiumSched::IDLE_STAND },
			  { TEXT("FAIL"), FailLocalId } },
			{ { TEXT("TASK_WAIT"), 0 }, { TEXT("TASK_STOP_MOVING"), 1 } },
			{ TEXT("Schedule IDLE_STAND Tasks TASK_WAIT 1") });
	}

	/** The real deployed corpus, or an empty string when it is not there. */
	FString DeployedCorpus()
	{
		const FString Directory =
			FElysiumContentPaths::CorpusRoot() / TEXT("ai") / TEXT("schedules");
		return IFileManager::Get().FileExists(*(Directory / TEXT("vocabulary.json")))
			? Directory
			: FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleCorpusOrderTest,
	"Elysium.Substrate.ScheduleCorpus.ParentBeforeChild",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleCorpusOrderTest::RunTest(const FString&)
{
	FScratchCorpus Scratch;
	WriteBase(Scratch);
	// Sorted by directory name this child comes FIRST, so a loader that used the sorted order
	// rather than the graph would give it the base's range.
	Scratch.Unit(TEXT("aaa_child"), TEXT("CNPC_VChild"), TEXT("cai_basenpc"),
		{ { TEXT("SCHED_CHILD_WALK"), 0x44 } }, {},
		// The child's text names a BASE task and sets a BASE schedule: both resolve only because
		// the base loaded first and the child's spaces walk the parent chain.
		{ TEXT("Schedule SCHED_CHILD_WALK Tasks TASK_WAIT 1 TASK_STOP_MOVING Schedule:IDLE_STAND") });

	FElysiumScheduleCorpus Corpus;
	FString Error;
	TestTrue(TEXT("the corpus loads"), Corpus.LoadFrom(Scratch.Root, Error));
	TestEqual(TEXT("no unit was skipped"), Corpus.Census().UnitsSkipped, 0);
	TestEqual(TEXT("no text was refused"), Corpus.Census().ParseFailures, 0);

	const FElysiumScheduleSpaceUnit* Base = Corpus.Unit(TEXT("cai_basenpc"));
	const FElysiumScheduleSpaceUnit* Child = Corpus.Unit(TEXT("aaa_child"));
	if (Base == nullptr || Child == nullptr)
	{
		AddError(TEXT("both units should be present"));
		return false;
	}

	// The base takes the namespace's seed; the child begins past everything the base took. That is
	// the whole point of the order -- `Init` reads the counter, so loading the child first would
	// have given IT the low range.
	const FElysiumLocalIdSpace& BaseSpace = Base->Space(EElysiumIdCategory::Schedule);
	const FElysiumLocalIdSpace& ChildSpace = Child->Space(EElysiumIdCategory::Schedule);
	TestEqual(TEXT("the base opens at the namespace seed"),
		BaseSpace.GlobalBase, ElysiumScheduleId::GlobalBase);
	TestTrue(TEXT("the child's global base is past the base's last id"),
		ChildSpace.GlobalBase > BaseSpace.TranslatedTop);

	// A species text naming a base schedule works, and it works without any class tree: the name
	// resolves in the FLAT global namespace, and the number then comes back through the loaded
	// space's own parent chain.
	const FElysiumScheduleProgram* Program = Corpus.Manager().FindByName(TEXT("SCHED_CHILD_WALK"));
	if (Program == nullptr || Program->Tasks.Num() != 2)
	{
		AddError(TEXT("the child's program should carry two tasks"));
		return false;
	}
	TestEqual(TEXT("a `Schedule:` operand resolves to the BASE's local number"),
		Program->Tasks[1].Data, static_cast<float>(ElysiumSched::IDLE_STAND));
	TestEqual(TEXT("and a base task identity is the same global id in both classes"),
		Program->Tasks[0].TaskId,
		Corpus.Namespace(EElysiumIdCategory::Task).Find(TEXT("TASK_WAIT")));

	// Retail seeds the squad-slot namespace before any space initialises, so those two names are
	// the only ones in it.
	TestEqual(TEXT("the two global squad slots are seeded"),
		Corpus.Namespace(EElysiumIdCategory::SquadSlot).Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleCorpusBrokenGraphTest,
	"Elysium.Substrate.ScheduleCorpus.BrokenGraph",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleCorpusBrokenGraphTest::RunTest(const FString&)
{
	// A missing parent and a cycle are both "this unit cannot be given a range that does not
	// collide", so both skip that unit and load the rest. Loading it anyway would be the silent
	// failure: right names, wrong numbers.
	{
		FScratchCorpus Scratch;
		WriteBase(Scratch);
		Scratch.Unit(TEXT("orphan"), TEXT("CNPC_VOrphan"), TEXT("cnpc_vnowhere"),
			{ { TEXT("SCHED_ORPHAN"), 0x44 } }, {},
			{ TEXT("Schedule SCHED_ORPHAN Tasks TASK_WAIT 1") });

		FElysiumScheduleCorpus Corpus;
		FString Error;
		AddExpectedError(TEXT("parents on"), EAutomationExpectedErrorFlags::Contains, 1);
		TestTrue(TEXT("the corpus still loads"), Corpus.LoadFrom(Scratch.Root, Error));
		TestEqual(TEXT("the orphan is skipped"), Corpus.Census().UnitsSkipped, 1);
		TestTrue(TEXT("and its programs are not published"),
			Corpus.Manager().FindByName(TEXT("SCHED_ORPHAN")) == nullptr);
		TestTrue(TEXT("while the base's are"),
			Corpus.Manager().FindByName(TEXT("IDLE_STAND")) != nullptr);
	}

	{
		FScratchCorpus Scratch;
		WriteBase(Scratch);
		Scratch.Unit(TEXT("ring_a"), TEXT("CNPC_VRingA"), TEXT("ring_b"),
			{ { TEXT("SCHED_RING_A"), 0x44 } }, {}, {});
		Scratch.Unit(TEXT("ring_b"), TEXT("CNPC_VRingB"), TEXT("ring_a"),
			{ { TEXT("SCHED_RING_B"), 0x45 } }, {}, {});

		FElysiumScheduleCorpus Corpus;
		FString Error;
		AddExpectedError(TEXT("parents on"), EAutomationExpectedErrorFlags::Contains, 2);
		TestTrue(TEXT("the corpus still loads"), Corpus.LoadFrom(Scratch.Root, Error));
		TestEqual(TEXT("both halves of the cycle are skipped"), Corpus.Census().UnitsSkipped, 2);
		TestTrue(TEXT("and the base still loaded"),
			Corpus.Manager().FindByName(TEXT("IDLE_STAND")) != nullptr);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleCorpusTextFailureTest,
	"Elysium.Substrate.ScheduleCorpus.TextFailureStopsOneSpace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleCorpusTextFailureTest::RunTest(const FString&)
{
	FScratchCorpus Scratch;
	WriteBase(Scratch);
	Scratch.Unit(TEXT("cnpc_vbad"), TEXT("CNPC_VBad"), TEXT("cai_basenpc"),
		{ { TEXT("SCHED_BAD_FIRST"), 0x44 },
		  { TEXT("SCHED_BAD_SECOND"), 0x45 },
		  { TEXT("SCHED_BAD_THIRD"), 0x46 } },
		{},
		{
			TEXT("Schedule SCHED_BAD_FIRST Tasks TASK_WAIT 1"),
			TEXT("Schedule SCHED_BAD_SECOND Tasks TASK_WAIT Memory:NO_SUCH_MEMORY"),
			TEXT("Schedule SCHED_BAD_THIRD Tasks TASK_WAIT 1"),
		});

	FElysiumScheduleCorpus Corpus;
	FString Error;
	AddExpectedError(TEXT("refused"), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("the corpus loads"), Corpus.LoadFrom(Scratch.Root, Error));

	const FElysiumScheduleSpaceUnit* Bad = Corpus.Unit(TEXT("cnpc_vbad"));
	if (Bad == nullptr)
	{
		AddError(TEXT("the unit should be present"));
		return false;
	}
	TestFalse(TEXT("slot 452 answers false for this class"), Bad->bAllTextsLoaded);
	TestEqual(TEXT("one text parsed before the failure"), Bad->NumParsed, 1);
	TestEqual(TEXT("the failure names its row"), Bad->FailureRow, FString(TEXT("unknown-value")));
	TestEqual(TEXT("and the file the log has to name"), Bad->FailureFile, FString(TEXT("text1.sch")));

	TestTrue(TEXT("the text before the failure is a program"),
		Corpus.Manager().FindByName(TEXT("SCHED_BAD_FIRST")) != nullptr);
	// Retail links the node before parsing its tasks, so the text that FAILED still leaves a
	// task-less program behind that a lookup answers.
	const FElysiumScheduleProgram* Failed = Corpus.Manager().FindByName(TEXT("SCHED_BAD_SECOND"));
	TestTrue(TEXT("the failing text leaves its record behind"), Failed != nullptr);
	if (Failed != nullptr)
	{
		TestEqual(TEXT("with no tasks on it"), Failed->Tasks.Num(), 0);
	}
	TestTrue(TEXT("and everything AFTER the failure is never loaded"),
		Corpus.Manager().FindByName(TEXT("SCHED_BAD_THIRD")) == nullptr);

	// One class's failure is one class's problem, which is exactly retail's cost.
	TestTrue(TEXT("another class's programs are untouched"),
		Corpus.Manager().FindByName(TEXT("IDLE_STAND")) != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleCorpusVerifyNumbersTest,
	"Elysium.Substrate.ScheduleCorpus.VerifyNumbers",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleCorpusVerifyNumbersTest::RunTest(const FString&)
{
	// `FAIL` is planted one off. This is the check that turns the constants in
	// `ElysiumScheduleNumbers.h` from remembered numbers into recovered ones.
	FScratchCorpus Scratch;
	WriteBase(Scratch, ElysiumSched::FAIL + 1);

	FElysiumScheduleCorpus Corpus;
	FString Error;
	TestTrue(TEXT("a disagreement does NOT refuse the load"), Corpus.LoadFrom(Scratch.Root, Error));

	TArray<FString> Errors;
	TestFalse(TEXT("the check fails"), Corpus.VerifyNumbers(Errors));
	const FString* Planted = Errors.FindByPredicate(
		[](const FString& Row) { return Row.Contains(TEXT("`FAIL`")); });
	TestTrue(TEXT("and it names the constant that disagreed"), Planted != nullptr);
	if (Planted != nullptr)
	{
		TestTrue(TEXT("with BOTH numbers, so the answer is in the message"),
			Planted->Contains(TEXT("0x43")) && Planted->Contains(TEXT("0x44")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleCorpusDeployedTest,
	"Elysium.Substrate.ScheduleCorpus.Deployed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleCorpusDeployedTest::RunTest(const FString&)
{
	const FString Directory = DeployedCorpus();
	if (Directory.IsEmpty())
	{
		AddInfo(TEXT("the corpus is not deployed; run `uv run elysium import ai-schedules`"));
		return true;
	}

	FElysiumScheduleCorpus Corpus;
	FString Error;
	if (!Corpus.LoadFrom(Directory, Error))
	{
		AddError(FString::Printf(TEXT("the deployed corpus did not load: %s"), *Error));
		return false;
	}

	const FElysiumScheduleCensus& Census = Corpus.Census();
	AddInfo(Corpus.DescribeCensus());

	// The numbers the export measured off the image, reproduced by loading what it published.
	TestEqual(TEXT("fifty-six spaces"), Census.Units, 56);
	TestEqual(TEXT("none skipped -- the graph has one root and no cycle"), Census.UnitsSkipped, 0);
	TestEqual(TEXT("six hundred and ninety-one texts"), Census.Texts, 691);
	TestEqual(TEXT("and every one of them parses"), Census.ParseFailures, 0);

	// 695 schedule names against 691 texts: four registered ids have no text, which is an anomaly
	// row in the export and four names here that resolve to nothing.
	TestEqual(TEXT("the schedule namespace holds every registered name"), Census.ScheduleNames, 695);
	TestEqual(TEXT("the two seeded squad slots, and no class registers a third"),
		Census.SquadSlotNames, 2);

	TArray<FString> NumberErrors;
	if (!Corpus.VerifyNumbers(NumberErrors))
	{
		for (const FString& Row : NumberErrors)
		{
			AddError(Row);
		}
		return false;
	}

	// The measurement pass P's plan wanted: how much of the shipped task vocabulary this runtime
	// actually runs. It is expected to be small; what must hold is that it is MEASURED.
	TestTrue(TEXT("some task identities bind to a body"), Census.PortedTasks > 0);
	TestTrue(TEXT("and most do not, which is the work queue"),
		Census.UnportedTasks > Census.PortedTasks);
	TestTrue(TEXT("the unported set is reached by real steps"), Census.UnportedSteps > 0);

	// Every class the image places, placed here too -- including the ones with no init body.
	TestTrue(TEXT("a class with no init body of its own finds its space"),
		Corpus.UnitForClass(TEXT("CNPC_VRat")) != nullptr);
	TestEqual(TEXT("and it is the space of the class above it"),
		Corpus.UnitForClass(TEXT("CNPC_VRat"))->Key, FString(TEXT("cnpc_vscurrying")));
	TestEqual(TEXT("CPayphone runs Troika's vocabulary"),
		Corpus.UnitForClass(TEXT("CPayphone"))->Key, FString(TEXT("cai_basenpctroika")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleWitnessTest,
	"Elysium.Substrate.ScheduleWitness",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleWitnessTest::RunTest(const FString&)
{
	// 0019 story 3's witness. The port ran an invented two-task program under this name; retail's
	// own text is TWELVE tasks with twelve interrupts, and it is what runs now.
	const FString Directory = DeployedCorpus();
	if (Directory.IsEmpty())
	{
		AddInfo(TEXT("the corpus is not deployed; run `uv run elysium import ai-schedules`"));
		return true;
	}

	FElysiumScheduleCorpus Corpus;
	FString Error;
	if (!Corpus.LoadFrom(Directory, Error))
	{
		AddError(FString::Printf(TEXT("the deployed corpus did not load: %s"), *Error));
		return false;
	}

	const FElysiumScheduleProgram* Program =
		Corpus.Manager().FindByName(TEXT("SCHED_TROIKA_CHASE_ENEMY_FAILED"));
	if (Program == nullptr)
	{
		AddError(TEXT("the witness program is not in the manager"));
		return false;
	}

	const FElysiumScheduleSpaceUnit* Troika = Corpus.Unit(TEXT("cai_basenpctroika"));
	TestTrue(TEXT("Troika loaded"), Troika != nullptr && Troika->bAllTextsLoaded);
	if (Troika != nullptr)
	{
		TestEqual(TEXT("the witness is Troika's local 0xb7"),
			Troika->Space(EElysiumIdCategory::Schedule).GlobalToLocal(Program->GlobalId),
			ElysiumSched::SCHED_TROIKA_CHASE_ENEMY_FAILED);
	}

	// The twelve tasks, in order, with the operand each was authored with. The spec's prose lists
	// eleven; it omits `TASK_WAIT_FOR_MOVEMENT`.
	const TCHAR* const Expected[] = {
		TEXT("TASK_STOP_MOVING"),
		TEXT("TASK_WAIT"),
		TEXT("TASK_SET_FAIL_SCHEDULE"),
		TEXT("TASK_SET_TOLERANCE_DISTANCE"),
		TEXT("TASK_FIND_COVER_FROM_ENEMY"),
		TEXT("TASK_SET_NPC_FLAG"),
		TEXT("TASK_RUN_PATH"),
		TEXT("TASK_WAIT_FOR_MOVEMENT"),
		TEXT("TASK_REMEMBER"),
		TEXT("TASK_FACE_ENEMY"),
		TEXT("TASK_SET_ACTIVITY"),
		TEXT("TASK_WAIT"),
	};
	TestEqual(TEXT("twelve tasks, not the eleven the prose lists"),
		Program->Tasks.Num(), static_cast<int32>(UE_ARRAY_COUNT(Expected)));
	if (Program->Tasks.Num() != UE_ARRAY_COUNT(Expected))
	{
		return false;
	}
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Expected); ++Index)
	{
		TestEqual(FString::Printf(TEXT("task %d"), Index),
			Corpus.TaskOps().NameOf(Program->Tasks[Index].TaskId), FString(Expected[Index]));
	}

	// The operands, which are what make this a program rather than a list of names.
	TestEqual(TEXT("TASK_WAIT 0.2"), Program->Tasks[1].Data, 0.2f);
	TestEqual(TEXT("SCHEDULE:STANDOFF resolves to the BASE's local number"),
		Program->Tasks[2].Data, static_cast<float>(ElysiumSched::STANDOFF));
	TestEqual(TEXT("TASK_SET_TOLERANCE_DISTANCE 24, in source units"),
		Program->Tasks[3].Data, 24.0f);
	TestEqual(TEXT("MEMORY:INCOVER is the mask bit, not an ordinal"), Program->Tasks[8].Data, 2.0f);

	// `NPCFlag:FORCE_RELAXED_ANIMS` is stored RAW, and the tokenizer had to split it into three.
	TestEqual(TEXT("NPCFlag: stores its word unconverted"),
		Program->Tasks[5].RawWord(), 0x00010000u);

	// `ACTIVITY:ACT_IDLE` interned, and the id maps back to the name `PlayActivity` needs.
	const FString* Activity =
		Corpus.Activities().NameOf(static_cast<int32>(Program->Tasks[10].Data));
	TestTrue(TEXT("the activity operand round-trips to its name"),
		Activity != nullptr && Activity->Equals(TEXT("ACT_IDLE")));

	// Eleven of the twelve identities have a body; `TASK_FIND_COVER_FROM_ENEMY` is new in pass C
	// and its runner verb refuses by default, so the step fails by name.
	int32 Ported = 0;
	for (const FElysiumScheduleStep& Step : Program->Tasks)
	{
		Ported += (Corpus.TaskOps().Find(Step.TaskId) != EElysiumTaskOp::Unknown) ? 1 : 0;
	}
	TestEqual(TEXT("every one of the witness's tasks has a body"), Ported, Program->Tasks.Num());

	TestEqual(TEXT("twelve interrupts"), Program->Interrupts.Num(), 12);
	TestTrue(TEXT("and none of them inverted"), Program->InvertedInterrupts.IsEmpty());
	TestEqual(TEXT("the program declares no flags"), Program->Flags, ElysiumScheduleFlags::None);
	return true;
}

#endif
