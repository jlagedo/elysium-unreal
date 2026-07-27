#include "ElysiumUISubsystem.h"

#include "ElysiumInputSubsystem.h"
#include "ElysiumMainMenu.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumUI, Log, All);

namespace
{
	const TCHAR* MenuModeName(EElysiumMenuMode Mode)
	{
		switch (Mode)
		{
		case EElysiumMenuMode::Pause:    return TEXT("pause");
		case EElysiumMenuMode::GameOver: return TEXT("game over");
		default:                         return TEXT("main");
		}
	}
}

void UElysiumUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	IConsoleManager& Console = IConsoleManager::Get();
	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("elysium.menu"),
		TEXT("Show the menu. 'elysium.menu pause' / 'elysium.menu gameover' show those item sets. "
			"This raises the screen only — `elysium.pausemenu` is what actually pauses the run."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			EElysiumMenuMode Mode = EElysiumMenuMode::Main;
			if (Args.Num() > 0)
			{
				if (Args[0].StartsWith(TEXT("p"), ESearchCase::IgnoreCase))
				{
					Mode = EElysiumMenuMode::Pause;
				}
				else if (Args[0].StartsWith(TEXT("g"), ESearchCase::IgnoreCase))
				{
					Mode = EElysiumMenuMode::GameOver;
				}
			}
			ShowMenu(Mode);
		}),
		ECVF_Default));

	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("elysium.menu.close"),
		TEXT("Hide the menu and return input to the game."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]() { HideMenu(); }),
		ECVF_Default));
}

void UElysiumUISubsystem::Deinitialize()
{
	HideMenu();
	for (IConsoleObject* Object : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Object);
	}
	ConsoleObjects.Reset();
	Super::Deinitialize();
}

void UElysiumUISubsystem::PushMenuScope()
{
	UElysiumInputSubsystem* Input = UElysiumInputSubsystem::Get(GetGameInstance());
	if (!Input || !Menu)
	{
		return;
	}

	FElysiumInputScope Scope;
	Scope.Name = TEXT("Menu");
	Scope.Priority = ElysiumInput::Priority::Menu;
	Scope.Mode = EElysiumInputMode::UIOnly;
	Scope.bShowCursor = true;
	// Focus the screen itself so Escape reaches it. Without this, focus stays on the game viewport
	// widget and the menu's key handler never runs — the pause menu would open on Esc and then
	// refuse to close on the same key.
	Scope.FocusWidget = Menu->TakeWidget();
	MenuScope = Input->Push(MoveTemp(Scope));
}

void UElysiumUISubsystem::PopMenuScope()
{
	if (UElysiumInputSubsystem* Input = UElysiumInputSubsystem::Get(GetGameInstance()))
	{
		Input->Pop(MenuScope);
	}
	MenuScope.Reset();
}

void UElysiumUISubsystem::ShowMenu(EElysiumMenuMode Mode)
{
	if (Menu)
	{
		if (CurrentMode == Mode)
		{
			return;
		}
		// A mode change is a different item set, and the tree is built once in RebuildWidget — so
		// swap the screen rather than trying to mutate it. This is the death-during-pause case.
		HideMenu();
	}

	UGameInstance* GI = GetGameInstance();
	APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	if (!PC)
	{
		UE_LOG(LogElysiumUI, Warning, TEXT("no local player controller — menu not shown"));
		return;
	}

	Menu = CreateWidget<UElysiumMainMenu>(PC, UElysiumMainMenu::StaticClass());
	if (!Menu)
	{
		UE_LOG(LogElysiumUI, Error, TEXT("failed to create the menu widget"));
		return;
	}
	CurrentMode = Mode;
	Menu->SetMenuMode(Mode);
	Menu->AddToViewport(/*ZOrder*/ 100);
	// A UCommonActivatableWidget is collapsed until it is activated. `bAutoActivate` only fires for
	// widgets pushed onto a UCommonActivatableWidgetContainer, and this one goes straight to the
	// viewport — so activate it by hand, or the tree builds and draws nothing.
	Menu->ActivateWidget();
	Menu->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	PushMenuScope();

	UE_LOG(LogElysiumUI, Log, TEXT("menu shown (%s)"), MenuModeName(Mode));
}

void UElysiumUISubsystem::HideMenu()
{
	if (!Menu)
	{
		return;
	}
	Menu->RemoveFromParent();
	Menu = nullptr;
	PopMenuScope();
	UE_LOG(LogElysiumUI, Log, TEXT("menu hidden"));
}
