#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Substrate/ElysiumIdNamespace.h"
#include "Substrate/ElysiumLocalIdSpace.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumScheduleId.h"
#include "Substrate/ElysiumScheduleManager.h"
#include "Substrate/ElysiumScheduleOperands.h"
#include "Substrate/ElysiumScheduleText.h"
#include "Substrate/ElysiumScheduleTokenizer.h"
#include "Substrate/ElysiumSymbolRegistry.h"
#include "Substrate/ElysiumTaskOps.h"

// The schedule-text parser `0x1030d850` and the tokenizer it reads through, against
// `docs/vtmb/npc-ai/schedule-kernel.md` § "The schedule-text parser `0x1030d850`, walked".
//
// The texts here are authored, not shipped: the shipped 691 arrive through the corpus and are
// pass C's. What these cases own is the body -- every row of the failure table, the two things
// that do NOT fail, the three raw-word prefixes, the `!` arm and the operand chain -- because a
// corpus test can only show that the 691 texts do not exercise a row, never that the row is right.

namespace
{
	/**
	 * A small class, with the four things a parse resolves against.
	 *
	 * Shaped like a real owner: a root schedule space registering `NONE` at 0 first (which is what
	 * `0x102cadd0` does, and what `Register`'s "the first id decides the base" rule requires), a
	 * root task space, and a flat condition namespace.
	 */
	struct FScratchClass
	{
		FElysiumIdNamespace Schedules;
		FElysiumIdNamespace Tasks;
		FElysiumIdNamespace Conditions;

		FElysiumLocalIdSpace ScheduleSpace;
		FElysiumLocalIdSpace TaskSpace;

		FElysiumSymbolRegistry Activities{ TEXT("DAT_1090fbe0") };
		FElysiumSymbolRegistry Models{ TEXT("DAT_10936b74") };
		FElysiumSymbolRegistry Sounds{ TEXT("DAT_1073dc3c") };

		FScratchClass()
		{
			Schedules.Category = TEXT("schedule");
			Tasks.Category = TEXT("task");
			Conditions.Category = TEXT("condition");

			ScheduleSpace.Init(Schedules, nullptr);
			TaskSpace.Init(Tasks, nullptr);

			ScheduleSpace.Register(TEXT("NONE"), 0x00, TEXT("schedule"), TEXT("CScratch"));
			ScheduleSpace.Register(TEXT("SCHED_WALK"), 0x01, TEXT("schedule"), TEXT("CScratch"));
			ScheduleSpace.Register(TEXT("SCHED_WAIT"), 0x02, TEXT("schedule"), TEXT("CScratch"));

			TaskSpace.Register(TEXT("TASK_WAIT"), 0x00, TEXT("task"), TEXT("CScratch"));
			TaskSpace.Register(TEXT("TASK_SET_ACTIVITY"), 0x01, TEXT("task"), TEXT("CScratch"));
			TaskSpace.Register(TEXT("TASK_REMEMBER"), 0x02, TEXT("task"), TEXT("CScratch"));
			TaskSpace.Register(TEXT("TASK_SET_NPC_FLAG"), 0x03, TEXT("task"), TEXT("CScratch"));
			TaskSpace.Register(TEXT("TASK_SET_SCHEDULE"), 0x04, TEXT("task"), TEXT("CScratch"));
			TaskSpace.Register(TEXT("TASK_PLAY_MODEL"), 0x05, TEXT("task"), TEXT("CScratch"));
			TaskSpace.Register(TEXT("TASK_MISC"), 0x06, TEXT("task"), TEXT("CScratch"));
			TaskSpace.Register(TEXT("TASK_HINT"), 0x07, TEXT("task"), TEXT("CScratch"));
			TaskSpace.Register(TEXT("TASK_DIST"), 0x08, TEXT("task"), TEXT("CScratch"));

			// Conditions are registered flat and their ORDINALS are what a mask carries, so these
			// three sit at 0, 1 and 2.
			Conditions.Insert(TEXT("COND_NO_CUSTOM_INTERRUPTS"), ElysiumScheduleId::GlobalBase + 0);
			Conditions.Insert(TEXT("COND_SEE_HATE"), ElysiumScheduleId::GlobalBase + 1);
			Conditions.Insert(TEXT("COND_LIGHT_DAMAGE"), ElysiumScheduleId::GlobalBase + 2);
		}

		FElysiumScheduleParseContext Context()
		{
			FElysiumScheduleParseContext Out;
			Out.ClassName = TEXT("CScratch");
			Out.ScheduleSpace = &ScheduleSpace;
			Out.TaskSpace = &TaskSpace;
			Out.Conditions = &Conditions;
			Out.Activities = &Activities;
			Out.Models = &Models;
			Out.Sounds = &Sounds;
			return Out;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleTextTokenizerTest,
	"Elysium.Substrate.ScheduleText.Tokenizer",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleTextTokenizerTest::RunTest(const FString&)
{
	// The rule that shapes the whole grammar: `:` is its own token, so a prefixed operand is three.
	const TArray<FElysiumScheduleToken> Prefixed =
		ElysiumScheduleTokenizer::Tokenize(TEXT("NPCFlag:FORCE_RELAXED_ANIMS"));
	TestEqual(TEXT("a prefixed operand is three tokens"), Prefixed.Num(), 3);
	TestEqual(TEXT("the prefix"), Prefixed[0].Text, FString(TEXT("NPCFlag")));
	TestTrue(TEXT("the colon stands alone"), Prefixed[1].Is(TEXT(':')));
	TestEqual(TEXT("the value"), Prefixed[2].Text, FString(TEXT("FORCE_RELAXED_ANIMS")));
	TestEqual(TEXT("spans are kept"), Prefixed[2].Offset, 8);

	// Every byte at or below space separates -- tabs and newlines included, and a text is not
	// line-structured at all.
	const TArray<FElysiumScheduleToken> Spaced =
		ElysiumScheduleTokenizer::Tokenize(TEXT("A\tB\r\n  C"));
	TestEqual(TEXT("separators of every kind"), Spaced.Num(), 3);

	// `//` to end of line, and only when doubled.
	const TArray<FElysiumScheduleToken> Commented =
		ElysiumScheduleTokenizer::Tokenize(TEXT("A // dropped\nB /C"));
	TestEqual(TEXT("a comment removes the rest of its line"), Commented.Num(), 3);
	TestEqual(TEXT("the token after the comment"), Commented[1].Text, FString(TEXT("B")));
	TestEqual(TEXT("a single slash is an ordinary word character"),
		Commented[2].Text, FString(TEXT("/C")));

	// A quoted group, with NO escape processing.
	const TArray<FElysiumScheduleToken> Quoted =
		ElysiumScheduleTokenizer::Tokenize(TEXT("\"two words\" tail"));
	TestEqual(TEXT("a quoted group is one token"), Quoted.Num(), 2);
	TestEqual(TEXT("the quotes are not part of the text"),
		Quoted[0].Text, FString(TEXT("two words")));
	TestEqual(TEXT("but they are part of its span"), Quoted[0].Length, 11);

	TestEqual(TEXT("a text of nothing but separators yields no tokens"),
		ElysiumScheduleTokenizer::Tokenize(TEXT("  \t\n")).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleTextGrammarTest,
	"Elysium.Substrate.ScheduleText.Grammar",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleTextGrammarTest::RunTest(const FString&)
{
	FScratchClass Scratch;

	const FString Text = TEXT(
		"Schedule SCHED_WALK\n"
		"  Tasks\n"
		"    TASK_WAIT           2.5\n"
		"    TASK_SET_ACTIVITY   Activity:ACT_IDLE\n"
		"  Interrupts\n"
		"    COND_SEE_HATE\n"
		"    COND_LIGHT_DAMAGE\n"
		"  Flags\n"
		"    DELAY_INTERRUPTS\n");

	const FElysiumScheduleParseResult Result =
		ElysiumScheduleText::Parse(Text, Scratch.Context());

	TestFalse(TEXT("the text parses"), Result.Failed());
	TestEqual(TEXT("one record"), Result.Programs.Num(), 1);
	if (Result.Programs.Num() != 1)
	{
		return false;
	}
	const FElysiumScheduleProgram& Program = Result.Programs[0];
	TestEqual(TEXT("the authored name is kept verbatim"),
		Program.Name, FString(TEXT("SCHED_WALK")));
	TestEqual(TEXT("the record carries its GLOBAL schedule id"),
		Program.GlobalId, ElysiumScheduleId::GlobalBase + 0x01);
	TestEqual(TEXT("two tasks"), Program.Tasks.Num(), 2);
	TestEqual(TEXT("the first task's identity is its global task id"),
		Program.Tasks[0].TaskId, ElysiumScheduleId::GlobalBase + 0x00);
	TestEqual(TEXT("a bare number goes through atof"), Program.Tasks[0].Data, 2.5f);
	TestTrue(TEXT("the interrupt mask carries the ordinals, not the identities"),
		Program.Interrupts.HasOrdinal(1) && Program.Interrupts.HasOrdinal(2));
	TestFalse(TEXT("and nothing it was not given"), Program.Interrupts.HasOrdinal(0));
	TestTrue(TEXT("the inverted mask is empty"), Program.InvertedInterrupts.IsEmpty());
	TestEqual(TEXT("DELAY_INTERRUPTS is flag bit 0"),
		Program.Flags, ElysiumScheduleFlags::DelayInterrupts);
	TestEqual(TEXT("no flag went unrecognised"), Result.UnknownFlags, 0);

	// Two records in one text, and the sections are genuinely optional.
	const FElysiumScheduleParseResult Pair = ElysiumScheduleText::Parse(TEXT(
		"Schedule SCHED_WALK Tasks TASK_WAIT 1\n"
		"Schedule SCHED_WAIT Tasks TASK_WAIT 2\n"), Scratch.Context());
	TestFalse(TEXT("a two-record text parses"), Pair.Failed());
	TestEqual(TEXT("two records"), Pair.Programs.Num(), 2);
	TestEqual(TEXT("in authored order"), Pair.Programs[1].Name, FString(TEXT("SCHED_WAIT")));

	// A text whose first token is not `Schedule` -- an empty one included -- is SUCCESS that loads
	// nothing. Retail's owner then carries on to its next text.
	const FElysiumScheduleParseResult Empty =
		ElysiumScheduleText::Parse(TEXT(""), Scratch.Context());
	TestFalse(TEXT("an empty text does not fail"), Empty.Failed());
	TestEqual(TEXT("and loads nothing"), Empty.Programs.Num(), 0);

	const FElysiumScheduleParseResult Foreign =
		ElysiumScheduleText::Parse(TEXT("// just a comment\nSomethingElse"), Scratch.Context());
	TestFalse(TEXT("a text that is not a schedule text does not fail"), Foreign.Failed());
	TestEqual(TEXT("and loads nothing"), Foreign.Programs.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleTextOperandsTest,
	"Elysium.Substrate.ScheduleText.Operands",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleTextOperandsTest::RunTest(const FString&)
{
	FScratchClass Scratch;

	const FString Text = TEXT(
		"Schedule SCHED_WALK Tasks\n"
		"  TASK_WAIT          Memory:INCOVER\n"
		"  TASK_WAIT          State:CRIMINAL_SUSPICION\n"
		"  TASK_DIST          DIST:TZIMISCE_CLAW\n"
		"  TASK_WAIT          TRUE\n"
		"  TASK_WAIT          off\n"
		"  TASK_SET_NPC_FLAG  NPCFlag:FORCE_RELAXED_ANIMS\n"
		"  TASK_SET_NPC_FLAG  NPCFlag:TASKS_FACE_TARGET\n"
		"  TASK_MISC          MiscFlag:D_Targeted\n"
		"  TASK_MISC          MiscFlag:NoSuchFlagAtAll\n"
		"  TASK_PLAY_MODEL    Model:models/character/npc/unique/jack.mdl\n"
		"  TASK_HINT          HintFlags:visible_nearest\n"
		"  TASK_HINT          HintFlags:nearest_random\n"
		"  TASK_SET_SCHEDULE  Schedule:SCHED_WAIT\n"
		"  TASK_WAIT          NotANumberAtAll\n");

	const FElysiumScheduleParseResult Result =
		ElysiumScheduleText::Parse(Text, Scratch.Context());
	TestFalse(TEXT("every operand form resolves"), Result.Failed());
	if (Result.Failed())
	{
		AddError(FString::Printf(TEXT("%s: %s"),
			ElysiumScheduleParseFailureName(Result.Failure), *Result.Message));
		return false;
	}
	const TArray<FElysiumScheduleStep>& Steps = Result.Programs[0].Tasks;
	TestEqual(TEXT("fourteen steps"), Steps.Num(), 14);

	TestEqual(TEXT("Memory: is a mask, converted"), Steps[0].Data, 2.0f);
	TestEqual(TEXT("State: names the state the oracle had unnamed"), Steps[1].Data, 14.0f);
	TestEqual(TEXT("DIST: is a run-time sentinel, not a distance"), Steps[2].Data, -1000001.0f);
	TestEqual(TEXT("TRUE is 1"), Steps[3].Data, 1.0f);
	TestEqual(TEXT("off is 0, case-insensitively"), Steps[4].Data, 0.0f);

	// The three raw-word prefixes store 32 bits unconverted; everything else stores a float.
	TestEqual(TEXT("NPCFlag: stores the first word's bit raw"),
		Steps[5].RawWord(), static_cast<uint32>(EElysiumNpcFlag::FORCE_RELAXED_ANIMS));
	TestEqual(TEXT("a second-word flag carries the sign bit as its marker"),
		Steps[6].RawWord(), 0x80000000u | static_cast<uint32>(EElysiumNpcFlag2::TASKS_FACE_TARGET));
	TestEqual(TEXT("MiscFlag: stores the INDEX, not the mask"), Steps[7].RawWord(), 1u);
	TestEqual(TEXT("and an unknown MiscFlag silently reads 0 rather than failing"),
		Steps[8].RawWord(), 0u);
	TestEqual(TEXT("Model: interns rather than failing"), Steps[9].RawWord(), 0u);
	TestEqual(TEXT("and the interning seam counted it"), Scratch.Models.InternedCount(), 1);

	// HintFlags is a substring search, and the nearest+random pair resolves to nearest alone.
	TestEqual(TEXT("HintFlags ORs every name it finds inside the token"), Steps[10].Data, 3.0f);
	TestEqual(TEXT("nearest and random together read as nearest"), Steps[11].Data, 2.0f);

	// A `Schedule:` operand is stored LOCAL, as retail stores it: the data word is a float and a
	// global id near 1,000,000,000 would not survive one.
	TestEqual(TEXT("Schedule: resolves to the class-local number"), Steps[12].Data, 2.0f);

	// A bare non-number reads 0.0 and does not fail.
	TestEqual(TEXT("a bare non-number is atof's 0"), Steps[13].Data, 0.0f);

	// `Activity:` interns too, and its id is what `TASK_SET_ACTIVITY` maps back to a name.
	const FElysiumScheduleParseResult WithActivity = ElysiumScheduleText::Parse(
		TEXT("Schedule SCHED_WAIT Tasks TASK_SET_ACTIVITY Activity:ACT_IDLE"), Scratch.Context());
	TestFalse(TEXT("an activity operand resolves"), WithActivity.Failed());
	const int32 ActivityId = static_cast<int32>(WithActivity.Programs[0].Tasks[0].Data);
	const FString* ActivityName = Scratch.Activities.NameOf(ActivityId);
	TestTrue(TEXT("the id maps back to the authored activity name"),
		ActivityName != nullptr && ActivityName->Equals(TEXT("ACT_IDLE")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleTextInterruptsTest,
	"Elysium.Substrate.ScheduleText.Interrupts",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleTextInterruptsTest::RunTest(const FString&)
{
	FScratchClass Scratch;

	// A `!` writes the SECOND mask. Both spellings: joined to its condition, and spaced apart.
	const FElysiumScheduleParseResult Result = ElysiumScheduleText::Parse(TEXT(
		"Schedule SCHED_WALK Tasks TASK_WAIT 1 Interrupts\n"
		"  COND_SEE_HATE\n"
		"  !COND_LIGHT_DAMAGE\n"
		"  ! COND_NO_CUSTOM_INTERRUPTS\n"), Scratch.Context());

	TestFalse(TEXT("the text parses"), Result.Failed());
	const FElysiumScheduleProgram& Program = Result.Programs[0];
	TestTrue(TEXT("the ordinary condition is in the ordinary mask"),
		Program.Interrupts.HasOrdinal(1));
	TestFalse(TEXT("and not in the inverted one"), Program.InvertedInterrupts.HasOrdinal(1));
	TestTrue(TEXT("a joined `!` inverts"), Program.InvertedInterrupts.HasOrdinal(2));
	TestTrue(TEXT("a spaced `!` inverts too"), Program.InvertedInterrupts.HasOrdinal(0));
	TestEqual(TEXT("the inverted mask holds exactly two"), Program.InvertedInterrupts.Num(), 2);

	// An unknown condition is one of the two things that do NOT fail a text: retail `DevMsg`s and
	// drops the bit, so a class naming a condition it never registered still loads.
	const FElysiumScheduleParseResult Unknown = ElysiumScheduleText::Parse(TEXT(
		"Schedule SCHED_WAIT Tasks TASK_WAIT 1 Interrupts COND_INVENTED_HERE"), Scratch.Context());
	TestFalse(TEXT("an unknown condition does not fail the text"), Unknown.Failed());
	TestEqual(TEXT("it is counted"), Unknown.SkippedConditions, 1);
	TestTrue(TEXT("and its bit is not guessed at"), Unknown.Programs[0].Interrupts.IsEmpty());

	// The other one: an unknown `Flags` word is an Error that still reads 0, and `NONE` reads 0
	// too -- so retail prints the same diagnostic for an authored `Flags NONE`, harmlessly.
	const FElysiumScheduleParseResult Flags = ElysiumScheduleText::Parse(TEXT(
		"Schedule SCHED_WAIT Tasks TASK_WAIT 1 Flags NONE SOMETHING_ELSE"), Scratch.Context());
	TestFalse(TEXT("an unknown flag does not fail the text"), Flags.Failed());
	TestEqual(TEXT("no flag bit is set"), Flags.Programs[0].Flags, ElysiumScheduleFlags::None);
	TestEqual(TEXT("both zeros are reported, NONE included"), Flags.UnknownFlags, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleTextFailuresTest,
	"Elysium.Substrate.ScheduleText.Failures",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleTextFailuresTest::RunTest(const FString&)
{
	FScratchClass Scratch;

	auto Refuses = [this, &Scratch](const TCHAR* What, const FString& Text,
		EElysiumScheduleParseFailure Expected)
	{
		const FElysiumScheduleParseResult Result =
			ElysiumScheduleText::Parse(Text, Scratch.Context());
		if (!Result.Failed())
		{
			AddError(FString::Printf(TEXT("%s: expected `%s` and the text parsed"),
				What, ElysiumScheduleParseFailureName(Expected)));
			return;
		}
		TestEqual(What, FString(ElysiumScheduleParseFailureName(Result.Failure)),
			FString(ElysiumScheduleParseFailureName(Expected)));
	};

	Refuses(TEXT("the same name declared twice"),
		TEXT("Schedule SCHED_WALK Tasks TASK_WAIT 1 Schedule SCHED_WALK Tasks TASK_WAIT 1"),
		EElysiumScheduleParseFailure::DuplicateScheduleName);

	Refuses(TEXT("a name the class never registered"),
		TEXT("Schedule SCHED_NOT_REGISTERED Tasks TASK_WAIT 1"),
		EElysiumScheduleParseFailure::UnknownScheduleName);

	Refuses(TEXT("a record with no `Tasks`"),
		TEXT("Schedule SCHED_WALK Interrupts COND_SEE_HATE"),
		EElysiumScheduleParseFailure::MissingTasks);

	Refuses(TEXT("a task name the class never registered"),
		TEXT("Schedule SCHED_WALK Tasks TASK_INVENTED 1"),
		EElysiumScheduleParseFailure::UnknownTask);

	Refuses(TEXT("a task with nothing after it"),
		TEXT("Schedule SCHED_WALK Tasks TASK_WAIT"),
		EElysiumScheduleParseFailure::MissingOperand);

	Refuses(TEXT("a prefix with nothing after its colon"),
		TEXT("Schedule SCHED_WALK Tasks TASK_WAIT Memory:"),
		EElysiumScheduleParseFailure::MissingOperand);

	// Distinct from the row above: this is the failure for FORGETTING an operand mid-list, and
	// retail spells it "Bad syntax at task #%d".
	Refuses(TEXT("a task whose operand is another task"),
		TEXT("Schedule SCHED_WALK Tasks TASK_WAIT TASK_WAIT 1"),
		EElysiumScheduleParseFailure::BadSyntaxAtTask);
	Refuses(TEXT("a task whose operand is a section keyword"),
		TEXT("Schedule SCHED_WALK Tasks TASK_WAIT Interrupts COND_SEE_HATE"),
		EElysiumScheduleParseFailure::BadSyntaxAtTask);

	Refuses(TEXT("a prefix that is none of the seventeen"),
		TEXT("Schedule SCHED_WALK Tasks TASK_WAIT Nonsense:Whatever"),
		EElysiumScheduleParseFailure::UnknownPrefix);

	Refuses(TEXT("an operand that is a bare colon"),
		TEXT("Schedule SCHED_WALK Tasks TASK_WAIT :"),
		EElysiumScheduleParseFailure::StrayColon);
	Refuses(TEXT("a second colon after a resolved value"),
		TEXT("Schedule SCHED_WALK Tasks TASK_WAIT Memory:INCOVER:"),
		EElysiumScheduleParseFailure::StrayColon);

	Refuses(TEXT("a value the resolver's table does not carry"),
		TEXT("Schedule SCHED_WALK Tasks TASK_WAIT Memory:NO_SUCH_MEMORY"),
		EElysiumScheduleParseFailure::UnknownValue);
	Refuses(TEXT("an NPCFlag: name the tables do not carry"),
		TEXT("Schedule SCHED_WALK Tasks TASK_SET_NPC_FLAG NPCFlag:NO_SUCH_FLAG"),
		EElysiumScheduleParseFailure::UnknownValue);
	Refuses(TEXT("a Schedule: operand naming nothing registered"),
		TEXT("Schedule SCHED_WALK Tasks TASK_SET_SCHEDULE Schedule:SCHED_NOWHERE"),
		EElysiumScheduleParseFailure::UnknownValue);

	// The 65th task. 64 are allowed.
	FString Capped = TEXT("Schedule SCHED_WALK Tasks");
	for (int32 Index = 0; Index < FElysiumScheduleProgram::MaxTasks; ++Index)
	{
		Capped += TEXT(" TASK_WAIT 1");
	}
	const FElysiumScheduleParseResult AtCap =
		ElysiumScheduleText::Parse(Capped, Scratch.Context());
	TestFalse(TEXT("sixty-four tasks are allowed"), AtCap.Failed());
	TestEqual(TEXT("and all sixty-four land"),
		AtCap.Programs[0].Tasks.Num(), FElysiumScheduleProgram::MaxTasks);
	Refuses(TEXT("a sixty-fifth task"), Capped + TEXT(" TASK_WAIT 1"),
		EElysiumScheduleParseFailure::TaskCap);

	// Retail links the node into the manager BEFORE parsing its tasks and no error path unlinks
	// it, so a failed text really does leave a task-less program behind that a lookup then answers.
	const FElysiumScheduleParseResult Failed = ElysiumScheduleText::Parse(
		TEXT("Schedule SCHED_WALK Tasks TASK_WAIT Memory:NO_SUCH_MEMORY"), Scratch.Context());
	TestTrue(TEXT("the text fails"), Failed.Failed());
	TestEqual(TEXT("and still publishes the record it died inside"), Failed.Programs.Num(), 1);
	TestEqual(TEXT("with no tasks on it"), Failed.Programs[0].Tasks.Num(), 0);
	TestEqual(TEXT("but with its name and id"),
		Failed.Programs[0].GlobalId, ElysiumScheduleId::GlobalBase + 0x01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleTextManagerTest,
	"Elysium.Substrate.ScheduleText.Manager",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleTextManagerTest::RunTest(const FString&)
{
	FScratchClass Scratch;
	FElysiumScheduleManager Manager;

	Manager.AddAll(ElysiumScheduleText::Parse(TEXT(
		"Schedule SCHED_WALK Tasks TASK_WAIT 1\n"
		"Schedule SCHED_WAIT Tasks TASK_WAIT 2 TASK_WAIT 3\n"), Scratch.Context()));

	TestEqual(TEXT("both programs are stored"), Manager.Num(), 2);

	const FElysiumScheduleProgram* ByName = Manager.FindByName(TEXT("sched_wait"));
	TestTrue(TEXT("`0x1030f350` compares case-insensitively"), ByName != nullptr);
	TestEqual(TEXT("and finds the right one"), ByName->Tasks.Num(), 2);

	const FElysiumScheduleProgram* ById =
		Manager.FindById(ElysiumScheduleId::GlobalBase + 0x01);
	TestTrue(TEXT("`0x1030f300` keys on the GLOBAL id"), ById != nullptr);
	TestEqual(TEXT("and finds the right one"), ById->Name, FString(TEXT("SCHED_WALK")));

	TestTrue(TEXT("a name nothing registered is not found"),
		Manager.FindByName(TEXT("SCHED_NOWHERE")) == nullptr);

	// Retail links at the HEAD of one global list, so when two classes register the same name the
	// last one loaded is what every lookup answers.
	FElysiumScheduleProgram Shadow;
	Shadow.Name = TEXT("SCHED_WALK");
	Shadow.GlobalId = ElysiumScheduleId::GlobalBase + 0x99;
	Shadow.Tasks.AddDefaulted(7);
	Manager.Add(MoveTemp(Shadow));

	TestEqual(TEXT("the shadowing is counted"), Manager.ShadowedCount(), 1);
	TestEqual(TEXT("and the newest program is what the name answers"),
		Manager.FindByName(TEXT("SCHED_WALK"))->Tasks.Num(), 7);
	TestEqual(TEXT("while the shadowed one is still reachable by its own id"),
		Manager.FindById(ElysiumScheduleId::GlobalBase + 0x01)->Tasks.Num(), 1);

	Manager.Reset();
	TestEqual(TEXT("a reset store is empty"), Manager.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleTextTaskOpsTest,
	"Elysium.Substrate.ScheduleText.TaskOps",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleTextTaskOpsTest::RunTest(const FString&)
{
	FScratchClass Scratch;

	FElysiumTaskOpTable Table;
	Table.Build(Scratch.Tasks);

	// Of the nine names the scratch class registers, five are tasks this runtime has a body for.
	TestEqual(TEXT("the bound identities"), Table.NumPorted(), 5);
	TestEqual(TEXT("the unported ones"), Table.NumUnported(), 4);
	TestTrue(TEXT("a bound id answers its op"),
		Table.Find(ElysiumScheduleId::GlobalBase + 0x00) == EElysiumTaskOp::Wait);
	TestTrue(TEXT("an unbound id answers Unknown -- which is the measurement, not an error"),
		Table.Find(ElysiumScheduleId::GlobalBase + 0x08) == EElysiumTaskOp::Unknown);
	TestTrue(TEXT("an id the table never saw answers Unknown too"),
		Table.Find(12345) == EElysiumTaskOp::Unknown);
	TestEqual(TEXT("every identity keeps its retail name"),
		Table.NameOf(ElysiumScheduleId::GlobalBase + 0x08), FString(TEXT("TASK_DIST")));

	// The reference count is what makes the unported list a work queue rather than an inventory.
	FElysiumScheduleManager Manager;
	Manager.AddAll(ElysiumScheduleText::Parse(TEXT(
		"Schedule SCHED_WALK Tasks\n"
		"  TASK_WAIT 1\n"
		"  TASK_DIST DIST:ACCUM\n"
		"  TASK_DIST DIST:DIALOG\n"
		"  TASK_HINT HintFlags:visible\n"), Scratch.Context()));

	TestEqual(TEXT("three steps name a task with no body"), Table.Measure(Manager), 3);
	TestEqual(TEXT("and the busiest hole is named with its count"),
		Table.UnportedByName().FindChecked(TEXT("TASK_DIST")), 2);
	TestEqual(TEXT("a quieter one too"),
		Table.UnportedByName().FindChecked(TEXT("TASK_HINT")), 1);
	TestEqual(TEXT("an unreferenced identity stays at zero"),
		Table.UnportedByName().FindChecked(TEXT("TASK_MISC")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleTextConditionOrdinalsTest,
	"Elysium.Substrate.ScheduleText.ConditionOrdinals",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleTextConditionOrdinalsTest::RunTest(const FString&)
{
	// A parsed mask is in GLOBAL condition ordinals, and the corpus registers 164 condition names
	// where `EElysiumNpcCond` spells fewer. A bit with no enumerator has to survive anyway.
	FElysiumNpcConditions Mask;
	Mask.SetOrdinal(200);
	TestTrue(TEXT("an ordinal with no enumerator still sets"), Mask.HasOrdinal(200));
	TestEqual(TEXT("and only one bit"), Mask.Num(), 1);
	TestTrue(TEXT("it is legible in a trace as its number"),
		Mask.Describe().Contains(TEXT("COND_200")));

	// An ordinal past the 256 this type holds is a corpus defect, not a condition: refused rather
	// than wrapped onto an innocent bit.
	Mask.SetOrdinal(256);
	Mask.SetOrdinal(-1);
	TestEqual(TEXT("an out-of-range ordinal sets nothing"), Mask.Num(), 1);
	TestFalse(TEXT("and reads as absent"), Mask.HasOrdinal(256));

	// An enumerated identity and its ordinal are the same bit -- which is what made the
	// `+1,000,000,000` a seed rather than a renumbering.
	FElysiumNpcConditions Named;
	Named.Set(EElysiumNpcCond::SeeHate);
	TestTrue(TEXT("an identity is its ordinal"),
		Named.HasOrdinal(static_cast<int32>(EElysiumNpcCond::SeeHate)));

	// `Tick`'s inverted test: an inverted interrupt fires on the ABSENCE of its condition, so what
	// interrupts is the part of the inverted mask the NPC is not reporting.
	FElysiumNpcConditions Inverted;
	Inverted.SetOrdinal(1);
	Inverted.SetOrdinal(2);
	FElysiumNpcConditions Reported;
	Reported.SetOrdinal(2);
	const FElysiumNpcConditions Firing = Inverted.Difference(Reported);
	TestTrue(TEXT("an inverted condition the NPC is NOT reporting fires"), Firing.HasOrdinal(1));
	TestFalse(TEXT("one it IS reporting does not"), Firing.HasOrdinal(2));

	Mask.ClearOrdinal(200);
	TestTrue(TEXT("clearing by ordinal empties it"), Mask.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleTextMiscFlagIndexTest,
	"Elysium.Substrate.ScheduleText.MiscFlagIndex",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleTextMiscFlagIndexTest::RunTest(const FString&)
{
	// `0x1030d390` and `0x1033cb00` walk the same 22 names and answer different things. The parser
	// calls the first; everything else in this runtime calls the second.
	TestEqual(TEXT("the parser's resolver answers the index"),
		ElysiumMiscFlags::ParseScheduleIndex(TEXT("Unconscious")), 0);
	TestEqual(TEXT("index, not mask"),
		ElysiumMiscFlags::ParseScheduleIndex(TEXT("D_Targeted")), 1);
	TestEqual(TEXT("case-insensitively, like every compare in the parser"),
		ElysiumMiscFlags::ParseScheduleIndex(TEXT("fired_gun")), 21);
	TestEqual(TEXT("an unknown name silently reads 0 -- which IS Unconscious"),
		ElysiumMiscFlags::ParseScheduleIndex(TEXT("NoSuchFlag")), 0);

	uint32 Mask = 0;
	TestTrue(TEXT("the mask reader still finds the same name"),
		ElysiumMiscFlags::ParseName(TEXT("D_Targeted"), Mask));
	TestEqual(TEXT("and answers the bit rather than the index"), Mask, 2u);

	TestEqual(TEXT("the table is the 22 at 0x10619ec8"), ElysiumMiscFlags::NumNames(), 22);
	return true;
}

#endif
