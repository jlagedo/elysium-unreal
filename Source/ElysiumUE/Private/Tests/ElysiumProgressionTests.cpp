// Content-free Substrate automation: quest state, quest presentation projection, and character generation.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "ElysiumAppState.h"
#include "ElysiumAudioLatency.h"
#include "ElysiumBinds.h"
#include "ElysiumBrushComponent.h"
#include "Player/ElysiumCameraShots.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraRig.h"
#include "ElysiumCameraSolve.h"
#include "Substrate/ElysiumCameraTrack.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumCommands.h"
#include "ElysiumContentPaths.h"
#include "Debug/ElysiumChannelRecorder.h"
#include "Debug/ElysiumConsole.h"
#include "Debug/ElysiumLogTap.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumDecals.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumNpcBody.h"
#include "Visual/ElysiumLightRig.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEnvironment.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumStub.h"
#include "ElysiumWeatherState.h"
#include "ElysiumFog.h"
#include "ElysiumEventQueue.h"
#include "ElysiumWireReport.h"
#include "ElysiumExpr.h"
#include "ElysiumGaitSpeeds.h"               // the animation's per-direction speed (CCC7)
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumGymSpec.h"
#include "Visual/ElysiumPoseDeviation.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumLineService.h"
#include "ElysiumLookCurve.h"                // the mouse path's pure rules (CCC3)
#include "Debug/ElysiumMoveCourses.h"        // the event-timed press's pure half (CCC3)
#include "ElysiumMapActor.h"
#include "ElysiumMapEpoch.h"
#include "Map/ElysiumFeedTargeting.h"
#include "Map/ElysiumMapCollision.h"
#include "ElysiumSoundCache.h"
#include "ElysiumMovementComponent.h"
#include "Visual/ElysiumObjModel.h"
#include "Visual/ElysiumNpcClips.h"
#include "ElysiumLocomotionSample.h"         // the body sample's pure rules (CCC1)
#include "ElysiumMoveSolve.h"                // ElysiumMove::StandViewZ / U — the gaze test's units
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumDisposition.h"    // FElysiumEyeTargetTuning
#include "ElysiumPawn.h"
#include "ElysiumPresentationSubsystem.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumInterestingPlaces.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumQuestLog.h"
#include "Substrate/ElysiumQuestView.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSkillClasses.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumScenePlayer.h"
#include "Substrate/ElysiumSheetMath.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Scripting/ElysiumPythonVM.h"
#include "ElysiumViewState.h"
#include "Visual/ElysiumRopes.h"
#include "Scripting/ElysiumScriptFS.h"
#include "ElysiumScriptHost.h"
#include "Scripting/ElysiumScriptNatives.h"
#include "Tests/ElysiumOverlapTestProbe.h"
#include "Tests/ElysiumTestServices.h"
#include "ElysiumTimeControl.h"
#include "ElysiumUseIcons.h"
#include "ElysiumUserCmd.h"
#include "ElysiumVariant.h"

#include "Math/RotationMatrix.h"
#include "Animation/AnimSequence.h"
#include "Serialization/MemoryWriter.h"
#include "Tests/AutomationCommon.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sound/SoundGenerator.h"
#include "Sound/SoundWaveProcedural.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// One context flag (runs anywhere) + the product filter (this project's own suite bucket).
// EAutomationTestFlags is a strong enum in 5.8, so the constant carries that type (ENUM_CLASS_FLAGS
// makes the `|` yield an EAutomationTestFlags), not int32.
namespace ElysiumProgressionTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// ==================================================================================================
// 9.4d — the quest log: the decision and the journal bookkeeping, with no world and no disk.
// `FElysiumQuestTables` is plain C++ with public arrays, so the catalogue below is hand-built and
// every rule `docs/vtmb/game_runtime.md` -> "Quests" records is driven directly.
// ==================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumQuestLogTest, "Elysium.Substrate.QuestLog", GElysiumTestFlags)

namespace
{
	// Two quests on one table, plus a third carrying a `botch` state — the type no shipped row
	// authors and the only thing that refuses a change.
	FElysiumQuestTables MakeQuestFixture()
	{
		FElysiumQuestTables T;

		FElysiumQuest& Knox = T.Quests[4].AddDefaulted_GetRef();   // santamonica
		Knox.Title = TEXT("Arthur Knox");
		Knox.DisplayName = TEXT("A Bounty For The Hunter");
		Knox.TableIndex = 4;
		Knox.Index = 0;
		for (int32 i = 1; i <= 3; ++i)
		{
			FElysiumQuestState& S = Knox.States.AddDefaulted_GetRef();
			S.Id = i;
			S.Type = (i == 3) ? TEXT("success") : TEXT("incomplete");
			S.Description = FString::Printf(TEXT("state %d"), i);
			if (i == 2) { S.AwardXp = TEXT("Carson01"); S.AwardMoney = 25; }
			if (i == 3) { S.Event = TEXT("G.Knox_Done = 1"); }
		}

		FElysiumQuest& Tut = T.Quests[4].AddDefaulted_GetRef();
		Tut.Title = TEXT("Tutorial");
		Tut.DisplayName = TEXT("Learning The Ropes");
		Tut.TableIndex = 4;
		Tut.Index = 1;
		FElysiumQuestState& TS = Tut.States.AddDefaulted_GetRef();
		TS.Id = 1;
		TS.Type = TEXT("incomplete");

		FElysiumQuest& Bad = T.Quests[0].AddDefaulted_GetRef();    // chinatown
		Bad.Title = TEXT("Botched");
		Bad.DisplayName = TEXT("Botched");
		Bad.TableIndex = 0;
		Bad.Index = 0;
		FElysiumQuestState& B1 = Bad.States.AddDefaulted_GetRef();
		B1.Id = 1; B1.Type = TEXT("botch");
		FElysiumQuestState& B2 = Bad.States.AddDefaulted_GetRef();
		B2.Id = 2; B2.Type = TEXT("incomplete");

		T.Reindex();
		return T;
	}
}

bool FElysiumQuestLogTest::RunTest(const FString&)
{
	using namespace ElysiumQuestLog;
	const FElysiumQuestTables Tables = MakeQuestFixture();
	TArray<FElysiumAssignedQuest> Journal;

	// --- The Type vocabulary: substring, in the engine's order, unknown reads as incomplete -----
	TestTrue(TEXT("incomplete"), ParseType(TEXT("incomplete")) == EType::Incomplete);
	TestTrue(TEXT("success"),    ParseType(TEXT("success"))    == EType::Success);
	TestTrue(TEXT("failure"),    ParseType(TEXT("failure"))    == EType::Failure);
	TestTrue(TEXT("botch"),      ParseType(TEXT("botch"))      == EType::Botch);
	TestTrue(TEXT("a substring match, not equality"),
		ParseType(TEXT("  Success!  ")) == EType::Success);
	TestTrue(TEXT("an unrecognised type reads as incomplete"),
		ParseType(TEXT("qqq")) == EType::Incomplete);
	TestEqual(TEXT("incomplete quest states map to HUD updates"),
		ElysiumQuestNotifications::KindForStateType(TEXT("incomplete")),
		EElysiumNotificationKind::QuestUpdated);
	TestEqual(TEXT("successful quest states map to HUD completions"),
		ElysiumQuestNotifications::KindForStateType(TEXT("success")),
		EElysiumNotificationKind::QuestCompleted);
	TestEqual(TEXT("failed quest states map to HUD failures"),
		ElysiumQuestNotifications::KindForStateType(TEXT("failure")),
		EElysiumNotificationKind::QuestFailed);
	TestEqual(TEXT("botched quest states share the failure presentation"),
		ElysiumQuestNotifications::KindForStateType(TEXT("botch")),
		EElysiumNotificationKind::QuestFailed);

	// --- An unknown title, and a state the quest does not have, do nothing at all ---------------
	FOutcome Out = Apply(Tables, Journal, TEXT("No Such Quest"), 1);
	TestFalse(TEXT("an unknown title does not resolve"), Out.bResolved);
	TestFalse(TEXT("and awards nothing"), Out.bChanged);
	TestEqual(TEXT("and writes no row"), Journal.Num(), 0);

	Out = Apply(Tables, Journal, TEXT("Arthur Knox"), 9);
	TestFalse(TEXT("a state the quest does not have does not resolve"), Out.bResolved);
	TestEqual(TEXT("and writes no row"), Journal.Num(), 0);

	Out = Apply(Tables, Journal, TEXT("Arthur Knox"), 0);
	TestFalse(TEXT("state 0 is unassigned, never a real state"), Out.bResolved);

	// --- First assignment: a row, order 1, and the state's awards -------------------------------
	Out = Apply(Tables, Journal, TEXT("Arthur Knox"), 1);
	TestTrue(TEXT("a first assignment resolves"), Out.bResolved);
	TestTrue(TEXT("and changes"), Out.bChanged);
	TestEqual(TEXT("the first quest of a run gets order 1"), Out.Order, 1);
	TestEqual(TEXT("one row"), Journal.Num(), 1);
	TestEqual(TEXT("on the right table"), Journal[0].Table, 4);
	TestEqual(TEXT("at the right quest"), Journal[0].Quest, 0);
	TestEqual(TEXT("at the right state"), Journal[0].State, 1);
	TestTrue(TEXT("marked unread"), Journal[0].bUnread);
	TestEqual(TEXT("state 1 awards no xp"), Out.AwardXpKey, FString());

	// --- A repeat set is a no-op: no award, no re-run -------------------------------------------
	Out = Apply(Tables, Journal, TEXT("Arthur Knox"), 1);
	TestTrue(TEXT("a repeat set still resolves"), Out.bResolved);
	TestFalse(TEXT("but does not change"), Out.bChanged);
	TestEqual(TEXT("and owes no xp"), Out.AwardXpKey, FString());
	TestEqual(TEXT("and owes no money"), Out.AwardMoney, 0);
	TestEqual(TEXT("still one row"), Journal.Num(), 1);

	// --- A forward change replaces the row in place and pays out --------------------------------
	Out = Apply(Tables, Journal, TEXT("Arthur Knox"), 2);
	TestTrue(TEXT("a forward change changes"), Out.bChanged);
	TestEqual(TEXT("the row is replaced, not appended"), Journal.Num(), 1);
	TestEqual(TEXT("at the new state"), Journal[0].State, 2);
	TestEqual(TEXT("keeping its order"), Journal[0].Order, 1);
	TestEqual(TEXT("and owes the authored key"), Out.AwardXpKey, FString(TEXT("Carson01")));
	TestEqual(TEXT("and the authored money"), Out.AwardMoney, 25);

	// --- Backwards fires too: the gate tests inequality, not direction --------------------------
	Out = Apply(Tables, Journal, TEXT("Arthur Knox"), 1);
	TestTrue(TEXT("a backwards move changes"), Out.bChanged);
	TestEqual(TEXT("and lands on the earlier state"), Journal[0].State, 1);

	// --- The title match is case- and whitespace-insensitive, and the row keeps the catalogue's --
	Out = Apply(Tables, Journal, TEXT("  arthur KNOX  "), 3);
	TestTrue(TEXT("a differently-cased, padded title resolves"), Out.bResolved);
	TestEqual(TEXT("and does not become a second row"), Journal.Num(), 1);
	TestEqual(TEXT("the row keeps the catalogue's spelling"),
		Journal[0].Title, FString(TEXT("Arthur Knox")));
	TestEqual(TEXT("state 3 carries the Event"), Out.Event, FString(TEXT("G.Knox_Done = 1")));

	// --- The second quest assigned gets order 2 -------------------------------------------------
	Out = Apply(Tables, Journal, TEXT("Tutorial"), 1);
	TestEqual(TEXT("the second quest assigned gets order 2"), Out.Order, 2);
	TestEqual(TEXT("two rows"), Journal.Num(), 2);

	// --- Leaving a `botch` state is refused outright --------------------------------------------
	TArray<FElysiumAssignedQuest> Botch;
	Out = Apply(Tables, Botch, TEXT("Botched"), 1);
	TestTrue(TEXT("entering a botched state is allowed"), Out.bChanged);
	Out = Apply(Tables, Botch, TEXT("Botched"), 2);
	TestTrue(TEXT("leaving it is refused"), Out.bBotched);
	TestFalse(TEXT("with nothing awarded"), Out.bChanged);
	TestEqual(TEXT("and the row untouched"), Botch[0].State, 1);

	// --- FindRow is the same case-insensitive match ---------------------------------------------
	TestNotNull(TEXT("FindRow matches case-insensitively"),
		FindRow(Journal, TEXT("ARTHUR knox")));
	TestNull(TEXT("and misses an unassigned quest"), FindRow(Journal, TEXT("Botched")));

	// --- Ordinal addressing is what SetQuest uses, not the authored ID --------------------------
	const FElysiumQuest* Knox = Tables.Find(TEXT("Arthur Knox"));
	TestNotNull(TEXT("the fixture quest resolves"), Knox);
	if (Knox)
	{
		TestNotNull(TEXT("ordinal 1 is the first state in file order"), Knox->StateByOrdinal(1));
		TestNull(TEXT("ordinal 0 is not a state"), Knox->StateByOrdinal(0));
		TestNull(TEXT("nor is one past the end"), Knox->StateByOrdinal(4));
		TestNull(TEXT("nor one past VtMB's 20-state cap"), Knox->StateByOrdinal(21));
	}

	return true;
}

// ==================================================================================================
// The journal as a screen reads it (9.4e) — `ElysiumQuestView`. The read side of the same rows
// `ElysiumQuestLog` writes: which column a row lands in, which hub tab shows it, and what order the
// list is in. Pure over a catalogue and an array, so the whole screen's model is testable with no
// world, no subsystem and no viewport.
// ==================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumQuestViewTest, "Elysium.Substrate.QuestView", GElysiumTestFlags)

bool FElysiumQuestViewTest::RunTest(const FString&)
{
	using namespace ElysiumQuestView;
	constexpr int32 SantaMonica = 4;
	constexpr int32 Chinatown   = 0;
	constexpr int32 Downtown    = 1;
	constexpr int32 Main        = FElysiumQuestTables::MainTable;

	FElysiumQuestTables Tables = MakeQuestFixture();

	// A cross-hub quest, which is the case the hub tabs cannot express on their own.
	FElysiumQuest& Plot = Tables.Quests[Main].AddDefaulted_GetRef();
	Plot.Title = TEXT("The Sarcophagus");
	Plot.DisplayName = TEXT("The Epic Of The Ankaran Sarcophagus");
	Plot.TableIndex = Main;
	Plot.Index = 0;
	FElysiumQuestState& PS1 = Plot.States.AddDefaulted_GetRef();
	PS1.Id = 1; PS1.Type = TEXT("incomplete"); PS1.Description = TEXT("The prince wants it NOW!");
	FElysiumQuestState& PS2 = Plot.States.AddDefaulted_GetRef();
	PS2.Id = 2; PS2.Type = TEXT("failure"); PS2.Description = TEXT("Gone.");
	Tables.Reindex();

	// --- the empty journal is a real state, not a degenerate one --------------------------------
	{
		const FView Empty = Build(Tables, {}, SantaMonica);
		TestEqual(TEXT("nothing active"), Empty.Active.Num(), 0);
		TestEqual(TEXT("nothing completed"), Empty.Completed.Num(), 0);
		TestEqual(TEXT("nothing failed"), Empty.Failed.Num(), 0);
		TestEqual(TEXT("and every tab reads zero"), Empty.HubActive[SantaMonica], 0);
	}

	// One row per column, plus the cross-hub plot quest and a row the catalogue cannot place.
	TArray<FElysiumAssignedQuest> Journal;
	Journal.Add({ TEXT("Tutorial"),        SantaMonica, 1, /*State*/ 1, /*Order*/ 1, /*bUnread*/ false });
	Journal.Add({ TEXT("Arthur Knox"),     SantaMonica, 0, /*State*/ 3, /*Order*/ 2, true });   // success
	Journal.Add({ TEXT("The Sarcophagus"), Main,        0, /*State*/ 1, /*Order*/ 3, false });
	Journal.Add({ TEXT("Botched"),         Chinatown,   0, /*State*/ 1, /*Order*/ 4, false });  // botch
	Journal.Add({ TEXT("Ghost Row"),       SantaMonica, 99, /*State*/ 1, /*Order*/ 5, false }); // no such quest

	const FView SM = Build(Tables, Journal, SantaMonica);

	// --- the type decides the column -------------------------------------------------------------
	TestEqual(TEXT("success lands in Completed"), SM.Completed.Num(), 1);
	if (SM.Completed.Num() == 1)
	{
		TestEqual(TEXT("and carries the catalogue's display name"),
			SM.Completed[0].DisplayName, FString(TEXT("A Bounty For The Hunter")));
		TestTrue(TEXT("and its unread marker"), SM.Completed[0].bUnread);
	}
	TestEqual(TEXT("nothing failed under this hub"), SM.Failed.Num(), 0);

	// Tutorial (incomplete) + the plot quest (cross-hub) + the unplaceable row. `Botched` is
	// Chinatown's, so it is not here even though `botch` counts as still-open.
	TestEqual(TEXT("three rows are open under Santa Monica"), SM.Active.Num(), 3);

	// --- newest assignment first -----------------------------------------------------------------
	if (SM.Active.Num() == 3)
	{
		TestEqual(TEXT("the newest row sorts first"), SM.Active[0].Order, 5);
		TestEqual(TEXT("then the next"), SM.Active[1].Order, 3);
		TestEqual(TEXT("then the oldest"), SM.Active[2].Order, 1);
	}

	// --- a catalogue miss degrades, it does not vanish --------------------------------------------
	const FEntry* Ghost = SM.Active.FindByPredicate(
		[](const FEntry& E) { return E.Title == TEXT("Ghost Row"); });
	TestNotNull(TEXT("a row the catalogue cannot place is still shown"), Ghost);
	if (Ghost)
	{
		TestFalse(TEXT("marked unresolved"), Ghost->bResolved);
		TestEqual(TEXT("headed by its own title"), Ghost->DisplayName, FString(TEXT("Ghost Row")));
		TestTrue(TEXT("with no description to show"), Ghost->Description.IsEmpty());
	}

	// --- `main` rides along in every hub ----------------------------------------------------------
	auto HasPlot = [](const FView& V)
	{
		return V.Active.ContainsByPredicate(
			[](const FEntry& E) { return E.Table == FElysiumQuestTables::MainTable; });
	};
	TestTrue(TEXT("the cross-hub quest shows under Santa Monica"), HasPlot(SM));
	TestTrue(TEXT("and under Downtown"), HasPlot(Build(Tables, Journal, Downtown)));
	TestTrue(TEXT("and under Chinatown"), HasPlot(Build(Tables, Journal, Chinatown)));

	// --- botch is still open, and belongs to its own hub -------------------------------------------
	const FView CT = Build(Tables, Journal, Chinatown);
	TestTrue(TEXT("a botch state reads as still open"),
		CT.Active.ContainsByPredicate([](const FEntry& E) { return E.Title == TEXT("Botched"); }));

	// --- the tab counts describe the TABS, not the journal ------------------------------------------
	// Santa Monica: Tutorial + Ghost Row + the plot quest. Chinatown: Botched + the plot quest.
	// Downtown and Hollywood have only the plot quest — which is exactly why the count is per-tab.
	TestEqual(TEXT("Santa Monica counts three"), SM.HubActive[SantaMonica], 3);
	TestEqual(TEXT("Chinatown counts two"), SM.HubActive[Chinatown], 2);
	TestEqual(TEXT("Downtown counts the plot quest alone"), SM.HubActive[Downtown], 1);

	// --- the completed row is not counted as open ----------------------------------------------------
	{
		TArray<FElysiumAssignedQuest> Done;
		Done.Add({ TEXT("Arthur Knox"), SantaMonica, 0, 3, 1, false });
		const FView V = Build(Tables, Done, SantaMonica);
		TestEqual(TEXT("a finished quest leaves the tab count at zero"), V.HubActive[SantaMonica], 0);
	}

	// --- the opening hub, for a character that has never opened the screen ----------------------------
	{
		TestEqual(TEXT("an empty journal opens on Santa Monica"),
			DefaultHub(Tables, {}), SantaMonica);

		// Chinatown alone has open work, so that is where the screen opens.
		TArray<FElysiumAssignedQuest> OnlyCT;
		OnlyCT.Add({ TEXT("Botched"), Chinatown, 0, 1, 1, false });
		TestEqual(TEXT("otherwise it opens where the work is"),
			DefaultHub(Tables, OnlyCT), Chinatown);
	}

	return true;
}


// ================================================================================================
// Chargen — the pools, the price of a dot, and the row filter (9.4f)
// ================================================================================================

namespace ElysiumChargenTest
{
	// A synthetic `stats.txt`: the real slot indices with hand-authored bounds and prices, so the
	// arithmetic is exercised with no export on disk. The clan half of the pools needs the real
	// `rules_tables.txt` and lives in the Content tier; here `FElysiumChargenRules::Rules` is left
	// null, which zeroes the clan term — exactly what every shipped attribute/ability subpool holds.
	FElysiumStat MakeStat(int32 Index, const TCHAR* Internal, int32 Min, int32 Max, int32 Default)
	{
		FElysiumStat S;
		S.Index = Index;
		S.InternalName = Internal;
		S.Name = Internal;
		S.Min = Min;
		S.Max = Max;
		S.Default = Default;
		S.MinSell = Min;
		S.MaxBuy = Max;
		S.MinExpr = FString::FromInt(Min);
		S.MaxExpr = FString::FromInt(Max);
		return S;
	}

	FElysiumRuleTable MakeTable(const TCHAR* Internal, const TArray<int32>& Values)
	{
		FElysiumRuleTable T;
		T.InternalName = Internal;
		T.Name = Internal;
		T.bClamping = true;
		for (int32 i = 0; i < Values.Num(); ++i)
		{
			T.Rows.Add(i, FString::FromInt(Values[i]));
		}
		return T;
	}

	// The three orderings a test needs, flattened as the file authors them: three category indices
	// per ordering, in primary/secondary/tertiary order.
	const TArray<int32> GOrderLookups =
	{
		0, 1, 2,   // 0 Physical_Social_Mental   / Talents_Skills_Knowledges
		0, 2, 1,   // 1 Physical_Mental_Social   / Talents_Knowledges_Skills
		1, 0, 2,   // 2 Social_Physical_Mental   / Skills_Talents_Knowledges
		1, 2, 0,   // 3
		2, 1, 0,   // 4
		2, 0, 1,   // 5
		0, 1, 2,   // 6 No_Order
	};

	void BuildStats(FElysiumStatTable& Out)
	{
		FElysiumStatContainer& Attribs = Out.Containers[(uint8)EElysiumTraitContainer::Attributes];
		FElysiumStatContainer& Abilities = Out.Containers[(uint8)EElysiumTraitContainer::Abilities];
		FElysiumStatContainer& Disciplines = Out.Containers[(uint8)EElysiumTraitContainer::Disciplines];
		Attribs.InternalName = TEXT("Attributes");
		Abilities.InternalName = TEXT("Abilities");
		Disciplines.InternalName = TEXT("Disciplines");

		// Attributes: the order stat, then Strength(1) .. Wits(9). No `New` — an attribute is priced
		// by `Raise` at every rating, including its first dot.
		FElysiumStat Order = MakeStat(0, TEXT("Attrib_Order"), 0, 6, 0);
		Order.NameMapping = TEXT("AttributeOrder");
		Order.Tables.Add(ElysiumFold(TEXT("Attribute_Order_Lookups")),
			MakeTable(TEXT("Attribute_Order_Lookups"), GOrderLookups));
		Order.Tables.Add(ElysiumFold(TEXT("Subpool_Attribute_Primary_Secondary_Tertiary")),
			MakeTable(TEXT("Subpool_Attribute_Primary_Secondary_Tertiary"), { 2, 1, 0 }));
		Order.Tables.Add(ElysiumFold(TEXT("Subpool_Attribute_Primary_Secondary_Tertiary_Kine")),
			MakeTable(TEXT("Subpool_Attribute_Primary_Secondary_Tertiary_Kine"), { 1, 1, 1 }));
		Attribs.Stats.Add(MoveTemp(Order));

		Attribs.DefaultCosts.Raise.Parse(TEXT("Current_Rating * 4"));
		Attribs.DefaultCosts.New = Attribs.DefaultCosts.Raise;
		Attribs.DefaultCosts.bPresent = true;
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EElysiumTraitContainer::Attributes))
		{
			if (Slot.Index == 0)
			{
				continue;
			}
			const bool bCore = Slot.Index >= 1 && Slot.Index <= 9;
			FElysiumStat S = MakeStat(Slot.Index, Slot.Internal, bCore ? 1 : 0, bCore ? 5 : 100,
				bCore ? 1 : 0);
			if (!bCore)
			{
				// The derived block is priced out of reach, which is how the shipped file keeps it
				// off the screen without a separate "buyable" flag.
				S.Costs.Raise.Parse(TEXT("10000"));
				S.Costs.New = S.Costs.Raise;
				S.Costs.bPresent = true;
			}
			Attribs.Stats.Add(MoveTemp(S));
		}

		// Abilities: a flat `New` of 3 and a per-rating `Raise`, so the 0 -> 1 step takes a different
		// branch from every step after it.
		FElysiumStat AbilityOrder = MakeStat(0, TEXT("Ability_Order"), 0, 6, 0);
		AbilityOrder.NameMapping = TEXT("AbilityOrder");
		AbilityOrder.Tables.Add(ElysiumFold(TEXT("Ability_Order_Lookups")),
			MakeTable(TEXT("Ability_Order_Lookups"), GOrderLookups));
		AbilityOrder.Tables.Add(ElysiumFold(TEXT("Subpool_Ability_Primary_Secondary_Tertiary")),
			MakeTable(TEXT("Subpool_Ability_Primary_Secondary_Tertiary"), { 3, 2, 1 }));
		AbilityOrder.Tables.Add(ElysiumFold(TEXT("Subpool_Ability_Primary_Secondary_Tertiary_Kine")),
			MakeTable(TEXT("Subpool_Ability_Primary_Secondary_Tertiary_Kine"), { 1, 1, 1 }));
		Abilities.Stats.Add(MoveTemp(AbilityOrder));

		Abilities.DefaultCosts.New.Parse(TEXT("3"));
		Abilities.DefaultCosts.Raise.Parse(TEXT("Current_Rating * 2"));
		Abilities.DefaultCosts.bPresent = true;
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EElysiumTraitContainer::Abilities))
		{
			if (Slot.Index != 0)
			{
				Abilities.Stats.Add(MakeStat(Slot.Index, Slot.Internal, 0, 5, 0));
			}
		}

		// Disciplines: `Min -1` / `Default -1`, which is what the row filter reads.
		Disciplines.DefaultCosts.New.Parse(TEXT("10"));
		Disciplines.DefaultCosts.Raise.Parse(TEXT("Current_Rating * 7"));
		Disciplines.DefaultCosts.bPresent = true;
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EElysiumTraitContainer::Disciplines))
		{
			Disciplines.Stats.Add(MakeStat(Slot.Index, Slot.Internal, -1, 5, -1));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumChargenTest, "Elysium.Substrate.Chargen", GElysiumTestFlags)
bool FElysiumChargenTest::RunTest(const FString&)
{
	using namespace ElysiumChargenTest;

	FElysiumStatTable Stats;
	BuildStats(Stats);

	FElysiumChargenRules Rules;
	Rules.Stats = &Stats;

	// --- which pool owns which slot --------------------------------------------------------------
	{
		const auto Pool = [](EElysiumTraitContainer C, int32 S)
		{
			return (int32)ElysiumChargen::PoolFor(C, S);
		};
		TestEqual(TEXT("Strength spends Physical"),
			Pool(EElysiumTraitContainer::Attributes, ElysiumSlot::Strength),
			(int32)EElysiumChargenPool::Physical);
		TestEqual(TEXT("Appearance spends Social"),
			Pool(EElysiumTraitContainer::Attributes, 6), (int32)EElysiumChargenPool::Social);
		TestEqual(TEXT("Wits spends Mental"),
			Pool(EElysiumTraitContainer::Attributes, 9), (int32)EElysiumChargenPool::Mental);
		TestEqual(TEXT("Subterfuge spends Talents"),
			Pool(EElysiumTraitContainer::Abilities, 4), (int32)EElysiumChargenPool::Talents);
		TestEqual(TEXT("Stealth spends Skills"),
			Pool(EElysiumTraitContainer::Abilities, 8), (int32)EElysiumChargenPool::Skills);
		TestEqual(TEXT("Academics spends Knowledges"),
			Pool(EElysiumTraitContainer::Abilities, 12), (int32)EElysiumChargenPool::Knowledges);
		TestEqual(TEXT("a discipline spends Disciplines"),
			Pool(EElysiumTraitContainer::Disciplines, 0), (int32)EElysiumChargenPool::Disciplines);

		// The order stat, the derived block and the whole parallel container are not sold.
		TestEqual(TEXT("Attrib_Order is not bought"),
			Pool(EElysiumTraitContainer::Attributes, ElysiumSlot::AttribOrder),
			(int32)EElysiumChargenPool::None);
		TestEqual(TEXT("Clan is not bought"),
			Pool(EElysiumTraitContainer::Attributes, ElysiumSlot::Clan),
			(int32)EElysiumChargenPool::None);
		TestEqual(TEXT("Experience is not bought"),
			Pool(EElysiumTraitContainer::Attributes, ElysiumSlot::Experience),
			(int32)EElysiumChargenPool::None);
		TestEqual(TEXT("Active_Disciplines is not bought"),
			Pool(EElysiumTraitContainer::ActiveDisciplines, 0), (int32)EElysiumChargenPool::None);
	}

	// --- the tier term: an ordering picks which category gets 2/1/0 and 3/2/1 -------------------
	{
		// Order 1 is `Physical_Mental_Social`, which is Brujah's: Physical primary (2), Mental
		// secondary (1), Social tertiary (0) — the reading the shipped screen shows as
		// `PHYSICAL(2)` / `SOCIAL` / `MENTAL(1)`, with the zero pool drawing no parenthetical.
		const FElysiumChargenPools P = ElysiumChargen::BuildPools(Rules, 2, 1, 0, true);
		TestEqual(TEXT("Physical_Mental_Social gives Physical 2"),
			P[EElysiumChargenPool::Physical], 2);
		TestEqual(TEXT("  Mental 1"), P[EElysiumChargenPool::Mental], 1);
		TestEqual(TEXT("  and Social 0"), P[EElysiumChargenPool::Social], 0);

		TestEqual(TEXT("Talents_Skills_Knowledges gives Talents 3"),
			P[EElysiumChargenPool::Talents], 3);
		TestEqual(TEXT("  Skills 2"), P[EElysiumChargenPool::Skills], 2);
		TestEqual(TEXT("  and Knowledges 1"), P[EElysiumChargenPool::Knowledges], 1);

		TestEqual(TEXT("attributes total 3 and abilities 6, whatever the ordering"),
			P[EElysiumChargenPool::Physical] + P[EElysiumChargenPool::Social]
			+ P[EElysiumChargenPool::Mental], 3);

		// Reordering moves the points; it never creates or destroys them.
		const FElysiumChargenPools Q = ElysiumChargen::BuildPools(Rules, 2, 4, 3, true);
		TestEqual(TEXT("Mental_Social_Physical gives Mental 2"), Q[EElysiumChargenPool::Mental], 2);
		TestEqual(TEXT("  and Physical 0"), Q[EElysiumChargenPool::Physical], 0);
		TestEqual(TEXT("Skills_Knowledges_Talents gives Skills 3"), Q[EElysiumChargenPool::Skills], 3);
		TestEqual(TEXT("  and Talents 1"), Q[EElysiumChargenPool::Talents], 1);
		TestEqual(TEXT("the total is the ordering's invariant"), Q.Total(), P.Total());

		// A kine character takes the flat 1/1/1 tables instead.
		const FElysiumChargenPools K = ElysiumChargen::BuildPools(Rules, 2, 1, 0, false);
		TestEqual(TEXT("a kine character gets 1 in every attribute pool"),
			K[EElysiumChargenPool::Physical] + K[EElysiumChargenPool::Social]
			+ K[EElysiumChargenPool::Mental], 3);
		TestEqual(TEXT("  and 1 Social rather than 0"), K[EElysiumChargenPool::Social], 1);
	}

	// --- the row filter: [0, 6) on the CURRENT value ---------------------------------------------
	{
		FElysiumSheet Sheet;
		Sheet.SeedFrom(Stats);
		TestFalse(TEXT("a discipline at the -1 default draws no row"),
			ElysiumChargen::IsRowVisible(Rules, Sheet, EElysiumTraitContainer::Disciplines, 0));

		Sheet.SetBase(EElysiumTraitContainer::Disciplines, 0, 1);
		Sheet.RecomputeCurrent(&Stats);
		TestTrue(TEXT("a clan discipline at 1 draws one"),
			ElysiumChargen::IsRowVisible(Rules, Sheet, EElysiumTraitContainer::Disciplines, 0));

		TestTrue(TEXT("an attribute at its default draws"),
			ElysiumChargen::IsRowVisible(Rules, Sheet, EElysiumTraitContainer::Attributes,
				ElysiumSlot::Strength));
		TestFalse(TEXT("the derived block never does"),
			ElysiumChargen::IsRowVisible(Rules, Sheet, EElysiumTraitContainer::Attributes,
				ElysiumSlot::MaxHealth));
	}

	// --- buying and selling ----------------------------------------------------------------------
	{
		FElysiumChargenState State;
		State.Clan = 2;
		State.Sheet.SeedFrom(Stats);
		State.Sheet.SetBase(EElysiumTraitContainer::Disciplines, 0, 1);
		State.Sheet.RecomputeCurrent(&Stats);
		State.Baseline = State.Sheet;
		State.Pools[EElysiumChargenPool::Physical] = 20;
		State.Pools[EElysiumChargenPool::Talents] = 20;
		State.Pools[EElysiumChargenPool::Disciplines] = 20;

		// A chargen point buys a dot outright — the `Costs` blocks price the level-up path, not this
		// one, and a 2-point pool against `Current_Rating * 4` could never buy anything.
		int32 Cost = 0;
		TestTrue(TEXT("Strength can be raised"), ElysiumChargen::CanBuy(State, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Cost));
		TestEqual(TEXT("  for one point"), Cost, 1);
		TestTrue(TEXT("  and it lands"), ElysiumChargen::Buy(State, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength));
		TestEqual(TEXT("  Strength is now 2"),
			State.Sheet.GetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Strength), 2);
		TestEqual(TEXT("  and one Physical point is spent"),
			State.Spent[EElysiumChargenPool::Physical], 1);
		TestEqual(TEXT("  leaving 19"), State.Remaining(EElysiumChargenPool::Physical), 19);

		// The point comes back — a sold dot restores the pool exactly.
		int32 Refund = 0;
		TestTrue(TEXT("the dot can be sold back"), ElysiumChargen::CanSell(State, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Refund));
		TestEqual(TEXT("  for what it cost"), Refund, Cost);
		TestTrue(TEXT("  and the sale lands"), ElysiumChargen::Sell(State, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength));
		TestEqual(TEXT("  restoring the pool exactly"),
			State.Remaining(EElysiumChargenPool::Physical), 20);

		// The baseline is the floor: a granted dot was never paid for.
		TestFalse(TEXT("a baseline dot cannot be sold"), ElysiumChargen::CanSell(State, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Refund));

		// An ability's first dot costs the same one point as its fifth — the `New`/`Raise` split is
		// the XP path's, and chargen never asks.
		TestTrue(TEXT("Brawl's first dot is buyable"), ElysiumChargen::CanBuy(State, Rules,
			EElysiumTraitContainer::Abilities, 1, Cost));
		TestEqual(TEXT("  for one point"), Cost, 1);
		TestTrue(TEXT("  it lands"), ElysiumChargen::Buy(State, Rules,
			EElysiumTraitContainer::Abilities, 1));
		TestTrue(TEXT("the second is buyable too"), ElysiumChargen::CanBuy(State, Rules,
			EElysiumTraitContainer::Abilities, 1, Cost));
		TestEqual(TEXT("  for the same one point"), Cost, 1);
		TestEqual(TEXT("  with one Talents point spent"),
			State.Spent[EElysiumChargenPool::Talents], 1);

		// Each dot is a point, so a pool spends down to the dot: 4 points is 4 dots, one at a time.
		FElysiumChargenState Four;
		Four.Sheet.SeedFrom(Stats);
		Four.Baseline = Four.Sheet;
		Four.Pools[EElysiumChargenPool::Physical] = 4;
		int32 Bought = 0;
		while (ElysiumChargen::Buy(Four, Rules, EElysiumTraitContainer::Attributes,
			ElysiumSlot::Dexterity))
		{
			++Bought;
		}
		// Dexterity starts at 1 and caps at 5, so the ceiling refuses before the pool empties.
		TestEqual(TEXT("Dexterity stops at its Max"),
			Four.Sheet.GetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Dexterity), 5);
		TestEqual(TEXT("  after four dots"), Bought, 4);
		TestEqual(TEXT("  which cost four points"), Four.Spent[EElysiumChargenPool::Physical], 4);
		TestTrue(TEXT("  spending the pool out exactly"), Four.IsSpentOut());
		TestFalse(TEXT("  and the sixth dot is refused"), ElysiumChargen::CanBuy(Four, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Dexterity, Cost));

		// An empty pool refuses too, and the refusal is the pool's, not the stat's.
		FElysiumChargenState Broke;
		Broke.Sheet.SeedFrom(Stats);
		Broke.Baseline = Broke.Sheet;
		TestFalse(TEXT("a spent-out pool refuses the next dot"), ElysiumChargen::CanBuy(Broke, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Cost));
		TestTrue(TEXT("  with the pool reporting nothing left"), Broke.IsSpentOut());

		// A hidden row is never buyable, whatever the pool holds.
		FElysiumChargenState Rich;
		Rich.Sheet.SeedFrom(Stats);
		Rich.Baseline = Rich.Sheet;
		Rich.Pools[EElysiumChargenPool::Disciplines] = 99;
		TestFalse(TEXT("a non-clan discipline cannot be bought at all"),
			ElysiumChargen::CanBuy(Rich, Rules, EElysiumTraitContainer::Disciplines, 0, Cost));

		// The derived block sits in no category, so no pool can reach it.
		Rich.Pools[EElysiumChargenPool::Physical] = 99;
		TestFalse(TEXT("a derived stat belongs to no pool"), ElysiumChargen::CanBuy(Rich, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, Cost));
	}

	// --- the same rows, spent against experience instead of pools ---------------------------------
	{
		// `BeginLevelUp` makes the live sheet both the working copy and the sell floor, so the only
		// dots that can be sold back are the ones bought in this session.
		FElysiumSheet Live;
		Live.SeedFrom(Stats);
		Live.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Experience, 20);
		Live.SetBase(EElysiumTraitContainer::Abilities, 1, 1);   // Brawl already at 1
		Live.RecomputeCurrent(&Stats);

		FElysiumChargenState XP;
		ElysiumChargen::BeginLevelUp(XP, Live, FElysiumSheetEffects());
		TestEqual(TEXT("the scratch takes the sheet's banked experience"), XP.Experience(), 20);

		// The XP path prices off the pre-purchase base, unlike chargen's flat point.
		int32 Cost = 0;
		TestTrue(TEXT("Strength is buyable"), ElysiumChargen::CanBuy(XP, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Cost));
		TestEqual(TEXT("  the 1->2 attribute dot costs 4"), Cost, 4);
		TestTrue(TEXT("  and it lands"), ElysiumChargen::Buy(XP, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength));
		TestEqual(TEXT("  debiting the experience slot"), XP.Experience(), 16);
		TestTrue(TEXT("the 2->3 dot costs more"), ElysiumChargen::CanBuy(XP, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Cost));
		TestEqual(TEXT("  namely 8"), Cost, 8);

		// Sell(r) refunds Buy(r-1) — the dot gives back exactly what it took.
		int32 Refund = 0;
		TestTrue(TEXT("it sells back"), ElysiumChargen::CanSell(XP, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Refund));
		TestEqual(TEXT("  for 4"), Refund, 4);
		TestTrue(TEXT("  and the sale lands"), ElysiumChargen::Sell(XP, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength));
		TestEqual(TEXT("  restoring the experience exactly"), XP.Experience(), 20);
		TestFalse(TEXT("  after which the pre-existing dot is not sellable"),
			ElysiumChargen::CanSell(XP, Rules, EElysiumTraitContainer::Attributes,
				ElysiumSlot::Strength, Refund));

		// An ability's first dot takes `New` on the XP path — the branch chargen never reaches.
		FElysiumChargenState Fresh;
		ElysiumChargen::BeginLevelUp(Fresh, Live, FElysiumSheetEffects());
		TestTrue(TEXT("Dodge at 0 prices as New"), ElysiumChargen::CanBuy(Fresh, Rules,
			EElysiumTraitContainer::Abilities, 2, Cost));
		TestEqual(TEXT("  a flat 3"), Cost, 3);
		TestTrue(TEXT("Brawl at 1 prices as Raise"), ElysiumChargen::CanBuy(Fresh, Rules,
			EElysiumTraitContainer::Abilities, 1, Cost));
		TestEqual(TEXT("  off the pre-purchase base"), Cost, 2);

		// Not enough banked is a refusal, and it is the only thing standing in the way.
		Fresh.SetExperience(1);
		TestFalse(TEXT("an unaffordable dot is refused"), ElysiumChargen::CanBuy(Fresh, Rules,
			EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, Cost));
		Fresh.SetExperience(4);
		TestTrue(TEXT("  and affordable again the moment the total covers it"),
			ElysiumChargen::CanBuy(Fresh, Rules, EElysiumTraitContainer::Attributes,
				ElysiumSlot::Strength, Cost));
	}

	// --- the string groups a symbolic value resolves through --------------------------------------
	{
		FElysiumStrings Strings;
		Strings.Groups.Add(TEXT("attributeorder"),
			{ TEXT("Physical_Social_Mental"), TEXT("Physical_Mental_Social") });
		TestEqual(TEXT("an ordering name resolves to its index"),
			Strings.IndexOf(TEXT("AttributeOrder"), TEXT("Physical_Mental_Social")), 1);
		TestEqual(TEXT("  case-insensitively"),
			Strings.IndexOf(TEXT("attributeorder"), TEXT("physical_social_mental")), 0);
		TestEqual(TEXT("  and an unknown name does not resolve"),
			Strings.IndexOf(TEXT("AttributeOrder"), TEXT("Nonsense")), (int32)INDEX_NONE);
		TestEqual(TEXT("the index reads back"),
			Strings.At(TEXT("AttributeOrder"), 1), FString(TEXT("Physical_Mental_Social")));
		TestEqual(TEXT("  and an absent group answers the default"),
			Strings.At(TEXT("NoSuchGroup"), 0, TEXT("fallback")), FString(TEXT("fallback")));
	}

	return true;
}

} // namespace ElysiumProgressionTests

#endif // WITH_DEV_AUTOMATION_TESTS
