#pragma once

#include "CoreMinimal.h"

// VtMB's World-of-Darkness d10 resolver — the pure rules leaf beside `ElysiumSheetMath`.
//
// A function over (pool, difficulty, weighting table, the owned RNG stream). No world, no clock, no
// engine service: given the Dice stream's state it is deterministic, which is what lets the save
// carry a run across a load (`Public/ElysiumRng.h`).
//
// The recovered algorithm — the roll struct's field map, the exploding-10 loop, the two 250 caps,
// the tiering and the health penalty — is canonical and is
// implemented here verbatim. The die's face distribution is NOT `rand()%10`: it is a 100-entry
// lookup the caller supplies as `FElysiumDiceTable` (`Substrate/ElysiumDiceTables.h`), loaded from
// `vdata/system/dicerolls.txt`.
//
// What this does NOT decide is what a roll MEANS. Thresholds, opposed comparisons, retry pacing and
// the botch table are consumer policy, consolidated in `docs/vtmb/skills-and-checks.md`; the whole
// result is returned so each consumer applies its own. A rating is not a roll: `ElysiumFeats::Calc`
// answers `CalcFeat` and never comes here.

struct FElysiumDiceTable;

// The result tier the roll writes back into `[0xc]`. The names are `dicerolls.txt`'s own
// `Text/RollResult` display strings, which confirm the enum 1:1.
enum class EElysiumRollTier : uint8
{
	Botched = 0,
	Failure = 1,
	PartialSuccess = 2,
	Success = 3,
	CriticalSuccess = 4,
};

// One roll, whole. `Successes` is the working counter the ctor SEEDS from the caller's automatic
// successes, so it is not merely a count of winning dice; `Tens` is the roll struct's `[2]`, kept
// because it is what the engine's own `vroll` report prints and an exploding roll is unreadable
// without it.
struct FElysiumRollResult
{
	int32 Successes = 0;
	int32 Botches = 0;
	int32 Net = 0;
	int32 Tens = 0;
	EElysiumRollTier Tier = EElysiumRollTier::Failure;   // the ctor's default is 1, not 0

	FString Describe() const;
};

namespace ElysiumDice
{
	// The initial-pool clamp (`FUN_101d88b0`, whose results buffer is 252 entries) and the roller's
	// SEPARATE cap on total rolls, which is what bounds a runaway 10-again explosion.
	inline constexpr int32 MaxPool = 250;
	inline constexpr int32 MaxRolls = 250;

	// `Difficulty` is the HUMAN WoD target number, 1..10 — the value `vroll` takes raw from its
	// argument. The engine stores it decremented and compares `face(0..9) >= difficulty - 1`, which
	// is exactly "the physical die shows at least the difficulty"; a port comparing a 0-based face
	// against a raw difficulty is off by one.
	//
	// `AutomaticSuccesses` is the ctor's successes seed (`p[1] = arg7`) — combat's
	// `Automatic_Str_Successes` / `Automatic_Soak_Successes`, which the CALLER resolves.
	// `HealthPenalty` is `[0xe]`, the wound modifier subtracted from the pool: the number comes from
	// `dicerolls.txt`'s `HealthModifiers`, but WHICH health level a character sits at is a consumer's
	// question, so it arrives resolved. Every shipped entry is 0.
	FElysiumRollResult Roll(int32 Pool, int32 Difficulty, const FElysiumDiceTable& Weighting,
		int32 AutomaticSuccesses = 0, int32 HealthPenalty = 0);

	// The tiering, separated because it is a total function of the two counters and every band edge
	// is worth asserting on its own. A net of 0 is a Failure whether or not anything succeeded; only
	// a NEGATIVE net with no raw success at all botches.
	//
	// A consumer owning a botch table rolls it when the tier is `Botched` and `Botches > 1` — the
	// engine's `[6]` object, which `vroll` passes as null. Nothing here owns one.
	EElysiumRollTier TierFor(int32 Successes, int32 Botches);

	const TCHAR* TierName(EElysiumRollTier Tier);
}
