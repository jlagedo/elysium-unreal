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
