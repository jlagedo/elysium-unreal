#pragma once

#include "CoreMinimal.h"

// VtMB's disposition table — `vdata/system/dispositiontable.txt`.
//
// A disposition is the NPC's emotional stance toward the player. It decides two things the
// shipped data states outright: which **animation set** the NPC idles in ("Animation Name", which
// keys the `Stance_<Name>_*` clips in the gendered stances bank), and how often it fidgets or
// changes stance while talking or waiting.
//
// `AnimName` picks a standing idle. The same rows also carry the expression and eye-target
// blocks for the NPC emotional-state model — `SetDisposition(name, level)` alone is 2,510 of
// 2,862 engine calls. This reader carries the whole row so both consumers share one parse.
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

	// The integrator has TWO rates and the fidget driver picks between them every think. Retail's
	// `FUN_102c0010` re-reads `m_flEyeIntegRate`@0x0E3C from the table through
	// `FUN_100ecdf0(table, disposition, index)` — index 0 in the branch that holds a converged gaze
	// (`0x100ece2a`, record field +0x23C) and index 1 in the branch that steps to the next fidget
	// cell (`0x100ece15`, record field +0x260). The record is 0x264 bytes and the parser
	// (`FUN_100eba00`) writes the disposition-level "Eye Turn Rate" into +0x23C and the one inside
	// the `EyeTarget` block into +0x260 — so the two spellings the file carries are not a
	// duplicate at all: they are the hold rate and the step rate.
	//
	// HoldRate is the disposition-level "Eye Turn Rate", authored 0.9 on every row that has it and
	// inherited by the rest through `CopyDataFrom`, so in practice it is global. It is filled from
	// `FElysiumDisposition::EyeTurnRate` by the parser, not by the `EyeTarget` block.
	float HoldRate = 0.9f;

	// StepRate is "Eye Turn Rate" as authored *inside* this block, which is the one that varies per
	// row: 0.3 Neutral, 0.95 Anger, 0.6 Disgust, 0.2 Apathy and Confused. The file's own comment
	// reads "0.1 is slow, 1.0 is instant", so both are the per-step coefficient of the fixed 0.1 s
	// integrator rather than a rate in units per second.
	float StepRate = 0.3f;
};

struct FElysiumDisposition
{
	FString Name;                  // the block name, e.g. "Neutral" — matches `default_disposition`
	FString AnimName;              // "Animation Name" — the `Stance_<AnimName>_Idle_*` token
	int32   Level = 1;             // "DispositionLevel"

	// The resting face selected beside the stance. `TalkingExpression` is the alternate row while
	// a line is actually playing; an omitted value inherits through the table just like retail's
	// record copy. Both name rows in `<model>_expressions.txt`.
	FString DefaultExpression;
	FString TalkingExpression;
	float ExpressionIntensity = 1.f;

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
	// spellings are real and they disagree because they are two different rates: this one is the
	// integrator's HOLD rate and the block's is its fidget STEP rate (see FElysiumEyeTargetTuning).
	// The parser mirrors this value into `EyeTarget.HoldRate` so the gaze layer needs only the
	// block.
	FElysiumEyeTargetTuning EyeTarget;
	float EyeTurnRate = 0.9f;

	bool IsValid() const { return !Name.IsEmpty(); }
};

// The parsed table. Load() is cheap (19 rows) and idempotent; the caller caches it.
struct FElysiumDispositionTable
{
	// File order is semantic. A name may repeat at several DispositionLevels (`Joy` has 1/2/3),
	// and `CopyDataFrom` may only refer to an earlier row. Keeping the records as an array preserves
	// both facts; Resolve folds the authored name and selects the requested level.
	TArray<FElysiumDisposition> Rows;

	bool IsValid() const { return !Rows.IsEmpty(); }
	bool Load(FString& OutError);
	// Content-free parser used by the substrate tests; Load is only the file seam around it.
	bool ParseText(const FString& Text, const FString& Source, FString& OutError);

	// The row for (name, level), decrementing toward level 1 when the requested level is absent,
	// or Neutral level 1 when the name itself is unknown.
	// The table's own comment makes Neutral the fallback: "The 'Neutral' disposition must be the
	// first one in the list, it is what the others will get their starting values from."
	const FElysiumDisposition* Resolve(const FString& Disposition, int32 Level = 1) const;

	// The `Stance_<...>_` token for a disposition — its "Animation Name", falling back to the
	// disposition's own name and finally to "Neutral", so a stance set always resolves.
	FString AnimNameFor(const FString& Disposition, int32 Level = 1) const;

	static const TCHAR* NeutralName;
};
