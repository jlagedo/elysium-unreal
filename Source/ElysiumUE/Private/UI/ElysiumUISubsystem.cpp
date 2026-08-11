#include "UI/ElysiumUISubsystem.h"

#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumInputScope.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayerUISubsystem.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"
#include "UI/ElysiumCharacterScreen.h"
#include "UI/ElysiumCharacterStage.h"
#include "UI/ElysiumChargenPopup.h"
#include "UI/ElysiumMainMenu.h"

#include "Blueprint/UserWidget.h"
#include "CommonActivatableWidget.h"
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

	// The character stage stands real actors in the map's world (UI/ElysiumCharacterStage.h), so the
	// screen holding it is application-lifetime but its rig is map-epoch state (S4). Close the screen
	// at the boundary, while that world is still standing.
	if (UElysiumMapSubsystem* Maps = Collection.InitializeDependency<UElysiumMapSubsystem>())
	{
		MapEpochRetiredHandle = Maps->OnMapEpochRetired().AddUObject(
			this, &UElysiumUISubsystem::OnMapEpochRetired);
	}

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

void UElysiumUISubsystem::ReleaseChargenHold()
{
	// Drop the wizard's pause without running its close tail. HideCharacterScreen's own tail
	// unpauses and then executes `teleport_player firetrans`, which is the recovered CharEditPanel
	// exit — correct when the panel closes, wrong when the world it would teleport into is going
	// away. Clearing the latch first is what keeps that tail on the panel's own path.
	if (!bChargenHold)
	{
		return;
	}
	if (UElysiumGameStateSubsystem* State = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr)
	{
		State->TimeControl().SetPaused(false);
	}
	bChargenHold = false;
}

void UElysiumUISubsystem::OnMapEpochRetired(uint64 Epoch)
{
	// A map epoch ending is teardown, not a panel close — the same distinction Deinitialize makes.
	// HideCharacterScreen then tears the stage down and drops both screen pointers; without it the
	// stage's FGCObject keeps the outgoing world's camera, backdrop and body alive for the rest of
	// the session, and chargen refuses to open again because CharacterScreen still reads as up.
	ReleaseChargenHold();
	HideCharacterScreen();
}

void UElysiumUISubsystem::Deinitialize()
{
	if (MapEpochRetiredHandle.IsValid())
	{
		if (UElysiumMapSubsystem* Maps = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>() : nullptr)
		{
			Maps->OnMapEpochRetired().Remove(MapEpochRetiredHandle);
		}
		MapEpochRetiredHandle.Reset();
	}
	ReleaseChargenHold();
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

	UElysiumPlayerUISubsystem* PlayerUI = UElysiumPlayerUISubsystem::Get(GetGameInstance());
	if (!PlayerUI)
	{
		UE_LOG(LogElysiumUI, Warning, TEXT("no local-player UI root — menu not shown"));
		return;
	}

	Menu = Cast<UElysiumMainMenu>(PlayerUI->PushWidget(
		EElysiumUILayer::SystemModal,
		UElysiumMainMenu::StaticClass(),
		[Mode](UCommonActivatableWidget& Widget)
		{
			UElysiumMainMenu& Screen = *CastChecked<UElysiumMainMenu>(&Widget);
			Screen.SetMenuMode(Mode);
			Screen.ConfigureScreenPolicy(EElysiumUIScreenKind::Menu);
		}));
	if (!Menu)
	{
		UE_LOG(LogElysiumUI, Error, TEXT("failed to push the menu widget"));
		return;
	}
	CurrentMode = Mode;

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
	if (UElysiumPlayerUISubsystem* PlayerUI = UElysiumPlayerUISubsystem::Get(GetGameInstance()))
	{
		PlayerUI->RemoveWidget(EElysiumUILayer::SystemModal, Menu);
	}
	Menu = nullptr;
	UE_LOG(LogElysiumUI, Log, TEXT("menu hidden"));
}

// ================================================================================================
// The character screen
// ================================================================================================

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
	UElysiumPlayerUISubsystem* PlayerUI = UElysiumPlayerUISubsystem::Get(GI);
	if (!PC || !PlayerUI)
	{
		UE_LOG(LogElysiumUI, Warning, TEXT("no local player/UI root — character screen not shown"));
		return;
	}

	// The in-game screen edits a SCRATCH, not the character: a dot bought here is pending until
	// ACCEPT, so CANCEL is a discard rather than an undo log. Same shape chargen uses, differing
	// only in the currency (`Substrate/ElysiumChargen.h`).
	TSharedPtr<FElysiumChargenState> Scratch;
	if (UElysiumGameStateSubsystem* State = GI->GetSubsystem<UElysiumGameStateSubsystem>())
	{
		const FElysiumPlayer* Player = State->PlayerEntity();
		const FElysiumSheetEffects* Effects = Player ? Player->SheetEffects() : nullptr;
		static const FElysiumSheetEffects Empty;

		Scratch = MakeShared<FElysiumChargenState>();
		ElysiumChargen::BeginLevelUp(*Scratch, State->PlayerSheet(), Effects ? *Effects : Empty);
		Scratch->Name = State->PlayerName();
	}

	// The body behind the panels. Raised for BOTH hosts: VtMB's own screen draws the character
	// through its translucent panels in game as well as at chargen.
	CharacterStage = MakeShared<FElysiumCharacterStage>();
	CharacterStage->Raise(PC->GetWorld(), PC);
	FElysiumCharacterScreenMode ScreenMode;
	ScreenMode.Tabs = { EElysiumCharacterTab::Sheet, EElysiumCharacterTab::Info,
		EElysiumCharacterTab::QuestLog };
	ScreenMode.Spend = EElysiumSpendMode::LevelUp;

	CharacterScreen = Cast<UElysiumCharacterScreen>(PlayerUI->PushWidget(
		EElysiumUILayer::GameModal,
		UElysiumCharacterScreen::StaticClass(),
		[this, Tab, Scratch, ScreenMode](UCommonActivatableWidget& Widget)
		{
			UElysiumCharacterScreen& Screen = *CastChecked<UElysiumCharacterScreen>(&Widget);
			Screen.SetMode(ScreenMode);
			Screen.SetActiveTab(Tab);
			Screen.SetSpendState(Scratch);
			Screen.OnAccept.BindUObject(this, &UElysiumUISubsystem::CommitCharacterSpend);
			Screen.OnCancel.BindUObject(this, &UElysiumUISubsystem::HideCharacterScreen);
			Screen.OnCharacterChanged.BindUObject(
				this, &UElysiumUISubsystem::UpdateCharacterStageBody);
			Screen.ConfigureScreenPolicy(EElysiumUIScreenKind::Character);
		}));
	if (!CharacterScreen)
	{
		CharacterStage->Teardown();
		CharacterStage.Reset();
		UE_LOG(LogElysiumUI, Error, TEXT("failed to push the character screen widget"));
		return;
	}
	UpdateCharacterStageBody();

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
	UElysiumPlayerUISubsystem* PlayerUI = UElysiumPlayerUISubsystem::Get(GI);
	UElysiumGameStateSubsystem* State = GI ? GI->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr;
	UElysiumRulebookSubsystem* Book = GI ? GI->GetSubsystem<UElysiumRulebookSubsystem>() : nullptr;
	if (!PC || !PlayerUI || !State || !Book)
	{
		UE_LOG(LogElysiumUI, Warning, TEXT("chargen: no controller / UI root / session — not shown"));
		return;
	}

	TSharedRef<FElysiumChargenState> Chargen = MakeShared<FElysiumChargenState>();
	Chargen->Currency = EElysiumChargenCurrency::Pools;
	Chargen->Name = State->PlayerName();
	// Whatever the session already seeded stands as the opening selection - `createplayer` commits
	// OVER a player that exists, it does not build one (`docs/vtmb/game_runtime.md`). The quiz route
	// overwrites both before the sheet is raised.
	{
		const FElysiumSheet& Seeded = State->PlayerSheet();
		Chargen->Clan = FElysiumSheet::IsValidClan(Seeded.Clan()) ? Seeded.Clan() : 2;
		Chargen->bMale = Seeded.IsMale();
		Chargen->HistoryId = 0;
	}
	Chargen->bSkipIntro = UElysiumGameFlowSubsystem::ShouldSkipIntro();
	PendingChargen = Chargen;

	// The retail panel releases `v_unpause` from its close tail. Take the matching modal hold here,
	// through time control only: chargen leaves the application state Playing and never raises the
	// pause menu.
	State->TimeControl().SetPaused(true);
	bChargenHold = true;

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
			ChargenPopup = Cast<UElysiumChargenPopup>(PlayerUI->PushWidget(
				EElysiumUILayer::GameModal,
				UElysiumChargenPopup::StaticClass(),
				[this](UCommonActivatableWidget& Widget)
				{
					UElysiumChargenPopup& Popup = *CastChecked<UElysiumChargenPopup>(&Widget);
					Popup.SetRun(ChargenRun);
					Popup.OnAnswer.BindUObject(this, &UElysiumUISubsystem::AnswerChargenPopup);
					Popup.ConfigureScreenPolicy(EElysiumUIScreenKind::Chargen);
				}));
			if (ChargenPopup)
			{
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
		if (UElysiumPlayerUISubsystem* PlayerUI = UElysiumPlayerUISubsystem::Get(GI))
		{
			PlayerUI->RemoveWidget(EElysiumUILayer::GameModal, ChargenPopup);
		}
		ChargenPopup = nullptr;
	}
	ChargenRun.Reset();
	OpenChargenSheet();
}

void UElysiumUISubsystem::OpenChargenSheet()
{
	UGameInstance* GI = GetGameInstance();
	APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
	UElysiumPlayerUISubsystem* PlayerUI = UElysiumPlayerUISubsystem::Get(GI);
	UElysiumRulebookSubsystem* Book = GI ? GI->GetSubsystem<UElysiumRulebookSubsystem>() : nullptr;
	if (!PC || !PlayerUI || !Book || !PendingChargen.IsValid())
	{
		return;
	}
	TSharedRef<FElysiumChargenState> Chargen = PendingChargen.ToSharedRef();

	// The three axes the shell already carries: two tabs instead of three, the pools as the
	// currency, and a name the player types. Everything else about the screen is the shared one.
	FElysiumCharacterScreenMode ScreenMode;
	ScreenMode.Tabs = { EElysiumCharacterTab::Base, EElysiumCharacterTab::Sheet };
	ScreenMode.Spend = EElysiumSpendMode::Chargen;
	ScreenMode.bNameEditable = true;

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

	CharacterScreen = Cast<UElysiumCharacterScreen>(PlayerUI->PushWidget(
		EElysiumUILayer::GameModal,
		UElysiumCharacterScreen::StaticClass(),
		[this, ScreenMode, Chargen](UCommonActivatableWidget& Widget)
		{
			UElysiumCharacterScreen& Screen = *CastChecked<UElysiumCharacterScreen>(&Widget);
			Screen.SetMode(ScreenMode);
			Screen.SetActiveTab(EElysiumCharacterTab::Base);
			Screen.SetSpendState(Chargen);
			Screen.OnAccept.BindUObject(this, &UElysiumUISubsystem::CommitChargen);
			Screen.OnCancel.BindUObject(this, &UElysiumUISubsystem::HideCharacterScreen);
			Screen.OnCharacterChanged.BindUObject(
				this, &UElysiumUISubsystem::UpdateCharacterStageBody);
			Screen.ConfigureScreenPolicy(EElysiumUIScreenKind::Chargen);
		}));
	if (!CharacterScreen)
	{
		UE_LOG(LogElysiumUI, Error, TEXT("failed to push the chargen sheet widget"));
		return;
	}
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
		UElysiumGameFlowSubsystem::SetSkipIntro(Chargen->bSkipIntro);
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
	if (!CharacterScreen && !ChargenPopup)
	{
		return;
	}
	// Closing is the last thing that marks the shown hub read; the screen owns that rule.
	if (CharacterScreen)
	{
		CharacterScreen->NotifyClosing();
	}
	// The stage restores the player's own camera before removing the active screen deactivates it
	// and releases its input scope.
	if (CharacterStage.IsValid())
	{
		CharacterStage->Teardown();
		CharacterStage.Reset();
	}
	UElysiumPlayerUISubsystem* PlayerUI = UElysiumPlayerUISubsystem::Get(GetGameInstance());
	if (CharacterScreen)
	{
		if (PlayerUI)
		{
			PlayerUI->RemoveWidget(EElysiumUILayer::GameModal, CharacterScreen);
		}
		CharacterScreen = nullptr;
	}
	if (ChargenPopup)
	{
		if (PlayerUI)
		{
			PlayerUI->RemoveWidget(EElysiumUILayer::GameModal, ChargenPopup);
		}
		ChargenPopup = nullptr;
	}
	ChargenRun.Reset();
	PendingChargen.Reset();

	// CharEditPanel's recovered mode!=0 close tail: unpause, then execute
	// `teleport_player firetrans`. Both ACCEPT and CANCEL close through here. The latch keeps the
	// in-game Sheet/Quest/Info and level-up hosts on their ordinary close path.
	if (bChargenHold)
	{
		if (UElysiumGameStateSubsystem* State = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr)
		{
			State->TimeControl().SetPaused(false);
		}
		bChargenHold = false;
		FElysiumCommands::Get().Execute(TEXT("teleport_player firetrans"));
	}
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
