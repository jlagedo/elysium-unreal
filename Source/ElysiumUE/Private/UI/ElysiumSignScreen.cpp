#include "UI/ElysiumSignScreen.h"

#include "UI/ElysiumActionButton.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	TAutoConsoleVariable<int32> CVarDrawSigns(
		TEXT("elysium.DrawSigns"), 1,
		TEXT("Draw game_sign/popup panels (1) or suppress them (0)."),
		ECVF_Default);

	const FName SignActionId(TEXT("Sign.Dismiss"));
	const FName SignGroup(TEXT("Sign"));

	constexpr float SignPanelWidth = 620.0f;
	constexpr float ContinueWidth = 190.0f;
	constexpr float ContinueHeight = 48.0f;
}

UElysiumSignScreen::UElysiumSignScreen()
{
	bIsBackHandler = true;
}

bool UElysiumSignScreen::ShouldDrawSigns()
{
	return CVarDrawSigns.GetValueOnGameThread() != 0;
}

void UElysiumSignScreen::ApplySign(const FElysiumSignData& InSign, float InAlpha,
	bool bInDismissible)
{
	(void)InAlpha;
	const bool bContentChanged = Sign.SourceFile != InSign.SourceFile
		|| Sign.Blocks.Num() != InSign.Blocks.Num();
	Sign = InSign;
	bDismissible = bInDismissible;
	if (PanelAction)
	{
		PanelAction->SetExecutable(bDismissible);
	}
	if (bContentChanged && PanelHost.IsValid())
	{
		PanelHost->SetContent(BuildPanelVisual());
	}
}

float UElysiumSignScreen::VirtualScale() const
{
	FVector2D Size(1920.0f, 1080.0f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	return ElysiumSign::ScaleFor(static_cast<float>(Size.Y));
}

FText UElysiumSignScreen::BuildBodyText() const
{
	FString Body;
	for (const FElysiumSignTextBlock& Block : Sign.Blocks)
	{
		const FString Text = Block.Text.TrimStartAndEnd();
		const FString Lower = Text.ToLower();
		const bool bContinueInstruction = Lower.Contains(TEXT("continue"))
			&& (Lower.Contains(TEXT("click")) || Lower.Contains(TEXT("press")));
		if (Text.IsEmpty() || bContinueInstruction)
		{
			continue;
		}
		if (!Body.IsEmpty())
		{
			Body += TEXT("\n\n");
		}
		Body += Text;
	}

	// Image-only and malformed signs must never become invisible input traps. The action remains
	// useful and visible even when the source carries no usable text.
	if (Body.IsEmpty())
	{
		Body = NSLOCTEXT("Elysium", "EmptySignBody", "Continue").ToString();
	}
	return FText::FromString(Body);
}

TSharedRef<SWidget> UElysiumSignScreen::BuildPanelVisual()
{
	check(PanelAction);
	const TWeakObjectPtr<UElysiumActionButton> WeakAction = PanelAction;
	PanelAction->SetSlateContent(
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
		.BorderBackgroundColor_Lambda([WeakAction]()
		{
			const UElysiumActionButton* Action = WeakAction.Get();
			if (!Action || !Action->IsExecutable())
			{
				return FSlateColor(FLinearColor(0.16f, 0.14f, 0.14f, 1.0f));
			}
			return FSlateColor(Action->IsActionSelected()
				? FLinearColor(0.55f, 0.06f, 0.08f, 1.0f)
				: FLinearColor(0.24f, 0.18f, 0.16f, 1.0f));
		})
		.Padding(FMargin(18.0f, 10.0f))
		[
			SNew(STextBlock)
			.Text(NSLOCTEXT("Elysium", "SignContinue", "Continue"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
			.ColorAndOpacity(FSlateColor(FLinearColor::White))
			.Justification(ETextJustify::Center)
		]);

	TSharedRef<SWidget> Result = SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
		.BorderBackgroundColor(FSlateColor(FLinearColor(0.42f, 0.29f, 0.14f, 1.0f)))
		.Padding(FMargin(2.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			.BorderBackgroundColor(FSlateColor(FLinearColor(0.018f, 0.016f, 0.020f, 0.97f)))
			.Padding(FMargin(32.0f, 28.0f))
			[
				SNew(SBox)
				.WidthOverride(SignPanelWidth)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(BuildBodyText())
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
						.ColorAndOpacity(FSlateColor(FLinearColor::White))
						.AutoWrapText(true)
						.WrapTextAt(SignPanelWidth)
						.LineHeightPercentage(1.12f)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 26.0f, 0.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(ContinueWidth)
						.HeightOverride(ContinueHeight)
						[ PanelAction->TakeWidget() ]
					]
				]
			]
		];
	return Result;
}

TSharedRef<SWidget> UElysiumSignScreen::RebuildWidget()
{
	(void)Super::RebuildWidget();
	BeginNavigationBuild();
	SetNavigationGroup(SignGroup, false, true);

	PanelAction = CreateActionButton(
		SignActionId, SignGroup, NSLOCTEXT("Elysium", "DismissSign", "Continue"),
		bDismissible, [this]() { OnDismiss.ExecuteIfBound(); });
	check(PanelAction);
	TSharedRef<SWidget> Panel = BuildPanelVisual();

	TSharedRef<SWidget> Result = SNew(SDPIScaler)
		.DPIScale_Lambda([this]() { return VirtualScale(); })
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(48.0f))
			[
				SAssignNew(PanelHost, SBox)
				[ Panel ]
			]
		];
	FinalizeNavigationBuild(SignActionId);
	return Result;
}

bool UElysiumSignScreen::NativeOnHandleBackAction()
{
	return true;
}

void UElysiumSignScreen::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	PanelAction = nullptr;
	PanelHost.Reset();
}
