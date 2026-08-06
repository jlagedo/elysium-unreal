#include "UI/ElysiumDialogueScreen.h"

#include "UI/ElysiumDialogueWidget.h"
#include "UI/ElysiumUIStyle.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/SDPIScaler.h"

UElysiumDialogueScreen::UElysiumDialogueScreen()
{
	bIsBackHandler = true;
}

void UElysiumDialogueScreen::ApplyDialogue(const FElysiumDialogueView& InDialogue)
{
	Dialogue = InDialogue;
	if (DialogueBox)
	{
		DialogueBox->SetDialogue(Dialogue.Speaker, Dialogue.Line, Dialogue.Choices, Dialogue.bTerminal);
	}
}

TSharedRef<SWidget> UElysiumDialogueScreen::RebuildWidget()
{
	DialogueBox = SNew(SElysiumDialogueBox)
		.Speaker(Dialogue.Speaker)
		.Line(Dialogue.Line)
		.Choices(Dialogue.Choices)
		.bTerminal(Dialogue.bTerminal)
		.OnChoose(FElysiumOnDlgChoice::CreateLambda([this](int32 Choice)
		{
			OnChoice.ExecuteIfBound(Choice);
		}));
	return SNew(SDPIScaler)
		.DPIScale_Lambda([this]() { return VirtualScale(); })
		[ DialogueBox.ToSharedRef() ];
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
		KeyEvent.GetKey(), Dialogue.Choices.Num(), Dialogue.bTerminal))
	{
		// A held key picks one line, not a run of them. The OS repeats key-down at ~30/s and the
		// turn advances on the next tick, so without this the repeats select choice 0 of each
		// following node in turn — through irreversible ones — faster than the lines can be read.
		// The repeat is still consumed: the key belongs to the dialogue whether or not it acts.
		if (KeyEvent.IsRepeat())
		{
			return FReply::Handled();
		}
		OnChoice.ExecuteIfBound(Choice.GetValue());
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geometry, KeyEvent);
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
