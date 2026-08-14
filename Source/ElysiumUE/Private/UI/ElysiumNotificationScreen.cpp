#include "UI/ElysiumNotificationScreen.h"

#include "UI/ElysiumUIStyle.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace ElysiumNotificationUI
{
	FText CategoryLabel(EElysiumNotificationKind Kind)
	{
		switch (Kind)
		{
		case EElysiumNotificationKind::ItemAcquired:
			return NSLOCTEXT("Elysium", "NotificationItemAcquired", "ITEM ACQUIRED");
		case EElysiumNotificationKind::QuestUpdated:
			return NSLOCTEXT("Elysium", "NotificationQuestUpdated", "QUEST UPDATED");
		case EElysiumNotificationKind::QuestCompleted:
			return NSLOCTEXT("Elysium", "NotificationQuestCompleted", "QUEST COMPLETED");
		case EElysiumNotificationKind::QuestFailed:
			return NSLOCTEXT("Elysium", "NotificationQuestFailed", "QUEST FAILED");
		default:
			return NSLOCTEXT("Elysium", "NotificationGeneric", "NOTICE");
		}
	}

	FText SubjectLabel(const FElysiumNotification& Notification)
	{
		if (Notification.Kind == EElysiumNotificationKind::ItemAcquired
			&& Notification.Quantity > 1)
		{
			return FText::FromString(FString::Printf(TEXT("%s \u00d7%d"),
				*Notification.Subject, Notification.Quantity));
		}
		return FText::FromString(Notification.Subject);
	}

	float OpacityAt(float ElapsedSeconds)
	{
		if (ElapsedSeconds <= 0.0f)
		{
			return 0.0f;
		}
		if (ElapsedSeconds < EnterSeconds)
		{
			return FMath::InterpEaseOut(0.0f, 1.0f, ElapsedSeconds / EnterSeconds, 2.0f);
		}
		const float ExitStart = EnterSeconds + HoldSeconds;
		if (ElapsedSeconds < ExitStart)
		{
			return 1.0f;
		}
		return 1.0f - FMath::Clamp((ElapsedSeconds - ExitStart) / ExitSeconds, 0.0f, 1.0f);
	}

	float OffsetYAt(float ElapsedSeconds)
	{
		if (ElapsedSeconds < EnterSeconds)
		{
			const float T = FMath::Clamp(ElapsedSeconds / EnterSeconds, 0.0f, 1.0f);
			return FMath::InterpEaseOut(-12.0f, 0.0f, T, 2.0f);
		}
		const float ExitStart = EnterSeconds + HoldSeconds;
		if (ElapsedSeconds < ExitStart)
		{
			return 0.0f;
		}
		const float T = FMath::Clamp((ElapsedSeconds - ExitStart) / ExitSeconds, 0.0f, 1.0f);
		return FMath::Lerp(0.0f, -8.0f, T);
	}
}

UElysiumNotificationScreen::UElysiumNotificationScreen()
{
	SetIsFocusable(false);
}

void UElysiumNotificationScreen::ApplyNotification(const FElysiumNotification& InNotification)
{
	Notification = InNotification;
	Notification.Quantity = FMath::Max(1, Notification.Quantity);
	if (CardHost)
	{
		InvalidateLayoutAndVolatility();
	}
}

TSharedRef<SWidget> UElysiumNotificationScreen::RebuildWidget()
{
	(void)Super::RebuildWidget();
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FSlateFontInfo CategoryFont = ElysiumUIFonts().Font(
		EElysiumFontRole::Label, EElysiumFontWeight::SemiBold, ElysiumUI::Type::Label, 1.0f);
	const FSlateFontInfo SubjectFont = ElysiumUIFonts().Font(
		EElysiumFontRole::Body, EElysiumFontWeight::SemiBold, 18.0f, 1.0f);

	TSharedRef<SWidget> Card = SAssignNew(CardHost, SBox)
		.WidthOverride(ElysiumNotificationUI::CardWidth)
		[
			SNew(SBorder)
			.BorderImage(White)
			.BorderBackgroundColor(FLinearColor(
				ElysiumUI::Palette::Blood.R, ElysiumUI::Palette::Blood.G,
				ElysiumUI::Palette::Blood.B, 0.92f))
			.Padding(FMargin(3.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SBorder)
				.BorderImage(White)
				.BorderBackgroundColor(FLinearColor(
					ElysiumUI::Palette::Ink.R, ElysiumUI::Palette::Ink.G,
					ElysiumUI::Palette::Ink.B, 0.93f))
				.Padding(FMargin(18.0f, 12.0f, 18.0f, 13.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return ElysiumNotificationUI::CategoryLabel(Notification.Kind);
						})
						.Font(CategoryFont)
						.ColorAndOpacity(ElysiumUI::Palette::Gold)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return ElysiumNotificationUI::SubjectLabel(Notification);
						})
						.Font(SubjectFont)
						.ColorAndOpacity(ElysiumUI::Palette::Bone)
						.AutoWrapText(true)
						.WrapTextAt(ElysiumNotificationUI::CardWidth - 39.0f)
					]
				]
			]
		];

	ApplyAnimationState();
	return SNew(SSafeZone)
		.IsTitleSafe(false)
		[
			SNew(SDPIScaler)
			.DPIScale_Lambda([this]() { return VirtualScale(); })
			[
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Top)
				.Padding(FMargin(0.0f, ElysiumNotificationUI::TopMargin, 0.0f, 0.0f))
				[ Card ]
			]
		];
}

void UElysiumNotificationScreen::NativeOnActivated()
{
	Elapsed = 0.0f;
	bFinishRequested = false;
	Super::NativeOnActivated();
	ApplyAnimationState();
}

void UElysiumNotificationScreen::NativeOnDeactivated()
{
	bFinishRequested = true;
	Super::NativeOnDeactivated();
}

void UElysiumNotificationScreen::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bSuspended || bFinishRequested || !IsActivated())
	{
		return;
	}
	Elapsed += FMath::Max(0.0f, InDeltaTime);
	ApplyAnimationState();
	if (Elapsed >= ElysiumNotificationUI::TotalSeconds)
	{
		bFinishRequested = true;
		OnFinished.ExecuteIfBound(this);
	}
}

void UElysiumNotificationScreen::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	CardHost.Reset();
}

float UElysiumNotificationScreen::VirtualScale() const
{
	FVector2D Size(1920.0f, 1080.0f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	return ElysiumUI::ScaleFor(Size.Y);
}

void UElysiumNotificationScreen::ApplyAnimationState()
{
	if (!CardHost)
	{
		return;
	}
	CardHost->SetRenderOpacity(ElysiumNotificationUI::OpacityAt(Elapsed));
	CardHost->SetRenderTransform(FSlateRenderTransform(
		FVector2D(0.0f, ElysiumNotificationUI::OffsetYAt(Elapsed))));
	CardHost->Invalidate(EInvalidateWidgetReason::Paint);
}
