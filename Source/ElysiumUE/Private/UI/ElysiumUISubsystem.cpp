#include "UI/ElysiumUISubsystem.h"

#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumInputSubsystem.h"
#include "UI/ElysiumCharacterScreen.h"
#include "UI/ElysiumMainMenu.h"

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

	// The two look knobs are read at tree-build time, so an open screen has to be rebuilt for a
	// console A/B to show. They are declared by the menu's own translation unit (they are its
	// knobs); this subsystem is what owns a live screen, so the sink lives here. Found rather than
	// referenced because a static TAutoConsoleVariable in another TU has no header.
	static const TCHAR* const LookCvars[] = { TEXT("elysium.MenuLayout"), TEXT("elysium.MenuScrim") };
	for (const TCHAR* Name : LookCvars)
	{
		if (IConsoleVariable* Var = Console.FindConsoleVariable(Name))
		{
			Var->SetOnChangedCallback(FConsoleVariableDelegate::CreateWeakLambda(
				this, [this](IConsoleVariable*) { RebuildMenu(); }));
		}
	}

	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("elysium.charscreen"),
		TEXT("elysium.charscreen [sheet|info|quest] — open the character screen on a tab, or close ")
		TEXT("it if it is already up. The same screen `C` and `L` open."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			EElysiumCharacterTab Tab = EElysiumCharacterTab::Sheet;
			if (Args.Num() > 0)
			{
				if (Args[0].StartsWith(TEXT("q")))      { Tab = EElysiumCharacterTab::QuestLog; }
				else if (Args[0].StartsWith(TEXT("i"))) { Tab = EElysiumCharacterTab::Info; }
			}
			if (IsCharacterScreenOpen() && CharacterScreen->ActiveTab() == Tab)
			{
				HideCharacterScreen();
			}
			else
			{
				ShowCharacterScreen(Tab);
			}
		}),
		ECVF_Default));

	RegisterCommands();
}

// ================================================================================================
// `questlog` / `chareditor` — two doors into one screen
// ================================================================================================

void UElysiumUISubsystem::RegisterCommands()
{
	FElysiumCommands& Registry = FElysiumCommands::Get();

	// Both are declared as ButtonPairs and key-bound already (`L` and `C`), so only the press edge
	// acts here — the release is VtMB's user-command bit latching, which is not this screen's business.
	auto Door = [this](EElysiumCharacterTab Tab)
	{
		return [this, Tab](const FElysiumCommandCall& Call)
		{
			if (!Call.bPressed)
			{
				return;
			}
			if (IsCharacterScreenOpen())
			{
				// The other key while it is up switches tab rather than closing: that is what makes
				// these two doors into one screen instead of two screens.
				if (CharacterScreen->ActiveTab() != Tab)
				{
					CharacterScreen->SetActiveTab(Tab);
				}
				else
				{
					HideCharacterScreen();
				}
				return;
			}
			ShowCharacterScreen(Tab);
		};
	};

	Bindings.Add(Registry.Bind(TEXT("questlog"),   Door(EElysiumCharacterTab::QuestLog)));
	Bindings.Add(Registry.Bind(TEXT("chareditor"), Door(EElysiumCharacterTab::Sheet)));
	Bindings.RemoveAll([](const FElysiumCommandBinding& B) { return !B.IsValid(); });
}

void UElysiumUISubsystem::UnregisterCommands()
{
	FElysiumCommands& Registry = FElysiumCommands::Get();
	for (FElysiumCommandBinding& Binding : Bindings)
	{
		Registry.Unbind(Binding);
	}
	Bindings.Reset();
}

void UElysiumUISubsystem::Deinitialize()
{
	HideCharacterScreen();
	HideMenu();
	UnregisterCommands();
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

void UElysiumUISubsystem::RebuildMenu()
{
	if (!Menu)
	{
		return;
	}
	const EElysiumMenuMode Mode = CurrentMode;
	HideMenu();
	ShowMenu(Mode);
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

// ================================================================================================
// The character screen
// ================================================================================================

void UElysiumUISubsystem::PushCharacterScope()
{
	UElysiumInputSubsystem* Input = UElysiumInputSubsystem::Get(GetGameInstance());
	if (!Input || !CharacterScreen)
	{
		return;
	}

	FElysiumInputScope Scope;
	Scope.Name = TEXT("Character");
	Scope.Priority = ElysiumInput::Priority::Character;
	Scope.Mode = EElysiumInputMode::UIOnly;
	Scope.bShowCursor = true;
	// As with the menu: the screen must hold focus or its own key handler never runs, and `L` would
	// open a panel that neither Escape nor `L` could close.
	Scope.FocusWidget = CharacterScreen->TakeWidget();
	CharacterScope = Input->Push(MoveTemp(Scope));
}

void UElysiumUISubsystem::PopCharacterScope()
{
	if (UElysiumInputSubsystem* Input = UElysiumInputSubsystem::Get(GetGameInstance()))
	{
		Input->Pop(CharacterScope);
	}
	CharacterScope.Reset();
}

void UElysiumUISubsystem::ShowCharacterScreen(EElysiumCharacterTab Tab)
{
	if (CharacterScreen)
	{
		// Already up: this is a tab change, which the screen swaps in place — no teardown, so
		// keyboard focus and the input scope survive it.
		CharacterScreen->SetActiveTab(Tab);
		return;
	}

	// The screen reads live session state and belongs to a run. Opening it over the front end or a
	// lost run would show an empty sheet for a character that does not exist yet.
	if (UGameInstance* GI = GetGameInstance())
	{
		const UElysiumGameFlowSubsystem* Flow = GI->GetSubsystem<UElysiumGameFlowSubsystem>();
		if (Flow && Flow->AppState() != EElysiumAppState::Playing)
		{
			return;
		}
	}

	UGameInstance* GI = GetGameInstance();
	APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	if (!PC)
	{
		UE_LOG(LogElysiumUI, Warning, TEXT("no local player controller — character screen not shown"));
		return;
	}

	CharacterScreen = CreateWidget<UElysiumCharacterScreen>(PC, UElysiumCharacterScreen::StaticClass());
	if (!CharacterScreen)
	{
		UE_LOG(LogElysiumUI, Error, TEXT("failed to create the character screen widget"));
		return;
	}
	CharacterScreen->SetActiveTab(Tab);
	CharacterScreen->AddToViewport(/*ZOrder*/ 90);   // under the menu, which can open over it
	// Collapsed until activated by hand — `bAutoActivate` only fires inside an activatable container.
	CharacterScreen->ActivateWidget();
	CharacterScreen->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	PushCharacterScope();

	UE_LOG(LogElysiumUI, Log, TEXT("character screen shown"));
}

void UElysiumUISubsystem::HideCharacterScreen()
{
	if (!CharacterScreen)
	{
		return;
	}
	// Closing is the last thing that marks the shown hub read; the screen owns that rule.
	CharacterScreen->NotifyClosing();
	CharacterScreen->RemoveFromParent();
	CharacterScreen = nullptr;
	PopCharacterScope();
	UE_LOG(LogElysiumUI, Log, TEXT("character screen hidden"));
}

bool UElysiumUISubsystem::CloseTopScreen()
{
	if (CharacterScreen)
	{
		HideCharacterScreen();
		return true;
	}
	return false;
}
