#pragma once

#include "CoreMinimal.h"
#include "ElysiumHUDTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"

#include "ElysiumPlayerUISubsystem.generated.h"

class IConsoleObject;
class APlayerController;
class UCommonActivatableWidget;
class UElysiumHUDModel;
class UElysiumUIRoot;
class UElysiumPresentationSubsystem;
class UWorld;

// The fixed composition order for one local player's screen. HUD content is passive; every
// interactive screen enters one of the semantic CommonUI containers above it.
enum class EElysiumUILayer : uint8
{
	Transient,
	Notification,
	GameModal,
	SystemModal,
	RuntimeLoading,
};

// Local-player lifetime owner for every player-facing UI surface. The world publisher can
// disappear on travel; this object survives, rebinds the stable HUD model, and keeps all screens
// inside one root instead of assigning unrelated viewport z-orders.
UCLASS()
class UElysiumPlayerUISubsystem final : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static UElysiumPlayerUISubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

	UFUNCTION(BlueprintPure, Category = "Elysium|HUD")
	UElysiumHUDModel* GetModel() const { return Model; }

	void RebindToWorld(UWorld* World);
	void SetPreviewMode(EElysiumHUDPreview Mode);

	// Debug and presentation owners may suppress the passive HUD without destroying the root or
	// any menu/loading screen above it. The state survives travel and controller replacement.
	void SetHUDSurfaceVisible(bool bVisible);
	bool IsHUDSurfaceVisible() const { return bHUDSurfaceVisible; }

	// The one entry/exit surface for activatable screens. Init runs on a freshly generated or pooled
	// instance before the container activates it, so callers must establish all per-open state there.
	UCommonActivatableWidget* PushWidget(
		EElysiumUILayer Layer,
		TSubclassOf<UCommonActivatableWidget> WidgetClass,
		TFunction<void(UCommonActivatableWidget&)> Init = {});
	void RemoveWidget(EElysiumUILayer Layer, UCommonActivatableWidget* Widget);
	UCommonActivatableWidget* GetActiveWidget(EElysiumUILayer Layer) const;

private:
	void EnsureRoot();
	void RemoveRoot();
	void UnbindPresentation();
	void OnViewPublished(const struct FElysiumViewState& View);
	void OnPostLoadMap(UWorld* LoadedWorld);

	UPROPERTY(Transient)
	TObjectPtr<UElysiumHUDModel> Model;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumUIRoot> Root;

	TWeakObjectPtr<UElysiumPresentationSubsystem> BoundPresentation;
	FDelegateHandle ViewPublishedHandle;
	FDelegateHandle PostLoadMapHandle;
	EElysiumHUDPreview PreviewMode = EElysiumHUDPreview::Off;
	bool bHUDSurfaceVisible = true;
	TArray<IConsoleObject*> ConsoleObjects;
};
