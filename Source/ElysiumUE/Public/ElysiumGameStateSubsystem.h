#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ElysiumGameClock.h"
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

private:
	FElysiumGlobalMap Globals;
	FElysiumQuestMap Quests;
	FElysiumGameClock Clock;

	TArray<IConsoleObject*> ConsoleObjects;
};
