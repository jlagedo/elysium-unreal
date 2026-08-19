#pragma once

#include "CoreMinimal.h"

// ================================================================================================
// 7. the five quests_*.txt — the quest catalogue
// ================================================================================================

struct FElysiumQuestState
{
	// The authored `"ID"`. DECORATIVE: `QuestJournal::AddCompletionState` never reads the key —
	// `SetQuest(title, N)` addresses the N-th state in FILE ORDER (`docs/vtmb/game_runtime.md` → "Quests").
	// Kept because every shipped row authors it equal to its own position, which is worth asserting.
	int32 Id = 0;
	FString Description;        // the journal body
	FString Type;               // success | failure | incomplete — drives the entry's colour
	FString AwardXp;            // an experience_table.txt KEY, not a number
	int32 AwardMoney = 0;       // authored in zero shipped rows; the schema is real
	FString Event;              // script data handed to the interpreter; zero shipped rows

	bool IsValid() const { return Id != 0 || !Description.IsEmpty(); }
};

struct FElysiumQuest
{
	FString Title;              // the key dialogue and scripts use: pc.SetQuest("Arthur Knox", 2)
	FString DisplayName;        // the journal heading
	int32 TableIndex = INDEX_NONE;  // which quests_* file — the save stores this
	int32 Index = INDEX_NONE;       // position within that file
	TArray<FElysiumQuestState> States;

	// State 0 is "unassigned" and is never authored, so a miss is the ordinary case.
	const FElysiumQuestState* StateById(int32 Id) const;

	// What `SetQuest(title, N)` actually addresses: the N-th state in file order, 1-based. This is
	// the engine's own lookup; `StateById` is the readable one the verb prints. VtMB caps a quest at
	// 20 states, so anything past that could not have been loaded either.
	static constexpr int32 MaxStates = 20;
	const FElysiumQuestState* StateByOrdinal(int32 OneBased) const;

	bool IsValid() const { return !Title.IsEmpty(); }
};

// A quest's address as the save holds it: which table, which quest within it.
struct FElysiumQuestRef
{
	int32 Table = INDEX_NONE;
	int32 Quest = INDEX_NONE;
	bool IsValid() const { return Table != INDEX_NONE && Quest != INDEX_NONE; }
};

struct FElysiumQuestTables
{
	// Fixed order, because the save stores the table index. Alphabetical by hub:
	// chinatown, downtown, hollywood, main, santamonica.
	static const TCHAR* HubNames[5];
	static constexpr int32 NumTables = 5;

	// `quests_main.txt` — the cross-hub table. It is not a place, so it has no tab of its own in the
	// quest log; its rows show under whichever hub is selected.
	static constexpr int32 MainTable = 3;

	// The four tables that ARE hubs, in tab order as the quest log lists them: Santa Monica,
	// Downtown, Hollywood, Chinatown. Alphabetical `HubNames` order is not tab order.
	static constexpr int32 HubTabOrder[4] = { 4, 1, 2, 0 };

	TArray<FElysiumQuest> Quests[NumTables];

	bool Load(FString& OutError);
	bool IsValid() const { return NumQuests() > 0; }

	// Rebuild the title index from `Quests`. `Load` calls it; a hand-built catalogue (the tests')
	// needs it because the lookup is the index, not a scan.
	void Reindex();

	// Case-insensitive, and trimmed on both sides — the engine `Q_trimspace`s a Title at load and
	// matches a journal row with `Q_strnicmp`.
	const FElysiumQuest* Find(const FString& Title, FElysiumQuestRef* OutRef = nullptr) const;
	const FElysiumQuest* At(const FElysiumQuestRef& Ref) const;
	int32 NumQuests() const;
	int32 NumStates() const;

private:
	TMap<FString, FElysiumQuestRef> ByTitle;
};
