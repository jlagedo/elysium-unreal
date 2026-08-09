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
DECLARE_DELEGATE_RetVal_TwoParams(TSharedRef<SWidget>, FElysiumBuildDlgChoice,
	int32 /*choice index*/, const FText& /*label*/);

namespace ElysiumDialogueUI
{
	// The dialogue type ramp is authored in virtual pixels, but FSlateFontInfo takes typographic
	// points and Slate rasterises them at 96 DPI. Convert once here; the screen-level SDPIScaler then
	// applies the shared ScreenH / 768 resolution law without the implicit 96 / 72 size inflation.
	inline constexpr float SlatePointsPerVirtualPixel = 72.0f / 96.0f;
	inline constexpr float SpeakerFontVirtualPixels = 22.0f;
	inline constexpr float LineFontVirtualPixels = 20.0f;
	inline constexpr float ChoiceFontVirtualPixels = 18.0f;
	inline constexpr float SpeakerFontPoints = SpeakerFontVirtualPixels * SlatePointsPerVirtualPixel;
	inline constexpr float LineFontPoints = LineFontVirtualPixels * SlatePointsPerVirtualPixel;
	inline constexpr float ChoiceFontPoints = ChoiceFontVirtualPixels * SlatePointsPerVirtualPixel;

	// Dialogue is authored inside the shared 768-high virtual canvas. At 16:9 this width occupies
	// 63.3% of the viewport, matching the low, centred response band without turning it into a
	// full-width subtitle slab. The lower inset keeps the frame close to the bottom edge while
	// remaining clear of display overscan.
	inline constexpr float ResponsePanelWidth = 864.0f;
	inline constexpr float ResponsePanelBottomInset = 24.0f;

	// Pure input policy shared by the CommonUI wrapper and retained Slate body. An engaged optional
	// carries the visible choice index; -1 advances a terminal line.
	TOptional<int32> ChoiceForKey(const FKey& Key, int32 NumChoices, bool bTerminal);
}

class SElysiumDialogueBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SElysiumDialogueBox) {}
		SLATE_ARGUMENT(FString, Speaker)                 // the NPC's name (targetname), header line
		SLATE_ARGUMENT(FString, Line)                    // the NPC subtitle for this turn
		SLATE_ARGUMENT(TArray<FString>, Choices)         // PC choice labels, in author order
		SLATE_ARGUMENT(bool, bTerminal)                  // no choices — show a single "continue"
		SLATE_EVENT(FElysiumBuildDlgChoice, OnBuildChoice)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetDialogue(const FString& Speaker, const FString& Line,
		const TArray<FString>& Choices, bool bInTerminal);

private:
	void RebuildDialogue(const FString& Speaker, const FString& Line,
		const TArray<FString>& Choices);
	FElysiumBuildDlgChoice BuildChoiceEvent;
	int32 NumChoices = 0;
	bool bTerminal = false;
};
