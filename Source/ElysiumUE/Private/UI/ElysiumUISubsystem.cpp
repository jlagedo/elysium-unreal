#include "UI/ElysiumUISubsystem.h"

#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"
#include "ElysiumInputSubsystem.h"
#include "UI/ElysiumCharacterScreen.h"
#include "UI/ElysiumCharacterStage.h"
#include "UI/ElysiumChargenPopup.h"
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

	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("elysium.chargen"),
		TEXT("elysium.chargen — open character creation. The same screen the genesis map's ")
		TEXT("`newplayer` trigger opens through `ccmd.createplayer`, reachable without it."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]() { ShowChargen(); }),
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

	// `createplayer` is the content's verb, not the player's: the genesis map's `newplayer` trigger
	// fires `ccmd.createplayer` and nothing is bound to a key. It is declared `Once`, so there is no
	// release edge to filter.
	Bindings.Add(Registry.Bind(TEXT("createplayer"),
		[this](const FElysiumCommandCall&) { ShowChargen(); }));
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

void UElysiumUISubsystem::PushCharacterScope(int32 Priority, const TCHAR* Name)
{
	UElysiumInputSubsystem* Input = UElysiumInputSubsystem::Get(GetGameInstance());
	if (!Input || !CharacterScreen)
	{
		return;
	}

	FElysiumInputScope Scope;
	Scope.Name = Name;
	Scope.Priority = Priority;
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

	// The in-game screen edits a SCRATCH, not the character: a dot bought here is pending until
	// ACCEPT, so CANCEL is a discard rather than an undo log. Same shape chargen uses, differing
	// only in the currency (`Substrate/ElysiumChargen.h`).
	if (UElysiumGameStateSubsystem* State = GI->GetSubsystem<UElysiumGameStateSubsystem>())
	{
		const FElysiumPlayer* Player = State->PlayerEntity();
		const FElysiumSheetEffects* Effects = Player ? Player->SheetEffects() : nullptr;
		static const FElysiumSheetEffects Empty;

		TSharedRef<FElysiumChargenState> Scratch = MakeShared<FElysiumChargenState>();
		ElysiumChargen::BeginLevelUp(*Scratch, State->PlayerSheet(), Effects ? *Effects : Empty);
		Scratch->Name = State->PlayerName();
		CharacterScreen->SetSpendState(Scratch);

		CharacterScreen->OnAccept.BindUObject(this, &UElysiumUISubsystem::CommitCharacterSpend);
		CharacterScreen->OnCancel.BindUObject(this, &UElysiumUISubsystem::HideCharacterScreen);
		CharacterScreen->OnCharacterChanged.BindUObject(
			this, &UElysiumUISubsystem::UpdateCharacterStageBody);
	}

	// The body behind the panels. Raised for BOTH hosts: VtMB's own screen draws the character
	// through its translucent panels in game as well as at chargen.
	CharacterStage = MakeShared<FElysiumCharacterStage>();
	CharacterStage->Raise(PC->GetWorld(), PC);
	UpdateCharacterStageBody();

	CharacterScreen->AddToViewport(/*ZOrder*/ 90);   // under the menu, which can open over it
	// Collapsed until activated by hand — `bAutoActivate` only fires inside an activatable container.
	CharacterScreen->ActivateWidget();
	CharacterScreen->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	PushCharacterScope(ElysiumInput::Priority::Character, TEXT("Character"));

	UE_LOG(LogElysiumUI, Log, TEXT("character screen shown"));
}

void UElysiumUISubsystem::ShowChargen()
{
	if (CharacterScreen)
	{
		// Already up — a second `createplayer` is the map's trigger re-firing, not a request for a
		// second wizard. The `newplayer` trigger disables itself after one shot, so this is defence.
		return;
	}

	UGameInstance* GI = GetGameInstance();
	APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	UElysiumGameStateSubsystem* State = GI ? GI->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr;
	UElysiumRulebookSubsystem* Book = GI ? GI->GetSubsystem<UElysiumRulebookSubsystem>() : nullptr;
	if (!PC || !State || !Book)
	{
		UE_LOG(LogElysiumUI, Warning, TEXT("chargen: no controller / session — not shown"));
		return;
	}

	TSharedRef<FElysiumChargenState> Chargen = MakeShared<FElysiumChargenState>();
	Chargen->Currency = EElysiumChargenCurrency::Pools;
	Chargen->Name = State->PlayerName();
	// Whatever the session already seeded stands as the opening selection - `createplayer` commits
	// OVER a player that exists, it does not build one (`docs/game_runtime.md`). The quiz route
	// overwrites both before the sheet is raised.
	{
		const FElysiumSheet& Seeded = State->PlayerSheet();
		Chargen->Clan = FElysiumSheet::IsValidClan(Seeded.Clan()) ? Seeded.Clan() : 2;
		Chargen->bMale = Seeded.IsMale();
		Chargen->HistoryId = 0;
	}
	PendingChargen = Chargen;

	// The stage stands behind whichever of the two is up, so it is raised before either.
	CharacterStage = MakeShared<FElysiumCharacterStage>();
	CharacterStage->Raise(PC->GetWorld(), PC);
	CharacterStage->SetBody(Book->Clans().PlayerBodyStem(Chargen->Clan, !Chargen->bMale, 0));

	// The wizard's own entry popup: route 1 walks the quiz, route 2 drops straight to the sheet.
	// With no `charcreatewizard.txt` there is no quiz to offer, so the sheet is all there is.
	if (Book->Wizard().IsValid())
	{
		ChargenRun = MakeShared<FElysiumWizRun>();
		ElysiumChargen::WizBegin(*ChargenRun, Book->Wizard(), *Chargen,
			ElysiumChargen::WizEntryPopup);
		if (ChargenRun->IsActive())
		{
			ChargenPopup = CreateWidget<UElysiumChargenPopup>(PC, UElysiumChargenPopup::StaticClass());
			if (ChargenPopup)
			{
				ChargenPopup->SetRun(ChargenRun);
				ChargenPopup->OnAnswer.BindUObject(this, &UElysiumUISubsystem::AnswerChargenPopup);
				ChargenPopup->AddToViewport(/*ZOrder*/ 91);
				ChargenPopup->ActivateWidget();
				ChargenPopup->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

				if (UElysiumInputSubsystem* Input = UElysiumInputSubsystem::Get(GI))
				{
					FElysiumInputScope Scope;
					Scope.Name = TEXT("Chargen");
					Scope.Priority = ElysiumInput::Priority::Chargen;
					Scope.Mode = EElysiumInputMode::UIOnly;
					Scope.bShowCursor = true;
					Scope.FocusWidget = ChargenPopup->TakeWidget();
					CharacterScope = Input->Push(MoveTemp(Scope));
				}
				UE_LOG(LogElysiumUI, Log, TEXT("chargen: entry popup shown"));
				return;
			}
		}
		ChargenRun.Reset();
	}

	OpenChargenSheet();
}

void UElysiumUISubsystem::AnswerChargenPopup(int32 Index)
{
	if (!ChargenRun.IsValid() || !PendingChargen.IsValid())
	{
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UElysiumRulebookSubsystem* Book = GI ? GI->GetSubsystem<UElysiumRulebookSubsystem>() : nullptr;

	if (ElysiumChargen::WizChoose(*ChargenRun, *PendingChargen, Index))
	{
		// Still questions to ask. The sex or the clan may already have moved, so the body follows.
		if (ChargenPopup) { ChargenPopup->Refresh(); }
		if (CharacterStage.IsValid() && Book)
		{
			CharacterStage->SetBody(Book->Clans().PlayerBodyStem(
				PendingChargen->Clan, !PendingChargen->bMale, 0));
		}
		return;
	}

	// The chain ended. Whatever the quiz tallied becomes an ordering, and the ordering a clan
	// suggestion - overridable on the Base tab, which is where the player lands next.
	if (Book)
	{
		ElysiumChargen::WizOrdering(*PendingChargen, Book->Wizard(), PendingChargen->Ordering);
		const int32 Suggested = ElysiumChargen::SuggestClan(
			Book->Wizard(), Book->Clans(), PendingChargen->Ordering);
		if (FElysiumSheet::IsValidClan(Suggested))
		{
			PendingChargen->Clan = Suggested;
		}
		UE_LOG(LogElysiumUI, Log, TEXT("chargen: quiz done - %d trait(s) ordered, clan %d suggested"),
			PendingChargen->Ordering.Num(), Suggested);
	}

	if (ChargenPopup)
	{
		ChargenPopup->RemoveFromParent();
		ChargenPopup = nullptr;
	}
	ChargenRun.Reset();
	PopCharacterScope();
	OpenChargenSheet();
}

void UElysiumUISubsystem::OpenChargenSheet()
{
	UGameInstance* GI = GetGameInstance();
	APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	UElysiumRulebookSubsystem* Book = GI ? GI->GetSubsystem<UElysiumRulebookSubsystem>() : nullptr;
	if (!PC || !Book || !PendingChargen.IsValid())
	{
		return;
	}
	TSharedRef<FElysiumChargenState> Chargen = PendingChargen.ToSharedRef();

	CharacterScreen = CreateWidget<UElysiumCharacterScreen>(PC, UElysiumCharacterScreen::StaticClass());
	if (!CharacterScreen)
	{
		UE_LOG(LogElysiumUI, Error, TEXT("failed to create the character screen widget"));
		return;
	}

	// The three axes the shell already carries: two tabs instead of three, the pools as the
	// currency, and a name the player types. Everything else about the screen is the shared one.
	FElysiumCharacterScreenMode ScreenMode;
	ScreenMode.Tabs = { EElysiumCharacterTab::Base, EElysiumCharacterTab::Sheet };
	ScreenMode.Spend = EElysiumSpendMode::Chargen;
	ScreenMode.bNameEditable = true;
	CharacterScreen->SetMode(ScreenMode);
	CharacterScreen->SetActiveTab(EElysiumCharacterTab::Base);

	FElysiumChargenRules Rules;
	Rules.Stats        = &Book->Stats();
	Rules.Rules        = &Book->Rules();
	Rules.Clans        = &Book->Clans();
	Rules.Histories    = &Book->Histories();
	Rules.Leveling     = &Book->Leveling();
	Rules.TraitEffects = &Book->TraitEffects();
	Rules.Feats        = &Book->Feats();
	Rules.Strings      = &Book->Strings();
	ElysiumChargen::ApplyBaseline(*Chargen, Rules);

	CharacterScreen->SetSpendState(Chargen);
	CharacterScreen->OnAccept.BindUObject(this, &UElysiumUISubsystem::CommitChargen);
	CharacterScreen->OnCancel.BindUObject(this, &UElysiumUISubsystem::HideCharacterScreen);
	CharacterScreen->OnCharacterChanged.BindUObject(
		this, &UElysiumUISubsystem::UpdateCharacterStageBody);

	CharacterScreen->AddToViewport(/*ZOrder*/ 90);
	CharacterScreen->ActivateWidget();
	CharacterScreen->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	// Above the character screen's own priority: there is no run to fall back into while the wizard
	// is up, so nothing beneath it may act.
	PushCharacterScope(ElysiumInput::Priority::Chargen, TEXT("Chargen"));
	UpdateCharacterStageBody();

	UE_LOG(LogElysiumUI, Log, TEXT("chargen sheet opened (clan %d, %d points to spend)"),
		Chargen->Clan, Chargen->Pools.Total());
}

void UElysiumUISubsystem::CommitChargen()
{
	UGameInstance* GI = GetGameInstance();
	UElysiumGameStateSubsystem* State = GI ? GI->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr;
	TSharedPtr<FElysiumChargenState> Chargen =
		CharacterScreen ? CharacterScreen->SpendState() : nullptr;

	if (State && Chargen.IsValid() && Chargen->Currency == EElysiumChargenCurrency::Pools)
	{
		State->CommitChargen(*Chargen);
	}
	PendingChargen.Reset();
	HideCharacterScreen();
}

void UElysiumUISubsystem::UpdateCharacterStageBody()
{
	if (!CharacterStage.IsValid())
	{
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UElysiumRulebookSubsystem* Book = GI ? GI->GetSubsystem<UElysiumRulebookSubsystem>() : nullptr;
	TSharedPtr<FElysiumChargenState> Spend =
		CharacterScreen ? CharacterScreen->SpendState() : nullptr;
	if (!Book || !Spend.IsValid())
	{
		return;
	}
	// Armour slot 0: the screen shows the character, not what they are wearing. The equipped tier
	// arrives with inventory.
	CharacterStage->SetBody(Book->Clans().PlayerBodyStem(Spend->Clan, !Spend->bMale, /*ArmorSlot*/ 0));
}

void UElysiumUISubsystem::CommitCharacterSpend()
{
	UGameInstance* GI = GetGameInstance();
	UElysiumGameStateSubsystem* State = GI ? GI->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr;
	TSharedPtr<FElysiumChargenState> Scratch =
		CharacterScreen ? CharacterScreen->SpendState() : nullptr;

	if (State && Scratch.IsValid() && Scratch->Currency == EElysiumChargenCurrency::Experience)
	{
		State->PlayerSheet() = Scratch->Sheet;
		// Through the character's own recompute, never `RecomputeCurrent(Stats)` — that overload
		// still compiles and silently drops every clan bane (`Source/ElysiumUE/CLAUDE.md`).
		if (FElysiumPlayer* Player = State->PlayerEntity())
		{
			Player->RecomputeSheet();
		}
		UE_LOG(LogElysiumUI, Log, TEXT("level-up spend committed (%d experience left)"),
			Scratch->Experience());
	}
	HideCharacterScreen();
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
	// The stage restores the player's own camera as it goes, so this must happen before the scope
	// pops and input returns to the game.
	if (CharacterStage.IsValid())
	{
		CharacterStage->Teardown();
		CharacterStage.Reset();
	}
	if (ChargenPopup)
	{
		ChargenPopup->RemoveFromParent();
		ChargenPopup = nullptr;
	}
	ChargenRun.Reset();
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
