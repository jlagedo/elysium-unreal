#include "Substrate/ElysiumInterestingPlace.h"

#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpc.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"

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
	MarkersUsed = 0; // 0x102d9c20 +0x588, constructor alone clears failed/ring/in-use
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
	return MarkersAllocated - MarkersUsed - FailedAttempts >= 1 && bEnabled && !IsInert(); // 0x102da0d0 signed entry gate
}

bool FElysiumInterestingPlace::HasMarker(const FElysiumEntityHandle& Npc) const
{
	if (!Npc.IsSet()) return false; // 0x10299a80 live visitor never matches a null row
	for (int32 MarkerIndex = 0; MarkerIndex < MarkersUsed; ++MarkerIndex) // 0x10299a80
	{
		if (Markers[MarkerIndex].Occupant == Npc) return true;
	}
	return false;
}

bool FElysiumInterestingPlace::Claim(const FElysiumEntityHandle& Npc, bool bFireArrived)
{
	if (!Npc.IsSet()) { if (bFireArrived) Arrived(Npc); return false; } // 0x102da7c0 null-visitor output-only arm
	if (!HasMarker(Npc)) return false; // 0x102da7c0: ClaimMarker never adds a row
	if (bFireArrived) Arrived(Npc); // 0x102da7c0 output precedes increment
	++InUse; // +0x564, repeated calls are not deduplicated by a claimant set
	return true;
}

void FElysiumInterestingPlace::Release(const FElysiumEntityHandle& Npc, bool bFireLeft)
{
	// 0x102da600: last-place writer before scan, clear visitor before output, then full-row swap.
	if (!Npc.IsSet()) { if (bFireLeft) Left(Npc); return; }
	if (World != nullptr)
	{
		FElysiumEntity* VisitorEntity = World->Resolve(Npc);
		if (VisitorEntity != nullptr && VisitorEntity->AsNpc() != nullptr)
			VisitorEntity->AsNpc()->RememberLastInterestingPlace(Handle.Index);
	}
	for (int32 MarkerIndex = 0; MarkerIndex < MarkersUsed; ++MarkerIndex)
	{
		if (Markers[MarkerIndex].Occupant != Npc) continue;
		Markers[MarkerIndex].Occupant = FElysiumEntityHandle::Invalid(); // 0x102da600 clear before output
		if (bFireLeft) Left(Npc);
		--InUse;
		--MarkersUsed;
		++ReleasesObserved; // 0x102da600 actual row removal, no count for a refused release
		Markers[MarkerIndex] = Markers[MarkersUsed];
		Markers[MarkersUsed] = FMarker();
		return;
	}
}

bool FElysiumInterestingPlace::IsEnabledFor(const FElysiumEntityHandle& Npc) const
{
	return bEnabled && !IsInert() && HasMarker(Npc); // 0x10299a80 live reservation
}

FVector FElysiumInterestingPlace::SampleSpot(const FVector& HullMinUnits, const FVector& HullMaxUnits, bool bKeepZ) const
{
	// 0x102d9fa0 assembly: X/Y then optional Z draws. Authored bounds/hull stay Source axes;
	// positions are the port's axes, so the sampled Y offset is mirrored (KernelHullTrace contract).
	FRandomStream& SpotStream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	FVector SampleUnits = Origin / ElysiumMove::U;
	SampleUnits.X += SpotStream.FRandRange(static_cast<float>(MinBoundsUnits.X - HullMinUnits.X), static_cast<float>(MaxBoundsUnits.X - HullMaxUnits.X));
	SampleUnits.Y -= SpotStream.FRandRange(static_cast<float>(MinBoundsUnits.Y - HullMinUnits.Y), static_cast<float>(MaxBoundsUnits.Y - HullMaxUnits.Y));
	if (!bKeepZ) SampleUnits.Z += SpotStream.FRandRange(static_cast<float>(MinBoundsUnits.Z - HullMinUnits.Z), static_cast<float>(MaxBoundsUnits.Z - HullMaxUnits.Z));
	return SampleUnits;
}

bool FElysiumInterestingPlace::OverlapsMarker(const FVector& PositionUnits, const FVector& HullMinUnits, const FVector& HullMaxUnits) const
{
	// 0x102da9e0 assembly: retain shipped mins.X used for both lower Y and Z tests (inclusive).
	for (int32 MarkerIndex = 0; MarkerIndex < MarkersUsed; ++MarkerIndex)
	{
		const FMarker& Marker = Markers[MarkerIndex];
		if (!Marker.Occupant.IsSet()) continue;
		const FVector LowerUnits = Marker.MinBoundsCm / ElysiumMove::U;
		const FVector UpperUnits = Marker.MaxBoundsCm / ElysiumMove::U;
		if (LowerUnits.X <= PositionUnits.X + HullMaxUnits.X
			&& LowerUnits.Y <= PositionUnits.Y - HullMinUnits.X // native lower-Y bug becomes port upper-Y
			&& LowerUnits.Z <= PositionUnits.Z + HullMaxUnits.Z
			&& PositionUnits.X + HullMinUnits.X <= UpperUnits.X
			&& PositionUnits.Y - HullMaxUnits.Y <= UpperUnits.Y
			&& PositionUnits.Z + HullMinUnits.X <= UpperUnits.Z) return true;
	}
	return false;
}

bool FElysiumInterestingPlace::PickSpotFor(FElysiumNpc& Npc, FVector& OutPositionUnits, bool bKeepZ)
{
	if (!IsAvailable()) return false; // 0x102da0d0 capacity-count-failures, then enabled
	FVector HullMinUnits, HullMaxUnits;
	FElysiumNpcBase::RetailCollisionExtents(Npc, HullMinUnits, HullMaxUnits); // +0x270 collision property
	HullMinUnits.X -= 1.0; HullMinUnits.Y -= 1.0; // 0x104454c0 hull padding, XY only
	HullMaxUnits.X += 1.0; HullMaxUnits.Y += 1.0;
	const FVector HullMinPortUnits(HullMinUnits.X, -HullMaxUnits.Y, HullMinUnits.Z); // 0x102da860 Source bounds -> port POSITION
	const FVector HullMaxPortUnits(HullMaxUnits.X, -HullMinUnits.Y, HullMaxUnits.Z);
	int32 OccupiedReplacements = 0; // shared over both attempts, 0x102da0d0
	for (int32 ClearanceAttempt = 0; ClearanceAttempt < 2; ++ClearanceAttempt)
	{
		OutPositionUnits = SampleSpot(HullMinUnits, HullMaxUnits, bKeepZ); // 0x102d9fa0
		while (OverlapsMarker(OutPositionUnits, HullMinUnits, HullMaxUnits))
		{
			OutPositionUnits = SampleSpot(HullMinUnits, HullMaxUnits, bKeepZ); // replacement drawn BEFORE >8 test
			if (++OccupiedReplacements > 8)
			{
				++FailedAttempts; // 0x102da41d once, never for a clearance refusal
				return false;
			}
		}
		FElysiumNpcBase::FKernelHullTrace Clearance;
		Npc.bForceNpcCheck = true; // 0x102a0fb0 full-hull stationary move-probe bracket
		const bool bHasClearance = Npc.KernelHullTrace(OutPositionUnits, OutPositionUnits, HullMinUnits, HullMaxUnits, 0x202400b, Clearance);
		Npc.bForceNpcCheck = false;
		if (!bHasClearance) return false; // collision producer unavailable: do not manufacture success
		const bool bClear = Clearance.Fraction >= 1.f && !Clearance.bStartSolid && !Clearance.bAllSolid;
		if (!bClear)
		{
			if (FailedBoxCursor > 3) FailedBoxCursor = 0; // 0x102d9ed0 wraps on writer entry
			FFailedBox& FailedBox = FailedBoxes[FailedBoxCursor++];
			FailedBox.Until = (World != nullptr ? World->NowSeconds() : 0.0) + 2.0;
			FailedBox.MinBoundsCm = (OutPositionUnits + HullMinPortUnits) * ElysiumMove::U;
			FailedBox.MaxBoundsCm = (OutPositionUnits + HullMaxPortUnits) * ElysiumMove::U;
		}
		if (!bClear && ClearanceAttempt == 0) continue;
		if (ClearanceAttempt == 1) UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Interesting place %s exhausted clearance attempts"), *DebugString()); // 0x102da435 also warns when second attempt is clear
		FMarker& AddedMarker = Markers[MarkersUsed++]; // AddMarker 0x102da860 precedes ClaimMarker
		AddedMarker.Occupant = Npc.Handle;
		AddedMarker.MinBoundsCm = (OutPositionUnits + HullMinPortUnits) * ElysiumMove::U;
		AddedMarker.MaxBoundsCm = (OutPositionUnits + HullMaxPortUnits) * ElysiumMove::U;
		++ReservationsObserved; // 0x102da860 actual AddMarker, not ClaimMarker
		return true; // 0x102da4bb even after exhausted hull clearance
	}
	return false;
}

void FElysiumInterestingPlace::Serialize(FElysiumSaveArchive& Ar)
{
	FElysiumEntity::Serialize(Ar); // 0x102d9240 allocation/count, then each EHANDLE and both POSITION rows
	Ar << MarkersAllocated << MarkersUsed << FailedAttempts;
	if (Ar.IsLoading())
	{
		if (MarkersAllocated < 0 || MarkersAllocated > 4096 || MarkersUsed < 0 || MarkersUsed > MarkersAllocated)
		{
			Ar.SetError();
			return; // corrupt archive guard, not a retail capacity floor
		}
		Markers.SetNum(MarkersAllocated); // 0x102d9320 spare rows cleared
		for (FMarker& Marker : Markers) Marker = FMarker();
	}
	for (int32 MarkerIndex = 0; MarkerIndex < MarkersUsed; ++MarkerIndex)
	{
		FMarker& Marker = Markers[MarkerIndex];
		Ar << Marker.Occupant << Marker.MinBoundsCm << Marker.MaxBoundsCm; // 0x102d9240/0x102d9320
	}
	// +0x564 in-use and +0x594..604 failed ring have no SAVE rows; constructor state survives.
}

void FElysiumInterestingPlace::RebaseSavedReferences(FElysiumEntityWorld& InWorld)
{
	for (int32 MarkerIndex = 0; MarkerIndex < MarkersUsed; ++MarkerIndex)
		Markers[MarkerIndex].Occupant = InWorld.RestoreHandle(Markers[MarkerIndex].Occupant); // 0x102d9320 before NPC scan
}

void FElysiumInterestingPlace::OnPostRestore(FElysiumEntityWorld& InWorld)
{
	(void)InWorld; // 0x102d9c20 restore half does not Spawn/zero restored markers
	ResolveTypeOrRemove(true);
	GroupMask = GroupId >= 1 && GroupId <= 32 ? (1 << (GroupId - 1)) : 1;
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
	Out.Emplace(TEXT("Occupancy"), FString::Printf(TEXT("%d/%d"), MarkersUsed, MarkersAllocated));
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

FElysiumEntityHandle FElysiumInterestingPlace::MarkerOccupant(int32 RowIndex) const
{
	// 0x102da9a0: used-row lookup; allocated spare rows answer NULL, no claimant-set substitute.
	return RowIndex >= 0 && RowIndex < MarkersUsed && Markers.IsValidIndex(RowIndex)
		? Markers[RowIndex].Occupant : FElysiumEntityHandle::Invalid();
}

FElysiumEntityHandle FElysiumInterestingPlace::ConversationTalkerSource() const
{
	// 0x102db760 +0x608 -> 0x102dcc20 conversation +0x504/current +0x518/list -> selected place's marker0.
	// The conversation activation/current-list producer is unbuilt (0018/18); never use our own first row instead.
	return FElysiumEntityHandle::Invalid();
}
