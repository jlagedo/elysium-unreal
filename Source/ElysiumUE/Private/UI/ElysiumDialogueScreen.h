#pragma once

#include "ElysiumViewState.h"
#include "UI/ElysiumActivatableScreen.h"

#include "ElysiumDialogueScreen.generated.h"

// CommonUI host for the retained dialogue Slate body. Each turn is initialized before activation;
// choices return through the owning HUD bridge to the presentation command seam.
UCLASS()
class UElysiumDialogueScreen final : public UElysiumActivatableScreen
{
	GENERATED_BODY()

public:
	UElysiumDialogueScreen();

	void SetDialogue(const FElysiumDialogueView& InDialogue) { Dialogue = InDialogue; }

	DECLARE_DELEGATE_OneParam(FOnChoice, int32);
	FOnChoice OnChoice;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual bool NativeOnHandleBackAction() override;

private:
	FElysiumDialogueView Dialogue;
	TSharedPtr<class SElysiumDialogueBox> DialogueBox;
};
