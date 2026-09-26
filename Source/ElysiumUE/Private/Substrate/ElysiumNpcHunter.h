#pragma once

#include "Substrate/ElysiumNpcHumanCombatant.h"

// `CNPC_VHunter` (primary vtable `0x104b9784`), built by `npc_VHunter` factory `0x10387f60`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcHunter : public FElysiumNpcHumanCombatant
{
public:
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_VHunter");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual void OnSeeEntity(FElysiumEntity* Seen) override;
	virtual int32 IRelationType(FElysiumEntity* Candidate) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	static FElysiumEntityHandle HunterSuspectHandle();
	static double HunterSuspectExpiry();
	void StampHunterSuspect(FElysiumEntity* Seen);

	// From `ElysiumNpcKernelSpeciesLifecycle10.inl`.
	/** `CNPC_VHunter::m_hPursuitPlayer` (`+0x6664`, datamap — `ElysiumNpcKernelShape.cpp`). Distinct
	 *  from `CNPC_VCop`'s word at the same offset, which family Debug10 reaches through
	 *  `CopPursuitPlayer()`; the same offset means a different thing per species, which is the rule 29b
	 *  recorded for everything above `+0x665c`. */
	FElysiumEntityHandle HunterPursuitPlayer;
	/** How many times the acquire and release arms ran. The COUNT itself is the player's — retail's
	 *  `+0x1d14` is a word on `CBasePlayer` and this runtime carries it as
	 *  `FElysiumPoliceState::HuntersInPursuit`. `FUN_1017f7b0` (`0x1017f7b0`) and `FUN_1017f830`
	 *  (`0x1017f830`) are already ported by family **Conditions** as the static
	 *  `FElysiumNpc::OnHunterPursuitStart(FElysiumPlayer&)` / `OnHunterPursuitStop(FElysiumPlayer&)`
	 *  pair, with that family's own receiver correction; this family CALLS them. These two are the
	 *  Hunter arm's own tally, so a case can say which arm fired without reading the shared counter. */
	int32 HunterPursuitStarts = 0;
	int32 HunterPursuitStops = 0;
	/** `FUN_10388c40` (`0x10388c40`), the Hunter's relationship write. Its whole body is
	 *  `InputSetRelationship(this, "player D_HT 10", 0)` — the literal at `0x1063bc28`, `'player D_HT
	 *  10'` with SPACES, i.e. the three-token grammar this runtime's `InputSetRelationship` already
	 *  parses.
	 *
	 *  It is `CNPC_VGuard1`'s `0x1037e2d0` MINUS the `+0x6660` latch byte, and that byte is the only
	 *  difference between the two 20-byte bodies. `0x1037e2d0` is family **SpeciesMisc10**'s
	 *  `Guard1HatePlayer()` (it has six other callers in `vfunc461`), so the Guard1 arm calls that and
	 *  this stands only for the Hunter's. */
	void HunterHatePlayer();
	/** `'player D_HT 10'` (`0x1063bc28`). */
	static const TCHAR* PlayerHateRelationshipSpec();

};
