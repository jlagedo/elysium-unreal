#include "UI/ElysiumHUDWidget.h"

#include "ElysiumContentPaths.h"
#include "ElysiumHUDModel.h"
#include "UI/ElysiumCommonUIInputData.h"
#include "UI/ElysiumUIStyle.h"
#include "UI/ElysiumUITexture.h"

#include "CommonInputSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumHUDWidget, Log, All);

namespace
{
	const FLinearColor HUDOutline(0.004f, 0.003f, 0.002f, 0.96f);
	constexpr int32 SelectorRowCapacity = 12;
	constexpr int32 RadialSlotCount = 8;
	constexpr float RadialBox = 500.0f;
	constexpr float RadialRadius = 160.0f;
	constexpr float RadialSlot = 80.0f;
	// Equipment sits above Life (38) + zone glyph (48) + gap (8) + label/bar (~44).
	constexpr float EquipmentBottomPad = 158.0f;

	int32 BriefEntryIndex(const FElysiumHUDSelectorView& Selector, int32 Offset)
	{
		const int32 Count = Selector.Entries.Num();
		if (Count <= 0 || Selector.SelectedIndex == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		return (Selector.SelectedIndex + Offset + Count) % Count;
	}

	FSlateFontInfo HUDFont(EElysiumFontRole Role, EElysiumFontWeight Weight,
		float VirtualSize, int32 OutlineSize = 1)
	{
		FSlateFontInfo Font = ElysiumUIFonts().Font(Role, Weight, VirtualSize, 1.0f);
		Font.OutlineSettings = FFontOutlineSettings(OutlineSize, HUDOutline);
		Font.OutlineSettings.bSeparateFillAlpha = true;
		return Font;
	}

	FSlateColor BloodPipColor(const UElysiumHUDModel* Model, int32 Index)
	{
		return Model && Index < Model->BloodPool
			? FSlateColor(ElysiumUI::Palette::BloodLit)
			: FSlateColor(FLinearColor(ElysiumUI::Palette::BoneDim.R,
				ElysiumUI::Palette::BoneDim.G, ElysiumUI::Palette::BoneDim.B, 0.45f));
	}

	TSharedRef<SWidget> BloodDroplet(UElysiumHUDModel* Model, int32 Index)
	{
		return SNew(SBox)
			.WidthOverride(12.0f)
			.HeightOverride(18.0f)
			.Visibility_Lambda([Model, Index]()
			{
				return Model && Index < Model->BloodCapacity
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				SNew(SOverlay)
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("●")))
					.Font(HUDFont(EElysiumFontRole::Data,
						EElysiumFontWeight::Regular, 10.0f))
					.ColorAndOpacity_Lambda([Model, Index]() { return BloodPipColor(Model, Index); })
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 1)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("▼")))
					.Font(HUDFont(EElysiumFontRole::Data,
						EElysiumFontWeight::Regular, 7.0f))
					.ColorAndOpacity_Lambda([Model, Index]() { return BloodPipColor(Model, Index); })
				]
			];
	}

	// The browsed category's own name where the publisher supplied one — the inventory selector
	// carries `items.txt`'s authored section name, so the heading says "Weapon (Ranged)" rather than
	// a coarser word the file never uses. The kind's name is the fallback for the selectors that
	// have no section behind them.
	FText SelectorHeading(const FElysiumHUDSelectorView& Selector)
	{
		if (!Selector.Heading.IsEmpty())
		{
			return FText::FromString(Selector.Heading.ToString().ToUpper());
		}
		switch (Selector.Type)
		{
		case EElysiumHUDSelector::Weapons:     return FText::FromString(TEXT("WEAPONS"));
		case EElysiumHUDSelector::Disciplines: return FText::FromString(TEXT("DISCIPLINES"));
		case EElysiumHUDSelector::Inventory:   return FText::FromString(TEXT("INVENTORY"));
		default:                               return FText::GetEmpty();
		}
	}
}

TSharedRef<SWidget> UElysiumHUDWidget::RebuildWidget()
{
	EnsureUseIconAtlas();
	EnsureContrastVeils();
	UElysiumHUDModel* M = Model;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FSlateFontInfo Label = HUDFont(
		EElysiumFontRole::Label, EElysiumFontWeight::SemiBold, ElysiumUI::Type::Label);
	const FSlateFontInfo Data = HUDFont(
		EElysiumFontRole::Data, EElysiumFontWeight::SemiBold, ElysiumUI::Type::Body);
	const FSlateFontInfo Caption = HUDFont(
		EElysiumFontRole::Data, EElysiumFontWeight::Regular, ElysiumUI::Type::Caption);

	TSharedRef<SHorizontalBox> BloodRow = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < 15; ++Index)
	{
		if (Index == 5 || Index == 10)
		{
			BloodRow->AddSlot().AutoWidth().Padding(3, 0)[SNew(SSpacer).Size(FVector2D(1, 1))];
		}
		BloodRow->AddSlot().AutoWidth().Padding(1, 0)[BloodDroplet(M, Index)];
	}

	TSharedRef<SHorizontalBox> BriefModeRow = SNew(SHorizontalBox);
	for (int32 Offset = -1; Offset <= 1; ++Offset)
	{
		BriefModeRow->AddSlot().AutoWidth().Padding(4, 0)
		[
			SNew(SBorder)
			.BorderImage(White)
			.BorderBackgroundColor_Lambda([M, Offset]()
			{
				return M && Offset == 0
					? FLinearColor(ElysiumUI::Palette::Blood.R, ElysiumUI::Palette::Blood.G,
						ElysiumUI::Palette::Blood.B, 0.72f * M->Selector.Alpha)
					: FLinearColor::Transparent;
			})
			.Visibility_Lambda([M, Offset]()
			{
				if (!M || !M->Selector.bBriefMode) return EVisibility::Collapsed;
				const int32 TargetIndex = BriefEntryIndex(M->Selector, Offset);
				if (TargetIndex == INDEX_NONE) return EVisibility::Collapsed;
				// A one- or two-entry list must not repeat the selected icon in the side slots.
				if (Offset != 0 && TargetIndex == M->Selector.SelectedIndex)
				{
					return EVisibility::Hidden;
				}
				return EVisibility::HitTestInvisible;
			})
			.Padding(4)
			[
				SNew(SBox).WidthOverride(64.0f).HeightOverride(64.0f)
				[
					SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
					[
						SNew(SImage)
						// The peek fades as one: the icons ride the same alpha the highlight does.
						.ColorAndOpacity_Lambda([M]()
						{
							return FLinearColor(1.0f, 1.0f, 1.0f, M ? M->Selector.Alpha : 0.0f);
						})
						.Image_Lambda([this, M, Offset]() -> const FSlateBrush*
						{
							if (!M) return nullptr;
							const int32 TargetIndex = BriefEntryIndex(M->Selector, Offset);
							return M->Selector.Entries.IsValidIndex(TargetIndex)
								? HudArtBrush(M->Selector.Entries[TargetIndex].Icon) : nullptr;
						})
					]
				]
			]
		];
	}

	TSharedRef<SVerticalBox> SelectorRows = SNew(SVerticalBox);
	for (int32 Index = 0; Index < SelectorRowCapacity; ++Index)
	{
		SelectorRows->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SBorder)
			.BorderImage(White)
			.BorderBackgroundColor_Lambda([M, Index]()
			{
				return M && M->Selector.SelectedIndex == Index
					? FLinearColor(ElysiumUI::Palette::Blood.R, ElysiumUI::Palette::Blood.G,
						ElysiumUI::Palette::Blood.B, 0.72f)
					: FLinearColor::Transparent;
			})
			.Visibility_Lambda([M, Index]()
			{
				return M && !M->Selector.bBriefMode && M->Selector.Entries.IsValidIndex(Index)
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			.Padding(FMargin(10, 6))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 12, 0))
				[
					SNew(SBox).HeightOverride(32.0f)
					.Visibility_Lambda([this, M, Index]()
					{
						return M && M->Selector.Entries.IsValidIndex(Index)
							&& HudArtBrush(M->Selector.Entries[Index].Icon)
							? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					[
						SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
						[
							SNew(SImage)
							.Image_Lambda([this, M, Index]() -> const FSlateBrush*
							{
								return M && M->Selector.Entries.IsValidIndex(Index)
									? HudArtBrush(M->Selector.Entries[Index].Icon) : nullptr;
							})
							.ColorAndOpacity_Lambda([M, Index]()
							{
								const bool bEnabled = !M || !M->Selector.Entries.IsValidIndex(Index)
									|| M->Selector.Entries[Index].bEnabled;
								return FLinearColor(1, 1, 1, bEnabled ? 1.0f : 0.4f);
							})
						]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(Label)
					.ColorAndOpacity_Lambda([M, Index]()
					{
						const bool bEnabled = !M || !M->Selector.Entries.IsValidIndex(Index)
							|| M->Selector.Entries[Index].bEnabled;
						return bEnabled ? ElysiumUI::Palette::Bone : ElysiumUI::Palette::Disabled;
					})
					.Text_Lambda([M, Index]()
					{
						return M && M->Selector.Entries.IsValidIndex(Index)
							? M->Selector.Entries[Index].Label : FText::GetEmpty();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(16, 0, 0, 0).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(Caption)
					.ColorAndOpacity(ElysiumUI::Palette::BoneDim)
					.Visibility_Lambda([M, Index]()
					{
						return M && M->Selector.Entries.IsValidIndex(Index)
							&& !M->Selector.Entries[Index].Detail.IsEmpty()
							? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					.Text_Lambda([M, Index]()
					{
						return M && M->Selector.Entries.IsValidIndex(Index)
							? M->Selector.Entries[Index].Detail : FText::GetEmpty();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(16, 0, 0, 0).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(Caption)
					.ColorAndOpacity(ElysiumUI::Palette::Bone)
					.Visibility_Lambda([M, Index]()
					{
						return M && M->Selector.Entries.IsValidIndex(Index)
							&& M->Selector.Entries[Index].Quantity > 0
							? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					.Text_Lambda([M, Index]()
					{
						return M && M->Selector.Entries.IsValidIndex(Index)
							&& M->Selector.Entries[Index].Quantity > 0
							? FText::AsNumber(M->Selector.Entries[Index].Quantity) : FText::GetEmpty();
					})
				]
			]
		];
	}

	TSharedRef<SOverlay> Content = SNew(SOverlay)
		.Visibility_Lambda([M]() { return M && M->bVisible
			? EVisibility::HitTestInvisible : EVisibility::Collapsed; });

	// Static local contrast beds beat scene-luminance sampling: no flicker, no palette inversion,
	// and only the corners occupied by the always-on meters pay the darkening cost.
	Content->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Bottom)
	[
		SNew(SBox).WidthOverride(430).HeightOverride(150)
		[
			SNew(SImage).Image(&LeftContrastBrush)
		]
	];
	Content->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
	[
		SNew(SBox).WidthOverride(430).HeightOverride(150)
		[
			SNew(SImage).Image(&RightContrastBrush)
		]
	];

	// Life: compact and continuous. Exact values remain visible for accessibility and testing.
	Content->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(FMargin(38, 0, 0, 38))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(SBox).HeightOverride(48.0f)
			.Visibility_Lambda([M]() { return M && M->ZoneState != EElysiumZoneState::None
				? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
				.HAlign(HAlign_Left)
				[
					SNew(SImage)
					.Image_Lambda([this, M]() -> const FSlateBrush*
					{
						return M ? HudArtBrush(ElysiumHUDArt::Area(M->ZoneState)) : nullptr;
					})
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("LIFE"))).Font(Label)
				.ColorAndOpacity(ElysiumUI::Palette::Gold)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Font(Data).ColorAndOpacity(ElysiumUI::Palette::Bone)
				.Text_Lambda([M]()
				{
					return M && M->bVitalsValid
						? FText::FromString(FString::Printf(TEXT("%d / %d"), M->Health, M->MaxHealth))
						: FText::GetEmpty();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
		[
			SNew(SBox).WidthOverride(240).HeightOverride(10)
			[
				SNew(SBorder).BorderImage(White).BorderBackgroundColor(HUDOutline).Padding(1)
				[
					SNew(SProgressBar)
					.Percent_Lambda([M]() -> TOptional<float>
					{
						return M && M->MaxHealth > 0
							? FMath::Clamp(float(M->Health) / float(M->MaxHealth), 0.0f, 1.0f) : 0.0f;
					})
					.FillColorAndOpacity_Lambda([M]()
					{
						return M && M->MaxHealth > 0 && M->Health * 4 <= M->MaxHealth
							? ElysiumUI::Palette::BloodLit : ElysiumUI::Palette::Bone;
					})
				]
			]
		]
	];

	// Vitae: capacity is discrete, so each readable droplet is one banked blood point.
	Content->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0, 0, 38, 34)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[BloodRow]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
			[
				SNew(STextBlock).Font(Data).ColorAndOpacity(ElysiumUI::Palette::Bone)
				.Text_Lambda([M]() { return M && M->bVitalsValid
					? FText::AsNumber(M->BloodPool) : FText::GetEmpty(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 3, 0, 0)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("VITAE"))).Font(Label)
			.ColorAndOpacity(ElysiumUI::Palette::Gold)
		]
	];

	// The focused victim's blood pool. Retail places this as a single horizontal bar at the top and
	// keeps the side HUD visible; the remaster preserves that screen structure with vector/SafeZone
	// composition. Its value is a projection only — the substrate pulse clock remains authoritative.
	Content->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 38, 0, 0)
	[
		SNew(SBox).WidthOverride(500).HeightOverride(14)
		.Visibility_Lambda([M]() { return M && M->bFeedVictimVisible
			? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(HUDOutline).Padding(2)
			[
				SNew(SProgressBar)
				.Percent_Lambda([M]() -> TOptional<float>
				{
					return M && M->FeedVictimBloodCapacity > 0
						? FMath::Clamp(float(M->FeedVictimBlood)
							/ float(M->FeedVictimBloodCapacity), 0.0f, 1.0f)
						: 0.0f;
				})
				.FillColorAndOpacity(ElysiumUI::Palette::BloodLit)
			]
		]
	];

	// The two standings. They sit together, out of the action corners and permanently legible:
	// Masquerade is a five-mark countdown whose exhaustion ends the run, and Humanity is the
	// frenzy input. Neither changes often, and both matter when they do.
	TSharedRef<SHorizontalBox> MasqueradePips = SNew(SHorizontalBox);
	for (int32 Mark = 0; Mark < ElysiumHUDArt::MasqueradeMarks; ++Mark)
	{
		MasqueradePips->AddSlot().AutoWidth().Padding(3, 0)
		[
			SNew(SBox).WidthOverride(14.0f).HeightOverride(14.0f)
			[
				SNew(SOverlay)
				// The mark itself. The sheet slot counts violations up, so the marks still held are
				// the ones past the current level.
				+ SOverlay::Slot()
				[
					SNew(SImage).Image(White)
					.ColorAndOpacity_Lambda([M, Mark]()
					{
						if (!M || !M->bVitalsValid) return FLinearColor::Transparent;
						return Mark >= M->Masquerade
							? ElysiumUI::Palette::Bone
							: FLinearColor(ElysiumUI::Palette::Ink.R, ElysiumUI::Palette::Ink.G,
								ElysiumUI::Palette::Ink.B, 0.65f);
					})
				]
				// The strike over a mark already lost, in the same blood the low-life bar takes.
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(16.0f).HeightOverride(3.0f)
					.Visibility_Lambda([M, Mark]()
					{
						return M && M->bVitalsValid && Mark < M->Masquerade
							? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					[
						SNew(SImage).Image(White).ColorAndOpacity(ElysiumUI::Palette::BloodLit)
					]
				]
			]
		];
	}

	Content->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 34, 38, 0)
	[
		SNew(SVerticalBox)
		.Visibility_Lambda([M]() { return M && M->bVitalsValid
			? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 10, 0))
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("MASQUERADE"))).Font(Label)
				.ColorAndOpacity(ElysiumUI::Palette::Gold)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MasqueradePips]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 6, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 10, 0))
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("HUMANITY"))).Font(Label)
				.ColorAndOpacity(ElysiumUI::Palette::Gold)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				// A ten-point scale reads faster as a value than as ten pips, and the colour carries
				// the part that matters: the lower it goes, the likelier the Beast takes over.
				SNew(STextBlock).Font(Data)
				.Text_Lambda([M]()
				{
					return M ? FText::AsNumber(M->Humanity) : FText::GetEmpty();
				})
				.ColorAndOpacity_Lambda([M]()
				{
					if (!M) return ElysiumUI::Palette::Bone;
					if (M->Humanity <= 2) return ElysiumUI::Palette::BloodLit;
					return M->Humanity <= 4 ? ElysiumUI::Palette::Amber : ElysiumUI::Palette::Bone;
				})
			]
		]
	];

	// Equipment and discipline regions collapse until their real gameplay owners publish data.

	TSharedRef<SHorizontalBox> EquipBox = SNew(SHorizontalBox)
		.Visibility_Lambda([M]() { return M && M->Equipment.bValid
			? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 8, 0))
		[
			SNew(SBox).WidthOverride(24.0f).HeightOverride(24.0f)
			.Visibility_Lambda([this, M]()
			{
				return M && HudArtBrush(ElysiumHUDArt::Category(M->Equipment.WeaponClass))
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
				[
					SNew(SImage)
					.Image_Lambda([this, M]() -> const FSlateBrush*
					{
						return M ? HudArtBrush(ElysiumHUDArt::Category(M->Equipment.WeaponClass)) : nullptr;
					})
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 12, 0))
		[
			SNew(SBox).HeightOverride(48.0f)
			.Visibility_Lambda([this, M]()
			{
				return M && HudArtBrush(M->Equipment.Icon)
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
				[
					SNew(SImage)
					.Image_Lambda([this, M]() -> const FSlateBrush*
					{
						return M ? HudArtBrush(M->Equipment.Icon) : nullptr;
					})
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(Label).ColorAndOpacity(ElysiumUI::Palette::Bone)
			.Text_Lambda([M]()
			{
				if (!M) return FText::GetEmpty();
				if (ElysiumHUDArt::ShowsAmmo(M->Equipment.WeaponClass))
				{
					return FText::FromString(FString::Printf(TEXT("%s    %d / %d"),
						*M->Equipment.Name.ToString(), M->Equipment.AmmoCurrent, M->Equipment.AmmoReserve));
				}
				return M->Equipment.Name;
			})
		];

	Content->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(FMargin(38, 0, 0, EquipmentBottomPad))
	[
		EquipBox
	];

	// What the player is wearing, beside the hand. Persistent and independent of the browsed
	// category, the way retail keeps the worn clothing on screen rather than inside the selector.
	// Its own art is a clan/sex/tier portrait this runtime cannot yet resolve, so the row leads with
	// the category glyph and names the garment.
	Content->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Bottom)
		.Padding(FMargin(38, 0, 0, EquipmentBottomPad + 44.0f))
	[
		SNew(SHorizontalBox)
		.Visibility_Lambda([M]() { return M && M->Worn.bValid
			? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 8, 0))
		[
			SNew(SBox).WidthOverride(28.0f).HeightOverride(28.0f)
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
				[
					SNew(SImage)
					.Image_Lambda([this, M]() -> const FSlateBrush*
					{
						return M ? HudArtBrush(M->Worn.Icon) : nullptr;
					})
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(Caption).ColorAndOpacity(ElysiumUI::Palette::BoneDim)
			.Text_Lambda([M]() { return M ? M->Worn.Name : FText::GetEmpty(); })
		]
	];

	Content->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(FMargin(0, 0, 38, 101))
	[
		SNew(SHorizontalBox)
		.Visibility_Lambda([M]() { return M && M->Discipline.bValid
			? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 10, 0))
		[
			SNew(SBox).HeightOverride(32.0f)
			.Visibility_Lambda([this, M]()
			{
				return M && HudArtBrush(M->Discipline.Icon)
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
				[
					SNew(SImage)
					.Image_Lambda([this, M]() -> const FSlateBrush*
					{
						return M ? HudArtBrush(M->Discipline.Icon) : nullptr;
					})
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(Label).ColorAndOpacity(ElysiumUI::Palette::Bone)
			.Text_Lambda([M]() { return M ? M->Discipline.Name : FText::GetEmpty(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(12, 0, 0, 0))
		[
			SNew(STextBlock).Font(Caption).ColorAndOpacity(ElysiumUI::Palette::BoneDim)
			.Visibility_Lambda([M]()
			{
				return M && M->Discipline.BloodCost > 0
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			.Text_Lambda([M]()
			{
				return M && M->Discipline.BloodCost > 0
					? FText::FromString(FString::Printf(TEXT("%d blood"), M->Discipline.BloodCost))
					: FText::GetEmpty();
			})
		]
	];

	Content->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(38, 0, 0, 0)
	[
		SNew(SBox).WidthOverride(380)
		.Visibility_Lambda([M]() { return M && M->Selector.IsOpen() && M->Selector.Type != EElysiumHUDSelector::Radial
			? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBorder).BorderImage(White)
				.BorderBackgroundColor(FLinearColor(ElysiumUI::Palette::Ink.R, ElysiumUI::Palette::Ink.G, ElysiumUI::Palette::Ink.B, 0.92f))
				.Visibility_Lambda([M]() { return M && M->Selector.bBriefMode ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				.Padding(12)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
					[
						SNew(STextBlock).Font(Label).ColorAndOpacity(ElysiumUI::Palette::GoldLit)
						.Text_Lambda([M]() { return M ? SelectorHeading(M->Selector) : FText::GetEmpty(); })
					]
					+ SVerticalBox::Slot().AutoHeight()[SelectorRows]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				BriefModeRow
			]
		]
	];

	// The stealth cluster. Situational by design: it is on screen only while the player is
	// crouched, so a readout about being unseen never competes with the ordinary walking HUD.
	// Bottom-centre, under the reticle, because it is the one thing being read continuously while
	// sneaking.
	TSharedRef<SHorizontalBox> ConcealmentSteps = SNew(SHorizontalBox);
	for (int32 Step = 0; Step < ElysiumHUDArt::ConcealmentSteps; ++Step)
	{
		ConcealmentSteps->AddSlot().AutoWidth().Padding(2, 0)
		[
			SNew(SBox).WidthOverride(16.0f).HeightOverride(10.0f)
			[
				SNew(SBorder).BorderImage(White).BorderBackgroundColor(HUDOutline).Padding(1)
				[
					SNew(SImage).Image(White)
					.ColorAndOpacity_Lambda([M, Step]()
					{
						// Unmeasured draws every step empty. An unmeasured gauge and a gauge reading
						// zero are different statements and must not look the same.
						if (!M || !M->Stealth.bConcealmentValid) return FLinearColor::Transparent;
						return Step <= M->Stealth.ConcealmentStep
							? ElysiumUI::Palette::Cyan : FLinearColor::Transparent;
					})
				]
			]
		];
	}

	Content->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0, 0, 0, 96))
	[
		SNew(SBorder).BorderImage(White)
		.BorderBackgroundColor(FLinearColor(ElysiumUI::Palette::Ink.R, ElysiumUI::Palette::Ink.G,
			ElysiumUI::Palette::Ink.B, 0.72f))
		.Visibility_Lambda([M]() { return M && M->Stealth.bSneaking
			? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		.Padding(FMargin(14, 8))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 12, 0))
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("SNEAKING"))).Font(Label)
					.ColorAndOpacity(ElysiumUI::Palette::GoldLit)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ConcealmentSteps]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(10, 0, 0, 0))
				[
					// The gauge names its own absence rather than letting an empty row read as light.
					SNew(STextBlock).Font(Caption).ColorAndOpacity(ElysiumUI::Palette::Disabled)
					.Text(FText::FromString(TEXT("--")))
					.Visibility_Lambda([M]() { return M && !M->Stealth.bConcealmentValid
						? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				]
			]
			// The observer line. An absent observer clears it: nobody eligible is looking, which is
			// an ordinary state and not a failure to report.
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 5, 0, 0)
			[
				SNew(STextBlock).Font(Caption)
				.Visibility_Lambda([M]() { return M && M->Stealth.bObserverValid
					? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.ColorAndOpacity_Lambda([M]()
				{
					if (!M) return ElysiumUI::Palette::Bone;
					switch (M->Stealth.Detection)
					{
					case EElysiumHUDDetection::Detected:  return ElysiumUI::Palette::BloodLit;
					case EElysiumHUDDetection::Searching: return ElysiumUI::Palette::Amber;
					default:                              return ElysiumUI::Palette::Bone;
					}
				})
				.Text_Lambda([M]()
				{
					if (!M || !M->Stealth.bObserverValid) return FText::GetEmpty();
					const TCHAR* State = TEXT("UNAWARE");
					switch (M->Stealth.Detection)
					{
					case EElysiumHUDDetection::Detected:  State = TEXT("SPOTTED"); break;
					case EElysiumHUDDetection::Searching: State = TEXT("SEARCHING"); break;
					default: break;
					}
					return FText::FromString(FString::Printf(TEXT("%s  %.0fm"),
						State, M->Stealth.ObserverDistanceMetres));
				})
			]
		]
	];

	TSharedRef<SCanvas> RadialCanvas = SNew(SCanvas);
	for (int32 Index = 0; Index < RadialSlotCount; ++Index)
	{
		RadialCanvas->AddSlot()
		.Position_Lambda([Index]()
		{
			const float Angle = (float(Index) / float(RadialSlotCount)) * 2.0f * UE_PI - UE_PI / 2.0f;
			return FVector2D(
				RadialBox * 0.5f + FMath::Cos(Angle) * RadialRadius,
				RadialBox * 0.5f + FMath::Sin(Angle) * RadialRadius);
		})
		.Size(FVector2D(RadialSlot, RadialSlot))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(White)
			.BorderBackgroundColor_Lambda([M, Index]()
			{
				return M && M->Selector.SelectedIndex == Index
					? FLinearColor(ElysiumUI::Palette::Blood.R, ElysiumUI::Palette::Blood.G, ElysiumUI::Palette::Blood.B, 0.9f)
					: FLinearColor(0, 0, 0, 0.6f);
			})
			.Visibility_Lambda([M, Index]()
			{
				return M && M->Selector.Entries.IsValidIndex(Index) ? EVisibility::HitTestInvisible : EVisibility::Hidden;
			})
			.Padding(8)
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
				[
					SNew(SImage)
					.Image_Lambda([this, M, Index]() -> const FSlateBrush*
					{
						return M && M->Selector.Entries.IsValidIndex(Index)
							? HudArtBrush(M->Selector.Entries[Index].Icon) : nullptr;
					})
					.ColorAndOpacity_Lambda([M, Index]()
					{
						const bool bEnabled = !M || !M->Selector.Entries.IsValidIndex(Index)
							|| M->Selector.Entries[Index].bEnabled;
						return FLinearColor(1, 1, 1, bEnabled ? 1.0f : 0.4f);
					})
				]
			]
		];
	}

	TSharedRef<SOverlay> RadialOverlay = SNew(SOverlay)
		.Visibility_Lambda([M]() { return M && M->Selector.IsOpen() && M->Selector.Type == EElysiumHUDSelector::Radial
			? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(RadialBox).HeightOverride(RadialBox)
			[
				RadialCanvas
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(200)
			[
				SNew(STextBlock).Font(Label).ColorAndOpacity(ElysiumUI::Palette::Bone).Justification(ETextJustify::Center)
				.Text_Lambda([M]()
				{
					if (!M || !M->Selector.Entries.IsValidIndex(M->Selector.SelectedIndex)) return FText::GetEmpty();
					return M->Selector.Entries[M->Selector.SelectedIndex].Label;
				})
			]
		];

	Content->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		RadialOverlay
	];

	// Aim cursor. Exported original use-icon cells remain the only game-authored icon dependency;
	// the semantic Use binding follows CommonInput's active device and always has a text fallback.
	Content->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("+")))
			.Font(HUDFont(EElysiumFontRole::Data, EElysiumFontWeight::Regular, 18, 2))
			.ColorAndOpacity(FLinearColor(1, 1, 1, 0.72f))
			.Visibility_Lambda([M, this]()
			{
				if (!M || M->Reticle == EElysiumHUDReticle::None) return EVisibility::Collapsed;
				return M->Reticle == EElysiumHUDReticle::Cross || !UseIconBrush()
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
		]
		// The third-person path: the plain white reticle at the crosshair rect. Retail switches to it
		// on the first frame of the mode change and runs no use-icon or arrow cursor there
		// (`docs/vtmb/camera-view-modes.md` §5, `0x1009b9e0`), so this slot carries no ring, no atlas
		// cell and no prompt — it is deliberately the whole of the third-person crosshair.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("+")))
			.Font(HUDFont(EElysiumFontRole::Data, EElysiumFontWeight::Regular, 18, 2))
			.ColorAndOpacity(FLinearColor::White)
			.Visibility_Lambda([M]()
			{
				return M && M->Reticle == EElysiumHUDReticle::ThirdPerson
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(48).HeightOverride(48)
			.Visibility_Lambda([M, this]() { return M && M->Reticle == EElysiumHUDReticle::UseIcon && UseIconBrush()
				? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SOverlay)
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(52).HeightOverride(52)
					[
						SNew(SImage).Image_Lambda([this]() { return UseIconBrush(); })
						.ColorAndOpacity_Lambda([M]()
						{
							return FLinearColor(HUDOutline.R, HUDOutline.G, HUDOutline.B,
								HUDOutline.A * (M ? M->UsePromptAlpha : 0.0f));
						})
					]
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(52).HeightOverride(52)
					[
						SNew(SImage).Image(&UseRingBrush).ColorAndOpacity_Lambda([M]()
						{
							return FLinearColor(HUDOutline.R, HUDOutline.G, HUDOutline.B,
								HUDOutline.A * (M ? M->UsePromptAlpha : 0.0f));
						})
					]
				]
				+ SOverlay::Slot()
				[
					SNew(SImage).Image_Lambda([this]() { return UseIconBrush(); })
					.ColorAndOpacity_Lambda([M]()
					{
						return FLinearColor(1, 1, 1, M ? M->UsePromptAlpha : 0.0f);
					})
				]
				+ SOverlay::Slot()
				[
					SNew(SImage).Image(&UseRingBrush)
					.ColorAndOpacity_Lambda([M]()
					{
						return FLinearColor(1, 1, 1, M ? M->UsePromptAlpha : 0.0f);
					})
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0, 70, 0, 0)
		[
			SNew(SBox).WidthOverride(36).HeightOverride(24)
			.Visibility_Lambda([M]() { return M && M->UsePromptAlpha > KINDA_SMALL_NUMBER
				? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SOverlay)
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image_Lambda([this]() { return UseBindingBrush(); })
					.Visibility_Lambda([this]() { return UseBindingBrush()
						? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					.ColorAndOpacity_Lambda([M]() { return FLinearColor(1, 1, 1,
						M ? M->UsePromptAlpha : 0.0f); })
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(Caption)
					.Text_Lambda([this]() { return UseBindingText(); })
					.Visibility_Lambda([this]() { return UseBindingBrush()
						? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
					.ColorAndOpacity_Lambda([M]()
					{
						const float Alpha = M ? M->UsePromptAlpha : 0.0f;
						const FLinearColor Base = M && M->bUseLocked
							? ElysiumUI::Palette::BloodLit : ElysiumUI::Palette::Bone;
						return FLinearColor(Base.R, Base.G, Base.B, Base.A * Alpha);
					})
				]
			]
		]
	];

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).IsTitleSafe(false)
			[
				SNew(SDPIScaler).DPIScale_Lambda([this]() { return VirtualScale(); })[Content]
			]
		]
		// Fade is intentionally outside the safe zone and above the complete player HUD.
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(White)
			.BorderBackgroundColor_Lambda([M]() { return M ? M->Fade : FLinearColor::Transparent; })
			.Visibility_Lambda([M]() { return M && M->Fade.A > KINDA_SMALL_NUMBER
				? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		];
}

void UElysiumHUDWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
}

float UElysiumHUDWidget::VirtualScale() const
{
	FVector2D Size(0, ElysiumUI::VirtualH);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	return ElysiumUI::ScaleFor(Size.Y);
}

FText UElysiumHUDWidget::UseBindingText() const
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const UCommonInputSubsystem* CommonInput = LocalPlayer
		? LocalPlayer->GetSubsystem<UCommonInputSubsystem>() : nullptr;
	const bool bGamepad = CommonInput
		&& CommonInput->GetCurrentInputType() == ECommonInputType::Gamepad;
	const FDataTableRowHandle& Handle = GetDefault<UElysiumCommonUIInputData>()->GetUseAction();
	const FElysiumCommonInputActionData* Action = Handle.GetRow<FElysiumCommonInputActionData>(
		TEXT("HUD Use prompt"));
	if (Action && CommonInput)
	{
		const FKey Key = Action->GetCurrentInputTypeInfo(CommonInput).GetKey();
		if (Key == EKeys::E || Key == EKeys::Gamepad_RightTriggerAxis)
		{
			return ElysiumInteraction::UseBindingText(bGamepad);
		}
		if (Key.IsValid())
		{
			return Key.GetDisplayName();
		}
	}
	return ElysiumInteraction::UseBindingText(bGamepad);
}

const FSlateBrush* UElysiumHUDWidget::UseBindingBrush() const
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const UCommonInputSubsystem* CommonInput = LocalPlayer
		? LocalPlayer->GetSubsystem<UCommonInputSubsystem>() : nullptr;
	const FDataTableRowHandle& Handle = GetDefault<UElysiumCommonUIInputData>()->GetUseAction();
	const FElysiumCommonInputActionData* Action = Handle.GetRow<FElysiumCommonInputActionData>(
		TEXT("HUD Use prompt"));
	if (!Action || !CommonInput)
	{
		return nullptr;
	}
	CurrentUseBindingBrush = Action->GetCurrentInputActionIcon(CommonInput);
	return CurrentUseBindingBrush.GetResourceObject() ? &CurrentUseBindingBrush : nullptr;
}

void UElysiumHUDWidget::EnsureUseIconAtlas()
{
	if (bUseAtlasLoadAttempted)
	{
		return;
	}
	bUseAtlasLoadAttempted = true;
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText,
		*(FElysiumContentPaths::Root() / TEXT("hud/use_icons.json"))))
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}
	UseAtlas = ElysiumUI::LoadPngTexture(FElysiumContentPaths::Root() / TEXT("hud/use_icons.png"));
	if (!UseAtlas)
	{
		return;
	}
	auto Configure = [this](FSlateBrush& Brush, const TSharedPtr<FJsonObject>& Object)
	{
		Brush.SetResourceObject(UseAtlas);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.ImageSize = FVector2D(48, 48);
		Brush.SetUVRegion(FBox2f(
			FVector2f(Object->GetNumberField(TEXT("u0")), Object->GetNumberField(TEXT("v0"))),
			FVector2f(Object->GetNumberField(TEXT("u1")), Object->GetNumberField(TEXT("v1")))));
	};
	if (const TSharedPtr<FJsonObject>* Ring; Root->TryGetObjectField(TEXT("ring"), Ring))
	{
		Configure(UseRingBrush, *Ring);
	}
	const TArray<TSharedPtr<FJsonValue>>* Icons = nullptr;
	if (Root->TryGetArrayField(TEXT("icons"), Icons))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Icons)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			if (Object)
			{
				FSlateBrush& Brush = UseIconBrushes.Add((int32)Object->GetNumberField(TEXT("n")));
				Configure(Brush, Object);
			}
		}
	}
}

const FSlateBrush* UElysiumHUDWidget::UseIconBrush() const
{
	return Model ? UseIconBrushes.Find(Model->UseIcon) : nullptr;
}

void UElysiumHUDWidget::EnsureContrastVeils()
{
	if (bContrastVeilsBuilt)
	{
		return;
	}
	bContrastVeilsBuilt = true;

	constexpr int32 Width = 32;
	constexpr int32 Height = 16;
	TArray<uint8> LeftAlpha;
	TArray<uint8> RightAlpha;
	LeftAlpha.SetNumUninitialized(Width * Height);
	RightAlpha.SetNumUninitialized(Width * Height);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		const float Vertical = FMath::Square(float(Y) / float(Height - 1));
		for (int32 X = 0; X < Width; ++X)
		{
			const float Left = FMath::Square(1.0f - float(X) / float(Width - 1));
			const float Right = FMath::Square(float(X) / float(Width - 1));
			LeftAlpha[Y * Width + X] = uint8(FMath::RoundToInt(96.0f * Left * Vertical));
			RightAlpha[Y * Width + X] = uint8(FMath::RoundToInt(96.0f * Right * Vertical));
		}
	}

	LeftContrastVeil = ElysiumUI::MakeAlphaRamp(LeftAlpha, Width, Height);
	RightContrastVeil = ElysiumUI::MakeAlphaRamp(RightAlpha, Width, Height);
	auto Configure = [](FSlateBrush& Brush, UTexture2D* Texture)
	{
		Brush.SetResourceObject(Texture);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.ImageSize = FVector2D(1, 1);
		Brush.TintColor = FSlateColor(FLinearColor::Black);
	};
	Configure(LeftContrastBrush, LeftContrastVeil);
	Configure(RightContrastBrush, RightContrastVeil);
}

const FSlateBrush* UElysiumHUDWidget::HudArtBrush(FName ArtPath)
{
	if (ArtPath.IsNone())
	{
		return nullptr;
	}
	if (const FSlateBrush* Found = HudArtBrushes.Find(ArtPath))
	{
		return Found->GetResourceObject() ? Found : nullptr;
	}

	const FString Path = FElysiumContentPaths::UiArt(ArtPath.ToString() + TEXT(".png"));
	UTexture2D* Texture = ElysiumUI::LoadPngTexture(Path);
	if (!Texture)
	{
		UE_LOG(LogElysiumHUDWidget, Verbose,
			TEXT("no HUD art at %s — run: uv run elysium export bundle ui"), *Path);
		HudArtTextures.Add(ArtPath, nullptr);
		HudArtBrushes.Add(ArtPath, FSlateBrush());
		return nullptr;
	}

	HudArtTextures.Add(ArtPath, Texture);
	FSlateBrush& Brush = HudArtBrushes.Add(ArtPath);
	Brush.SetResourceObject(Texture);
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	return &Brush;
}
