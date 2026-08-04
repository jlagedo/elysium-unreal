#include "UI/ElysiumUIRoot.h"

#include "ElysiumHUDModel.h"
#include "UI/ElysiumHUDWidget.h"

#include "CommonActivatableWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Components/Widget.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"

void UElysiumUIRoot::EnsureContainers()
{
	if (!TransientStack)
	{
		TransientStack = NewObject<UCommonActivatableWidgetStack>(this, TEXT("TransientStack"));
		NotificationQueue = NewObject<UCommonActivatableWidgetQueue>(this, TEXT("NotificationQueue"));
		GameModalStack = NewObject<UCommonActivatableWidgetStack>(this, TEXT("GameModalStack"));
		SystemModalStack = NewObject<UCommonActivatableWidgetStack>(this, TEXT("SystemModalStack"));
		RuntimeLoadingStack = NewObject<UCommonActivatableWidgetStack>(this, TEXT("RuntimeLoadingStack"));
	}
}

TSharedRef<SWidget> UElysiumUIRoot::RebuildWidget()
{
	EnsureContainers();
	APlayerController* PC = GetOwningPlayer();
	HUDWidget = PC ? CreateWidget<UElysiumHUDWidget>(PC, UElysiumHUDWidget::StaticClass()) : nullptr;
	if (HUDWidget)
	{
		HUDWidget->SetModel(Model);
		HUDWidget->SetVisibility(bHUDSurfaceVisible
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			HUDWidget ? HUDWidget->TakeWidget() : SNullWidget::NullWidget
		]
		+ SOverlay::Slot()
		[
			TransientStack ? TransientStack->TakeWidget() : SNullWidget::NullWidget
		]
		+ SOverlay::Slot()
		[
			NotificationQueue ? NotificationQueue->TakeWidget() : SNullWidget::NullWidget
		]
		+ SOverlay::Slot()
		[
			GameModalStack ? GameModalStack->TakeWidget() : SNullWidget::NullWidget
		]
		+ SOverlay::Slot()
		[
			SystemModalStack ? SystemModalStack->TakeWidget() : SNullWidget::NullWidget
		]
		+ SOverlay::Slot()
		[
			RuntimeLoadingStack ? RuntimeLoadingStack->TakeWidget() : SNullWidget::NullWidget
		];
}

void UElysiumUIRoot::SetHUDSurfaceVisible(bool bVisible)
{
	bHUDSurfaceVisible = bVisible;
	if (HUDWidget)
	{
		HUDWidget->SetVisibility(bVisible
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UElysiumUIRoot::DeactivateAllScreens()
{
	EnsureContainers();
	TransientStack->ClearWidgets();
	NotificationQueue->ClearWidgets();
	GameModalStack->ClearWidgets();
	SystemModalStack->ClearWidgets();
	RuntimeLoadingStack->ClearWidgets();
}

UCommonActivatableWidgetContainerBase* UElysiumUIRoot::ResolveLayer(EElysiumUILayer Layer) const
{
	switch (Layer)
	{
	case EElysiumUILayer::Transient:      return TransientStack;
	case EElysiumUILayer::Notification:   return NotificationQueue;
	case EElysiumUILayer::GameModal:      return GameModalStack;
	case EElysiumUILayer::SystemModal:    return SystemModalStack;
	case EElysiumUILayer::RuntimeLoading: return RuntimeLoadingStack;
	default:                              return nullptr;
	}
}

UCommonActivatableWidget* UElysiumUIRoot::PushWidget(
	EElysiumUILayer Layer,
	TSubclassOf<UCommonActivatableWidget> WidgetClass,
	TFunctionRef<void(UCommonActivatableWidget&)> Init)
{
	EnsureContainers();
	UCommonActivatableWidgetContainerBase* Container = ResolveLayer(Layer);
	return Container && WidgetClass
		? Container->AddWidget<UCommonActivatableWidget>(WidgetClass, Init)
		: nullptr;
}

void UElysiumUIRoot::RemoveWidget(EElysiumUILayer Layer, UCommonActivatableWidget* Widget)
{
	if (UCommonActivatableWidgetContainerBase* Container = ResolveLayer(Layer); Container && Widget)
	{
		Container->RemoveWidget(*Widget);
	}
}

UCommonActivatableWidget* UElysiumUIRoot::GetActiveWidget(EElysiumUILayer Layer) const
{
	if (UCommonActivatableWidgetContainerBase* Container = ResolveLayer(Layer))
	{
		return Container->GetActiveWidget();
	}
	return nullptr;
}

void UElysiumUIRoot::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	HUDWidget = nullptr;
}
