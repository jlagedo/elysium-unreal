#pragma once

#include "CoreMinimal.h"

// `system/sound_volume_table.txt` — the game-sound levels and their occlusion policy.
//
// The authored half of NPC hearing (`docs/vtmb/npc-ai-reverse-engineering.md` → "Visual and
// auditory input"). Four blocks, read as one table:
//
//   * `VolumeLevels`         — level index → the radius an average human hears it at.
//   * `OccludedVolumeLevels` — the same indices → 1 when world geometry can occlude the level.
//   * `SoundTypes`           — the named category a producer emits (`PLAYER_GUNSHOT_PISTOL`,
//                              `NPC_TAKE_DAMAGE`, `DOOR_NORMAL`) → the level it carries.
//   * `MiscData`             — loose tuning tags; the shipped file authors `PLAYER_RUN_SPEED` and
//                              comments the other two out, so an absent tag is the ordinary case.
//
// **Radii stay in the authored Source game units.** This is the data layer; the one conversion to
// centimetres happens at the consumer edge through `ElysiumMove::U`, like every other recovered
// distance in the runtime.

// One `VolumeLevels` row joined to its `OccludedVolumeLevels` answer.
struct FElysiumSoundLevel
{
	int32 Level = INDEX_NONE;    // the authored level index — the value a `SoundTypes` row names
	float RadiusUnits = 0.f;     // Source game units
	bool  bOccludable = false;   // 0 = world geometry cannot stop it (the loud/level-3 family)

	bool IsValid() const { return Level != INDEX_NONE; }
};

// One `SoundTypes` row. `Name` is the FOLDED spelling: the KV reader lowercases every block key,
// which is also how every lookup here is keyed.
struct FElysiumSoundCategory
{
	FString Name;
	int32 Level = INDEX_NONE;
};

struct FElysiumSoundVolumeTable
{
	// The semantic levels the file's own comments name. A producer emits a CATEGORY and never a
	// level; these exist for the two places that need the level itself — the unknown-category
	// default and the absent-file fallback.
	static constexpr int32 SilentLevel        = 0;
	static constexpr int32 QuietLevel         = 1;
	static constexpr int32 NormalLevel        = 2;
	static constexpr int32 LoudLevel          = 3;
	static constexpr int32 PlayerStealthLevel = 4;
	static constexpr int32 FeedingLevel       = 5;

	TArray<FElysiumSoundLevel> Levels;      // file order, which is also level order
	TArray<FElysiumSoundCategory> Categories;   // file order
	TMap<FString, FString> Misc;            // `MiscData`, folded tag -> raw value

	bool Load(FString& OutError);
	bool IsValid() const { return !Levels.IsEmpty() && !Categories.IsEmpty(); }

	// Rebuild both indices from `Levels`/`Categories`. `Load` calls it; a hand-built table (the
	// tests') needs it because the lookup IS the index, not a scan — the rule `FElysiumQuestTables`
	// established.
	void Reindex();

	const FElysiumSoundLevel* Level(int32 Index) const;
	// The level a named category carries, or null when the table names no such category — which is
	// the caller's cue to fall back and say so. Case-insensitive, as every KV lookup in the game is.
	const FElysiumSoundLevel* FindCategory(const FString& Category) const;
	int32 NumLevels() const { return Levels.Num(); }
	int32 NumCategories() const { return Categories.Num(); }

	float MiscFloat(const TCHAR* Tag, float Def) const;

	// What a resolver uses when `sound_volume_table.txt` is absent entirely: LEVEL_2 exactly as the
	// shipped file authors it. Failing open to the normal level is faithful rather than a guess —
	// it is the same level an unknown category already resolves to — and it is the shape
	// `FElysiumDiceTable::Uniform()` established for a missing export.
	static const FElysiumSoundLevel& NormalFallback();

private:
	TMap<int32, int32> LevelByIndex;
	TMap<FString, int32> CategoryByName;
};
