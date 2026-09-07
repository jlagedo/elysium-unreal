#include "UI/ElysiumTerminalScreen.h"

#include "UI/ElysiumActionButton.h"
#include "UI/ElysiumTerminalInputWidget.h"

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
	// Unity-blob safety: this file and ElysiumTerminalProjection.cpp both name the phosphor,
	// and an anonymous namespace does not separate two translation units the build merges.
	const FLinearColor ScreenPhosphor(0.63f, 0.88f, 0.70f, 1.0f);
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
	const bool bChanged = bNewSession || Terminal.Revision != InTerminal.Revision;
	Terminal = InTerminal;
	if (TerminalInput)
	{
		// The leaf owns the draft and its own three clears (new session, mode change, explicit).
		TerminalInput->SetSession(Terminal);
	}
	if (bChanged && GetCachedWidget().IsValid())
	{
		RebuildActions();
	}
}

FString UElysiumTerminalScreen::GetDraftText() const
{
	return TerminalInput ? TerminalInput->GetDraft() : FString();
}

bool UElysiumTerminalScreen::SubmitDraft()
{
	if (!Terminal.IsOpen())
	{
		return false;
	}
	if (Terminal.InputMode == 2)
	{
		// The bare `"hackcmd "`: acknowledge mode accepts an empty command and nothing else.
		if (!OnAcknowledge.IsBound())
		{
			return false;
		}
		return OnAcknowledge.Execute(Terminal.Owner, Terminal.SessionSerial);
	}
	const FString Draft = GetDraftText();
	const bool bAccepted = SubmitCommand(Draft);
	if (bAccepted && TerminalInput)
	{
		TerminalInput->ClearDraft();
	}
	return bAccepted;
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
	return OnCommand.Execute(Terminal.Owner, Terminal.SessionSerial, Command);
}

void UElysiumTerminalScreen::PublishDraft()
{
	OnDraft.ExecuteIfBound(Terminal.Owner, GetDraftText());
}

void UElysiumTerminalScreen::BindInputDelegates()
{
	if (!TerminalInput)
	{
		return;
	}
	TerminalInput->OnSubmitLine.BindWeakLambda(this, [this](const FString& Line)
	{
		return SubmitCommand(Line);
	});
	TerminalInput->OnSubmitCharacter.BindWeakLambda(this, [this](TCHAR Character)
	{
		return Terminal.IsOpen() && OnCharacter.IsBound()
			&& OnCharacter.Execute(Terminal.Owner, Terminal.SessionSerial, Character);
	});
	TerminalInput->OnAcknowledge.BindWeakLambda(this, [this]()
	{
		if (Terminal.IsOpen() && OnAcknowledge.IsBound())
		{
			OnAcknowledge.Execute(Terminal.Owner, Terminal.SessionSerial);
		}
	});
	TerminalInput->OnQuit.BindWeakLambda(this, [this]()
	{
		if (Terminal.IsOpen() && OnQuit.IsBound())
		{
			OnQuit.Execute(Terminal.Owner, Terminal.SessionSerial);
		}
	});
	TerminalInput->OnBreak.BindWeakLambda(this, [this]()
	{
		if (Terminal.IsOpen() && OnBreak.IsBound())
		{
			OnBreak.Execute(Terminal.Owner, Terminal.SessionSerial);
		}
	});
	TerminalInput->OnDraftChanged.BindWeakLambda(this, [this]() { PublishDraft(); });
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
			.ColorAndOpacity(FSlateColor(ScreenPhosphor))
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
	BuildTerminalActions();
	FinalizeNavigationBuild(DefaultActionId);
	if (IsActivated() && TerminalInput && GetOwningPlayer())
	{
		TerminalInput->SetUserFocus(GetOwningPlayer());
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
	else if (!TerminalInput)
	{
		TerminalInput = WidgetTree->ConstructWidget<UElysiumTerminalInputWidget>(
			UElysiumTerminalInputWidget::StaticClass(), TEXT("TerminalKeyboard"));
		if (TerminalInput)
		{
			BindInputDelegates();
			TerminalInput->SetSession(Terminal);
		}
		else
		{
			UE_LOG(LogElysiumTerminalUI, Warning,
				TEXT("terminal UI failed to create its keyboard for %s serial %u"),
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
			TerminalInput ? TerminalInput->TakeWidget() : SNullWidget::NullWidget
		];
}

UWidget* UElysiumTerminalScreen::NativeGetDesiredFocusTarget() const
{
	return TerminalInput ? TerminalInput : Super::NativeGetDesiredFocusTarget();
}

bool UElysiumTerminalScreen::HandleNavigation(EElysiumNavigationDirection Direction)
{
	const int32 Delta = Direction == EElysiumNavigationDirection::Up
		|| Direction == EElysiumNavigationDirection::Left ? -1 : 1;
	// Action visuals live in the render-target Slate tree, outside the viewport focus path. Keep
	// keyboard focus on the invisible keyboard while updating the durable semantic selection.
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
	// Escape and Ctrl+C are NOT intercepted here any more. They are terminal keys with a mode-
	// dependent meaning in retail's client — Escape sends `hackcmd quit` in line and raw mode but
	// the bare `"hackcmd "` in acknowledge mode (`0x100c7090`) — and only the focused leaf knows
	// which mode is live. Handling them at the screen made Escape mean `quit` in every mode.
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
	if (TerminalInput && TerminalInput->HasKeyboardFocus() && !bGamepadNavigation)
	{
		// Do not let the navigable-screen W/A/S/D aliases consume ordinary terminal text entry.
		return UElysiumActivatableScreen::NativeOnPreviewKeyDown(Geometry, KeyEvent);
	}
	return Super::NativeOnPreviewKeyDown(Geometry, KeyEvent);
}

bool UElysiumTerminalScreen::NativeOnHandleBackAction()
{
	// The platform back action is Escape's semantic twin, and it carries the same mode split.
	if (!Terminal.IsOpen())
	{
		return true;
	}
	FElysiumTerminalIntentDelegate& Intent = Terminal.InputMode == 2 ? OnAcknowledge : OnQuit;
	if (Intent.IsBound())
	{
		Intent.Execute(Terminal.Owner, Terminal.SessionSerial);
	}
	return true;
}

void UElysiumTerminalScreen::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	TerminalInput = nullptr;
}
