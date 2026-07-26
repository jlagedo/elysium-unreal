#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ElysiumGameClock.h"
#include "ElysiumScriptHost.h"
#include "ElysiumTimeControl.h"
#include "ElysiumVariant.h"
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

// The player's character sheet, as far as the opening needs it. VtMB keeps the numeric sheet as
// datamap fields on the player entity (`game_runtime.md` §2: numbers on the entity, story in `G`,
// progress in quests) and `ccmd.createplayer` writes it during chargen on `sp_genesisdevice_1`.
// There is no player entity and no chargen yet (P8/P9), so this stands in for both: the minimum
// the tutorial's own scripts read off `pc`.
//
// `Clan` uses the LEVEL-SCRIPT indexing 2..8 (Brujah 2 … Ventrue 8) that `pc.clan` carries — not
// the 1..7 `ClanNameFunc` display enum, and not `clandoc`'s ordering. `game_runtime.md` §3 records
// that two indexings exist; the scripts (`IsClan`, `unhidePlus`'s 9/10/11 patch-type sentinels)
// speak this one.
//
// `Stats` is the open-ended remainder (`base_<discipline>`, attributes, abilities). 9.4 loads
// `vdata/system/*.txt` into it; until then it stays empty and readers fall back to their defaults.
struct FElysiumPlayerSheet
{
	int32 Clan = 2;                  // pc.clan (2..8)
	bool bMale = true;               // pc.IsMale()
	TMap<FName, int32> Stats;        // base_<name> -> rating (9.4)

	// Clan display names indexed by the 2..8 encoding; index 0/1 unused.
	static const TCHAR* ClanName(int32 Clan);
	// Case-insensitive name -> 2..8, or 0 when unrecognised (drives `elysium.newgame brujah`).
	static int32 ClanFromName(const FString& Name);
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
	// `python_bridge.md`): Get never fails. Setting a Void value deletes the key, mirroring
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
	// String->int quest state; miss -> 0. (XP/RPG sheet load onto this subsystem later — 9.4.)
	int32 GetQuestState(const FString& Quest) const;
	void SetQuestState(const FString& Quest, int32 State);
	bool HasQuest(const FString& Quest) const;
	const FElysiumQuestMap& GetQuests() const { return Quests; }

	// --- Player sheet + New Game ---------------------------------------------------
	const FElysiumPlayerSheet& PlayerSheet() const { return Sheet; }
	FElysiumPlayerSheet& PlayerSheet() { return Sheet; }

	// Seed the state a fresh story run starts from. New Game in retail is four maps
	// (`level_transitions.md`): chargen on `sp_genesisdevice_1` writes the sheet, then the theatre
	// embrace + trial, then a landmark transition into `sp_tutorial_1`. Chargen (8.6) and the
	// choreographed intro (P9) are unbuilt, so this seeds exactly what survives that chain and is
	// read afterwards, and the caller travels straight to the story entry.
	//
	// Seeded flags, and why each one:
	//   Story_State = -4   the intro spine's "theatre done" value (chargen -5, leaving tutorial -2)
	//   Tut_Jack    =  0   the tutorial beat counter DialogPostProcess dispatches on
	//   Tut_Patch   =  0   arms the patch's beat-1 relocation (teleport_fade -> the retail start)
	//   Linux_Wine  =  1   set by BOTH vamputil.setBasic() and setPlus(), i.e. by the patch-type
	//                      selection; `logic_pythoncheck linux_check` fires OnFalse -> popup_linux
	//                      without it, so seeding it stands in for having configured the patch
	// The Patch_Plus family (PP / Patch_Plus / Jack_Extra / Flynn_Extra / Extra_Lines) is left at
	// G's default 0 = the patch's "Basic" profile, the closest to retail. `G` is default-0 on miss
	// (RE3), so the zeros need no explicit seeding; only the non-zero flags are written.
	void BeginNewGame(int32 Clan, bool bMale);

	// Drop the run: `G`, the quest map, the sheet and the clock all go back to their fresh-process
	// values. Called by UElysiumGameFlowSubsystem::QuitToMenu (11.3) so the menu's backdrop world
	// cannot be running behind a half-live session, and so the next New Game starts from nothing.
	// This IS the session record until 11.4/11.9 give it a name — everything a run owns today lives
	// on this subsystem.
	void EndSession();

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
	FElysiumGlobalMap Globals;
	FElysiumQuestMap Quests;
	FElysiumPlayerSheet Sheet;
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
};
