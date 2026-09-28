#pragma once

#include "Substrate/ElysiumNpcVampire.h"

// `CNPC_VHengeyokai` (primary vtable `0x104b683c`), built by `npc_VHengeyokai` factory
// `0x1037e610`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcHengeyokai : public FElysiumNpcVampire
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VHengeyokai", FElysiumNpcVampire)

	// --- Select19 (story 0019/8 lane L06): the helpers `SelectSchedule` `0x1037fca0` calls --------
	/** `0x10382f60` — the melee-attack pick (`0x15c` / `0x16a`) by enemy distance; clears `+0x6678`. */
	int32 Select19HengeyokaiMeleeAttackSchedule();
	/** `0x10382d40` — the find-fish gate: `0x16b` `FIND_FISH` when a fish is found, else writes
	 *  `+0x6678 = 2`, `+0x667c = 0` and answers 0. */
	int32 Select19HengeyokaiFindFishSchedule();
	/** SEAM for `0x10381cd0`, the fish search `0x10382d40` asks. No fish sweep stands here; answers
	 *  false, retail's no-fish arm. */
	bool Select19HengeyokaiFindFish();

	// Slots 599 / 600: `0x10381750` / `0x10381780`, the melee-enter pair with every gate gone (story 5
	// commit B: the census walk had counted them carried by `CNPC_VHuman`'s bodies).
	virtual bool Slot599(int32 Arg) override;
	virtual bool Slot600(FElysiumEntity* Enemy) override;

	// The pickup chain's row for this class: the attach body `0x10382670`, the release body `0x10382400`,
	// the carrier bone, the `m_hPickupTarget` offset and the collision-ignore re-arm.
	static const FPickupSpecies& PickupRow();
	/** `0x10382670` — the attach: the shared head, then `dynamic_cast<CRagdollProp*>(param_1)`,
	 *  `SetHeld(true)` (`0x10157890`) and the element at the decoded `m_SecurePickupParam`
	 *  (`+0x66a0`, `0x10430130`; `ElementKey` is not read), the shared tail, then
	 *  `m_hPhysicsAnimlink = link`, `FINDING_BODY` off and `FormBit(true)` (`0x10381c00`). */
	bool AttachPickupAnimlink(FElysiumEntity* Carried, int32 ElementKey);
	/** `0x10382400` — the release: the shared throw toward `AimTarget` (`param_1`), then
	 *  `m_hPickupTarget = -1` BEFORE the collision re-arm (`0x102c43b0`, 0.75 s), then
	 *  `FormBit(false)`. No `StartIgnoringCollision`. */
	void ReleasePickupAnimlink(const FElysiumEntity* AimTarget);

	// The constructor `0x1037e680`: its hull store (`docs/vtmb/data/class_hulls.json`).
	FElysiumNpcHengeyokai();

	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual int32 NPC_EarlyTranslateActivity(int32 Activity) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual void TaskFail(int32 Reason) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual bool NavIgnoreCollision(FElysiumEntity* Other) override;
	virtual int32 DrawDebugTextOverlays() override;
	virtual int32 GetUsedHullBits() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;
	virtual bool SuppressesDamageFlinch(const FElysiumDmg& Dmg) const override;
	virtual void OnScheduleChange(int32 NewSchedule) override;

	// +0x6678 m_iShunnedFindFish (`CNPC_VHengeyokai`): its own shunned-find counter, written by
	// `NPCInit` (`0x1037fa70`) and `TaskFail` (`0x10380510`).
	int32 HengeyokaiShunnedFindFish = 0;
	// +0x6680 m_ePathMode (`CNPC_VHengeyokai`, walked; not in its datamap, so never saved): the
	// Tzimisce's `m_ePathMode` (+0x668c) mirrored on this class. `NPCInit` `0x1037fa70` zeroes it,
	// `StartTask` `0x103805d0` sets 2 when routing to `m_hPickupTarget`, and `OnScheduleChange`
	// `0x10383090` zeroes it; no slot-410 override reads it (story 5 step 4 split it off the
	// Tzimisce's word, which one port member had carried for both).
	int32 HengeyokaiPathMode = 0;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	/** `thunk_FUN_10381c80(this)` — `CNPC_VHengeyokai`'s carry-form probe, `m_bfAINPCFlags` (`+0x14b8`)
	 *  bit `0x20 CARRYING_BODY`. Not a seam: the port carries the word. */
	bool HengeyokaiCarryFormBit() const;

	// From `ElysiumNpcBosses.inl`.
	// `CNPC_VHengeyokai`'s pickup chain. `+0x6664`/`+0x6668`/`+0x6684`/`+0x6690` are its own datamap
	// words; family Motor's `PickupTarget`/`PickupTargetPos` are `CNPC_VTzimisce`'s at +0x6670/+0x6674
	// and are a different species' fact at a different offset.
	FElysiumEntityHandle HengeyokaiPickupTarget;             // +0x6664 m_hPickupTarget
	int32 HengeyokaiPickupTargetGrabBone = 0;                // +0x6668 m_iPickupTargetGrabBone
	FVector HengeyokaiPickupTargetPos = FVector::ZeroVector;  // +0x6684 m_vecPickupTargetPos, SOURCE units
	FElysiumEntityHandle HengeyokaiPhysicsAnimlink;          // +0x6690 m_hPhysicsAnimlink
	// +0x6698 m_SecurePickupParam, the `MvsnSec::CSecureType<int>` whose encoded word is +0x66a0.
	// Carried decoded (see the standing facts above). `0x10382670` hands it to the carried ragdoll's
	// `+0x2fc` and `+0x424`; nothing in layers 0–9 writes it but the constructor `0x1037e680`.
	int32 HengeyokaiPickupParam = 0;
	/** `+0x66a4 CNPC_VHengeyokai::m_BlacklistedEntities` — the store `0x10382970` appends to and
	 *  `0x10382aa0`/`0x10382b30` walk. `+0x66a8` (allocation count), `+0x66ac` (grow size), `+0x66b0`
	 *  (size) and `+0x66b4` (element pointer) are `CUtlMemory`'s own bookkeeping and have no counterpart
	 *  on a `TArray`; the constructor's reserve of 8 rows is not observable and is not reproduced. */
	TArray<FBlacklistedEntity> HengeyokaiBlacklist;
	void SetCarriedRagdollHeld(const FElysiumEntityHandle& Carried, bool bHeld);
	/** `0x10381e90` — `CNPC_VHengeyokai`'s grab-bone search. Walks the fixed two-name bone table
	 *  (`PTR_s_Bone01_1063bd88`: `"Bone01"`, `"Bone04"`, terminated by an empty string) on the grab
	 *  target, keeps the bone whose world position is nearest this NPC's own `GetOrigin()` (slot 220)
	 *  inside 1025 units, and writes it to `m_vecPickupTargetPos` / `m_iPickupTargetGrabBone`. When the
	 *  target does not cast, it writes the target's own origin with bone 0 and answers true. */
	bool FindPickupTargetGrabBone(const FElysiumEntity* InTarget);
	/** The two bone names `FindPickupTargetGrabBone` walks, in retail's order, `nullptr`-terminated. */
	static const TCHAR* const* PickupGrabBoneNames();
	/** `0x103822a0` — `CNPC_VHengeyokai`'s facing gate on its grab target: the 2-D yaw of the vector
	 *  from me to `Target` versus `GetAngles().y`, wrapped by `UTIL_AngleDiff`, must land inside
	 *  `[-20, +20]` degrees. Answers TRUE when `Target` is null or `m_hPickupTarget` does not resolve —
	 *  retail's own early-out, and the permissive one. */
	bool FUN_103822a0(const FElysiumEntity* InTarget) const;
	/** The pure rule behind it, so the cone can be measured without a world: retail's own
	 *  `UTIL_AngleDiff(UTIL_VecToYaw(delta), yaw)` inside `[_DAT_1049ae98, _DAT_1044eb0c]`. `Delta` is
	 *  in THIS world's axes and `YawDegrees` is Source's, exactly as family Facing's readers take them. */
	static bool WithinPickupFacingCone(const FVector& Delta, float YawDegrees);
	/** `0x10382970` — append `Entity` to `m_BlacklistedEntities` with an expiry of
	 *  `curtime + _DAT_1044eb0c` (20 s). Retail's `CUtlVector` grow (4, then double, then by the grow
	 *  size) and the zero-length `memmove` it always performs are bookkeeping with no observable effect
	 *  and are not reproduced; the APPEND and the STAMP are. */
	void AddBlacklistedEntity(const FElysiumEntity* Entity);
	/** `0x10382b30` — the index of `Entity` in `m_BlacklistedEntities`, or `INDEX_NONE`. Retail resolves
	 *  each stored `EHANDLE` and compares the POINTER, so a dead handle matches a null candidate. */
	int32 FindBlacklistedEntity(const FElysiumEntity* Entity) const;
	/** `0x10382aa0` — is `Entity` still blacklisted? Found and not yet expired answers true; found and
	 *  expired swap-removes the row with the LAST one and answers false; not found answers false.
	 *  Byte-for-byte the same body as `CNPC_VBaseBoss`'s `0x10366400` at `+0x665c`, which is family
	 *  **Species**' row — the rule below is written once and both stores can use it. */
	bool IsEntityBlacklisted(const FElysiumEntity* Entity);
	/** The pure rule over any such store, so both species' arrays are measurable without a world.
	 *  `Index` is what `FindBlacklistedEntity` answered. */
	static bool BlacklistTestAndExpire(TArray<FBlacklistedEntity>& Store, int32 Index, double Now);

	/** Slot 9's string, the one line `CNPC_VHengeyokai#124` adds. Retail takes the FIRST word of
	 *  whatever slot 9 returns and substitutes the empty string for null. **SEAM**: slot 9 is a
	 *  generated stub owned by another story; answers the empty string, which is retail's null arm. */
	FString HengeyokaiSlot9String() const;

	// From `ElysiumNpcLifecycle19.inl`.
	static constexpr int32 HullIndexHengeyokai = 0x12;   // `0x1037fa70`
	/** `CNPC_VHengeyokai` species words; `m_iShunnedFindFish` (`+0x6678`) is `HengeyokaiShunnedFindFish`. */
	bool bHengeyokaiJustFoundFish = false;       // +0x667c
	bool bHengeyokaiInSharkForm = false;         // +0x6694
	double HengeyokaiShunnedFishTimer = 0.0;     // +0x6670

	// From `ElysiumNpcMisc.inl`.
	/** `0x10381ca0` — the read half of the pair above: has `m_flFishTimer` (`+0x666c`) reached curtime?
	 *  NAMED `FormBitTimerExpired`, not 29c's `FormBit`: that name is the setter's, and one method
	 *  cannot be both a `void(bool)` and a `bool()`. */
	bool FormBitTimerExpired() const;

	// From `ElysiumNpcMotor.inl`.
	/** `thunk_FUN_102e1e20(m_pMotor, -1)` — `FUN_10382d20`'s cancel of the motor's queued facing/link
	 *  state. **SEAM**: shares the Facing family's finding that this mover keeps no facing queue. */
	void MotorCancelLinkFacing();
	/** `FUN_10382d20` `0x10382d20` — the other half of the same unrecovered link object: cancel the
	 *  motor's queued facing/link state with -1. */
	void ClearLinkActivity();

	/** `CNPC_VHengeyokai` thaw side-effect count (`0x10383130`). */
	int32 HengeyokaiThawCalls = 0;

	// --- 0019/8 lane L07, Conditions19 ---------------------------------------------------------
	/** `0x10382020` (no checklist row): the throw line -- from `EyePosition + 80 * right` (the right
	 *  vector of slot 221's angles, `_DAT_1049a198` = -80.0 subtracted) to the enemy's eye, mask
	 *  `0x600400b`, `CTraceFilterSimple(this, 0)`; true when `fraction == 1.0`. False for no enemy. */
	bool HengeyokaiThrowLosTest(FElysiumEntity* Enemy);


	// --- 0019/8 Boss19 (lane L12) --------------------------------------------------------------
	/** `m_nSkinCrossfade` and `m_flSkinCrossfadeTime` — the two `CBaseAnimating` words
	 *  `FadeToSkin` (`0x1008d6d0`) and `SetSkinFadeTime` (`0x1008d5f0`) write. The crossfade itself is
	 *  client-side; the Hengeyokai's morph is their only NPC writer, so they stand on this class. */
	int32 HengeyokaiSkinCrossfade = 0;
	float HengeyokaiSkinCrossfadeTime = 0.f;
	/** `0x103830e0` — the morph entry `RunAI` (`0x10380120`) and `OnTakeDamage_Alive` (`0x103801d0`)
	 *  share: stamp `NPC_VHengeyokai.cpp:0x985`, install `0x16e` (not forced), `SetSkinFadeTime(0.0)`,
	 *  `FadeToSkin(1)`. Lane L09's `OnTakeDamage_Alive` calls it at `0x1038025d`. */
	void HengeyokaiEnterMorph();

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual void Event_Killed(void* Arg0) override;
	virtual void UpdatePresenceEffect() override;
	virtual bool PlayerKnockbackReaction(FElysiumEntity* Arg0, int32 Arg1) override;
	virtual int32 OnTakeDamage_Alive(void* Arg0) override;
	virtual void RunAI(bool Arg0) override;
	virtual void GatherConditions() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;
	// --- Lane L05 (story 8 RunTask19)
	/** `+0x6674 m_flTaskFailTimer` (absolute curtime), which `TASK 0xc9` waits out. **SEAM** word. */
	double HengeyokaiTaskFailTimer = 0.0;
	/** `FUN_10383470` `0x10383470` -- `TASK 0x14b`'s whole arm. **SEAM**: counted. */
	int32 HengeyokaiTask14bCalls = 0;
	void HengeyokaiTask14b();
};
