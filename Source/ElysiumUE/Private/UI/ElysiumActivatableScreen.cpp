#include "UI/ElysiumActivatableScreen.h"

#include "ElysiumInputSubsystem.h"

UElysiumActivatableScreen::UElysiumActivatableScreen()
{
	SetIsFocusable(true);
}

void UElysiumActivatableScreen::ConfigureScreenPolicy(EElysiumUIScreenKind Kind)
{
	PopInputScope();
	InputScope = FElysiumInputScope();
	InputScope.Mode = EElysiumInputMode::UIOnly;
	InputScope.CursorPolicy = EElysiumCursorPolicy::Auto;
	switch (Kind)
	{
	case EElysiumUIScreenKind::Menu:
		InputScope.Name = TEXT("Menu");
		InputScope.Priority = ElysiumInput::Priority::Menu;
		break;
	case EElysiumUIScreenKind::Character:
		InputScope.Name = TEXT("Character");
		InputScope.Priority = ElysiumInput::Priority::Character;
		break;
	case EElysiumUIScreenKind::Dialogue:
		InputScope.Name = TEXT("Dialogue");
		InputScope.Priority = ElysiumInput::Priority::Dialogue;
		break;
	case EElysiumUIScreenKind::Loot:
		InputScope.Name = TEXT("Loot");
		InputScope.Priority = ElysiumInput::Priority::Loot;
		break;
	case EElysiumUIScreenKind::Chargen:
		InputScope.Name = TEXT("Chargen");
		InputScope.Priority = ElysiumInput::Priority::Chargen;
		break;
	case EElysiumUIScreenKind::Sign:
		InputScope.Name = TEXT("Sign");
		InputScope.Priority = ElysiumInput::Priority::Sign;
		break;
	}
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
		UWidget* DesiredFocus = NativeGetDesiredFocusTarget();
		InputScope.FocusWidget = DesiredFocus ? DesiredFocus->TakeWidget() : TakeWidget();
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

void UElysiumActivatableScreen::RefreshInputFocusTarget(UWidget* Widget)
{
	if (!InputScopeHandle.IsValid())
	{
		return;
	}
	if (UElysiumInputSubsystem* Input = UElysiumInputSubsystem::Get(GetGameInstance()))
	{
		Input->SetFocusWidget(InputScopeHandle, Widget ? Widget->TakeWidget() : TakeWidget());
	}
}

void UElysiumActivatableScreen::PopInputScope()
{
	if (UElysiumInputSubsystem* Input = UElysiumInputSubsystem::Get(GetGameInstance()))
	{
		Input->Pop(InputScopeHandle);
	}
	InputScopeHandle.Reset();
}
