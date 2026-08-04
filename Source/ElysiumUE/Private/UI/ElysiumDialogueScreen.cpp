#include "UI/ElysiumDialogueScreen.h"

#include "UI/ElysiumDialogueWidget.h"

UElysiumDialogueScreen::UElysiumDialogueScreen()
{
	bIsBackHandler = true;
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
	return DialogueBox.ToSharedRef();
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
