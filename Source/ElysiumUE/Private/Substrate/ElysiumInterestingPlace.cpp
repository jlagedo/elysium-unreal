#include "Substrate/ElysiumInterestingPlace.h"

#include "Substrate/ElysiumNpcLog.h"

// --- Story 29c-1, family Lifecycle ----------------------------------------------------------------

bool FElysiumInterestingPlace::ResolveTypeOrRemove(bool bWarn)
{
	// The half `CAI_InterestingPlace::Spawn` (`0x102d9c20`) and its `OnRestore`
	// share, transcribed from the first: `strcmp(m_sType, "")` — an EMPTY type never looks up and
	// never removes — then `thunk_FUN_102dd9c0(m_sType)` into `field_0x548`, and a miss prints
	// "Could not find InterestingPlaceT..." and `UTIL_Remove`s the entity.
	if (Type.IsEmpty())
	{
		return true;
	}
	const FElysiumInterestingPlaceTable* Table = ElysiumInterestingPlaces::Types();
	ResolvedType = Table ? Table->Find(Type) : nullptr;
	if (ResolvedType == nullptr)
	{
		// Retail's two call sites differ ONLY in severity: `Spawn` uses `Warning`, `OnRestore` uses
		// `DevMsg`. Both then remove.
		if (bWarn)
		{
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("Could not find InterestingPlaceType %s"), *Type);
		}
		else
		{
			UE_LOG(LogElysiumNpcEnt, Verbose,
				TEXT("Could not find InterestingPlaceType %s"), *Type);
		}
		Kill();   // thunk_FUN_101cd970 — UTIL_Remove
		return false;
	}
	return true;
}

void FElysiumInterestingPlace::Spawn()
{
	// 0x102d9c20, in retail's own order.
	//
	// 1. The type lookup, which RETURNS on a miss — nothing below runs for a place whose type does
	//    not resolve, because the entity is already gone.
	if (!ResolveTypeOrRemove(/*bWarn=*/true))
	{
		return;
	}
	// 2. The marker table: a `DevMsg` above 20, then `m_iMarkersAllocated` records of stride `0x1c`
	//    with the first dword of each zeroed. An allocation of zero or fewer allocates nothing and
	//    zeroes nothing, which is the loop's own guard.
	if (MarkersAllocated > MarkerSpeedWarningThreshold)
	{
		UE_LOG(LogElysiumNpcEnt, Log,
			TEXT("Warning: Possible speed issues with %d interesting-place markers"),
			MarkersAllocated);
	}
	Markers.Reset();
	if (MarkersAllocated > 0)
	{
		Markers.SetNum(MarkersAllocated);
		for (FMarker& Marker : Markers)
		{
			Marker.Occupant = FElysiumEntityHandle::Invalid();   // the zeroed first dword
		}
	}
	// 3. `field_0x588 = 0` (a word with no recovered reader), then the group fold. Out of 1..32
	//    folds to the LITERAL 1 — this is NOT `CAI_Hint::Spawn`'s `-1`, and the difference is what
	//    makes an unauthored place join group 1 rather than every group.
	GroupMask = (GroupId >= 1 && GroupId <= 32) ? (1 << (GroupId - 1)) : 1;
	// 4. The rating clamp, LAST, and asymmetric: a negative rating becomes 0 and RETURNS (the
	//    high-side test is skipped), a rating over 5 becomes 5.
	if (Rating < 0)
	{
		Rating = 0;
		return;
	}
	if (Rating > 5)
	{
		Rating = 5;
	}
}

bool FElysiumInterestingPlace::IsAvailable() const
{
	return bEnabled && !IsInert() && Claimants.Num() < FMath::Max(1, MaxNpcs);
}

bool FElysiumInterestingPlace::Claim(const FElysiumEntityHandle& Npc)
{
	if (Claimants.Contains(Npc.Index))
	{
		return true;
	}
	if (!IsAvailable())
	{
		return false;
	}
	Claimants.Add(Npc.Index);
	return true;
}

bool FElysiumInterestingPlace::IsEnabledFor(const FElysiumEntityHandle& Npc) const
{
	return bEnabled && !IsInert() && Claimants.Contains(Npc.Index);
}

void FElysiumInterestingPlace::Arrived(const FElysiumEntityHandle& Npc)
{
	FireOutput(FName(TEXT("OnNPCArrived")), Npc);
}

void FElysiumInterestingPlace::Left(const FElysiumEntityHandle& Npc)
{
	FireOutput(FName(TEXT("OnNPCLeft")), Npc);
}

void FElysiumInterestingPlace::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("Enabled"), bEnabled ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Type"), Type);
	Out.Emplace(TEXT("Occupancy"), FString::Printf(TEXT("%d/%d"), Claimants.Num(), FMath::Max(1, MaxNpcs)));
	Out.Emplace(TEXT("Group"), FString::FromInt(GroupId));
	Out.Emplace(TEXT("Rating"), FString::FromInt(Rating));
}

namespace ElysiumInterestingPlaces
{
	const FElysiumInterestingPlaceTable* Types()
	{
		static FElysiumInterestingPlaceTable Table;
		static bool bAttempted = false;
		static bool bLoaded = false;
		if (!bAttempted)
		{
			bAttempted = true;
			FString Error;
			bLoaded = Table.Load(Error);
			if (!bLoaded)
			{
				UE_LOG(LogElysiumNpcEnt, Warning, TEXT("interesting-place types unavailable: %s"), *Error);
			}
			else
			{
				UE_LOG(LogElysiumNpcEnt, Log, TEXT("loaded %d interesting-place types"), Table.Num());
			}
		}
		return bLoaded ? &Table : nullptr;
	}
}
