#pragma once

#include "Substrate/ElysiumSignData.h"
#include "UI/ElysiumNavigableScreen.h"

#include "ElysiumSignScreen.generated.h"

class SBox;
class UElysiumActionButton;

// CommonUI host for game_sign/tutorial text. SignData supplies the message and dismissal policy;
// presentation is a project-owned, resolution-independent floating panel with one Continue action.
UCLASS()
class UElysiumSignScreen final : public UElysiumNavigableScreen
{
	GENERATED_BODY()

public:
	UElysiumSignScreen();

	void ApplySign(const FElysiumSignData& InSign, float InAlpha, bool bInDismissible);
	static bool ShouldDrawSigns();

	DECLARE_DELEGATE(FOnDismissSign);
	FOnDismissSign OnDismiss;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual bool NativeOnHandleBackAction() override;

private:
	float VirtualScale() const;
	FText BuildBodyText() const;
	TSharedRef<SWidget> BuildPanelVisual();

	FElysiumSignData Sign;
	bool bDismissible = false;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumActionButton> PanelAction;

	TSharedPtr<SBox> PanelHost;
};
