#include "UI/ElysiumChargenPopup.h"

#include "ElysiumContentPaths.h"
#include "Player/ElysiumCommandBus.h"
#include "UI/ElysiumUIStyle.h"
#include "UI/ElysiumUITexture.h"

#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumChargenPopup, Log, All);

namespace
{
	namespace Layout
	{
		// The authored `Region`/`TextRegion` are read as INTENT, not as a coordinate system
		// (`docs/project/remaster-direction.md` axis 1): the page is centred in the 1024x768 virtual canvas
		// and its text column takes the proportion the data asks for, rather than being placed at
		// literal pixels.
		inline constexpr float PageW      = 1024.0f;
		inline constexpr float PageH      = 768.0f;
		inline constexpr float TextInsetX = 190.0f;   // the shipped TextRegion's own left inset
		inline constexpr float TextTop    = 150.0f;
		inline constexpr float TextW      = 648.0f;
		inline constexpr float AnswerTop  = 40.0f;
		inline constexpr float AnswerGap  = 14.0f;
		inline constexpr float AnswerPadX = 30.0f;
	}

	// `Bkg_Image "Interface/Pop_Ups/Pop_Up_1"` -> `out/ui/art/interface/pop_ups/pop_up_1.png`. The
	// data spells it with the engine's own capitalisation and no extension.
	FString ArtRelFor(const FString& BkgImage)
	{
		return BkgImage.IsEmpty()
			? FString()
			: BkgImage.ToLower().Replace(TEXT("\\"), TEXT("/")) + TEXT(".png");
	}

	// The separator rule VtMB draws between multi-line answers.
	const TCHAR* GSeparator = TEXT("interface/pop_ups/pop_up_line.png");
}

UElysiumChargenPopup::UElysiumChargenPopup()
{
	bIsBackHandler = true;
}

bool UElysiumChargenPopup::NativeOnHandleBackAction()
{
	// The authored wizard has no backward edge. Consume Back so it cannot escape into gameplay or
	// open the pause menu behind the modal.
	return true;
}

float UElysiumChargenPopup::VirtualScale() const
{
	FVector2D Size(1920.0f, 1080.0f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	return ElysiumUI::ScaleFor(static_cast<float>(Size.Y));
}

void UElysiumChargenPopup::SetRun(TSharedPtr<FElysiumWizRun> InRun)
{
	Run = InRun;
	Refresh();
}

void UElysiumChargenPopup::Refresh()
{
	if (PageHost.IsValid())
	{
		PageHost->SetContent(BuildPage());
	}
}

const FSlateBrush* UElysiumChargenPopup::Art(const FString& RelPath)
{
	if (RelPath.IsEmpty() || ArtMissing.Contains(RelPath))
	{
		return nullptr;
	}
	if (const TSharedPtr<FSlateBrush>* Found = ArtBrushes.Find(RelPath))
	{
		return Found->Get();
	}

	TObjectPtr<UTexture2D>* Cached = ArtTextures.Find(RelPath);
	if (!Cached)
	{
		UTexture2D* Loaded = ElysiumUI::LoadPngTexture(FElysiumContentPaths::UiArt(RelPath));
		if (!Loaded)
		{
			ArtMissing.Add(RelPath);
			return nullptr;
		}
		Cached = &ArtTextures.Add(RelPath, Loaded);
	}

	TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
	Brush->SetResourceObject(*Cached);
	Brush->ImageSize = FVector2D((*Cached)->GetSizeX(), (*Cached)->GetSizeY());
	Brush->DrawAs = ESlateBrushDrawType::Image;
	ArtBrushes.Add(RelPath, Brush);
	return Brush.Get();
}

TSharedRef<SWidget> UElysiumChargenPopup::BuildPage()
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();

	const FElysiumWizPopup* Popup = Run.IsValid() ? Run->Popup : nullptr;
	if (Popup == nullptr)
	{
		return SNew(SSpacer);
	}

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).WidthOverride(Layout::TextW)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Popup->Text))
				.Font(Fonts.Font(EElysiumFontRole::Body, EElysiumFontWeight::Regular,
				                 ElysiumUI::Type::Body, 1.0f))
				.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Bone))
				.AutoWrapText(true)
			]
		];

	const FSlateBrush* Rule = Art(GSeparator);

	TSharedRef<SVerticalBox> Answers = SNew(SVerticalBox);
	for (int32 i = 0; i < Run->Choices.Num(); ++i)
	{
		const FElysiumWizAction& Action = *Run->Choices[i];
		// The answers are numbered in the data itself ("1. Male?"), so nothing is prefixed here —
		// doing so would double the number on every shipped question.
		Answers->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, Layout::AnswerGap)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FSlateColor(FLinearColor::Transparent))
			.Padding(FMargin(Layout::AnswerPadX, ElysiumUI::Space::XS))
			.OnMouseButtonDown_Lambda([this, i](const FGeometry&, const FPointerEvent&) -> FReply
			{
				OnAnswer.ExecuteIfBound(i);
				return FReply::Handled();
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Action.Text))
					.Font(Fonts.Font(EElysiumFontRole::Body, EElysiumFontWeight::Regular,
					                 ElysiumUI::Type::Label, 1.0f))
					.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::GoldLit))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, ElysiumUI::Space::XS, 0.0f, 0.0f)
				[
					Rule
						? StaticCastSharedRef<SWidget>(
							SNew(SBox).HeightOverride(3.0f)[ SNew(SImage).Image(Rule) ])
						: StaticCastSharedRef<SWidget>(
							SNew(SBox).HeightOverride(1.0f)
							[
								SNew(SImage)
								.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
								.ColorAndOpacity(FSlateColor(
									ElysiumUI::Palette::Amber.CopyWithNewOpacity(0.35f)))
							])
				]
			]
		];
	}

	Column->AddSlot().FillHeight(1.0f).Padding(0.0f, Layout::AnswerTop, 0.0f, 0.0f)
	[
		SNew(SScrollBox) + SScrollBox::Slot()[ Answers ]
	];

	return SNew(SBox)
		.Padding(FMargin(Layout::TextInsetX, Layout::TextTop, Layout::TextInsetX,
		                 ElysiumUI::Space::L))
		[
			Column
		];
}

TSharedRef<SWidget> UElysiumChargenPopup::RebuildWidget()
{
	(void)Super::RebuildWidget();

	ArtBrushes.Reset();

	TSharedRef<SOverlay> Root = SNew(SOverlay);
	// Black under everything: the popup covers the whole screen, and the page art is a bordered
	// frame with transparent middle rather than an opaque background.
	Root->AddSlot()
	[
		SNew(SImage)
		.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.01f, 0.01f, 0.012f, 1.0f)))
	];
	// Bound rather than set: successive questions can carry different page art, and the whole tree
	// is not rebuilt between them.
	Root->AddSlot()
	[
		SNew(SImage).Image_Lambda([this]() -> const FSlateBrush*
		{
			const FElysiumWizPopup* Current = Run.IsValid() ? Run->Popup : nullptr;
			return Current ? Art(ArtRelFor(Current->BkgImage)) : nullptr;
		})
	];
	Root->AddSlot()[ SAssignNew(PageHost, SBox)[ BuildPage() ] ];

	return SNew(SDPIScaler)
		.DPIScale_Lambda([this]() { return VirtualScale(); })
		[
			SNew(SBox).WidthOverride(Layout::PageW).HeightOverride(Layout::PageH)
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				Root
			]
		];
}

void UElysiumChargenPopup::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	ArtBrushes.Reset();
	PageHost.Reset();
}
