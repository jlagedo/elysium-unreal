#include "UI/ElysiumHUDWidget.h"

#include "ElysiumContentPaths.h"
#include "ElysiumHUDModel.h"
#include "UI/ElysiumUIStyle.h"
#include "UI/ElysiumUITexture.h"

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
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor HUDOutline(0.004f, 0.003f, 0.002f, 0.96f);

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

	FText SelectorHeading(EElysiumHUDSelector Type)
	{
		switch (Type)
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

	TSharedRef<SVerticalBox> SelectorRows = SNew(SVerticalBox);
	for (int32 Index = 0; Index < 6; ++Index)
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
				return M && M->Selector.Entries.IsValidIndex(Index)
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			.Padding(FMargin(10, 6))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Font(Label)
					.ColorAndOpacity(ElysiumUI::Palette::Bone)
					.Text_Lambda([M, Index]()
					{
						return M && M->Selector.Entries.IsValidIndex(Index)
							? M->Selector.Entries[Index].Label : FText::GetEmpty();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(16, 0, 0, 0)
				[
					SNew(STextBlock)
					.Font(Caption)
					.ColorAndOpacity(ElysiumUI::Palette::BoneDim)
					.Text_Lambda([M, Index]()
					{
						return M && M->Selector.Entries.IsValidIndex(Index)
							? M->Selector.Entries[Index].Detail : FText::GetEmpty();
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
	Content->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(38, 0, 0, 38)
	[
		SNew(SVerticalBox)
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

	// Masquerade is deliberately a neutral numeric contract until the faithful mask-state mapping
	// is wired. It avoids inventing whether the published value means breaches or remaining marks.
	Content->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 34, 38, 0)
	[
		SNew(STextBlock).Font(Label).ColorAndOpacity(ElysiumUI::Palette::Gold)
		.Text_Lambda([M]() { return M && M->bVitalsValid
			? FText::FromString(FString::Printf(TEXT("MASQUERADE  %d"), M->Masquerade))
			: FText::GetEmpty(); })
	];

	// Equipment and discipline regions collapse until their real gameplay owners publish data.
	Content->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(38, 0, 0, 92)
	[
		SNew(STextBlock).Font(Label).ColorAndOpacity(ElysiumUI::Palette::Bone)
		.Visibility_Lambda([M]() { return M && M->Equipment.bValid
			? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		.Text_Lambda([M]()
		{
			return M ? FText::FromString(FString::Printf(TEXT("%s    %d / %d"),
				*M->Equipment.Name.ToString(), M->Equipment.AmmoCurrent, M->Equipment.AmmoReserve))
				: FText::GetEmpty();
		})
	];
	Content->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0, 0, 38, 91)
	[
		SNew(STextBlock).Font(Label).ColorAndOpacity(ElysiumUI::Palette::Bone)
		.Visibility_Lambda([M]() { return M && M->Discipline.bValid
			? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		.Text_Lambda([M]() { return M ? M->Discipline.Name : FText::GetEmpty(); })
	];

	Content->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0, 0, 0, 120)
	[
		SNew(SBox).WidthOverride(380)
		.Visibility_Lambda([M]() { return M && M->Selector.IsOpen()
			? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		[
			SNew(SBorder).BorderImage(White)
			.BorderBackgroundColor(FLinearColor(ElysiumUI::Palette::Ink.R,
				ElysiumUI::Palette::Ink.G, ElysiumUI::Palette::Ink.B, 0.92f))
			.Padding(12)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
				[
					SNew(STextBlock).Font(Label).ColorAndOpacity(ElysiumUI::Palette::GoldLit)
					.Text_Lambda([M]() { return M ? SelectorHeading(M->Selector.Type) : FText::GetEmpty(); })
				]
				+ SVerticalBox::Slot().AutoHeight()[SelectorRows]
			]
		]
	];

	// Aim cursor. Exported original use-icon cells remain the only game-authored icon dependency.
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
						.ColorAndOpacity(HUDOutline)
					]
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(52).HeightOverride(52)
					[
						SNew(SImage).Image(&UseRingBrush).ColorAndOpacity(HUDOutline)
					]
				]
				+ SOverlay::Slot()[SNew(SImage).Image_Lambda([this]() { return UseIconBrush(); })]
				+ SOverlay::Slot()[SNew(SImage).Image(&UseRingBrush)]
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
