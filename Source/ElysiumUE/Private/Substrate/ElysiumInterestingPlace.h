#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntity.h"
#include "Substrate/ElysiumInterestingPlaces.h"

// `intersting_place` — retail's shipped classname is misspelled. The entity owns enable/capacity,
// the authored timing/orientation/type fields, and arrival/leave outputs. NPCs own reservations:
// the logical state remains saveable in the substrate while Unreal only moves the body.

class FElysiumInterestingPlace final : public FElysiumEntity
{
public:
	FString Type;
	bool bEnabled = true;
	int32 GroupId = 0;
	int32 Rating = 0;
	int32 TestFlags = 0;
	bool bMatchOrientation = false;
	float MinTime = 5.0f;
	float MaxTime = 10.0f;

	// --- Story 29c-1, family Lifecycle: `CAI_InterestingPlace`'s own spawn and restore -------------
	//
	// The three words `Spawn` (`0x102d9c20`) writes that the keyfields above do not carry, by retail
	// offset. Walked at `docs/vtmb/npc-ai/lifecycle.md`.

	// +0x0548 — the `interestingplacetypelist.txt` row `m_sType` resolved to. Null until `Spawn`
	// runs, and null on a place whose type did not resolve — which is the arm that removes it.
	const FElysiumInterestingPlaceType* ResolvedType = nullptr;

	// +0x0574 `m_iGroupID` AFTER the fold. Retail folds in place: `1 << (id - 1)` for an id in
	// 1..32, and the literal `1` for anything else — NOT `0xffffffff`, which is `CAI_Hint::Spawn`'s
	// fold and a different body. The port keeps the authored id beside the mask rather than folding
	// in place, because `FElysiumNpc::AcceptsAmbientGroup` reads the authored id under its own named
	// divergence; nothing else about the fold changes.
	int32 GroupMask = 0;

	// +0x0584 `m_iMarkersAllocated` (key `max_npcs`; the datamap replay, offset 1412 -- +0x0578 is
	// `m_iRating`, offset 1400) and +0x0580 `m_pMarkers`: a zeroed table of
	// `m_iMarkersAllocated` records of stride `0x1c`, whose first dword is the occupant handle.
	// Over 20 of them makes retail `DevMsg` "Warning: Possible speed issues w...".
	int32 MarkersAllocated = 0;
	struct FMarker // 0x102da860, stride0x1c: EHANDLE + two absolute POSITION bounds
	{
		FElysiumEntityHandle Occupant;
		FVector MinBoundsCm = FVector::ZeroVector, MaxBoundsCm = FVector::ZeroVector;
	};
	int32 MarkersUsed = 0; // +0x588, 0x102da860
	int32 FailedAttempts = 0; // +0x58c INT SAVE, 0x102da0d0
	uint64 ReservationsObserved = 0, ReleasesObserved = 0; // diagnostic actual 0x102da860/0x102da600 writes, never SAVE
	int32 InUse = 0; // +0x564, unsaved constructor word, 0x102da7c0
	FVector MinBoundsUnits = FVector::ZeroVector; // +0x54c VECTOR SAVE KEY min_bounds
	FVector MaxBoundsUnits = FVector::ZeroVector; // +0x558 VECTOR SAVE KEY max_bounds
	struct FFailedBox // 0x102d9ed0, unsaved four-slot ring
	{
		double Until = 0.0;
		FVector MinBoundsCm = FVector::ZeroVector, MaxBoundsCm = FVector::ZeroVector;
	};
	FFailedBox FailedBoxes[4];
	int32 FailedBoxCursor = 0; // +0x604, wraps on next write when >3
	bool PickSpotFor(class FElysiumNpc& Npc, FVector& OutPositionUnits, bool bKeepZ = true); // 0x102da0d0
	FElysiumEntityHandle MarkerOccupant(int32 RowIndex) const; // 0x102da9a0 raw row reader
	FElysiumEntityHandle ConversationTalkerSource() const; // 0x102db760 -> 0x102dcc20, unavailable 0018/18
	bool HasMarker(const FElysiumEntityHandle& Npc) const; // 0x10299a80
	virtual void Serialize(FElysiumSaveArchive& Ar) override; // 0x102d9240/0x102d9320
	virtual void RebaseSavedReferences(FElysiumEntityWorld& InWorld) override; // 0x101a2e40
	virtual void OnPostRestore(FElysiumEntityWorld& InWorld) override; // 0x102d9c20 restore half
	TArray<FMarker> Markers;
	static constexpr int32 MarkerSpeedWarningThreshold = 0x14;

	// `CAI_InterestingPlace::Spawn` (`0x102d9c20`), slot 103.
	virtual void Spawn() override;

	// The half `Spawn` runs (retail's `OnRestore` ran it too): resolve `m_sType`
	// against the type table when it is non-empty, and answer false when the lookup misses (the arm
	// that removes the entity). An EMPTY `m_sType` never looks up and never removes — retail's own first test.
	bool ResolveTypeOrRemove(bool bWarn);

	bool IsAvailable() const;
	bool Claim(const FElysiumEntityHandle& Npc, bool bFireArrived = false); // 0x102da7c0 output before in-use write
	void Release(const FElysiumEntityHandle& Npc, bool bFireLeft = false); // 0x102da600 full-row swap removal
	bool IsEnabledFor(const FElysiumEntityHandle& Npc) const;
	void Arrived(const FElysiumEntityHandle& Npc);
	void Left(const FElysiumEntityHandle& Npc);
	void InputEnable(const FElysiumInputArgs&) { bEnabled = true; }
	void InputDisable(const FElysiumInputArgs&) { bEnabled = false; }
	void InputToggle(const FElysiumInputArgs&) { bEnabled = !bEnabled; }

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

private:
	FVector SampleSpot(const FVector& HullMinUnits, const FVector& HullMaxUnits, bool bKeepZ) const; // 0x102d9fa0
	bool OverlapsMarker(const FVector& PositionUnits, const FVector& HullMinUnits, const FVector& HullMaxUnits) const; // 0x102da9e0
};

namespace ElysiumInterestingPlaces
{
	// The one-shot `interestingplacetypelist.txt` load, kept beside the entity whose `type` keys
	// into it. Null when the table did not load; the failure is warned once at the first call.
	const FElysiumInterestingPlaceTable* Types();
}
