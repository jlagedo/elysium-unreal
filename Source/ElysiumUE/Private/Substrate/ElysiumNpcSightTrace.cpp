#include "Substrate/ElysiumNpcSightTrace.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumWorldServices.h"

namespace ElysiumNpcSight
{
	namespace
	{
		// `0x10450a9c`.
		constexpr double CornerPull = 0.9;
		// The probe values the table names: the eye and the centre.
		constexpr int32 ProbeCentre = 1;
		constexpr int32 ProbeFirstTopCorner = 2;
		constexpr int32 ProbeFirstBottomCorner = 6;
		constexpr int32 ProbeLastCorner = 9;
	}

	FVector VisibleTargetOrigin(int32 Probe, const FVector& TargetEyeCm, const FVector& OriginCm,
		const FVector& MinsCm, const FVector& MaxsCm)
	{
		if (Probe == ProbeCentre)
		{
			return (MinsCm + MaxsCm) * 0.5 + OriginCm;                       // 100a7360
		}
		if (Probe < ProbeFirstTopCorner || Probe > ProbeLastCorner)
		{
			return TargetEyeCm;                                               // 100a7890: 0, 10, > 10
		}
		const int32 Index = Probe - ProbeFirstTopCorner;                      // 0..7
		const bool bBottom = Probe >= ProbeFirstBottomCorner;
		const int32 Corner = Index & 3;                                       // mm, mM, Mm, MM
		const double CornerX = ((Corner & 2) != 0 ? MaxsCm.X : MinsCm.X) + OriginCm.X;
		const double CornerY = ((Corner & 1) != 0 ? MaxsCm.Y : MinsCm.Y) + OriginCm.Y;
		const double CornerZ = (bBottom ? MinsCm.Z : MaxsCm.Z) + OriginCm.Z;
		const double CentreZ = (MinsCm.Z + MaxsCm.Z) * 0.5 + OriginCm.Z;
		return FVector(CornerX, CornerY, CentreZ + CornerPull * (CornerZ - CentreZ));
	}

	bool Visible(const IElysiumEmbodiment& Embodiment, const FVisibleQuery& Query,
		FElysiumEntityHandle* OutBlocker)
	{
		if (OutBlocker != nullptr)
		{
			*OutBlocker = FElysiumEntityHandle::Invalid();
		}

		FElysiumRetailTrace Trace;
		Trace.StartCm = Query.EyeCm;
		Trace.EndCm = Query.TargetCm;
		Trace.RetailMask = Query.Mask;
		if (Query.Looker.IsSet())
		{
			Trace.Ignore.Add(Query.Looker);
		}
		if (Query.SecondIgnore.IsSet())
		{
			Trace.Ignore.Add(Query.SecondIgnore);
		}
		FElysiumRetailTraceResult Result;
		if (!Embodiment.TraceRetail(Trace, Result))
		{
			// Headless: no collision world. The brush-only answer every sight caller read before the
			// retail trace, and a block there is always the world's.
			return Embodiment.QueryLineOfSight(Query.EyeCm, Query.TargetCm);
		}

		// The nearest character the filter keeps. `Characters` is nearest first.
		const FElysiumRetailTraceCharacter* Kept = nullptr;
		for (const FElysiumRetailTraceCharacter& Character : Result.Characters)
		{
			const FElysiumEntity* Entity =
				Query.World != nullptr ? Query.World->Resolve(Character.Entity) : nullptr;
			if (Entity == nullptr)
			{
				continue;
			}
			// `CTraceFilterFVisible::ShouldHitEntity 0x10107630`: `m_bNPCTransparent` skips. The
			// TwoEnt filter has no such gate.
			if (!Query.bNpcsBlock && Entity->bNpcTransparent)
			{
				continue;
			}
			Kept = &Character;
			break;
		}

		const bool bWorldHit = Result.Fraction < 1.f;
		const bool bCharacterFirst = Kept != nullptr && (!bWorldHit || Kept->Fraction < Result.Fraction);
		if (!bWorldHit && Kept == nullptr)
		{
			return true;                                                     // fraction == 1.0
		}
		const FElysiumEntityHandle Hit = bCharacterFirst ? Kept->Entity : Result.HitEntity;
		if (Hit.IsSet() && Hit == Query.Target)
		{
			return true;                                                     // tr.m_pEnt == target
		}
		if (OutBlocker != nullptr)
		{
			*OutBlocker = Hit;                                               // Invalid: the static world
		}
		return false;
	}
}
