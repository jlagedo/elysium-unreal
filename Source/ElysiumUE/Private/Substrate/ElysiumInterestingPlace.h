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
	int32 MaxNpcs = 1;
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

	// +0x0578 `m_iMarkersAllocated` and +0x0580 `m_pMarkers`: a zeroed table of
	// `m_iMarkersAllocated` records of stride `0x1c`, whose first dword is the occupant handle.
	// Over 20 of them makes retail `DevMsg` "Warning: Possible speed issues w...".
	int32 MarkersAllocated = 0;
	struct FMarker { FElysiumEntityHandle Occupant; };
	TArray<FMarker> Markers;
	static constexpr int32 MarkerSpeedWarningThreshold = 0x14;

	// `CAI_InterestingPlace::Spawn` (`0x102d9c20`), slot 103.
	virtual void Spawn() override;

	// `CAI_InterestingPlace::OnRestore` (`0x102d9dd0`), slot 130 — the `CAISound` restore hook and
	// then the SAME type lookup and self-destruct-on-miss `Spawn` runs, at `DevMsg` rather than
	// `Warning` severity. Returns whether the place survived the restore.
	bool OnRestoreResolveType();

	// The half `Spawn` and `OnRestoreResolveType` share: resolve `m_sType` against the type table
	// when it is non-empty, and answer false when the lookup misses (the arm that removes the
	// entity). An EMPTY `m_sType` never looks up and never removes — retail's own first test.
	bool ResolveTypeOrRemove(bool bWarn);

	// `CAI_InterestingPlaceConverstation::vfunc5` (`0x102dbbc0`). **29c read this as "best guess: the
	// class constructor ... conditionally hides on bit 0 of param_1". It is not.** It is slot 5, the
	// LIFETIME slot, and its body is MSVC's scalar deleting destructor verbatim: destroy the two
	// sub-objects, destroy the six `COutput`s, chain the base destructor, and then
	// `if (flags & 1) operator delete(this)` — bit 0 is the *free the memory* flag, not a hide.
	// Answers whether the storage is freed. The six output names are below.
	static bool ConversationPlaceDeletingDtor(uint8 DeleteFlags);
	static TConstArrayView<const TCHAR*> ConversationPlaceOutputNames();

	bool IsAvailable() const;
	bool Claim(const FElysiumEntityHandle& Npc);
	void Release(const FElysiumEntityHandle& Npc) { Claimants.Remove(Npc.Index); }
	bool IsEnabledFor(const FElysiumEntityHandle& Npc) const;
	void Arrived(const FElysiumEntityHandle& Npc);
	void Left(const FElysiumEntityHandle& Npc);
	void InputEnable(const FElysiumInputArgs&) { bEnabled = true; }
	void InputDisable(const FElysiumInputArgs&) { bEnabled = false; }
	void InputToggle(const FElysiumInputArgs&) { bEnabled = !bEnabled; }

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

private:
	TSet<int32> Claimants;
};

namespace ElysiumInterestingPlaces
{
	// The one-shot `interestingplacetypelist.txt` load, kept beside the entity whose `type` keys
	// into it. Null when the table did not load; the failure is warned once at the first call.
	const FElysiumInterestingPlaceTable* Types();
}
