#pragma once

#include "Substrate/ElysiumNpcHumanCombatant.h"

// `CNPC_VCop` (primary vtable `0x104b09f4`), built by `npc_VCop` factory `0x103704f0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcCop : public FElysiumNpcHumanCombatant
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VCop", FElysiumNpcHumanCombatant)

	virtual void Slot597(FElysiumEntity* Other, int32 Priority) override;
	virtual void NPCInit() override;
	virtual void UpdateOnRemove() override;
	virtual void OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual void OnSeeEntity(FElysiumEntity* Seen) override;
	virtual int32 IRelationType(FElysiumEntity* Candidate) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual void DrawDebugGeometryOverlays() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	/** `CNPC_VCop::m_hPursuitPlayer` (`+0x6664`), the ` Pursuit` suffix of the cop's relationship label.
	 *  **SEAM**: no port word; answers null. */
	FElysiumEntity* CopPursuitPlayer() const;
	/** The cop class's two STATIC globals — `DAT_1093ac3c`, the provoker handle every cop shares, and
	 *  `_DAT_1093aca8`, the curtime it expires at. `CNPC_VCop::OnSeeEntity` (`0x10370560`, family
	 *  Senses10) is the writer and both this body and `CNPC_VCop::IRelationType` (family Conditions10)
	 *  are the readers. **SEAM**: neither global is stood here; answers false, which drops the
	 *  ` Suspect` suffix. */
	bool CopSuspectIs(const FElysiumEntity* Candidate) const;

	/** `+0x6671 CNPC_VCop::m_bCountedAlive` and `+0x6672`, its unnamed twin. The census row names only
	 *  the first; the second is read and written by the same body at the same width and has no
	 *  recovered name, so it is spelled by offset — the convention 29b uses for an unsettled word.
	 *  `+0x6671` is `CNPC_VTzimisceRunner::m_bDeathNoticeProcessed` on another class, which family
	 *  Species already declares, so these are declared by class here. */
	bool bCopCountedAlive = false;      // +0x6671 CNPC_VCop
	bool bCopCountedSecond = false;     // +0x6672 CNPC_VCop
	/** `DAT_1093acac` and `DAT_1093acb0`, the two PROCESS-WIDE live-cop censuses `CNPC_VCop` keeps.
	 *  Statics and not per-NPC words, because retail's are: `CNPC_VCop::Spawn` increments the first for
	 *  every cop in the level and `SelectSchedule` reads the total.
	 *
	 *  Ported as file statics, the way family Lifecycle ported the Werewolf's shared `rdtsc` pair, with
	 *  named accessors so a fixture can read and reset them. **They answer nothing today in the sense
	 *  that nothing INCREMENTS them** — `CNPC_VCop::Spawn` and `OnStateChange` are other stories' rows
	 *  — so the decrement this family ports is the only writer so far. That is a missing producer, not
	 *  a missing rule, and the guard (`only when the byte is set`) is what keeps the count from going
	 *  negative meanwhile. */
	static int32& CopAliveCensus();    // DAT_1093acac
	static int32& CopSecondCensus();   // DAT_1093acb0

	static FElysiumEntityHandle CopSuspectHandle();
	static double CopSuspectExpiry();
	/** `0x10370560` and `0x10387fd0` themselves — the two stamps, so a test can drive them directly.
	 *  Both are reset by `ResetSpeciesSuspectGlobals`, which exists because a process-lifetime static
	 *  outlives a headless world and retail's own lifetime is the process too. */
	void StampCopSuspect(FElysiumEntity* Seen);

	// From `ElysiumNpcSpeciesMisc10.inl`.
	/** `CNPC_VCop::vfunc597` (`0x10372cc0`), slot 597's species arm: ADDS the `m_hPursuitPlayer` latch
	 *  and the `"Player D_HT 10"` relationship write in front of the Troika base `0x102b4fb0`, which
	 *  then runs unchanged. Called from `FElysiumNpc::Slot597`.
	 *
	 *  CORRECTION to the walk: `argument+0xa8` is `m_pPlayer`, the player self-downcast cache, not a
	 *  "troika sub-object" — so the latch stores the PLAYER's own handle. */
	void CopSlot597Prologue(FElysiumEntity* Other);
	/** `+0x6664 CNPC_VCop::m_hPursuitPlayer`. Family **Debug10** declared the READER (`CopPursuitPlayer`)
	 *  as a seam answering null; this is the word its writer needs, so that seam now resolves it. */
	FElysiumEntityHandle CopPursuitHandle;

	// From `ElysiumNpcState19.inl`.
	/** `CNPC_VCop::m_bWasEverInCombat` (`+0x6670`). */
	bool bWasEverInCombat = false;
	void CopOnStateChange(int32 OldRetail, int32 NewRetail);

	// --- 0019/8 Spawn19 (lane L08): words and helpers the family's bodies need (three searches each
	// in the L08 report) ---
	int32 CopOldPlayerRelationType = 0;     // +0x6668 CNPC_VCop::m_eOldPlayerRelationType (datamap)

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual void Slot596(FElysiumEntity* Arg0) override;
	virtual void Slot598(FElysiumEntity* Arg0) override;
};
