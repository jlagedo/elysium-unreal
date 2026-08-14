#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ElysiumGameClock.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumScriptHost.h"
#include "ElysiumTimeControl.h"
#include "ElysiumVariant.h"
#include "ElysiumWorldServices.h"
#include "ElysiumGameStateSubsystem.generated.h"

// `G` and the quest map mirror Python dicts, which are case-sensitive — Unreal's default
// FString/FName map keys are not. These key funcs restore case sensitivity so `G.Story_State`
// and a hypothetical `G.story_state` stay distinct, exactly as the retail engine keeps them.
template <typename ValueType>
struct TElysiumCaseSensitiveStringKeyFuncs
	: BaseKeyFuncs<TPair<FString, ValueType>, FString, /*bInAllowDuplicateKeys*/ false>
{
	static FORCEINLINE const FString& GetSetKey(const TPair<FString, ValueType>& Element) { return Element.Key; }
	static FORCEINLINE bool Matches(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); }
	static FORCEINLINE uint32 GetKeyHash(const FString& Key) { return FCrc::StrCrc32(*Key); }
};

using FElysiumGlobalMap = TMap<FString, FElysiumVariant, FDefaultSetAllocator, TElysiumCaseSensitiveStringKeyFuncs<FElysiumVariant>>;
using FElysiumQuestMap = TMap<FString, int32, FDefaultSetAllocator, TElysiumCaseSensitiveStringKeyFuncs<int32>>;

class FElysiumEntityWorld;

namespace ElysiumQuestNotifications
{
	// Presentation classification of a quest catalogue state's recovered type string.
	EElysiumNotificationKind KindForStateType(const FString& Type);
}

// One recorded expression evaluation (field-6 payload or a hand-run elysium.eval/exec) for the
// debug layer's recent-eval log (P5 5.2). Debug-only history; not saved.
struct FElysiumEvalRecord
{
	double Time = 0.0;      // game-clock seconds at record time
	FString Source;        // the payload string
	FString Result;        // FElysiumVariant::Describe() of the value, or the error text
	bool bError = false;   // true when the eval failed (error-to-false)
};

// One recorded native-binding call (a `vampire`-module global or a Character method) for the
// debug layer's native-call log (P5 5.3). The evaluator pushes one whenever a native name is
// invoked, so `FindPlayer().RemoveItem(...)` and friends are visible even while their backing
// systems are still stubs. Debug-only history; not saved.
struct FElysiumNativeCallRecord
{
	double Time = 0.0;      // game-clock seconds at record time
	FString Call;          // reconstructed call, e.g. "FindPlayer().RemoveItem(Int(5))"
	FString Result;        // FElysiumVariant::Describe() of the return value
	bool bStub = false;    // true when the call only logged (no backing system yet)
};

// Persistent-across-travel game state (R8). Owns the three things that outlive any single
// map load: the `G` global flag store, the quest string->int map, and the game clock. The
// entity world and its event queue die with AElysiumMapActor; this lives on the game
// instance, so travel keeps `G`, quests, and curtime intact.
//
// It holds no UObject-reflected state — the stores are plain structs carrying only
// FElysiumVariant/int (no object refs), so there is nothing for the GC to trace and
// save/load (M6) is mechanical.
UCLASS()
class UElysiumGameStateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- `G` — the global flag bag -------------------------------------------------
	// Engine-owned in VtMB (a `PyDataManager` injected into __main__), one flat namespace,
	// values overwhelmingly small ints. Default-on-miss is integer 0 (decompiled tp_getattr,
	// `docs/vtmb/python_bridge.md`): Get never fails. Setting a Void value deletes the key, mirroring
	// tp_setattr where assigning None removes it.

	FElysiumVariant GetGlobal(const FString& Key) const;
	void SetGlobal(const FString& Key, const FElysiumVariant& Value);
	bool HasGlobal(const FString& Key) const;
	void ClearGlobal(const FString& Key);
	void ClearAllGlobals(); // G.ClearAll
	TArray<FString> GlobalKeys() const;

	// The common shape — read/write a flag as an int (miss -> 0).
	int32 GetGlobalInt(const FString& Key) const { return GetGlobal(Key).ToInt(); }
	void SetGlobalInt(const FString& Key, int32 Value) { SetGlobal(Key, FElysiumVariant::Int(Value)); }

	const FElysiumGlobalMap& GetGlobals() const { return Globals; }

	// --- Quests --------------------------------------------------------------------
	// String->int quest state; miss -> 0. The map stays authoritative (732 `SetQuest` call sites,
	// default-0 on miss is VtMB's own contract); what hangs off it is everything that happens
	// AROUND a change — the completion state's awards and the journal row (`docs/vtmb/game_runtime.md` ->
	// "Quests"). The state value is the completion state's 1-based ORDINAL in file order, which is
	// what `SetQuest`'s second argument is.
	int32 GetQuestState(const FString& Quest) const;
	bool HasQuest(const FString& Quest) const;
	const FElysiumQuestMap& GetQuests() const { return Quests; }

	// The script/dialogue funnel. Writes the state, then resolves the catalogue and pays out:
	// `AwardMoney`, then `AwardXP`, then `Event` — the engine's own order.
	void SetQuestState(const FString& Quest, int32 State);

	// The wholesale replace a save load performs. Awards NOTHING and leaves the journal alone (it
	// arrives with the player record): a load must not replay a run's worth of awards.
	void RestoreQuests(TArray<TPair<FString, int32>>&& In);

	// The journal — ASSIGNED_QUEST rows on the player record, in assignment order.
	const TArray<FElysiumAssignedQuest>& Journal() const { return Record.Journal; }

	// The quest log's selected hub tab (`m_iCurrQuestLogArea`), INDEX_NONE until the screen is first
	// opened. Player state in VtMB, so it lives on the record and persists with the character.
	int32 QuestLogArea() const { return Record.QuestLogArea; }
	void SetQuestLogArea(int32 Hub) { Record.QuestLogArea = Hub; }

	// Clear the unread marker on every row belonging to `Hub` (and to the cross-hub `main` table,
	// which the screen shows in every tab). **Ours, not VtMB's** — the engine sets the byte on every
	// write and never reads it, so the panel's own clear rule is unrecovered (`docs/architecture/ui-architecture.md`).
	void MarkQuestsRead(int32 Hub);

	// --- The player record + New Game (11.4) ---------------------------------------
	// S3: the player *is* an entity, so the live sheet lives on that entity for as long as a map
	// does; this record is the durable half that crosses a map boundary and, at 11.9, a save. The
	// entity hydrates from it at map build and dehydrates back into it when the world is torn down.
	const FElysiumPlayerRecord& PlayerRecord() const { return Record; }
	FElysiumPlayerRecord& PlayerRecord() { return Record; }

	// The name typed at chargen (9.4f). Empty until then, and every reader must render that state
	// rather than substituting a placeholder.
	const FString& PlayerName() const { return Record.Name; }
	void SetPlayerName(const FString& In) { Record.Name = In; }

	// The sheet every reader should go through: **the live player entity's when a map is up, the
	// record's otherwise** (the front end, a New Game seeding before any map exists, a headless
	// probe eval). One rule, so a write can never land on the copy that is about to be overwritten.
	const FElysiumSheet& PlayerSheet() const;
	FElysiumSheet& PlayerSheet();

	// VtMB's rulebook — the data half of the sheet. The substrate already holds this subsystem, so
	// this is the one path from an entity to `stats.txt` (`World->GetGameState()->Rulebook()`).
	// Null in a bare test world; every caller handles that by leaving the sheet unclamped.
	class UElysiumRulebookSubsystem* Rulebook() const;
	// `stats.txt`'s four containers, or null when the rulebook is absent or failed to load.
	const struct FElysiumStatTable* Stats() const;

	// The live player entity in the current map, or null (no map, a menu backdrop, or the world
	// built without one). The one place that resolution happens.
	class FElysiumPlayer* PlayerEntity() const;

	// The run is lost: the combat character's death path calls this and the session raises 11.3's
	// GameOver state. It lives here rather than on a world service because the substrate already
	// holds this subsystem, and "the run ended" is session state, not a map capability.
	void NotifyPlayerKilled();

	// The other lost run: the masquerade counter reached its ceiling (5). Same shape and the same
	// owner as death, with its own game-over reason.
	void NotifyMasqueradeBreach();

	// Seed the state a fresh story run starts from. New Game in retail is four maps
	// (`docs/vtmb/level_transitions.md`): chargen on `sp_genesisdevice_1` writes the sheet, then the theatre
	// embrace + trial, then a landmark transition into `sp_tutorial_1`. Chargen (8.6) and the
	// choreographed intro (P9) are unbuilt, so this seeds exactly what survives that chain and is
	// read afterwards, and the caller travels straight to the story entry.
	//
	// Seeded flags, and why each one:
	//   Story_State = -4   the intro spine's "theatre done" value (chargen -5, leaving tutorial -2)
	//   Tut_Jack    =  0   the tutorial beat counter DialogPostProcess dispatches on
	//   Tut_Patch   =  0   arms the Plus profile's beat-1 relocation after Jack's first exchange
	//   Linux_Wine  =  1   set by BOTH vamputil.setBasic() and setPlus(), i.e. by the patch-type
	//                      selection; `logic_pythoncheck linux_check` fires OnFalse -> popup_linux
	//                      without it, so seeding it stands in for having configured the patch
	// The Patch_Plus family (PP / Patch_Plus / Jack_Extra / Flynn_Extra / Extra_Lines) remains at
	// G's default 0 only during pre-map bootstrap. The first map's `unhidePlus()` invokes Elysium's
	// pinned Plus selector, and the patch's own `setPlus()` remains the sole writer of those values.
	void BeginNewGame(int32 Clan, bool bMale);

	// Chargen's ACCEPT: land a finished character on the record and on the live player entity — the
	// name, the whole spent sheet (clan and sex are slots on it), the History and the trait-effect
	// group it names. The only door chargen writes through, so what a New Game seeds and what the
	// wizard produces cannot drift (`Substrate/ElysiumChargen.h`).
	void CommitChargen(const struct FElysiumChargenState& State);

	// Drop the run: `G`, the quest map, the player record and the clock all go back to their
	// fresh-process values. Called by UElysiumGameFlowSubsystem::QuitToMenu (11.3) so the menu's
	// backdrop world cannot be running behind a half-live session, and so the next New Game starts
	// from nothing. `G` and the quest map join the record as save blocks at 11.9.
	void EndSession();

	// --- The per-map snapshots (11.9) ------------------------------------------------
	// A run holds the current map plus a frozen snapshot of every other map visited, so walking back
	// into Santa Monica finds it as you left it (`docs/architecture/save-architecture.md` §5). They live here for the
	// same reason `G` does: session lifetime, not map lifetime. The entity world writes one at every
	// teardown and reads one back at every build, which is why travel and save cannot drift apart.
	const FElysiumMapSnapshot* FindMapSnapshot(const FString& Map) const;
	void StoreMapSnapshot(FElysiumMapSnapshot&& Snapshot);
	const TMap<FString, FElysiumMapSnapshot>& MapSnapshots() const { return Snapshots; }
	void SetMapSnapshots(TMap<FString, FElysiumMapSnapshot>&& In);

	// Forget one map, so the next build treats it as never visited and runs its opening from
	// scratch. A run never does this — a fire-once trigger staying fired is the faithful
	// behaviour. It exists for the dev entries that replay a map's opening (`elysium.newgame_ttd`).
	void ClearMapSnapshot(const FString& Map);

	// First-visit order, for the World block. A map is "visited" the first time it is frozen.
	const TArray<FString>& VisitedMaps() const { return Visited; }
	void SetVisitedMaps(TArray<FString>&& In) { Visited = MoveTemp(In); }

	// --- Clock + time control (S1) -------------------------------------------------
	// The clock is read-only to everyone but the facade beside it (FElysiumGameClock friends
	// FElysiumTimeControl and nothing else), so `Now` moves in exactly one place: the map
	// actor's gameplay tick, through TimeControl().AdvanceFrame.
	const FElysiumGameClock& GameClock() const { return Clock; }

	// The one pause / time-scale facade, over the clock and engine time together
	// (runtime-architecture.md §4). `elysium.pause` / `elysium.timescale` / `elysium.step`.
	FElysiumTimeControl& TimeControl() { return TimeCtl; }
	const FElysiumTimeControl& TimeControl() const { return TimeCtl; }

	// --- Script host (B6) ----------------------------------------------------------
	// The seam field-6 Python payloads evaluate through. Initialized to FElysiumNullScriptHost
	// (logs + returns Void); the M4 evaluator installs itself via SetScriptHost. Lives here (GI
	// scope) so it persists across map travel, like the entity world's other collaborators.
	IElysiumScriptHost& ScriptHost() const { return *ScriptHostPtr; }
	void SetScriptHost(TUniquePtr<IElysiumScriptHost> InHost);

	// --- Level script (P9 9.3) -----------------------------------------------------------
	// Import the map's `worldspawn.levelscript` module into the installed host. AElysiumMapActor
	// calls this once per map load, before the spawn pass, so the module's top-level code (its
	// constants, imports and class defs) is in place before any entity evaluates a field-6 payload
	// against it. The module name is remembered, so installing a different host re-imports it there
	// — swapping hosts mid-session never leaves the new one with an empty namespace.
	// Reports false with a reason when the host has no interpreter or the import raised; that is a
	// logged condition, not a failure of map load (payloads still evaluate, minus level names).
	bool LoadLevelScript(const FString& Module);
	const FString& CurrentLevelScriptModule() const { return LevelScriptModule; }

	// Outcome of the last LoadLevelScript, for the debug layer. Empty error = loaded.
	bool IsLevelScriptLoaded() const { return bLevelScriptLoaded; }
	const FString& LevelScriptError() const { return LevelScriptLoadError; }

	// --- Live script evaluation (P5 5.4) --------------------------------------------------
	// Arm/disarm real evaluation on the whole scripting surface (field-6, logic_pythoncheck,
	// ScheduleTask): on -> the preferred real host, off -> the null host (logs + Void). Live is the
	// map-load default. Driven by the `elysium.script.live` verb and the Scripting Cog window.
	void SetLiveScriptEval(bool bEnable);
	bool IsLiveScriptEval() const;

	// The real host installed when evaluation is live: the embedded CPython VM when the module was
	// built with it and the interpreter comes up, else the ElysiumExpr evaluator. `elysium.script.
	// cpython [0|1]` overrides the choice for A/B; a CPython VM that fails to start falls back
	// rather than leaving every eval Void, which would be indistinguishable from error-to-false.
	TUniquePtr<IElysiumScriptHost> MakePreferredScriptHost();

	// --- Expression-eval convenience + debug history (P5 5.2) ----------------------------
	// Evaluate a script string against the CURRENT map's entity world + this G store, for the
	// console verbs and the Cog window. Runs through the installed host, so it resolves exactly what
	// a field-6 payload resolves — including the loaded level script's names. Expression and
	// statement shapes both work; the value is the expression's, or the last statement's. Records
	// into the recent-eval log. Total (never throws): on error OutError carries the reason and the
	// result is Void (error-to-false, RE3).
	FElysiumVariant EvalScript(const FString& Source, FString& OutError);

	// Push one evaluation onto the recent-eval ring (called by the expr host + EvalScript).
	void RecordEval(const FString& Source, const FElysiumVariant& Result, bool bError, const FString& Error);
	const TArray<FElysiumEvalRecord>& RecentEvals() const { return EvalHistory; }
	void ClearEvalHistory() { EvalHistory.Reset(); }

	// --- Native-binding call history (P5 5.3) --------------------------------------------
	// The evaluator records every native global / Character-method dispatch here (and bumps the
	// per-name call counter) so the debug layer can show which parts of the `vampire` surface the
	// running content exercises. Debug-only; not saved.
	void RecordNativeCall(const FString& Call, const FElysiumVariant& Result, bool bStub, FName Name);
	const TArray<FElysiumNativeCallRecord>& RecentNativeCalls() const { return NativeCallHistory; }
	void ClearNativeCallHistory() { NativeCallHistory.Reset(); NativeCallCounts.Reset(); }
	int32 NativeCallCount(FName Name) const { const int32* C = NativeCallCounts.Find(Name); return C ? *C : 0; }

	// The current map's entity world (via the map subsystem), or null between maps.
	FElysiumEntityWorld* CurrentEntityWorld() const;

private:
	// `elysium.quest` — the journal, one quest, or a real state change.
	void ExecQuest(const TArray<FString>& Args);
	// The map-epoch boundary (S4): drop the leaving map's level-script state and forward the retire
	// to the installed host, which is plain C++ and cannot subscribe on its own.
	void OnMapEpochRetired(uint64 Epoch);

	FElysiumGlobalMap Globals;
	FElysiumQuestMap Quests;
	// The durable player (11.4). The live one is the entity in the current map.
	FElysiumPlayerRecord Record;
	// The frozen maps of this run (11.9), keyed by map name, plus first-visit order.
	TMap<FString, FElysiumMapSnapshot> Snapshots;
	TArray<FString> Visited;
	FElysiumGameClock Clock;
	// Declared after the clock it holds a reference to.
	FElysiumTimeControl TimeCtl{ Clock };
	TUniquePtr<IElysiumScriptHost> ScriptHostPtr;

	// The current map's level-script module + the last import's outcome (P9 9.3).
	FString LevelScriptModule;
	FString LevelScriptLoadError;
	bool bLevelScriptLoaded = false;

	// Recent-eval ring (debug-only), newest last; capped at EvalHistoryMax.
	static constexpr int32 EvalHistoryMax = 64;
	TArray<FElysiumEvalRecord> EvalHistory;

	// Recent native-call ring + per-name call counters (debug-only), newest last (P5 5.3).
	static constexpr int32 NativeCallHistoryMax = 64;
	TArray<FElysiumNativeCallRecord> NativeCallHistory;
	TMap<FName, int32> NativeCallCounts;

	TArray<IConsoleObject*> ConsoleObjects;
	FDelegateHandle MapEpochRetiredHandle;
};
