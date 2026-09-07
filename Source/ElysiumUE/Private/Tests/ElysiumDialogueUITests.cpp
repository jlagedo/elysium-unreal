#if WITH_DEV_AUTOMATION_TESTS

// D5 Presentation — the dialogue box's own rules, asserted with no viewport, no world and no RHI.
//
// Everything here is a rule about what the player SEES and what a key MEANS, and each is a named
// modernization of a retail behaviour the port replaces rather than reproduces:
//
//   M-REQ       retail encodes a dependency as a font/colour (`GetFontForFlagsDependency`
//               `0x10053f10`) and N dot glyphs (`CDialogDependency::ToStr` `0x100ea5e0`); the port
//               writes `[ PERSUASION 4/7 ]` before the sentence.
//   M-DISABLED  a row failing only its skill front stays visible, greyed and unpickable, keeping
//               its number so the keys never move.
//   M-REVEAL    the band is published with the line instead of being withheld by
//               `ShowPlayerChoices` while `IsTalking()`.
//   M-SKIP      retail's hurry verb (`dialogpick -2`, `0x102c0bb0`) becomes Space-while-speaking.

#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumViewState.h"
#include "UI/ElysiumActionButton.h"
#include "UI/ElysiumDialogueScreen.h"
#include "UI/ElysiumDialogueWidget.h"

#include "CommonInputSettings.h"
#include "ICommonInputModule.h"
#include "Misc/AutomationTest.h"
#include "Tests/ElysiumDialogueTestHelpers.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace ElysiumDialogueUITests
{
using ElysiumDialogueTestHelpers::ElysiumDlgRow;
using ElysiumDialogueTestHelpers::ElysiumDlgBytes;

// The constructed Slate is the only honest witness for "the row was drawn": a disabled row is
// deliberately NOT an action, so `FindAction` cannot see it and only the widget tree can.
inline bool SlateShowsText(const TSharedRef<SWidget>& Widget, const TCHAR* Text)
{
	if (Widget->GetType() == TEXT("STextBlock")
		&& StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString() == Text)
	{
		return true;
	}
	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
	{
		if (SlateShowsText(Children->GetChildAt(Index), Text))
		{
			return true;
		}
	}
	return false;
}

// M-DISABLED's dimming is one render-opacity set on the row's content, so counting widgets carrying
// exactly that opacity counts disabled rows without reaching into the box's private layout.
inline int32 CountDimmedWidgets(const TSharedRef<SWidget>& Widget)
{
	int32 Count = FMath::IsNearlyEqual(
		Widget->GetRenderOpacity(), ElysiumDialogueUI::DisabledRowOpacity) ? 1 : 0;
	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
	{
		Count += CountDimmedWidgets(Children->GetChildAt(Index));
	}
	return Count;
}

static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

// The two injected gate interfaces, faked. Deliberately local to this file: these tests assert the
// PRESENTATION of a gate result, so the sheet only has to be able to produce one.
struct FSheetFake final : public IElysiumDlgSheet, public IElysiumDlgTraitResolver
{
	struct FTrait
	{
		EElysiumDlgTraitClass Class = EElysiumDlgTraitClass::Unknown;
		int32 Id = 0;
		int32 Value = 0;
	};
	TMap<FString, FTrait> Traits;
	int32 Blood = 10;
	int32 Clan = ElysiumDlgClan::Ventrue;

	FSheetFake()
	{
		// The shipped vocabulary's real classes: `Persuasion`/`Intimidate` are feats, `Humanity` is
		// an attribute, `Dominate` is discipline slot 6 — the slot `TestSimple` (`0x100e9760`)
		// gates on Ventrue, which is why this fake player is Ventrue.
		Traits.Add(TEXT("Persuasion"), { EElysiumDlgTraitClass::Feat, 7, 4 });
		Traits.Add(TEXT("Intimidate"), { EElysiumDlgTraitClass::Feat, 6, 7 });
		Traits.Add(TEXT("Humanity"), { EElysiumDlgTraitClass::Attribute, 27, 7 });
		Traits.Add(TEXT("Dominate"), { EElysiumDlgTraitClass::Discipline, 6, 2 });
	}

	virtual bool ResolveTrait(const FString& Name, EElysiumDlgTraitClass& OutClass,
		int32& OutId) const override
	{
		if (const FTrait* T = Traits.Find(Name))
		{
			OutClass = T->Class;
			OutId = T->Id;
			return true;
		}
		return false;
	}
	virtual FString DisciplineName(int32 Id) const override
	{
		return Id == 6 ? FString(TEXT("Dominate")) : FString();
	}

	virtual int32 CalcFeat(const FString& FeatName) const override
	{
		const FTrait* T = Traits.Find(FeatName);
		return T ? T->Value : 0;
	}
	virtual int32 Stat(EElysiumDlgTraitClass Class, int32 Id) const override
	{
		for (const TPair<FString, FTrait>& Row : Traits)
		{
			if (Row.Value.Class == Class && Row.Value.Id == Id)
			{
				return Row.Value.Value;
			}
		}
		return 0;
	}
	virtual int32 Discipline(int32 Id) const override
	{
		return Stat(EElysiumDlgTraitClass::Discipline, Id);
	}
	virtual int32 BloodPool() const override { return Blood; }
	virtual bool IsMale() const override { return true; }
	virtual int32 ClanOffset() const override { return Clan; }
};

FElysiumDlgGateLabel MakeLabel(const TCHAR* Trait, EElysiumDlgTraitClass Kind, int32 Have,
	int32 Required, int32 BloodCost = 0)
{
	FElysiumDlgGateLabel Label;
	Label.bValid = true;
	Label.Trait = Trait;
	Label.Kind = Kind;
	Label.Have = Have;
	Label.Required = Required;
	Label.BloodCost = BloodCost;
	return Label;
}

// A conversation over synthetic rows with the fake sheet bound.
struct FBand
{
	TSharedPtr<FElysiumDlgFile> File;
	TSharedPtr<FElysiumDlgConversation> Conv;
	TSharedPtr<FSheetFake> Sheet;

	bool Build(FAutomationTestBase& Test, const TArray<FString>& Rows,
		TSet<FString> PythonTrue = TSet<FString>())
	{
		Sheet = MakeShared<FSheetFake>();
		File = MakeShared<FElysiumDlgFile>();
		if (!Test.TestTrue(TEXT("band fixture parses"),
			FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), *File)))
		{
			return false;
		}
		File->SourcePath = TEXT("dlg/test/ui_band.dlg");
		Conv = MakeShared<FElysiumDlgConversation>(File.ToSharedRef(), /*bMale*/ true,
			/*bMalk*/ false,
			[PythonTrue](const FString& Source) { return PythonTrue.Contains(Source); },
			[](const FString&) {});
		// The sheet doubles as the resolver; both handles alias the same object, which is safe
		// because the conversation only ever reads through them.
		Conv->SetGateContext(Sheet, TSharedPtr<const IElysiumDlgTraitResolver>(Sheet),
			ElysiumDlgClan::Ventrue);
		Conv->Start();
		return true;
	}
};
}

// -------------------------------------------------------------------------------------------
// M-REQ — the requirement label text, in isolation. Pure: no Slate, no world, no conversation.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueChoiceLabelsTest,
	"Elysium.UI.DialogueChoiceLabels", ElysiumDialogueUITests::GElysiumTestFlags)
bool FElysiumDialogueChoiceLabelsTest::RunTest(const FString&)
{
	using namespace ElysiumDialogueUITests;

	// A feat the player is short of — the shape the modernization names.
	TestEqual(TEXT("a failed feat check reads have/required"),
		ElysiumDialogueUI::FormatRequirementLabel(
			MakeLabel(TEXT("Persuasion"), EElysiumDlgTraitClass::Feat, 4, 7)),
		FString(TEXT("[ PERSUASION 4/7 ]")));

	// M-REQ shows the label on PASSING rows too: the player reads what the line cost them, not
	// only what they lack. Retail's font encoding could not say this at all.
	TestEqual(TEXT("a passing feat check still carries its label"),
		ElysiumDialogueUI::FormatRequirementLabel(
			MakeLabel(TEXT("Intimidate"), EElysiumDlgTraitClass::Feat, 7, 7)),
		FString(TEXT("[ INTIMIDATE 7/7 ]")));

	// Attributes and abilities use the same shape — the label is about the trait, not its class.
	TestEqual(TEXT("an attribute check reads the same"),
		ElysiumDialogueUI::FormatRequirementLabel(
			MakeLabel(TEXT("Humanity"), EElysiumDlgTraitClass::Attribute, 6, 8)),
		FString(TEXT("[ HUMANITY 6/8 ]")));
	TestEqual(TEXT("an ability check reads the same"),
		ElysiumDialogueUI::FormatRequirementLabel(
			MakeLabel(TEXT("Brawl"), EElysiumDlgTraitClass::Ability, 2, 3)),
		FString(TEXT("[ BRAWL 2/3 ]")));

	// A discipline names its price: `TestSimple` (`0x100e9760`) requires the rating AND the pool,
	// and `pc_charge_dependency` (`0x100e8b90`) takes the pool on the pick.
	TestEqual(TEXT("a discipline appends its blood cost"),
		ElysiumDialogueUI::FormatRequirementLabel(
			MakeLabel(TEXT("Dominate"), EElysiumDlgTraitClass::Discipline, 2, 2, 2)),
		FString(TEXT("[ DOMINATE 2/2 \u00B7 2 BLOOD ]")));
	// S6 — the blood-short arm of M-DISABLED. The rating passes and the pool does not, so the price
	// is written have/needed: a bare `2 BLOOD` beside a passing `3/2` would read as an unexplained
	// refusal, naming neither the half that failed nor by how much.
	FElysiumDlgGateLabel BloodShort =
		MakeLabel(TEXT("Dominate"), EElysiumDlgTraitClass::Discipline, 3, 2, 2);
	BloodShort.Pool = 1;
	BloodShort.bBloodShort = true;
	TestEqual(TEXT("a blood-short discipline names the pool it lacks"),
		ElysiumDialogueUI::FormatRequirementLabel(BloodShort),
		FString(TEXT("[ DOMINATE 3/2 · 1/2 BLOOD ]")));
	// An empty pool is still a pool: 0/2 is the reading, not a suppressed suffix.
	FElysiumDlgGateLabel Drained = BloodShort;
	Drained.Pool = 0;
	TestEqual(TEXT("a drained pool reads 0 rather than dropping the price"),
		ElysiumDialogueUI::FormatRequirementLabel(Drained),
		FString(TEXT("[ DOMINATE 3/2 · 0/2 BLOOD ]")));
	// Affordable: the same row with the pool met keeps the plain price, so the have/needed form is
	// only ever spent on the failure it explains.
	FElysiumDlgGateLabel Affordable = BloodShort;
	Affordable.Pool = 8;
	Affordable.bBloodShort = false;
	TestEqual(TEXT("an affordable discipline keeps the plain price"),
		ElysiumDialogueUI::FormatRequirementLabel(Affordable),
		FString(TEXT("[ DOMINATE 3/2 · 2 BLOOD ]")));

	TestEqual(TEXT("a costless discipline row omits the blood suffix"),
		ElysiumDialogueUI::FormatRequirementLabel(
			MakeLabel(TEXT("Presence"), EElysiumDlgTraitClass::Discipline, 1, 0, 0)),
		FString(TEXT("[ PRESENCE 1/0 ]")));

	// No labellable skill front: a pure-Python gate, an unresolved trait and an authored
	// negative-threshold failure route all arrive with `bValid` clear and draw no label.
	FElysiumDlgGateLabel PythonOnly;
	TestEqual(TEXT("a python-only gate carries no label"),
		ElysiumDialogueUI::FormatRequirementLabel(PythonOnly), FString());
	FElysiumDlgGateLabel Nameless = MakeLabel(TEXT(""), EElysiumDlgTraitClass::Feat, 1, 2);
	TestEqual(TEXT("a nameless trait carries no label"),
		ElysiumDialogueUI::FormatRequirementLabel(Nameless), FString());

	return !HasAnyErrors();
}

// -------------------------------------------------------------------------------------------
// The publish: what a real conversation projects into the view the box reads.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueViewPublishTest,
	"Elysium.UI.DialogueViewPublish", ElysiumDialogueUITests::GElysiumTestFlags)
bool FElysiumDialogueViewPublishTest::RunTest(const FString&)
{
	using namespace ElysiumDialogueUITests;

	FBand Band;
	// Row 12 passes (Intimidate 7/7), row 13 fails only its skill front and is kept disabled
	// (Persuasion 4/7), row 14 is an authored negative-threshold failure route (`Humanity -5`,
	// the player has 7) and hides — a negative row is never shown disabled — and row 15 is
	// ungated. Author order is 12, 13, 15.
	if (!Band.Build(*this, {
		ElysiumDlgRow(11, TEXT("Well?"), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(12, TEXT("Do as I say."), TEXT("21"), TEXT("Intimidate 7"), TEXT("")),
		ElysiumDlgRow(13, TEXT("Hear me out."), TEXT("21"), TEXT("Persuasion 7"), TEXT("")),
		ElysiumDlgRow(14, TEXT("I am past caring."), TEXT("21"), TEXT("Humanity -5"), TEXT("")),
		ElysiumDlgRow(15, TEXT("Nothing, forget it."), TEXT("21"), TEXT(""), TEXT("")),
		ElysiumDlgRow(21, TEXT("Fine."), TEXT("#"), TEXT(""), TEXT("")),
	}))
	{
		return false;
	}

	FElysiumDialogueView View;
	ElysiumDialogueUI::FillTurn(*Band.Conv, View);

	TestEqual(TEXT("the subtitle is the NPC line"), View.Line, FString(TEXT("Well?")));
	if (!TestEqual(TEXT("the band shows the enabled, the disabled and the ungated row"),
		View.Choices.Num(), 3))
	{
		return false;
	}

	// M-DISABLED: numbering is positional across enabled and disabled rows, so the disabled row
	// occupies number 2 and the ungated row keeps number 3.
	TestEqual(TEXT("row 1 is the passing skill row"), View.Choices[0].Text,
		FString(TEXT("Do as I say.")));
	TestTrue(TEXT("row 1 is enabled"), View.Choices[0].bEnabled);
	TestEqual(TEXT("row 1 carries its passing label"), View.Choices[0].Label,
		FString(TEXT("[ INTIMIDATE 7/7 ]")));
	TestEqual(TEXT("row 1 keeps its .dlg id"), View.Choices[0].LineId, 12);

	TestEqual(TEXT("row 2 is the failed skill row"), View.Choices[1].Text,
		FString(TEXT("Hear me out.")));
	TestFalse(TEXT("row 2 is disabled"), View.Choices[1].bEnabled);
	TestEqual(TEXT("row 2 carries its requirement label"), View.Choices[1].Label,
		FString(TEXT("[ PERSUASION 4/7 ]")));
	TestEqual(TEXT("row 2 reports what the player has"), View.Choices[1].Have, 4);
	TestEqual(TEXT("row 2 reports what the row asks"), View.Choices[1].Required, 7);
	TestTrue(TEXT("row 2 names the trait class"),
		View.Choices[1].Kind == EElysiumDlgTraitClass::Feat);

	TestEqual(TEXT("row 3 is the ungated row, not the negative-threshold route"),
		View.Choices[2].Text, FString(TEXT("Nothing, forget it.")));
	TestTrue(TEXT("row 3 is enabled"), View.Choices[2].bEnabled);
	TestEqual(TEXT("an ungated row carries no label"), View.Choices[2].Label, FString());

	TestEqual(TEXT("the numbering is author order, 1..N across both"),
		ElysiumDialogueUI::RowTextFor(View.Choices, 1).Number, 2);
	TestEqual(TEXT("the disabled row's number carries its label"),
		ElysiumDialogueUI::RowTextFor(View.Choices, 1).Label,
		FString(TEXT("[ PERSUASION 4/7 ]")));
	TestEqual(TEXT("the ids are parallel to the rows"), View.ChoiceIds.Num(), 3);
	TestEqual(TEXT("row 2's id is the disabled row's"), View.ChoiceIds[1], 13);
	TestEqual(TEXT("row 3's id is the ungated row's"), View.ChoiceIds[2], 15);
	TestFalse(TEXT("this band has a valid reply"), View.bNoValidReply);

	// The no-valid-reply substitution (`get_pc_responses` `0x100e82d0`): every row gated out with
	// no automatic continuation replaces the NPC's own subtitle and offers one Continue.
	FBand Gated;
	if (Gated.Build(*this, {
		ElysiumDlgRow(11, TEXT("Say something."), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(12, TEXT("Locked."), TEXT("21"), TEXT("G.Locked == 1"), TEXT("")),
		ElysiumDlgRow(21, TEXT("Fine."), TEXT("#"), TEXT(""), TEXT("")),
	}))
	{
		FElysiumDialogueView Substituted;
		ElysiumDialogueUI::FillTurn(*Gated.Conv, Substituted);
		TestTrue(TEXT("the machine reports the authoring gap"), Substituted.bNoValidReply);
		TestEqual(TEXT("presentation substitutes the NPC subtitle"), Substituted.Line,
			FString(FElysiumDlgConversation::NoValidReplyText()));
		TestEqual(TEXT("a python-gated row stays hidden, not disabled"),
			Substituted.Choices.Num(), 0);
	}

	// The voice ending is not a new turn, but it does change what the box draws (the skip hint
	// comes down, Space becomes Continue), so it is part of the reconcile key rather than of the
	// revision. The conversation pointer is identity-only content: the reconcile never dereferences
	// it and, since S7, never compares it either.
	const FElysiumDlgConversation* const ConvId = Band.Conv.Get();
	FElysiumDialogueView Speaking;
	Speaking.Conversation = ConvId;
	Speaking.DialogSerial = 4;
	Speaking.Revision = 7;
	Speaking.bNpcSpeaking = true;
	FElysiumDialogueView Silent = Speaking;
	Silent.bNpcSpeaking = false;

	using EAction = ElysiumView::EDialogueAction;
	TestEqual(TEXT("the same turn while still speaking leaves the box alone"),
		ElysiumView::ReconcileDialogue(4, 7, Speaking, /*bShownSpeaking*/ true),
		EAction::None);
	TestEqual(TEXT("the voice ending refreshes the box without a new turn"),
		ElysiumView::ReconcileDialogue(4, 7, Silent, /*bShownSpeaking*/ true),
		EAction::Rebuild);
	TestEqual(TEXT("and the refreshed box is then stable again"),
		ElysiumView::ReconcileDialogue(4, 7, Silent, /*bShownSpeaking*/ false),
		EAction::None);

	// S7 — the identity itself. A one-turn conversation that closes and immediately opens another
	// in the same frame can have the successor land on the freed allocation, and every conversation
	// starts its revision at 1, so (address, revision) repeats and the box would keep the dead band
	// up. The world's open serial is monotonic per `OpenDialog` and is what the key compares.
	FElysiumDialogueView FirstTurn;
	FirstTurn.Conversation = ConvId;
	FirstTurn.DialogSerial = 9;
	FirstTurn.Revision = 1;
	FElysiumDialogueView Closed;                               // nothing open: serial 0, null conv
	FElysiumDialogueView SuccessorAtSameAddress = FirstTurn;   // same pointer, same revision 1
	SuccessorAtSameAddress.DialogSerial = 10;
	TestEqual(TEXT("the same conversation on the same turn is still stable"),
		ElysiumView::ReconcileDialogue(9, 1, FirstTurn), EAction::None);
	TestEqual(TEXT("a successor reusing the address AND revision 1 still rebuilds"),
		ElysiumView::ReconcileDialogue(9, 1, SuccessorAtSameAddress), EAction::Rebuild);
	TestEqual(TEXT("a closed world tears the box down by serial, not by pointer"),
		ElysiumView::ReconcileDialogue(9, 1, Closed), EAction::Teardown);
	TestEqual(TEXT("nothing shown and nothing open stays silent"),
		ElysiumView::ReconcileDialogue(0, 0, Closed), EAction::None);
	TestEqual(TEXT("the first open rebuilds from an empty screen"),
		ElysiumView::ReconcileDialogue(0, 0, FirstTurn), EAction::Rebuild);

	return !HasAnyErrors();
}

// -------------------------------------------------------------------------------------------
// Input: a disabled row's key does nothing, and Space means skip while the voice runs.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueInputDisabledAndSkipTest,
	"Elysium.UI.DialogueInputDisabledAndSkip", ElysiumDialogueUITests::GElysiumTestFlags)
bool FElysiumDialogueInputDisabledAndSkipTest::RunTest(const FString&)
{
	using namespace ElysiumDialogueUITests;

	FElysiumDialogueView Turn;
	Turn.Choices.AddDefaulted(3);
	Turn.Choices[0].Text = TEXT("Enabled one");
	Turn.Choices[1].Text = TEXT("Disabled two");
	Turn.Choices[1].bEnabled = false;
	Turn.Choices[1].Label = TEXT("[ PERSUASION 4/7 ]");
	Turn.Choices[2].Text = TEXT("Enabled three");
	Turn.ChoiceIds = { 12, 13, 15 };
	Turn.bNpcSpeaking = true;
	Turn.bCanSkip = true;

	// M-DISABLED: the key map is positional, so the disabled row still OWNS its number — the row a
	// key names never moves when the player's skills change. The refusal is the screen's.
	const TOptional<int32> Two = ElysiumDialogueUI::ChoiceForKey(EKeys::Two, 3, false);
	TestTrue(TEXT("the disabled row still owns key 2"), Two.IsSet() && Two.GetValue() == 1);
	const TOptional<int32> Three = ElysiumDialogueUI::ChoiceForKey(EKeys::Three, 3, false);
	TestTrue(TEXT("the row after it keeps key 3"), Three.IsSet() && Three.GetValue() == 2);

	UElysiumDialogueScreen* Screen = NewObject<UElysiumDialogueScreen>();
	Screen->ApplyDialogue(Turn);
	TestTrue(TEXT("the screen accepts an enabled row"), Screen->AcceptsChoice(0));
	TestFalse(TEXT("the screen refuses the disabled row"), Screen->AcceptsChoice(1));
	TestTrue(TEXT("the screen accepts the row after it"), Screen->AcceptsChoice(2));
	TestFalse(TEXT("the screen refuses a row that does not exist"), Screen->AcceptsChoice(9));
	TestTrue(TEXT("Continue is always acceptable on a live band"), Screen->AcceptsChoice(-1));

	// Building the widget is what proves the disabled row never becomes an action: only the two
	// enabled rows get a CommonUI button, so the row is unclickable, unhoverable and unreachable
	// by gamepad focus at once, and focus repairs past it.
	// CommonUI ensures on an unloaded input table the moment a CommonButton is constructed, and a
	// headless run has never opened a screen, so the settings are loaded here rather than relying
	// on some earlier test in the same process having done it.
	ICommonInputModule::GetSettings().LoadData();
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	TestNotNull(TEXT("the enabled row is an action"), Screen->FindAction(TEXT("Dialogue.Choice.12")));
	TestNull(TEXT("the disabled row is not an action"),
		Screen->FindAction(TEXT("Dialogue.Choice.13")));
	TestNotNull(TEXT("the row after it is an action"),
		Screen->FindAction(TEXT("Dialogue.Choice.15")));
	TestEqual(TEXT("focus lands on the first enabled row"), Screen->GetSelectedActionId(),
		FName(TEXT("Dialogue.Choice.12")));
	TestTrue(TEXT("navigation moves straight past the disabled row"),
		Screen->Navigate(EElysiumNavigationDirection::Down));
	TestEqual(TEXT("...to the next enabled row"), Screen->GetSelectedActionId(),
		FName(TEXT("Dialogue.Choice.15")));

	// A live automatic wait owns the keys and resolves from voice completion, so nothing is
	// acceptable through them — except the skip verb, which is what ends that wait.
	FElysiumDialogueView Automatic = Turn;
	Automatic.Choices.Reset();
	Automatic.ChoiceIds.Reset();
	Automatic.bAwaitingAutomatic = true;
	Screen->ApplyDialogue(Automatic);
	TestFalse(TEXT("a live automatic wait refuses Continue"), Screen->AcceptsChoice(-1));

	// M-SKIP. Space is the hurry verb exactly while the voice runs and the world would act on it;
	// once it is done, the same key is Continue on a terminal turn.
	TestTrue(TEXT("Space is skip while the NPC speaks"),
		ElysiumDialogueUI::IsSkipKey(EKeys::SpaceBar, true, true));
	TestFalse(TEXT("Space is not skip once the voice ends"),
		ElysiumDialogueUI::IsSkipKey(EKeys::SpaceBar, false, true));
	TestFalse(TEXT("Space is not skip when the world would refuse the hurry"),
		ElysiumDialogueUI::IsSkipKey(EKeys::SpaceBar, true, false));
	const TOptional<int32> Advance =
		ElysiumDialogueUI::ChoiceForKey(EKeys::SpaceBar, 0, /*bTerminal*/ true);
	TestTrue(TEXT("Space advances a terminal turn"),
		Advance.IsSet() && Advance.GetValue() == -1);

	return !HasAnyErrors();
}

// -------------------------------------------------------------------------------------------
// S3 — the band where every row failed its skill front. `bTerminal` is raised by
// `FElysiumDlgConversation::IsTerminalLine()`, which is "no ENABLED choice" — so it is TRUE for
// exactly the M-DISABLED band that the modernization exists to show. Drawing only Continue there
// would erase every requirement label at the one moment the player needs to read them.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueDisabledTerminalBandTest,
	"Elysium.UI.DialogueDisabledTerminalBand", ElysiumDialogueUITests::GElysiumTestFlags)
bool FElysiumDialogueDisabledTerminalBandTest::RunTest(const FString&)
{
	using namespace ElysiumDialogueUITests;

	FElysiumDialogueView Turn;
	Turn.Speaker = TEXT("Jack");
	Turn.Line = TEXT("Well?");
	Turn.Choices.AddDefaulted(2);
	Turn.Choices[0].Text = TEXT("Do as I say.");
	Turn.Choices[0].Label = TEXT("[ INTIMIDATE 4/7 ]");
	Turn.Choices[0].bEnabled = false;
	Turn.Choices[1].Text = TEXT("Hear me out.");
	Turn.Choices[1].Label = TEXT("[ PERSUASION 4/7 ]");
	Turn.Choices[1].bEnabled = false;
	Turn.ChoiceIds = { 12, 13 };
	// What the publisher sends for this band: no enabled row, so the conversation calls itself
	// terminal while `Choices` still carries both rows.
	Turn.bTerminal = true;

	ICommonInputModule::GetSettings().LoadData();
	UElysiumDialogueScreen* Screen = NewObject<UElysiumDialogueScreen>();
	Screen->ApplyDialogue(Turn);
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	// Both rows are drawn — as plain Slate, never as actions, which is the single stroke that makes
	// a disabled row unclickable, unhoverable and unreachable by focus.
	TestTrue(TEXT("the first failed row is drawn with its sentence"),
		ElysiumDialogueUITests::SlateShowsText(Slate, TEXT("Do as I say.")));
	TestTrue(TEXT("the first failed row keeps its requirement label"),
		ElysiumDialogueUITests::SlateShowsText(Slate, TEXT("[ INTIMIDATE 4/7 ]")));
	TestTrue(TEXT("the second failed row is drawn with its sentence"),
		ElysiumDialogueUITests::SlateShowsText(Slate, TEXT("Hear me out.")));
	TestTrue(TEXT("the second failed row keeps its requirement label"),
		ElysiumDialogueUITests::SlateShowsText(Slate, TEXT("[ PERSUASION 4/7 ]")));
	TestTrue(TEXT("the rows keep their positional numbers"),
		ElysiumDialogueUITests::SlateShowsText(Slate, TEXT("1."))
			&& ElysiumDialogueUITests::SlateShowsText(Slate, TEXT("2.")));
	TestEqual(TEXT("both rows are dimmed (M-DISABLED)"),
		ElysiumDialogueUITests::CountDimmedWidgets(Slate), 2);

	// And Continue is drawn after them, as the only action on the turn.
	TestTrue(TEXT("Continue is drawn as well"),
		ElysiumDialogueUITests::SlateShowsText(Slate, TEXT("Continue")));
	TestNotNull(TEXT("Continue is the turn's action"),
		Screen->FindAction(TEXT("Dialogue.Continue")));
	TestNull(TEXT("the first failed row is not an action"),
		Screen->FindAction(TEXT("Dialogue.Choice.12")));
	TestNull(TEXT("the second failed row is not an action"),
		Screen->FindAction(TEXT("Dialogue.Choice.13")));
	TestEqual(TEXT("focus repairs onto Continue, the only reachable row"),
		Screen->GetSelectedActionId(), FName(TEXT("Dialogue.Continue")));

	// The keys: 1 and 2 still NAME their rows (positional, M-DISABLED) and the screen refuses both.
	// Key 1 must not quietly become Continue while row 1 is on screen.
	const TOptional<int32> One = ElysiumDialogueUI::ChoiceForKey(EKeys::One, 2, /*bTerminal*/ true);
	TestTrue(TEXT("key 1 still names row 1, not Continue"), One.IsSet() && One.GetValue() == 0);
	TestFalse(TEXT("...and the screen refuses it"), Screen->AcceptsChoice(0));
	const TOptional<int32> Two = ElysiumDialogueUI::ChoiceForKey(EKeys::Two, 2, /*bTerminal*/ true);
	TestTrue(TEXT("key 2 still names row 2"), Two.IsSet() && Two.GetValue() == 1);
	TestFalse(TEXT("...and the screen refuses it too"), Screen->AcceptsChoice(1));

	// Space and Enter are the way out.
	const TOptional<int32> Space = ElysiumDialogueUI::ChoiceForKey(EKeys::SpaceBar, 2, true);
	TestTrue(TEXT("Space advances the all-disabled band"), Space.IsSet() && Space.GetValue() == -1);
	const TOptional<int32> Enter = ElysiumDialogueUI::ChoiceForKey(EKeys::Enter, 2, true);
	TestTrue(TEXT("Enter advances it too"), Enter.IsSet() && Enter.GetValue() == -1);
	TestTrue(TEXT("the screen accepts the advance"), Screen->AcceptsChoice(-1));

	// The genuinely empty band is still Continue-only: no rows, nothing to dim.
	FElysiumDialogueView Empty = Turn;
	Empty.Choices.Reset();
	Empty.ChoiceIds.Reset();
	Screen->ApplyDialogue(Empty);
	const TSharedRef<SWidget> EmptySlate = Screen->TakeWidget();
	TestEqual(TEXT("a real terminal line dims nothing"),
		ElysiumDialogueUITests::CountDimmedWidgets(EmptySlate), 0);
	TestTrue(TEXT("a real terminal line still offers Continue"),
		ElysiumDialogueUITests::SlateShowsText(EmptySlate, TEXT("Continue")));

	return !HasAnyErrors();
}

// -------------------------------------------------------------------------------------------
// S8 — the pick carries the row's `.dlg` line id, not just its position. The band the player
// clicked can already have been replaced (an automatic turn, a script), and a bare position would
// then name whatever sentence now sits there.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueChoiceCarriesLineIdTest,
	"Elysium.UI.DialogueChoiceCarriesLineId", ElysiumDialogueUITests::GElysiumTestFlags)
bool FElysiumDialogueChoiceCarriesLineIdTest::RunTest(const FString&)
{
	using namespace ElysiumDialogueUITests;

	// The UI half: what the screen reports when a row is activated.
	FElysiumDialogueView Turn;
	Turn.Choices.AddDefaulted(2);
	Turn.Choices[0].Text = TEXT("Do as I say.");
	Turn.Choices[1].Text = TEXT("Nothing, forget it.");
	Turn.ChoiceIds = { 12, 15 };

	ICommonInputModule::GetSettings().LoadData();
	UElysiumDialogueScreen* Screen = NewObject<UElysiumDialogueScreen>();
	Screen->ApplyDialogue(Turn);
	int32 SeenIndex = INDEX_NONE;
	int32 SeenLineId = INDEX_NONE;
	Screen->OnChoice.BindLambda([&SeenIndex, &SeenLineId](int32 Index, int32 LineId)
	{
		SeenIndex = Index;
		SeenLineId = LineId;
	});
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->ExecuteAction(TEXT("Dialogue.Choice.15"));
	TestEqual(TEXT("the pick reports its position"), SeenIndex, 1);
	TestEqual(TEXT("the pick carries the row's .dlg line id"), SeenLineId, 15);

	// The id is resolved when the row is picked, not when the button was built, so a band replaced
	// in place reports the sentence the player is actually looking at.
	FElysiumDialogueView Replaced = Turn;
	Replaced.Choices[1].Text = TEXT("Something else entirely.");
	Replaced.ChoiceIds = { 12, 44 };
	Screen->ApplyDialogue(Replaced);
	Screen->ExecuteAction(TEXT("Dialogue.Choice.44"));
	TestEqual(TEXT("the replaced band reports its own id at the same position"), SeenLineId, 44);
	TestEqual(TEXT("...at the same position"), SeenIndex, 1);

	// The substrate half: a real world refuses a position whose id no longer matches. The world is
	// bare (no owner actor, no services) — dialogue is explicitly headless-safe.
	TArray<FString> Rows = {
		ElysiumDlgRow(11, TEXT("Well?"), TEXT("#"), TEXT(""), TEXT("")),
		ElysiumDlgRow(12, TEXT("Do as I say."), TEXT("21"), TEXT(""), TEXT("")),
		ElysiumDlgRow(15, TEXT("Nothing, forget it."), TEXT("21"), TEXT(""), TEXT("")),
		ElysiumDlgRow(21, TEXT("Fine."), TEXT("#"), TEXT(""), TEXT("")),
	};
	TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
	if (!TestTrue(TEXT("the stale-pick fixture parses"),
		FElysiumDlgFile::ParseBytes(ElysiumDlgBytes(Rows), File.Get())))
	{
		return false;
	}
	File->SourcePath = TEXT("dlg/test/ui_stale.dlg");

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__dlg_ui_test__");
	// A BODILESS pedestrian: `OpenDialog` requires the owner to yield a body session, and a bodied
	// NPC refuses one until a think has admitted its mind. Nothing here is about the body.
	FElysiumEntityDef Speaker;
	Speaker.Classname = TEXT("npc_VPedestrian");
	Speaker.TargetName = TEXT("dlg_owner");
	Defs.Defs.Add(MoveTemp(Speaker));
	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);
	// `Activate` only ARMS the mind's admission barrier; an unadmitted NPC refuses
	// `BeginDialogueBodySession` and `OpenDialog` with it. One deterministic think admits it.
	World.Tick(0.0);
	FElysiumEntity* Owner = World.FindByName(TEXT("dlg_owner"));
	if (!TestNotNull(TEXT("the fixture world has a dialogue owner"), Owner))
	{
		return false;
	}

	TSharedRef<FElysiumDlgConversation> Conversation = MakeShared<FElysiumDlgConversation>(
		File, /*bMale*/ true, /*bMalk*/ false,
		[](const FString&) { return true; }, [](const FString&) {});
	Conversation->Start();
	World.OpenDialog(Owner->Handle, Conversation);
	if (!TestNotNull(TEXT("the fixture world opened the conversation"), World.GetOpenDialog()))
	{
		return false;
	}
	TestTrue(TEXT("an open conversation carries a non-zero serial"),
		World.GetOpenDialogSerial() != 0);
	TestEqual(TEXT("the world agrees with the published id at position 0"),
		Conversation->VisibleChoiceLineId(0), 12);
	TestEqual(TEXT("...and at position 1"), Conversation->VisibleChoiceLineId(1), 15);

	// A stale pick: the position is live but names a different row than the one the player saw.
	const uint32 BeforeRevision = Conversation->Revision();
	World.PlayerDialogChoose(0, /*ExpectedLineId*/ 15);
	TestEqual(TEXT("a position whose id does not match is refused"),
		Conversation->Revision(), BeforeRevision);
	TestEqual(TEXT("...and the band is untouched"),
		Conversation->VisibleChoiceLineId(0), 12);

	// The matching pick goes through, and so does a caller that supplies no id (the console verb).
	World.PlayerDialogChoose(0, /*ExpectedLineId*/ 12);
	TestTrue(TEXT("the matching id is accepted"), Conversation->Revision() != BeforeRevision);

	World.CloseDialog(/*bSilent*/ true);
	(void)Slate;
	return !HasAnyErrors();
}

// -------------------------------------------------------------------------------------------
// S10d — M-CAP allows a band of any size. The keyboard shortcut is 1-9; rows past that are reached
// by mouse or by focus navigation, which walks the whole band.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueLargeBandTest,
	"Elysium.UI.DialogueLargeBand", ElysiumDialogueUITests::GElysiumTestFlags)
bool FElysiumDialogueLargeBandTest::RunTest(const FString&)
{
	using namespace ElysiumDialogueUITests;

	FElysiumDialogueView Turn;
	Turn.Choices.AddDefaulted(12);
	for (int32 i = 0; i < 12; ++i)
	{
		Turn.Choices[i].Text = FString::Printf(TEXT("Response %d"), i + 1);
		Turn.ChoiceIds.Add(100 + i);
	}

	// The numbering rule is 1..N with no cap: row 12 is "12.", not a hidden or renumbered row.
	TestEqual(TEXT("the twelfth row is numbered 12"),
		ElysiumDialogueUI::RowTextFor(Turn.Choices, 11).Number, 12);

	ICommonInputModule::GetSettings().LoadData();
	UElysiumDialogueScreen* Screen = NewObject<UElysiumDialogueScreen>();
	Screen->ApplyDialogue(Turn);
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	int32 Drawn = 0;
	int32 Reachable = 0;
	for (int32 i = 0; i < 12; ++i)
	{
		Drawn += ElysiumDialogueUITests::SlateShowsText(
			Slate, *FString::Printf(TEXT("Response %d"), i + 1)) ? 1 : 0;
		Reachable += Screen->FindAction(
			FName(*FString::Printf(TEXT("Dialogue.Choice.%d"), 100 + i))) != nullptr ? 1 : 0;
	}
	TestEqual(TEXT("all twelve rows are drawn"), Drawn, 12);
	TestEqual(TEXT("all twelve rows are focusable actions, not just the first nine"),
		Reachable, 12);

	// Key 9 names row 9 (index 8) and stops there: rows 10-12 have no shortcut, and no key silently
	// wraps onto them.
	const TOptional<int32> Nine = ElysiumDialogueUI::ChoiceForKey(EKeys::Nine, 12, false);
	TestTrue(TEXT("key 9 names row 9"), Nine.IsSet() && Nine.GetValue() == 8);
	const TOptional<int32> Pad = ElysiumDialogueUI::ChoiceForKey(EKeys::NumPadNine, 12, false);
	TestTrue(TEXT("the numpad agrees"), Pad.IsSet() && Pad.GetValue() == 8);
	TestFalse(TEXT("key 0 names nothing"),
		ElysiumDialogueUI::ChoiceForKey(EKeys::Zero, 12, false).IsSet());

	// Focus reaches the tenth row and beyond by navigation alone, which is what makes M-CAP's
	// unshortcut rows playable on keyboard and gamepad.
	TestEqual(TEXT("focus starts on row 1"), Screen->GetSelectedActionId(),
		FName(TEXT("Dialogue.Choice.100")));
	for (int32 Step = 0; Step < 11; ++Step)
	{
		TestTrue(TEXT("navigation walks the whole band"),
			Screen->Navigate(EElysiumNavigationDirection::Down));
	}
	TestEqual(TEXT("...and arrives at row 12"), Screen->GetSelectedActionId(),
		FName(TEXT("Dialogue.Choice.111")));

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
