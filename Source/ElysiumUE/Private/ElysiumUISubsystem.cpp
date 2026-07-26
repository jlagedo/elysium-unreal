#include "ElysiumUISubsystem.h"

#include "ElysiumMainMenu.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#include "CogCommon.h"
#if ENABLE_COG
#include "CogSubsystem.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogElysiumUI, Log, All);

void UElysiumUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	IConsoleManager& Console = IConsoleManager::Get();
	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("elysium.menu"),
		TEXT("Show the menu. 'elysium.menu pause' shows the pause item set."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const bool bPause = Args.Num() > 0 && Args[0].StartsWith(TEXT("p"));
			ShowMenu(bPause);
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

void UElysiumUISubsystem::ApplyInputMode(bool bUIOnly)
{
	UGameInstance* GI = GetGameInstance();
	APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}
	if (bUIOnly)
	{
		PC->SetInputMode(FInputModeUIOnly().SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock));
		PC->SetShowMouseCursor(true);
#if ENABLE_COG
		// A game screen outranks the debug UI for the mouse. While Cog holds input, ImGui consumes
		// the click before Slate sees it and every menu item is dead — so a menu coming up takes it
		// back. F1 still re-enables Cog deliberately; this only revokes an inherited capture.
		//
		// **Only when it is already enabled.** SetEnableInput dereferences the ImGui context
		// (ClearInputMouse), and Cog creates that lazily on its first tick — calling it from here at
		// boot crashed outright. `bEnableInput` defaults false and only becomes true via a call that
		// already required a live context, so "true" is a safe proxy for "initialised".
		if (UWorld* World = PC->GetWorld())
		{
			if (UCogSubsystem* Cog = World->GetSubsystem<UCogSubsystem>())
			{
				if (Cog->GetContext().GetEnableInput())
				{
					Cog->GetContext().SetEnableInput(false);
				}
			}
		}
#endif
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
}

void UElysiumUISubsystem::ShowMenu(bool bPauseMode)
{
	if (Menu)
	{
		return;
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
	Menu->SetPauseMode(bPauseMode);
	Menu->AddToViewport(/*ZOrder*/ 100);
	// A UCommonActivatableWidget is collapsed until it is activated. `bAutoActivate` only fires for
	// widgets pushed onto a UCommonActivatableWidgetContainer, and this one goes straight to the
	// viewport — so activate it by hand, or the tree builds and draws nothing.
	Menu->ActivateWidget();
	Menu->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ApplyInputMode(/*bUIOnly*/ true);

	UE_LOG(LogElysiumUI, Log, TEXT("menu shown (%s)"), bPauseMode ? TEXT("pause") : TEXT("main"));
}

void UElysiumUISubsystem::HideMenu()
{
	if (!Menu)
	{
		return;
	}
	Menu->RemoveFromParent();
	Menu = nullptr;
	ApplyInputMode(/*bUIOnly*/ false);
	UE_LOG(LogElysiumUI, Log, TEXT("menu hidden"));
}
