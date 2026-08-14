#include "UI/ElysiumDialogueScreen.h"

#include "UI/ElysiumDialogueWidget.h"
#include "UI/ElysiumActionButton.h"
#include "UI/ElysiumUIStyle.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
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
			Dialogue.bTerminal, Dialogue.bAwaitingAutomatic);
		const int32 FallbackChoice = Dialogue.Choices.IsEmpty()
			? -1 : FMath::Clamp(PreviousChoice, 0, Dialogue.Choices.Num() - 1);
		FinalizeNavigationBuild(ActionIdForChoice(FallbackChoice));
	}
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
		.OnBuildChoice(FElysiumBuildDlgChoice::CreateUObject(
			this, &UElysiumDialogueScreen::BuildChoiceAction));
	TSharedRef<SWidget> Result = SNew(SDPIScaler)
		.DPIScale_Lambda([this]() { return VirtualScale(); })
		[ DialogueBox.ToSharedRef() ];
	FinalizeNavigationBuild(ActionIdForChoice(Dialogue.Choices.IsEmpty() ? -1 : 0));
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
		ExecuteAction(ActionIdForChoice(Choice.GetValue()));
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geometry, KeyEvent);
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

TSharedRef<SWidget> UElysiumDialogueScreen::BuildChoiceAction(int32 Choice, const FText& Label)
{
	UElysiumActionButton* Button = CreateActionButton(
		ActionIdForChoice(Choice), TEXT("Dialogue"), Label, true,
		[this, Choice]() { OnChoice.ExecuteIfBound(Choice); });
	check(Button);
	const TWeakObjectPtr<UElysiumActionButton> WeakButton = Button;
	Button->SetSlateContent(
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
		.BorderBackgroundColor_Lambda([WeakButton]()
		{
			const UElysiumActionButton* Action = WeakButton.Get();
			return FSlateColor(Action && Action->IsActionSelected()
				? FLinearColor(0.62f, 0.10f, 0.12f, 0.55f)
				: FLinearColor(1.0f, 1.0f, 1.0f, 0.05f));
		})
		.Padding(FMargin(10.0f, 6.0f))
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle(
				"Regular", ElysiumDialogueUI::ChoiceFontPoints))
			.ColorAndOpacity_Lambda([WeakButton]()
			{
				const UElysiumActionButton* Action = WeakButton.Get();
				return FSlateColor(Action && Action->IsActionSelected()
					? FLinearColor::White : FLinearColor(0.86f, 0.85f, 0.82f, 1.0f));
			})
			.AutoWrapText(true)
			.Text(Label)
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
