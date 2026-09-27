#pragma once

#include "Substrate/ElysiumNpcVampire.h"

// `CNPC_VVampireBoss` (primary vtable `0x104a7b94`), built by `npc_VVampireBoss` factory
// `0x103c4fa0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcVampireBoss : public FElysiumNpcVampire
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VVampireBoss", FElysiumNpcVampire)

	virtual void NPCInit() override;
	virtual int32 Restore(void* Archive) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// +0x667c m_MorphModelName (`CNPC_VVampireBoss`), KEY MorphModel: the model the protean swap
	// (`InputTransformModel` `0x103c75f0`, story 8) morphs into.
	FString VampireBossMorphModelName;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcCombat10.inl`.
	/** `CNPC_VVampireBoss::GetCurrHealthPercent` (`0x103c6830`), 356 bytes, no slot. The COMPLEMENT of
	 *  `HealthToPercent`: `stat0xf / stat0x11` as a float, guarded by `ABS(cap) > 1e-05`
	 *  (`_DAT_104ce8c0`) on the DIVISOR — the decompiler's `(a < eps) == (a == eps)` idiom resolves to
	 *  `a > eps` — and `_DAT_104454c4` = **0.0** otherwise. */
	float GetCurrHealthPercent() const;

	// From `ElysiumNpcDamage.inl`.
	// `CNPC_VVampireBoss`'s gore words — the per-body-region emitter names and the four live emitters.
	FString BodyEmitterNames[4];                 // +0x6684 m_pBodyEmitterNames[4] (datamap, string_t)
	FElysiumEntityHandle ParticleEmitters[4];    // +0x66a0 m_hParticleEmitters[4] (datamap)
	/** SEAM for `CVDmg_t::Apply`'s AOE sound pick: the victim's own vtable `+0x50c`, whose answer
	 *  `CausePlayerAOEDamage` switches its impact sound id on, and `+0x500`, which plays it. Neither
	 *  slot has a body in this substrate. `+0x50c` answers 0, which selects the default id `0x79`, and
	 *  the chosen id is recorded rather than played — the CHOICE is what is recovered. */
	int32 AoeTraceAttackResultCode(const FElysiumEntity* Victim) const;
	TArray<int32> AoeImpactSounds;
	/** `0x103c7230` — `CNPC_VVampireBoss::CausePlayerAOEDamage(const Vector& centreUnits, float radius)`.
	 *  Only with a live `m_hClosestPlayer` and only when its origin is strictly inside `radius` of the
	 *  centre: trace from the centre to the player with `CTraceFilterWorldOnly` and mask 1, build a
	 *  `CVDmg_t` through `CVDmg_t::Set(1, 0x40, (int)distanceSquared)` — family LETHAL, `DMG_BLAST`,
	 *  and the damage input is the SQUARED distance, which is retail's own arithmetic and not a slip —
	 *  dispatch it at the player with `DispatchTraceAttack`, then pick an impact sound off the result
	 *  of the player's own vtable `+0x50c`: 0x79 by default, 0x7a on 1, 0x7b on 3, played through its
	 *  `+0x500`. */
	void CausePlayerAOEDamage(const FVector& CentreUnits, float RadiusUnits);
	/** `0x103c6eb0` — `CNPC_VVampireBoss::ClearBodyEmitterNames`: all four `m_pBodyEmitterNames`
	 *  entries to the null string, unconditionally. */
	void ClearBodyEmitterNames();
	/** `0x103c6df0` — `CNPC_VVampireBoss::SetBodyEmitterName(int region, string_t name)`. One store,
	 *  no bound check — retail indexes the four-entry array with the caller's word as given. */
	void SetBodyEmitterName(int32 Region, const FString& Name);
	/** `0x103c7010` — `CNPC_VVampireBoss::SpawnBodyEmitter(int region, CBaseEntity* attachTo)`.
	 *  Answers nothing when `attachTo` is null or the region's name is unset; otherwise creates the
	 *  named emitter and either one-shots it on ITSELF when the region is 3 (`+0x3cc(this, 1)`) or
	 *  attaches it at `attachTo` (`+0x3cc(this, 2, attachTo)`). Note retail never STARTS it here —
	 *  `+0x3c4` is not called — unlike every other emitter body in this family. */
	int32 SpawnBodyEmitter(int32 Region, const FElysiumEntityHandle& AttachTo);
	/** `0x103c7150` — `CNPC_VVampireBoss::KillBodyEmitters`: walk all four `m_hParticleEmitters`, and
	 *  for each that still resolves call its stop (`+0x3c8`) then `thunk_FUN_100fbbb0(entity, 0.1)`,
	 *  the 0.1 s fade-and-remove. The handles are NOT cleared. */
	void KillBodyEmitters();
	/** `0x103c67f0` — `CNPC_VVampireBoss`'s "has it been longer than this since I last attacked".
	 *  `curtime - m_flLastAttackTime (+0x5d9c) > Threshold`, strictly. 29c named the target
	 *  `FElysiumNpc::LastAttackTime`, which is already the NAME OF THE MEMBER 29b declared for
	 *  `+0x5d9c`; the body lands under the elapsed-form name instead and the report says so. */
	bool LastAttackTimeElapsed(float ThresholdSeconds) const;

	// From `ElysiumNpcLifecycle19.inl`.
	void VampireBossNPCInit();          // `0x103c5840`

	// From `ElysiumNpcMotor.inl`.
	/** `thunk_FUN_102c4e80(this)` — the commit every `SetupJump`/`SetupSuperJump` ends on, which takes
	 *  the three jump words this family has just written and starts the leap. **SEAM**: records that
	 *  the commit was reached and starts nothing. */
	void CommitSetupJump();
	void SetupJumpRise(float Enabled, float Rise);

	// From `ElysiumNpcSaveRestore10.inl`.
	/** `CNPC_VVampireBoss::Restore` (`0x103c5910`) — the Troika body, then a post-load reset of the
	 *  monster-model override: `m_pMonsterModelName` (`+0x6680`) := null, `ClearBodyEmitterNames()`
	 *  (family Damage's, `0x103c6eb0`), and `m_pszMonsterClassname` (`+0x6694`) := the literal
	 *  `"npc_VVampireBoss"`. The write order is the listing's (`103c5972` the model name, `103c597c`
	 *  the emitter names, `103c5981` the classname).
	 *
	 *  This is also the body the other bosses' own slot-127 overrides call as their base
	 *  (`CNPC_VAndreiBlood` `0x1035cf80`, `CNPC_VAsianVampire` `0x10360e10`, the Chang brothers
	 *  `0x1036b170`, `CNPC_VSabbatLeader` `0x103a6e80`, `CNPC_VSheriffMan` `0x103ae7f0`), so it sits
	 *  between the Troika body and those rows: each of them calls this directly, as retail does. */
	int32 VampireBossRestore(void* Archive);
	/** `+0x6680 CNPC_VVampireBoss::m_pMonsterModelName` and `+0x6694 m_pszMonsterClassname` — the two
	 *  words `CNPC_VVampireBoss::Restore` resets. Neither had a carrier before this story. */
	FString VampireBossMonsterModelName;
	FString VampireBossMonsterClassname;

	// From `ElysiumNpcSpeciesMisc10.inl`.
	/** `0x103c67f0` — `curtime - m_flLastAttackTime > Seconds`, the boss line's shared idle test. */
	bool AttackIdleLongerThan(float Seconds) const;
	/** `CNPC_VVampireBoss::WaitForTransformation` (`0x103c63c0`), one gate then three writes: nothing
	 *  until `curtime` passes `m_flProteanTransformStartTime + _DAT_104ce8bc` (**2.0** s); then
	 *  `CVStatList_t::Set(stat 0x0f Health, 0)` on the type-0 list — a full heal, because `0x0f` is the
	 *  WOUND counter; then fire the transform partner's `+0x6664` output with this as both activator and
	 *  caller; then `TaskComplete(false)`. */
	void WaitForTransformation();
	double ProteanTransformStartTime = 0.0;      // `m_flProteanTransformStartTime`
	FElysiumEntityHandle TransformPartner;       // `m_hTransformPartner`
	/** `0x103c6a00` — `m_HealthPercentRecord (+0x6698) = GetCurrHealthPercent()`, the boss line's
	 *  snapshot taken before a phase. Five direct callers across the boss species. */
	void RecordHealthPercent();
	/** `0x103c6a20` — `GetCurrHealthPercent() - m_HealthPercentRecord`, IN THAT ORDER. The percent rises
	 *  with damage, so this answers POSITIVE for a body that has lost health (standing fact one). */
	float HealthPercentLostSinceRecord() const;
	float BossHealthPercentRecord = 0.f;   // +0x6698 `m_HealthPercentRecord` (walked)
	/** `CNPC_VVampireBoss::SpawnBodyEmitters` (`0x103c6f40`): `KillBodyEmitters` FIRST, then four fixed
	 *  iterations of `SpawnBodyEmitter(i, name)` over the pointer table at
	 *  `PTR_s_Bip01_L_Hand_1065e6d0`, storing each spawned entity's handle into `m_hParticleEmitters[i]`
	 *  (`+0x66a0`). A spawn that answers null leaves that slot's PREVIOUS handle standing rather than
	 *  clearing it, which is why the kill has to run first.
	 *
	 *  The four attachment names read out of the pinned image at `0x65e6d0`: `Bip01 L Hand`,
	 *  `Bip01 R Hand`, `Bip01 Spine`, `Bip01 Spine` — the last two are the same string. */
	void VampireBossSpawnBodyEmitters();
	static const TCHAR* VampireBossEmitterAttachment(int32 Region);
	/** The ATTACHMENT name each `SpawnBodyEmitter` call was handed. Family Damage's
	 *  `SpawnBodyEmitter(region, attachTo)` takes the attach ENTITY (retail's second argument is a
	 *  `CBaseEntity*`), so the bone this table supplies is recorded beside the call rather than folded
	 *  into it — it is the half of the decision the seam does not carry. */
	FString BodyEmitterAttachments[4];

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void NPCThink() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;

	// --- 0019/8 L04 (StartTask19 species): private helpers ---
	/** `+0x669c CNPC_VVampireBoss::m_fTaskStartTime` (datamap): `StartTask` `0x103c5ac0` stamps curtime
	 *  on every task before its switch. (The species shape map's `ABSENT` row for `+0x669c` now has
	 *  this member; the integrator rebinds it.) */
	double VampireBossTaskStartTime = 0.0;
	/** Slot 618 on the vampire-boss line (`CALL [EDX+0x9a8]`, `StartTask` task `0x14c` and the species
	 *  `0x36`/`0x37` arms). **SEAM**: `CNPC_VVampireBoss::TransformationStart` `0x103c60a0` and
	 *  `CNPC_VSabbatLeader::TransformationStart` `0x103ab310` are family Spawn19's rows; counted until
	 *  they land behind this virtual. */
	virtual void TransformationStartSlot618();
	int32 TransformationStartCalls = 0;
	/** SEAM for `CBaseAnimating::MatchOriginAnglesToAnimation("bip01", 1, 1)` (`0x1000577c`), the
	 *  unhide arm's snap onto the root bone. Recorded; the transform is left alone (the same seam
	 *  `FElysiumNpcWerewolf::MatchOriginAnglesCalls` stands). */
	TArray<FMatchOriginAnglesCall> VampireBossMatchOriginAnglesCalls;
};
