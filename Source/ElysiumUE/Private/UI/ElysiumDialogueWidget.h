#pragma once

#include "CoreMinimal.h"
#include "ElysiumViewState.h"
#include "UI/ElysiumUIStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

// Translucent panel docked to the lower screen: speaker name, NPC subtitle, and a numbered list of
// PC choices. A dumb view — it snapshots one conversation turn and reports the player's pick
// through OnChoose; the branch machine, the `.dlg` data, and OnDialogEnd all live in the substrate.
// Number keys 1-9 select; a terminal line offers a single "continue"; Space hurries the voice.
//
// **M-DISABLED** rows arrive here already decided (`FElysiumDialogueChoiceView::bEnabled`). A
// disabled row is drawn dimmed as plain Slate and is never given a CommonUI action, which is what
// makes it simultaneously unclickable, unhoverable and unreachable by gamepad focus — the screen
// only ever builds action buttons for rows it will accept.
DECLARE_DELEGATE_OneParam(FElysiumOnDlgChoice, int32);   // choice index >= 0, or -1 to advance a terminal line

// One drawable response row, already split into its runs. The number and the requirement label are
// separate runs from the sentence because they are drawn in different colours (M-REQ), and the
// split is made once here so the enabled path (a CommonUI action button built by the screen) and
// the disabled path (plain Slate built by the box) cannot drift apart.
struct FElysiumDialogueRowText
{
	int32 Number = 0;   // 1-based position in author order; 0 = the Continue affordance, unnumbered
	FString Label;      // the M-REQ requirement run, or empty
	FString Text;       // the sentence
};

DECLARE_DELEGATE_RetVal_TwoParams(TSharedRef<SWidget>, FElysiumBuildDlgChoice,
	int32 /*choice index, -1 = continue*/, const FElysiumDialogueRowText& /*row*/);

struct FElysiumDlgGateLabel;
class FElysiumDlgConversation;

namespace ElysiumDialogueUI
{
	// The dialogue type ramp, in the shared 768-high virtual canvas. `FElysiumUIFontLibrary::Font`
	// takes virtual pixels and the screen-level SDPIScaler applies `ElysiumUI::ScaleFor(ScreenH)`
	// once at the root, so nothing here converts to typographic points.
	//
	// The whole band is one face -- Inter, the `Data` role (owner call, 2026-09-07). Dialogue is
	// read at speed over a lit 3D scene, so the band wants the most neutral, highest-legibility
	// face the project ships: Inter's tall x-height and open apertures survive the busy backdrop
	// and small sizes where Spectral's serifs close up. Spectral SC stays the voice of the menus
	// and the sheet; it is not the voice of a conversation.
	//
	// The SIZES are retail's, measured (see `RetailBand*` below): the line and the choices share
	// one size, as `CHudDialog` draws them, and the two are told apart by colour alone. Retail's
	// em normalizes to 17.8 vp, so the band rounds it to 18. The speaker name has no retail
	// counterpart -- VtMB's box carries no name -- and stays a smaller attribution above the line.
	inline constexpr float SpeakerFontVirtualPixels = 15.0f;
	inline constexpr float LineFontVirtualPixels = 18.0f;
	inline constexpr float ChoiceFontVirtualPixels = 18.0f;
	inline constexpr float LabelFontVirtualPixels = 12.0f;   // the M-REQ requirement run
	inline constexpr float HintFontVirtualPixels = 11.0f;    // the M-SKIP hint

	// Retail's band, measured off a 1999x1124 client capture of Jack's tutorial conversation and
	// divided by that client's 1124/768 canvas scale. Cap height 19 px and x-height 14 px put the
	// em at ~26 px; the baseline pitch is a dead-constant 40.5 px across all seven drawn rows.
	// The orange NPC line and the white choices measure identically, which is why the two font
	// sizes above are equal. The capture also confirms `ResponsePanelWidth`: retail's box is
	// 1267 px wide, or 866 vp.
	//
	// Kept as named constants because the port's row rhythm is checked against them rather than
	// eyeballed -- the wall-of-choices feel is set by the pitch, not by the size.
	inline constexpr float RetailBandEmVirtualPixels = 17.8f;
	inline constexpr float RetailBandPitchVirtualPixels = 27.7f;
	inline constexpr float RetailBandLeadingVirtualPixels =
		RetailBandPitchVirtualPixels - RetailBandEmVirtualPixels;   // ~9.9

	// The choice row's two selection channels, shared with the screen because the enabled row is
	// built there (a CommonUI action button) and the disabled row here (plain Slate), and the two
	// must not drift. With the row's plate removed these ARE the affordance: the sentence lifts
	// bone -> white and the number takes the amber accent. Retail paints every row identically
	// (`CHudDialog`, keyboard-only), so the highlight is the port's own, for mouse and gamepad.
	inline const FLinearColor ColChoiceIdle(0.74f, 0.73f, 0.70f, 1.0f);
	inline const FLinearColor ColChoiceSelected(1.0f, 1.0f, 1.0f, 1.0f);
	inline const FLinearColor ColChoiceNumberIdle(0.55f, 0.54f, 0.51f, 1.0f);
	inline const FLinearColor ColChoiceNumberSelected = ElysiumUI::Palette::Amber;

	// A choice row's own box. Retail spends ~9.9 vp of leading above the em and no padding at all;
	// Slate's text block already contributes the face's ascent+descent above the em (~0.21 em for
	// Inter, ~3.8 vp at 18), so the widget budget below is the remainder. Horizontal padding is
	// ours -- it is the hover/selection plate retail has no equivalent for.
	inline constexpr float ChoiceRowPaddingHorizontal = 10.0f;
	inline constexpr float ChoiceRowPaddingVertical = 2.0f;
	inline constexpr float ChoiceRowGap = 1.0f;   // the slot padding between adjacent rows

	// What the two above cost per row, the quantity the pitch test constrains.
	inline constexpr float ChoiceRowLeadingBudget =
		2.0f * ChoiceRowPaddingVertical + 2.0f * ChoiceRowGap;   // 6

	// The speaker name is drawn uppercase to keep the small-caps silhouette the Spectral SC name
	// used to carry. Inter has no small-caps face, so the air comes from tracking instead, in
	// 1/1000 em -- lighter than `ElysiumUI::Type::TrackLabel`, which is cut for true small caps.
	inline constexpr float SpeakerTrackingEm = 0.06f;

	// M-DISABLED's dimming. A failed skill row stays legible enough to read its requirement label
	// and plainly too dim to be mistaken for a pickable line.
	inline constexpr float DisabledRowOpacity = 0.45f;

	// Dialogue is authored inside the shared 768-high virtual canvas. At 16:9 this width occupies
	// 63.3% of the viewport, matching the low, centred response band without turning it into a
	// full-width subtitle slab. The lower inset keeps the frame close to the bottom edge while
	// remaining clear of display overscan.
	inline constexpr float ResponsePanelWidth = 864.0f;
	inline constexpr float ResponsePanelBottomInset = 24.0f;

	// M-REQ. The requirement run drawn before a choice's sentence, replacing retail's font/colour
	// encoding (`GetFontForFlagsDependency` `0x10053f10`) and its N dot glyphs
	// (`CDialogDependency::ToStr` `0x100ea5e0`) with a Baldur's-Gate-3-shaped bracket:
	//
	//     [ PERSUASION 4/7 ]              a feat/attribute/ability check, have / required
	//     [ DOMINATE 2/2 · 2 BLOOD ]      a discipline also names the pool it spends
	//
	// Shown on PASSING rows too, so the player reads what a line cost them, not only what they
	// lack. Empty for a label the gate did not mark valid — a pure-Python gate, an unresolved
	// trait, or an authored negative-threshold failure route, none of which are labelled.
	//
	// Pure, and deliberately free of Slate and of the entity world: it is the whole of the M-REQ
	// text rule and is asserted directly in `Elysium.UI.DialogueChoiceLabels`.
	FString FormatRequirementLabel(const FElysiumDlgGateLabel& Label);

	// Pure input policy shared by the CommonUI wrapper and retained Slate body. An engaged optional
	// carries the visible choice index; -1 advances a terminal line. A normal automatic wait owns
	// the keys but resolves only from voice completion, so it produces no action.
	//
	// Enablement is NOT consulted here: the map from key to row is positional (M-DISABLED numbers
	// 1..N across enabled and disabled rows alike, so the number of a line never moves). The screen
	// resolves the row and then refuses a disabled one.
	TOptional<int32> ChoiceForKey(const FKey& Key, int32 NumChoices, bool bTerminal,
		bool bAwaitingAutomatic = false);

	// M-SKIP — retail's hurry verb (pick `-2`, `0x102c0bb0`) as a key. Space while the NPC's voice
	// is still running ends the line early; it takes precedence over Space-as-Continue, which is
	// what the same key means once the voice is done.
	bool IsSkipKey(const FKey& Key, bool bNpcSpeaking, bool bCanSkip);

	// The runs of one row, laid out. `TextColour` and `NumberColour` are attributes so the screen
	// can bind both to its action button's selection state -- with no plate behind the row, those
	// two runs ARE the selection affordance. An unbound `NumberColour` takes the quiet default,
	// which is what the disabled rows want. The requirement run is fixed palette either way.
	TSharedRef<SWidget> BuildRowContent(const FElysiumDialogueRowText& Row,
		const TAttribute<FSlateColor>& TextColour,
		const TAttribute<FSlateColor>& NumberColour = TAttribute<FSlateColor>());

	// The world-free half of the dialogue publish: the subtitle and the response band, projected
	// out of the branch machine exactly once per turn. Everything that needs the entity world
	// (speaker name, owner handle, whether the voice is running) is the publisher's and stays
	// there; everything that is a rule about the turn itself lives here so it is assertable with a
	// conversation and nothing else.
	void FillTurn(const FElysiumDlgConversation& Conversation, FElysiumDialogueView& Out);

	// The row text for the Nth published choice — the one place the "1..N in author order across
	// enabled and disabled" rule (M-DISABLED) is expressed.
	FElysiumDialogueRowText RowTextFor(const TArray<FElysiumDialogueChoiceView>& Choices, int32 Index);
}

class SElysiumDialogueBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SElysiumDialogueBox) {}
		SLATE_ARGUMENT(FString, Speaker)                 // the NPC's name (targetname), header line
		SLATE_ARGUMENT(FString, Line)                    // the NPC subtitle for this turn
		SLATE_ARGUMENT(TArray<FElysiumDialogueChoiceView>, Choices)  // PC rows, in author order
		SLATE_ARGUMENT(bool, bTerminal)                  // no choices — show a single "continue"
		SLATE_ARGUMENT(bool, bAwaitingAutomatic)         // voice owns advancement; show no response row
		SLATE_ARGUMENT(bool, bNpcSpeaking)               // the line's voice is still running
		SLATE_ARGUMENT(bool, bCanSkip)                   // the hurry verb would do something
		SLATE_EVENT(FElysiumBuildDlgChoice, OnBuildChoice)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetDialogue(const FString& Speaker, const FString& Line,
		const TArray<FElysiumDialogueChoiceView>& Choices, bool bInTerminal,
		bool bInAwaitingAutomatic, bool bInNpcSpeaking = false, bool bInCanSkip = false);

private:
	void RebuildDialogue(const FString& Speaker, const FString& Line,
		const TArray<FElysiumDialogueChoiceView>& Choices);
	// The dimmed, action-less presentation of a failed skill check (M-DISABLED).
	TSharedRef<SWidget> BuildDisabledRow(int32 Number,
		const FElysiumDialogueChoiceView& Choice) const;

	FElysiumBuildDlgChoice BuildChoiceEvent;
	int32 NumChoices = 0;
	bool bTerminal = false;
	bool bAwaitingAutomatic = false;
	bool bNpcSpeaking = false;
	bool bCanSkip = false;
};
