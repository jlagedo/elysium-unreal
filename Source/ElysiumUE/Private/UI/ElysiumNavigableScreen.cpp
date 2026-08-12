#include "UI/ElysiumNavigableScreen.h"

#include "UI/ElysiumActionButton.h"

#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

namespace
{
	const FName DefaultGroup(TEXT("Default"));
}

UElysiumNavigableScreen::UElysiumNavigableScreen()
{
	bAutoRestoreFocus = true;
	SetNavigationGroup(DefaultGroup, false, true);
}

void UElysiumNavigableScreen::BeginNavigationBuild()
{
	// A successful action owns input until the screen changes. Rebuilding the action set is the
	// transition boundary that permits the next activation.
	bActionExecutionPending = false;
	RestoreActionId = SelectedActionId;
	SelectedActionId = NAME_None;
	ActionButtons.Reset();
	Actions.Reset();
	Groups.Reset();
	GeneratedActionNumber = 0;
	SetNavigationGroup(DefaultGroup, false, true);
}

UElysiumActionButton* UElysiumNavigableScreen::CreateActionButton(
	FName ActionId,
	FName GroupId,
	const FText& Label,
	bool bExecutable,
	TFunction<void()> OnExecute,
	TFunction<void()> OnSecondary,
	const FText& Caption)
{
	if (!WidgetTree || ActionId.IsNone() || FindRecord(ActionId))
	{
		return nullptr;
	}

	const FName WidgetName(*FString::Printf(TEXT("Action_%d"), GeneratedActionNumber++));
	UElysiumActionButton* Button = WidgetTree->ConstructWidget<UElysiumActionButton>(
		UElysiumActionButton::StaticClass(), WidgetName);
	if (!Button)
	{
		return nullptr;
	}

	Button->Configure(ActionId, Label, Caption, bExecutable);
	Button->OnActionActivated().AddUObject(this,
		&UElysiumNavigableScreen::HandleActionActivated);
	Button->OnSecondaryActivated().AddUObject(this,
		&UElysiumNavigableScreen::HandleSecondaryActivated);
	Button->OnHighlighted().AddUObject(this,
		&UElysiumNavigableScreen::HandleActionHighlighted);

	ActionButtons.Add(Button);
	FActionRecord& Record = Actions.AddDefaulted_GetRef();
	Record.Id = ActionId;
	Record.Group = GroupId.IsNone() ? DefaultGroup : GroupId;
	Record.Button = Button;
	Record.Execute = MoveTemp(OnExecute);
	Record.Secondary = MoveTemp(OnSecondary);
	return Button;
}

void UElysiumNavigableScreen::FinalizeNavigationBuild(FName DefaultActionId)
{
	FName Wanted = RestoreActionId;
	if (!FindRecord(Wanted))
	{
		Wanted = FindRecord(DefaultActionId)
			? DefaultActionId
			: (Actions.IsEmpty() ? NAME_None : Actions[0].Id);
	}
	RestoreActionId = NAME_None;

	if (!Wanted.IsNone())
	{
		SelectAction(Wanted, IsActivated());
	}
	RequestRefreshFocus();
	RefreshInputFocusTarget(NativeGetDesiredFocusTarget());
}

void UElysiumNavigableScreen::SetNavigationGroup(FName GroupId, bool bHorizontal,
	bool bVertical, bool bWrapHorizontal, bool bWrapVertical)
{
	FGroupPolicy& Policy = Groups.FindOrAdd(GroupId.IsNone() ? DefaultGroup : GroupId);
	Policy.bHorizontal = bHorizontal;
	Policy.bVertical = bVertical;
	Policy.bWrapHorizontal = bWrapHorizontal;
	Policy.bWrapVertical = bWrapVertical;
}

void UElysiumNavigableScreen::SetActionNeighbor(FName ActionId,
	EElysiumNavigationDirection Direction, FName NeighborActionId)
{
	if (FActionRecord* Record = FindRecord(ActionId))
	{
		NeighborFor(*Record, Direction) = NeighborActionId;
	}
}

FName UElysiumNavigableScreen::GetActionGroup(FName ActionId) const
{
	const FActionRecord* Record = FindRecord(ActionId);
	return Record ? Record->Group : NAME_None;
}

TArray<FName> UElysiumNavigableScreen::GetActionsInGroup(FName GroupId) const
{
	TArray<FName> Result;
	for (const FActionRecord& Record : Actions)
	{
		if (Record.Group == GroupId)
		{
			Result.Add(Record.Id);
		}
	}
	return Result;
}

UElysiumActionButton* UElysiumNavigableScreen::GetSelectedAction() const
{
	return FindAction(SelectedActionId);
}

UElysiumActionButton* UElysiumNavigableScreen::FindAction(FName ActionId) const
{
	const FActionRecord* Record = FindRecord(ActionId);
	return Record ? Record->Button.Get() : nullptr;
}

bool UElysiumNavigableScreen::SelectAction(FName ActionId, bool bGiveFocus)
{
	UElysiumActionButton* Wanted = FindAction(ActionId);
	if (!Wanted)
	{
		return false;
	}

	const FName PreviousActionId = SelectedActionId;
	SelectedActionId = ActionId;
	for (UElysiumActionButton* Button : ActionButtons)
	{
		if (Button)
		{
			Button->SetActionSelected(Button == Wanted);
		}
	}
	if (PreviousActionId != SelectedActionId)
	{
		HandleSelectedActionChanged(PreviousActionId, SelectedActionId);
	}

	RefreshInputFocusTarget(Wanted);
	if (bGiveFocus)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			Wanted->SetUserFocus(PC);
		}
	}
	return true;
}

bool UElysiumNavigableScreen::SelectAdjacentInGroup(FName GroupId, int32 Delta,
	bool bWrap, bool bGiveFocus)
{
	const TArray<FName> GroupActions = GetActionsInGroup(GroupId);
	if (GroupActions.IsEmpty())
	{
		return false;
	}

	int32 Index = GroupActions.IndexOfByKey(SelectedActionId);
	if (Index == INDEX_NONE)
	{
		Index = Delta >= 0 ? 0 : GroupActions.Num() - 1;
	}
	else
	{
		Index += Delta;
		if (bWrap)
		{
			Index = (Index % GroupActions.Num() + GroupActions.Num()) % GroupActions.Num();
		}
		else
		{
			Index = FMath::Clamp(Index, 0, GroupActions.Num() - 1);
		}
	}
	return SelectAction(GroupActions[Index], bGiveFocus);
}

bool UElysiumNavigableScreen::Navigate(EElysiumNavigationDirection Direction)
{
	if (HandleNavigation(Direction))
	{
		return true;
	}

	const FActionRecord* Current = FindRecord(SelectedActionId);
	if (!Current)
	{
		return Actions.IsEmpty() ? false : SelectAction(Actions[0].Id);
	}
	const FName Explicit = NeighborFor(*Current, Direction);
	if (!Explicit.IsNone())
	{
		return SelectAction(Explicit);
	}

	const FGroupPolicy* Policy = Groups.Find(Current->Group);
	if (!Policy)
	{
		return false;
	}
	if (Direction == EElysiumNavigationDirection::Up && Policy->bVertical)
	{
		return SelectAdjacentInGroup(Current->Group, -1, Policy->bWrapVertical);
	}
	if (Direction == EElysiumNavigationDirection::Down && Policy->bVertical)
	{
		return SelectAdjacentInGroup(Current->Group, 1, Policy->bWrapVertical);
	}
	if (Direction == EElysiumNavigationDirection::Left && Policy->bHorizontal)
	{
		return SelectAdjacentInGroup(Current->Group, -1, Policy->bWrapHorizontal);
	}
	if (Direction == EElysiumNavigationDirection::Right && Policy->bHorizontal)
	{
		return SelectAdjacentInGroup(Current->Group, 1, Policy->bWrapHorizontal);
	}
	return false;
}

bool UElysiumNavigableScreen::ExecuteAction(FName ActionId)
{
	FActionRecord* Record = FindRecord(ActionId);
	UElysiumActionButton* Button = Record ? Record->Button.Get() : nullptr;
	if (bActionExecutionPending || !Record || !Button || !Button->IsExecutable()
		|| !Record->Execute)
	{
		return false;
	}

	SelectAction(ActionId, false);
	bActionExecutionPending = true;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
		[this](float)
		{
			bActionExecutionPending = false;
			return false;
		}));
	TFunction<void()> Execute = Record->Execute;
	Execute();
	return true;
}

bool UElysiumNavigableScreen::ExecuteSelectedAction()
{
	return ExecuteAction(SelectedActionId);
}

bool UElysiumNavigableScreen::ExecuteSelectedSecondaryAction()
{
	FActionRecord* Record = FindRecord(SelectedActionId);
	UElysiumActionButton* Button = Record ? Record->Button.Get() : nullptr;
	if (bActionExecutionPending || !Record || !Button || !Button->IsExecutable()
		|| !Record->Secondary)
	{
		return false;
	}

	bActionExecutionPending = true;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
		[this](float)
		{
			bActionExecutionPending = false;
			return false;
		}));
	TFunction<void()> Secondary = Record->Secondary;
	Secondary();
	return true;
}

bool UElysiumNavigableScreen::HandleNavigation(EElysiumNavigationDirection Direction)
{
	return false;
}

void UElysiumNavigableScreen::HandleSelectedActionChanged(FName PreviousActionId,
	FName NewActionId)
{
}

UWidget* UElysiumNavigableScreen::NativeGetDesiredFocusTarget() const
{
	if (UElysiumActionButton* Selected = GetSelectedAction())
	{
		return Selected;
	}
	return Actions.IsEmpty() ? Super::NativeGetDesiredFocusTarget() : Actions[0].Button.Get();
}

FReply UElysiumNavigableScreen::NativeOnPreviewKeyDown(const FGeometry& Geometry,
	const FKeyEvent& KeyEvent)
{
	if (const TOptional<EElysiumNavigationDirection> Direction = DirectionForKey(KeyEvent.GetKey()))
	{
		if (Navigate(Direction.GetValue()))
		{
			return FReply::Handled();
		}
	}
	if (KeyEvent.GetKey() == EKeys::SpaceBar)
	{
		if (!KeyEvent.IsRepeat())
		{
			ExecuteSelectedAction();
		}
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, KeyEvent);
}

UElysiumNavigableScreen::FActionRecord* UElysiumNavigableScreen::FindRecord(FName ActionId)
{
	return Actions.FindByPredicate(
		[ActionId](const FActionRecord& Record) { return Record.Id == ActionId; });
}

const UElysiumNavigableScreen::FActionRecord* UElysiumNavigableScreen::FindRecord(
	FName ActionId) const
{
	return Actions.FindByPredicate(
		[ActionId](const FActionRecord& Record) { return Record.Id == ActionId; });
}

FName& UElysiumNavigableScreen::NeighborFor(FActionRecord& Record,
	EElysiumNavigationDirection Direction)
{
	switch (Direction)
	{
	case EElysiumNavigationDirection::Up:    return Record.Up;
	case EElysiumNavigationDirection::Down:  return Record.Down;
	case EElysiumNavigationDirection::Left:  return Record.Left;
	case EElysiumNavigationDirection::Right: return Record.Right;
	}
	return Record.Down;
}

const FName& UElysiumNavigableScreen::NeighborFor(const FActionRecord& Record,
	EElysiumNavigationDirection Direction)
{
	return NeighborFor(const_cast<FActionRecord&>(Record), Direction);
}

TOptional<EElysiumNavigationDirection> UElysiumNavigableScreen::DirectionForKey(const FKey& Key)
{
	if (Key == EKeys::Up || Key == EKeys::W || Key == EKeys::Gamepad_DPad_Up
		|| Key == EKeys::Gamepad_LeftStick_Up)
	{
		return EElysiumNavigationDirection::Up;
	}
	if (Key == EKeys::Down || Key == EKeys::S || Key == EKeys::Gamepad_DPad_Down
		|| Key == EKeys::Gamepad_LeftStick_Down)
	{
		return EElysiumNavigationDirection::Down;
	}
	if (Key == EKeys::Left || Key == EKeys::A || Key == EKeys::Gamepad_DPad_Left
		|| Key == EKeys::Gamepad_LeftStick_Left)
	{
		return EElysiumNavigationDirection::Left;
	}
	if (Key == EKeys::Right || Key == EKeys::D || Key == EKeys::Gamepad_DPad_Right
		|| Key == EKeys::Gamepad_LeftStick_Right)
	{
		return EElysiumNavigationDirection::Right;
	}
	return TOptional<EElysiumNavigationDirection>();
}

void UElysiumNavigableScreen::HandleActionActivated(UElysiumActionButton& Button)
{
	ExecuteAction(Button.GetActionId());
}

void UElysiumNavigableScreen::HandleSecondaryActivated(UElysiumActionButton& Button)
{
	SelectAction(Button.GetActionId(), false);
	ExecuteSelectedSecondaryAction();
}

void UElysiumNavigableScreen::HandleActionHighlighted(UElysiumActionButton& Button)
{
	SelectAction(Button.GetActionId(), true);
}
