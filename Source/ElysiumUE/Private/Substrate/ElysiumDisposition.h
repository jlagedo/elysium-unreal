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

// The `EyeTarget` block — where the gaze layer gets every one of its timings and rates. Retail
// compiles in no fallbacks for these: the built-in defaults sit in BSS and are zero, so a row that
// failed to parse would leave a character perfectly still rather than visibly wrong.
struct FElysiumEyeTargetTuning
{
	// "Default Direction" — a keypad cell (see FidgetPoints) the character returns to. Every
	// shipped row authors 0, which the table's own comment defines as "fall back to normal look
	// behavior", i.e. the selection cascade rather than a fixed head-relative point.
	int32 DefaultDirection = 0;

	// "Fidget Points" — three head-relative keypad cells the saccade walks in order. The cells are
	// laid out like a numeric keypad with 5 at centre, each step ±20° of yaw and pitch projected
	// 25 units out. A triple of all -1 means "pick a random cell 1-9 each time" instead of walking
	// a fixed path, and that is what every row but Anger, Disgust, Apathy and Confused authors.
	int32 FidgetPoints[3] = { -1, -1, -1 };

	// "Min Interval"/"Max Interval" — how long a converged gaze is held before a fidget sequence
	// starts. "Hold Min"/"Hold Max" — how long each cell within one sequence is held.
	float MinInterval = 5.f;
	float MaxInterval = 8.f;
	float HoldMin = 0.15f;
	float HoldMax = 0.25f;

	// "Eye Turn Rate" as authored *inside* this block, which is the one that varies per row:
	// 0.3 Neutral, 0.95 Anger, 0.6 Disgust, 0.2 Apathy and Confused. The file's own comment reads
	// "0.1 is slow, 1.0 is instant", so it is the per-step coefficient of the fixed 0.1 s
	// integrator rather than a rate in units per second.
	float TurnRate = 0.3f;
};

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

	// The blink cadence, in seconds: each blink is scheduled a uniform random interval after the
	// last. Authored at disposition level and, in the shipped table, only on `Neutral` — from which
	// every other row inherits it, so in practice these are global. The 300 ms envelope the toggle
	// drives is the client's and is not in this file.
	float MinBlinkInterval = 2.5f;
	float MaxBlinkInterval = 6.f;

	// The gaze block, and beside it the *second* "Eye Turn Rate" the file carries — this one at
	// disposition level rather than inside `EyeTarget`, authored 0.9 on every row that has it. Both
	// spellings are real and they disagree, so both are kept: the gaze integrator uses the block's,
	// which is the one that varies per disposition.
	FElysiumEyeTargetTuning EyeTarget;
	float EyeTurnRate = 0.9f;

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
