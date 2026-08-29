#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"
#include "Substrate/ElysiumTriggerBase.h"

// `trigger_stealth_mod` (`docs/vtmb/stealth.md` -> "`trigger_stealth_mod`").
//
// A `CBaseTrigger` leaf whose whole specialization is two lines: the authored integer
// `stealth_modifier` is ADDED to the toucher's raw aggregate on begin and SUBTRACTED on end. That
// is the balanced overlap contribution — nothing here hides the player, fires an output of its own,
// or reads the base filter's verdict.
//
// Three properties the recovered body has and this one keeps:
//
//   * the specialized half runs AFTER the base callback and does NOT inspect the base filter
//     result. Spawnflags and `filtername` govern the base trigger's own admission and output work;
//     the modifier body is guarded by combat-character embodiment alone;
//   * the stored sum is NEVER clamped. Only `GetStealthModifier` clamps, at the read. Overlapping
//     volumes add, and leaving one subtracts its own contribution, which only restores the correct
//     remainder while the storage is raw;
//   * one begin/end pair per contact. The lease below is what enforces that on our side.

class FElysiumStealthModTrigger final : public FElysiumTriggerBase
{
public:
	// `stealth_modifier` -> `m_nStealthMod` (+0x598). Integer, and signed: nothing in the parser or
	// in the aggregate rejects a negative volume.
	int32 StealthMod = 0;

	// The PHYSICAL admission. `FElysiumTriggerBase::CanBeginTouch` is this runtime's placement of
	// retail's `PassesTriggerFilters`, and it gates on the ALLOW_CLIENTS spawnflag and `filtername`
	// — which is the base trigger's OUTPUT policy. The recovered modifier body does not inspect that
	// verdict, so the contact has to reach this leaf even when the base would refuse it; the base's
	// own `OnTouchStart` below still applies the filter to its own half of the work.
	virtual bool CanBeginTouch(const FElysiumEntityHandle& Activator) const override;

	virtual void OnTouchStart(const FElysiumEntityHandle& Activator) override;
	virtual void OnTouchEnd(const FElysiumEntityHandle& Activator) override;
	virtual void Serialize(FElysiumSaveArchive& Ar) override;

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

	int32 NumLeases() const { return Leases.Num(); }

private:
	// The overlap lease, keyed by contact. The underlying touch-link system supplies one begin/end
	// pair per contact, and this holds the runtime to it: a repeated begin for a contact already
	// credited adds nothing, and an end for a contact never credited subtracts nothing. Without it
	// a refreshed brush body (`SetDisabled` re-links overlaps) could credit the same character
	// twice and leave a permanent contribution behind.
	//
	// SEAM: the ABNORMAL teardown edge — this trigger killed, or its body disabled, while a
	// character is still inside — has no hook to hang a release on. `FElysiumEntity::Kill` is not
	// virtual and the world's reap raises no per-entity callback, so whether the overlap system
	// delivers a final end in every one of those cases is exactly the live acceptance
	// `docs/vtmb/stealth.md` flags as open. The lease is the place a tested policy attaches without
	// the aggregate contract changing.
	TArray<FElysiumEntityHandle> Leases;
};
