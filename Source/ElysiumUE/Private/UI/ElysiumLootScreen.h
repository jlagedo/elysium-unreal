#pragma once

#include "ElysiumViewState.h"
#include "UI/ElysiumNavigableScreen.h"

#include "ElysiumLootScreen.generated.h"

class SBox;

// CommonUI projection for one authoritative loot session. Rows carry only compact slot ids;
// executing one submits a Take/Give intent back through presentation.
UCLASS()
class UElysiumLootScreen final : public UElysiumNavigableScreen
{
	GENERATED_BODY()

public:
	UElysiumLootScreen();
	void ApplyLoot(const FElysiumLootView& InLoot);

	DECLARE_DELEGATE_TwoParams(FOnLootTransfer, bool /*bTake*/, int32 /*Slot*/);
	DECLARE_DELEGATE(FOnCloseLoot);
	FOnLootTransfer OnTransfer;
	FOnCloseLoot OnClose;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual bool NativeOnHandleBackAction() override;

private:
	TSharedRef<SWidget> BuildPanel();
	TSharedRef<SWidget> BuildList(const TArray<FElysiumLootEntryView>& Entries,
		bool bTake, FName Group);
	TSharedRef<SWidget> BuildActionVisual(class UElysiumActionButton& Action,
		const FElysiumLootEntryView& Entry, bool bTake);
	float VirtualScale() const;

	FElysiumLootView Loot;
	TSharedPtr<SBox> PanelHost;
};
