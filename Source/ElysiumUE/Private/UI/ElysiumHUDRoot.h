#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"

#include "ElysiumHUDRoot.generated.h"

class SBox;
class UElysiumHUDModel;
class UElysiumHUDWidget;

// One local-player-owned viewport root. The empty overlay hosts are intentional seams for the
// transient selector, game-modal and system-modal layers; this first slice only populates the HUD.
UCLASS()
class UElysiumHUDRoot final : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	void SetModel(UElysiumHUDModel* InModel) { Model = InModel; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UElysiumHUDModel> Model;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumHUDWidget> HUDWidget;

	TSharedPtr<SBox> TransientHost;
	TSharedPtr<SBox> GameModalHost;
	TSharedPtr<SBox> SystemModalHost;
};
