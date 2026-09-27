#pragma once

#include "Substrate/ElysiumNpcHumanCombatant.h"

// `CNPC_VGhoulCroucher` (primary vtable `0x104b50ac`), built by `npc_VGhoulCroucher` factory
// `0x1037a670`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcGhoulCroucher : public FElysiumNpcHumanCombatant
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VGhoulCroucher", FElysiumNpcHumanCombatant)

	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual void SetModel(TCHAR* ModelName) override;
	virtual void OnVictimHitByMe(FElysiumEntity* Victim) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual bool CanBeSetOnFire() override;
	virtual void StartTouchSpecies(FElysiumEntity* Other) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcConditionsBodies.inl`.
	bool bWasDisturbed = false;    // +0x6666 CNPC_VGhoulCroucher::m_bWasDisturbed (datamap)
	bool bUnawareExited = false;   // +0x6667 CNPC_VGhoulCroucher::m_bUnawareExited (datamap)
	/** `CNPC_VGhoulCroucher::IsDisturbed` (`0x1037bb20`) — `return m_bWasDisturbed`, and nothing else.
	 *  The producer `ElysiumNpc.cpp`'s stealth-kill gate names as absent. */
	bool IsDisturbed() const;
	/** `CNPC_VGhoulCroucher::OnDisturbed` (`0x1037b6e0`) — the once-latch: on the FIRST disturbance it
	 *  latches `m_bWasDisturbed`, clears `m_bUnawareExited`, and then splits on whether the disturber is
	 *  a player. `Disturber` is retail's `param_1`, the entity that did it; null is allowed and takes
	 *  the non-player arm. */
	void OnDisturbed(FElysiumEntity* Disturber);

	// From `ElysiumNpcDamage.inl`.
	// `CNPC_VGhoulCroucher`'s authored `on_fire` keyfield, the one word its `CanBeSetOnFire` reads.
	bool bGhoulSpawnBurning = false;             // +0x6665 m_bSpawnBurning (datamap, KEY on_fire)

	// From `ElysiumNpcLifecycle.inl`.
	/** `+0x6670 CNPC_VGhoulCroucher::m_hBurningParticle` — the particle entity its `ScriptUnhide`
	 *  (`0x1037c2f0`) kills on the way back up. */
	FElysiumEntityHandle BurningParticle;
	/** `+0x6668 CNPC_VGhoulCroucher::m_nUnawareType`, the index `UnawareTableA`/`UnawareTableB`
	 *  (`0x1037b870` / `0x1037b890`) read the two static tables with. */
	int32 UnawareType = 0;
	/** `CNPC_VGhoulCroucher::ScriptUnhide` (`0x1037c2f0`) — the base, then resolve
	 *  `m_hBurningParticle` and, when it resolves to a live entity, dispatch its vtable `+0x138`
	 *  (slot 78, the particle's own `ScriptUnhide`). Retail does NOT clear the handle. */
	void GhoulCroucherScriptUnhideTail();
	/** `CNPC_VGhoulCroucher`'s two static tables, indexed by `m_nUnawareType` (`+0x6668`):
	 *  `DAT_1063abcc` (`0x1037b870`) and `DAT_1063abdc` (`0x1037b890`). **Unrecovered**: the tables'
	 *  purpose — message, sound or activity selection — is not settled and neither table's contents are
	 *  read anywhere the corpus pins. The INDEXING is the whole recovered body and is what lands. */
	int32 UnawareTableA() const;
	int32 UnawareTableB() const;
	/** SEAM for the two tables above. Both answer 0 and name their retail global; the day either is
	 *  decoded, the row lands here and both readers come right. */
	static int32 UnawareTableEntry(const TCHAR* RetailTable, int32 Index);

	// From `ElysiumNpcLifecycle19.inl`.
	/** `CNPC_VGhoulCroucher::m_bSpawnDisturbed` (`+0x6664`). `bGhoulSpawnBurning` is Damage's. */
	bool bGhoulSpawnDisturbed = false;
	int32 GhoulBurningParticleCreates = 0;

	/** `m_flNextTouchBurnTime` (`+0x666c`, `CNPC_VGhoulCroucher`) — the touch-burn re-arm stamp. */
	double GhoulNextTouchBurnTime = 0.0;
	/** `_DAT_1044ffd0` — **5.0**, a DOUBLE, read at file offset `0x44ffd0`. */
	static constexpr double GhoulTouchBurnIntervalSeconds = 5.0;
	/** `1037c028 PUSH 0x40a00000` — the touch burn's damage, **5.0**, against the **10.0**
	 *  (`0x41200000`) this class's own `OnVictimHitByMe` (`0x1037be80`) passes to the same body. */
	static constexpr float GhoulTouchBurnDamage = 5.0f;

	// From `ElysiumNpcSpeciesMisc10.inl`.
	/** `CNPC_VGhoulCroucher::BurnPlayer` (`0x1037c090`). With a non-null target: walk every hitbox of
	 *  its model calling `CBaseCombatCharacter::BurnHitbox(target, index, 5.0, 0.5)`, then build a
	 *  `CTakeDamageInfo(inflictor = my active weapon, attacker = this, damage, bits = 8)` and
	 *  `TakeDamage` it. The caller (`0x1037be80`) passes **10.0**. */
	void BurnPlayer(FElysiumEntity* Target, float Damage);
	TArray<FBurnHitboxCall> BurnHitboxCalls;

	// --- 0019/8 Spawn19 (lane L08): words and helpers the family's bodies need (three searches each
	// in the L08 report) ---
	/** Slot 77 `0x1037c1c0` `CNPC_VGhoulCroucher::ScriptHide`. `FElysiumEntity::ScriptHide` is NOT
	 *  virtual, so this HIDES rather than overrides; the integrator makes the base `virtual` (retail
	 *  dispatches slot 77 through `+0x134`: the Troika tail `0x102c1e48`, and this body's own `0x1037c286`
	 *  on the particle). Until then only a caller holding the species type reaches it. */
	void ScriptHide();

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual bool EnterGrappleState(const FElysiumEntityHandle& Partner, EElysiumGrappleRole Role, EElysiumGrappleType Type, int32 Position = INDEX_NONE, bool bHolster = true) override;
	virtual int32 OnTakeDamage_Alive(void* Arg0) override;
	virtual void NPCThink() override;
	virtual void GatherConditions() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;
};
