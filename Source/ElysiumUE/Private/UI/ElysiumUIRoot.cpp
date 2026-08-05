#include "UI/ElysiumUIRoot.h"

#include "ElysiumHUDModel.h"
#include "UI/ElysiumHUDWidget.h"

#include "CommonActivatableWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/Widget.h"

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

	RootOverlay->AddChildToOverlay(HUDWidget);
	RootOverlay->AddChildToOverlay(TransientStack);
	RootOverlay->AddChildToOverlay(NotificationQueue);
	RootOverlay->AddChildToOverlay(GameModalStack);
	RootOverlay->AddChildToOverlay(SystemModalStack);
	RootOverlay->AddChildToOverlay(RuntimeLoadingStack);
}

TSharedRef<SWidget> UElysiumUIRoot::RebuildWidget()
{
	EnsureContainers();
	HUDWidget->SetModel(Model);
	HUDWidget->SetVisibility(bHUDSurfaceVisible
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
