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
	ELYSIUM_NPC_CLASS("CNPC_VAnimal", FElysiumNpc)

	// `+0xac m_pAnimal`: the self-cache `CNPC_VAnimal`'s constructor (0x1035eba0) sets.
	virtual FElysiumNpcAnimal* AsAnimal() override { return this; }

	virtual int32 CanPlaySequence(bool bDisregardState, int32 InterruptLevel) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual void TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm, void* Tolerance, void* SecondTolerance) override;

	// Own datamap words (`CNPC_VAnimal`). The three keys a map authors on every animal leaf; no
	// retail constructor writes them, so an unauthored key reads zero. Their reader,
	// `CNPC_VDog::GatherConditions` (`0x10374b00`), is story-8 residue.
	int32 AnimalFriendshipLevel = 0;      // +0x6664 m_iFriendshipLevel, KEY friendship_level
	float AnimalWarnRangeUnits = 0.f;     // +0x6668 m_flWarnRange, KEY warn_range (SOURCE units)
	float AnimalConflictRangeUnits = 0.f; // +0x666c m_flConflictRange, KEY conflict_range

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcLifecycle2.inl`.
	/** Ledger `CNPC_VAnimal::m_bPlayerAttackedMe` at `+0x6660`. TaxiDriver's `bTaxiFirstThink` and
	 *  Pedestrian's pre-death bounds are different classes at the same offset. */
	bool bPlayerAttackedMe = false;

	// --- 0019/8 lane L07, Conditions19 ---------------------------------------------------------
	/** `+0x665c CNPC_VAnimal::m_iPlayerFriendshipState` (datamap): 0 unknown, 1 wary, 2 befriended.
	 *  `CNPC_VDog::GatherConditions` (`0x10374b00`) is its writer. */
	int32 AnimalPlayerFriendshipState = 0;

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual int32 OnTakeDamage_Alive(void* Arg0) override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;
};
