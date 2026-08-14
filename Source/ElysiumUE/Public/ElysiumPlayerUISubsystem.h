#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumHUDTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"

#include "ElysiumPlayerUISubsystem.generated.h"

class IConsoleObject;
class APlayerController;
class UCommonActivatableWidget;
class UElysiumDialogueScreen;
class UElysiumHUDModel;
class UElysiumLootScreen;
class UElysiumNotificationScreen;
class UElysiumUIRoot;
class UElysiumPresentationSubsystem;
class UElysiumSignScreen;
class UElysiumTerminalScreen;
class UWorld;
struct FElysiumDialogueView;
struct FElysiumNotification;
class FElysiumDlgConversation;

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
	void OnNotification(const FElysiumNotification& Notification);
	void OnNotificationFinished(UElysiumNotificationScreen* Screen);
	void SetNotificationSurfaceAvailable(bool bAvailable);
	void ExecuteNotificationPreview(const TArray<FString>& Args);
	void OnPostLoadMap(UWorld* LoadedWorld);
	void ReconcileDialogue(const FElysiumDialogueView& Dialogue);
	void ReconcileSign(const struct FElysiumViewState& View);
	void ShowSign(const struct FElysiumViewState& View);
	void HideSign();
	void OnSignDismiss();
	void ShowDialogue(const FElysiumDialogueView& Dialogue);
	void HideDialogue();
	void OnDialogueChoice(int32 VisibleIndex);
	void ReconcileLoot(const struct FElysiumLootView& Loot);
	void ShowLoot(const struct FElysiumLootView& Loot);
	void HideLoot();
	void OnLootTransfer(bool bTake, int32 Slot);
	void OnLootClose();
	void ReconcileTerminal(const struct FElysiumTerminalView& Terminal);
	void ShowTerminal(const struct FElysiumTerminalView& Terminal);
	void HideTerminal();
	bool OnTerminalCommand(const FElysiumEntityHandle& Owner, uint32 SessionSerial,
		const FString& Command);

	UPROPERTY(Transient)
	TObjectPtr<UElysiumHUDModel> Model;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumUIRoot> Root;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumDialogueScreen> DialogueScreen;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumSignScreen> SignScreen;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumLootScreen> LootScreen;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumTerminalScreen> TerminalScreen;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UElysiumNotificationScreen>> NotificationScreens;

	TWeakObjectPtr<UElysiumPresentationSubsystem> BoundPresentation;
	// Identity only: never dereferenced after publication, because the conversation is map-owned.
	const FElysiumDlgConversation* ShownDialogue = nullptr;
	const struct FElysiumSignData* ShownSign = nullptr;
	uint32 ShownDialogueRevision = 0;
	FElysiumEntityHandle ShownLootOwner;
	uint32 ShownLootRevision = 0;
	FElysiumEntityHandle ShownTerminalOwner;
	uint32 ShownTerminalSerial = 0;
	uint32 ShownTerminalRevision = 0;
	FDelegateHandle ViewPublishedHandle;
	FDelegateHandle NotificationHandle;
	FDelegateHandle PostLoadMapHandle;
	EElysiumHUDPreview PreviewMode = EElysiumHUDPreview::Off;
	bool bHUDSurfaceVisible = true;
	bool bNotificationSurfaceAvailable = false;
	TArray<IConsoleObject*> ConsoleObjects;
};
