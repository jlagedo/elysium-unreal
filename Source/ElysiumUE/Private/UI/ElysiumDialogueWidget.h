#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

// B4 — the interim visual-novel dialogue box. Pure geometry + transparency on native Slate (no
// textures, no VGUI, no sign machinery): a translucent panel docked to the lower screen with the
// speaker name, the NPC subtitle, and a numbered list of clickable PC choices. It is a dumb view —
// it snapshots one conversation turn and reports the player's pick through OnChoose; the branch
// machine, the `.dlg` data, and OnDialogEnd all live in the substrate. 9.2 replaces it with the real
// UI on the 8.6 stack. Number keys 1-9 select; a terminal line offers a single "continue".
DECLARE_DELEGATE_OneParam(FElysiumOnDlgChoice, int32);   // choice index >= 0, or -1 to advance a terminal line

class SElysiumDialogueBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SElysiumDialogueBox) {}
		SLATE_ARGUMENT(FString, Speaker)                 // the NPC's name (targetname), header line
		SLATE_ARGUMENT(FString, Line)                    // the NPC subtitle for this turn
		SLATE_ARGUMENT(TArray<FString>, Choices)         // PC choice labels, in author order
		SLATE_ARGUMENT(bool, bTerminal)                  // no choices — show a single "continue"
		SLATE_EVENT(FElysiumOnDlgChoice, OnChoose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// The box owns keyboard focus while open so number-key selection works under its UI-only scope.
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

private:
	FReply Pick(int32 Index);

	FElysiumOnDlgChoice OnChooseEvent;
	int32 NumChoices = 0;
	bool bTerminal = false;

	// SButton stores the style by pointer, not by value, so it must outlive Construct — hold it on the
	// widget (a Construct-local would dangle and crash on the first paint).
	FButtonStyle ChoiceRowStyle;
};
