#include "UI/ElysiumTerminalScreen.h"

#include "UI/ElysiumActionButton.h"

#include "Blueprint/WidgetTree.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumTerminalUI, Log, All);

namespace
{
	const FName TerminalActions(TEXT("Terminal.Actions"));
	const FLinearColor Phosphor(0.63f, 0.88f, 0.70f, 1.0f);
}

UElysiumTerminalScreen::UElysiumTerminalScreen()
{
	bIsBackHandler = true;
}

void UElysiumTerminalScreen::ApplyTerminal(const FElysiumTerminalView& InTerminal)
{
	const bool bNewSession = Terminal.Owner != InTerminal.Owner
		|| Terminal.SessionSerial != InTerminal.SessionSerial;
	if (!bNewSession && InTerminal.Revision < Terminal.Revision)
	{
		return;
	}
	const bool bInputModeChanged = Terminal.InputMode != InTerminal.InputMode;
	const bool bChanged = bNewSession || Terminal.Revision != InTerminal.Revision;
	Terminal = InTerminal;
	if (bNewSession || bInputModeChanged)
	{
		SetDraftText(FString());
	}
	if (bChanged && GetCachedWidget().IsValid())
	{
		RebuildActions();
	}
}

void UElysiumTerminalScreen::SetDraftText(const FString& Text)
{
	if (!CommandEntry)
	{
		return;
	}
	const int32 Limit = FMath::Max(0, Terminal.MaxInput);
	const FString Clamped = Text.Left(Limit);
	bUpdatingDraft = true;
	CommandEntry->SetText(FText::FromString(Clamped));
	bUpdatingDraft = false;
}

FString UElysiumTerminalScreen::GetDraftText() const
{
	return CommandEntry ? CommandEntry->GetText().ToString() : FString();
}

bool UElysiumTerminalScreen::SubmitDraft()
{
	const FString Command = Terminal.InputMode == 2 ? FString() : GetDraftText();
	return SubmitCommand(Command);
}

void UElysiumTerminalScreen::HandleDraftChanged(const FText& Text)
{
	if (bUpdatingDraft)
	{
		return;
	}
	const FString Changed = Text.ToString();
	const int32 Limit = FMath::Max(0, Terminal.MaxInput);
	if (Changed.Len() > Limit)
	{
		SetDraftText(Changed.Left(Limit));
	}
}

void UElysiumTerminalScreen::HandleDraftCommitted(const FText&, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		SubmitDraft();
	}
}

bool UElysiumTerminalScreen::SubmitCommand(const FString& Command)
{
	if (!Terminal.IsOpen())
	{
		return false;
	}
	if (!OnCommand.IsBound())
	{
		UE_LOG(LogElysiumTerminalUI, Warning,
			TEXT("terminal UI cannot submit '%s': no command route is bound for %s serial %u"),
			*Command, *Terminal.Owner.ToString(), Terminal.SessionSerial);
		return false;
	}
	const bool bAccepted = OnCommand.Execute(Terminal.Owner, Terminal.SessionSerial, Command);
	if (bAccepted)
	{
		SetDraftText(FString());
	}
	return bAccepted;
}

void UElysiumTerminalScreen::ConfigureEditor()
{
	if (!CommandEntry)
	{
		return;
	}
	FEditableTextStyle Style = FCoreStyle::Get().GetWidgetStyle<FEditableTextStyle>(
		TEXT("NormalEditableText"));
	Style.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 18));
	Style.SetColorAndOpacity(FSlateColor(Phosphor));
	CommandEntry->SetWidgetStyle(Style);
	CommandEntry->SetMinimumDesiredWidth(1.0f);
	CommandEntry->SetClearKeyboardFocusOnCommit(false);
	CommandEntry->SetSelectAllTextOnCommit(false);
	CommandEntry->SetRevertTextOnEscape(false);
	CommandEntry->SetIsPassword(Terminal.InputMode == 1);
	CommandEntry->SetIsReadOnly(Terminal.InputMode == 2);
	CommandEntry->SetHintText(Terminal.InputMode == 1
		? NSLOCTEXT("Elysium", "TerminalPasswordHint", "Password")
		: Terminal.InputMode == 2
			? NSLOCTEXT("Elysium", "TerminalAcknowledgeHint", "Press Enter")
			: NSLOCTEXT("Elysium", "TerminalCommandHint", "Type menu or command"));
}

TSharedRef<SWidget> UElysiumTerminalScreen::BuildActionVisual(
	UElysiumActionButton& Action, const FText& Label)
{
	const TWeakObjectPtr<UElysiumActionButton> WeakAction(&Action);
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
		.BorderBackgroundColor_Lambda([WeakAction]()
		{
			const UElysiumActionButton* Button = WeakAction.Get();
			if (!Button || !Button->IsExecutable())
			{
				return FSlateColor(FLinearColor(0.035f, 0.055f, 0.045f, 0.72f));
			}
			return FSlateColor(Button->IsActionSelected()
				? FLinearColor(0.16f, 0.34f, 0.22f, 0.96f)
				: FLinearColor(0.025f, 0.10f, 0.055f, 0.86f));
		})
		.Padding(FMargin(12.0f, 6.0f))
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 13))
			.ColorAndOpacity(FSlateColor(Phosphor))
		];
}

void UElysiumTerminalScreen::BuildTerminalActions()
{
	for (const FElysiumTerminalActionView& ActionView : Terminal.Actions)
	{
		const FName ActionId(*ActionView.Id);
		if (ActionId.IsNone())
		{
			UE_LOG(LogElysiumTerminalUI, Warning,
				TEXT("terminal UI ignored an action with an empty id for %s serial %u"),
				*Terminal.Owner.ToString(), Terminal.SessionSerial);
			continue;
		}
		const FText Label = FText::FromString(ActionView.Label);
		UElysiumActionButton* Action = CreateActionButton(
			ActionId, TerminalActions, Label, ActionView.bEnabled,
			[this, Command = ActionView.Command]() { SubmitCommand(Command); }, {},
			FText::FromString(ActionView.Explanation));
		if (!Action)
		{
			UE_LOG(LogElysiumTerminalUI, Warning,
				TEXT("terminal UI failed to create action '%s' for %s serial %u"),
				*ActionView.Id, *Terminal.Owner.ToString(), Terminal.SessionSerial);
			continue;
		}
		if (DefaultActionId.IsNone())
		{
			DefaultActionId = ActionId;
		}
		Action->SetSlateContent(BuildActionVisual(*Action, Label));
	}
}

void UElysiumTerminalScreen::RebuildActions()
{
	BeginNavigationBuild();
	SetNavigationGroup(TerminalActions, true, true, true, true);
	DefaultActionId = NAME_None;
	ConfigureEditor();
	BuildTerminalActions();
	FinalizeNavigationBuild(DefaultActionId);
	if (IsActivated() && CommandEntry && GetOwningPlayer())
	{
		CommandEntry->SetUserFocus(GetOwningPlayer());
	}
}

TSharedRef<SWidget> UElysiumTerminalScreen::RebuildWidget()
{
	(void)Super::RebuildWidget();
	if (!WidgetTree)
	{
		UE_LOG(LogElysiumTerminalUI, Warning,
			TEXT("terminal UI rebuild has no WidgetTree for %s serial %u"),
			*Terminal.Owner.ToString(), Terminal.SessionSerial);
	}
	else if (!CommandEntry)
	{
		CommandEntry = WidgetTree->ConstructWidget<UEditableText>(
			UEditableText::StaticClass(), TEXT("TerminalCommandEntry"));
		if (CommandEntry)
		{
			CommandEntry->OnTextChanged.AddDynamic(this,
				&UElysiumTerminalScreen::HandleDraftChanged);
			CommandEntry->OnTextCommitted.AddDynamic(this,
				&UElysiumTerminalScreen::HandleDraftCommitted);
		}
		else
		{
			UE_LOG(LogElysiumTerminalUI, Warning,
				TEXT("terminal UI failed to create its command editor for %s serial %u"),
				*Terminal.Owner.ToString(), Terminal.SessionSerial);
		}
	}

	RebuildActions();
	// This one-pixel transparent input shell is the only viewport widget. The visible console is
	// rendered exclusively through the monitor material above.
	return SNew(SBox)
		.WidthOverride(1.0f)
		.HeightOverride(1.0f)
		.RenderOpacity(0.0f)
		[
			CommandEntry ? CommandEntry->TakeWidget() : SNullWidget::NullWidget
		];
}

UWidget* UElysiumTerminalScreen::NativeGetDesiredFocusTarget() const
{
	return CommandEntry ? CommandEntry : Super::NativeGetDesiredFocusTarget();
}

bool UElysiumTerminalScreen::HandleNavigation(EElysiumNavigationDirection Direction)
{
	const int32 Delta = Direction == EElysiumNavigationDirection::Up
		|| Direction == EElysiumNavigationDirection::Left ? -1 : 1;
	// Action visuals live in the render-target Slate tree, outside the viewport focus path. Keep
	// keyboard focus on the invisible editor while updating the durable semantic selection.
	return SelectAdjacentInGroup(TerminalActions, Delta, true, false);
}

void UElysiumTerminalScreen::HandleSelectedActionChanged(FName, FName)
{
	// The selection is semantic only: the glass carries the authority's grid, and retail draws no
	// action list on it at all.
}

FReply UElysiumTerminalScreen::NativeOnPreviewKeyDown(
	const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Escape)
	{
		if (!KeyEvent.IsRepeat())
		{
			SubmitCommand(TEXT("quit"));
		}
		return FReply::Handled();
	}
	if (KeyEvent.GetKey() == EKeys::C && KeyEvent.IsControlDown())
	{
		if (!KeyEvent.IsRepeat())
		{
			SubmitCommand(TEXT("break"));
		}
		return FReply::Handled();
	}
	if (KeyEvent.GetKey() == EKeys::Gamepad_FaceButton_Bottom)
	{
		if (!KeyEvent.IsRepeat())
		{
			if (!ExecuteSelectedAction())
			{
				SubmitDraft();
			}
		}
		return FReply::Handled();
	}

	const FKey Key = KeyEvent.GetKey();
	const bool bGamepadNavigation = Key == EKeys::Gamepad_DPad_Up
		|| Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_DPad_Left
		|| Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Up
		|| Key == EKeys::Gamepad_LeftStick_Down || Key == EKeys::Gamepad_LeftStick_Left
		|| Key == EKeys::Gamepad_LeftStick_Right;
	if (CommandEntry && CommandEntry->HasKeyboardFocus() && !bGamepadNavigation)
	{
		// Do not let the navigable-screen W/A/S/D aliases consume ordinary terminal text entry.
		return UElysiumActivatableScreen::NativeOnPreviewKeyDown(Geometry, KeyEvent);
	}
	return Super::NativeOnPreviewKeyDown(Geometry, KeyEvent);
}

bool UElysiumTerminalScreen::NativeOnHandleBackAction()
{
	SubmitCommand(TEXT("quit"));
	return true;
}

void UElysiumTerminalScreen::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	CommandEntry = nullptr;
}
