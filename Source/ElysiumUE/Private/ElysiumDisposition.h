#pragma once

#include "CoreMinimal.h"

// VtMB's disposition table — `vdata/system/dispositiontable.txt`, mirrored by PL5b.
//
// A disposition is the NPC's emotional stance toward the player. It decides two things the
// shipped data states outright: which **animation set** the NPC idles in ("Animation Name", which
// keys the `Stance_<Name>_*` clips in the gendered stances bank), and how often it fidgets or
// changes stance while talking or waiting.
//
// The table is the data behind two roadmap tasks, so it lives on its own rather than inside
// either: **8.5** reads `AnimName` to pick a standing idle, and **9.9** — the single largest
// engine demand in the game at 2,862 calls, `SetDisposition(name, level)` alone being 2,510 —
// needs the same rows plus the expression/eye-target blocks for the NPC emotional-state model.
// This reader carries the whole row so 9.9 extends it rather than reparsing.
//
// `default_disposition` is authored on 242 of 243 `npc_*` entities across the exported maps
// (239 of them `Neutral`).

struct FElysiumDisposition
{
	FString Name;                  // the block name, e.g. "Neutral" — matches `default_disposition`
	FString AnimName;              // "Animation Name" — the `Stance_<AnimName>_Idle_*` token
	int32   Level = 0;             // "DispositionLevel"

	// Fidget/stance-change pacing, verbatim from the table. Chances are percentages, thresholds
	// are seconds. The comments in the shipped file explain them: a character has
	// `StandingFidgetChance` of picking a fidget while not talking, and
	// `StandingStanceChangeChance` of changing stance once the player has left it waiting
	// `StandingStanceChangeThreshold` seconds.
	float TalkingStanceChangeThreshold = 0.f;
	int32 TalkingStanceChangeChance = 0;
	int32 StandingFidgetChance = 0;
	float StandingStanceChangeThreshold = 0.f;
	int32 StandingStanceChangeChance = 0;

	bool IsValid() const { return !Name.IsEmpty(); }
};

// The parsed table. Load() is cheap (19 rows) and idempotent; the caller caches it.
struct FElysiumDispositionTable
{
	// Keyed by the block name, case-insensitively — the maps spell it `Neutral`, `Anger`, `fear`,
	// `Damaged`, and the table's own casing is not what the entity keyfield uses.
	TMap<FString, FElysiumDisposition> Rows;

	bool IsValid() const { return !Rows.IsEmpty(); }
	bool Load(FString& OutError);

	// The row for a `default_disposition` value, or the Neutral row when it names nothing known.
	// The table's own comment makes Neutral the fallback: "The 'Neutral' disposition must be the
	// first one in the list, it is what the others will get their starting values from."
	const FElysiumDisposition* Resolve(const FString& Disposition) const;

	// The `Stance_<...>_` token for a disposition — its "Animation Name", falling back to the
	// disposition's own name and finally to "Neutral", so a stance set always resolves.
	FString AnimNameFor(const FString& Disposition) const;

	static const TCHAR* NeutralName;
};
