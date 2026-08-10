#include "Substrate/ElysiumDice.h"

#include "ElysiumRng.h"
#include "Substrate/ElysiumRulebook.h"

FString FElysiumRollResult::Describe() const
{
	return FString::Printf(TEXT("%s (%d) — %d successes, %d botches, net %d, %d tens"),
		ElysiumDice::TierName(Tier), static_cast<int32>(Tier), Successes, Botches, Net, Tens);
}

namespace ElysiumDice
{
	FElysiumRollResult Roll(int32 Pool, int32 Difficulty, const FElysiumDiceTable& Weighting,
		int32 AutomaticSuccesses, int32 HealthPenalty)
	{
		FElysiumRollResult Result;

		// The ctor clamps the pool into its results buffer, seeds the successes counter from the
		// caller rather than zeroing it, and carries the wound penalty as a subtraction off the pool
		// the roller walks — the roller's own debug print emits `pool - [0xe]` as the dice thrown.
		Pool = FMath::Min(Pool, MaxPool) - HealthPenalty;
		Result.Successes = AutomaticSuccesses;
		Result.Net = AutomaticSuccesses;

		if (Pool < 1)
		{
			// An empty pool never enters the loop, so the tier stays the ctor's default of 1 — a
			// failure regardless of how many automatic successes were seeded.
			Result.Tier = EElysiumRollTier::Failure;
			return Result;
		}

		const int32 InternalDifficulty = Difficulty - 1;
		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Dice);

		int32 DiceLeft = Pool;
		int32 Rolls = 0;
		while (DiceLeft >= 1 && Rolls < MaxRolls)
		{
			// The raw draw is uniform over [0,99] and the TABLE decides what face that is. Drawing a
			// face directly would be the same thing only for the shipped tables.
			const int32 Face = Weighting.Face(Rng.RandRange(0, FElysiumDiceTable::NumEntries - 1));
			++Rolls;

			if (Face == FElysiumDiceTable::NumFaces - 1)
			{
				// A physical 10, tested FIRST: it succeeds at any difficulty and adds a die back.
				++Result.Successes;
				++Result.Tens;
				++DiceLeft;
			}
			else if (Face == 0)
			{
				// A physical 1, tested before the difficulty compare: a 1 is never a success.
				++Result.Botches;
			}
			else if (Face >= InternalDifficulty)
			{
				++Result.Successes;
			}
			--DiceLeft;
		}

		Result.Net = Result.Successes - Result.Botches;
		Result.Tier = TierFor(Result.Successes, Result.Botches);
		return Result;
	}

	EElysiumRollTier TierFor(int32 Successes, int32 Botches)
	{
		const int32 Net = Successes - Botches;
		if (Net >= 5) { return EElysiumRollTier::CriticalSuccess; }
		if (Net >= 3) { return EElysiumRollTier::Success; }
		if (Net >= 1) { return EElysiumRollTier::PartialSuccess; }
		if (Net == 0 || Successes != 0) { return EElysiumRollTier::Failure; }
		return EElysiumRollTier::Botched;
	}

	const TCHAR* TierName(EElysiumRollTier Tier)
	{
		switch (Tier)
		{
		case EElysiumRollTier::Botched:         return TEXT("Botched");
		case EElysiumRollTier::Failure:         return TEXT("Failure");
		case EElysiumRollTier::PartialSuccess:  return TEXT("Partial Success");
		case EElysiumRollTier::Success:         return TEXT("Success");
		case EElysiumRollTier::CriticalSuccess: return TEXT("Critical Success");
		default:                                return TEXT("?");
		}
	}
}
