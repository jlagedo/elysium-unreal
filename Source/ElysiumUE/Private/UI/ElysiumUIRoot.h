#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "ElysiumPlayerUISubsystem.h"

#include "ElysiumUIRoot.generated.h"

class UCommonActivatableWidget;
class UCommonActivatableWidgetContainerBase;
class UCommonActivatableWidgetQueue;
class UCommonActivatableWidgetStack;
class UElysiumHUDModel;
class UElysiumHUDWidget;

// The one local-player-owned viewport root. The passive HUD and every interactive CommonUI layer
// share this tree, so paint order, activation, Back routing and focus restoration have one owner.
UCLASS()
class UElysiumUIRoot final : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	void SetModel(UElysiumHUDModel* InModel) { Model = InModel; }
	void SetHUDSurfaceVisible(bool bVisible);
	void DeactivateAllScreens();

	UCommonActivatableWidget* PushWidget(
		EElysiumUILayer Layer,
		TSubclassOf<UCommonActivatableWidget> WidgetClass,
		TFunctionRef<void(UCommonActivatableWidget&)> Init);
	void RemoveWidget(EElysiumUILayer Layer, UCommonActivatableWidget* Widget);
	UCommonActivatableWidget* GetActiveWidget(EElysiumUILayer Layer) const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	UCommonActivatableWidgetContainerBase* ResolveLayer(EElysiumUILayer Layer) const;
	void EnsureContainers();

	UPROPERTY(Transient)
	TObjectPtr<UElysiumHUDModel> Model;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumHUDWidget> HUDWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidgetStack> TransientStack;

	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidgetQueue> NotificationQueue;

	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidgetStack> GameModalStack;

	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidgetStack> SystemModalStack;

	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidgetStack> RuntimeLoadingStack;

	bool bHUDSurfaceVisible = true;
};
