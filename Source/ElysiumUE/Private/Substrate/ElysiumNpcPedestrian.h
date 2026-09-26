#pragma once

#include "Substrate/ElysiumNpcHuman.h"

// `CNPC_VPedestrian` (primary vtable `0x104c0dc4`), built by `npc_VDialogPedestrian` factory
// `0x103a1d20`; `npc_VPedestrian` factory `0x103a1cb0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcPedestrian : public FElysiumNpcHuman
{
public:
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_VPedestrian");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void OnRestore(bool bFromLoad) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual void BuildScheduleTestBits(FElysiumNpcConditions& InOutMask) override;
	virtual int32 IRelationType(FElysiumEntity* Candidate) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcLifecycle19.inl`.
	/** `CNPC_VPedestrian::m_bFirstThink` (`+0x6678`) and `CNPC_VTaxiDriver::m_bFirstThink` (`+0x6660`).
	 *  Different offsets, two members. */
	bool bPedestrianFirstThink = false;
	/** `CNPC_VPedestrian::m_eLevelResetType` (`+0x667c`). */
	int32 PedestrianLevelResetType = 0;
	/** SEAM for `CBaseEntity::Relink` (`0x1001514a` through `0x101cf600`), which `CNPC_VPedestrian`'s
	 *  level reset runs after it resizes the hull. No spatial partition stands at this tier; counted. */
	int32 RestoreRelinkCalls = 0;

	// From `ElysiumNpcSensesBodies.inl`.
	static uint32 DecodeWitnessedLevel(uint32 Stored);

	// From `ElysiumNpcSpeciesMisc10.inl`.
	/** `CNPC_VPedestrian::CreateCorpse` (`0x103a38c0`), slot 301's species body. BEFORE the base it
	 *  snapshots the collision OBB — `m_Collision` vtable `+4` into `m_vecPreDeathMins` (`+0x6660`) and
	 *  `+8` into `m_vecPreDeathMaxs` (`+0x666c`) — because the base resizes the hull; then
	 *  `CBaseCombatCharacter::CreateCorpse`, `ThinkSet(NULL, 0.0, NULL)` and `SetSolid(SOLID_NONE)`.
	 *
	 *  **NOT WIRED TO SLOT 301.** Slot 301's Troika-line body (`0x1032c0e0`,
	 *  `CBaseCombatCharacter::CreateCorpse`, layer 14) carries no verdict yet, so `gen_kernel_shape`
	 *  still emits its stub and there is no port method to hang the species case on. The body lands
	 *  under its recovered name and the gap is named in the story's answer. */
	void PedestrianCreateCorpse();
	FVector PedestrianPreDeathMinsUnits = FVector::ZeroVector;   // +0x6660 `m_vecPreDeathMins`
	FVector PedestrianPreDeathMaxsUnits = FVector::ZeroVector;   // +0x666c `m_vecPreDeathMaxs`
	/** SEAM for `CBaseCombatCharacter::CreateCorpse` (`0x1032c0e0`, slot 301's Troika-line body): this
	 *  substrate stands no corpse entity at the kernel tier and that row carries no verdict, so the
	 *  chain call is counted. `ThinkSet(NULL, 0.0, NULL)` and `SetSolid(SOLID_NONE)` are recorded beside
	 *  it for the same reason — neither has a port word, and the ORDER around the base is the load-bearing
	 *  half of this body. */
	int32 PedestrianCreateCorpseCalls = 0;
	bool bPedestrianCorpseThinkStopped = false;
	int32 PedestrianCorpseSolid = -1;

};
