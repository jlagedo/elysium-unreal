#pragma once

#include "Substrate/ElysiumNpcAnimal.h"

// `CNPC_VZombie` (primary vtable `0x104d1d3c`), built by `npc_VZombie` factory `0x103ddd70`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcZombie : public FElysiumNpcAnimal
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VZombie", FElysiumNpcAnimal)

	// --- Select19 (story 0019/8 lane L06): the words `SelectSchedule` `0x103df2e0` passes ----------
	/** `+0x6680 m_vecDeathForceVector` (datamap) and `+0x668c m_DeathDamageInfo` (walked), the two
	 *  arguments of the dead-zombie `CreateCorpse` call; both ABSENT in the shape map until now, and
	 *  no ported body writes them yet (the death-force bodies are story-8 residue). */
	FVector ZombieDeathForceVector = FVector::ZeroVector;
	FElysiumDmg ZombieDeathDamageInfo;

	virtual void Slot25(FElysiumEntity* Victim) override;
	virtual void Slot26(FElysiumEntity* Victim) override;
	virtual bool ShouldPlayFloatSound() override;
	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual void SetModel(TCHAR* ModelName) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual bool FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual int32 DrawDebugTextOverlays() override;
	virtual void OnVictimHitByMe(FElysiumEntity* Victim) override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual bool ShouldPlayIdleSound() override;
	virtual void TraceAttack(void* InInfo, const FVector& DirUnits, void* InTrace) override;

	// Own datamap words (`CNPC_VZombie`), the two keys `npc_maker_zombie` also carries on its own
	// class. Their readers (`CreateCorpse` `0x103dfbb0`, `OnTakeDamage` `0x103e06d0`, slot 432
	// `0x103df850`) are story-8 residue.
	bool bZombieShouldRagdoll = false;   // +0x6675 m_bShouldRagdoll, KEY should_ragdoll
	float ZombieRemoveDistUnits = 0.f;   // +0x66dc m_flRemoveDist, KEY remove_distance

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcDamage.inl`.
	// `CNPC_VZombie`'s head-hit byte, `+0x66e1` (walked; no datamap row, unsaved as retail).
	// `TraceAttack` `0x103e0430` raises it on a hitgroup-1 hit and clears it otherwise, and
	// `OnTakeDamage` `0x103e06d0` reads it (`103e0883`) to choose `zombie_headshot_dmg_emitter` or
	// `zombie_headshot_death_emitter`. It is NOT `m_bShouldGib` (`+0x66e0`), a separate word:
	// `OnTakeDamage` sets that one on an over-threshold hit (`103e0801`) and `CreateCorpse`
	// `0x103dfbb0` reads it. Both readers are story-8 residue (story 5 step 4r: this byte was
	// misnamed as the gib latch).
	bool bZombieHeadHit = false;                 // +0x66e1 (walked)
	/** `0x103e0430` — `CNPC_VZombie`'s prologue as a pure rule, so both arms are measurable. The
	 *  head-hit byte is raised when the hitgroup is 1 (a head hit) and cleared otherwise; a non-head hit only
	 *  forces an ammo type when the attacker's active weapon's capability mask intersects `0x18000`
	 *  (the melee-block capability), and the type forced is the SECOND cvar for a head hit and the
	 *  FIRST for a qualifying melee one. Answers whether an ammo type is forced. */
	static bool ZombieTraceAttackPrologue(int32 HitGroup, bool bAttackerWeaponIsMelee,
		int32 FirstCvarAmmoType, int32 SecondCvarAmmoType, bool& OutHeadHit, int32& OutAmmoType);
	/** SEAM for `DAT_10940404` and `DAT_1094044c` — the two cvar-backed ammo-type cells
	 *  `CNPC_VZombie::TraceAttack` reads. Both pointer cells live past `.data`'s raw size and no corpus
	 *  function constructs them: **unrecovered**, and both answer 0, which is an unconstructed cvar's
	 *  own answer. */
	int32 ZombieGibAmmoTypeCvar(int32 Which) const;

	/** `CNPC_VZombie`'s own condition bitfield at `+0x5c5c`, walked 0..0xbf. **SEAM**: `+0x5c5c` is one
	 *  of the schedule block's six words with no port member of its own; answers false for every id. */
	bool ZombieConditionBit(int32 ConditionId) const;

	// From `ElysiumNpcLifecycle19.inl`.
	static constexpr int32 ZombieCrawlScheduleRetailId = 0x161;
	/** `CNPC_VZombie`. */
	bool bZombieNeedsCrawlOutOfGround = false;   // +0x667c
	double ZombieGrappleReadyTimer = 0.0;        // +0x66d8

	/** `0x103e12c0` / `0x103e12f0` — `CNPC_VZombie`'s slots 25 and 26, both firing
	 *  `m_OnAttackedVictim` (`+0x66e8`) with no base forward. */
	void FUN_103e12c0(FElysiumEntity* Victim);

	// --- 0019/8 Damage19 (lane L09): `OnTakeDamage` `0x103e06d0`'s word and cvar ------------
	/** `+0x66e0 m_bShouldGib` — raised at `103e0801` when the hit beats the head threshold;
	 *  `CreateCorpse` `0x103dfbb0` reads it. No port word stood for it (`bZombieHeadHit` is +0x66e1). */
	bool bZombieShouldGib = false;
	/** SEAM for the ConVar object at `DAT_1094049c`, read as `cv->vtable[1]() ? 0 : cv->m_nValue`
	 *  (`103e07cd..103e07e5`). The object is unnamed and never constructed by any corpus function
	 *  (`vtmb_globals 1094049c`: two readers, no writer), so its name and default are unrecovered;
	 *  an unconstructed ConVar reads 0, which is the answer. */
	int32 ZombieHeadThresholdCvar() const;

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual int32 OnTakeDamage(void* Arg0) override;
	virtual void CreateCorpse(const FVector& Arg0, void* Arg1) override;
	virtual void NPCThink() override;
	virtual void RunAI(bool Arg0) override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;
	// --- Lane L05 (story 8 RunTask19): `TASK 0x14e` hands `CreateCorpse` L06's two words above
	// (`+0x6680 m_vecDeathForceVector`, `+0x668c m_DeathDamageInfo`); the rebase onto Select19
	// dropped this lane's copies.

	// --- 0019/8 L04 (StartTask19 species): private helpers ---
	/** `+0x66e4`, no datamap name: the feeding variant `StartTask` `0x103dfd80` task `0x153` switches on
	 *  (1 -> `0x1098`, 2 -> `0x109b`, 3 -> `0x109e`). Its writer is unrecovered (`vtmb_readers 0x66e4`
	 *  answers none); 0 keeps `m_Activity`. */
	int32 ZombieFeedVariant = 0;
	/** SEAM for the player's slot 425, `CBasePlayer::BeFedOnByZombie` `0x10168700`, which task `0x152`
	 *  calls with this zombie. No port body starts a mode-8 grapple from the player side; counted and
	 *  answers false (the refusal). */
	bool ZombieBeFedOnByZombie(class FElysiumPlayer& Player);
	int32 ZombieBeFedOnCalls = 0;

	// --- Story 8 lane L13b (Think19): the helper `NPCThink` 0x103dfa20 calls -------------------------
	/** `0x103e0a00` (retail name unrecovered), the type-1 zombie's per-think reacquire. With no enemy
	 *  (or one with no combat-character cache `+0x9c`): the live closest player (`+0x628c`) answering
	 *  the obfuscate test `0x10146a80` becomes the enemy through `SetEnemy` `0x10279a50`, otherwise it
	 *  is handed to slot 596. With an enemy: not obfuscated -> slot 544 `UpdateEnemyMemory` at its
	 *  origin; obfuscated and remembered (`0x102dfa20`) -> `0x10279c20`, the memory's `MarkAsEluded`. */
	void ZombieReacquireEnemy();
};
