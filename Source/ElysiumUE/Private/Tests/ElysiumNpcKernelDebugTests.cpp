#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **Debug**. Every assertion below comes from the decompiled C, the LISTING or
// the `.rdata` of the row it names: the 119 `COND_*` symbols `0x102c8ce0` registers and the 119
// three-letter abbreviations `0x1027e7f0` answers, the four navigation-type names of `0x1027e760`,
// the two scheduling-error class names, the three species short-name blocks and their exact ids,
// `ReportAIState`'s eight `DevMsg` arms in order, `DrawDebugStatOverlays`'s three-way slot dispatch,
// and the eight `m_debugOverlays` gates of `0x10275760` with the box extents and colour bytes the
// listing pushes.
//
// Where a body can only answer "nothing" because its input is a seam — the studio header, the task
// namespace, the node graph, the navigator route, the collision OBB — the case says so: that the
// seam is asked and that the refusal is the recovered one.

static constexpr EAutomationTestFlags GElysiumNpcKernelDebugFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The classnames these cases spawn, checked against BOTH tables: the spawn registry
	// (`Substrate/ElysiumNpcClasses.cpp`) and the census (`ElysiumNpcKernelShape.cpp`).
	//   `npc_VHumanCombatant`  — registered, and claimed by `CNPC_VHumanCombatant`, which has no
	//                            slot-76 override, so it takes the Troika line's body.
	//   `npc_VTzimisceRunner`  — registered, and claimed by `CNPC_VBaseBoss` AND by
	//                            `CNPC_VTzimisceRunner`; the most derived claimant is the latter and
	//                            it fills slot 76 with the BOSS body `0x10366290`.
	// `CNPC_VWerewolf`, `CNPC_VMingXiao`, `CNPC_VMingXiaoTentacle` and `CScriptedTarget` are
	// exercised by retail class NAME through the family's own tables and the census, because no
	// registered classname reaches them.
	const TCHAR* const GDebugCombatant = TEXT("npc_VHumanCombatant");
	const TCHAR* const GDebugBoss = TEXT("npc_VTzimisceRunner");

	// The captured lines' `Retail` column, joined — the shape an arm-order assertion reads.
	FString DebugRetailOrder(const TArray<FElysiumNpc::FDebugLine>& Lines)
	{
		TArray<FString> Parts;
		Parts.Reserve(Lines.Num());
		for (const FElysiumNpc::FDebugLine& Line : Lines)
		{
			Parts.Add(FString(Line.Retail));
		}
		return FString::Join(Parts, TEXT("|"));
	}

	FElysiumNpcWorldBuilder DebugBuilder(const TCHAR* Classname)
	{
		FElysiumNpcWorldBuilder Builder(TEXT("debug"), 29u);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		Builder.AddNpc(TEXT("subject"), FVector::ZeroVector, Classname);
		return Builder;
	}
}

// -------------------------------------------------------------------------------------------------
// The two condition name tables — `DAT_109203dc` and `0x1027e7f0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebugConditionNamesTest,
	"Elysium.Substrate.NpcKernelDebug.ConditionNames", GElysiumNpcKernelDebugFlags)
bool FElysiumNpcKernelDebugConditionNamesTest::RunTest(const FString&)
{
	// `0x102ea020`: -1 is the ONE id with a name of its own.
	TestEqual(TEXT("-1 answers the namespace's null symbol"),
		FString(FElysiumNpc::GlobalConditionName(INDEX_NONE)), FString(TEXT("<<null>>")));

	// The registrar's boundaries and every id whose registration is out of call order — the two
	// places `0x102c8ce0` breaks its own sequence, which is what proves the table is keyed by id.
	const TPair<int32, const TCHAR*> Rows[] = {
		{ 0x00, TEXT("COND_NONE") },
		{ 0x01, TEXT("COND_SEE_UNKNOWN") },
		{ 0x0b, TEXT("COND_DETECTED_ATTACK") },
		{ 0x1e, TEXT("COND_INVESTIGATE_LEVEL") },
		{ 0x22, TEXT("COND_SUPERNATURAL_ATTACK_LEVEL") },
		{ 0x38, TEXT("COND_WAS_BUMPED") },
		{ 0x46, TEXT("COND_SEE_ENEMY") },
		{ 0x49, TEXT("COND_TARGET_OCCLUDED") },
		// registered out of order, between 0x49 and 0x4c
		{ 0x55, TEXT("COND_ENEMY_TOO_FAR") },
		{ 0x4c, TEXT("COND_LIGHT_DAMAGE") },
		{ 0x5c, TEXT("COND_TASK_FAILED") },
		{ 0x63, TEXT("COND_WEAPON_BLOCKED_BY_FRIEND") },
		{ 0x6b, TEXT("COND_HEAR_THUMPER") },
		// registered out of order, between 0x6b and 0x6e
		{ 0x6c, TEXT("COND_HEAR_BUGBAIT") },
		{ 0x6d, TEXT("COND_HEAR_COMBAT") },
		{ 0x73, TEXT("COND_FLOATING_OFF_GROUND") },
		{ 0x76, TEXT("COND_NPC_UNFREEZE") },
	};
	for (const TPair<int32, const TCHAR*>& Row : Rows)
	{
		TestEqual(*FString::Printf(TEXT("condition 0x%02x is %s"), Row.Key, Row.Value),
			FString(FElysiumNpc::GlobalConditionName(Row.Key)), FString(Row.Value));
	}

	// 0x77 is the first id the base registrar does NOT register: the namespace answers no symbol,
	// which retail hands straight to `printf` as a null `%s`.
	TestNull(TEXT("0x77 is not in the base namespace"), FElysiumNpc::GlobalConditionName(0x77));
	TestNull(TEXT("and neither is a script-range id"),
		FElysiumNpc::GlobalConditionName(1000000000));

	// The short table, `0x1027e7f0`, over the same ids. Each abbreviation is the same condition:
	// `fai` is TASK_FAILED, `bmp` is WAS_BUMPED, `fog` is FLOATING_OFF_GROUND — and `fog` is the one
	// string in the block that sits outside it, at `0x1058b0a0`.
	const TPair<int32, const TCHAR*> ShortRows[] = {
		{ 0x00, TEXT("non") }, { 0x01, TEXT("seu") }, { 0x0b, TEXT("dat") },
		{ 0x1e, TEXT("piv") }, { 0x22, TEXT("psa") }, { 0x38, TEXT("bmp") },
		{ 0x46, TEXT("see") }, { 0x49, TEXT("toc") }, { 0x4a, TEXT("els") },
		{ 0x55, TEXT("etf") }, { 0x4c, TEXT("ldm") }, { 0x5c, TEXT("fai") },
		{ 0x63, TEXT("wbf") }, { 0x6b, TEXT("hth") }, { 0x6c, TEXT("hbb") },
		{ 0x73, TEXT("fog") }, { 0x76, TEXT("ufz") },
	};
	for (const TPair<int32, const TCHAR*>& Row : ShortRows)
	{
		TestEqual(*FString::Printf(TEXT("short name 0x%02x is %s"), Row.Key, Row.Value),
			FString(FElysiumNpc::ShortConditionNameTable(Row.Key)), FString(Row.Value));
	}

	// The `default:` arm covers everything outside 0x00..0x76 INCLUDING -1: `0x1027e7f0` has no -1
	// case, unlike the nav-type table beside it.
	for (const int32 Outside : { -1, 0x77, 0x100, 1000000000 })
	{
		TestEqual(*FString::Printf(TEXT("%d takes the *** arm"), Outside),
			FString(FElysiumNpc::ShortConditionNameTable(Outside)), FString(TEXT("***")));
	}

	// Every one of the 119 ids has BOTH a long and a short name, and none of them is the default.
	int32 Covered = 0;
	for (int32 Id = 0; Id <= 0x76; ++Id)
	{
		const TCHAR* Long = FElysiumNpc::GlobalConditionName(Id);
		const TCHAR* Short = FElysiumNpc::ShortConditionNameTable(Id);
		if (Long != nullptr && FCString::Strcmp(Short, TEXT("***")) != 0
			&& FCString::Strlen(Short) == 3 && FString(Long).StartsWith(TEXT("COND_")))
		{
			++Covered;
		}
	}
	TestEqual(TEXT("all 119 base conditions carry both names"), Covered, 0x77);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The id spaces, and slots 458 / 449 over them.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebugIdSpacesTest,
	"Elysium.Substrate.NpcKernelDebug.IdSpaces", GElysiumNpcKernelDebugFlags)
bool FElysiumNpcKernelDebugIdSpacesTest::RunTest(const FString&)
{
	// The CONDITION chain: the Troika line's space is empty and its parent, `CAI_BaseNPC`'s, holds
	// 0x00..0x76 at global base 0 — so the walk skips the first row and translates by identity.
	int32 ConditionCount = 0;
	const FElysiumNpc::FKernelIdSpace* Conditions =
		FElysiumNpc::ConditionIdSpaceRows(ConditionCount);
	TestEqual(TEXT("two condition spaces, Troika then base"), ConditionCount, 2);
	TestEqual(TEXT("the Troika condition space is the 9999 empty sentinel"),
		Conditions[0].LocalBase, 9999);
	TestEqual(TEXT("CAI_BaseNPC's holds 0x00..0x76"), Conditions[1].LocalTop, 0x76);
	TestEqual(TEXT("at global base 0"), Conditions[1].GlobalBase, 0);

	for (const int32 Id : { 0, 1, 0x46, 0x76 })
	{
		TestEqual(*FString::Printf(TEXT("condition %d translates to itself"), Id),
			FElysiumNpc::IdSpaceLocalToGlobal(Conditions, ConditionCount, Id), Id);
	}
	TestEqual(TEXT("-1 stays -1 without walking at all"),
		FElysiumNpc::IdSpaceLocalToGlobal(Conditions, ConditionCount, INDEX_NONE), INDEX_NONE);
	TestEqual(TEXT("0x77 falls off the end of the chain"),
		FElysiumNpc::IdSpaceLocalToGlobal(Conditions, ConditionCount, 0x77), INDEX_NONE);

	// The TASK chain: both rows empty, and the base one is unrecovered rather than known-empty.
	int32 TaskCount = 0;
	const FElysiumNpc::FKernelIdSpace* Tasks = FElysiumNpc::TaskIdSpaceRows(TaskCount);
	TestEqual(TEXT("two task spaces"), TaskCount, 2);
	for (int32 Index = 0; Index < TaskCount; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("%s's task space is empty"), Tasks[Index].RetailClass),
			Tasks[Index].LocalBase, 9999);
	}
	for (const int32 Id : { 0, 1, 0x69, 441 })
	{
		TestEqual(*FString::Printf(TEXT("task %d has nowhere to translate"), Id),
			FElysiumNpc::IdSpaceLocalToGlobal(Tasks, TaskCount, Id), INDEX_NONE);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebugNameSlotsTest,
	"Elysium.Substrate.NpcKernelDebug.NameSlots", GElysiumNpcKernelDebugFlags)
bool FElysiumNpcKernelDebugNameSlotsTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture Fixture(DebugBuilder(GDebugCombatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	// Slot 458 `ConditionName` — the two-step of `0x102cc300`.
	TestEqual(TEXT("ConditionName(-1) is the null symbol"),
		FString(Npc->ConditionName(INDEX_NONE)), FString(TEXT("<<null>>")));
	TestEqual(TEXT("ConditionName(0x46) is COND_SEE_ENEMY"),
		FString(Npc->ConditionName(0x46)), FString(TEXT("COND_SEE_ENEMY")));
	TestEqual(TEXT("and the port's own enum value agrees"),
		FString(Npc->ConditionName(static_cast<int32>(EElysiumNpcCond::SeeEnemy))),
		FString(TEXT("COND_SEE_ENEMY")));
	// An id at or above 1e9 SKIPS the translation and goes straight to the namespace, which carries
	// no script-registered symbols here.
	TestNull(TEXT("a script-range id skips the id space and finds no symbol"),
		Npc->ConditionName(1000000000));

	// Slot 449 `TaskName` — the same shape, and the task namespace is the family's one seam. Every
	// id translates to -1 because both task spaces are the 9999 empty sentinel, and `IdToSymbol(-1)`
	// is `"<<null>>"` — so the seam's refusal is spelled with retail's own null symbol rather than
	// with a null pointer, exactly as it would be for a task id retail failed to translate.
	TestEqual(TEXT("TaskName(-1) is the null symbol"),
		FString(Npc->TaskName(INDEX_NONE)), FString(TEXT("<<null>>")));
	TestEqual(TEXT("and so is every other id: the 441-symbol table is not carried"),
		FString(Npc->TaskName(0x69)), FString(TEXT("<<null>>")));
	// A script-range id skips the translation entirely and reaches the namespace directly, where
	// there is no symbol at all — a null pointer, not the null symbol.
	TestNull(TEXT("a script-range task id finds no symbol"), Npc->TaskName(1000000000));

	// Slot 407 `GetNavTypeName` — `0x1027e760`'s whole jump table, read out of `.rdata`.
	TestEqual(TEXT("0 is Ground"), FString(Npc->GetNavTypeName(0)), FString(TEXT("Ground")));
	TestEqual(TEXT("1 is Jump"), FString(Npc->GetNavTypeName(1)), FString(TEXT("Jump")));
	TestEqual(TEXT("2 is Fly"), FString(Npc->GetNavTypeName(2)), FString(TEXT("Fly")));
	TestEqual(TEXT("3 is Climb"), FString(Npc->GetNavTypeName(3)), FString(TEXT("Climb")));
	TestEqual(TEXT("-1 is None, NOT the empty string 29c's walk read"),
		FString(Npc->GetNavTypeName(INDEX_NONE)), FString(TEXT("None")));
	for (const int32 Outside : { 4, 99 })
	{
		TestEqual(*FString::Printf(TEXT("%d is **UNKNOWN**"), Outside),
			FString(Npc->GetNavTypeName(Outside)), FString(TEXT("**UNKNOWN**")));
	}

	// Slot 451 and its base body: two different class names, and every spawnable leaf is on the
	// Troika line so every one of them answers the second.
	TestEqual(TEXT("the base body answers CAI_BaseNPC"),
		FString(FElysiumNpc::BaseSchedulingErrorName()), FString(TEXT("CAI_BaseNPC")));
	TestEqual(TEXT("the Troika line's override answers CAI_BaseNPCTroika"),
		FString(Npc->GetSchedulingErrorName()), FString(TEXT("CAI_BaseNPCTroika")));

	// Slot 14 `DebugGetClassName` — the address of `CBaseEntity+0x24`, which the shape map binds to
	// the registry's classname.
	TestEqual(TEXT("DebugGetClassName is the spawned classname"),
		FString(Npc->DebugGetClassName()), FString(GDebugCombatant));

	// `CNPC_VTzimisce::GetEventName` — ids 2..8 and nothing else.
	const TCHAR* const Events[] = { TEXT("START_IDLE"), TEXT("START_FIDGET"), TEXT("START_RUN"),
		TEXT("START_LANDHARD"), TEXT("START_ATTACK"), TEXT("START_ATTACKBIG"),
		TEXT("START_POUNCE") };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Events); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("anim event %d is %s"), Index + 2, Events[Index]),
			FString(FElysiumNpc::TzimisceEventName(Index + 2)), FString(Events[Index]));
	}
	for (const int32 Outside : { 0, 1, 9, 100 })
	{
		TestNull(*FString::Printf(TEXT("event %d falls through to CBaseAnimating"), Outside),
			FElysiumNpc::TzimisceEventName(Outside));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 408's species table — every row by name.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebugShortConditionSpeciesTest,
	"Elysium.Substrate.NpcKernelDebug.ShortConditionSpecies", GElysiumNpcKernelDebugFlags)
bool FElysiumNpcKernelDebugShortConditionSpeciesTest::RunTest(const FString&)
{
	int32 Count = 0;
	const FElysiumNpc::FShortConditionSpecies* Rows =
		FElysiumNpc::ShortConditionSpeciesRows(Count);
	TestEqual(TEXT("three census classes override slot 408"), Count, 3);

	// Every row, by name: the class resolves in the census, the census agrees on the body, the block
	// starts at 0x77 — the id straight above the base table's last — and the names are the ones in
	// `.rdata`.
	const TCHAR* const MingXiao[] = { TEXT("xfr"), TEXT("xfl"), TEXT("xmr"), TEXT("xml"),
		TEXT("xbr"), TEXT("xbl"), TEXT("xsp"), TEXT("xmh") };
	const TCHAR* const Tentacle[] = { TEXT("tfl"), TEXT("tsc"), TEXT("tpe") };
	const TCHAR* const Werewolf[] = { TEXT("ww0"), TEXT("ww1"), TEXT("ww2"), TEXT("ww3"),
		TEXT("ww4") };

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FElysiumNpc::FShortConditionSpecies& Row = Rows[Index];
		const FString Name(Row.RetailClass);
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(Row.RetailClass);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), *Name), Cls);
		if (Cls == nullptr)
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s fills slot 408 with %s"), *Name, Row.Body),
			FString(ElysiumNpcKernelClass::BodyOf(Cls, 408)), FString(Row.Body));
		TestEqual(*FString::Printf(TEXT("%s's block starts at 0x77"), *Name), Row.FirstId, 0x77);

		const TCHAR* const* Expected = nullptr;
		int32 ExpectedCount = 0;
		if (Name == TEXT("CNPC_VMingXiao"))
		{
			Expected = MingXiao;
			ExpectedCount = UE_ARRAY_COUNT(MingXiao);
		}
		else if (Name == TEXT("CNPC_VMingXiaoTentacle"))
		{
			Expected = Tentacle;
			ExpectedCount = UE_ARRAY_COUNT(Tentacle);
		}
		else
		{
			Expected = Werewolf;
			ExpectedCount = UE_ARRAY_COUNT(Werewolf);
		}
		TestEqual(*FString::Printf(TEXT("%s's block is %d ids"), *Name, ExpectedCount),
			Row.NameCount, ExpectedCount);
		for (int32 Slot = 0; Slot < FMath::Min(Row.NameCount, ExpectedCount); ++Slot)
		{
			TestEqual(*FString::Printf(TEXT("%s 0x%02x is %s"), *Name, Row.FirstId + Slot,
				Expected[Slot]), FString(Row.Names[Slot]), FString(Expected[Slot]));
		}
	}

	// Only the werewolf's body pushes a scope trace; the other two do not.
	const FElysiumNpc::FShortConditionSpecies* Wolf =
		FElysiumNpc::ShortConditionSpeciesOf(TEXT("CNPC_VWerewolf"));
	TestNotNull(TEXT("the werewolf row exists"), Wolf);
	if (Wolf != nullptr)
	{
		TestTrue(TEXT("and it is the only scope-traced body"), Wolf->bScopeTraced);
	}
	const FElysiumNpc::FShortConditionSpecies* Xiao =
		FElysiumNpc::ShortConditionSpeciesOf(TEXT("CNPC_VMingXiao"));
	TestNotNull(TEXT("the MingXiao row exists"), Xiao);
	if (Xiao != nullptr)
	{
		TestFalse(TEXT("and is not scope traced"), Xiao->bScopeTraced);
	}
	TestNull(TEXT("a class outside the table has no row"),
		FElysiumNpc::ShortConditionSpeciesOf(TEXT("CNotAClass")));

	// The join the other way: every census override of slot 408 has a row here.
	int32 CensusOverrides = 0;
	bool bEveryOverrideHasARow = true;
	for (const FElysiumNpcClassSlot& Override : ElysiumNpcKernelShape::Overrides())
	{
		if (Override.Slot != 408)
		{
			continue;
		}
		++CensusOverrides;
		const FElysiumNpc::FShortConditionSpecies* Row =
			FElysiumNpc::ShortConditionSpeciesOf(Override.Class);
		if (Row == nullptr || FCString::Strcmp(Row->Body, Override.Address) != 0)
		{
			bEveryOverrideHasARow = false;
			AddError(FString::Printf(TEXT("slot 408 override %s (%s) has no matching table row"),
				Override.Class, Override.Address));
		}
	}
	TestEqual(TEXT("the census records three slot-408 overrides"), CensusOverrides, 3);
	TestTrue(TEXT("and every one of them is a row of this table"), bEveryOverrideHasARow);

	// A spawned combatant has no slot-408 override, so it reads the base table for every id
	// including the ones the species blocks claim.
	FElysiumNpcWorldFixture Fixture(DebugBuilder(GDebugCombatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	TestEqual(TEXT("a combatant reads the base table"),
		FString(Npc->GetShortConditionName(0x46)), FString(TEXT("see")));
	TestEqual(TEXT("and 0x77 is the *** default for it, not a species name"),
		FString(Npc->GetShortConditionName(0x77)), FString(TEXT("***")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 581 `ReportAIState` — the eight DevMsg arms, in order.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebugReportAIStateTest,
	"Elysium.Substrate.NpcKernelDebug.ReportAIState", GElysiumNpcKernelDebugFlags)
bool FElysiumNpcKernelDebugReportAIStateTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture Fixture(DebugBuilder(GDebugCombatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	Npc->Schedule.Clear();

	// `ACT_INVALID` is -1 on BOTH activity words, which is the arm that suppresses the activity
	// line: retail's gate is `m_Activity != -1 && m_IdealActivity != -1`.
	Npc->ActivityNumber = INDEX_NONE;
	Npc->IdealActivityNumber = INDEX_NONE;

	FElysiumNpc::BeginDebugCapture();
	Npc->ReportAIState();
	const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();

	// Retail's order, with the arms this NPC's state does NOT take absent: no activity pair, no
	// schedule, no enemy, not moving. `Leader.` and `\n` are UNCONDITIONAL — retail prints them for
	// every NPC where SDK 2013 gates them on `IsLeader()`.
	TestEqual(TEXT("the arms run in 0x102779a0's order"), DebugRetailOrder(Lines),
		FString(TEXT("%s: |State: %s, |No Schedule, |No enemy |Leader.|\n|")
			TEXT("Yaw speed:%3.1f,Health: %3d\n|Groundent: NULL\n\n")));

	// Every line is on the `DevMsg` channel: `ReportAIState` is the only body in this family that
	// does not go through the message ring.
	bool bAllDevMsg = true;
	for (const FElysiumNpc::FDebugLine& Line : Lines)
	{
		bAllDevMsg = bAllDevMsg && FCString::Strcmp(Line.Channel, TEXT("DevMsg")) == 0;
	}
	TestTrue(TEXT("and all of them are DevMsg"), bAllDevMsg);

	// The classname line is the classname, and the health line carries the seam's 0.0 yaw speed
	// beside the entity's real health.
	TestEqual(TEXT("the first line names the classname"), Lines[0].Text,
		FString::Printf(TEXT("%s: "), GDebugCombatant));
	TestTrue(TEXT("the yaw/health line carries the seam's 0.0 and the live health"),
		Lines[6].Text.StartsWith(TEXT("Yaw speed:0.0,Health:")));

	// Both words set: the activity line appears, and it appears SECOND — between the state line and
	// the schedule line, which is where `0x102779a0` puts it. Setting only one of the two is not
	// enough, which is the whole of the `&&`.
	Npc->ActivityNumber = 3;
	Npc->IdealActivityNumber = INDEX_NONE;
	FElysiumNpc::BeginDebugCapture();
	Npc->ReportAIState();
	TestFalse(TEXT("one activity alone does not open the arm"),
		DebugRetailOrder(FElysiumNpc::EndDebugCapture())
			.Contains(TEXT("Activity: %s  -  Ideal Activity: %s")));

	Npc->IdealActivityNumber = 4;
	FElysiumNpc::BeginDebugCapture();
	Npc->ReportAIState();
	TestTrue(TEXT("both open it, and it lands between State and the schedule"),
		DebugRetailOrder(FElysiumNpc::EndDebugCapture())
			.Contains(TEXT("State: %s, |Activity: %s  -  Ideal Activity: %s\n|No Schedule, ")));
	Npc->ActivityNumber = INDEX_NONE;
	Npc->IdealActivityNumber = INDEX_NONE;

	// With a schedule installed the "No Schedule, " arm is replaced by the schedule name, and the
	// task line does NOT appear because `CurrentRetailTaskNumber` is a seam that answers false.
	Npc->Schedule.Current = EElysiumScheduleId::IdleStand;
	FElysiumNpc::BeginDebugCapture();
	Npc->ReportAIState();
	const TArray<FElysiumNpc::FDebugLine> WithSchedule = FElysiumNpc::EndDebugCapture();
	const FString Order = DebugRetailOrder(WithSchedule);
	TestTrue(TEXT("the schedule arm replaces the no-schedule arm"),
		Order.Contains(TEXT("Schedule %s, ")) && !Order.Contains(TEXT("No Schedule, ")));
	TestFalse(TEXT("and the task line stays absent: the task NUMBER is a seam"),
		Order.Contains(TEXT("Task %d (#%d), ")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 76 `DrawDebugStatOverlays` — the three-way dispatch.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebugStatOverlaysTest,
	"Elysium.Substrate.NpcKernelDebug.StatOverlays", GElysiumNpcKernelDebugFlags)
bool FElysiumNpcKernelDebugStatOverlaysTest::RunTest(const FString&)
{
	// The census is what the dispatch reads, so assert it first: the boss body and the Troika body
	// are two different addresses at the same slot.
	TestEqual(TEXT("CNPC_VBaseBoss fills slot 76 with the distance body"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VBaseBoss")), 76)),
		FString(TEXT("0x10366290")));
	TestEqual(TEXT("CNPC_VTzimisceRunner inherits it"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VTzimisceRunner")), 76)),
		FString(TEXT("0x10366290")));
	TestEqual(TEXT("CAI_BaseNPCTroika's own body is the expression dump"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CAI_BaseNPCTroika")), 76)),
		FString(TEXT("0x1029c010")));

	// A combatant with no dialogue: `0x1029c010`'s first arm tail-calls the base, so the lines are
	// the BASE body's and the expression dump never runs. And the base body's own first gate is
	// `GetModelPtr() != NULL`, which a bodiless fixture NPC fails — so nothing is printed at all,
	// and that refusal is the recovered one.
	{
		FElysiumNpcWorldFixture Fixture(DebugBuilder(GDebugCombatant));
		FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
		if (!TestNotNull(TEXT("the combatant spawned"), Npc))
		{
			return false;
		}
		FElysiumNpcWorldFixture::Quiet({ Npc });

		Npc->Model.Reset();
		FElysiumNpc::BeginDebugCapture();
		Npc->DrawDebugStatOverlays();
		TestEqual(TEXT("no model, no lines"), FElysiumNpc::EndDebugCapture().Num(), 0);

		// Give it a model and the base body's fixed head runs: the sequence line takes the
		// `(INVALID)` arm because the studio-header seam refuses, then the cycle, then the activity
		// arm, the state, the move type — and it STOPS at the schedule gate with no "no schedule"
		// line, which is what separates this body from `ReportAIState`.
		Npc->Model = TEXT("models/character/npc/unique/test.mdl");
		Npc->Schedule.Clear();
		FElysiumNpc::BeginDebugCapture();
		Npc->DrawDebugStatOverlays();
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("the base body's five unconditional lines"), DebugRetailOrder(Lines),
			FString(TEXT("Seq: (INVALID)|Cycle: %.2f|Actv: RESET|State: %s, |Move: %s, ")));
		TestTrue(TEXT("the move type names the nav-type seam's -1, which is None"),
			Lines.Last().Text == FString(TEXT("Move: None, ")));

		// Run a schedule and the tail appears: the `Schd:` line, the current task and one line per
		// task with `->`/`<-` on the current index and `Task:` only on the first.
		Npc->Schedule.Current = EElysiumScheduleId::IdleStand;
		Npc->Schedule.TaskIndex = 0;
		FElysiumNpc::BeginDebugCapture();
		Npc->DrawDebugStatOverlays();
		const TArray<FElysiumNpc::FDebugLine> WithSchedule = FElysiumNpc::EndDebugCapture();
		const FString Order = DebugRetailOrder(WithSchedule);
		TestTrue(TEXT("the schedule line appears"), Order.Contains(TEXT("Schd: %s, ")));
		TestTrue(TEXT("and the per-task lines after it"), Order.Contains(TEXT("%s%s%s%s")));

		const FElysiumSchedule* Program = ElysiumScheduleFor(EElysiumScheduleId::IdleStand);
		if (Program != nullptr && Program->Tasks.Num() > 0)
		{
			// The first per-task line: prefix `Task:`, the current marker `->`, the step's name and
			// the closing `<-`. The decompiled C swaps the two markers; the listing does not.
			int32 First = INDEX_NONE;
			for (int32 Index = 0; Index < WithSchedule.Num(); ++Index)
			{
				if (FCString::Strcmp(WithSchedule[Index].Retail, TEXT("%s%s%s%s")) == 0)
				{
					First = Index;
					break;
				}
			}
			if (TestTrue(TEXT("a per-task line was emitted"), First != INDEX_NONE))
			{
				TestEqual(TEXT("task 0 is prefixed and both markers bracket it"),
					WithSchedule[First].Text,
					FString::Printf(TEXT("Task:->%s<-"),
						ElysiumTaskName(Program->Tasks[0].Task)));
				if (Program->Tasks.Num() > 1)
				{
					TestEqual(TEXT("task 1 is indented seven spaces with no markers"),
						WithSchedule[First + 1].Text,
						FString::Printf(TEXT("          %s"),
							ElysiumTaskName(Program->Tasks[1].Task)));
				}
			}
		}
	}

	// A boss takes `0x10366290`: the distance line FIRST, then the base body — never the Troika
	// expression dump, even with a dialogue set.
	{
		FElysiumNpcWorldFixture Fixture(DebugBuilder(GDebugBoss));
		FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
		if (!TestNotNull(TEXT("the boss spawned"), Npc))
		{
			return false;
		}
		FElysiumNpcWorldFixture::Quiet({ Npc });
		Npc->Model.Reset();
		Npc->Senses.Memory.ClosestPlayerDistanceCm = 254.f;   // 100 source units

		FElysiumNpc::BeginDebugCapture();
		Npc->DrawDebugStatOverlays();
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("the distance line is the boss body's only output with no model"),
			Lines.Num(), 1);
		if (Lines.Num() == 1)
		{
			TestEqual(TEXT("and it is the retail literal"), FString(Lines[0].Retail),
				FString(TEXT("Dist to player: %.3f")));
			TestEqual(TEXT("with the distance in SOURCE units"), Lines[0].Text,
				FString(TEXT("Dist to player: 100.000")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebugTroikaStatOverlaysTest,
	"Elysium.Substrate.NpcKernelDebug.TroikaStatOverlays", GElysiumNpcKernelDebugFlags)
bool FElysiumNpcKernelDebugTroikaStatOverlaysTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture Fixture(DebugBuilder(GDebugCombatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	// `0x1029c010` past its `m_iDialog` gate. The three list arms — the expression rows, the four
	// anim layers and the scene time — are seams that answer nothing, so what remains is the fixed
	// spine plus the two conditional lines.
	Npc->Dialogue.DialogQue.Reset();
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugStatOverlays();
	const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
	TestEqual(TEXT("the Troika dump's recovered order"), DebugRetailOrder(Lines),
		FString(TEXT("Expression / Gesture information for %s:|Seq (%.2f): %s / %s |")
			TEXT("Disposition:  %s,  Expression: %s (%.2f)|No Scene Entity|")
			TEXT("Eye targets -  current: %d  default: %d  step: %d|Talk Time Remaining: %.2f")));

	// The expression name is `"None"` when no expression is set — `IsValidExpressionIndex`'s false
	// arm, `0x10547418`.
	TestTrue(TEXT("an NPC with no expression reads None"),
		Lines[2].Text.Contains(TEXT("Expression: None")));
	// The eye-target line carries the two seams' -1 default beside the live step.
	TestTrue(TEXT("the default eye target is the seam's -1"),
		Lines[4].Text.Contains(TEXT("default: -1")));

	// A queued dialogue line adds exactly one more, between the look-at arm and the talk time.
	Npc->Dialogue.DialogQue = TEXT("line.wav");
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugStatOverlays();
	const TArray<FElysiumNpc::FDebugLine> Queued = FElysiumNpc::EndDebugCapture();
	TestTrue(TEXT("the queued-dialog line appears"),
		DebugRetailOrder(Queued).Contains(TEXT("Qued Dialog: %s|Talk Time Remaining: %.2f")));

	// The talk-time clamp: a past deadline reads 0.00 rather than a negative.
	Npc->TalkingUntil = -100.0;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugStatOverlays();
	const TArray<FElysiumNpc::FDebugLine> Clamped = FElysiumNpc::EndDebugCapture();
	TestEqual(TEXT("an expired talk time is floored at 0.0, _DAT_104454c4"),
		Clamped.Last().Text, FString(TEXT("Talk Time Remaining: 0.00")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 123's base body — the eight gates, in order, with the listing's literals.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebugGeometryOverlaysTest,
	"Elysium.Substrate.NpcKernelDebug.GeometryOverlays", GElysiumNpcKernelDebugFlags)
bool FElysiumNpcKernelDebugGeometryOverlaysTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture Fixture(DebugBuilder(GDebugCombatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	Npc->Schedule.Clear();

	// Nothing set: every gate is closed and the two tail calls draw nothing.
	Npc->DebugOverlays = 0;
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugGeometryOverlays();
	TestEqual(TEXT("a zero m_debugOverlays draws nothing"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	// `0x1000`: the collision box. The OBB seam refuses, which is retail's DEGENERATE arm — a ±5
	// box at the origin in orange at alpha 20, NOT the ±5 red box the other arm would draw.
	Npc->DebugOverlays = 0x1000;
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugGeometryOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("one box"), Lines.Num(), 1);
		if (Lines.Num() == 1)
		{
			TestEqual(TEXT("through NDebugOverlay::Box"), FString(Lines[0].Retail),
				FString(TEXT("NDebugOverlay::Box")));
			TestEqual(TEXT("with the degenerate arm's extents and colour"), Lines[0].Text,
				FString(TEXT("(0.0 0.0 0.0) mins=(-5.0 -5.0 -5.0) maxs=(5.0 5.0 5.0) ")
					TEXT("rgba=(255 128 0 20)")));
		}
	}

	// `0x2000`: the nearest nav node. The node graph is a seam, so the arm runs and draws nothing.
	Npc->DebugOverlays = 0x2000;
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugGeometryOverlays();
	TestEqual(TEXT("the nav-node arm asks the seam and draws nothing"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	// `0x4000`: the navigator route, also a seam.
	Npc->DebugOverlays = 0x4000;
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugGeometryOverlays();
	TestEqual(TEXT("the route arm asks the seam and draws nothing"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	// `0x400000`: the view cone. **Dead for every NPC this runtime stands** — the second gate is
	// `m_pBaseNPCTroika == 0` and this leaf IS the Troika line. Assert the gate, not a drawing.
	TestTrue(TEXT("this leaf is always a CAI_BaseNPCTroika"), Npc->IsBaseNpcTroika());
	Npc->DebugOverlays = 0x400000;
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugGeometryOverlays();
	TestEqual(TEXT("so the view-cone arm never runs"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	// `0x200000`: the enemy and target lines. With neither set, nothing; the arm is exercised by the
	// gate alone because `GetEnemy()` and `m_hTargetEnt` are both empty on a quiet fixture NPC.
	Npc->DebugOverlays = 0x200000;
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugGeometryOverlays();
	TestEqual(TEXT("no enemy and no target means no lines"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	// `0x20000`: the enemy-memory labels. An empty memory means no records to label.
	Npc->DebugOverlays = 0x20000;
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugGeometryOverlays();
	TestEqual(TEXT("an empty CAI_Memory labels nothing"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	// The un-gated save-position arm: it is NOT a `m_debugOverlays` bit at all. It fires when the
	// NPC is running `SCHED_FORCED_GO` — retail's id 0x39 — and draws a magenta wireframe ±5 box at
	// `m_vSavePosition`. `ElysiumScheduleNumber` is what makes the number the gate rather than a
	// port-side enum comparison.
	Npc->DebugOverlays = 0;
	bool bFoundForcedGo = false;
	for (int32 Raw = 0; Raw <= static_cast<int32>(EElysiumScheduleId::ScriptedFollowPath); ++Raw)
	{
		const EElysiumScheduleId Id = static_cast<EElysiumScheduleId>(Raw);
		if (ElysiumScheduleNumber(Id) == 0x39)
		{
			Npc->Schedule.Current = Id;
			bFoundForcedGo = true;
			break;
		}
	}
	if (bFoundForcedGo)
	{
		Npc->LastPosition = FVector(254.f, 0.f, 0.f);   // 100 source units
		FElysiumNpc::BeginDebugCapture();
		Npc->BaseDrawDebugGeometryOverlays();
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("SCHED_FORCED_GO draws the save-position box"), Lines.Num(), 1);
		if (Lines.Num() == 1)
		{
			TestEqual(TEXT("magenta, alpha 0, half-extent 5, at m_vSavePosition"), Lines[0].Text,
				FString(TEXT("(100.0 0.0 0.0) mins=(-5.0 -5.0 -5.0) maxs=(5.0 5.0 5.0) ")
					TEXT("rgba=(255 0 255 0)")));
		}
		Npc->Schedule.Clear();
	}
	else
	{
		AddInfo(TEXT("this runtime registers no schedule at retail number 0x39 (SCHED_FORCED_GO), "
			"so the save-position arm has no program to fire on"));
	}

	// `0x10000`: the ZAP bit, and it is not a drawing at all. It vacates the squad slot, drops the
	// active weapon and schedules removal — and it does NOT clear the bit, so it re-fires on every
	// call. The observable half here is the removal.
	Npc->DebugOverlays = 0x10000;
	TestFalse(TEXT("the NPC is alive before the zap arm"), Npc->bDead);
	Npc->BaseDrawDebugGeometryOverlays();
	TestTrue(TEXT("the zap arm removes it"), Npc->bDead);
	TestEqual(TEXT("and leaves the bit set, so it would fire again"),
		Npc->DebugOverlays & 0x10000, 0x10000);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CScriptedTarget` and `CAI_Hint` — slots 123 and 124 on two classes no map stands here.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebugForeignOverlaysTest,
	"Elysium.Substrate.NpcKernelDebug.ForeignOverlays", GElysiumNpcKernelDebugFlags)
bool FElysiumNpcKernelDebugForeignOverlaysTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture Fixture(DebugBuilder(GDebugCombatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	// `CScriptedTarget` is not a spawnable leaf; the census gives it no entity classname. The body
	// is exercised against the two words family Species declared for it.
	TestNull(TEXT("no classname resolves to CScriptedTarget"),
		ElysiumNpcKernelClass::OfClassname(TEXT("scripted_target")));

	// The gate is one test of two bits, 0x4 OR 0x20.
	Npc->DebugOverlays = 0;
	FElysiumNpc::BeginDebugCapture();
	Npc->ScriptedTargetDrawDebugGeometryOverlays();
	TestEqual(TEXT("neither 0x4 nor 0x20 draws nothing"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	Npc->bScriptedTargetDisabled = false;
	Npc->ScriptedTargetLastPositionUnits = FVector(10.f, 0.f, 0.f);
	for (const int32 Bit : { 0x4, 0x20 })
	{
		Npc->DebugOverlays = Bit;
		FElysiumNpc::BeginDebugCapture();
		Npc->ScriptedTargetDrawDebugGeometryOverlays();
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(*FString::Printf(TEXT("bit 0x%x opens the same gate"), Bit), Lines.Num(), 3);
		if (Lines.Num() == 3)
		{
			TestEqual(TEXT("the enabled arm's white +-8 box at m_vLastPosition"), Lines[0].Text,
				FString(TEXT("(10.0 0.0 0.0) mins=(-8.0 -8.0 -8.0) maxs=(8.0 8.0 8.0) ")
					TEXT("rgba=(255 255 255 0)")));
			TestEqual(TEXT("then the red +-5 box at the origin"), Lines[1].Text,
				FString(TEXT("(0.0 0.0 0.0) mins=(-5.0 -5.0 -5.0) maxs=(5.0 5.0 5.0) ")
					TEXT("rgba=(255 0 0 0)")));
			TestEqual(TEXT("then the line to m_vLastPosition"), FString(Lines[2].Retail),
				FString(TEXT("NDebugOverlay::Line")));
		}
	}

	// Disabled: ONE box, colour (200,100,100), and the listing hands its maxs where the mins go.
	Npc->bScriptedTargetDisabled = true;
	Npc->DebugOverlays = 0x24;
	FElysiumNpc::BeginDebugCapture();
	Npc->ScriptedTargetDrawDebugGeometryOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("the disabled arm draws one box"), Lines.Num(), 1);
		if (Lines.Num() == 1)
		{
			TestEqual(TEXT("with the mins and maxs the listing pushes, in that order"),
				Lines[0].Text,
				FString(TEXT("(0.0 0.0 0.0) mins=(5.0 5.0 5.0) maxs=(-5.0 -5.0 -5.0) ")
					TEXT("rgba=(200 100 100 0)")));
		}
	}

	// Slot 124 on the same class: three numbered lines and a return of base + 3.
	Npc->DebugOverlays = 0;
	TestEqual(TEXT("without the 0x1 bit the line count is unchanged"),
		Npc->ScriptedTargetDrawDebugTextOverlays(), 0);

	Npc->DebugOverlays = 0x1;
	Npc->bScriptedTargetDisabled = false;
	FElysiumNpc::BeginDebugCapture();
	const int32 Next = Npc->ScriptedTargetDrawDebugTextOverlays();
	const TArray<FElysiumNpc::FDebugLine> Text = FElysiumNpc::EndDebugCapture();
	TestEqual(TEXT("three lines"), Text.Num(), 3);
	TestEqual(TEXT("and the next free line is base + 3"), Next, 3);
	if (Text.Num() == 3)
	{
		TestEqual(TEXT("line 0 is the enabled state"), Text[0].Text, FString(TEXT("State: On")));
		TestEqual(TEXT("numbered 0"), Text[0].Line, 0);
		TestEqual(TEXT("line 1 is the absent Next target"), Text[1].Text,
			FString(TEXT("Next: -NONE-")));
		TestEqual(TEXT("line 2 is -LOOKING- while enabled"), Text[2].Text,
			FString(TEXT("User: -LOOKING-")));
		TestEqual(TEXT("numbered 2"), Text[2].Line, 2);
	}
	// Disabled flips the third line's no-target spelling and nothing else.
	Npc->bScriptedTargetDisabled = true;
	FElysiumNpc::BeginDebugCapture();
	Npc->ScriptedTargetDrawDebugTextOverlays();
	const TArray<FElysiumNpc::FDebugLine> Off = FElysiumNpc::EndDebugCapture();
	if (TestEqual(TEXT("still three lines"), Off.Num(), 3))
	{
		TestEqual(TEXT("State: Off"), Off[0].Text, FString(TEXT("State: Off")));
		TestEqual(TEXT("and -NONE- rather than -LOOKING-"), Off[2].Text,
			FString(TEXT("User: -NONE-")));
	}

	// `CAI_Hint::DrawDebugTextOverlays` — the two words are parameters because no hint store carries
	// them. The first format is a BARE `%i`, with no label at all.
	TestEqual(TEXT("without the 0x1 bit the hint adds no lines"),
		FElysiumNpc::HintDrawDebugTextOverlays(4, 0, 13, 10.0, 0.0), 4);

	FElysiumNpc::BeginDebugCapture();
	const int32 HintNext = FElysiumNpc::HintDrawDebugTextOverlays(4, 0x1, 13, 10.0, 2.5);
	const TArray<FElysiumNpc::FDebugLine> Hint = FElysiumNpc::EndDebugCapture();
	TestEqual(TEXT("two lines, starting at the line it was handed"), Hint.Num(), 2);
	TestEqual(TEXT("and the next free line is that plus two"), HintNext, 6);
	if (Hint.Num() == 2)
	{
		TestEqual(TEXT("the hint type prints bare, no label"), Hint[0].Text, FString(TEXT("13")));
		TestEqual(TEXT("numbered from the caller's line"), Hint[0].Line, 4);
		TestEqual(TEXT("the delay is the remaining wait"), Hint[1].Text,
			FString(TEXT("delay 7.500000")));
	}
	// The floor is `_DAT_104454c4` = 0.0: an expired hint reads zero, never a negative.
	FElysiumNpc::BeginDebugCapture();
	FElysiumNpc::HintDrawDebugTextOverlays(0, 0x1, 3, 1.0, 99.0);
	const TArray<FElysiumNpc::FDebugLine> Expired = FElysiumNpc::EndDebugCapture();
	if (TestEqual(TEXT("two lines for an expired hint too"), Expired.Num(), 2))
	{
		TestEqual(TEXT("floored at 0.0"), Expired[1].Text, FString(TEXT("delay 0.000000")));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VWerewolf` — slot 620 and the hull draw.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebugWerewolfDrawsTest,
	"Elysium.Substrate.NpcKernelDebug.WerewolfDraws", GElysiumNpcKernelDebugFlags)
bool FElysiumNpcKernelDebugWerewolfDrawsTest::RunTest(const FString&)
{
	// **Slot 620 is not one virtual across the hierarchy.** The census says `CNPC_VWerewolf#620` is
	// `DrawBBoxOverlay` (`0x103d5050`) while `CNPC_VSabbatLeader#620` is `FootstepSound`
	// (`0x103aa5e0`) — two classes whose vtables diverge before that index. So the dispatch has to
	// check the ADDRESS and not merely "does this class override slot 620", and that is what the
	// body does.
	const FElysiumNpcClass* Wolf = ElysiumNpcKernelClass::Find(TEXT("CNPC_VWerewolf"));
	TestNotNull(TEXT("CNPC_VWerewolf is a census class"), Wolf);
	if (Wolf != nullptr)
	{
		TestEqual(TEXT("and fills slot 620 with DrawBBoxOverlay"),
			FString(ElysiumNpcKernelClass::BodyOf(Wolf, 620)), FString(TEXT("0x103d5050")));
	}
	const FElysiumNpcClass* Sabbat = ElysiumNpcKernelClass::Find(TEXT("CNPC_VSabbatLeader"));
	if (Sabbat != nullptr)
	{
		TestEqual(TEXT("while CNPC_VSabbatLeader's slot 620 is a different function entirely"),
			FString(ElysiumNpcKernelClass::BodyOf(Sabbat, 620)), FString(TEXT("0x103aa5e0")));
	}
	// No registered classname reaches `CNPC_VWerewolf`, which is why the body is exercised by the
	// census rather than by a spawn.
	TestNull(TEXT("no spawn classname resolves to CNPC_VWerewolf"),
		ElysiumNpcKernelClass::OfClassname(TEXT("npc_VWerewolf")));

	FElysiumNpcWorldFixture Fixture(DebugBuilder(GDebugCombatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	// A non-werewolf falls straight through to `CBaseEntity::DrawBBoxOverlay`, which is a seam.
	FElysiumNpc::BeginDebugCapture();
	Npc->DrawBBoxOverlay();
	TestEqual(TEXT("a combatant draws no recoloured box"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	// `DrawDebugHullAtPoint`: the box is drawn at the point it is handed, in the recovered colour
	// (255, 100, 0) at alpha 100, with the hull extents the seam refuses — so a degenerate box, and
	// the radius the second call carries is 0.
	FElysiumNpc::BeginDebugCapture();
	Npc->DrawDebugHullAtPoint(FVector(12.f, -3.f, 4.f), 0.f);
	const TArray<FElysiumNpc::FDebugLine> Hull = FElysiumNpc::EndDebugCapture();
	TestEqual(TEXT("a box and the unrecovered second call"), Hull.Num(), 2);
	if (Hull.Num() == 2)
	{
		TestEqual(TEXT("the box carries retail's colour and alpha"), Hull[0].Text,
			FString(TEXT("(12.0 -3.0 4.0) mins=(0.0 0.0 0.0) maxs=(0.0 0.0 0.0) ")
				TEXT("rgba=(255 100 0 100)")));
		TestEqual(TEXT("and the second call is emitted under its address, unrecovered"),
			FString(Hull[1].Retail), FString(TEXT("0x1000566e")));
	}
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
