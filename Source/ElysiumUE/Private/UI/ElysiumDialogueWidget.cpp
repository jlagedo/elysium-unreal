#include "UI/ElysiumDialogueWidget.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// The one palette — geometry + transparency, no art. Warm bone type on a near-black translucent
	// slab, a blood-tint accent for the speaker, matching the project's grimy gothic direction.
	const FLinearColor ColPanel(0.02f, 0.02f, 0.03f, 0.86f);
	const FLinearColor ColChoiceIdle(1.0f, 1.0f, 1.0f, 0.05f);
	const FLinearColor ColChoiceHover(0.62f, 0.10f, 0.12f, 0.55f);
	const FLinearColor ColSpeaker(0.80f, 0.18f, 0.18f, 1.0f);
	const FLinearColor ColLine(0.92f, 0.90f, 0.85f, 1.0f);
	const FLinearColor ColChoiceText(0.86f, 0.85f, 0.82f, 1.0f);
	const FLinearColor ColRule(0.80f, 0.18f, 0.18f, 0.5f);

	const FSlateBrush* WhiteBox() { return FCoreStyle::Get().GetBrush("GenericWhiteBox"); }
}

TOptional<int32> ElysiumDialogueUI::ChoiceForKey(
	const FKey& Key, int32 NumChoices, bool bTerminal)
{
	if (bTerminal || NumChoices == 0)
	{
		return Key == EKeys::SpaceBar || Key == EKeys::Enter || Key == EKeys::One
			? TOptional<int32>(-1) : TOptional<int32>();
	}

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

void SElysiumDialogueBox::Construct(const FArguments& InArgs)
{
	OnChooseEvent = InArgs._OnChoose;
	// A flat, chrome-free button whose only visible state is our own translucent row fill (idle -> a
	// faint white wash, hovered/pressed -> a blood tint), so the choice list reads as geometry. Held as
	// a member (ChoiceRowStyle) because SButton keeps the style by pointer for its lifetime.
	ChoiceRowStyle = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder");
	ChoiceRowStyle.SetNormal(*WhiteBox());
	ChoiceRowStyle.SetHovered(*WhiteBox());
	ChoiceRowStyle.SetPressed(*WhiteBox());
	ChoiceRowStyle.Normal.TintColor = FSlateColor(ColChoiceIdle);     // faint white wash at rest
	ChoiceRowStyle.Hovered.TintColor = FSlateColor(ColChoiceHover);   // blood tint under the cursor
	ChoiceRowStyle.Pressed.TintColor = FSlateColor(ColChoiceHover);
	ChoiceRowStyle.NormalForeground = FSlateColor(ColChoiceText);
	ChoiceRowStyle.HoveredForeground = FSlateColor(FLinearColor::White);
	ChoiceRowStyle.PressedForeground = FSlateColor(FLinearColor::White);
	SetDialogue(InArgs._Speaker, InArgs._Line, InArgs._Choices, InArgs._bTerminal);
}

void SElysiumDialogueBox::SetDialogue(const FString& Speaker, const FString& Line,
	const TArray<FString>& Choices, bool bInTerminal)
{
	NumChoices = Choices.Num();
	bTerminal = bInTerminal;
	RebuildDialogue(Speaker, Line, Choices);
}

void SElysiumDialogueBox::RebuildDialogue(const FString& Speaker, const FString& Line,
	const TArray<FString>& Choices)
{
	const FSlateFontInfo SpeakerFont = FCoreStyle::GetDefaultFontStyle(
		"Bold", ElysiumDialogueUI::SpeakerFontPoints);
	const FSlateFontInfo LineFont = FCoreStyle::GetDefaultFontStyle(
		"Regular", ElysiumDialogueUI::LineFontPoints);
	const FSlateFontInfo ChoiceFont = FCoreStyle::GetDefaultFontStyle(
		"Regular", ElysiumDialogueUI::ChoiceFontPoints);

	TSharedRef<SVerticalBox> Inner = SNew(SVerticalBox);

	// Speaker name.
	Inner->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
	[
		SNew(STextBlock)
		.Font(SpeakerFont)
		.ColorAndOpacity(FSlateColor(ColSpeaker))
		.Text(FText::FromString(Speaker.IsEmpty() ? TEXT("???") : Speaker))
	];

	// NPC subtitle (word-wrapped to the panel width).
	Inner->AddSlot().AutoHeight().Padding(0, 0, 0, 10)
	[
		SNew(STextBlock)
		.Font(LineFont)
		.ColorAndOpacity(FSlateColor(ColLine))
		.AutoWrapText(true)
		.Text(FText::FromString(Line))
	];

	// A thin rule between the line and the responses.
	Inner->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		SNew(SBox).HeightOverride(1.0f)
		[
			SNew(SBorder).BorderImage(WhiteBox()).BorderBackgroundColor(ColRule)
		]
	];

	if (bTerminal || NumChoices == 0)
	{
		// Terminal line — a single continue affordance ends the conversation.
		Inner->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SButton)
			.ButtonStyle(&ChoiceRowStyle)
			.ContentPadding(FMargin(10, 6))
			.HAlign(HAlign_Left)
			.OnClicked(FOnClicked::CreateSP(this, &SElysiumDialogueBox::Pick, -1))
			[
				SNew(STextBlock).Font(ChoiceFont).ColorAndOpacity(FSlateColor(ColChoiceText))
				.Text(FText::FromString(TEXT("[ Continue ]")))
			]
		];
	}
	else
	{
		// One numbered, clickable row per choice.
		for (int32 i = 0; i < NumChoices; ++i)
		{
			const FString Label = FString::Printf(TEXT("%d.  %s"), i + 1, *Choices[i]);
			Inner->AddSlot().AutoHeight().Padding(0, 2)
			[
				SNew(SButton)
				.ButtonStyle(&ChoiceRowStyle)
				.ContentPadding(FMargin(10, 6))
				.HAlign(HAlign_Left)
				.OnClicked(FOnClicked::CreateSP(this, &SElysiumDialogueBox::Pick, i))
				[
					SNew(STextBlock).Font(ChoiceFont).ColorAndOpacity(FSlateColor(ColChoiceText))
					.AutoWrapText(true)
					.Text(FText::FromString(Label))
				]
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

FReply SElysiumDialogueBox::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (const TOptional<int32> Choice = ElysiumDialogueUI::ChoiceForKey(
		KeyEvent.GetKey(), NumChoices, bTerminal))
	{
		return Pick(Choice.GetValue());
	}
	return FReply::Unhandled();
}

FReply SElysiumDialogueBox::Pick(int32 Index)
{
	OnChooseEvent.ExecuteIfBound(Index);
	return FReply::Handled();
}
