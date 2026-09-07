#include "UI/ElysiumDialogueWidget.h"

#include "ElysiumDlg.h"
#include "UI/ElysiumUIStyle.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// The one palette — geometry + transparency, no art. Warm bone type on a black translucent
	// slab, a blood-tint accent for the speaker, matching the project's grimy gothic direction.
	//
	// The slab is PURE black at retail's measured alpha. The tint is a linear colour, and the
	// former `0.02` was not black: linear 0.02 is sRGB byte 39, so the panel composited to ~40/255
	// over a lit scene where retail's box measures ~15/255 — 2.7x too light, which is the whole of
	// the "too grey" complaint. Alpha 0.69 is retail's own, recovered from the Jack capture by
	// solving `inside = scene * (1 - alpha)` across six channel samples above and below the box
	// (0.65-0.71, mean 0.688). Zero, not a near-zero: any lift here is multiplied by a large area.
	const FLinearColor ColPanel(0.0f, 0.0f, 0.0f, 0.69f);
	const FLinearColor ColSpeaker(0.80f, 0.18f, 0.18f, 1.0f);
	const FLinearColor ColLine(0.92f, 0.90f, 0.85f, 1.0f);
	const FLinearColor ColRule(0.80f, 0.18f, 0.18f, 0.5f);

	// A choice row carries NO plate (owner call, 2026-09-07). At the retail-tight pitch the former
	// per-row `SBorder` — white at 5% idle, blood at 55% selected — turned four adjacent rows into
	// ruled table cells, and four stacked 5% whites are themselves a grey wash over the slab.
	// Selection is carried by the text instead, on two channels so it does not rest on brightness
	// alone: the sentence lifts bone -> white, and the row's number takes the amber accent. Retail
	// has no equivalent (it is keyboard-only, `CHudDialog` paints every row identically), so this
	// affordance is the port's own and exists for mouse and gamepad focus.
	const FLinearColor ColChoiceText = ElysiumDialogueUI::ColChoiceIdle;
	// The row number and the M-REQ requirement run. The label is the sheet's own accent (the art
	// gold), deliberately not the speaker's blood red — it is information, not emphasis.
	const FLinearColor ColNumber = ElysiumDialogueUI::ColChoiceNumberIdle;
	const FLinearColor ColLabel = ElysiumUI::Palette::Amber;
	const FLinearColor ColHint(0.62f, 0.61f, 0.58f, 1.0f);

	const FSlateBrush* WhiteBox() { return FCoreStyle::Get().GetBrush("GenericWhiteBox"); }
}

FString ElysiumDialogueUI::FormatRequirementLabel(const FElysiumDlgGateLabel& Label)
{
	// `bValid` is the substrate's own answer to "is there a labellable skill front here" — set only
	// for a resolved, non-inverted trait. A pure-Python gate, an unresolved name and an authored
	// negative-threshold failure route all arrive with it clear and are drawn as plain rows.
	if (!Label.bValid || Label.Trait.IsEmpty())
	{
		return FString();
	}
	FString Out = FString::Printf(TEXT("[ %s %d/%d"),
		*Label.Trait.ToUpper(), Label.Have, Label.Required);
	if (Label.Kind == EElysiumDlgTraitClass::Discipline && Label.BloodCost > 0)
	{
		// The second half of retail's discipline test (`TestSimple` `0x100e9760` compares the rating
		// AND the blood pool) and the price `pc_charge_dependency` `0x100e8b90` takes on the pick.
		// U+00B7 as an escape, not a literal byte: the file stays pure ASCII so no compiler
		// codepage assumption can turn the separator into mojibake.
		//
		// M-DISABLED's second failure mode: the rating passes but the pool is short. The row is
		// disabled for the blood, not for the rating, so a bare `2 BLOOD` beside a passing `3/2`
		// would read as an unexplained refusal. When the substrate marks the pool short the price
		// is written as have/needed (`[ DOMINATE 3/2 \u00B7 1/2 BLOOD ]`) so the label names the
		// half that actually failed; an affordable row keeps the plain price.
		Out += Label.bBloodShort
			? FString::Printf(TEXT(" \u00B7 %d/%d BLOOD"), Label.Pool, Label.BloodCost)
			: FString::Printf(TEXT(" \u00B7 %d BLOOD"), Label.BloodCost);
	}
	Out += TEXT(" ]");
	return Out;
}

void ElysiumDialogueUI::FillTurn(const FElysiumDlgConversation& Conversation,
	FElysiumDialogueView& Out)
{
	const bool bMale = Conversation.PlayerMale();
	const int32 ClanOffset = Conversation.PlayerClanOffset();

	Out.bNoValidReply = Conversation.NoValidReply();
	if (const FElysiumDlgLine* NpcLine = Conversation.CurrentNpcLine())
	{
		// The band gated every row out with no automatic continuation: retail replaces the NPC's
		// own subtitle rather than leaving a line with nothing under it (`get_pc_responses`
		// `0x100e82d0`). The substitution is presentation's, which is why the machine only reports
		// the condition.
		Out.Line = Conversation.NoValidReply()
			? FElysiumDlgConversation::NoValidReplyText()
			: NpcLine->DisplayText(bMale, ClanOffset);
	}

	// Author order, enabled and disabled alike (M-DISABLED). The gate was evaluated once by
	// `FElysiumDlgDependency::Explain` when the band was gathered and is never re-evaluated: the
	// row carries its verdict and its M-REQ label as text, so nothing downstream evaluates at all.
	const TArray<FElysiumDlgVisibleChoice>& Visible = Conversation.VisibleChoices();
	for (int32 v = 0; v < Visible.Num(); ++v)
	{
		const FElysiumDlgLine* Choice = Conversation.VisibleChoice(v);
		if (!Choice)
		{
			continue;
		}
		FElysiumDialogueChoiceView& Row = Out.Choices.AddDefaulted_GetRef();
		Row.Text = Choice->DisplayText(bMale, ClanOffset);
		Row.bEnabled = Visible[v].bEnabled;
		Row.Kind = Visible[v].Gate.Label.Kind;
		Row.Have = Visible[v].Gate.Label.Have;
		Row.Required = Visible[v].Gate.Label.Required;
		Row.BloodCost = Visible[v].Gate.Label.BloodCost;
		Row.Label = FormatRequirementLabel(Visible[v].Gate.Label);
		Row.LineId = Choice->Id;
		Out.ChoiceIds.Add(Choice->Id);
	}
	Out.bAwaitingAutomatic = Conversation.IsAwaitingAutomatic();
}

FElysiumDialogueRowText ElysiumDialogueUI::RowTextFor(
	const TArray<FElysiumDialogueChoiceView>& Choices, int32 Index)
{
	FElysiumDialogueRowText Row;
	if (!Choices.IsValidIndex(Index))
	{
		return Row;
	}
	// M-DISABLED: the number is the position in author order, counted across enabled and disabled
	// rows alike, so a line keeps its key whether or not the player has the skill for its sibling.
	Row.Number = Index + 1;
	Row.Label = Choices[Index].Label;
	Row.Text = Choices[Index].Text;
	return Row;
}

TSharedRef<SWidget> ElysiumDialogueUI::BuildRowContent(const FElysiumDialogueRowText& Row,
	const TAttribute<FSlateColor>& TextColour, const TAttribute<FSlateColor>& NumberColour)
{
	// An unset number colour is the ordinary quiet grey; a bound one is the selection channel.
	const TAttribute<FSlateColor> Number = NumberColour.IsSet() || NumberColour.IsBound()
		? NumberColour : TAttribute<FSlateColor>(FSlateColor(ColNumber));
	const FSlateFontInfo NumberFont = ElysiumUIFonts().Font(
		EElysiumFontRole::Data, EElysiumFontWeight::SemiBold, ChoiceFontVirtualPixels, 1.0f);
	const FSlateFontInfo LabelFont = ElysiumUIFonts().Font(
		EElysiumFontRole::Data, EElysiumFontWeight::SemiBold, LabelFontVirtualPixels, 1.0f);
	const FSlateFontInfo TextFont = ElysiumUIFonts().Font(
		EElysiumFontRole::Data, EElysiumFontWeight::Regular, ChoiceFontVirtualPixels, 1.0f);

	TSharedRef<SHorizontalBox> Runs = SNew(SHorizontalBox);
	if (Row.Number > 0)
	{
		Runs->AddSlot().AutoWidth().VAlign(VAlign_Top).Padding(0, 0, 8, 0)
		[
			SNew(SBox).MinDesiredWidth(18.0f)
			[
				SNew(STextBlock)
				.Font(NumberFont)
				.ColorAndOpacity(Number)
				.Text(FText::FromString(FString::Printf(TEXT("%d."), Row.Number)))
			]
		];
	}
	if (!Row.Label.IsEmpty())
	{
		// M-REQ: a separate run in the accent colour, before the sentence. Never wrapped — the
		// bracket reads as one token.
		Runs->AddSlot().AutoWidth().VAlign(VAlign_Top).Padding(0, 0, 8, 0)
		[
			SNew(STextBlock)
			.Font(LabelFont)
			.ColorAndOpacity(FSlateColor(ColLabel))
			.Text(FText::FromString(Row.Label))
		];
	}
	Runs->AddSlot().FillWidth(1.0f).VAlign(VAlign_Top)
	[
		SNew(STextBlock)
		.Font(TextFont)
		.ColorAndOpacity(TextColour)
		.AutoWrapText(true)
		.Text(FText::FromString(Row.Text))
	];
	return Runs;
}

TOptional<int32> ElysiumDialogueUI::ChoiceForKey(
	const FKey& Key, int32 NumChoices, bool bTerminal, bool bAwaitingAutomatic)
{
	if (bAwaitingAutomatic && !bTerminal)
	{
		return TOptional<int32>();
	}
	if (NumChoices == 0)
	{
		// Nothing but the Continue affordance is on screen, so key 1 names it as well.
		return Key == EKeys::SpaceBar || Key == EKeys::Enter || Key == EKeys::One
			? TOptional<int32>(-1) : TOptional<int32>();
	}
	if (bTerminal && (Key == EKeys::SpaceBar || Key == EKeys::Enter))
	{
		// A terminal turn that still carries rows (every row disabled, M-DISABLED) draws those rows
		// AND Continue. Only Space/Enter advance: the number keys keep naming their rows, which the
		// screen then refuses, so key 1 can never silently mean "continue" while row 1 is on screen.
		return TOptional<int32>(-1);
	}

	// M-CAP allows a band of any size; the keyboard shortcut covers 1-9 and rows beyond that are
	// reached by mouse or by gamepad/arrow focus, which walks the whole band. The numbering itself
	// is still 1..N (M-DISABLED), so no key ever names a different row as the band grows.
	static const FKey Row[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	static const FKey Pad[] = { EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree,
		EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven,
		EKeys::NumPadEight, EKeys::NumPadNine };
	for (int32 Index = 0; Index < NumChoices && Index < UE_ARRAY_COUNT(Row); ++Index)
	{
		if (Key == Row[Index] || Key == Pad[Index])
		{
			return Index;
		}
	}
	return TOptional<int32>();
}

bool ElysiumDialogueUI::IsSkipKey(const FKey& Key, bool bNpcSpeaking, bool bCanSkip)
{
	return bNpcSpeaking && bCanSkip && Key == EKeys::SpaceBar;
}

void SElysiumDialogueBox::Construct(const FArguments& InArgs)
{
	BuildChoiceEvent = InArgs._OnBuildChoice;
	SetDialogue(InArgs._Speaker, InArgs._Line, InArgs._Choices, InArgs._bTerminal,
		InArgs._bAwaitingAutomatic, InArgs._bNpcSpeaking, InArgs._bCanSkip);
}

void SElysiumDialogueBox::SetDialogue(const FString& Speaker, const FString& Line,
	const TArray<FElysiumDialogueChoiceView>& Choices, bool bInTerminal,
	bool bInAwaitingAutomatic, bool bInNpcSpeaking, bool bInCanSkip)
{
	NumChoices = Choices.Num();
	bTerminal = bInTerminal;
	bAwaitingAutomatic = bInAwaitingAutomatic;
	bNpcSpeaking = bInNpcSpeaking;
	bCanSkip = bInCanSkip;
	RebuildDialogue(Speaker, Line, Choices);
}

TSharedRef<SWidget> SElysiumDialogueBox::BuildDisabledRow(int32 Number,
	const FElysiumDialogueChoiceView& Choice) const
{
	FElysiumDialogueRowText Row;
	Row.Number = Number;
	Row.Label = Choice.Label;
	Row.Text = Choice.Text;

	// Plain Slate, never a CommonUI action: that is what makes it unclickable, unhoverable and
	// unreachable by gamepad focus in one stroke, rather than three separate suppressions.
	TSharedRef<SWidget> Content = ElysiumDialogueUI::BuildRowContent(
		Row, FSlateColor(ColChoiceText));
	Content->SetRenderOpacity(ElysiumDialogueUI::DisabledRowOpacity);
	return SNew(SBox).Padding(FMargin(ElysiumDialogueUI::ChoiceRowPaddingHorizontal,
		ElysiumDialogueUI::ChoiceRowPaddingVertical))[ Content ];
}

void SElysiumDialogueBox::RebuildDialogue(const FString& Speaker, const FString& Line,
	const TArray<FElysiumDialogueChoiceView>& Choices)
{
	// The project's own faces (M-UI). One face for the whole band: Inter, the `Data` role, with
	// the speaker's air coming from tracking rather than a small-caps cut (ElysiumDialogueWidget.h).
	FSlateFontInfo SpeakerFont = ElysiumUIFonts().Font(
		EElysiumFontRole::Data, EElysiumFontWeight::SemiBold,
		ElysiumDialogueUI::SpeakerFontVirtualPixels, 1.0f);
	SpeakerFont.LetterSpacing =
		FMath::RoundToInt(ElysiumDialogueUI::SpeakerTrackingEm * 1000.0f);
	const FSlateFontInfo LineFont = ElysiumUIFonts().Font(
		EElysiumFontRole::Data, EElysiumFontWeight::Regular,
		ElysiumDialogueUI::LineFontVirtualPixels, 1.0f);
	const FSlateFontInfo HintFont = ElysiumUIFonts().Font(
		EElysiumFontRole::Data, EElysiumFontWeight::Regular,
		ElysiumDialogueUI::HintFontVirtualPixels, 1.0f);
	TSharedRef<SVerticalBox> Inner = SNew(SVerticalBox);

	Inner->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
	[
		SNew(STextBlock)
		.Font(SpeakerFont)
		.ColorAndOpacity(FSlateColor(ColSpeaker))
		.Text(FText::FromString(
			(Speaker.IsEmpty() ? FString(TEXT("???")) : Speaker).ToUpper()))
	];

	Inner->AddSlot().AutoHeight().Padding(0, 0, 0, 10)
	[
		SNew(STextBlock)
		.Font(LineFont)
		.ColorAndOpacity(FSlateColor(ColLine))
		.AutoWrapText(true)
		.Text(FText::FromString(Line))
	];

	// M-SKIP. Retail has no such affordance (the hurry verb is the undocumented `dialogpick -2`);
	// the port names the key while the verb would do something, and takes the hint down with the
	// voice — which is why the voice flag is part of the reconcile key.
	if (bNpcSpeaking && bCanSkip)
	{
		Inner->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(STextBlock)
			.Font(HintFont)
			.ColorAndOpacity(FSlateColor(ColHint))
			.Text(NSLOCTEXT("Elysium", "DialogueSkipHint", "Space: skip"))
		];
	}

	// A thin rule between the line and the responses.
	Inner->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		SNew(SBox).HeightOverride(1.0f)
		[
			SNew(SBorder).BorderImage(WhiteBox()).BorderBackgroundColor(ColRule)
		]
	];

	if (bAwaitingAutomatic && !bTerminal)
	{
		// The preceding NPC turn remains readable while its voice owns the transition. The editor's
		// `(Auto-Link)`/`(Auto-End)` marker is deliberately absent from the response band.
	}
	else
	{
		// The band is drawn whenever it has rows, terminal or not. `bTerminal` is raised by any
		// turn with no ENABLED row — which is exactly the M-DISABLED case, a band whose rows all
		// fail their skill fronts — so keying the Continue-only branch on it would erase the
		// requirement labels precisely where the modernization exists to show them. The empty band
		// (a real terminal line, the no-valid-reply substitution, or the forced visible response
		// retail makes when Auto-End finds no audio, `0x100e82d0`) is the only Continue-only case.
		for (int32 i = 0; i < NumChoices; ++i)
		{
			// M-REVEAL keeps the band up while the NPC speaks, so this list is built once per turn
			// and not gated on `IsTalking()` as `ShowPlayerChoices` is.
			Inner->AddSlot().AutoHeight().Padding(0, ElysiumDialogueUI::ChoiceRowGap)
			[
				Choices[i].bEnabled
					? (BuildChoiceEvent.IsBound()
						? BuildChoiceEvent.Execute(i, ElysiumDialogueUI::RowTextFor(Choices, i))
						: SNullWidget::NullWidget)
					: BuildDisabledRow(i + 1, Choices[i])
			];
		}
		if (bTerminal || NumChoices == 0)
		{
			// The one way out of a turn that offers no pickable row, drawn after the rows so the
			// player reads what they lacked and then leaves.
			FElysiumDialogueRowText Continue;
			Continue.Text = TEXT("Continue");
			Inner->AddSlot().AutoHeight().Padding(0, ElysiumDialogueUI::ChoiceRowGap)
			[
				BuildChoiceEvent.IsBound()
					? BuildChoiceEvent.Execute(-1, Continue)
					: SNullWidget::NullWidget
			];
		}
	}

	// Dock the response band low and centred, leaving the dialogue partner visible above it. These
	// metrics are virtual-canvas values; the screen's SDPIScaler turns them into the same viewport
	// proportions at every supported 16:9 resolution.
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.0f) [ SNullWidget::NullWidget ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		.Padding(0, 0, 0, ElysiumDialogueUI::ResponsePanelBottomInset)
		[
			SNew(SBox).WidthOverride(ElysiumDialogueUI::ResponsePanelWidth)
			[
				SNew(SBorder)
				.BorderImage(WhiteBox())
				.BorderBackgroundColor(ColPanel)
				.Padding(FMargin(28, 20))
				[
					Inner
				]
			]
		]
	];
}
