#pragma once

#include "CoreMinimal.h"

namespace ElysiumKeyValues { struct FKvNode; }

struct FElysiumFeat;

// --- dicerolls.txt — the d10 weighting tables and the wound penalty ---

// One `Rules/TableWeightings` child. The engine draws `RandomInt(0, 99)` and reads `Faces[draw]`,
// so the table IS the die rather than a bias applied to one: it is the whole mapping from the raw
// draw to a face `0..9`, a physical d10 showing `1..10`.
//
// The three shipped tables (`Normal`, `Heavy`, `Light`) are each a plain uniform d10
// (`face = draw / 10`), which is what makes the uniform fallback below faithful rather than
// approximate. The mechanism is real all the same — the file's own comment invites a mod to
// reweight a die — so the resolver reads this instead of assuming uniformity.
struct FElysiumDiceTable
{
	static constexpr int32 NumEntries = 100;   // the raw draw's domain, [0, 99]
	static constexpr int32 NumFaces = 10;

	// The block key the weighting is selected by (`Normal`, `Heavy`, `Light`), lowercased by the KV
	// reader as every block key is; `Name` is the block's own authored display string.
	FString InternalName;
	FString Name;
	int32 Faces[NumEntries];

	FElysiumDiceTable();    // constructs the uniform d10

	// An ABSENT entry reads as face 1, not 0 — the loader's own `GetInt(key, 1)` default
	// (`FUN_101d90c0`), which matters only for a partially authored mod table.
	void Load(const ElysiumKeyValues::FKvNode& Node);

	// `Faces[Draw]`. The INDEX is clamped into range; the value is whatever was authored, because a
	// face outside 0..9 would be a modder's statement rather than a parse error.
	int32 Face(int32 Draw) const;
	bool IsUniform() const;

	// What a resolver rolls on when `dicerolls.txt` is absent. Failing open to uniform is faithful,
	// not a guess: every shipped table is this table.
	static const FElysiumDiceTable& Uniform();
};

struct FElysiumDiceTables
{
	// File order. Index 0 is the engine's own fallback for a weighting name that matches nothing.
	TArray<FElysiumDiceTable> Tables;

	// `Rules/HealthModifiers` — health level (0..7) -> the dice a wounded character loses off a
	// pool, which is the roll struct's `[0xe]`. Every shipped entry is 0, so the penalty is a no-op
	// today; it is loaded rather than assumed so a patch that authors one applies.
	//
	// WHICH level a character sits at is a consumer's question, so this is the lookup and not the
	// answer — `ElysiumDice::Roll` takes the resolved number. Nothing reads it yet.
	TArray<int32> HealthModifiers;

	bool Load(FString& OutError);
	bool IsValid() const { return !Tables.IsEmpty(); }

	// Rebuild the name index from `Tables`. `Load` calls it; a hand-built set (the tests') needs it
	// because the lookup IS the index, not a scan — the rule `FElysiumQuestTables` established.
	void Reindex();

	// Case-insensitive. A miss falls back to index 0, as the engine's own selection does, and to the
	// uniform table when nothing loaded at all.
	const FElysiumDiceTable& Find(const FString& InternalName) const;
	const FElysiumDiceTable& At(int32 Index) const;
	int32 Num() const { return Tables.Num(); }

	// The weighting a feat rolls on, PC or NPC side. `FElysiumFeat` stores the authored NAME and the
	// join lives here, so the two files stay independently loadable and a feat read costs nothing
	// when no roll is involved. All 23 shipped feats name `Normal` on both sides.
	const FElysiumDiceTable& ForFeat(const FElysiumFeat& Feat, bool bNpc) const;

	// The pool penalty for a health level; 0 for an unauthored level or an unloaded table.
	int32 HealthModifier(int32 HealthLevel) const;

private:
	TMap<FString, int32> ByName;
};
