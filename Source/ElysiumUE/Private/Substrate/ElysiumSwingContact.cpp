#include "Substrate/ElysiumSwingContact.h"

#include "ElysiumMoveSolve.h"   // ElysiumMove::U -- centimetres per Source unit

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

	int32 SampleCount(const FVector& ACm, const FVector& BCm)
	{
		// `MeleeSwingStep 0x10343020`: `n = ceil(|B - A| * [0x10488874])`, `n < 2` -> 1.
		// The product is taken in double, as the x87 stack takes it (the length times the f32
		// constant, never rounded back to f32 before `_ceil`): the constant is a hair ABOVE 1/6, so
		// a segment of exactly 6 units answers 2, of exactly 12 answers 3.
		const double LengthUnits = FVector::Dist(ACm, BCm) / static_cast<double>(ElysiumMove::U);
		if (!FMath::IsFinite(LengthUnits))
		{
			return 1;   // crash guard (named): a non-finite segment has no length to sample
		}
		const int32 Samples = FMath::CeilToInt32(LengthUnits * static_cast<double>(SamplesPerUnit));
		return Samples < 2 ? 1 : Samples;
	}

	float SampleFraction(int32 SampleIndex, int32 Samples)
	{
		// `0x10343fc4`: `n > 1 ? 1.0 - i / (n - 1) : 0.0` (`[0x104454c0]` = 1.0, `[0x104454c4]` = 0.0).
		return Samples > 1
			? 1.0f - static_cast<float>(SampleIndex) / static_cast<float>(Samples - 1)
			: 0.0f;
	}

	bool WallPlaneQualifies(const FVector& PlaneNormal)
	{
		// `0x10344221..0x1034425c`: `normal . normal > 0`; `0x10344262..0x1034428a`: `|normal.z| <
		// 0.3`.
		return PlaneNormal.SizeSquared() > 0.0
			&& FMath::Abs(PlaneNormal.Z) < static_cast<double>(WallMaxNormalZ);
	}

	bool WallBlocksSwing(const FVector& PlaneNormal, const FVector& ForwardWorld,
		const FVector& HitPointCm, const FVector& AttackerOriginCm)
	{
		if (!WallPlaneQualifies(PlaneNormal))
		{
			return false;
		}
		// `0x103442ac..0x103442eb`: forward.z = 0; its 2-D length^2 must exceed `[0x1049e038]^2`.
		FVector Forward(ForwardWorld.X, ForwardWorld.Y, 0.0);
		if (!(Forward.SizeSquared() > static_cast<double>(WallMinForwardLenSq)))
		{
			return false;
		}
		Forward.Normalize();   // `0x103442f8`
		// `0x10344300..0x1034433d`: `|dot(forward, normal)| > 0.7071`.
		if (!(FMath::Abs(FVector::DotProduct(Forward, PlaneNormal))
			> static_cast<double>(WallMinFacingDot)))
		{
			return false;
		}
		// `0x1034433f..0x10344382`: `sqrt(dx^2 + dy^2) < 20.0`, hit point against `GetAbsOrigin()`.
		const double Dx = HitPointCm.X - AttackerOriginCm.X;
		const double Dy = HitPointCm.Y - AttackerOriginCm.Y;
		return FMath::Sqrt(Dx * Dx + Dy * Dy)
			< static_cast<double>(WallMaxDistanceUnits * ElysiumMove::U);
	}
}
