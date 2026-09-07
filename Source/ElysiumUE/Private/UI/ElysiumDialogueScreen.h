#pragma once

#include "ElysiumViewState.h"
#include "UI/ElysiumNavigableScreen.h"

#include "ElysiumDialogueScreen.generated.h"

struct FElysiumDialogueRowText;

// CommonUI host for the retained dialogue Slate body. One instance spans a conversation and updates
// turns in place; choices return through the local-player UI owner to the presentation command seam.
UCLASS()
class UElysiumDialogueScreen final : public UElysiumNavigableScreen
{
	GENERATED_BODY()

public:
	UElysiumDialogueScreen();

	void ApplyDialogue(const FElysiumDialogueView& InDialogue);

	// (visible index, the row's `.dlg` line id). The id travels with the pick so the substrate can
	// refuse a stale one: the band the player clicked may already have been replaced by an
	// automatic turn or a script, and a bare position would then name a different sentence.
	// -1/INDEX_NONE is the Continue affordance, which carries no row.
	DECLARE_DELEGATE_TwoParams(FOnChoice, int32, int32);
	FOnChoice OnChoice;

	// M-SKIP — the hurry verb. Separate from OnChoice because it is not a pick: it ends the voice
	// and leaves the same band up (retail `dialogpick -2`, `0x102c0bb0`).
	DECLARE_DELEGATE(FOnSkip);
	FOnSkip OnSkip;

	// Whether the screen would act on this row right now. Public so the input policy is assertable
	// headlessly: a disabled row (M-DISABLED) and any row during a live automatic wait are refused.
	bool AcceptsChoice(int32 Choice) const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual bool NativeOnHandleBackAction() override;
	// The base screen claims SpaceBar in PREVIEW to activate the focused action, so M-SKIP has to
	// be claimed there too or the hurry verb would pick a response instead of ending the voice.
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,
		const FKeyEvent& KeyEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

private:
	float VirtualScale() const;
	FName ActionIdForChoice(int32 Choice) const;
	// The `.dlg` row id currently published at this position, INDEX_NONE for Continue or a
	// position the band no longer has. Resolved at pick time, not at build time, so the id sent is
	// always the one the box is showing.
	int32 LineIdForChoice(int32 Choice) const;
	int32 FirstFocusableChoice(int32 Preferred) const;
	TSharedRef<SWidget> BuildChoiceAction(int32 Choice, const FElysiumDialogueRowText& Row);

	FElysiumDialogueView Dialogue;
	TSharedPtr<class SElysiumDialogueBox> DialogueBox;
};
