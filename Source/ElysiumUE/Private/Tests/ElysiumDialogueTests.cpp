// Content-free Substrate automation: dialogue parsing, branching, automatic rows, starting lines, and CPython writers.
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
#include "Tests/ElysiumDialogueTestHelpers.h"
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
namespace ElysiumDialogueTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
}

namespace ElysiumDialogueTestHelpers
{
	// Build one 13-field `.dlg` row in the on-disk shape (`{ TAB content TAB }` concatenated) from the
	// fields the tests care about; cols 6-11 are empty. Handy so the fixtures read like the data.
	FString ElysiumDlgRow(int32 Id, const FString& Text, const FString& Link,
		const FString& Cond, const FString& Action, const FString& Malk)
	{
		auto F = [](const FString& S) { return FString::Printf(TEXT("{\t%s\t}"), *S); };
		FString R;
		R += F(FString::FromInt(Id)); // 0
		R += F(Text);                 // 1 male
		R += F(Text);                 // 2 female (same)
		R += F(Link);                 // 3
		R += F(Cond);                 // 4
		R += F(Action);               // 5
		for (int32 i = 6; i <= 11; ++i) { R += F(FString()); }
		R += F(Malk);                 // 12 — Malkavian-PC variant
		return R;
	}

	TArray<uint8> ElysiumDlgBytes(const TArray<FString>& Rows)
	{
		FString Joined = FString::Join(Rows, TEXT("\r\n")) + TEXT("\r\n");
		TArray<uint8> Bytes;
		Bytes.Reserve(Joined.Len());
		for (const TCHAR C : Joined) { Bytes.Add(static_cast<uint8>(C)); }   // Latin-1 round-trip
		return Bytes;
	}
}

namespace ElysiumDialogueTests
{
using ElysiumDialogueTestHelpers::ElysiumDlgBytes;
using ElysiumDialogueTestHelpers::ElysiumDlgRow;

// =====================================================================================
// 9.1 / B4 — `.dlg` parser, the dlgexpr normalizer, and the branch state machine. All
// content-free: a synthetic in-memory `.dlg` and injected condition/action callbacks, so
// the branch logic is tested independently of both the normalizer and the script host.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgParseTest, "Elysium.Substrate.DlgParse", GElysiumTestFlags)
bool FElysiumDlgParseTest::RunTest(const FString&)
{
	TArray<FString> Rows;
	Rows.Add(ElysiumDlgRow(11, TEXT("Greeting."), TEXT("#"), TEXT("G.Story_State = -3"), TEXT("")));
	Rows.Add(ElysiumDlgRow(12, TEXT("Who are you?"), TEXT("21"), TEXT("not IsClan(pc,\"Malkavian\")"), TEXT(""), TEXT("The rain of ages?")));
	Rows.Add(ElysiumDlgRow(13, TEXT("Padding"), TEXT(""), TEXT(""), TEXT("")));   // empty link = padding

	FElysiumDlgFile File;
	TestTrue(TEXT("parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), File));
	TestEqual(TEXT("row count"), File.Lines.Num(), 3);

	const FElysiumDlgLine* Npc = File.FindById(11);
	if (TestNotNull(TEXT("finds id 11"), Npc))
	{
		TestTrue(TEXT("11 is an NPC line"), Npc->IsNpcLine());
		TestEqual(TEXT("11 text"), Npc->Text(true), FString(TEXT("Greeting.")));
		TestEqual(TEXT("11 col-4 kept raw"), Npc->Condition, FString(TEXT("G.Story_State = -3")));
	}
	const FElysiumDlgLine* Pc = File.FindById(12);
	if (TestNotNull(TEXT("finds id 12"), Pc))
	{
		TestTrue(TEXT("12 is a PC choice"), Pc->IsPcChoice());
		TestEqual(TEXT("12 link target"), Pc->LinkTarget(), 21);
		// col-12 is the Malkavian variant: a non-Malkavian sees col-1, a Malkavian sees col-12.
		TestEqual(TEXT("12 normal text (non-malk)"), Pc->RawFor(true, false), FString(TEXT("Who are you?")));
		TestEqual(TEXT("12 malkavian variant"), Pc->RawFor(true, true), FString(TEXT("The rain of ages?")));
	}
	TestTrue(TEXT("13 is padding"), File.FindById(13)->Role == EElysiumDlgRole::Padding);

	// 14-field tolerance (the kiki.dlg typo): a valid 13-field prefix parses, the extra is ignored.
	FString FourteenField = ElysiumDlgRow(1, TEXT("hi"), TEXT("#"), TEXT(""), TEXT("")) + TEXT("{\textra\t}");
	FElysiumDlgFile Wide;
	TestTrue(TEXT("14-field row parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes({ FourteenField }), Wide));
	TestEqual(TEXT("14-field row yields one line"), Wide.Lines.Num(), 1);
	TestEqual(TEXT("14-field text intact"), Wide.Lines[0].Text(true), FString(TEXT("hi")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgExprTest, "Elysium.Substrate.DlgExpr", GElysiumTestFlags)
bool FElysiumDlgExprTest::RunTest(const FString&)
{
	using namespace ElysiumDlgExpr;

	// A skill-only condition: implicit `>=`, wrapped in CalcFeat. The receiver is explicit, because
	// `CalcFeat` is a Character method — a bare call resolves to nothing in either host.
	TestEqual(TEXT("bare skillcheck"), ConditionToPython(TEXT("Seduction 7")),
		FString(TEXT("pc.CalcFeat(\"Seduction\") >= 7")));
	// Explicit relop preserved.
	TestEqual(TEXT("skillcheck relop"), ConditionToPython(TEXT("Humanity >= 5")),
		FString(TEXT("pc.CalcFeat(\"Humanity\") >= 5")));
	// Skillcheck joined to a python expr by `&` -> `and`.
	TestEqual(TEXT("skillcheck & expr"), ConditionToPython(TEXT("Seduction 7 & G.Johnny_Dead == 0")),
		FString(TEXT("pc.CalcFeat(\"Seduction\") >= 7 and G.Johnny_Dead == 0")));
	// `|` -> `or`.
	TestEqual(TEXT("pipe -> or"), ConditionToPython(TEXT("Persuasion 7 | G.x == 1")),
		FString(TEXT("pc.CalcFeat(\"Persuasion\") >= 7 or G.x == 1")));
	// The `M_`/`F_` prefix is the engine dependency's SEX GATE, not part of the trait name — the
	// corpus authors the same beat twice, once per sex, with different lines (`cal.dlg`).
	TestEqual(TEXT("male-gated skillcheck"), ConditionToPython(TEXT("M_Persuasion 3")),
		FString(TEXT("(pc.IsMale() and pc.CalcFeat(\"Persuasion\") >= 3)")));
	TestEqual(TEXT("female-gated skillcheck"), ConditionToPython(TEXT("F_Seduction 8 & G.x == 0")),
		FString(TEXT("(not pc.IsMale() and pc.CalcFeat(\"Seduction\") >= 8) and G.x == 0")));
	// A trait that merely STARTS with a letter and an underscore is not gated.
	TestEqual(TEXT("an ordinary underscored trait is untouched"),
		ConditionToPython(TEXT("Max_Health 5")),
		FString(TEXT("pc.CalcFeat(\"Max_Health\") >= 5")));
	// A pure python condition (no skillcheck) round-trips (normalised spacing).
	TestEqual(TEXT("pure python"), ConditionToPython(TEXT("G.Patch_Plus == 0")),
		FString(TEXT("G.Patch_Plus == 0")));
	// A member/call ident that happens to precede a number is NOT a skillcheck.
	TestEqual(TEXT("member not skillcheck"), ConditionToPython(TEXT("pc.humanity >= 5")),
		FString(TEXT("pc.humanity >= 5")));
	TestEqual(TEXT("call not skillcheck"), ConditionToPython(TEXT("OneOfSet(1,4)")),
		FString(TEXT("OneOfSet(1,4)")));
	// Empty -> empty.
	TestEqual(TEXT("empty condition"), ConditionToPython(TEXT("")), FString());

	// Actions: `&` between statements -> `;`; a lone assignment round-trips.
	TestEqual(TEXT("action assign"), ActionToPython(TEXT("G.Tut_Jack = 1")),
		FString(TEXT("G.Tut_Jack = 1")));
	TestEqual(TEXT("action &-join -> ;"), ActionToPython(TEXT("G.a = 1 & G.b = 2")),
		FString(TEXT("G.a = 1 ; G.b = 2")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgDisplayTest, "Elysium.Substrate.DlgDisplay", GElysiumTestFlags)
bool FElysiumDlgDisplayTest::RunTest(const FString&)
{
	using ElysiumDlgText::StripStageDirections;

	// The real jack_tutorial line 11 opener: a leading `[...]` and an interior one, no space after either.
	TestEqual(TEXT("jack opener stripped"),
		StripStageDirections(TEXT("[laughing at something no one else thinks is funny]What a scene, man! [chuckle]How 'bout that?")),
		FString(TEXT("What a scene, man! How 'bout that?")));
	// A direction between words leaves a single space, not a doubled one.
	TestEqual(TEXT("interior collapse"), StripStageDirections(TEXT("a [nods] b")), FString(TEXT("a b")));
	// No brackets -> verbatim (fast path).
	TestEqual(TEXT("no directions"), StripStageDirections(TEXT("Who are you?")), FString(TEXT("Who are you?")));
	// An unterminated `[` is kept (not a stage direction).
	TestEqual(TEXT("unterminated bracket kept"), StripStageDirections(TEXT("cost is [50")), FString(TEXT("cost is [50")));
	// A direction-only string strips to empty.
	TestEqual(TEXT("direction-only -> empty"), StripStageDirections(TEXT("[sighs]")), FString());

	// The line accessor path: raw Text() keeps the direction, DisplayText() drops it. col-12 (Malkavian)
	// replaces the text for a Malkavian player, and is stage-direction-stripped the same way.
	FElysiumDlgLine Line;
	Line.TextMale = TEXT("[chuckle]Hey there.");
	Line.TextMalkavian = TEXT("[cackles]The walls whisper hello.");
	Line.Role = EElysiumDlgRole::NpcLine;
	TestEqual(TEXT("raw text verbatim"), Line.Text(true), FString(TEXT("[chuckle]Hey there.")));
	TestEqual(TEXT("display non-malk stripped"), Line.DisplayText(true, false), FString(TEXT("Hey there.")));
	TestEqual(TEXT("display malk variant stripped"), Line.DisplayText(true, true), FString(TEXT("The walls whisper hello.")));
	// A line with no col-12 falls back to the gendered text even for a Malkavian.
	FElysiumDlgLine Plain;
	Plain.TextMale = TEXT("Plain.");
	TestEqual(TEXT("no malk variant -> col-1"), Plain.DisplayText(true, true), FString(TEXT("Plain.")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgBranchTest, "Elysium.Substrate.DlgBranch", GElysiumTestFlags)
bool FElysiumDlgBranchTest::RunTest(const FString&)
{
	// A miniature of the jack_tutorial shape: a starting sentinel selects the real entry (11),
	// a gated + an ungated choice, a follow NPC line (21) with a choice that sets a flag and ends.
	TArray<FString> Rows;
	Rows.Add(ElysiumDlgRow(100, TEXT("(Starting Condition)"), TEXT("11"), TEXT("START"), TEXT("")));
	Rows.Add(ElysiumDlgRow(1, TEXT(""), TEXT("#"), TEXT(""), TEXT("")));
	Rows.Add(ElysiumDlgRow(11, TEXT("Greeting."), TEXT("#"), TEXT("SPEAK_11"), TEXT("")));
	Rows.Add(ElysiumDlgRow(12, TEXT("Gated"), TEXT("21"), TEXT("SHOW"), TEXT("PICK_12")));
	Rows.Add(ElysiumDlgRow(13, TEXT("Hidden"), TEXT("31"), TEXT("HIDE"), TEXT("")));
	Rows.Add(ElysiumDlgRow(14, TEXT("Always"), TEXT("21"), TEXT(""), TEXT("")));    // ungated
	Rows.Add(ElysiumDlgRow(21, TEXT("More."), TEXT("#"), TEXT(""), TEXT("")));
	Rows.Add(ElysiumDlgRow(22, TEXT("Set flag & bye"), TEXT("0"), TEXT(""), TEXT("SET_FLAG")));

	TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
	if (!TestTrue(TEXT("fixture parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), File.Get())))
	{
		return false;
	}

	// Record actions; a condition passes unless it is the string "HIDE".
	TArray<FString> Ran;
	auto Cond = [](const FString& C) { return C != TEXT("HIDE"); };
	auto Act = [&Ran](const FString& A) { Ran.Add(A); };

	FElysiumDlgConversation Conv(File, /*bMale*/ true, /*bMalk*/ false, Cond, Act);
	Conv.Start();

	// The sentinel selects line 11 rather than the physical first NPC line, then its col-4 action runs.
	if (TestNotNull(TEXT("opened on an NPC line"), Conv.CurrentNpcLine()))
	{
		TestEqual(TEXT("entry is line 11"), Conv.CurrentNpcLine()->Id, 11);
	}
	TestTrue(TEXT("ran SPEAK_11"), Ran.Contains(TEXT("SPEAK_11")));

	// Two of the three following rows are visible (gated SHOW passes, HIDE fails, ungated shows).
	TestEqual(TEXT("visible choice count"), Conv.VisibleChoices().Num(), 2);
	TestEqual(TEXT("first visible is 12"), Conv.VisibleChoice(0)->Id, 12);
	TestEqual(TEXT("second visible is 14"), Conv.VisibleChoice(1)->Id, 14);

	// Pick the first choice -> its action runs, jump to NPC 21.
	Conv.Choose(0);
	TestTrue(TEXT("ran PICK_12"), Ran.Contains(TEXT("PICK_12")));
	if (TestNotNull(TEXT("advanced to a line"), Conv.CurrentNpcLine()))
	{
		TestEqual(TEXT("now on line 21"), Conv.CurrentNpcLine()->Id, 21);
	}

	// Line 21 offers one ending choice; picking it runs the flag action then ends the conversation.
	TestEqual(TEXT("21 has one choice"), Conv.VisibleChoices().Num(), 1);
	Conv.Choose(0);
	TestTrue(TEXT("ran SET_FLAG"), Ran.Contains(TEXT("SET_FLAG")));
	TestTrue(TEXT("conversation is over"), Conv.IsOver());
	TestNull(TEXT("no current line after end"), Conv.CurrentNpcLine());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgAutomaticTest,
	"Elysium.Substrate.DlgAutomatic", GElysiumTestFlags)
bool FElysiumDlgAutomaticTest::RunTest(const FString&)
{
	// The exact authored shape behind Jack's tutorial transition: the first NPC line stays current,
	// a synthetic PC-role row silently follows to the next NPC line, and an Auto-End closes only
	// after that second spoken line. A preceding ordinary response proves the automatic row owns the
	// whole response band rather than merely hiding itself.
	TArray<FString> Rows;
	Rows.Add(ElysiumDlgRow(1, TEXT("[like the Fonz]Alright."), TEXT("#"), TEXT(""), TEXT("")));
	Rows.Add(ElysiumDlgRow(2, TEXT("Must not be shown"), TEXT("10"), TEXT(""), TEXT("")));
	Rows.Add(ElysiumDlgRow(3, TEXT("  (auto-link)  "), TEXT("10"), TEXT(""), TEXT("AUTO_LINK")));
	Rows.Add(ElysiumDlgRow(10, TEXT("Uhh... why don't we, uh, step out back here."), TEXT("#"), TEXT(""), TEXT("")));
	Rows.Add(ElysiumDlgRow(11, TEXT("(Auto-End)"), TEXT("0"), TEXT(""), TEXT("AUTO_END")));

	TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
	File->SourcePath = TEXT("dlg/main characters/automatic_test.dlg");
	if (!TestTrue(TEXT("automatic fixture parses"),
		FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), File.Get())))
	{
		return false;
	}
	File->SourcePath = TEXT("dlg/main characters/automatic_test.dlg");

	FElysiumDlgLine Ordinary;
	Ordinary.Role = EElysiumDlgRole::PcChoice;
	Ordinary.TextMale = TEXT("I mention (Auto-Link), but I am dialogue.");
	TestFalse(TEXT("classification is exact rather than substring-based"), Ordinary.IsAutomatic());

	TArray<FString> Ran;
	TSharedRef<FElysiumDlgConversation> Conv = MakeShared<FElysiumDlgConversation>(
		File, /*bMale*/ true, /*bMalk*/ false,
		[](const FString&) { return true; },
		[&Ran](const FString& Action) { Ran.Add(Action); });
	Conv->Start();
	if (TestNotNull(TEXT("first spoken line remains active"), Conv->CurrentNpcLine()))
	{
		TestEqual(TEXT("the presented line is Alright"),
			Conv->CurrentNpcLine()->DisplayText(true, false), FString(TEXT("Alright.")));
	}
	TestTrue(TEXT("the Auto-Link is pending"), Conv->IsAwaitingAutomatic());
	TestFalse(TEXT("an automatic turn is not terminal"), Conv->IsTerminalLine());
	TestEqual(TEXT("no synthetic or ordinary response is visible"), Conv->VisibleChoices().Num(), 0);
	if (TestNotNull(TEXT("the pending control row is inspectable"), Conv->PendingAutomatic()))
	{
		TestEqual(TEXT("the first passing automatic row wins"), Conv->PendingAutomatic()->Id, 3);
	}
	const uint32 WaitingRevision = Conv->Revision();
	Conv->Choose(0);
	TestEqual(TEXT("choice input cannot skip an automatic wait"), Conv->Revision(), WaitingRevision);
	TestTrue(TEXT("the automatic action has not run before completion"), Ran.IsEmpty());

	Conv->ResolveAutomatic();
	TestTrue(TEXT("Auto-Link action runs at resolution"), Ran.Contains(TEXT("AUTO_LINK")));
	if (TestNotNull(TEXT("Auto-Link reaches the next spoken line"), Conv->CurrentNpcLine()))
	{
		TestEqual(TEXT("the follow-up line is current"), Conv->CurrentNpcLine()->Id, 10);
	}
	TestTrue(TEXT("the follow-up Auto-End is independently pending"), Conv->IsAwaitingAutomatic());
	Conv->ResolveAutomatic();
	TestTrue(TEXT("Auto-End action runs at resolution"), Ran.Contains(TEXT("AUTO_END")));
	TestTrue(TEXT("Auto-End closes the conversation"), Conv->IsOver());

	// The world owns the timing join. Completing voice 1 advances exactly one automatic edge and
	// submits voice 2; it cannot cascade through Auto-End until that new handle also completes.
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__dlg_automatic_test__");
	FElysiumEntityDef Jack;
	Jack.Classname = TEXT("npc_VVampire");
	Jack.TargetName = TEXT("Jack");
	Jack.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
	Defs.Defs.Add(MoveTemp(Jack));
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0); // admit the NPC mind/body before dialogue acquires it
	FElysiumEntity* JackEntity = World.FindByName(TEXT("Jack"));
	if (!TestNotNull(TEXT("world fixture has Jack"), JackEntity))
	{
		return false;
	}

	Ran.Reset();
	TSharedRef<FElysiumDlgConversation> Timed = MakeShared<FElysiumDlgConversation>(
		File, true, false, [](const FString&) { return true; },
		[&Ran](const FString& Action) { Ran.Add(Action); });
	Timed->Start();
	World.OpenDialog(JackEntity->Handle, Timed);
	TestEqual(TEXT("opening Alright submits one voice"), Services.NumLiveVoices(), 1);
	if (!TestNotNull(TEXT("the timed conversation opens"), World.GetOpenDialog()))
	{
		return false;
	}
	World.Tick(0.1);
	TestEqual(TEXT("a live Alright voice keeps its line displayed"),
		World.GetOpenDialog()->CurrentNpcLine()->Id, 1);

	Services.CompleteAllVoices();
	World.Tick(0.2);
	if (TestNotNull(TEXT("voice completion keeps the conversation open"), World.GetOpenDialog()))
	{
		TestEqual(TEXT("voice 1 completion advances to the follow-up line"),
			World.GetOpenDialog()->CurrentNpcLine()->Id, 10);
	}
	TestEqual(TEXT("the follow-up line owns a new live voice"), Services.NumLiveVoices(), 1);
	World.Tick(0.3);
	TestNotNull(TEXT("the live follow-up voice prevents same-frame Auto-End"), World.GetOpenDialog());

	Services.CompleteAllVoices();
	World.Tick(0.4);
	TestNull(TEXT("Auto-End resolves only after the follow-up voice completes"), World.GetOpenDialog());
	TestTrue(TEXT("both automatic row actions ran once"),
		Ran.Num() == 2 && Ran[0] == TEXT("AUTO_LINK") && Ran[1] == TEXT("AUTO_END"));

	// An automatic row action is allowed to run arbitrary script and can replace the conversation
	// synchronously. The completion which invoked it must not then advance or close that new session.
	TSharedRef<FElysiumDlgFile> ReplacingFile = MakeShared<FElysiumDlgFile>();
	TestTrue(TEXT("re-entrant source fixture parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes({
		ElysiumDlgRow(101, TEXT("Old turn."), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(102, TEXT("(Auto-Link)"), TEXT("103"), TEXT(""), TEXT("REPLACE")),
		ElysiumDlgRow(103, TEXT("Old follow-up."), TEXT("#"), TEXT(""), TEXT("")),
	}), ReplacingFile.Get()));
	ReplacingFile->SourcePath = TEXT("dlg/replacing.dlg");
	TSharedRef<FElysiumDlgFile> ReplacementFile = MakeShared<FElysiumDlgFile>();
	TestTrue(TEXT("re-entrant replacement fixture parses"),
		FElysiumDlgFile::ParseBytes(ElysiumDlgBytes({
			ElysiumDlgRow(201, TEXT("Replacement remains."), TEXT("#"), TEXT(""), TEXT("")),
		}), ReplacementFile.Get()));
	ReplacementFile->SourcePath = TEXT("dlg/replacement.dlg");
	TSharedRef<FElysiumDlgConversation> Replacement = MakeShared<FElysiumDlgConversation>(
		ReplacementFile, true, false, [](const FString&) { return true; }, [](const FString&) {});
	Replacement->Start();
	TSharedRef<FElysiumDlgConversation> Replacing = MakeShared<FElysiumDlgConversation>(
		ReplacingFile, true, false, [](const FString&) { return true; },
		[&World, JackEntity, Replacement](const FString& Action)
		{
			if (Action == TEXT("REPLACE"))
			{
				World.OpenDialog(JackEntity->Handle, Replacement);
			}
		});
	Replacing->Start();
	World.OpenDialog(JackEntity->Handle, Replacing);
	Services.CompleteAllVoices();
	World.Tick(0.5);
	TestTrue(TEXT("the old completion cannot reclaim a synchronously replaced session"),
		World.GetOpenDialog() == &Replacement.Get());
	if (TestNotNull(TEXT("the replacement session remains presented"), World.GetOpenDialog()))
	{
		TestEqual(TEXT("the replacement line remains current"),
			World.GetOpenDialog()->CurrentNpcLine()->Id, 201);
	}
	World.CloseDialog(/*bSilent*/ true);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueBodySceneTest,
	"Elysium.Substrate.Dialogue.BodyScene", GElysiumTestFlags)
bool FElysiumDialogueBodySceneTest::RunTest(const FString&)
{
	ElysiumScene::ClearCache();
	ON_SCOPE_EXIT { ElysiumScene::ClearCache(); };

	TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
	if (!TestTrue(TEXT("Jack dialogue fixture parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes({
		ElysiumDlgRow(11, TEXT("Opening line."), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(12, TEXT("Continue."), TEXT("21"), TEXT(""), TEXT("")),
		ElysiumDlgRow(21, TEXT("Follow-up line."), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(22, TEXT("Done."), TEXT("0"), TEXT(""), TEXT("")),
	}), File.Get())))
	{
		return false;
	}
	File->SourcePath = TEXT("dlg/main characters/jack_tutorial.dlg");

	auto RegisterLineScene = [&File](int32 LineId, const FString& Clip)
	{
		const FString Key = FPaths::SetExtension(
			FElysiumLineService::DialogueLineSource(File->SourcePath, LineId), TEXT("vcd"));
		const FString Text = FString::Printf(
			TEXT("actor \"Jack\"\n{\n")
			TEXT(" channel \"Speech\"\n {\n  event speak \"NPC Line\"\n  {\n")
			TEXT("   time 0.0 1.5\n   param \"character/dlg/test/line%d_col_e.wav\"\n")
			TEXT("  }\n }\n channel \"Gestures\"\n {\n  event gesture \"body\"\n  {\n")
			TEXT("   time 0.0 2.0\n   param \"%s\"\n  }\n }\n}\n"),
			LineId, *Clip);
		ElysiumScene::RegisterInline(Key, Text);
	};
	RegisterLineScene(11, TEXT("Smiling_Jack_line11_col_E"));
	RegisterLineScene(21, TEXT("Smiling_Jack_line21_col_E"));

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.ClipSeconds = 2.533333f; // Jack's authored shared-male waveover01 duration
	Services.StanceClips.Idle[0] = TEXT("Stance_Neutral_Idle_1");
	Services.StanceClips.Idle[1] = TEXT("Stance_Neutral_Idle_2");
	Services.StanceClips.Idle[2] = TEXT("Stance_Neutral_Idle_3");
	ElysiumStance::ApplyPrecacheFallbacks(Services.StanceClips);
	FElysiumDisposition Neutral;
	Neutral.Name = TEXT("Neutral");
	Neutral.AnimName = TEXT("Neutral");
	FElysiumDisposition Joy;
	Joy.Name = TEXT("Joy");
	Joy.AnimName = TEXT("Joy");
	Services.DispositionRows.Add(TEXT("neutral|1"), Neutral);
	Services.DispositionRows.Add(TEXT("joy|1"), Joy);
	// This model authors the Neutral<->Joy cross-disposition transition; HasNpcClip gates
	// SetDisposition's PlayNpcClip attempt on it (B4).
	Services.KnownNpcClips.Add(TEXT("smiling_jack"), { TEXT("Stance_Trans_Neutral_1_Joy_1") });

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__dialogue_body_scene_test__");
	FElysiumEntityDef Jack;
	Jack.Classname = TEXT("npc_VVampire");
	Jack.TargetName = TEXT("Jack");
	Jack.Keys.Add(TEXT("model"),
		TEXT("models/character/npc/unique/smiling_jack/smiling_jack.mdl"));
	// Jack starts Neutral so the disposition assertion below exercises a real Neutral -> Joy
	// transition: with no key here `Disposition` is empty, its row does not resolve, and the
	// transition bails on an empty old stance before it names a clip.
	Jack.Keys.Add(TEXT("default_disposition"), TEXT("Neutral"));
	Defs.Defs.Add(MoveTemp(Jack));
	FElysiumEntityDef Waveover;
	Waveover.Classname = TEXT("scripted_sequence");
	Waveover.TargetName = TEXT("sJack_waveover");
	Waveover.Keys.Add(TEXT("m_iszEntity"), TEXT("Jack"));
	Waveover.Keys.Add(TEXT("m_iszPlay"), TEXT("waveover01"));
	Waveover.Keys.Add(TEXT("m_fMoveTo"), TEXT("0"));
	Defs.Defs.Add(MoveTemp(Waveover));
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0); // admit the NPC mind before dialogue acquires the body
	FElysiumEntity* JackEntity = World.FindByName(TEXT("Jack"));
	FElysiumEntity* WaveoverEntity = World.FindByName(TEXT("sJack_waveover"));
	if (!TestNotNull(TEXT("world fixture has Jack"), JackEntity)
		|| !TestNotNull(TEXT("world fixture has Jack's waveover beat"), WaveoverEntity))
	{
		return false;
	}
	World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), WaveoverEntity->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("beat two starts Jack's authored waveover"),
		Services.Count(TEXT("PlayNpcClip smiling_jack waveover01 loop=0")), 1);
	TestTrue(TEXT("the waveover beat claims Jack until dialogue interrupts it"),
		JackEntity->ScriptOwner == WaveoverEntity->Handle);

	TSharedRef<FElysiumDlgConversation> Conversation = MakeShared<FElysiumDlgConversation>(
		File, true, false, [](const FString&) { return true; }, [](const FString&) {});
	Conversation->Start();
	Services.Calls.Reset();
	World.OpenDialog(JackEntity->Handle, Conversation);
	TestFalse(TEXT("dialogue cancels the older waveover body claim"), JackEntity->ScriptOwner.IsSet());
	TestTrue(TEXT("the cancelled waveover has no delayed action deadline"),
		WaveoverEntity->SaveBlockReason() == nullptr);
	TestTrue(TEXT("line 11 owns Jack's body immediately"),
		World.HasActiveDialogueBodyClip(JackEntity->Handle));
	TestEqual(TEXT("line 11 submits its authored body clip exactly once"),
		Services.Count(TEXT("PlayNpcClip smiling_jack Smiling_Jack_line11_col_E loop=0")), 1);
	const int32 IdleRefreshesAfterHandoff = Services.Count(TEXT("RefreshNpcIdle smiling_jack"));
	World.Tick(3.0); // beyond waveover01's old deadline, while the dialogue gesture remains live
	TestEqual(TEXT("the cancelled beat cannot reset the newer dialogue clip at its old deadline"),
		Services.Count(TEXT("RefreshNpcIdle smiling_jack")), IdleRefreshesAfterHandoff);
	TestTrue(TEXT("the dialogue body scene survives the old waveover deadline"),
		World.HasActiveDialogueBodyClip(JackEntity->Handle));

	World.Tick(3.1);
	TestEqual(TEXT("the dialogue stance think cannot overwrite a live line clip"),
		Services.Count(TEXT("PlayNpcClip smiling_jack Stance_Neutral_Idle_1")), 0);
	TestTrue(TEXT("the instanced scene seeks the body on its own clock"),
		Services.Saw(TEXT("SeekCinematicClip 0.100")));

	World.PlayerDialogChoose(0);
	TestEqual(TEXT("advancing replaces line 11 with line 21's body clip"),
		Services.Count(TEXT("PlayNpcClip smiling_jack Smiling_Jack_line21_col_E loop=0")), 1);
	TestTrue(TEXT("line replacement releases the outgoing pose to the stance path"),
		Services.Saw(TEXT("RefreshNpcIdle smiling_jack")));

	World.CloseDialog(/*bSilent=*/true);
	TestFalse(TEXT("dialogue close releases the active body scene"),
		World.HasActiveDialogueBodyClip(JackEntity->Handle));
	const int32 SeeksAtClose = Services.Count(TEXT("SeekCinematicClip"));
	World.Tick(3.2);
	TestEqual(TEXT("a closed line scene receives no stale body tick"),
		Services.Count(TEXT("SeekCinematicClip")), SeeksAtClose);

	// The dialogue owner is intentionally accepted by SetDisposition. IsFeedBusy() also includes
	// bInDialog for feed refusal, so the transition gate must ask the combat-character feed state
	// directly rather than rejecting every open conversation.
	TSharedRef<FElysiumDlgFile> DispositionFile = MakeShared<FElysiumDlgFile>();
	TestTrue(TEXT("disposition dialogue fixture parses"), FElysiumDlgFile::ParseBytes(ElysiumDlgBytes({
		ElysiumDlgRow(101, TEXT("No body event."), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(102, TEXT("Done."), TEXT("0"), TEXT(""), TEXT("")),
	}), DispositionFile.Get()));
	DispositionFile->SourcePath = TEXT("dlg/main characters/disposition_dialog.dlg");
	const FString DispositionSceneKey = FPaths::SetExtension(
		FElysiumLineService::DialogueLineSource(DispositionFile->SourcePath, 101), TEXT("vcd"));
	ElysiumScene::RegisterInline(DispositionSceneKey,
		TEXT("actor \"Jack\"\n{\n channel \"Speech\"\n {\n  event speak \"line\"\n  {\n")
		TEXT("   time 0.0 1.0\n   param \"character/dlg/test/line101_col_e.wav\"\n  }\n }\n}\n"));
	TSharedRef<FElysiumDlgConversation> DispositionConversation = MakeShared<FElysiumDlgConversation>(
		DispositionFile, true, false, [](const FString&) { return true; }, [](const FString&) {});
	DispositionConversation->Start();
	World.OpenDialog(JackEntity->Handle, DispositionConversation);
	Services.Calls.Reset();
	TestTrue(TEXT("SetDisposition succeeds while dialogue owns the body"),
		static_cast<FElysiumAnimating*>(JackEntity)->SetDisposition(TEXT("Joy"), 1));
	TestEqual(TEXT("dialogue no longer makes the authored disposition transition unreachable"),
		Services.Count(TEXT("PlayNpcClip smiling_jack Stance_Trans_Neutral_1_Joy_1 loop=0")), 1);
	World.CloseDialog(/*bSilent=*/true);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgStartingLineTest,
	"Elysium.Substrate.DlgStartingLine", GElysiumTestFlags)
bool FElysiumDlgStartingLineTest::RunTest(const FString&)
{
	// The retail classifier is a case-insensitive substring test over raw col-1 and accepts all three
	// spellings. These rows are PC-role records because their col-3 is the target NPC line.
	for (const TCHAR* Text : { TEXT("(Starting Condition)"), TEXT("prefix STARTING-CONDITION suffix"),
		TEXT("starting_condition") })
	{
		FElysiumDlgLine Line;
		Line.TextMale = Text;
		TestTrue(FString::Printf(TEXT("'%s' classifies as a starting sentinel"), Text),
			Line.IsStartingCondition());
	}
	FElysiumDlgLine Ordinary;
	Ordinary.TextMale = TEXT("A normal response");
	TestFalse(TEXT("ordinary dialogue is not a sentinel"), Ordinary.IsStartingCondition());

	// A passing dangling link does not stop the scan; the first later passing valid link wins, and a
	// still-later valid row cannot override it. The selected NPC action proves EnterNpcLine is reused.
	TArray<FString> Rows;
	Rows.Add(ElysiumDlgRow(100, TEXT("(Starting Condition)"), TEXT("999"), TEXT("BAD_LINK"), TEXT("")));
	Rows.Add(ElysiumDlgRow(101, TEXT("(starting-condition)"), TEXT("30"), TEXT("FALSE"), TEXT("")));
	Rows.Add(ElysiumDlgRow(102, TEXT("(STARTING_CONDITION)"), TEXT("20"), TEXT("FIRST_TRUE"), TEXT("")));
	Rows.Add(ElysiumDlgRow(103, TEXT("(Starting Condition)"), TEXT("30"), TEXT("LATER_TRUE"), TEXT("")));
	Rows.Add(ElysiumDlgRow(1, TEXT("Fallback."), TEXT("#"), TEXT(""), TEXT("")));
	Rows.Add(ElysiumDlgRow(20, TEXT("Selected."), TEXT("#"), TEXT("ENTER_20"), TEXT("")));
	Rows.Add(ElysiumDlgRow(30, TEXT("Too late."), TEXT("#"), TEXT("ENTER_30"), TEXT("")));

	TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
	if (!TestTrue(TEXT("selector fixture parses"),
		FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), File.Get())))
	{
		return false;
	}
	TArray<FString> Ran;
	FElysiumDlgConversation Ordered(File, true, false,
		[](const FString& Condition)
		{
			return Condition == TEXT("BAD_LINK") || Condition == TEXT("FIRST_TRUE")
				|| Condition == TEXT("LATER_TRUE");
		},
		[&Ran](const FString& Action) { Ran.Add(Action); });
	Ordered.Start();
	if (TestNotNull(TEXT("ordered selector opens"), Ordered.CurrentNpcLine()))
	{
		TestEqual(TEXT("first passing valid link wins"), Ordered.CurrentNpcLine()->Id, 20);
	}
	TestTrue(TEXT("selected line enters through the ordinary action path"), Ran.Contains(TEXT("ENTER_20")));
	TestFalse(TEXT("later passing row is not entered"), Ran.Contains(TEXT("ENTER_30")));

	// A usescript integer overrides line 1 when no sentinel passes; no usescript means line 1.
	FElysiumDlgConversation ScriptFallback(File, true, false,
		[](const FString&) { return false; }, [](const FString&) {},
		[]() -> TOptional<int32> { return 30; });
	ScriptFallback.Start();
	if (TestNotNull(TEXT("usescript fallback opens"), ScriptFallback.CurrentNpcLine()))
	{
		TestEqual(TEXT("usescript integer is the starting line"), ScriptFallback.CurrentNpcLine()->Id, 30);
	}

	FElysiumDlgConversation LineOneFallback(File, true, false,
		[](const FString&) { return false; }, [](const FString&) {});
	LineOneFallback.Start();
	if (TestNotNull(TEXT("line-1 fallback opens"), LineOneFallback.CurrentNpcLine()))
	{
		TestEqual(TEXT("absent usescript selects line 1"), LineOneFallback.CurrentNpcLine()->Id, 1);
	}

	// A used script that returns no integer produces 0 in retail. Acquire then substitutes the first
	// stored line id; use a file with neither line 0 nor line 1 to make that final fallback observable.
	TSharedRef<FElysiumDlgFile> FirstStored = MakeShared<FElysiumDlgFile>();
	if (!TestTrue(TEXT("first-stored fixture parses"), FElysiumDlgFile::ParseBytes(
		ElysiumDlgBytes({ ElysiumDlgRow(50, TEXT("First stored."), TEXT("#"), TEXT(""), TEXT("")) }),
		FirstStored.Get())))
	{
		return false;
	}
	FElysiumDlgConversation InvalidScriptResult(FirstStored, true, false,
		[](const FString&) { return false; }, [](const FString&) {},
		[]() -> TOptional<int32> { return 0; });
	InvalidScriptResult.Start();
	if (TestNotNull(TEXT("first-stored fallback opens"), InvalidScriptResult.CurrentNpcLine()))
	{
		TestEqual(TEXT("invalid usescript result falls back to first stored line"),
			InvalidScriptResult.CurrentNpcLine()->Id, 50);
	}

	return true;
}

// =====================================================================================
// 9.3 CPython end-to-end — the writers and the two-phase spawn driven through the REAL
// Python glue (arg parsing, __getattr__ dispatch, the module globals), not just the C++
// substrate. Self-skips when the embedded VM is unavailable, so it never yields a false
// failure on a non-CPython build/host.
// =====================================================================================

#if defined(ELYSIUM_WITH_CPYTHON) && ELYSIUM_WITH_CPYTHON
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCPythonWritersTest, "Elysium.Substrate.CPythonWriters", GElysiumTestFlags)
bool FElysiumCPythonWritersTest::RunTest(const FString&)
{
	FElysiumPythonVM& VM = FElysiumPythonVM::Get();
	FString Err;
	if (!VM.EnsureStarted(Err))
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: embedded CPython unavailable (%s)"), *Err));
		return true;   // self-skip: never a false failure where the SDK is absent
	}

	// A bare world (Owner null, so no bodies/visuals) with one named entity to drive through Python.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__py_test__");
	FElysiumEntityDef Relay;
	Relay.Classname = TEXT("logic_relay");
	Relay.TargetName = TEXT("wr_target");
	Relay.Origin = FVector(1, 2, 3);
	Defs.Defs.Add(MoveTemp(Relay));
	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	// Ctx.World is what CurrentWorld() resolves against for the duration of each eval.
	FElysiumScriptContext Ctx;
	Ctx.World = &World;
	auto Eval = [&](const TCHAR* Src)
	{
		FString E;
		const FElysiumVariant V = VM.Eval(FString(Src), Ctx, E);
		if (!E.IsEmpty()) { AddError(FString::Printf(TEXT("eval '%s' raised: %s"), Src, *E)); }
		return V;
	};

	// SetName re-keys through the glue: old targetname stops resolving, the new one resolves.
	Eval(TEXT("FindEntityByName(\"wr_target\").SetName(\"wr_renamed\")"));
	TestNull(TEXT("old name gone after SetName"), World.FindByName(TEXT("wr_target")));
	FElysiumEntity* Renamed = World.FindByName(TEXT("wr_renamed"));
	if (!TestNotNull(TEXT("new name resolves after SetName"), Renamed))
	{
		return false;
	}

	// SetOrigin (tuple-arg form) mutates the runtime origin.
	Eval(TEXT("FindEntityByName(\"wr_renamed\").SetOrigin((7,8,9))"));
	TestTrue(TEXT("SetOrigin moved the runtime origin"), Renamed->Origin.Equals(FVector(7, 8, 9)));

	// SetModel + GetModelName round-trip through the glue (clean string, no repr).
	Eval(TEXT("FindEntityByName(\"wr_renamed\").SetModel(\"models/test/foo.mdl\")"));
	const FElysiumVariant Model = Eval(TEXT("FindEntityByName(\"wr_renamed\").GetModelName()"));
	TestEqual(TEXT("GetModelName reflects SetModel"), Model.ToString(), FString(TEXT("models/test/foo.mdl")));

	// Two-phase spawn end to end: CreateEntityNoSpawn -> SetName -> CallEntitySpawn.
	Eval(TEXT("__pytest_e = CreateEntityNoSpawn(\"math_counter\", (4,5,6), (0,0,0))"));
	Eval(TEXT("__pytest_e.SetName(\"pytest_spawned\")"));
	FElysiumEntity* Spawned = World.FindByName(TEXT("pytest_spawned"));
	if (TestNotNull(TEXT("CreateEntityNoSpawn made a findable entity"), Spawned))
	{
		TestFalse(TEXT("not spawned before CallEntitySpawn"), Spawned->bSpawnCalled);
		TestTrue(TEXT("origin came from the create args"), Spawned->Origin.Equals(FVector(4, 5, 6)));
		Eval(TEXT("CallEntitySpawn(__pytest_e)"));
		TestTrue(TEXT("spawned after CallEntitySpawn"), Spawned->bSpawnCalled);
	}

	// --- 9.7d: the shadowed-name split, through the real getattro -------------------------
	// `Whisper` and `FrenzyTrigger` are `vamputil.py` helpers AND datamap input names, so the
	// receiver — not the name — decides which one runs (script_api.md). Stand up the helper the
	// way vamputil defines it (a plain `__main__` function) and check the two spellings stay
	// distinct: the bare call reaches the script, the qualified one the datamap input.
	World.SpawnPlayer();
	Eval(TEXT("__pytest_whispers = []"));
	Eval(TEXT("def Whisper(s):\n    __pytest_whispers.append(s)\n"));
	Eval(TEXT("def FrenzyTrigger(char):\n    char.FrenzyTrigger(1)\n"));

	TestTrue(TEXT("pc resolves to the player entity"), Eval(TEXT("pc is not None")).ToBool());
	TestTrue(TEXT("pc.Whisper is not the __main__ helper"),
		Eval(TEXT("pc.Whisper is not Whisper")).ToBool());
	TestTrue(TEXT("pc.FrenzyTrigger is not the __main__ helper"),
		Eval(TEXT("pc.FrenzyTrigger is not FrenzyTrigger")).ToBool());

	Eval(TEXT("Whisper(\"Crying\")"));      // bare -> the script helper (24 `.dlg` sites)
	Eval(TEXT("pc.Whisper(\"Crying\")"));   // receiver-qualified -> the player datamap input (9 sites)
	World.Tick(0.0);
	TestEqual(TEXT("only the bare spelling ran the helper"),
		Eval(TEXT("len(__pytest_whispers)")).ToInt(), 1);
	// `__main__` is process-global, so take the two real script names back out again.
	Eval(TEXT("del Whisper, FrenzyTrigger, __pytest_whispers"));

	// OneOfSet through the real module global: the same exactly-one-of-N set property, with the
	// roll pinned so the assertion is deterministic.
	ElysiumScriptNatives::SetOneOfSetRoll(2);
	TestTrue(TEXT("the roll's row passes under CPython"), Eval(TEXT("OneOfSet(3,7)")).ToBool());
	TestFalse(TEXT("a sibling row does not"), Eval(TEXT("OneOfSet(4,7)")).ToBool());
	TestEqual(TEXT("a 7-row set shows exactly one row"),
		Eval(TEXT("len([w for w in range(1,8) if OneOfSet(w,7)])")).ToInt(), 1);
	ElysiumScriptNatives::SetOneOfSetRoll(-1);

	return true;
}
#endif // ELYSIUM_WITH_CPYTHON

} // namespace ElysiumDialogueTests

#endif // WITH_DEV_AUTOMATION_TESTS
