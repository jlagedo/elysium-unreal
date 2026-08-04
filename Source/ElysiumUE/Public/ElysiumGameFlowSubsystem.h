#pragma once

#include "CoreMinimal.h"
#include "ElysiumAppState.h"
#include "ElysiumCommands.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ElysiumGameFlowSubsystem.generated.h"

class AGameModeBase;
class UWorld;
class UGameViewportClient;
class SWidget;
class AElysiumMapActor;

// Which slot ring a save belongs to. `trigger_autosave` fires Auto; the pause menu fires Manual;
// the quicksave binding fires Quick. The slots themselves are 11.9's.
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

	// m_iVHistoryID. -1 = unset; chargen writes it (9.4).
	int32 HistoryId = -1;

	// Chargen's attribute/ability/discipline allocation, applied onto the sheet (9.4).
	TMap<FName, int32> Spends;

	// Where the chain is entered:
	//   ""/"story"   the retail chain from `sp_genesisdevice_1` (chargen), at its info_player_start
	//   "tutorial"   straight to `sp_tutorial_1` at its `tutorial` landmark — the dev shortcut
	//   "<map>"           a bare map load
	//   "<map>@<landmark>" a map entered at a named info_landmark
	// `elysium.SkipIntro` does not act here: "story" always resolves to genesis, and the cvar governs
	// only where genesis's exit leads (ResolveIntroSkip).
	FString EntryPoint;
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

	// Both spellings invoke the same fresh-state theatre replay. The compact alias is the command
	// used by the opening-scene QA loop; keeping the pair as data makes registration testable without
	// constructing a game instance in the content-free automation tier.
	inline const TCHAR* const TheatreReplayCommands[] =
	{
		TEXT("elysium.newgame_ttd"),
		TEXT("newgame_ttd"),
	};

	// The intro skip, as a decision over a requested destination. Returns true when it rewrote one.
	//
	// The theatre act is P12's, so with the skip on, a transition into `sp_theatre` lands instead at
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
// scope stack at 11.5), and a dynamic delegate would force the enum into UObject reflection for
// nothing.
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnElysiumAppStateChanged, EElysiumAppState /*Old*/, EElysiumAppState /*New*/);

// The application state machine and the session's entry points (roadmap 11.3,
// `docs/architecture/runtime-architecture.md` §10). GI-scoped: boot, the session and the app state all outlive any
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

	// --- State ---------------------------------------------------------------------------------
	EElysiumAppState AppState() const { return State; }
	bool IsInSession() const { return ElysiumAppState::IsInSession(State); }
	FOnElysiumAppStateChanged& OnAppStateChanged() { return AppStateChanged; }

	// --- Boot ----------------------------------------------------------------------------------
	// The whole of the old AElysiumGameMode::BeginPlay decision tree, made once. Called from
	// Initialize (game-instance init), which is before any world exists — so it only *decides*;
	// the first NotifyWorldReady executes the plan.
	void BootFromCommandLine();

	// The game mode's one call. Under hard travel this runs in every fresh world: if a map load is
	// pending it is this world's, so start its runtime build and remain Loading until MapReady;
	// otherwise this is the boot world and the boot plan runs.
	void NotifyWorldReady(AGameModeBase* Mode);

	// --- Session -------------------------------------------------------------------------------
	// Clear the session and enter the story. Returns false when the entry map is not exported+baked.
	bool NewGame(const FElysiumNewGameRequest& Request);

	// `elysium.SkipIntro`. Static because the readers are the travel funnel and the chargen footer,
	// neither of which is holding this subsystem — the cvar lives here because the story chain does.
	static bool ShouldSkipIntro();
	static void SetSkipIntro(bool bSkip);

	// 11.9 owns the payload; these are the seam every caller (menu, `trigger_autosave`, the quicksave
	// binding, MCP) goes through, so nothing has to be re-plumbed when it lands. They log and report
	// false today.
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

	// --- Pause ---------------------------------------------------------------------------------
	// Playing <-> Paused: the world is held (clock + engine) and the pause menu is raised. Only
	// legal from Playing — the front end is an empty shell and has no run to pause.
	void SetPaused(bool bPaused);
	void TogglePause();
	bool IsPaused() const { return State == EElysiumAppState::Paused; }

	// --- Game over -----------------------------------------------------------------------------
	// The run is lost: hold the world and raise the Load/Quit screen. Called by the combat
	// character's death path (11.4 / 9.4) and by the masquerade meter; `elysium.gameover` is the
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
	// does nothing from a running state, so a hand `elysium.pause` survives a travel exactly as S1
	// says it does.
	void ReleasePauseHold();

	// Resolve FElysiumNewGameRequest::EntryPoint to a map + landmark. False when nothing resolves.
	bool ResolveEntryPoint(const FString& EntryPoint, FString& OutMap, FString& OutLandmark) const;

	// --- Loading screen ------------------------------------------------------------------------
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

	// --- The named verbs (11.6) ----------------------------------------------------------------
	// `cancelselect`, `togglemainmenu`, `pause`, `save`, `load`. Registered here, not on the player
	// controller, because Escape has to resolve while a screen holds input UI-only and no controller
	// is seeing keys — this subsystem outlives every world and every screen.
	void RegisterCommands();
	void UnregisterCommands();

	EElysiumAppState State = EElysiumAppState::Boot;
	FOnElysiumAppStateChanged AppStateChanged;
	EElysiumGameOverReason GameOverReason = EElysiumGameOverReason::Killed;

	// The boot decision, made once in Initialize and consumed by the first NotifyWorldReady.
	enum class EBootKind : uint8
	{
		Menu,      // raise the static main menu in the empty boot world
		NewGame,   // straight into the story entry, seeded
		DevMap,    // -ElysiumMap=<name>: a bare load with the mock character seeded
		GreenRoom, // -ElysiumGreenRoom with no map named: the stage world, no VtMB map loaded
	};
	EBootKind BootKind = EBootKind::Menu;
	FString   BootMap;             // DevMap only
	bool      bBootExecuted = false;

	FDelegateHandle PrepareLoadingScreenHandle;
	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle MapReadyHandle;
	FDelegateHandle MapFailedHandle;
	TWeakObjectPtr<UGameViewportClient> RuntimeLoadingViewport;
	TSharedPtr<SWidget> RuntimeLoadingWidget;
	FString RuntimeLoadingFailure;

	TArray<IConsoleObject*> ConsoleObjects;
	TArray<FElysiumCommandBinding> Bindings;
};
