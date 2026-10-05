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
	ELYSIUM_NPC_CLASS("CNPC_VPedestrian", FElysiumNpcHuman)

	virtual void NPCInit() override;
	virtual void OnRestore(bool bFromLoad) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual void BuildScheduleTestBits(FElysiumNpcConditions& InOutMask) override;
	virtual int32 IRelationType(FElysiumEntity* Candidate) override;
	// Slot 550 `0x103a1de0` (`CoverRadius`): `FLD [0x104563b0]` = 4096.0, the cover and tactical-hint
	// search radius; the base line answers 1024.0.
	virtual float CoverRadius() override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcLifecycle2.inl`.
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
	 *  Slot 301 dispatches this wrapper around the complete base corpse transaction. */
	virtual void CreateCorpse(const FVector& Force, void* InInfo) override;
	void PedestrianCreateCorpse(const FVector& Force = FVector::ZeroVector, void* InInfo = nullptr);
	FVector PedestrianPreDeathMinsUnits = FVector::ZeroVector;   // +0x6660 `m_vecPreDeathMins`
	FVector PedestrianPreDeathMaxsUnits = FVector::ZeroVector;   // +0x666c `m_vecPreDeathMaxs`
	/** 0x103a38c0 diagnostics beside the real base/clear/solid writes. */
	int32 PedestrianCreateCorpseCalls = 0;
	bool bPedestrianCorpseThinkStopped = false;
	int32 PedestrianCorpseSolid = -1;

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Slot27(FElysiumEntity* Arg0) override;
	virtual void Spawn() override;
	virtual void RunAI(bool Arg0) override;
	virtual void GatherConditions() override;
	virtual int32 SpeciesSelectSchedule() override;
};
