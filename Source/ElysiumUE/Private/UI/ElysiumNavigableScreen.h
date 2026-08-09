#pragma once

#include "UI/ElysiumActivatableScreen.h"

#include "ElysiumNavigableScreen.generated.h"

class UElysiumActionButton;

enum class EElysiumNavigationDirection : uint8
{
	Up,
	Down,
	Left,
	Right,
};

// Common focus/selection owner for every interactive Elysium screen. The action id is the durable
// state; widgets are replaceable projections rebuilt whenever a dialogue turn or character page
// changes.
UCLASS(Abstract)
class UElysiumNavigableScreen : public UElysiumActivatableScreen
{
	GENERATED_BODY()

public:
	UElysiumNavigableScreen();

	FName GetSelectedActionId() const { return SelectedActionId; }
	UElysiumActionButton* GetSelectedAction() const;
	UElysiumActionButton* FindAction(FName ActionId) const;

	bool SelectAction(FName ActionId, bool bGiveFocus = true);
	bool Navigate(EElysiumNavigationDirection Direction);
	bool ExecuteAction(FName ActionId);
	bool ExecuteSelectedAction();
	bool ExecuteSelectedSecondaryAction();

protected:
	// A dynamic rebuild is a transaction: save the durable selection, create the replacement
	// actions, then restore it (or choose the caller's default) and refresh CommonUI focus once.
	void BeginNavigationBuild();
	UElysiumActionButton* CreateActionButton(
		FName ActionId,
		FName GroupId,
		const FText& Label,
		bool bExecutable,
		TFunction<void()> OnExecute,
		TFunction<void()> OnSecondary = {},
		const FText& Caption = FText::GetEmpty());
	void FinalizeNavigationBuild(FName DefaultActionId = NAME_None);

	void SetNavigationGroup(FName GroupId, bool bHorizontal, bool bVertical,
		bool bWrapHorizontal = true, bool bWrapVertical = true);
	void SetActionNeighbor(FName ActionId, EElysiumNavigationDirection Direction,
		FName NeighborActionId);
	FName GetActionGroup(FName ActionId) const;
	TArray<FName> GetActionsInGroup(FName GroupId) const;
	bool SelectAdjacentInGroup(FName GroupId, int32 Delta, bool bWrap,
		bool bGiveFocus = true);

	// Complex pages may consume a direction before the ordinary group rule runs (trait adjustment,
	// quest hubs and cross-region transitions). Return true when the direction was handled.
	virtual bool HandleNavigation(EElysiumNavigationDirection Direction);
	virtual void HandleSelectedActionChanged(FName PreviousActionId, FName NewActionId);

	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,
		const FKeyEvent& KeyEvent) override;

private:
	struct FActionRecord
	{
		FName Id;
		FName Group;
		TWeakObjectPtr<UElysiumActionButton> Button;
		TFunction<void()> Execute;
		TFunction<void()> Secondary;
		FName Up;
		FName Down;
		FName Left;
		FName Right;
	};

	struct FGroupPolicy
	{
		bool bHorizontal = false;
		bool bVertical = true;
		bool bWrapHorizontal = true;
		bool bWrapVertical = true;
	};

	FActionRecord* FindRecord(FName ActionId);
	const FActionRecord* FindRecord(FName ActionId) const;
	static FName& NeighborFor(FActionRecord& Record, EElysiumNavigationDirection Direction);
	static const FName& NeighborFor(const FActionRecord& Record,
		EElysiumNavigationDirection Direction);
	static TOptional<EElysiumNavigationDirection> DirectionForKey(const FKey& Key);

	void HandleActionActivated(UElysiumActionButton& Button);
	void HandleSecondaryActivated(UElysiumActionButton& Button);
	void HandleActionHighlighted(UElysiumActionButton& Button);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UElysiumActionButton>> ActionButtons;

	TArray<FActionRecord> Actions;
	TMap<FName, FGroupPolicy> Groups;
	FName SelectedActionId;
	FName RestoreActionId;
	int32 GeneratedActionNumber = 0;
	bool bActionExecutionPending = false;
};
