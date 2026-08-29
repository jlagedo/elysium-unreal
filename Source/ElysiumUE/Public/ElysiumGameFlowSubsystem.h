#pragma once

#include "CoreMinimal.h"
#include "ElysiumAppState.h"
#include "ElysiumCommands.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ElysiumGameFlowSubsystem.generated.h"

class AGameModeBase;
class UWorld;
class AElysiumMapActor;
class UElysiumLoadingScreen;

// Which slot ring a save belongs to. `trigger_autosave` fires Auto; the pause menu fires Manual;
// the quicksave binding fires Quick. The slots themselves live on UElysiumSaveSubsystem.
enum class EElysiumSaveKind : uint8
{
	Manual,
	Quick,
	Auto,
};

// Why the run ended. Both are VtMB loss conditions (`docs/vtmb/game_runtime.md` §3): the combat character's
// death path, and the masquerade meter reaching 5.
enum class EElysiumGameOverReason : uint8
{
	Killed,
	MasqueradeBreach,
};

// What a New Game is, as data. The four-map retail chain is driven entirely by the maps' own
// entities and landmarks (`docs/vtmb/level_transitions.md`), so the only thing the flow chooses is where the
// chain is entered — everything after that is content.
struct FElysiumNewGameRequest
{
	// The level-script 2..8 clan encoding (2 Brujah .. 8 Ventrue). 0 = ask, i.e. run chargen —
	// NewGame seeds Brujah so the genesis wizard has a valid player to edit; the wizard overwrites
	// clan and sex when the player commits.
	int32 Clan = 0;
	bool  bMale = true;

	// m_iVHistoryID. -1 = unset; chargen writes it.
	int32 HistoryId = -1;

	// Chargen's attribute/ability/discipline allocation, applied onto the sheet.
	TMap<FName, int32> Spends;

	// Where the chain is entered:
	//   ""/"story"   the retail chain from `sp_genesisdevice_1` (chargen), at its info_player_start
	//   "tutorial"   straight to `sp_tutorial_1` at its `tutorial` landmark — the dev shortcut
	//   "<map>"           a bare map load
	//   "<map>@<landmark>" a map entered at a named info_landmark
	// `elysium.SkipIntro` does not act here: "story" always resolves to genesis, and the cvar governs
	// only where genesis's exit leads (ResolveIntroSkip).
	FString EntryPoint;

	// Forget the entry map's stored snapshot on arrival, so its opening chain fires again. A run
	// never asks for this — a fire-once trigger staying fired is the faithful behaviour and the
	// snapshot is right to replay it — but a debug entry that exists to watch an opening does.
	// NewGame owns arming and unwinding it, so a caller only states the intent.
	bool bReplayEntryMap = false;
};

// The retail New Game chain, in order (`docs/architecture/runtime-architecture.md` §10). Held as data because the
// transitions between these maps are the maps' own `trigger_changelevel` wires, not code.
namespace ElysiumStory
{
	// sp_genesisdevice_1 (chargen) -> sp_theatre (embrace + trial) -> sp_tutorial_1 -> sm_pawnshop_1
	inline const TCHAR* const ChargenMap = TEXT("sp_genesisdevice_1");
	inline const TCHAR* const TheatreMap = TEXT("sp_theatre");
	inline const TCHAR* const TutorialMap = TEXT("sp_tutorial_1");
	inline const TCHAR* const TutorialLandmark = TEXT("tutorial");
	inline const TCHAR* const SantaMonicaMap = TEXT("sm_pawnshop_1");

	// A fixed New Game entry: one verb (optionally a compact alias) that seeds a run and enters it at
	// a named point. `elysium.newgame` is the parameterised door onto the same flow; these are the
	// preset ones, and adding another is a row here rather than a second registration/failure path.
	// Every row builds an ordinary FElysiumNewGameRequest and goes through NewGame, so a preset can
	// never acquire session handling of its own.
	struct FElysiumNewGameEntry
	{
		const TCHAR* Verb = nullptr;
		const TCHAR* Alias = nullptr;    // null when the entry has one spelling
		const TCHAR* Help = nullptr;
		const TCHAR* EntryPoint = nullptr;
		bool bReplayEntryMap = false;
	};

	// The theatre replay: `sp_theatre`'s opening is a single trigger_once the player spawns straight
	// onto at the `newgame` landmark, so once fired, re-entering correctly finds it spent. This is the
	// dev way back in. Character identity and sheet allocation come from MakeMockCharacterRequest,
	// so every developer entry exercises the same player while this row owns only its destination.
	inline constexpr FElysiumNewGameEntry NewGameEntries[] =
	{
		{
			TEXT("elysium.newgame_ttd"),
			TEXT("newgame_ttd"),
			TEXT("elysium.newgame_ttd — theatre debug: new run entered at sp_theatre's `newgame` "
				"landmark with the map's state forgotten, so its opening chain fires again"),
			TEXT("sp_theatre@newgame"),
			/*bReplayEntryMap*/ true,
		},
	};

	inline TArrayView<const FElysiumNewGameEntry> NewGameEntryTable()
	{
		return MakeArrayView(NewGameEntries, UE_ARRAY_COUNT(NewGameEntries));
	}

	// The one developer character preset reproduces the retail tutorial reference sheet: female
	// Malkavian, Completely Batshit, and the untouched authored Malkavian_CharGen baseline. Developer
	// entry points choose only where this character enters the story.
	FElysiumNewGameRequest MakeMockCharacterRequest(
		const FString& EntryPoint, bool bReplayEntryMap = false);

	// The row as a request. Kept out of the table itself because the clan name resolves through the
	// sheet's own encoding in MakeMockCharacterRequest rather than being re-typed here.
	FElysiumNewGameRequest MakeNewGameRequest(const FElysiumNewGameEntry& Entry);

	// The intro skip, as a decision over a requested destination. Returns true when it rewrote one.
	//
	// With the skip on, a transition into `sp_theatre` lands instead at
	// the `tutorial` landmark on `sp_tutorial_1` — where the theatre's own `tutorial_change` would
	// have delivered the player. The offset and yaw are dropped with it: the offset a
	// trigger_changelevel captures is measured from the SOURCE map's landmark (genesis's `newgame`),
	// and that anchor means nothing against the tutorial's `tutorial`, so the rewrite is a
	// direct-entry placement (bHasYaw false = face the landmark's own angles).
	//
	// A divergence, and marked as one in `docs/vtmb/level_transitions.md` — reversible by `elysium.SkipIntro 0`,
	// which takes the authored route. Pure so the rule is testable with no world and no cvar.
	bool ResolveIntroSkip(bool bSkip, FString& Map, FString& Landmark, FVector& Offset, bool& bHasYaw);

	// The theatre's hidden exit becomes touchable only after its closing cinematic has moved the
	// Unreal player body. That scene-space displacement is not a destination-map placement: carrying
	// it through the ordinary landmark formula puts the player away from and below the tutorial porch.
	// Match the story-entry contract by making only this authored leg a direct landmark entry. Pure so
	// the exception cannot spread into ordinary trigger_changelevel travel unnoticed.
	bool ResolveTheatreExitPlacement(const FString& SourceMap, const FString& Map,
		const FString& Landmark, FVector& Offset, bool& bHasYaw);
}

// Old state, new state. Non-dynamic: the listeners are C++ (the UI subsystem, the HUD, the input
// scope stack), and a dynamic delegate would force the enum into UObject reflection for nothing.
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnElysiumAppStateChanged, EElysiumAppState /*Old*/, EElysiumAppState /*New*/);

// The application state machine and the session's entry points
// (`docs/architecture/runtime-architecture.md` §10). GI-scoped: boot, the session and the app state all outlive any
// one world, and travel is exactly when they change.
//
// This is the only owner of `EElysiumAppState`, of the boot decision, and of the loading screen.
// `AElysiumGameMode` keeps the pawn/HUD/controller classes and the no-pawn-on-a-backdrop rule; its
// BeginPlay is one `NotifyWorldReady(this)`.
UCLASS()
class UElysiumGameFlowSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// State.
	EElysiumAppState AppState() const { return State; }
	bool IsInSession() const { return ElysiumAppState::IsInSession(State); }
	FOnElysiumAppStateChanged& OnAppStateChanged() { return AppStateChanged; }

	// Boot.
	// The boot decision, made once. Called from Initialize (game-instance init), which is before
	// any world exists — so it only *decides*; the first NotifyWorldReady executes the plan.
	void BootFromCommandLine();

	// The game mode's one call. Under hard travel this runs in every fresh world: if a map load is
	// pending it is this world's, so start its runtime build and remain Loading until MapReady;
	// otherwise this is the boot world and the boot plan runs.
	void NotifyWorldReady(AGameModeBase* Mode);

	// Session.
	// Clear the session and enter the story. Returns false when the entry map is not exported+baked.
	bool NewGame(const FElysiumNewGameRequest& Request);

	// `elysium.SkipIntro`. Static because the readers are the travel funnel and the chargen footer,
	// neither of which is holding this subsystem — the cvar lives here because the story chain does.
	static bool ShouldSkipIntro();
	static void SetSkipIntro(bool bSkip);

	// UElysiumSaveSubsystem owns the payload; these are the seam every caller (menu,
	// `trigger_autosave`, the quicksave binding, MCP) goes through.
	bool LoadGame(const FString& SlotName);
	bool SaveGame(const FString& SlotName, EElysiumSaveKind Kind);

	// Drop the run and return to the static menu in the empty front-end shell. The session record is
	// cleared, so the next New Game starts from nothing.
	bool QuitToMenu();

	// Re-travel the current map, releasing a pause hold and dropping the menu first (the pause
	// menu's Reload).
	bool ReloadMap();

	// Enter the green room's stage world (`elysium.gr`): the map subsystem builds an empty level with
	// a stage actor in it, and the app follows an ordinary load into it — Loading here, Playing when
	// the stage publishes ready. Legal from anywhere, including the front end, because a green room
	// entered from the menu is a session like any other. False with the reason in OutError.
	bool EnterGreenRoom(FString& OutError);

	// The same stage world with nothing armed over it: the empty level and the pawn, and no lab.
	// The movement gym's host — it brings its own geometry and runs under `-nullrhi`, where the lab
	// refuses.
	bool EnterStageWorld(FString& OutError);

	// Clear and seed the session half of NewGame without travelling. NewGame, bare dev-map boot
	// and direct dev-map navigation without a player all use this, so every mocked entry applies
	// the same full character sheet.
	bool SeedNewGameState(const FElysiumNewGameRequest& Request);

	// Pause.
	// Playing <-> Paused: the world is held (clock + engine) and the pause menu is raised. Only
	// legal from Playing — the front end is an empty shell and has no run to pause.
	void SetPaused(bool bPaused);
	void TogglePause();
	bool IsPaused() const { return State == EElysiumAppState::Paused; }

	// Game over.
	// The run is lost: hold the world and raise the Load/Quit screen. Called by the combat
	// character's death path and by the masquerade meter; `elysium.gameover` is the
	// scriptable echo. The MakePlayerUnkillable latch on `events_player` is the caller's gate — the
	// damage system owns it, not the flow.
	void TriggerGameOver(EElysiumGameOverReason Reason);
	EElysiumGameOverReason LastGameOverReason() const { return GameOverReason; }

private:
	// The one state writer. Refuses a transition the table forbids (with a warning), broadcasts on a
	// real change, and reconciles the screen with the new state.
	bool SetAppState(EElysiumAppState NewState);

	// Which screen belongs to a state — the one rule, so no call site has to remember to close a
	// menu. It is also what makes the raw dev verbs correct: `elysium.map` and `elysium.reload`
	// travel without going through this subsystem, and the Loading transition their OpenLevel
	// raises is what takes a stale menu off the dying world's viewport.
	void ApplyMenuForState(EElysiumAppState NewState);

	// Release a hold this subsystem put on (Paused / GameOver) before leaving the state. Deliberately
	// does nothing from a running state, so a hand `elysium.pause` survives a travel.
	void ReleasePauseHold();

	// Resolve FElysiumNewGameRequest::EntryPoint to a map + landmark. False when nothing resolves.
	bool ResolveEntryPoint(const FString& EntryPoint, FString& OutMap, FString& OutLandmark) const;

	// Loading screen.
	// The engine's own movie player, over the OpenLevel flush. The hook is
	// IGameMoviePlayer::OnPrepareLoadingScreen rather than FCoreUObjectDelegates::PreLoadMap,
	// because the movie player binds PreLoadMap itself at engine init — ahead of this subsystem —
	// and calls PlayMovie() from there, so a SetupLoadingScreen in our own PreLoadMap handler would
	// always be one load too late. OnPrepareLoadingScreen is broadcast from inside PlayMovie when no
	// attributes are prepared, which is the order-independent seam.
	void OnPrepareLoadingScreen();
	void OnPreLoadMap(const FString& MapName);
	void OnPostLoadMap(UWorld* LoadedWorld);
	void OnMapRuntimeReady(AElysiumMapActor* Map);
	void OnMapRuntimeFailed(AElysiumMapActor* Map, const FString& Reason);
	void ShowRuntimeLoadingOverlay(UWorld* World, const FText& Message, bool bForce = false);
	void HideRuntimeLoadingOverlay();

	// The named verbs.
	// `cancelselect`, `togglemainmenu`, `pause`, `save`, `load`. Registered here, not on the player
	// controller, because Escape has to resolve while a screen holds input UI-only and no controller
	// is seeing keys — this subsystem outlives every world and every screen.
	void RegisterCommands();
	void UnregisterCommands();

	// The body both stage entries share: travel, then the Loading -> ready -> Playing path a map
	// load takes. `bWithGreenRoom` decides only whether the lab is armed over it.
	bool EnterStage(FString& OutError, bool bWithGreenRoom);

	EElysiumAppState State = EElysiumAppState::Boot;
	FOnElysiumAppStateChanged AppStateChanged;
	EElysiumGameOverReason GameOverReason = EElysiumGameOverReason::Killed;

	// The boot decision, made once in Initialize and consumed by the first NotifyWorldReady.
	enum class EBootKind : uint8
	{
		Menu,      // raise the static main menu in the empty boot world
		NewGame,   // straight into the story entry, seeded
		DevMap,    // -ElysiumMap=<name>: a bare load with the mock character seeded
		Stage,     // the stage world, no VtMB map loaded: -ElysiumGreenRoom, or -MoveGym
	};
	EBootKind BootKind = EBootKind::Menu;
	FString   BootMap;             // DevMap only
	// Whether a Stage boot also arms the green room's lab. The movement gym wants the empty level
	// and nothing else, and it runs under `-nullrhi` where the lab refuses outright.
	bool      bStageWantsGreenRoom = false;
	bool      bBootExecuted = false;

	FDelegateHandle PrepareLoadingScreenHandle;
	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle MapReadyHandle;
	FDelegateHandle MapFailedHandle;
	UPROPERTY(Transient)
	TObjectPtr<UElysiumLoadingScreen> RuntimeLoadingScreen;
	FString RuntimeLoadingFailure;

	TArray<IConsoleObject*> ConsoleObjects;
	TArray<FElysiumCommandBinding> Bindings;
};
