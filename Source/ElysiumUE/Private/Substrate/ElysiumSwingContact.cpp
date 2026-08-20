#include "Substrate/ElysiumSwingContact.h"

namespace ElysiumSwing
{
	int32 SubStepCount(float SpanSeconds)
	{
		if (!FMath::IsFinite(SpanSeconds) || SpanSeconds <= 0.0f)
		{
			return 0;
		}
		return FMath::FloorToInt32(SpanSeconds * SubStepHz);
	}

	float ClampBatchSpan(float AccumulatedSeconds, bool& bOutClamped)
	{
		bOutClamped = FMath::IsFinite(AccumulatedSeconds) && AccumulatedSeconds > MaxBatchSeconds;
		return bOutClamped ? MaxBatchSeconds : AccumulatedSeconds;
	}

	bool ExceedsBatchTravel(const FVector& From, const FVector& To)
	{
		if (From.ContainsNaN() || To.ContainsNaN())
		{
			// A coordinate that is not a number is the discontinuity, not an exception to it.
			return true;
		}
		return FVector::DistSquared(From, To)
			> static_cast<double>(MaxBatchTravelCm) * static_cast<double>(MaxBatchTravelCm);
	}

	void SubStepIntervals(float PrevCycle, float CurCycle, int32 SubSteps, TArray<FInterval>& Out)
	{
		Out.Reset();
		if (SubSteps < 1)
		{
			return;
		}
		// A non-finite phase is not a position on the clip. The pose layer's own reader reports one
		// where it can name the body; here there is no body to name, so it is treated as "the clip
		// did not advance" and the walk tests the instant rather than a garbage span.
		const float Start = FMath::IsFinite(PrevCycle) ? FMath::Clamp(PrevCycle, 0.0f, 1.0f) : 0.0f;
		float End = FMath::IsFinite(CurCycle) ? FMath::Clamp(CurCycle, 0.0f, 1.0f) : Start;
		if (End < Start)
		{
			End = Start;
		}

		Out.Reserve(SubSteps);
		const float Step = (End - Start) / static_cast<float>(SubSteps);
		for (int32 Index = 0; Index < SubSteps; ++Index)
		{
			FInterval Interval;
			Interval.Start = Start + Step * static_cast<float>(Index);
			// The last interval takes the measured end exactly rather than the accumulated one, so
			// the walk cannot fall a float epsilon short of a record that opens at the frame's edge.
			Interval.End = (Index == SubSteps - 1) ? End : Start + Step * static_cast<float>(Index + 1);
			Out.Add(Interval);
		}
	}

	bool WindowOverlaps(float WindowStart, float WindowEnd, float IntervalStart, float IntervalEnd)
	{
		return WindowStart <= IntervalEnd && IntervalStart <= WindowEnd;
	}

	bool RecordsOverlap(const FElysiumSwingRecord& A, const FElysiumSwingRecord& B)
	{
		return WindowOverlaps(A.Start, A.End, B.Start, B.End);
	}

	bool IsMarked(const TArray<FElysiumEntityHandle>& Hits, const FElysiumEntityHandle& Victim)
	{
		return Hits.Contains(Victim);
	}

	void MarkHit(const TArray<FElysiumSwingRecord>& Records, int32 HitIndex,
		const FElysiumEntityHandle& Victim, TArray<TArray<FElysiumEntityHandle>>& InOutHits)
	{
		if (!Records.IsValidIndex(HitIndex) || !Victim.IsSet())
		{
			return;
		}
		if (InOutHits.Num() < Records.Num())
		{
			InOutHits.SetNum(Records.Num());
		}
		for (int32 Index = 0; Index < Records.Num(); ++Index)
		{
			// The hitting record itself always overlaps its own window, so it needs no separate arm.
			if (RecordsOverlap(Records[HitIndex], Records[Index])
				&& !InOutHits[Index].Contains(Victim))
			{
				InOutHits[Index].Add(Victim);
			}
		}
	}

	void ClearClosedRecords(const TArray<FElysiumSwingRecord>& Records, float BatchStart,
		float BatchEnd, TArray<TArray<FElysiumEntityHandle>>& InOutHits)
	{
		if (InOutHits.Num() < Records.Num())
		{
			InOutHits.SetNum(Records.Num());
		}
		const float Low = FMath::Min(BatchStart, BatchEnd);
		const float High = FMath::Max(BatchStart, BatchEnd);
		for (int32 Index = 0; Index < Records.Num(); ++Index)
		{
			if (!WindowOverlaps(Records[Index].Start, Records[Index].End, Low, High))
			{
				InOutHits[Index].Reset();
			}
		}
	}
}
