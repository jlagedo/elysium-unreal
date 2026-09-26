#pragma once

#include "Substrate/ElysiumNpcHuman.h"

// `CNPC_VGuard1` (primary vtable `0x104b5c74`), built by `npc_VGuard1` factory `0x1037c5b0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcGuard1 : public FElysiumNpcHuman
{
public:
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_VGuard1");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual void BuildScheduleTestBits(FElysiumNpcConditions& InOutMask) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcSchedule.inl`.
	/** The species halves of slot 453 `BuildScheduleTestBits`, each the body of its class's override
	 *  (story 5 step 3). `CNPC_VGuard1` (`0x1037cdf0`) REPLACES the Troika body (it calls the empty
	 *  `CAI_BaseNPC` base); the other three call `0x102ad140` first and add their own bits. */
	void Guard1BuildScheduleTestBits(FElysiumNpcConditions& InOutMask);

	// From `ElysiumNpcSpeciesLifecycle10.inl`.
	/** `CNPC_VGuard1::OnStateChange` (`0x1037d020`)'s pre-step, UNCONDITIONALLY and with no reference
	 *  to either state: `if (GetEnemy() && GetEnemy()->m_pPlayer) 0x1037e2d0(this)`. `GetEnemy()` is
	 *  dispatched TWICE (`1037d026`, `1037d034`) and retail does not cache it; both calls are made here
	 *  because a species override of slot 167 could answer differently between them. */
	void Guard1StateChangePreStep();

	// From `ElysiumNpcSpeciesMisc10.inl`.
	/** `CNPC_VGuard1`'s hate latch (`0x1037e2d0`), reached from `OnStateChange` (`0x1037d020`) and six
	 *  times from `vfunc461` (`0x1037d290`): set `+0x6660` and call `InputSetRelationship` with the
	 *  literal `"player D_HT 10"` at priority 0 — lower case `player`, unlike the cop's `"Player"`. */
	void Guard1HatePlayer();
	bool bGuard1HatesPlayer = false;   // +0x6660 CNPC_VGuard1 (walked; the retail name is unrecovered)

};
