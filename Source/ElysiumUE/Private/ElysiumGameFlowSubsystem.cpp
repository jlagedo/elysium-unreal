#include "ElysiumGameFlowSubsystem.h"

#include "ElysiumContentPaths.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumUIStyle.h"
#include "ElysiumUISubsystem.h"

#include "Camera/CameraActor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "MoviePlayer.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumFlow, Log, All);

// The menu backdrop camera, as "x,y,z,pitch,yaw" in world centimetres and degrees. Deliberately
// NOT an entry in ElysiumVantages::Table: that table is the profiling and screenshot baseline, and
// Resolve("") returns every vantage for a map — adding one there would silently change what
// profile.bat and shots.bat measure. Retune in-game with `elysium.campos`, which logs a
// paste-ready position/rotation including pitch.
static TAutoConsoleVariable<FString> CVarMenuVantage(
	TEXT("elysium.MenuVantage"),
	TEXT("-3160,-1910,-90,0,83"),
	TEXT("Menu backdrop camera as x,y,z,pitch,yaw (world cm / degrees)."),
	ECVF_Default);

// The map the menu stands in. Santa Monica's hub — the Asylum frontage — rather than the story
// entry: it is the game's signature exterior, it carries NPCs who idle in frame, and it is not the
// map New Game enters, so the menu never has to look like the level it is about to load.
static TAutoConsoleVariable<FString> CVarMenuMap(
	TEXT("elysium.MenuMap"),
	TEXT("sm_hub_1"),
	TEXT("Map loaded as the menu backdrop."),
	ECVF_Default);

// Boot to the menu (1) or straight into play (0). -ElysiumMap= already bypasses the menu entirely,
// so this is the A/B for the story boot path. Read once, at game-instance init.
static TAutoConsoleVariable<int32> CVarBootMenu(
	TEXT("elysium.BootMenu"),
	1,
	TEXT("1 = cold boot raises the main menu over a backdrop; 0 = boot straight into New Game."),
	ECVF_Default);

// Retail's New Game is a four-map chain and the first two maps need chargen (9.4) and the
// choreographed theatre act (P12). Until those land, "story" resolves to the tutorial entry — the
// same landmark the real `sp_theatre` transition arrives at, so the flow is not re-plumbed when
// P12 lands, only this default flips.
static TAutoConsoleVariable<int32> CVarSkipIntro(
	TEXT("elysium.SkipIntro"),
	1,
	TEXT("1 = New Game enters at sp_tutorial_1; 0 = enter the full chain at sp_genesisdevice_1."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarLoadingScreen(
	TEXT("elysium.LoadingScreen"),
	1,
	TEXT("1 = show the loading screen over a map load; 0 = A/B it away."),
	ECVF_Default);

// The floor stops a fast local load reading as a flicker. Spent inside the engine's own
// WaitForMovieToFinish, i.e. before the map actor's build pass.
static TAutoConsoleVariable<float> CVarLoadingScreenMinTime(
	TEXT("elysium.LoadingScreenMinTime"),
	0.75f,
	TEXT("Minimum seconds the loading screen stays up."),
	ECVF_Default);

namespace
{
	// The loading screen's visual tree.
	//
	// **Pure Slate, no UObjects.** It is rendered by FSlateLoadingSynchronizationMechanism on a
	// separate thread while the game thread is blocked inside LoadMap — which is also where the
	// engine runs garbage collection. So it takes its type from FCoreStyle (Slate's own composite
	// font, not a UFont asset) and its ground from a core brush, rather than from the
	// FElysiumUIFontLibrary / decoded-texture path every other screen uses. The palette is still
	// ours: ElysiumUI::Palette is plain FLinearColor constants with nothing to collect.
	TSharedRef<SWidget> BuildLoadingScreen()
	{
		const FSlateBrush* Solid = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));

		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 22);

		return SNew(SBorder)
			.BorderImage(Solid)
			.BorderBackgroundColor(FSlateColor(ElysiumUI::Palette::Ink))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(0.0f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Bottom)
				.Padding(FMargin(48.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.0f, 0.0f, 16.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(NSLOCTEXT("Elysium", "Loading", "LOADING"))
						.Font(Font)
						.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Gold))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						// SCircularThrobber, not SThrobber: only the circular one takes a tint, and
						// the loading screen's whole palette is the point of building it by hand.
						SNew(SCircularThrobber)
						.NumPieces(8)
						.Radius(14.0f)
						.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Blood))
					]
				]
			];
	}
}

// ================================================================================================
// Lifetime
// ================================================================================================

void UElysiumGameFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// The map subsystem is the travel owner and the game state subsystem holds the session; both
	// are reached per call (they are siblings on the same game instance), but the dependency is
	// declared so ordering is not a matter of luck.
	Collection.InitializeDependency<UElysiumMapSubsystem>();
	Collection.InitializeDependency<UElysiumGameStateSubsystem>();
	Collection.InitializeDependency<UElysiumUISubsystem>();

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

	// The player-facing pause, not the dev hold. `elysium.pause` (11.1) holds the clock and engine
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

	// Moved here from the map subsystem with 11.3: New Game is a session decision, and the session
	// is this subsystem's. The argument form is unchanged.
	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("elysium.newgame"),
		TEXT("elysium.newgame [clan] [m|f] [entry] — seed a new story context and enter it. clan = a "
			"name (brujah..ventrue) or the 2..8 script encoding; entry = story|tutorial|<map>[@<landmark>]"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			FElysiumNewGameRequest Request;
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
			}
			// Anything but an explicit f/female stays male, matching the sheet default.
			Request.bMale = !(Args.Num() > 1 && Args[1].StartsWith(TEXT("f"), ESearchCase::IgnoreCase));
			if (Args.Num() > 2)
			{
				Request.EntryPoint = Args[2];
			}
			NewGame(Request);
		}),
		ECVF_Cheat));

	ConsoleObjects.Add(Console.RegisterConsoleCommand(
		TEXT("elysium.quittomenu"),
		TEXT("elysium.quittomenu — drop the run and return to the main menu over its backdrop map"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]() { QuitToMenu(); }),
		ECVF_Default));

	// S10 — the game-over path has a named command, so a script, a test and an agent can reach it
	// while the systems that will drive it (damage 9.4, the masquerade meter) are still unbuilt.
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

// ================================================================================================
// The named verbs (11.6)
// ================================================================================================

void UElysiumGameFlowSubsystem::RegisterCommands()
{
	FElysiumCommands& Registry = FElysiumCommands::Get();

	// `cancelselect` is VtMB's Escape verb, and it is one verb with two key sources: the router's
	// binding while the game has input, and `UElysiumMainMenu::NativeOnKeyDown` while a screen holds
	// it UI-only and the controller sees nothing. Both arrive here, which is what 11.5 left open.
	Bindings.Add(Registry.Bind(TEXT("cancelselect"), [this](const FElysiumCommandCall&)
	{
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
	// is a manual save. Both land on the 11.9 seam, which logs until persistence exists.
	Bindings.Add(Registry.Bind(TEXT("save"), [this](const FElysiumCommandCall& Call)
	{
		const bool bQuick = Call.Args.Equals(TEXT("quick"), ESearchCase::IgnoreCase);
		SaveGame(bQuick ? TEXT("quick") : (Call.Args.IsEmpty() ? TEXT("slot0") : Call.Args),
			bQuick ? EElysiumSaveKind::Quick : EElysiumSaveKind::Manual);
	}));

	Bindings.Add(Registry.Bind(TEXT("load"), [this](const FElysiumCommandCall& Call)
	{
		LoadGame(Call.Args.IsEmpty() ? TEXT("quick") : Call.Args);
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

// ================================================================================================
// State
// ================================================================================================

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
		// what keeps a hold alive across a travel (S1, 11.1).
		return;
	}
	if (UElysiumGameStateSubsystem* GameState = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr)
	{
		GameState->TimeControl().SetPaused(false);
	}
}

// ================================================================================================
// Boot
// ================================================================================================

void UElysiumGameFlowSubsystem::BootFromCommandLine()
{
	// An explicit map (play.bat <map>) is the dev path: load it bare, with the mock character seeded
	// so the dialogue gates that read the player sheet resolve.
	int32 NewGameFlag = 1;
	FParse::Value(FCommandLine::Get(), TEXT("ElysiumNewGame="), NewGameFlag);

	FString CmdMap;
	if (FParse::Value(FCommandLine::Get(), TEXT("ElysiumMap="), CmdMap) && !CmdMap.IsEmpty())
	{
		BootKind = EBootKind::DevMap;
		BootMap = CmdMap;
	}
	else if (NewGameFlag == 0)
	{
		// -ElysiumNewGame=0 loads the story map through the dev path instead of through New Game,
		// to A/B a bare load against the seeded run.
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
		(BootKind == EBootKind::Menu)    ? FString(TEXT("menu over the backdrop")) :
		(BootKind == EBootKind::NewGame) ? FString(TEXT("new game")) :
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
		Maps->SpawnPendingMap();
		if (Maps->IsMenuBackdrop())
		{
			// A backdrop world builds the look only; seat the fixed camera and raise the menu over it.
			EnterMenuBackdrop();
			SetAppState(EElysiumAppState::FrontEnd);
		}
		else
		{
			SetAppState(EElysiumAppState::Playing);
		}
		return;
	}

	// No pending load: this is the boot world (/Game/Elysium). Run the plan decided at GI init.
	if (bBootExecuted)
	{
		return;
	}
	bBootExecuted = true;

	switch (BootKind)
	{
	case EBootKind::Menu:
		if (Maps->TravelForMenu(CVarMenuMap.GetValueOnGameThread()))
		{
			SetAppState(EElysiumAppState::Loading);
		}
		break;

	case EBootKind::NewGame:
		NewGame(FElysiumNewGameRequest{});
		break;

	case EBootKind::DevMap:
		// Chargen is not built yet (9.4), so seed a mock character the way New Game seeds its
		// initial game data — clan Tremere, male. This binds the player sheet the dialogue gates
		// read (`IsClan(pc,…)`, `pc.base_*`), sets Story_State=-4/Tut_Jack=0/Tut_Patch=0, and seeds
		// Linux_Wine=1 (which also keeps the tutorial's `linux_check` popup down). BeginNewGame only
		// seeds state — it does not travel — so the bare load below still runs.
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			GameState->BeginNewGame(FElysiumSheet::ClanFromName(TEXT("Tremere")), /*bMale*/ true);
		}
		if (Maps->Travel(BootMap))
		{
			SetAppState(EElysiumAppState::Loading);
		}
		break;
	}
}

void UElysiumGameFlowSubsystem::EnterMenuBackdrop()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	// Parse the vantage; a malformed cvar falls back to the world origin looking level rather than
	// refusing to show a menu.
	FVector Loc = FVector::ZeroVector;
	FRotator Rot = FRotator::ZeroRotator;
	TArray<FString> Parts;
	CVarMenuVantage.GetValueOnGameThread().ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() >= 5)
	{
		Loc = FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
		Rot = FRotator(FCString::Atof(*Parts[3]), FCString::Atof(*Parts[4]), 0.0f);
	}
	else
	{
		UE_LOG(LogElysiumFlow, Warning, TEXT("elysium.MenuVantage is malformed — using the world origin"));
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	if (ACameraActor* Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Loc, Rot, Params))
	{
		// Blend time zero: the menu is the first thing on screen, so there is nothing to blend from.
		PC->SetViewTarget(Camera);
	}
	// The menu itself is raised by the FrontEnd transition the caller makes next — one rule, one
	// place (ApplyMenuForState).
}

// ================================================================================================
// Session
// ================================================================================================

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
		// The full chain starts at chargen. Both chargen (9.4) and the theatre act (P12) are
		// unbuilt, so the shipped default enters at the tutorial — the landmark the real
		// `sp_theatre` transition arrives at, so nothing downstream changes when they land.
		if (CVarSkipIntro.GetValueOnGameThread() == 0)
		{
			OutMap = ElysiumStory::ChargenMap;
			OutLandmark.Reset();
			return true;
		}
		Entry = TEXT("tutorial");
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

bool UElysiumGameFlowSubsystem::NewGame(const FElysiumNewGameRequest& Request)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	UElysiumGameStateSubsystem* GameState = GI ? GI->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr;
	if (!Maps || !GameState)
	{
		return false;
	}

	FString Map;
	FString Landmark;
	if (!ResolveEntryPoint(Request.EntryPoint, Map, Landmark))
	{
		UE_LOG(LogElysiumFlow, Warning, TEXT("New Game: cannot resolve entry point '%s'"), *Request.EntryPoint);
		return false;
	}

	// Check the destination before touching the session. BeginNewGame is destructive — it clears
	// `G`, the quests and the sheet — so a New Game that cannot travel must not have thrown the
	// current run away on the way to failing. This is Travel's own precondition (an export beside a
	// baked level), asked in advance.
	if (!Maps->ExportedMaps().Contains(Map))
	{
		UE_LOG(LogElysiumFlow, Warning,
			TEXT("New Game: entry map '%s' is not exported+baked — session left untouched"), *Map);
		return false;
	}

	// Clan 0 means "ask" — chargen (9.4). Until it exists, the mock default stands in, which is what
	// the boot path and the MCP tool already do.
	const int32 Clan = (Request.Clan == 0) ? 2 : Request.Clan;

	// BeginNewGame clears `G`, the quest map and the sheet, then writes the flags that survive
	// retail's intro chain. It does not travel.
	GameState->BeginNewGame(Clan, Request.bMale);
	if (Request.HistoryId >= 0 || Request.Spends.Num() > 0)
	{
		// 9.4 owns the history table and the chargen spends; recorded rather than dropped so the
		// call sites that will fill them are traceable now.
		UE_LOG(LogElysiumFlow, Log,
			TEXT("New Game: history %d and %d chargen spends carried but unapplied (9.4)"),
			Request.HistoryId, Request.Spends.Num());
	}

	if (!Maps->Travel(Map, Landmark))
	{
		UE_LOG(LogElysiumFlow, Error, TEXT("New Game: travel to '%s' was refused"), *Map);
		return false;
	}

	ReleasePauseHold();
	SetAppState(EElysiumAppState::Loading);
	return true;
}

bool UElysiumGameFlowSubsystem::LoadGame(const FString& SlotName)
{
	// 11.9. The shape is settled (`save-architecture.md`): restore the session record, then travel
	// the saved map with a restore payload — which is why it goes through this one seam and not
	// through the map subsystem.
	UE_LOG(LogElysiumFlow, Warning, TEXT("LoadGame('%s'): save/load is unbuilt (roadmap 11.9)"), *SlotName);
	return false;
}

bool UElysiumGameFlowSubsystem::SaveGame(const FString& SlotName, EElysiumSaveKind Kind)
{
	const TCHAR* KindName =
		Kind == EElysiumSaveKind::Quick ? TEXT("quick") :
		Kind == EElysiumSaveKind::Auto  ? TEXT("auto")  : TEXT("manual");
	UE_LOG(LogElysiumFlow, Warning, TEXT("SaveGame('%s', %s): save/load is unbuilt (roadmap 11.9)"),
		*SlotName, KindName);
	return false;
}

bool UElysiumGameFlowSubsystem::QuitToMenu()
{
	UGameInstance* GI = GetGameInstance();
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!Maps)
	{
		return false;
	}

	const FString Map = CVarMenuMap.GetValueOnGameThread();
	if (!Maps->TravelForMenu(Map))
	{
		UE_LOG(LogElysiumFlow, Error,
			TEXT("quit to menu: backdrop map '%s' is not exported+baked — staying put"), *Map);
		return false;
	}

	// Only past the point of no return: the run is over.
	ReleasePauseHold();
	if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
	{
		GameState->EndSession();
	}
	SetAppState(EElysiumAppState::Loading);
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

// ================================================================================================
// Pause and game over
// ================================================================================================

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
	if (UElysiumGameStateSubsystem* GameState = GI ? GI->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr)
	{
		// Both halves at once: engine pause freezes actor ticks, physics and animation; the clock
		// hold freezes thinks, the event queue, movers and ScheduleTask (S1).
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
	if (UElysiumGameStateSubsystem* GameState = GI ? GI->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr)
	{
		GameState->TimeControl().SetPaused(true);
	}
	// GameOverReason is set above because the screen the transition raises reads it for its headline.
	SetAppState(EElysiumAppState::GameOver);

	UE_LOG(LogElysiumFlow, Display, TEXT("game over: %s"),
		Reason == EElysiumGameOverReason::MasqueradeBreach ? TEXT("masquerade breached") : TEXT("killed"));
}

// ================================================================================================
// Loading screen
// ================================================================================================

void UElysiumGameFlowSubsystem::OnPrepareLoadingScreen()
{
	if (CVarLoadingScreen.GetValueOnGameThread() == 0)
	{
		return;
	}

	FLoadingScreenAttributes Attributes;
	Attributes.WidgetLoadingScreen = BuildLoadingScreen();
	Attributes.bAutoCompleteWhenLoadingCompletes = true;
	Attributes.bMoviesAreSkippable = false;
	Attributes.MinimumLoadingScreenDisplayTime = CVarLoadingScreenMinTime.GetValueOnGameThread();
	GetMoviePlayer()->SetupLoadingScreen(Attributes);
}

void UElysiumGameFlowSubsystem::OnPreLoadMap(const FString& MapName)
{
	// Every travel passes through here, including the ones the substrate starts on its own
	// (trigger_changelevel, a scripted ChangeMap), so the state is right even when no menu asked.
	SetAppState(EElysiumAppState::Loading);
}

void UElysiumGameFlowSubsystem::OnPostLoadMap(UWorld* LoadedWorld)
{
	// Deliberately does not leave Loading: the level is in memory, but AElysiumMapActor has not
	// built the map's runtime half yet (collision, the substrate, bodies). NotifyWorldReady — which
	// runs after that build — is what settles into FrontEnd or Playing.
}
