#include "UI/ElysiumUIRoot.h"

#include "ElysiumHUDModel.h"
#include "UI/ElysiumHUDWidget.h"

#include "CommonActivatableWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Widget.h"

namespace
{
	void AddFullscreenLayer(UOverlay& Overlay, UWidget& Child)
	{
		UOverlaySlot* Slot = Overlay.AddChildToOverlay(&Child);
		check(Slot);
		Slot->SetHorizontalAlignment(HAlign_Fill);
		Slot->SetVerticalAlignment(VAlign_Fill);
	}

	void ConfigureInstantLayer(UCommonActivatableWidgetContainerBase& Layer)
	{
		// The old viewport surfaces appeared synchronously. Keep that contract until a layer has an
		// authored transition of its own; CommonUI's inherited 0.4 s default is not presentation policy.
		Layer.SetTransitionDuration(0.0f);
	}
}

void UElysiumUIRoot::EnsureContainers()
{
	if (TransientStack)
	{
		return;
	}

	// CommonUI creates activatable screens through the container's owning UUserWidget. Keep every
	// layer in this root's WidgetTree so that ownership can be resolved during AddWidget().
	Initialize();
	check(WidgetTree);

	UOverlay* RootOverlay = WidgetTree->ConstructWidget<UOverlay>(
		UOverlay::StaticClass(), TEXT("RootOverlay"));
	WidgetTree->RootWidget = RootOverlay;

	HUDWidget = WidgetTree->ConstructWidget<UElysiumHUDWidget>(
		UElysiumHUDWidget::StaticClass(), TEXT("HUD"));
	TransientStack = WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(
		UCommonActivatableWidgetStack::StaticClass(), TEXT("TransientStack"));
	NotificationQueue = WidgetTree->ConstructWidget<UCommonActivatableWidgetQueue>(
		UCommonActivatableWidgetQueue::StaticClass(), TEXT("NotificationQueue"));
	GameModalStack = WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(
		UCommonActivatableWidgetStack::StaticClass(), TEXT("GameModalStack"));
	SystemModalStack = WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(
		UCommonActivatableWidgetStack::StaticClass(), TEXT("SystemModalStack"));
	RuntimeLoadingStack = WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(
		UCommonActivatableWidgetStack::StaticClass(), TEXT("RuntimeLoadingStack"));

	ConfigureInstantLayer(*TransientStack);
	ConfigureInstantLayer(*NotificationQueue);
	ConfigureInstantLayer(*GameModalStack);
	ConfigureInstantLayer(*SystemModalStack);
	ConfigureInstantLayer(*RuntimeLoadingStack);

	AddFullscreenLayer(*RootOverlay, *HUDWidget);
	AddFullscreenLayer(*RootOverlay, *TransientStack);
	AddFullscreenLayer(*RootOverlay, *NotificationQueue);
	AddFullscreenLayer(*RootOverlay, *GameModalStack);
	AddFullscreenLayer(*RootOverlay, *SystemModalStack);
	AddFullscreenLayer(*RootOverlay, *RuntimeLoadingStack);
}

TSharedRef<SWidget> UElysiumUIRoot::RebuildWidget()
{
	EnsureContainers();
	HUDWidget->SetModel(Model);
	HUDWidget->SetVisibility(bHUDSurfaceVisible
		? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	NotificationQueue->SetVisibility(bNotificationSurfaceVisible
		? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	return Super::RebuildWidget();
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

void UElysiumUIRoot::SetNotificationSurfaceVisible(bool bVisible)
{
	bNotificationSurfaceVisible = bVisible;
	if (NotificationQueue)
	{
		NotificationQueue->SetVisibility(bVisible
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
