#pragma once

#include "Substrate/ElysiumNpc.h"

// `CNPC_VAnimal` (primary vtable `0x104a876c`), built by `npc_VAnimal` factory `0x1035eaa0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcAnimal : public FElysiumNpc
{
public:
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_VAnimal");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual int32 CanPlaySequence(bool bDisregardState, int32 InterruptLevel) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual void TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm, void* Tolerance, void* SecondTolerance) override;

	// Own datamap words (`CNPC_VAnimal`). The three keys a map authors on every animal leaf; no
	// retail constructor writes them, so an unauthored key reads zero. Their reader,
	// `CNPC_VDog::GatherConditions` (`0x10374b00`), is story-8 residue.
	int32 AnimalFriendshipLevel = 0;      // +0x6664 m_iFriendshipLevel, KEY friendship_level
	float AnimalWarnRangeUnits = 0.f;     // +0x6668 m_flWarnRange, KEY warn_range (SOURCE units)
	float AnimalConflictRangeUnits = 0.f; // +0x666c m_flConflictRange, KEY conflict_range

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcLifecycle19.inl`.
	/** Ledger `CNPC_VAnimal::m_bPlayerAttackedMe` at `+0x6660`. TaxiDriver's `bTaxiFirstThink` and
	 *  Pedestrian's pre-death bounds are different classes at the same offset. */
	bool bPlayerAttackedMe = false;

};
