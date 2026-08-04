#pragma once

#include "CoreMinimal.h"
#include "ElysiumHUDTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"

#include "ElysiumHUDSubsystem.generated.h"

class IConsoleObject;
class APlayerController;
class UElysiumHUDModel;
class UElysiumHUDRoot;
class UElysiumPresentationSubsystem;
class UWorld;

// Local-player lifetime owner for the in-game HUD. The world publisher can disappear on travel;
// this object survives, rebinds to the new publisher, and keeps engine code talking to one stable
// model/root interface instead of reaching into widgets.
UCLASS()
class UElysiumHUDSubsystem final : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static UElysiumHUDSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

	UFUNCTION(BlueprintPure, Category = "Elysium|HUD")
	UElysiumHUDModel* GetModel() const { return Model; }

	void RebindToWorld(UWorld* World);
	void SetPreviewMode(EElysiumHUDPreview Mode);
	// Take the whole root off screen — reticle, vitals and fade together. For the debug harnesses
	// that own the viewport and are not showing the player anything: a green room is a neutral
	// stage, and a masquerade meter over a model being inspected is noise. Idempotent, and the
	// state survives a rebind, because a travel must not quietly put the HUD back.
	void SetHidden(bool bInHidden);
	bool IsHidden() const { return bHidden; }

private:
	void EnsureRoot();
	void RemoveRoot();
	void UnbindPresentation();
	void OnViewPublished(const struct FElysiumViewState& View);
	void OnPostLoadMap(UWorld* LoadedWorld);

	UPROPERTY(Transient)
	TObjectPtr<UElysiumHUDModel> Model;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumHUDRoot> Root;

	TWeakObjectPtr<UElysiumPresentationSubsystem> BoundPresentation;
	FDelegateHandle ViewPublishedHandle;
	FDelegateHandle PostLoadMapHandle;
	EElysiumHUDPreview PreviewMode = EElysiumHUDPreview::Off;
	bool bHidden = false;
	TArray<IConsoleObject*> ConsoleObjects;
};
