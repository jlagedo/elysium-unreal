#include "UI/ElysiumActivatableScreen.h"

#include "ElysiumInputSubsystem.h"

UElysiumActivatableScreen::UElysiumActivatableScreen()
{
	SetIsFocusable(true);
}

void UElysiumActivatableScreen::ConfigureInputScope(
	FName Name,
	int32 ScopePriority,
	EElysiumInputMode Mode,
	bool bShowCursor)
{
	PopInputScope();
	InputScope = FElysiumInputScope();
	InputScope.Name = Name;
	InputScope.Priority = ScopePriority;
	InputScope.Mode = Mode;
	InputScope.bShowCursor = bShowCursor;
	bHasInputScope = true;
}

void UElysiumActivatableScreen::ClearInputScope()
{
	PopInputScope();
	InputScope = FElysiumInputScope();
	bHasInputScope = false;
}

void UElysiumActivatableScreen::NativeOnActivated()
{
	Super::NativeOnActivated();
	if (!bHasInputScope || InputScopeHandle.IsValid())
	{
		return;
	}
	if (UElysiumInputSubsystem* Input = UElysiumInputSubsystem::Get(GetGameInstance()))
	{
		// Elysium owns the engine input mode, cursor and gameplay contexts; CommonUI restores
		// NativeGetDesiredFocusTarget as screens enter and leave the activatable tree. The scope
		// still has to name the widget, because `FInputModeUIOnly` only MOVES focus when it is
		// given one — `FInputModeDataBase::SetFocusAndLocking` is a no-op on an invalid widget and
		// never clears or redirects. Leaving it unset strands keyboard focus on the game viewport,
		// and a UIOnly screen with no focus takes no key at all: a conversation opens that only the
		// mouse can answer. Both mechanisms name the same target, so they cannot disagree.
		InputScope.FocusWidget = TakeWidget();
		InputScopeHandle = Input->Push(InputScope);
	}
}

void UElysiumActivatableScreen::NativeOnDeactivated()
{
	PopInputScope();
	Super::NativeOnDeactivated();
}

UWidget* UElysiumActivatableScreen::NativeGetDesiredFocusTarget() const
{
	return const_cast<UElysiumActivatableScreen*>(this);
}

void UElysiumActivatableScreen::PopInputScope()
{
	if (UElysiumInputSubsystem* Input = UElysiumInputSubsystem::Get(GetGameInstance()))
	{
		Input->Pop(InputScopeHandle);
	}
	InputScopeHandle.Reset();
}
