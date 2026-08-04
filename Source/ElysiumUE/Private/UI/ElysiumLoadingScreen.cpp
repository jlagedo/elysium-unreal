#include "UI/ElysiumLoadingScreen.h"

#include "UI/ElysiumUIStyle.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> ElysiumLoadingUI::Build(const FText& Message, bool bShowThrobber)
{
	const FSlateBrush* Solid = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 22);

	return SNew(SBorder)
		.BorderImage(Solid)
		.BorderBackgroundColor(FSlateColor(ElysiumUI::Palette::Ink))
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(0.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(48.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(Message)
					.Font(Font)
					.Justification(ETextJustify::Right)
					.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Gold))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SCircularThrobber)
					.NumPieces(8)
					.Radius(14.0f)
					.Visibility(bShowThrobber ? EVisibility::Visible : EVisibility::Collapsed)
					.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Blood))
				]
			]
		];
}

TSharedRef<SWidget> UElysiumLoadingScreen::RebuildWidget()
{
	return ElysiumLoadingUI::Build(Message, bShowThrobber);
}
