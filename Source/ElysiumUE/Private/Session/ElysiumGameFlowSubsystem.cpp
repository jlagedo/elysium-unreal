#include "ElysiumGameFlowSubsystem.h"

#include "ElysiumContentPaths.h"
#include "ElysiumEntityWorld.h"   // FElysiumEntityWorld::Detach — New Game's session disclaim
#include "ElysiumSessionSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumSessionSettings.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayerUISubsystem.h"
#include "UI/ElysiumLoadingScreen.h"
#include "UI/ElysiumUISubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "MoviePlayer.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumFlow, Log, All);

// Boot to the menu (1) or straight into play (0). -ElysiumMap= already bypasses the menu entirely,
// so this is the story boot path's menu-vs-play switch. Read once, at game-instance init.
static TAutoConsoleVariable<int32> CVarBootMenu(
	TEXT("elysium.BootMenu"),
	1,
	TEXT("1 = cold boot raises the static main menu; 0 = boot straight into New Game."),
	ECVF_Default);

// New Game always enters the chain at genesis; this governs only the leg AFTER it. The theatre act
// so with the skip on, a transition into `sp_theatre` is rewritten to the tutorial landmark
// that act would have delivered to — applied once, at the travel funnel (ElysiumStory::ResolveIntroSkip,
// UElysiumMapSubsystem::RequestLandmarkTravel). Ours, not VtMB's: retail's own switch is the
// `vchar_skip_intro` ConVar behind the wizard's `Skip Intro` checkbox, whose reader is not yet
// recovered (`docs/vtmb/level_transitions.md`). Reversible — 0 takes the authored route.
static TAutoConsoleVariable<int32> CVarSkipIntro(
	TEXT("elysium.SkipIntro"),
	1,
	TEXT("1 = leaving genesis skips the theatre act and lands at sp_tutorial_1's `tutorial` landmark; ")
	TEXT("0 = take the authored route to sp_theatre."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarLoadingScreen(
	TEXT("elysium.LoadingScreen"),
	1,
	TEXT("1 = show the loading screen over a map load; 0 = A/B it away."),
	ECVF_Default);

// Lifetime.

void UElysiumGameFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// The map subsystem is the travel owner and the game state subsystem holds the session; both
	// are reached per call (they are siblings on the same game instance), but the dependency is
	// declared so ordering is not a matter of luck.
	Collection.InitializeDependency<UElysiumMapSubsystem>();
	Collection.InitializeDependency<UElysiumSessionSubsystem>();
	Collection.InitializeDependency<UElysiumUISubsystem>();
	if (UElysiumMapSubsystem* Maps = GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>())
	{
		MapReadyHandle = Maps->OnCurrentMapReady().AddUObject(
			this, &UElysiumGameFlowSubsystem::OnMapRuntimeReady);
		MapFailedHandle = Maps->OnCurrentMapFailed().AddUObject(
			this, &UElysiumGameFlowSubsystem::OnMapRuntimeFailed);
	}

	BootFromCommandLine();

	// The loading screen. See the header for why this is OnPrepareLoadingScreen and not PreLoadMap.
	PrepareLoadingScreenHandle = GetMoviePlayer()->OnPrepareLoadingScreen().AddUObject(
		this, &UElysiumGameFlowSubsystem::OnPrepareLoadingScreen);
	// The state half is order-independent, so it rides the ordinary map delegates.
	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(
		this, &UElysiumGameFlowSubsystem::OnPreLoadMap);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UElysiumGameFlowSubsystem::OnPostLoadMap);

	RegisterCommands();

	IConsoleManager& Console = IConsoleManager::Get();

	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("elysium.appstate"),
		TEXT("elysium.appstate — print the application state (Boot/FrontEnd/Loading/Playing/Paused/GameOver)"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			UE_LOG(LogElysiumFlow, Display, TEXT("app state: %s (session %s, world %s)"),
				ElysiumAppState::Name(State),
				IsInSession() ? TEXT("live") : TEXT("none"),
				ElysiumAppState::HoldsWorld(State) ? TEXT("held") : TEXT("running"));
		}),
		ECVF_Default));

	// The player-facing pause, not the dev hold. `elysium.pause` holds the clock and engine
	// with no menu and no state change; this is what Esc does.
	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("elysium.pausemenu"),
		TEXT("elysium.pausemenu [0|1] — pause/resume the run and raise the pause menu (no arg = toggle)"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				TogglePause();
			}
			else
			{
				SetPaused(Args[0].ToBool());
			}
		}),
		ECVF_Default));

	// New Game is a session decision, and the session is this subsystem's.
	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("elysium.newgame"),
		TEXT("elysium.newgame [clan] [m|f] [entry] — seed a new story context and enter it. Defaults "
			"to a female Malkavian with a complete starting sheet at sp_tutorial_1's tutorial landmark; "
			"clan = a name (brujah..ventrue) or the 2..8 script encoding; "
			"entry = story|tutorial|<map>[@<landmark>]"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			FElysiumNewGameRequest Request =
				ElysiumStory::MakeMockCharacterRequest(TEXT("tutorial"));
			if (Args.Num() > 0)
			{
				const int32 Parsed = FElysiumSheet::ClanFromName(Args[0]);
				if (Parsed == 0)
				{
					UE_LOG(LogElysiumFlow, Warning,
						TEXT("elysium.newgame: unknown clan '%s' (brujah|gangrel|malkavian|nosferatu|"
							"toreador|tremere|ventrue, or 2..8) — using Brujah"), *Args[0]);
				}
				Request.Clan = (Parsed == 0) ? 2 : Parsed;
				// The complete allocation is authored for the Malkavian History-adjusted order and
				// clan disciplines. An explicit different clan asks for identity only, preserving
				// the command's existing override without pretending those dots are portable.
				if (Request.Clan != FElysiumSheet::ClanFromName(TEXT("Malkavian")))
				{
					Request.HistoryId = INDEX_NONE;
					Request.Spends.Reset();
				}
			}
			if (Args.Num() > 1)
			{
				Request.bMale = !Args[1].StartsWith(TEXT("f"), ESearchCase::IgnoreCase);
			}
			if (Args.Num() > 2)
			{
				Request.EntryPoint = Args[2];
			}
			NewGame(Request);
		}),
		ECVF_Cheat));

	// The preset New Game entries, registered from their table. Each row is an ordinary request
	// through the one NewGame funnel, so a preset carries no session handling of its own and adding
	// another is a row rather than a second entry path.
	for (const ElysiumStory::FElysiumNewGameEntry& Entry : ElysiumStory::NewGameEntryTable())
	{
		// By value: the row is trivially copyable, so the lambda outliving the loop is not a question
		// a reader has to ask.
		const FConsoleCommandDelegate Run = FConsoleCommandDelegate::CreateWeakLambda(this,
			[this, Entry]()
			{
				if (!NewGame(ElysiumStory::MakeNewGameRequest(Entry)))
				{
					UE_LOG(LogElysiumFlow, Warning, TEXT("%s: '%s' needs both an export and a bake"),
						Entry.Verb, Entry.EntryPoint);
				}
			});
		ConsoleObjects.Add(Console.RegisterConsoleCommand(Entry.Verb, Entry.Help, Run, ECVF_Cheat));
		if (Entry.Alias != nullptr)
		{
			const FString AliasHelp =
				FString::Printf(TEXT("%s — compact alias for %s"), Entry.Alias, Entry.Verb);
			ConsoleObjects.Add(
				Console.RegisterConsoleCommand(Entry.Alias, *AliasHelp, Run, ECVF_Cheat));
		}
	}

	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("elysium.quittomenu"),
		TEXT("elysium.quittomenu — drop the run and return to the static main menu"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]() { QuitToMenu(); }),
		ECVF_Default));

	// The game-over path has a named command, so a script, a test and an agent can reach it.
	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("elysium.gameover"),
		TEXT("elysium.gameover [killed|masquerade] — end the run and raise the game-over screen"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const bool bMasquerade = Args.Num() > 0 && Args[0].StartsWith(TEXT("m"), ESearchCase::IgnoreCase);
			TriggerGameOver(bMasquerade ? EElysiumGameOverReason::MasqueradeBreach
			                            : EElysiumGameOverReason::Killed);
		}),
		ECVF_Cheat));
}

void UElysiumGameFlowSubsystem::Deinitialize()
{
	HideRuntimeLoadingOverlay();
	if (UElysiumMapSubsystem* Maps = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>() : nullptr)
	{
		Maps->OnCurrentMapReady().Remove(MapReadyHandle);
		Maps->OnCurrentMapFailed().Remove(MapFailedHandle);
	}
	if (PrepareLoadingScreenHandle.IsValid())
	{
		GetMoviePlayer()->OnPrepareLoadingScreen().Remove(PrepareLoadingScreenHandle);
		PrepareLoadingScreenHandle.Reset();
	}
	FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);

	UnregisterCommands();

	for (IConsoleObject* Object : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Object);
	}
	ConsoleObjects.Reset();

	Super::Deinitialize();
}

// The named verbs.

void UElysiumGameFlowSubsystem::RegisterCommands()
{
	FElysiumCommands& Registry = FElysiumCommands::Get();

	// `cancelselect` is VtMB's Escape verb, and it is one verb with two key sources: the router's
	// binding while the game has input, and `UElysiumMainMenu::NativeOnKeyDown` while a screen holds
	// it UI-only and the controller sees nothing. Both arrive here.
	Bindings.Add(Registry.Bind(TEXT("cancelselect"), [this](const FElysiumCommandCall&)
	{
		// "Close panel, else open menu" — VtMB's own reading of Escape (`docs/vtmb/controls.md`). A panel the
		// player opened is what Escape is for first; only with nothing to close does it reach pause.
		if (UElysiumUISubsystem* UI = GetGameInstance()->GetSubsystem<UElysiumUISubsystem>())
		{
			if (UI->CloseTopScreen())
			{
				return;
			}
		}

		// Pause is the only mode Escape leaves: the front end has nothing behind it to go back to,
		// and a lost run is not dismissible. In those two it is consumed and nothing happens.
		if (State == EElysiumAppState::Paused)
		{
			SetPaused(false);
		}
		else if (State == EElysiumAppState::Playing)
		{
			SetPaused(true);
		}
	}));

	Bindings.Add(Registry.Bind(TEXT("togglemainmenu"), [this](const FElysiumCommandCall&)
	{
		TogglePause();
	}));

	Bindings.Add(Registry.Bind(TEXT("pause"), [this](const FElysiumCommandCall&)
	{
		TogglePause();
	}));

	// `save quick` / `load quick` are the two the default binds carry (F9 / F12); a bare slot name
	// is a manual save. Both land on UElysiumSessionSubsystem.
	Bindings.Add(Registry.Bind(TEXT("save"), [this](const FElysiumCommandCall& Call)
	{
		const bool bQuick = Call.Args.Equals(TEXT("quick"), ESearchCase::IgnoreCase);
		// An empty manual `save` takes the next free Elysium-NNN slot; the slot name is the
		// subsystem's ring rule, not this call site's.
		SaveGame(bQuick ? FString() : Call.Args,
			bQuick ? EElysiumSaveKind::Quick : EElysiumSaveKind::Manual);
	}));

	Bindings.Add(Registry.Bind(TEXT("load"), [this](const FElysiumCommandCall& Call)
	{
		LoadGame(Call.Args.IsEmpty() ? FString(TEXT("Quick")) : Call.Args);
	}));

	Bindings.RemoveAll([](const FElysiumCommandBinding& B) { return !B.IsValid(); });
}

void UElysiumGameFlowSubsystem::UnregisterCommands()
{
	FElysiumCommands& Registry = FElysiumCommands::Get();
	for (FElysiumCommandBinding& Binding : Bindings)
	{
		Registry.Unbind(Binding);
	}
	Bindings.Reset();
}

// State.

bool UElysiumGameFlowSubsystem::SetAppState(EElysiumAppState NewState)
{
	if (!ElysiumAppState::CanEnter(State, NewState))
	{
		UE_LOG(LogElysiumFlow, Warning, TEXT("refused app-state transition %s -> %s"),
			ElysiumAppState::Name(State), ElysiumAppState::Name(NewState));
		return false;
	}
	if (NewState == State)
	{
		return true;
	}

	const EElysiumAppState Old = State;
	State = NewState;
	UE_LOG(LogElysiumFlow, Log, TEXT("app state %s -> %s"),
		ElysiumAppState::Name(Old), ElysiumAppState::Name(State));
	ApplyMenuForState(State);
	AppStateChanged.Broadcast(Old, State);
	return true;
}

void UElysiumGameFlowSubsystem::ApplyMenuForState(EElysiumAppState NewState)
{
	UElysiumUISubsystem* UI = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumUISubsystem>() : nullptr;
	if (!UI)
	{
		return;
	}
	switch (NewState)
	{
	case EElysiumAppState::FrontEnd: UI->ShowMenu(EElysiumMenuMode::Main);     break;
	case EElysiumAppState::Paused:   UI->ShowMenu(EElysiumMenuMode::Pause);    break;
	case EElysiumAppState::GameOver: UI->ShowMenu(EElysiumMenuMode::GameOver); break;
	default:                         UI->HideMenu();                          break;
	}
}

void UElysiumGameFlowSubsystem::ReleasePauseHold()
{
	if (!ElysiumAppState::HoldsWorld(State))
	{
		// A hand `elysium.pause` on a running world is the dev's, not ours — leaving it alone is
		// what keeps a hold alive across a travel.
		return;
	}
	if (UElysiumSessionSubsystem* GameState = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumSessionSubsystem>() : nullptr)
	{
		GameState->TimeControl().SetPaused(false);
	}
}

// Boot.

void UElysiumGameFlowSubsystem::BootFromCommandLine()
{
	// An explicit map (uv run elysium run play <map>) is the dev path: load it bare, with the mock character seeded
	// so the dialogue gates that read the player sheet resolve.
	int32 NewGameFlag = 1;
	FParse::Value(FCommandLine::Get(), TEXT("ElysiumNewGame="), NewGameFlag);

	FString CmdMap;
	if (FParse::Value(FCommandLine::Get(), TEXT("ElysiumMap="), CmdMap) && !CmdMap.IsEmpty())
	{
		BootKind = EBootKind::DevMap;
		BootMap = CmdMap;
	}
	// The green room with no map named boots into its own stage world: an empty level, no travel, no
	// map build. Naming a map keeps the DevMap branch above, which is how the cases that read a real
	// map's entities — the theatre camera streams, the courtroom oracle — still get one.
	else if (FParse::Param(FCommandLine::Get(), TEXT("ElysiumGreenRoom")))
	{
		BootKind = EBootKind::Stage;
		bStageWantsGreenRoom = true;
	}
	// The movement gym wants the same empty level and nothing else in it — the geometry it measures
	// against is the one it builds itself, and a map behind it would only be something to fall
	// through. Without this branch a gym launch with no `-ElysiumMap` falls to the menu.
	else if (FParse::Param(FCommandLine::Get(), TEXT("MoveGym")))
	{
		BootKind = EBootKind::Stage;
	}
	// The cast run wants the same empty level, for the same reason: the arena it records over is
	// geometry it stands itself, and a map behind it would only be somewhere else to walk.
	else if (FParse::Param(FCommandLine::Get(), TEXT("ElysiumCast")))
	{
		BootKind = EBootKind::Stage;
	}
	else if (NewGameFlag == 0)
	{
		// -ElysiumNewGame=0 loads the story map through the dev path instead of through New Game,
		// a bare load against the seeded run.
		BootKind = EBootKind::DevMap;
		BootMap = UElysiumMapSubsystem::StoryEntryMap();
	}
	else if (CVarBootMenu.GetValueOnGameThread() != 0)
	{
		BootKind = EBootKind::Menu;
	}
	else
	{
		BootKind = EBootKind::NewGame;
	}

	const FString Plan =
		(BootKind == EBootKind::Menu)    ? FString(TEXT("static menu in the boot world")) :
		(BootKind == EBootKind::NewGame) ? FString(TEXT("new game")) :
		(BootKind == EBootKind::Stage)   ? FString(bStageWantsGreenRoom
			? TEXT("green-room stage world") : TEXT("stage world")) :
		                                   FString::Printf(TEXT("dev map '%s'"), *BootMap);
	UE_LOG(LogElysiumFlow, Log, TEXT("boot plan: %s"), *Plan);
}

void UElysiumGameFlowSubsystem::NotifyWorldReady(AGameModeBase* Mode)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!Maps)
	{
		return;
	}

	// Under hard travel this runs in every fresh world. If a map load is pending, this world is the
	// baked level the engine opened for it: build that map's runtime half and settle.
	if (Maps->HasPendingMapLoad())
	{
		// Spawning starts Elysium's runtime construction but deliberately leaves the application in
		// Loading. Only the current actor's post-activation callback can expose this world.
		Maps->SpawnPendingMap();
		return;
	}

	// Quit-to-menu has opened the empty shell rather than a VtMB map, so there is no map actor to
	// publish ready. BeginPlay in that fresh shell is the completion signal for this one travel.
	if (State == EElysiumAppState::Loading && Maps->IsMenuBackdrop())
	{
		HideRuntimeLoadingOverlay();
		RuntimeLoadingFailure.Reset();
		SetAppState(EElysiumAppState::FrontEnd);
		return;
	}

	// No pending load: this is the boot world (/Game/ElysiumGenerated/Boot). Run the plan decided
	// at GI init.
	if (bBootExecuted)
	{
		return;
	}
	bBootExecuted = true;

	switch (BootKind)
	{
	case EBootKind::Menu:
	{
		bool bTravelStarted = false;
		if (Maps->EnterFrontEnd(bTravelStarted))
		{
			SetAppState(bTravelStarted ? EElysiumAppState::Loading : EElysiumAppState::FrontEnd);
		}
		break;
	}

	case EBootKind::NewGame:
		NewGame(FElysiumNewGameRequest{});
		break;

	case EBootKind::DevMap:
		// A bare dev map still needs the same complete mock sheet that the named New Game entries
		// carry so dialogue, clan variants, disciplines and skill gates see one consistent player.
		// SeedNewGameState does not travel, so the bare load below remains bare.
		if (!SeedNewGameState(ElysiumStory::MakeMockCharacterRequest(FString())))
		{
			UE_LOG(LogElysiumFlow, Error, TEXT("dev-map boot could not seed the mock character"));
			break;
		}
		if (Maps->Travel(BootMap))
		{
			SetAppState(EElysiumAppState::Loading);
		}
		break;

	case EBootKind::Stage:
	{
		// The boot world is already the empty level the stage wants, so this builds in place: no
		// travel, no map, and the state follows the same Loading -> ready -> Playing path a map load
		// does.
		FString Error;
		if (!EnterStage(Error, bStageWantsGreenRoom))
		{
			UE_LOG(LogElysiumFlow, Error, TEXT("stage boot failed: %s"), *Error);
		}
		break;
	}
	}
}

// Session.

bool UElysiumGameFlowSubsystem::ResolveEntryPoint(const FString& EntryPoint,
	FString& OutMap, FString& OutLandmark) const
{
	FString Entry = EntryPoint.TrimStartAndEnd();
	if (Entry.IsEmpty())
	{
		Entry = TEXT("story");
	}

	if (Entry.Equals(TEXT("story"), ESearchCase::IgnoreCase))
	{
		// The chain starts at genesis, and it enters through the map's own `info_player_start` — no
		// landmark, because nothing transitioned into it. From there the map drives itself: the spawn
		// lands inside the `newplayer` trigger, which fires `G.Story_State = -5` and
		// `ccmd.createplayer`, and the wizard's close teleports the player onto the exit
		// (`docs/vtmb/level_transitions.md`). Where that exit *goes* is the skip's business, not this function's.
		OutMap = ElysiumStory::ChargenMap;
		OutLandmark.Reset();
		return true;
	}

	if (Entry.Equals(TEXT("tutorial"), ESearchCase::IgnoreCase))
	{
		OutMap = ElysiumStory::TutorialMap;
		OutLandmark = ElysiumStory::TutorialLandmark;
		return true;
	}

	// "<map>" or "<map>@<landmark>".
	if (!Entry.Split(TEXT("@"), &OutMap, &OutLandmark))
	{
		OutMap = Entry;
		OutLandmark.Reset();
	}
	return !OutMap.IsEmpty();
}

bool UElysiumGameFlowSubsystem::ShouldSkipIntro()
{
	return CVarSkipIntro.GetValueOnGameThread() != 0;
}

void UElysiumGameFlowSubsystem::SetSkipIntro(bool bSkip)
{
	CVarSkipIntro->Set(bSkip ? 1 : 0, ECVF_SetByCode);
}

namespace ElysiumStory
{
	FElysiumNewGameRequest MakeMockCharacterRequest(
		const FString& EntryPoint, bool bReplayEntryMap)
	{
		FElysiumNewGameRequest Request;
		Request.Clan = FElysiumSheet::ClanFromName(TEXT("Malkavian"));
		Request.bMale = false;
		// The retail tutorial reference character: histories000.txt row 63, "Completely Batshit".
		// ApplyBaseline supplies the untouched female Malkavian sheet; no chargen dots are added here,
		// so Jack's authored modified-character guards admit the normal tutorial opener.
		Request.HistoryId = 63;
		Request.EntryPoint = EntryPoint;
		Request.bReplayEntryMap = bReplayEntryMap;
		return Request;
	}

	FElysiumNewGameRequest MakeNewGameRequest(const FElysiumNewGameEntry& Entry)
	{
		return MakeMockCharacterRequest(
			(Entry.EntryPoint != nullptr) ? FString(Entry.EntryPoint) : FString(),
			Entry.bReplayEntryMap);
	}

	bool ResolveIntroSkip(bool bSkip, FString& Map, FString& Landmark,
		FVector& Offset, bool& bHasYaw)
	{
		if (!bSkip || !Map.Equals(TheatreMap, ESearchCase::IgnoreCase))
		{
			return false;
		}

		Map = TutorialMap;
		Landmark = TutorialLandmark;
		Offset = FVector::ZeroVector;
		bHasYaw = false;
		return true;
	}

	bool ResolveTheatreExitPlacement(const FString& SourceMap, const FString& Map,
		const FString& Landmark, FVector& Offset, bool& bHasYaw)
	{
		if (!SourceMap.Equals(TheatreMap, ESearchCase::IgnoreCase)
			|| !Map.Equals(TutorialMap, ESearchCase::IgnoreCase)
			|| !Landmark.Equals(TutorialLandmark, ESearchCase::IgnoreCase))
		{
			return false;
		}

		Offset = FVector::ZeroVector;
		bHasYaw = false;
		return true;
	}
}

bool UElysiumGameFlowSubsystem::SeedNewGameState(const FElysiumNewGameRequest& Request)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumSessionSubsystem* GameState = GI ? GI->GetSubsystem<UElysiumSessionSubsystem>() : nullptr;
	if (!GameState)
	{
		UE_LOG(LogElysiumFlow, Error, TEXT("New Game: no game-state subsystem; session was not seeded"));
		return false;
	}

	int32 Clan = Request.Clan;
	if (Clan == 0)
	{
		Clan = FElysiumSheet::ClanFromName(TEXT("Brujah"));
	}
	else if (!FElysiumSheet::IsValidClan(Clan))
	{
		UE_LOG(LogElysiumFlow, Warning,
			TEXT("New Game: invalid clan %d — using Brujah"), Clan);
		Clan = FElysiumSheet::ClanFromName(TEXT("Brujah"));
	}

	// BeginNewGame clears `G`, the quest map, the snapshots, the sheet and the clock, then writes the
	// flags that survive retail's intro chain. It does not travel.
	GameState->BeginNewGame(Clan, Request.bMale);

	// A request that already carries a character — the MCP tool, a save-less dev entry, or a caller
	// that ran the wizard headless — lands it through chargen's own funnel, so a seeded character
	// and one the player built go down the same path.
	if (Request.HistoryId >= 0 || Request.Spends.Num() > 0)
	{
		FElysiumChargenState Chargen;
		Chargen.Currency = EElysiumChargenCurrency::Pools;
		Chargen.Name = GameState->PlayerName();
		Chargen.Clan = Clan;
		Chargen.bMale = Request.bMale;
		Chargen.HistoryId = Request.HistoryId;

		FElysiumChargenRules Rules;
		if (UElysiumRulebookSubsystem* Book = GameState->Rulebook())
		{
			Rules.Stats        = &Book->Stats();
			Rules.Rules        = &Book->Rules();
			Rules.Clans        = &Book->Clans();
			Rules.Histories    = &Book->Histories();
			Rules.Leveling     = &Book->Leveling();
			Rules.TraitEffects = &Book->TraitEffects();
			Rules.Feats        = &Book->Feats();
			Rules.Strings      = &Book->Strings();
			Rules.ExcludedEquip = &Book->ExcludedEquip();
		}
		ElysiumChargen::ApplyBaseline(Chargen, Rules);

		// Each spend names a trait and how many dots to buy on top of the baseline. A name the
		// rulebook does not own, or a dot the pool cannot pay for, is reported rather than silently
		// dropped — a request that asks for more than chargen allows is a caller bug.
		int32 Applied = 0, Refused = 0;
		for (const TPair<FName, int32>& Spend : Request.Spends)
		{
			EElysiumTraitContainer Container = EElysiumTraitContainer::Attributes;
			int32 Slot = INDEX_NONE;
			if (!ElysiumFindSheetSlot(*Spend.Key.ToString(), Container, Slot))
			{
				UE_LOG(LogElysiumFlow, Warning,
					TEXT("New Game: chargen spend '%s' does not name a character-sheet trait"),
					*Spend.Key.ToString());
				++Refused;
				continue;
			}
			for (int32 i = 0; i < Spend.Value; ++i)
			{
				if (ElysiumChargen::Buy(Chargen, Rules, Container, Slot))
				{
					++Applied;
				}
				else
				{
					UE_LOG(LogElysiumFlow, Warning,
						TEXT("New Game: chargen refused dot %d of %d for '%s'"),
						i + 1, Spend.Value, *Spend.Key.ToString());
					++Refused;
					break;
				}
			}
		}
		GameState->CommitChargen(Chargen);
		UE_LOG(LogElysiumFlow, Log,
			TEXT("New Game: history %d applied, %d chargen dot(s) bought, %d refused"),
			Request.HistoryId, Applied, Refused);
	}
	return true;
}

bool UElysiumGameFlowSubsystem::NewGame(const FElysiumNewGameRequest& Request)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!Maps || !GI || !GI->GetSubsystem<UElysiumSessionSubsystem>())
	{
		UE_LOG(LogElysiumFlow, Error,
			TEXT("New Game: required map/game-state subsystem is unavailable"));
		return false;
	}

	FString Map;
	FString Landmark;
	if (!ResolveEntryPoint(Request.EntryPoint, Map, Landmark))
	{
		UE_LOG(LogElysiumFlow, Warning, TEXT("New Game: cannot resolve entry point '%s'"), *Request.EntryPoint);
		return false;
	}

	// Check the destination before touching the session. Seeding is destructive — it clears `G`,
	// the quests, the snapshots, the sheet and the clock, and disclaims the running world — so a New
	// Game that cannot travel must not throw the current run away on the way to failing. This is
	// Travel's own precondition (an export beside a baked level), asked in advance.
	if (!Maps->ExportedMaps().Contains(Map))
	{
		UE_LOG(LogElysiumFlow, Warning,
			TEXT("New Game: entry map '%s' is not exported+baked — session left untouched"), *Map);
		return false;
	}

	if (!SeedNewGameState(Request))
	{
		return false;
	}

	// A replay entry forgets the destination's stored snapshot so its opening chain fires again. It is
	// a request consumed on arrival rather than a clear here, because the travel below tears the
	// current map down at end of frame and that teardown would re-freeze what was just cleared.
	if (Request.bReplayEntryMap)
	{
		Maps->RequestFreshMapState();
	}

	if (!Maps->Travel(Map, Landmark))
	{
		if (Request.bReplayEntryMap)
		{
			Maps->ConsumeFreshMapState();   // nothing travelled; do not leave it armed for a later map
		}
		UE_LOG(LogElysiumFlow, Error, TEXT("New Game: travel to '%s' was refused"), *Map);
		return false;
	}

	ReleasePauseHold();
	SetAppState(EElysiumAppState::Loading);
	return true;
}

bool UElysiumGameFlowSubsystem::LoadGame(const FString& SlotName)
{
	// One restore path, and it is the one travel already uses: the save subsystem writes the
	// payload back over the session and asks for the travel, and this subsystem owns the state move.
	UGameInstance* GI = GetGameInstance();
	UElysiumSessionSubsystem* Saves = GI ? GI->GetSubsystem<UElysiumSessionSubsystem>() : nullptr;
	if (!Saves)
	{
		return false;
	}

	// An empty slot name is the menu's "Load Game" with no picker: take the most
	// recent slot on disk, which is what a player pressing it with one save expects.
	FString Slot = SlotName;
	if (Slot.IsEmpty())
	{
		TArray<FElysiumSaveSlotInfo> Slots;
		Saves->ListSlots(Slots);
		if (Slots.Num() == 0)
		{
			UE_LOG(LogElysiumFlow, Warning, TEXT("LoadGame: there are no saves"));
			return false;
		}
		Slot = Slots[0].Slot;
	}

	FString Error;
	if (!Saves->Load(Slot, Error))
	{
		UE_LOG(LogElysiumFlow, Warning, TEXT("LoadGame('%s') refused: %s"), *Slot, *Error);
		return false;
	}

	ReleasePauseHold();
	SetAppState(EElysiumAppState::Loading);
	return true;
}

bool UElysiumGameFlowSubsystem::SaveGame(const FString& SlotName, EElysiumSaveKind Kind)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumSessionSubsystem* Saves = GI ? GI->GetSubsystem<UElysiumSessionSubsystem>() : nullptr;
	if (!Saves)
	{
		return false;
	}
	FString Slot;
	FString Error;
	if (!Saves->RequestSave({Kind, SlotName}, Slot, Error))
	{
		UE_LOG(LogElysiumFlow, Warning, TEXT("SaveGame(%s) refused: %s"),
			UElysiumSessionSubsystem::KindName(Kind), *Error);
		return false;
	}
	return true;
}

bool UElysiumGameFlowSubsystem::QuitToMenu()
{
	UGameInstance* GI = GetGameInstance();
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!Maps)
	{
		return false;
	}

	bool bTravelStarted = false;
	if (!Maps->EnterFrontEnd(bTravelStarted))
	{
		UE_LOG(LogElysiumFlow, Error, TEXT("quit to menu: could not enter the front-end shell"));
		return false;
	}

	// Only past the point of no return: the run is over.
	ReleasePauseHold();
	if (UElysiumSessionSubsystem* GameState = GI->GetSubsystem<UElysiumSessionSubsystem>())
	{
		GameState->EndSession();
	}
	SetAppState(bTravelStarted ? EElysiumAppState::Loading : EElysiumAppState::FrontEnd);
	return true;
}

bool UElysiumGameFlowSubsystem::ReloadMap()
{
	UGameInstance* GI = GetGameInstance();
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!Maps || !Maps->Reload())
	{
		return false;
	}
	ReleasePauseHold();
	SetAppState(EElysiumAppState::Loading);
	return true;
}

bool UElysiumGameFlowSubsystem::EnterGreenRoom(FString& OutError)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!Maps)
	{
		OutError = TEXT("no map subsystem");
		return false;
	}
	return EnterStage(OutError, /*bWithGreenRoom*/ true);
}

bool UElysiumGameFlowSubsystem::EnterStageWorld(FString& OutError)
{
	return EnterStage(OutError, /*bWithGreenRoom*/ false);
}

bool UElysiumGameFlowSubsystem::EnterStage(FString& OutError, bool bWithGreenRoom)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!Maps)
	{
		OutError = TEXT("no map subsystem");
		return false;
	}
	// The stage world's player is the game's player. It seats the same pawn, builds the same player
	// entity and runs the same player think a map does, so the one thing that would make it a
	// mannequin is the character record behind it: a cold stage launch never went through New Game,
	// and a zeroed record means no clan, no clan effects, no soak and no derived health block — a
	// sheet every rule that reads one (combat, disciplines, feats, skill gates) is fail-closed
	// against. Seed the developer character `elysium.newgame` seeds, through the same
	// SeedNewGameState funnel and with no entry point, because nothing here travels.
	//
	// Only when the session is carrying no character: `elysium.gr` opened mid-run must keep the run's
	// own player, and seeding is destructive (BeginNewGame clears `G`, the quests, the snapshots, the
	// sheet and the clock). The clan slot is the test because it is what a valid character always
	// has and a zeroed record never does.
	if (UElysiumSessionSubsystem* GameState = GI->GetSubsystem<UElysiumSessionSubsystem>())
	{
		if (!FElysiumSheet::IsValidClan(GameState->PlayerRecord().Sheet.Clan()))
		{
			if (SeedNewGameState(ElysiumStory::MakeMockCharacterRequest(FString())))
			{
				UE_LOG(LogElysiumFlow, Log,
					TEXT("stage: seeded the developer character — '%s', %s %s"),
					*GameState->PlayerRecord().Name,
					FElysiumSheet::ClanName(GameState->PlayerRecord().Sheet.Clan()),
					GameState->PlayerRecord().Sheet.IsMale() ? TEXT("male") : TEXT("female"));
			}
			else
			{
				UE_LOG(LogElysiumFlow, Warning,
					TEXT("stage: the developer character could not be seeded — the stage world's "
						"player will carry a zeroed sheet"));
			}
		}
	}

	// Asked from inside a stage world, this only re-arms whatever was armed over it: nothing loads,
	// so nothing will publish ready, and moving to Loading would strand the app behind the overlay
	// forever.
	const bool bAlreadyStanding = Maps->IsStageWorld();
	const bool bEntered = bWithGreenRoom
		? Maps->EnterGreenRoom(OutError) : Maps->EnterStageWorld(OutError);
	if (!bEntered)
	{
		return false;
	}
	if (!bAlreadyStanding)
	{
		// Otherwise the stage world is a play world like any other: an ordinary load, and the stage
		// actor's own ready callback is what lifts Loading to Playing. Set after the call, so a
		// refused entry leaves the state it found.
		ReleasePauseHold();
		SetAppState(EElysiumAppState::Loading);
	}
	return true;
}

// Pause and game over.

void UElysiumGameFlowSubsystem::SetPaused(bool bPaused)
{
	const EElysiumAppState Target = bPaused ? EElysiumAppState::Paused : EElysiumAppState::Playing;
	if (Target == State)
	{
		return;
	}
	if (!ElysiumAppState::CanEnter(State, Target))
	{
		// The common one: Esc in FrontEnd. There is no run to hold, and the backdrop running behind
		// the menu is the point of it — so this is a no-op, not an error.
		UE_LOG(LogElysiumFlow, Verbose, TEXT("pause ignored in %s"), ElysiumAppState::Name(State));
		return;
	}

	UGameInstance* GI = GetGameInstance();
	if (UElysiumSessionSubsystem* GameState = GI ? GI->GetSubsystem<UElysiumSessionSubsystem>() : nullptr)
	{
		// Both halves at once: engine pause freezes actor ticks, physics and animation; the clock
		// hold freezes thinks, the event queue, movers and ScheduleTask.
		GameState->TimeControl().SetPaused(bPaused);
	}
	// The pause menu follows the state, not this call (ApplyMenuForState).
	SetAppState(Target);
}

void UElysiumGameFlowSubsystem::TogglePause()
{
	SetPaused(State == EElysiumAppState::Playing);
}

void UElysiumGameFlowSubsystem::TriggerGameOver(EElysiumGameOverReason Reason)
{
	if (!ElysiumAppState::CanEnter(State, EElysiumAppState::GameOver))
	{
		UE_LOG(LogElysiumFlow, Warning, TEXT("game over ignored in %s — no run to end"),
			ElysiumAppState::Name(State));
		return;
	}
	GameOverReason = Reason;

	UGameInstance* GI = GetGameInstance();
	if (UElysiumSessionSubsystem* GameState = GI ? GI->GetSubsystem<UElysiumSessionSubsystem>() : nullptr)
	{
		GameState->TimeControl().SetPaused(true);
	}
	// GameOverReason is set above because the screen the transition raises reads it for its headline.
	SetAppState(EElysiumAppState::GameOver);

	UE_LOG(LogElysiumFlow, Display, TEXT("game over: %s"),
		Reason == EElysiumGameOverReason::MasqueradeBreach ? TEXT("masquerade breached") : TEXT("killed"));
}

// Loading screen.

void UElysiumGameFlowSubsystem::OnPrepareLoadingScreen()
{
	if (CVarLoadingScreen.GetValueOnGameThread() == 0)
	{
		return;
	}

	FLoadingScreenAttributes Attributes;
	Attributes.WidgetLoadingScreen = ElysiumLoadingUI::Build(
		NSLOCTEXT("Elysium", "Loading", "LOADING"), /*bShowThrobber*/ true);
	Attributes.bAutoCompleteWhenLoadingCompletes = true;
	Attributes.bMoviesAreSkippable = false;
	Attributes.MinimumLoadingScreenDisplayTime = GetDefault<UElysiumSessionSettings>()->LoadingScreenMinTime;
	GetMoviePlayer()->SetupLoadingScreen(Attributes);
}

void UElysiumGameFlowSubsystem::OnPreLoadMap(const FString& MapName)
{
	HideRuntimeLoadingOverlay();
	RuntimeLoadingFailure.Reset();
	// Every travel passes through here, including the ones the substrate starts on its own
	// (trigger_changelevel, a scripted ChangeMap), so the state is right even when no menu asked.
	SetAppState(EElysiumAppState::Loading);
}

void UElysiumGameFlowSubsystem::OnPostLoadMap(UWorld* LoadedWorld)
{
	// PostLoadMap ends the blocking engine load, not Elysium's runtime build. Hand the same pure-
	// Slate visual from MoviePlayer to the game viewport before the movie auto-completes; normal
	// ticks can now finish collision/player readiness without exposing a partial world.
	if (State == EElysiumAppState::Loading)
	{
		if (RuntimeLoadingFailure.IsEmpty())
		{
			ShowRuntimeLoadingOverlay(LoadedWorld,
				NSLOCTEXT("Elysium", "LoadingRuntime", "LOADING"));
		}
		else
		{
			ShowRuntimeLoadingOverlay(LoadedWorld, FText::FromString(RuntimeLoadingFailure), true);
		}
	}
}

void UElysiumGameFlowSubsystem::OnMapRuntimeReady(AElysiumMapActor* Map)
{
	if (!Map || State != EElysiumAppState::Loading)
	{
		UE_LOG(LogElysiumFlow, Warning, TEXT("ignored map-ready callback in %s from %s"),
			ElysiumAppState::Name(State), *GetNameSafe(Map));
		return;
	}

	HideRuntimeLoadingOverlay();
	RuntimeLoadingFailure.Reset();
	SetAppState(EElysiumAppState::Playing);
}

void UElysiumGameFlowSubsystem::OnMapRuntimeFailed(AElysiumMapActor* Map, const FString& Reason)
{
	// Fail closed. The map subsystem already rejected stale actors; retain Loading and replace the
	// spinner with the structured failed prerequisite so the partial world is never exposed.
	const FString MapLabel = Map ? Map->MapName : FString(TEXT("<unknown>"));
	const FText Message = FText::FromString(FString::Printf(
		TEXT("MAP LOAD FAILED\n%s\n%s"), *MapLabel, *Reason));
	RuntimeLoadingFailure = Message.ToString();
	ShowRuntimeLoadingOverlay(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr,
		Message, /*bForce*/ true);
	UE_LOG(LogElysiumFlow, Error, TEXT("runtime activation failed for %s: %s"),
		*MapLabel, *Reason);
}

void UElysiumGameFlowSubsystem::ShowRuntimeLoadingOverlay(
	UWorld* World, const FText& Message, bool bForce)
{
	if ((!bForce && CVarLoadingScreen.GetValueOnGameThread() == 0) || !World)
	{
		return;
	}
	UElysiumPlayerUISubsystem* UI = UElysiumPlayerUISubsystem::Get(World);
	if (!UI)
	{
		return;
	}
	UI->RebindToWorld(World);

	HideRuntimeLoadingOverlay();
	const bool bFailed = Message.ToString().StartsWith(TEXT("MAP LOAD FAILED"));
	RuntimeLoadingScreen = Cast<UElysiumLoadingScreen>(UI->PushWidget(
		EElysiumUILayer::RuntimeLoading,
		UElysiumLoadingScreen::StaticClass(),
		[Message, bFailed](UCommonActivatableWidget& Widget)
	{
		CastChecked<UElysiumLoadingScreen>(&Widget)->SetLoadingState(Message, !bFailed);
	}));
}

void UElysiumGameFlowSubsystem::HideRuntimeLoadingOverlay()
{
	if (RuntimeLoadingScreen)
	{
		if (UElysiumPlayerUISubsystem* UI = UElysiumPlayerUISubsystem::Get(this))
		{
			UI->RemoveWidget(EElysiumUILayer::RuntimeLoading, RuntimeLoadingScreen);
		}
		RuntimeLoadingScreen = nullptr;
	}
}
