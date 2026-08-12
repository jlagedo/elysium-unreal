#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumHUDModel.h"
#include "Substrate/ElysiumSignData.h"
#include "UI/ElysiumActionButton.h"
#include "UI/ElysiumCharacterScreen.h"
#include "UI/ElysiumChargenPopup.h"
#include "UI/ElysiumDialogueScreen.h"
#include "UI/ElysiumDialogueWidget.h"
#include "UI/ElysiumCommonUIInputData.h"
#include "UI/ElysiumMainMenu.h"
#include "UI/ElysiumSignScreen.h"
#include "UI/ElysiumUIRoot.h"
#include "UI/ElysiumUIStyle.h"

#include "CommonActivatableWidget.h"
#include "CommonInputSettings.h"
#include "ICommonInputModule.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/SWidget.h"

namespace
{
	int32 CountSlateWidgetsOfType(const TSharedRef<SWidget>& Widget, FName WidgetType)
	{
		int32 Count = Widget->GetType() == WidgetType ? 1 : 0;
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			Count += CountSlateWidgetsOfType(Children->GetChildAt(Index), WidgetType);
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHUDModelProjectionTest,
	"Elysium.Substrate.UI.HUDModelProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumHUDModelProjectionTest::RunTest(const FString& Parameters)
{
	UElysiumHUDModel* Model = NewObject<UElysiumHUDModel>();
	FElysiumViewState View;
	Model->Apply(View);
	TestFalse(TEXT("default view suppresses HUD"), Model->bVisible);
	TestFalse(TEXT("default vitals are invalid"), Model->bVitalsValid);
	TestFalse(TEXT("unwired equipment remains invalid"), Model->Equipment.bValid);
	TestFalse(TEXT("unwired selector remains closed"), Model->Selector.IsOpen());

	View.bPlayerSurface = true;
	View.Vitals.bValid = true;
	View.Vitals.Health = 73;
	View.Vitals.MaxHealth = 110;
	View.Vitals.BloodPool = 7;
	View.Vitals.MaxBloodPool = 12;
	View.Vitals.Humanity = 6;
	View.Vitals.Masquerade = 2;
	View.Interaction.Icon = 0;
	Model->Apply(View);

	TestTrue(TEXT("player surface displays HUD"), Model->bVisible);
	TestEqual(TEXT("health projects exactly"), Model->Health, 73);
	TestEqual(TEXT("maximum health projects exactly"), Model->MaxHealth, 110);
	TestEqual(TEXT("blood projects exactly"), Model->BloodPool, 7);
	TestEqual(TEXT("blood capacity projects exactly"), Model->BloodCapacity, 12);
	TestEqual(TEXT("ordinary aim resolves to cross"), Model->Reticle, EElysiumHUDReticle::Cross);
	TestFalse(TEXT("production projection never invents equipment"), Model->Equipment.bValid);

	View.Interaction.bVisible = true;
	View.Interaction.bActionable = true;
	View.Interaction.Icon = 4;
	View.Interaction.PromptAlpha = 0.75f;
	Model->Apply(View);
	TestEqual(TEXT("published use icon resolves to context cursor"),
		Model->Reticle, EElysiumHUDReticle::UseIcon);
	TestEqual(TEXT("prompt fade alpha projects exactly"), Model->UsePromptAlpha, 0.75f);
	TestTrue(TEXT("actionable focus projects exactly"), Model->bUseActionable);
	TestEqual(TEXT("interaction publishes the semantic Use action"),
		Model->UseAction, FName(TEXT("Use")));
	TestEqual(TEXT("keyboard use binding text"),
		ElysiumInteraction::UseBindingText(false).ToString(), FString(TEXT("E")));
	TestEqual(TEXT("gamepad use binding text"),
		ElysiumInteraction::UseBindingText(true).ToString(), FString(TEXT("RB")));

	View.bSignHidesHUD = true;
	Model->Apply(View);
	TestFalse(TEXT("HideHUD suppresses the complete heads-up model"), Model->bVisible);
	TestEqual(TEXT("HideHUD suppresses the cursor through the shared rule"),
		Model->Reticle, EElysiumHUDReticle::None);

	View.bSignHidesHUD = false;
	View.bCinematic = true;
	Model->Apply(View);
	TestFalse(TEXT("scripted camera suppresses the heads-up layer"), Model->bVisible);
	TestEqual(TEXT("scripted camera suppresses the cursor through the shared rule"),
		Model->Reticle, EElysiumHUDReticle::None);
	TestEqual(TEXT("cinematic suppression retains live health for an immediate return"),
		Model->Health, 73);

	Model->Apply(FElysiumViewState(), EElysiumHUDPreview::Weapon);
	TestTrue(TEXT("preview can exercise the HUD without gameplay owners"), Model->bVisible);
	TestTrue(TEXT("weapon preview supplies representative equipment"), Model->Equipment.bValid);
	TestEqual(TEXT("weapon preview opens the selector"),
		Model->Selector.Type, EElysiumHUDSelector::Weapons);
	TestTrue(TEXT("weapon preview has a selected entry"),
		Model->Selector.Entries.IsValidIndex(Model->Selector.SelectedIndex));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUIRootPushTest,
	"Elysium.Substrate.UI.CompositionRootPush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumUIRootPushTest::RunTest(const FString& Parameters)
{
	UElysiumUIRoot* Root = NewObject<UElysiumUIRoot>();
	// Keep the retained root alive for the whole test. A temporary TakeWidget() reference tears the
	// Slate containers back down before AddWidget(), which can only prove UObject registration.
	const TSharedRef<SWidget> RootSlate = Root->TakeWidget();
	UOverlay* Overlay = Cast<UOverlay>(Root->GetWidgetFromName(TEXT("RootOverlay")));
	TestNotNull(TEXT("root exposes its structural overlay"), Overlay);
	if (Overlay)
	{
		TestEqual(TEXT("root has passive HUD plus five semantic layers"),
			Overlay->GetChildrenCount(), 6);
		for (int32 Index = 0; Index < Overlay->GetChildrenCount(); ++Index)
		{
			UWidget* Child = Overlay->GetChildAt(Index);
			UOverlaySlot* Slot = Child ? Cast<UOverlaySlot>(Child->Slot) : nullptr;
			TestNotNull(*FString::Printf(TEXT("root child %d has an overlay slot"), Index), Slot);
			if (Slot)
			{
				TestEqual(*FString::Printf(TEXT("root child %d fills horizontally"), Index),
					Slot->GetHorizontalAlignment(), HAlign_Fill);
				TestEqual(*FString::Printf(TEXT("root child %d fills vertically"), Index),
					Slot->GetVerticalAlignment(), VAlign_Fill);
			}
			if (UCommonActivatableWidgetContainerBase* Layer =
				Cast<UCommonActivatableWidgetContainerBase>(Child))
			{
				TestEqual(*FString::Printf(TEXT("layer %d has explicit instant transition"), Index),
					Layer->GetTransitionDuration(), 0.0f);
			}
		}
	}

	UCommonActivatableWidget* Screen = Root->PushWidget(
		EElysiumUILayer::SystemModal,
		UCommonActivatableWidget::StaticClass(),
		[](UCommonActivatableWidget&) {});

	TestNotNull(TEXT("a CommonUI screen can be created through the root layer"), Screen);
	UCommonActivatableWidgetContainerBase* SystemModalLayer = nullptr;
	if (Overlay)
	{
		for (int32 Index = 0; Index < Overlay->GetChildrenCount(); ++Index)
		{
			UWidget* Child = Overlay->GetChildAt(Index);
			if (Child && Child->GetFName() == TEXT("SystemModalStack"))
			{
				SystemModalLayer = Cast<UCommonActivatableWidgetContainerBase>(Child);
				break;
			}
		}
	}
	TestNotNull(TEXT("the system-modal layer exists"), SystemModalLayer);
	TestTrue(TEXT("the pushed screen is registered with the requested layer"),
		SystemModalLayer && SystemModalLayer->GetWidgetList().Contains(Screen));
	TestTrue(TEXT("the requested screen is the active CommonUI leaf"),
		SystemModalLayer && SystemModalLayer->GetActiveWidget() == Screen);
	TestTrue(TEXT("the active CommonUI leaf completed activation"), Screen && Screen->IsActivated());

	UCommonActivatableWidget* Over = Root->PushWidget(
		EElysiumUILayer::SystemModal,
		UCommonActivatableWidget::StaticClass(),
		[](UCommonActivatableWidget&) {});
	TestTrue(TEXT("a later same-layer modal becomes the active leaf"),
		SystemModalLayer && SystemModalLayer->GetActiveWidget() == Over);
	Root->RemoveWidget(EElysiumUILayer::SystemModal, Over);
	TestTrue(TEXT("closing the top modal restores the underlying leaf"),
		SystemModalLayer && SystemModalLayer->GetActiveWidget() == Screen);
	(void)RootSlate;
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumActivatableRebuildNotificationTest,
	"Elysium.Substrate.UI.ActivatableRebuildNotification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumActivatableRebuildNotificationTest::RunTest(const FString& Parameters)
{
	ICommonInputModule::GetSettings().LoadData();
	UElysiumMainMenu* Menu = NewObject<UElysiumMainMenu>();
	Menu->SetMenuMode(EElysiumMenuMode::Pause);
	int32 RebuildNotifications = 0;
	const FDelegateHandle Handle = UCommonActivatableWidget::OnRebuilding.AddLambda(
		[Menu, &RebuildNotifications](UCommonActivatableWidget& Widget)
		{
			if (&Widget == Menu)
			{
				++RebuildNotifications;
			}
		});

	Menu->TakeWidget();
	UCommonActivatableWidget::OnRebuilding.Remove(Handle);

	TestEqual(TEXT("custom Slate screen announces its rebuild to the CommonUI action router"),
		RebuildNotifications, 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUINavigationStateTest,
	"Elysium.Substrate.UI.NavigationState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumUINavigationStateTest::RunTest(const FString& Parameters)
{
	ICommonInputModule::GetSettings().LoadData();

	// Menu rows are real CommonUI targets, including unavailable rows whose captions explain why.
	UElysiumMainMenu* Menu = NewObject<UElysiumMainMenu>();
	Menu->SetMenuMode(EElysiumMenuMode::Pause);
	const TSharedRef<SWidget> MenuSlate = Menu->TakeWidget();
	TestEqual(TEXT("pause menu selects its first primary action"), Menu->GetSelectedActionId(),
		FName(TEXT("VMainMenu_BTN_CONTINUE")));
	UElysiumActionButton* Desired = Cast<UElysiumActionButton>(Menu->GetDesiredFocusTarget());
	TestNotNull(TEXT("desired focus is a semantic action rather than the screen wrapper"), Desired);
	TestTrue(TEXT("desired focus is the selected action"), Desired == Menu->GetSelectedAction());
	TestTrue(TEXT("menu action retains its visible Slate label inside the CommonButton"),
		Desired && Desired->HasSlateContent());
	TestTrue(TEXT("vertical menu wraps to its last action"),
		Menu->Navigate(EElysiumNavigationDirection::Up));
	TestEqual(TEXT("wrapped menu action is stable by id"), Menu->GetSelectedActionId(),
		FName(TEXT("VMainMenu_BTN_MAINMENU")));
	TestTrue(TEXT("vertical menu wraps back to the first action"),
		Menu->Navigate(EElysiumNavigationDirection::Down));
	TestEqual(TEXT("menu wrap returns to Continue"), Menu->GetSelectedActionId(),
		FName(TEXT("VMainMenu_BTN_CONTINUE")));

	UElysiumActionButton* Disabled = Menu->FindAction(TEXT("VMainMenu_BTN_LOADGAME"));
	TestNotNull(TEXT("disabled menu item remains an action target"), Disabled);
	if (Disabled)
	{
		TestTrue(TEXT("disabled explanatory item remains focusable"), Disabled->GetIsFocusable());
		TestFalse(TEXT("disabled explanatory item cannot execute"), Disabled->IsExecutable());
		TestTrue(TEXT("disabled explanatory item can be selected"),
			Menu->SelectAction(Disabled->GetActionId(), false));
		TestFalse(TEXT("disabled explanatory activation is a no-op"),
			Menu->ExecuteSelectedAction());
		TestFalse(TEXT("disabled item carries an explanatory caption"),
			Disabled->GetActionCaption().IsEmpty());
	}

	// Dialogue retains the durable response identity across a turn rebuild. If that response is
	// removed, it repairs to the nearest surviving visible row rather than a dead widget address.
	UElysiumDialogueScreen* Dialogue = NewObject<UElysiumDialogueScreen>();
	FElysiumDialogueView Turn;
	Turn.Choices = { TEXT("First"), TEXT("Second"), TEXT("Third") };
	Turn.ChoiceIds = { 101, 202, 303 };
	Dialogue->ApplyDialogue(Turn);
	int32 ChoiceCount = 0;
	int32 LastChoice = INDEX_NONE;
	Dialogue->OnChoice.BindLambda([&ChoiceCount, &LastChoice](int32 Choice)
	{
		++ChoiceCount;
		LastChoice = Choice;
	});
	const TSharedRef<SWidget> DialogueSlate = Dialogue->TakeWidget();
	TestEqual(TEXT("dialogue defaults to its first stable response"),
		Dialogue->GetSelectedActionId(), FName(TEXT("Dialogue.Choice.101")));
	UElysiumActionButton* First = Dialogue->GetSelectedAction();
	TestTrue(TEXT("dialogue moves to the next response"),
		Dialogue->Navigate(EElysiumNavigationDirection::Down));
	TestEqual(TEXT("dialogue selection names the response identity"),
		Dialogue->GetSelectedActionId(), FName(TEXT("Dialogue.Choice.202")));

	UElysiumActionButton* Second = Dialogue->GetSelectedAction();
	TestNotNull(TEXT("selected dialogue response is a CommonUI action"), Second);
	TestFalse(TEXT("the previous dialogue response loses its selected visual state"),
		First && First->IsActionSelected());
	TestTrue(TEXT("the focused dialogue response gains the selected visual state"),
		Second && Second->IsActionSelected());
	TestTrue(TEXT("dialogue action retains its visible response content inside the CommonButton"),
		Second && Second->HasSlateContent());
	if (Second)
	{
		Second->OnActionActivated().Broadcast(*Second);
		Second->OnActionActivated().Broadcast(*Second);
	}
	TestEqual(TEXT("CommonUI activation reaches the semantic choice exactly once"), ChoiceCount, 1);
	TestEqual(TEXT("semantic callback uses visible response order"), LastChoice, 1);

	Turn.Revision = 2;
	Dialogue->ApplyDialogue(Turn);
	TestEqual(TEXT("same response identity survives dialogue refresh"),
		Dialogue->GetSelectedActionId(), FName(TEXT("Dialogue.Choice.202")));
	Dialogue->SelectAction(TEXT("Dialogue.Choice.303"), false);
	Turn.Revision = 3;
	Turn.Choices = { TEXT("First"), TEXT("Second") };
	Turn.ChoiceIds = { 101, 202 };
	Dialogue->ApplyDialogue(Turn);
	TestEqual(TEXT("contracted dialogue repairs to nearest surviving row"),
		Dialogue->GetSelectedActionId(), FName(TEXT("Dialogue.Choice.202")));
	Turn.Revision = 4;
	Turn.Choices.Reset();
	Turn.ChoiceIds.Reset();
	Turn.bTerminal = true;
	Dialogue->ApplyDialogue(Turn);
	TestEqual(TEXT("terminal dialogue exposes one Continue action"),
		Dialogue->GetSelectedActionId(), FName(TEXT("Dialogue.Continue")));

	// Chargen uses the same stable-selection and contraction rules, with a vertical wrap.
	FElysiumWizPopup Popup;
	Popup.InternalName = TEXT("TestPopup");
	Popup.Text = TEXT("Choose");
	Popup.Actions.SetNum(3);
	Popup.Actions[0].Text = TEXT("One");
	Popup.Actions[1].Text = TEXT("Two");
	Popup.Actions[2].Text = TEXT("Three");
	TSharedPtr<FElysiumWizRun> Run = MakeShared<FElysiumWizRun>();
	Run->Popup = &Popup;
	for (const FElysiumWizAction& Action : Popup.Actions)
	{
		Run->Choices.Add(&Action);
	}
	UElysiumChargenPopup* Chargen = NewObject<UElysiumChargenPopup>();
	Chargen->SetRun(Run);
	const TSharedRef<SWidget> ChargenSlate = Chargen->TakeWidget();
	TestEqual(TEXT("chargen defaults to its first answer"), Chargen->GetSelectedActionId(),
		FName(TEXT("Chargen.TestPopup.0")));
	Chargen->Navigate(EElysiumNavigationDirection::Up);
	TestEqual(TEXT("chargen answers wrap upward"), Chargen->GetSelectedActionId(),
		FName(TEXT("Chargen.TestPopup.2")));
	Run->Choices.SetNum(2);
	Chargen->Refresh();
	TestEqual(TEXT("contracted chargen choices repair to nearest answer"),
		Chargen->GetSelectedActionId(), FName(TEXT("Chargen.TestPopup.1")));

	// Character/chargen rows use explicit region routing: vertical enters/leaves the body, while
	// horizontal selection executes the focused base option through the same semantic action.
	UElysiumCharacterScreen* Character = NewObject<UElysiumCharacterScreen>();
	FElysiumCharacterScreenMode CharacterMode;
	CharacterMode.Tabs = { EElysiumCharacterTab::Base, EElysiumCharacterTab::Sheet };
	CharacterMode.Spend = EElysiumSpendMode::Chargen;
	Character->SetMode(CharacterMode);
	TSharedPtr<FElysiumChargenState> CharacterState = MakeShared<FElysiumChargenState>();
	CharacterState->Clan = 2;
	Character->SetSpendState(CharacterState);
	Character->SetActiveTab(EElysiumCharacterTab::Base);
	int32 CharacterCancelCount = 0;
	Character->OnCancel.BindLambda([&CharacterCancelCount]() { ++CharacterCancelCount; });
	const TSharedRef<SWidget> CharacterSlate = Character->TakeWidget();
	TestEqual(TEXT("character screen defaults to its active major tab"),
		Character->GetSelectedActionId(),
		FName(*FString::Printf(TEXT("Character.Tab.%d"), int32(EElysiumCharacterTab::Base))));
	Character->Navigate(EElysiumNavigationDirection::Down);
	TestEqual(TEXT("Down enters the first base-option row"), Character->GetSelectedActionId(),
		FName(TEXT("Character.Base.Clan.0")));
	Character->Navigate(EElysiumNavigationDirection::Right);
	TestEqual(TEXT("Right cycles and selects the next base option"),
		Character->GetSelectedActionId(), FName(TEXT("Character.Base.Clan.1")));
	TestEqual(TEXT("base-option routing executes the same clan action"), CharacterState->Clan, 3);
	Character->Navigate(EElysiumNavigationDirection::Up);
	Character->Navigate(EElysiumNavigationDirection::Up);
	TestEqual(TEXT("vertical region routing wraps from tabs to the footer cancel action"),
		Character->GetSelectedActionId(), FName(TEXT("Character.Footer.Cancel")));
	TestTrue(TEXT("footer cancel is an ordinary executable semantic action"),
		Character->ExecuteSelectedAction());
	TestEqual(TEXT("footer cancel and Back share one callback"), CharacterCancelCount, 1);

	// A sign is one centered text panel and an ordinary visible Continue action. Legacy background
	// metadata is deliberately ignored; dwell/CloseOnLeftClick still controls executability.
	FElysiumSignData Sign;
	Sign.bParsed = true;
	Sign.SourceFile = TEXT("automation-sign");
	Sign.Background.bValid = true;
	Sign.Background.ImageName = TEXT("interface/pop_ups/automation_missing");
	Sign.Background.bHasWide = true;
	Sign.Background.bHasTall = true;
	Sign.Background.Wide = 2048;
	Sign.Background.Tall = 1024;
	Sign.Background.bCentre = true;
	FElysiumSignTextBlock BodyBlock;
	BodyBlock.Text = TEXT("Authored popup body");
	BodyBlock.XPos = 836;
	BodyBlock.YPos = 310;
	BodyBlock.Wide = 375;
	BodyBlock.Tall = 375;
	BodyBlock.TextColor = FLinearColor::White;
	Sign.Blocks.Add(BodyBlock);
	FElysiumSignTextBlock ContinueBlock;
	ContinueBlock.Text = TEXT("left-click to continue");
	ContinueBlock.XPos = 922;
	ContinueBlock.YPos = 706;
	ContinueBlock.Wide = 215;
	ContinueBlock.Tall = 28;
	ContinueBlock.TextColor = FLinearColor::White;
	Sign.Blocks.Add(ContinueBlock);
	UElysiumSignScreen* SignScreen = NewObject<UElysiumSignScreen>();
	SignScreen->ApplySign(Sign, 1.0f, false);
	int32 DismissCount = 0;
	SignScreen->OnDismiss.BindLambda([&DismissCount]() { ++DismissCount; });
	const TSharedRef<SWidget> SignSlate = SignScreen->TakeWidget();
	UElysiumActionButton* SignAction = SignScreen->FindAction(TEXT("Sign.Dismiss"));
	TestNotNull(TEXT("sign panel is one CommonUI action"), SignAction);
	TestTrue(TEXT("sign action retains its visible Continue presentation"),
		SignAction && SignAction->HasSlateContent());
	SignSlate->SlatePrepass(1.0f);
	TestTrue(TEXT("sign panel has a stable readable minimum presentation size"),
		SignSlate->GetDesiredSize().X > 600.0f && SignSlate->GetDesiredSize().Y > 100.0f);
	TestEqual(TEXT("sign renders one body and one replacement Continue label"),
		CountSlateWidgetsOfType(SignSlate, FName(TEXT("STextBlock"))), 2);
	TestEqual(TEXT("Continue is rendered inside the real CommonButton subtree"),
		SignAction ? CountSlateWidgetsOfType(SignAction->TakeWidget(), FName(TEXT("STextBlock"))) : -1,
		1);
	TestEqual(TEXT("sign action exposes the same semantic Continue label"),
		SignAction ? SignAction->GetActionLabel().ToString() : FString(), FString(TEXT("Continue")));
	TestTrue(TEXT("sign action is the desired focus target"),
		SignAction && SignScreen->GetDesiredFocusTarget() == SignAction);
	TestFalse(TEXT("sign cannot execute before the world authorizes dismissal"),
		SignScreen->ExecuteSelectedAction());
	TestEqual(TEXT("blocked sign input is consumed without dismissal"), DismissCount, 0);
	SignScreen->ApplySign(Sign, 1.0f, true);
	TestTrue(TEXT("authorized sign action dismisses"), SignScreen->ExecuteSelectedAction());
	TestFalse(TEXT("transition latch suppresses a repeated sign activation"),
		SignScreen->ExecuteSelectedAction());
	TestEqual(TEXT("sign dismissal reaches its semantic request exactly once"), DismissCount, 1);
	(void)MenuSlate;
	(void)DialogueSlate;
	(void)ChargenSlate;
	(void)CharacterSlate;
	(void)SignSlate;

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUIScalingAndDialogueInputTest,
	"Elysium.Substrate.UI.ScalingAndDialogueInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumUIScalingAndDialogueInputTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Full HD uses the shared 768-high virtual canvas"),
		ElysiumUI::ScaleFor(1080.0f), 1080.0f / ElysiumUI::VirtualH);
	TestEqual(TEXT("1440p uses the shared 768-high virtual canvas"),
		ElysiumUI::ScaleFor(1440.0f), 1440.0f / ElysiumUI::VirtualH);
	TestEqual(TEXT("4K uses the shared 768-high virtual canvas"),
		ElysiumUI::ScaleFor(2160.0f), 2160.0f / ElysiumUI::VirtualH);
	TestEqual(TEXT("4K HUD metrics are exactly twice Full HD metrics"),
		ElysiumUI::ScaleFor(2160.0f) / ElysiumUI::ScaleFor(1080.0f), 2.0f);

	// The response band is authored from the retail framing, then kept resolution-independent by
	// the shared virtual-canvas scale. All supported acceptance resolutions are 16:9, so both
	// normalized dimensions must remain stable across the whole range.
	for (const FVector2D Viewport : { FVector2D(1920.0, 1080.0), FVector2D(2560.0, 1440.0),
		FVector2D(3840.0, 2160.0) })
	{
		const float Scale = ElysiumUI::ScaleFor(static_cast<float>(Viewport.Y));
		constexpr float SlateRenderDPI = 96.0f;
		constexpr float PointsPerInch = 72.0f;
		TestTrue(*FString::Printf(TEXT("dialogue choice type follows the virtual canvas at %.0fx%.0f"),
			Viewport.X, Viewport.Y),
			FMath::IsNearlyEqual(
				ElysiumDialogueUI::ChoiceFontPoints * (SlateRenderDPI / PointsPerInch) * Scale,
				ElysiumDialogueUI::ChoiceFontVirtualPixels * Scale));
		TestTrue(*FString::Printf(TEXT("dialogue line type follows the virtual canvas at %.0fx%.0f"),
			Viewport.X, Viewport.Y),
			FMath::IsNearlyEqual(
				ElysiumDialogueUI::LineFontPoints * (SlateRenderDPI / PointsPerInch) * Scale,
				ElysiumDialogueUI::LineFontVirtualPixels * Scale));
		TestTrue(*FString::Printf(TEXT("dialogue speaker type follows the virtual canvas at %.0fx%.0f"),
			Viewport.X, Viewport.Y),
			FMath::IsNearlyEqual(
				ElysiumDialogueUI::SpeakerFontPoints * (SlateRenderDPI / PointsPerInch) * Scale,
				ElysiumDialogueUI::SpeakerFontVirtualPixels * Scale));
		TestEqual(*FString::Printf(TEXT("dialogue width is 63.3%% at %.0fx%.0f"),
			Viewport.X, Viewport.Y),
			ElysiumDialogueUI::ResponsePanelWidth * Scale / static_cast<float>(Viewport.X),
			0.6328125f);
		TestEqual(*FString::Printf(TEXT("dialogue lower inset is 3.125%% at %.0fx%.0f"),
			Viewport.X, Viewport.Y),
			ElysiumDialogueUI::ResponsePanelBottomInset * Scale /
				static_cast<float>(Viewport.Y),
			0.03125f);
	}

	const TOptional<int32> Second = ElysiumDialogueUI::ChoiceForKey(EKeys::Two, 3, false);
	TestTrue(TEXT("dialogue number key resolves"), Second.IsSet());
	if (Second)
	{
		TestEqual(TEXT("dialogue number key uses visible choice order"), Second.GetValue(), 1);
	}
	TestFalse(TEXT("dialogue rejects a choice outside the visible range"),
		ElysiumDialogueUI::ChoiceForKey(EKeys::Four, 3, false).IsSet());
	const TOptional<int32> Continue = ElysiumDialogueUI::ChoiceForKey(EKeys::Enter, 0, true);
	TestTrue(TEXT("terminal dialogue accepts Enter"), Continue.IsSet());
	if (Continue)
	{
		TestEqual(TEXT("terminal dialogue reports advance"), Continue.GetValue(), -1);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUICompositionPolicyTest,
	"Elysium.Substrate.UI.CompositionPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumUICompositionPolicyTest::RunTest(const FString& Parameters)
{
	UCommonInputSettings* CommonInput = GetMutableDefault<UCommonInputSettings>();
	CommonInput->LoadData();
	const FDataTableRowHandle Click = CommonInput->GetDefaultClickAction();
	const FDataTableRowHandle Back = CommonInput->GetDefaultBackAction();
	const FDataTableRowHandle Use = GetDefault<UElysiumCommonUIInputData>()->GetUseAction();
	TestFalse(TEXT("CommonUI never installs a competing default input mode"),
		CommonInput->GetEnableDefaultInputConfig());
	TestNotNull(TEXT("native Accept action table resolves"), Click.DataTable.Get());
	TestEqual(TEXT("native Accept action row resolves"), Click.RowName, FName(TEXT("Accept")));
	TestNotNull(TEXT("native Back action table resolves"), Back.DataTable.Get());
	TestEqual(TEXT("native Back action row resolves"), Back.RowName, FName(TEXT("Back")));
	TestNotNull(TEXT("native Use action table resolves"), Use.DataTable.Get());
	TestEqual(TEXT("native Use action row resolves"), Use.RowName, FName(TEXT("Use")));
	const FElysiumCommonInputActionData* UseData =
		Use.GetRow<FElysiumCommonInputActionData>(TEXT("HUD test"));
	TestTrue(TEXT("native Use action binds keyboard E"),
		UseData && UseData->IsKeyBoundToInputActionData(EKeys::E));
	TestTrue(TEXT("native Use action binds gamepad RB"),
		UseData && UseData->IsKeyBoundToInputActionData(EKeys::Gamepad_RightShoulder));

	const FString SourceRoot = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir() / TEXT("Source/ElysiumUE"));
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *SourceRoot, TEXT("*.cpp"), true, false);

	struct FRule
	{
		const TCHAR* Needle;
		const TCHAR* AllowedFile;
	};
	const FRule Rules[] =
	{
		{ TEXT("AddToViewport("), nullptr },
		{ TEXT("AddViewportWidgetContent("), nullptr },
		{ TEXT("AddToPlayerScreen("), TEXT("ElysiumPlayerUISubsystem.cpp") },
		{ TEXT("ActivateWidget("), nullptr },
		{ TEXT("DeactivateWidget("), nullptr },
		{ TEXT("SetInputMode("), TEXT("ElysiumInputSubsystem.cpp") },
	};

	for (const FString& File : Files)
	{
		if (File.EndsWith(TEXT("ElysiumHUDTests.cpp")))
		{
			continue;
		}
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *File))
		{
			AddError(FString::Printf(TEXT("Could not inspect UI composition policy in %s"), *File));
			continue;
		}
		for (const FRule& Rule : Rules)
		{
			if (Contents.Contains(Rule.Needle) &&
				(!Rule.AllowedFile || !File.EndsWith(Rule.AllowedFile)))
			{
				AddError(FString::Printf(TEXT("%s uses forbidden composition call %s"),
					*File, Rule.Needle));
			}
		}
	}

	return !HasAnyErrors();
}

#endif
