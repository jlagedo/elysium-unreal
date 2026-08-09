#pragma once

#include "ElysiumViewState.h"
#include "UI/ElysiumNavigableScreen.h"

#include "ElysiumDialogueScreen.generated.h"

// CommonUI host for the retained dialogue Slate body. One instance spans a conversation and updates
// turns in place; choices return through the local-player UI owner to the presentation command seam.
UCLASS()
class UElysiumDialogueScreen final : public UElysiumNavigableScreen
{
	GENERATED_BODY()

public:
	UElysiumDialogueScreen();

	void ApplyDialogue(const FElysiumDialogueView& InDialogue);

	DECLARE_DELEGATE_OneParam(FOnChoice, int32);
	FOnChoice OnChoice;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual bool NativeOnHandleBackAction() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

private:
	float VirtualScale() const;
	FName ActionIdForChoice(int32 Choice) const;
	TSharedRef<SWidget> BuildChoiceAction(int32 Choice, const FText& Label);

	FElysiumDialogueView Dialogue;
	TSharedPtr<class SElysiumDialogueBox> DialogueBox;
};
