#pragma once

#include "Substrate/ElysiumNpcBaseBoss.h"

// `CNPC_VTzimisce` (primary vtable `0x104cb934`), built by `npc_VTzimisce` factory `0x103b6bf0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcTzimisce : public FElysiumNpcBaseBoss
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VTzimisce", FElysiumNpcBaseBoss)

	// --- Select19 (story 0019/8 lane L06): the helpers `SelectSchedule` `0x103bb7c0` calls --------
	/** `0x103bc4e0` — the find-body gate (retail name UNRECOVERED): answers `0x18f`/`0x190`
	 *  (`FIND_BODY` / `_WALK`) when a body is found, else writes `+0x66b8 = 2`, `+0x66bc = 0` and 0. */
	int32 Select19TzimisceFindBodySchedule();
	/** SEAM for `0x103be180`, the body search `0x103bc4e0` asks. No body sweep stands here (the
	 *  pickup chain's own search is story-8 residue); answers false, retail's no-body arm. */
	bool Select19TzimisceFindBody();
	/** `0x103bc820` — the melee-attack pick (`0x177` / `0x178` / `0x179`) by enemy distance. */
	int32 Select19TzimisceMeleeAttackSchedule();
	/** `0x103bca20` — the `MELEE_IDLE` continuation. */
	int32 Select19TzimisceMeleeIdleContinuation();
	/** `0x103bcaf0` — the `MELEE_ADVANCE` continuation. */
	int32 Select19TzimisceMeleeAdvanceContinuation();
	/** `0x103bcb60` — the retreat / dodge / block continuation. */
	int32 Select19TzimisceMeleeRetreatContinuation();
	/** `0x103bcc00` — the attack continuation. */
	int32 Select19TzimisceMeleeAttackContinuation();
	/** `0x103bf660`, the pounce probe: family Conditions19's `TzimiscePounceTest` below. */
	bool Select19TzimiscePounceProbe();
	/** SEAM for slot 627 (`vtable +0x9cc`, `0x103b9e30`), the fidget-voice body only this class fills
	 *  (`SPI_FIDGET` through the three `tzimisce_voice_*` ConVars when slot 486 allows). Counted. */
	int32 Select19TzimisceFidgetCalls = 0;
	/** SEAM for `CBaseAnimating::m_fSequencePastHalf` (`+0x568`), which `0x103bcc00` reads first. The
	 *  animation layer carries no such byte; answers false (a sequence not yet past half). */
	bool Select19SequencePastHalf() const;

	// The constructor `0x103b6c60`: its hull store (`docs/vtmb/data/class_hulls.json`).
	FElysiumNpcTzimisce();

	virtual int32 CanPlaySequence(bool bDisregardState, int32 InterruptLevel) override;
	virtual void DeathSound() override;
	virtual void Slot593() override;
	virtual void NPCInit() override;
	virtual void StartNPC() override;
	virtual void Precache() override;
	virtual int32 NPC_EarlyTranslateActivity(int32 Activity) override;
	virtual void OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual bool FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4) override;
	virtual float ResolveTaskDistance(float Distance) override;
	virtual void TaskFail(int32 Reason) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual float MaxYawSpeed() override;
	virtual bool NavIgnoreCollision(FElysiumEntity* Other) override;
	virtual int32 DrawDebugTextOverlays() override;
	virtual int32 GetUsedHullBits() override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual void TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm, void* Tolerance, void* SecondTolerance) override;
	virtual void OnScheduleChange(int32 NewSchedule) override;
	virtual void JustMadeSound() override;
	virtual void IdleSound() override;
	virtual void PainSound() override;

	// +0x66b8 m_iShunnedFindBody (`CNPC_VTzimisce`): its own shunned-find counter, written by
	// `NPCInit` (`0x103b91d0`) and `TaskFail` (`0x103ba350`).
	int32 TzimisceShunnedFindBody = 0;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	/** `thunk_FUN_103be130(this)` — `CNPC_VTzimisce`'s carry-body probe, the same `+0x14b8` bit 5. */
	bool TzimisceCarryFormBit() const;

	// From `ElysiumNpcConditionsBodies.inl`.
	/** `CNPC_VTzimisce::vfunc463`'s expression map (`0x103ba2c0` + `0x103b9f50`): the retail state to
	 *  one of `PTR_s_normal_10653120`'s four names. Null for a state the switch does not name, which is
	 *  the arm that writes nothing. Pure, so the map is drivable with no NPC at all. */
	static const TCHAR* StateChangeExpressionName(EElysiumNpcState NewState);
	/** SEAM for `CBaseCombatCharacter::LookupExpressionIndex` + `0x103b9f90(this, index, 1.0)`. There is
	 *  no `SetExpression` in this runtime (the script API lists it as a stub), so this records the NAME
	 *  in `DefExpression` and blends nothing. `BlendSeconds` is retail's own `1.0`. */
	void SetDefaultExpression(const TCHAR* ExpressionName, float BlendSeconds);

	// From `ElysiumNpcDamage.inl`.
	// `CNPC_VTzimisce`'s link handle. Family Motor owns `m_hPickupTarget` (+0x6670) and `m_ePathMode`
	// (+0x668c); `+0x6684` is this family's, read and cleared by `0x103bf170`.
	FElysiumEntityHandle TzimiscePhysicsAnimlink;   // +0x6684 m_hPhysicsAnimlink (datamap)
	/** `0x103bf170` — `CNPC_VTzimisce`'s pickup release. 29c named the target `VGargoyleGibCleanup` on
	 *  the strength of the offset shapes and flagged it unconfirmed; the offsets settle it the other
	 *  way — `+0x6670` is `CNPC_VTzimisce::m_hPickupTarget` (family Motor's `PickupTarget`) and
	 *  `+0x6684` its `m_hPhysicsAnimlink`, and `thunk_FUN_103be0b0` is the Tzimisce `CARRYING_BODY`
	 *  flag write. The name is kept so the overlay row matches. Body, in order: clear the pickup target
	 *  to -1; re-arm the collision ignore at 0.75 s; resolve, remove and clear the link handle —
	 *  **`UTIL_Remove` is called even when the handle does NOT resolve**, on a null pointer, which is
	 *  retail's own unguarded call; then clear the carrying-body flag. */
	void VGargoyleGibCleanup();

	// From `ElysiumNpcDebug.inl`.
	/** `CNPC_VTzimisce::GetEventName` (`0x103bdd10`) — slot 241's only species override. Answers the
	 *  fixed name for anim-event ids 2..8 and null for everything else, which is the caller's signal to
	 *  fall through to `CBaseAnimating::GetEventName`. */
	static const TCHAR* TzimisceEventName(int32 EventId);


	// From `ElysiumNpcHints.inl`.
	/** `0x103bfa50` — `CNPC_VTzimisce`'s two-group hint pick (14000 vs 14001 within 200 units, nearer
	 *  wins, loser released with a 0.5 s reuse delay). NAMED for what it does: the generic
	 *  `FindHintNode` above is a different behaviour that happens to share 29c's target name. */
	int32 SelectTzimisceHintNode(const FElysiumEntity* Anchor);
	/** SEAM for `0x103bfc20`, the usability check `SelectTzimisceHintNode` applies to its winner before
	 *  choosing between the two schedule ids. Answers false. */
	bool IsTzimisceHintUsable(int32 HintNode, const FElysiumEntity* Anchor) const;

	// From `ElysiumNpcLifecycle19.inl`.
	/** `CNPC_VTzimisce` species words; `PickupTarget` (`+0x6670`), `PathMode` (`+0x668c`) and
	 *  `TzimisceShunnedFindBody` (`+0x66b8`) are declared with the class's other words. */
	bool bTzimisceFirstEnemy = false;            // +0x6689
	bool bTzimisceJustFoundBody = false;         // +0x66bc
	double TzimiscePounceCheckTimer = 0.0;       // +0x66ac
	double TzimisceShunnedBodyTimer = 0.0;       // +0x66b0
	int32 TzimisceStartNpcRearms = 0;
	int32 ExpressionMapResets = 0;           // `0x103b9f50`

	// From `ElysiumNpcMotor.inl`.
	// `CNPC_VTzimisce`'s pickup triple, read by its slot 410 `TranslateNavGoalPosition` (`0x103bf580`):
	// +0x6670 `m_hPickupTarget`, +0x6674 `m_vecPickupTargetPos` (SOURCE units) and +0x668c
	// `m_ePathMode`. Note the offsets: `m_ePathMode` is the HIGHEST of the three, not the lowest — the
	// ledger's one-line walk of that body has the triple in the wrong order.
	FElysiumEntityHandle PickupTarget;
	FVector PickupTargetPos = FVector::ZeroVector;
	/** `CNPC_VTzimisce::vfunc410` `0x103bf580` — the species branch of slot 410. Slot 410's own body is
	 *  the base `0x101a6420` and remains the generator's. */
	bool TranslateNavGoalPositionTzimisce(const FVector& GoalUnits, FVector& OutGoalUnits) const;

	// From `ElysiumNpcPositions.inl`.
	/** `CNPC_VTzimisce::vfunc389` `0x103bfd80`. The generated `Weapon_ShootPosition` keeps the Troika
	 *  line's body (`0x103338c0`, another family's row); this is the species branch beside it. False
	 *  means the activity is neither `0x106` nor `0x107` and the base answer stands. */
	bool WeaponShootPositionTzimisce(const FVector& SrcCm, FVector& OutCm) const;
	/** The pure form: `Src` offset along the body basis by the three scaled terms, with the RIGHT term
	 *  SUBTRACTED for activity `0x106` and ADDED for `0x107` — which is the only difference between the
	 *  two arms. */
	static FVector TzimisceAimOffset(const FVector& SrcCm, const FVector& Forward, const FVector& Right,
		const FVector& Up, float ForwardScale, float RightScale, float UpScale, bool bAddRight);
	/** The three `ConVar`s the override scales the basis by — 0 `DAT_1093cbac` (up,
	 *  `tzimisce_claw_left_z` "40"), 1 `DAT_1093cbf4` (right, `tzimisce_claw_left_y` "25"), 2
	 *  `DAT_1093cc3c` (forward, `tzimisce_claw_left_x` "0"). */
	static float TzimisceAimConVar(int32 Which);

	// From `ElysiumNpcSpecies.inl`.
	// `CNPC_VTzimisce`'s own words. `m_hPickupTarget` (+0x6670) and `m_vecPickupTargetPos` (+0x6674)
	// are family **Motor**'s `PickupTarget`/`PickupTargetPos`; `m_hPhysicsAnimlink` (+0x6684) is family
	// **Damage**'s `TzimiscePhysicsAnimlink`. All three are read through their owners here.
	int32 TzimiscePickupGrabBone = INDEX_NONE;   // +0x6680 m_iPickupTargetGrabBone (datamap)
	double TzimisceBodyTimer = 0.0;              // +0x66a4 m_flBodyTimer (datamap), an absolute stamp
	bool bTzimisceDidFakeThrow = false;          // +0x66b4 m_bDidFakeThrow (datamap)
	// `+0x6690` with allocation count `+0x6694`, grow size `+0x6698`, element count `+0x669c` and the
	// element mirror `+0x66a0` — the SAME `CUtlVector<{EHANDLE, float}>` shape as the boss blacklist
	// above, written a second time on a different class at a different offset.
	TArray<FBlacklistedEntity> TzimisceBlacklist;
	/** The three ConVars `CNPC_VTzimisce`'s slot 488 reads before firing `SPI_DIES` — `DAT_1093cf94`
	 *  `tzimisce_voice_pitch` "100", `DAT_1093cfdc` `tzimisce_voice_attn` "65" (`+0x2c` ints) and
	 *  `DAT_1093cebc` `tzimisce_voice_volume` "1" (`+0x28`, handed over as the float's dword). Answers
	 *  true for indices 0..2; any other index answers false and 0. */
	bool TzimisceDeathScriptArgument(int32 SingletonIndex, int32& OutArgument) const;
	/** `0x103be0b0` / `0x103be150` — `CNPC_VTzimisce`'s `CARRYING_BODY` latch and its timer read. */
	void FUN_103be0b0(bool bCarrying);
	bool FUN_103be150() const;
	/** `0x103be3d0` — `CNPC_VTzimisce`'s nearest-forearm grab-bone search. */
	bool FUN_103be3d0(FElysiumEntity* InTarget);
	/** `0x103be8e0` — `CNPC_VTzimisce`: is the pickup target close enough to grab? */
	bool FUN_103be8e0(FElysiumEntity* InTarget);
	/** `0x103bea90` / `0x103bef20` — `CNPC_VTzimisce`'s physics-animlink release and attach. */
	void FUN_103bea90(FElysiumEntity* AimTarget);
	bool FUN_103bef20(FElysiumEntity* InTarget, int32 ElementKey);
	/** `0x103bf200` / `0x103bf330` / `0x103bf3c0` — `CNPC_VTzimisce`'s own blacklist triple. */
	void FUN_103bf200(const FElysiumEntityHandle& Entity);
	bool FUN_103bf330(const FElysiumEntity* Candidate);
	int32 FUN_103bf3c0(const FElysiumEntity* Candidate) const;
	/** `0x103bf560` — `CNPC_VTzimisce`: release the motor's yaw hold. */
	void FUN_103bf560();


	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcMotor.inl`.
	int32 PathMode = 0;

	// --- 0019/8 lane L07, Conditions19 ---------------------------------------------------------
	/** `0x103be630` (no checklist row): the THROW_LOS line -- from `EyePosition - right *
	 *  tzimisce_throw_pos_y` to the enemy's eye, mask `0x400b`, `CTraceFilterSimple(this, 0)`; true when
	 *  `fraction == 1.0`. False for no enemy. */
	bool TzimisceThrowLosTest(FElysiumEntity* Enemy);
	/** `0x103bf660` (no checklist row): the pounce test. With an enemy (slot 167): its last known
	 *  position (`0x102dfed0`) through the lead helper `0x102c3b50`, both points raised by 0.1, the
	 *  squared distance inside `[40000, 360000]`, then a hull trace (this NPC's hull, mask
	 *  `0x202400b`) that must end ON the enemy. The argument retail pushes is never read. */
	bool TzimiscePounceTest();
	/** SEAM for ConVar `tzimisce_throw_pos_y` (`0x1093cad0`, default "-80"), read as `IsCommand() ? 0.0f
	 *  : m_fValue`. Not a row of `ElysiumNpcKernelTunables.h` (hot); answers the shipped default. */
	static float TzimisceThrowPosYConVar();
	/** SEAM for ConVar `tzimisce_pounce` (`0x1093cce0`, default "1"), read as `!IsCommand() &&
	 *  m_nValue`. Answers the shipped default. */
	static int32 TzimiscePounceConVar();

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual void Event_Killed(void* Arg0) override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;
	virtual void NPCThink() override;
	virtual void RunAI(bool Arg0) override;

	// --- RunAi19 (story 0019/8 lane L13a): what slot 432 `0x103bdef0` calls ---------------------
	/** The reaction half of `0x103bdef0` (`0x103bdf00`..`0x103bdf2a`): a null blocker does nothing;
	 *  kind 1 takes the step-up `0x103c0860` -- UNREACHABLE in retail, the sweep `0x103c0160` only
	 *  ever writes 0 -- and any other kind the push `0x103c05e0`. */
	void RunAi19TzimisceReact(FElysiumEntity* Blocker, int32 Kind);
	/** SEAMS for the three `CNPC_VTzimisce` ConVars the sweep and the push read (`IsCommand ? 0 :
	 *  m_fValue`, flags 0, no console variable stands for them): `tzimisce_obstruction_lookahead`
	 *  (object `0x1093cd28`, default "24"), `tzimisce_obstruction_scalar` (`0x1093cb18`, "5") and
	 *  `tzimisce_obstruction_z` (`0x1093cb60`, "75"). Mutable, as a ConVar is. */
	static float& TzimisceObstructionLookaheadConVar();
	static float& TzimisceObstructionScalarConVar();
	static float& TzimisceObstructionZConVar();
	virtual void GatherConditions() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;
	// --- Lane L05 (story 8 RunTask19)
	/** `+0x66a8 m_flTaskFailTimer` (absolute curtime), which `TASK 0xc9` waits out. **SEAM** word: its
	 *  writer (`CNPC_VTzimisce::StartTask`) is lane L04's. */
	double TzimisceTaskFailTimer = 0.0;

	// --- 0019/8 L04 (StartTask19 species): private helpers ---
	/** SEAM for `0x103bf440` (the carry facing), counted; `0x103bf660` is `TzimiscePounceTest`
	 *  (family Conditions19), counted here for the StartTask19 tests. */
	void FUN_103bf440();
	bool FUN_103bf660();
	int32 Fun103bf440Calls = 0;
	int32 Fun103bf660Calls = 0;
};
