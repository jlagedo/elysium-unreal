#include "UI/ElysiumDialogueScreen.h"

#include "UI/ElysiumDialogueWidget.h"
#include "UI/ElysiumActionButton.h"
#include "UI/ElysiumUIStyle.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Text/STextBlock.h"

UElysiumDialogueScreen::UElysiumDialogueScreen()
{
	bIsBackHandler = true;
}

void UElysiumDialogueScreen::ApplyDialogue(const FElysiumDialogueView& InDialogue)
{
	int32 PreviousChoice = 0;
	const FName PreviousAction = GetSelectedActionId();
	for (int32 Choice = 0; Choice < Dialogue.Choices.Num(); ++Choice)
	{
		if (ActionIdForChoice(Choice) == PreviousAction)
		{
			PreviousChoice = Choice;
			break;
		}
	}
	Dialogue = InDialogue;
	if (DialogueBox)
	{
		BeginNavigationBuild();
		SetNavigationGroup(TEXT("Dialogue"), false, true);
		DialogueBox->SetDialogue(Dialogue.Speaker, Dialogue.Line, Dialogue.Choices,
			Dialogue.bTerminal, Dialogue.bAwaitingAutomatic, Dialogue.bNpcSpeaking,
			Dialogue.bCanSkip);
		FinalizeNavigationBuild(ActionIdForChoice(FirstFocusableChoice(PreviousChoice)));
	}
}

int32 UElysiumDialogueScreen::FirstFocusableChoice(int32 Preferred) const
{
	// A contracted band repairs to the nearest surviving row, as before; M-DISABLED adds that a
	// disabled row never becomes an action, so the repair walks outward to the nearest ENABLED one
	// rather than landing on a position with no widget behind it. -1 is the Continue affordance.
	const int32 Num = Dialogue.Choices.Num();
	if (Num == 0)
	{
		return -1;
	}
	const int32 Start = FMath::Clamp(Preferred, 0, Num - 1);
	if (Dialogue.Choices[Start].bEnabled)
	{
		return Start;
	}
	for (int32 Distance = 1; Distance < Num; ++Distance)
	{
		if (Dialogue.Choices.IsValidIndex(Start + Distance)
			&& Dialogue.Choices[Start + Distance].bEnabled)
		{
			return Start + Distance;
		}
		if (Dialogue.Choices.IsValidIndex(Start - Distance)
			&& Dialogue.Choices[Start - Distance].bEnabled)
		{
			return Start - Distance;
		}
	}
	return -1;
}

TSharedRef<SWidget> UElysiumDialogueScreen::RebuildWidget()
{
	(void)Super::RebuildWidget();
	BeginNavigationBuild();
	SetNavigationGroup(TEXT("Dialogue"), false, true);

	DialogueBox = SNew(SElysiumDialogueBox)
		.Speaker(Dialogue.Speaker)
		.Line(Dialogue.Line)
		.Choices(Dialogue.Choices)
		.bTerminal(Dialogue.bTerminal)
		.bAwaitingAutomatic(Dialogue.bAwaitingAutomatic)
		.bNpcSpeaking(Dialogue.bNpcSpeaking)
		.bCanSkip(Dialogue.bCanSkip)
		.OnBuildChoice(FElysiumBuildDlgChoice::CreateUObject(
			this, &UElysiumDialogueScreen::BuildChoiceAction));
	TSharedRef<SWidget> Result = SNew(SDPIScaler)
		.DPIScale_Lambda([this]() { return VirtualScale(); })
		[ DialogueBox.ToSharedRef() ];
	FinalizeNavigationBuild(ActionIdForChoice(FirstFocusableChoice(0)));
	return Result;
}

void UElysiumDialogueScreen::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	DialogueBox.Reset();
}

bool UElysiumDialogueScreen::NativeOnHandleBackAction()
{
	// Dialogue advances only through its authored terminal/choice actions.
	return true;
}

FReply UElysiumDialogueScreen::NativeOnPreviewKeyDown(
	const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	// M-SKIP, claimed ahead of the navigable screen's own SpaceBar rule (which activates the
	// focused action): while the voice runs, Space is the hurry verb, not a pick and not Continue.
	// It is the one dialogue key that acts during an automatic wait, because it ends that wait.
	if (ElysiumDialogueUI::IsSkipKey(KeyEvent.GetKey(), Dialogue.bNpcSpeaking, Dialogue.bCanSkip))
	{
		if (!KeyEvent.IsRepeat())
		{
			OnSkip.ExecuteIfBound();
		}
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, KeyEvent);
}

FReply UElysiumDialogueScreen::NativeOnKeyDown(
	const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (const TOptional<int32> Choice = ElysiumDialogueUI::ChoiceForKey(
		KeyEvent.GetKey(), Dialogue.Choices.Num(), Dialogue.bTerminal,
		Dialogue.bAwaitingAutomatic))
	{
		// A held key picks one line, not a run of them. The OS repeats key-down at ~30/s and the
		// turn advances on the next tick, so without this the repeats select choice 0 of each
		// following node in turn — through irreversible ones — faster than the lines can be read.
		// The repeat is still consumed: the key belongs to the dialogue whether or not it acts.
		if (KeyEvent.IsRepeat())
		{
			return FReply::Handled();
		}
		// M-DISABLED: the number resolves positionally so the keys never move, and the row is
		// refused here. The key is still consumed — it belongs to the conversation, and letting it
		// fall through to the game would fire a weapon slot behind the open panel.
		if (!AcceptsChoice(Choice.GetValue()))
		{
			return FReply::Handled();
		}
		ExecuteAction(ActionIdForChoice(Choice.GetValue()));
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geometry, KeyEvent);
}

bool UElysiumDialogueScreen::AcceptsChoice(int32 Choice) const
{
	if (Dialogue.bAwaitingAutomatic && !Dialogue.bTerminal)
	{
		return false;
	}
	if (Choice < 0)
	{
		return true;   // Continue: terminal, no-valid-reply, or the forced visible response
	}
	return Dialogue.Choices.IsValidIndex(Choice) && Dialogue.Choices[Choice].bEnabled;
}

FName UElysiumDialogueScreen::ActionIdForChoice(int32 Choice) const
{
	if (Choice < 0)
	{
		return TEXT("Dialogue.Continue");
	}
	const int32 StableId = Dialogue.ChoiceIds.IsValidIndex(Choice)
		? Dialogue.ChoiceIds[Choice] : Choice;
	return FName(*FString::Printf(TEXT("Dialogue.Choice.%d"), StableId));
}

int32 UElysiumDialogueScreen::LineIdForChoice(int32 Choice) const
{
	return Dialogue.ChoiceIds.IsValidIndex(Choice) ? Dialogue.ChoiceIds[Choice] : INDEX_NONE;
}

TSharedRef<SWidget> UElysiumDialogueScreen::BuildChoiceAction(int32 Choice,
	const FElysiumDialogueRowText& Row)
{
	// Only ever called for a row the screen will accept — a disabled row is drawn by the box as
	// plain Slate and gets no action, which is what keeps it out of focus and off the mouse.
	UElysiumActionButton* Button = CreateActionButton(
		ActionIdForChoice(Choice), TEXT("Dialogue"), FText::FromString(Row.Text), true,
		[this, Choice]() { OnChoice.ExecuteIfBound(Choice, LineIdForChoice(Choice)); });
	check(Button);
	const TWeakObjectPtr<UElysiumActionButton> WeakButton = Button;
	// No plate behind the row (owner call, 2026-09-07). At the retail-tight pitch a per-row filled
	// border reads as a table cell, and the idle fill is a grey wash over the panel; the row is
	// bare text on the slab, exactly as `CHudDialog` paints it. Selection therefore has to live in
	// the type, and it uses two channels so it is not brightness alone: the sentence goes bone ->
	// white and the number takes the amber accent. An `SBox`, not an `SBorder` -- there is no
	// brush left to draw, only padding.
	const auto IsSelected = [WeakButton]()
	{
		const UElysiumActionButton* Action = WeakButton.Get();
		return Action && Action->IsActionSelected();
	};
	Button->SetSlateContent(
		SNew(SBox)
		.Padding(FMargin(ElysiumDialogueUI::ChoiceRowPaddingHorizontal,
			ElysiumDialogueUI::ChoiceRowPaddingVertical))
		[
			ElysiumDialogueUI::BuildRowContent(Row,
				TAttribute<FSlateColor>::CreateLambda([IsSelected]()
				{
					return FSlateColor(IsSelected()
						? ElysiumDialogueUI::ColChoiceSelected
						: ElysiumDialogueUI::ColChoiceIdle);
				}),
				TAttribute<FSlateColor>::CreateLambda([IsSelected]()
				{
					return FSlateColor(IsSelected()
						? ElysiumDialogueUI::ColChoiceNumberSelected
						: ElysiumDialogueUI::ColChoiceNumberIdle);
				}))
		]);
	return Button->TakeWidget();
}

float UElysiumDialogueScreen::VirtualScale() const
{
	FVector2D Size(1920.0f, 1080.0f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	return ElysiumUI::ScaleFor(static_cast<float>(Size.Y));
}
