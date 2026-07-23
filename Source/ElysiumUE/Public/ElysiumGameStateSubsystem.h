#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ElysiumGameClock.h"
#include "ElysiumScriptHost.h"
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

	// --- Clock ---------------------------------------------------------------------
	FElysiumGameClock& GameClock() { return Clock; }
	const FElysiumGameClock& GameClock() const { return Clock; }

	// --- Script host (B6) ----------------------------------------------------------
	// The seam field-6 Python payloads evaluate through. Initialized to FElysiumNullScriptHost
	// (logs + returns Void); the M4 evaluator installs itself via SetScriptHost. Lives here (GI
	// scope) so it persists across map travel, like the entity world's other collaborators.
	IElysiumScriptHost& ScriptHost() const { return *ScriptHostPtr; }
	void SetScriptHost(TUniquePtr<IElysiumScriptHost> InHost);

	// --- Live field-6 evaluation (P5 5.2, opt-in) ----------------------------------------
	// Arm/disarm the real expression evaluator on the field-6 path: on -> swap in the expr host,
	// off -> restore the null host. Default off (the null host stands in at boot); 5.4 makes the
	// expr host the unconditional default and adds logic_pythoncheck + ScheduleTask. Driven by the
	// `elysium.script.live` verb and the Scripting Cog window checkbox.
	void SetLiveScriptEval(bool bEnable);
	bool IsLiveScriptEval() const;

	// --- Expression-eval convenience + debug history (P5 5.2) ----------------------------
	// Evaluate/execute a script string against the CURRENT map's entity world + this G store, for
	// the console verbs and the Cog window. Eval = a single expression (returns its value); Exec =
	// statement(s) (assignment / bare call). Both record into the recent-eval log. Total (never
	// throws): on error OutError carries the reason and the result is Void.
	FElysiumVariant EvalScript(const FString& Source, bool bExec, FString& OutError);

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
	FElysiumGameClock Clock;
	TUniquePtr<IElysiumScriptHost> ScriptHostPtr;

	// Recent-eval ring (debug-only), newest last; capped at EvalHistoryMax.
	static constexpr int32 EvalHistoryMax = 64;
	TArray<FElysiumEvalRecord> EvalHistory;

	// Recent native-call ring + per-name call counters (debug-only), newest last (P5 5.3).
	static constexpr int32 NativeCallHistoryMax = 64;
	TArray<FElysiumNativeCallRecord> NativeCallHistory;
	TMap<FName, int32> NativeCallCounts;

	TArray<IConsoleObject*> ConsoleObjects;
};
