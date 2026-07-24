#include "ElysiumDialogueWidget.h"

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

void SElysiumDialogueBox::Construct(const FArguments& InArgs)
{
	OnChooseEvent = InArgs._OnChoose;
	NumChoices = InArgs._Choices.Num();
	bTerminal = InArgs._bTerminal;

	const FSlateFontInfo SpeakerFont = FCoreStyle::GetDefaultFontStyle("Bold", 22);
	const FSlateFontInfo LineFont = FCoreStyle::GetDefaultFontStyle("Regular", 20);
	const FSlateFontInfo ChoiceFont = FCoreStyle::GetDefaultFontStyle("Regular", 18);

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

	TSharedRef<SVerticalBox> Inner = SNew(SVerticalBox);

	// Speaker name.
	Inner->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
	[
		SNew(STextBlock)
		.Font(SpeakerFont)
		.ColorAndOpacity(FSlateColor(ColSpeaker))
		.Text(FText::FromString(InArgs._Speaker.IsEmpty() ? TEXT("???") : InArgs._Speaker))
	];

	// NPC subtitle (word-wrapped to the panel width).
	Inner->AddSlot().AutoHeight().Padding(0, 0, 0, 10)
	[
		SNew(STextBlock)
		.Font(LineFont)
		.ColorAndOpacity(FSlateColor(ColLine))
		.AutoWrapText(true)
		.Text(FText::FromString(InArgs._Line))
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
			const FString Label = FString::Printf(TEXT("%d.  %s"), i + 1, *InArgs._Choices[i]);
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

	// Dock the panel to the lower-centre of the screen, capped to a readable width, with the world
	// visible above it (VN framing). The outer fill slot pushes the panel to the bottom.
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.0f) [ SNullWidget::NullWidget ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 40)
		[
			SNew(SBox).WidthOverride(1100.0f)
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
	const FKey Key = KeyEvent.GetKey();

	if (bTerminal || NumChoices == 0)
	{
		if (Key == EKeys::SpaceBar || Key == EKeys::Enter || Key == EKeys::One)
		{
			return Pick(-1);
		}
		return FReply::Unhandled();
	}

	// Number keys 1..9 (top row and numpad) select the matching visible choice.
	static const FKey Row[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	static const FKey Pad[] = { EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour,
		EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
	for (int32 i = 0; i < NumChoices && i < 9; ++i)
	{
		if (Key == Row[i] || Key == Pad[i])
		{
			return Pick(i);
		}
	}
	return FReply::Unhandled();
}

FReply SElysiumDialogueBox::Pick(int32 Index)
{
	OnChooseEvent.ExecuteIfBound(Index);
	return FReply::Handled();
}
