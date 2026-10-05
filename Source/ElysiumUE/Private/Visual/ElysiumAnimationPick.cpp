#include "Visual/ElysiumAnimationPick.h"

#include "ElysiumRng.h"

int32 ElysiumAnimationPick::Weighted(TConstArrayView<FCandidate> Candidates)
{
	if (Candidates.IsEmpty()) { return INDEX_NONE; } // 0x10427fc0
	if (Candidates.Num() == 1) { return Candidates[0].Sequence; } // 0x10427fc0
	int32 TotalWeight = 0; // 0x10427fc0
	for (const FCandidate& Candidate : Candidates) { TotalWeight += Candidate.Weight; } // 0x10427fc0
	// 0x1070b244 slot 2: owner ruling K4, every animation pick shares NpcSchedule.
	FRandomStream& PickStream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	if (TotalWeight <= 0) // 0x10427fc0
	{
		return Candidates[PickStream.RandRange(0, Candidates.Num() - 1)].Sequence; // 0x10427fc0
	}
	int32 Remaining = TotalWeight == 1 ? 0 : PickStream.RandRange(0, TotalWeight - 1); // 0x10427fc0 -> vstdlib 0x10002e60
	for (const FCandidate& Candidate : Candidates) // 0x10427fc0
	{
		if (Candidate.Weight > Remaining) { return Candidate.Sequence; } // 0x10427fc0
		Remaining -= Candidate.Weight; // 0x10427fc0: weight <= r, table order
	}
	return INDEX_NONE; // 0x10427fc0
}

int32 ElysiumAnimationPick::Heaviest(TConstArrayView<FCandidate> Candidates)
{
	if (Candidates.IsEmpty()) { return INDEX_NONE; } // 0x104280f0
	int32 BestSequence = Candidates[0].Sequence; // 0x104280f0: first is incumbent even at MIN_int32
	int32 BestWeight = MIN_int32; // 0x104280f0
	for (const FCandidate& Candidate : Candidates) // 0x104280f0
	{
		if (BestWeight < Candidate.Weight) // 0x104280f0: strict comparison, first tie
		{
			BestWeight = Candidate.Weight; // 0x104280f0
			BestSequence = Candidate.Sequence; // 0x104280f0
		}
	}
	return BestSequence; // 0x104280f0: no random draw
}
