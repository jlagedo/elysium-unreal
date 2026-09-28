#pragma once

#include "Substrate/ElysiumNpcBaseBoss.h"

// `CNPC_VMingXiao` (primary vtable `0x104bc704`), built by `npc_VMingXiao` factory `0x10390e70`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcMingXiao : public FElysiumNpcBaseBoss
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VMingXiao", FElysiumNpcBaseBoss)

	// --- Select19 (story 0019/8 lane L06): the words and seams `0x10394120` / `0x103941e0` read ---
	// `+0x6680 m_hRangedWeapon` (`MingXiaoRangedWeapon`, declared with lane L07's block below) is also
	// read by `PreSelectSchedule` `0x10394120`: unset until `Spawn` `0x103927a0` lands, so the
	// pre-selector takes retail's `Weapon_Switch(NULL, 0)` arm.
	/** `+0x674c CNPC_VMingXiao::m_flChargeReadyTime` (FIELD_TIME), the charge cooldown
	 *  `SelectSchedule` `0x103941e0` reads and writes; ABSENT in the shape map until now. */
	double MingXiaoChargeReadyTime = 0.0;
	/** `thunk_FUN_101e8da0(0x10739d08)` — `LEA EAX,[ECX+0x2bc]`, the `Ming_Xiao_Info/General` slice of
	 *  the Rules.txt feat list (`0x101e6310`), read by byte offset: `+0x8` ThrowChance (the pickup-draw
	 *  ceiling), `+0xc` / `+0x10` ChargeResetTimeNormal / ChargeResetTimeDesperate. Read from the
	 *  session rulebook with the loader's defaults (60, 10.0, 10.0). */
	float Select19MingXiaoTuningField(int32 Offset) const;

	// The constructor `0x10390ee0`: its hull store (`docs/vtmb/data/class_hulls.json`).
	FElysiumNpcMingXiao();

	virtual int32 CanPlaySequence(bool bDisregardState, int32 InterruptLevel) override;
	virtual void Precache() override;
	virtual int32 Save(void* Archive) override;
	virtual int32 Restore(void* Archive) override;
	virtual void UpdateOnRemove() override;
	virtual int32 SelectIdealStateRetail() override;
	virtual FVector GetShootEnemyDir(const FVector& ShootPositionCm, int32 A, int32 B) override;
	virtual int32 SelectScheduleMeleeCombat(int32 Unused) override;
	virtual float ResolveTaskDistance(float Distance) override;
	virtual void TaskFail(int32 Reason) override;
	virtual int32 HealthToPercent() override;
	virtual int32 SelectScheduleRangedCombat(int32 Arg) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual float MaxYawSpeed() override;
	virtual bool NavIgnoreCollision(FElysiumEntity* Other) override;
	virtual void DrawDebugGeometryOverlays() override;
	virtual const TCHAR* GetShortConditionName(int32 ConditionId) override;
	virtual void OnChangeActivity(int32 Activity) override;
	virtual int32 GetUsedHullBits() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;
	virtual void TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm, void* Tolerance, void* SecondTolerance) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcBosses.inl`. `+0x667c m_hMeleeWeapon`, a SAVE word no ported body reads
	// yet; it stands so its datamap row persists.
	FElysiumEntityHandle MingXiaoMeleeWeapon;

	// From `ElysiumNpcDamage.inl`.
	/** `0x10397dd0` — `CNPC_VMingXiao`: reset `m_flSpitAttackTimer` (+0x66c0) to 0, **only when both
	 *  parameters are non-null**. Retail's two parameters are never read for anything else, so the
	 *  presence test is the whole of the condition and is reproduced as a pair of bools. */
	void SpitAttackTimer(bool bFirstParamSet, bool bSecondParamSet);

	// From `FElysiumNpcMingXiaoTentacle` (the move manifest's corrected owner).
	// From `ElysiumNpcLifecycle.inl`.
	bool bProxyRegistered[MingXiaoProxySlots] = { false, false, false, false, false, false };
	/** `CNPC_VMingXiao`'s `FUN_10397b40` — may `Proxy` take a proxy slot right now? Retail, arm by arm:
	 *  a null argument answers false; `curtime < m_flProxyReadyTimer` answers false; the argument's own
	 *  `+0x6660` slot index must resolve back to the argument through `+0x66a8`; an already-registered
	 *  slot answers TRUE at once; otherwise count the six slots that are either live handles or
	 *  registered, and only a count of ZERO registers this slot and answers true. */
	bool ProxyReadyTimer(const FElysiumEntity* Proxy, double Now);
	/** `proxy->+0x6660`: the tentacle's own `m_iTentacleID` (`FElysiumNpcMingXiaoTentacle::TentacleId`),
	 *  read through its class; `INDEX_NONE` for anything that is not a tentacle. */
	int32 ProxySlotIndexOf(const FElysiumEntity* Proxy) const;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcAnim.inl`.
	// `CNPC_VMingXiao::BodyGroup` `0x10398800` — writes `m_nBody` (+0x067c) from the severed-tentacle
	// mask or from its own cvar. Three arms, in retail's order.
	void BodyGroup();
	// The cvar `BodyGroup` reads (`DAT_1093bb14`, through `ConVar::IsCommand()` and `m_nValue`):
	// `debug_tentacle_mask`, shipped "-1", the arm that takes the tentacle mask.
	bool BodyGroupCvarIsCommand() const;
	int32 BodyGroupCvarValue() const;

	// From `ElysiumNpcBosses.inl`.
	// `CNPC_VMingXiao`'s own words. `m_rhProxies` (+0x668c) and `m_rhSeveredTentacles` (+0x66a8) are
	// family **Squad**'s (`ElysiumNpcSquad.inl`) and `m_bBlockedByFriend` (+0x6750) is family
	// **Motor**'s; this family reads all three through their owners rather than standing copies.
	int32 MingXiaoTentacleId = INDEX_NONE;       // +0x6674 m_iTentacleID, -1 on the head
	double MingXiaoProxyReadyTimer = 0.0;        // +0x66a4 m_flProxyReadyTimer, an absolute deadline
	double MingXiaoAttackTimers[6] = {};         // +0x66c4 m_rflAttackTimers[6], absolute deadlines
	int32 MingXiaoConnectedTentacleCount = 0;    // +0x670c m_iConnectedTentacleCount
	uint32 MingXiaoSeveredTentacleMask = 0;      // +0x6710 m_iSeveredTentacleMask
	FElysiumEntityHandle MingXiaoThrowObject;    // +0x6718 m_hThrowObject
	int32 MingXiaoThrowingTentacle = 0;          // +0x671c m_eThrowingTentacle
	int32 MingXiaoThrowableObjectMode = 0;       // +0x673c m_eThrowableObjectMode
	/** `0x10396dc0` — `CNPC_VMingXiao::SelectSchedule`'s grabbed-object arm. With a live
	 *  `m_hThrowObject` and `m_eThrowableObjectMode` strictly inside `(2, 5)`, condition `0x7b` answers
	 *  schedule `0x15c` and condition `0x7c` answers `0x15d`; anything else answers 0. Retail also
	 *  writes its own `__FILE__`/`__LINE__` (`"E:\\Vampire\\main\\dlls\\hl2_dll\\NPC_…"`, lines 0xbe1
	 *  and 0xbe5) into the selector trace at `+0x1b30`/`+0x1b34`; that pair is `_ABSENT` in this
	 *  runtime's shape map, whose mind transition trace carries the same account. */
	int32 FUN_10396dc0() const;
	/** `0x10397a50` — `CNPC_VMingXiao::Event_Killed`'s proxy arm. Stamps `m_flProxyReadyTimer` with
	 *  `curtime + max(Tuning[100] + Tuning[0x68] * (6 - m_iConnectedTentacleCount), 0)` and, when the
	 *  dead proxy (a `CNPC_VMingXiao`, id `+0x6674`) is still `m_rhProxies[id]`, severs slot `id`
	 *  through `0x10397930`. */
	void FUN_10397a50(const FElysiumEntity* Proxy, TFunctionRef<float(int32)> TuningField);
	int32 LastSeveredTentacle = INDEX_NONE;
	/** `0x10398000` — `(m_iSeveredTentacleMask & (1 << n)) == 0`, which every MingXiao body spells as
	 *  "tentacle n is still there". Not a row of this family; two lines, and three of this family's
	 *  bodies gate on it, so it is written rather than seamed. */
	bool IsTentacleConnected(int32 TentacleId) const;
	/** `0x10398870` — `m_iTentacleID != -1`, retail's "this MingXiao is a proxy, not the head". */
	bool IsMingXiaoProxy() const;
	/** `0x10397f70` — `CNPC_VMingXiao::Spawn`/`StartTask`'s rate pick: a proxy answers `Tuning[0x20]`
	 *  flat; the head answers `max(Tuning[0x6c] + Tuning[0x70] * (6 - m_iConnectedTentacleCount), 0)`. */
	float FUN_10397f70(TFunctionRef<float(int32)> TuningField) const;
	/** `0x10398030` — the six-way tentacle attack gate `SelectSchedule` and `GatherConditions` share.
	 *  `Slot` is 0..5. Answers whether the slot may attack; `OutSchedule` carries retail's schedule id
	 *  (`0x112a`..`0x112d` for slots 0–3, none for 4 and 5). `bTestMelee` is retail's `param_2`, which
	 *  adds the `m_hMeleeWeapon` / slot 331 `ChooseMeleeAttackSequence` test on top. */
	bool FUN_10398030(int32 Slot, bool bTestMelee, int32& OutSchedule);
	/** SEAM for `0x10398030`'s `param_2` tail: `m_hMeleeWeapon`'s owner (`+0xa0`), its activity through
	 *  the weapon's `+0x5a4`, this NPC's slot 376 `NPC_TranslateActivity`, `GetEnemy()`'s `+0x9c`, and
	 *  slot 331 `ChooseMeleeAttackSequence` — whose Troika body is story 29d's. Answers false, which is
	 *  retail's REFUSAL arm, so the melee-tested form of the gate never opens. */
	bool ChooseMeleeAttackSequenceSeam() const;
	/** `0x103983d0` — `CNPC_VMingXiao::RunTask`'s throw-force curve, selected by `m_eThrowingTentacle`
	 *  (0..5). Recovered from the LISTING (`vtmb_asm 0x103983d0`): the decompiled C lost the jump table
	 *  and the ST0 return. Slots 0–3 answer `max(Tuning[0x74] + Tuning[0x78] * (6 - count), 0) * Scale`
	 *  with `Scale` picked by the closest-player distance; slots 4–5 answer
	 *  `max(First + Second * (6 - count), 0)` with no second factor; anything else answers 20.0. */
	float FUN_103983d0(int32 Selector, TFunctionRef<float(int32)> TuningField) const;
	/** `0x103989b0` — which tentacle should take a pedestal that is abeam of me. The pure rule, so it
	 *  can be measured without a world: the target must be inside 64 units of my own height, the
	 *  NORMALIZED 2-D direction to it must land in `[-0.17, 0.5]` of `Forward`, and its side against
	 *  `Right` picks tentacle 5 (at or left of centre) or tentacle 4. `Delta` is target minus me in
	 *  SOURCE units and in THIS world's axes, as `Forward`/`Right` are. */
	static bool PedestalTaskForSide(const FVector& DeltaUnits, const FVector& Forward,
		const FVector& Right, bool bTentacle4Connected, bool bTentacle5Connected, int32& OutTask);
	/** The body itself, reading `m_vecForward` (+0x6290) and `m_vecRight` (+0x629c). Both are retail's
	 *  cached basis and NOTHING in this runtime writes them, so the member form takes its miss arm
	 *  until a sense pass fills them; the pure form above is the recovered rule. */
	bool FUN_103989b0(const FVector& TargetOriginUnits, int32& OutTask) const;
	/** `0x10398b20` — MingXiao's pedestal pick. Only with `m_flClosestPlayerDistance` at or past 150
	 *  units, at least one of tentacles 4 and 5 connected, AND the species cvar `DAT_1093ba8c` reading
	 *  non-zero: walk every entity within 257 units, skipping any the boss blacklist still holds, keep
	 *  those whose `m_iName` starts with `"Pedestal"` (eight characters, case-insensitive) and whose
	 *  velocity is within 0.1 of zero on all three axes, ask `FUN_103989b0` which tentacle takes it, and
	 *  keep the nearest winner — the search radius shrinking to each winner's distance as retail's does.
	 *  Answers the chosen entity, having written the aim point and forward through `0x10398890`. */
	FElysiumEntity* FUN_10398b20(int32& InOutTask, FVector& OutAimPointUnits, FVector& OutForward);
	/** The `DAT_1093ba8c` cvar gate `0x10398b20` puts in front of its whole search
	 *  (`!cvar->vfunc1() && cvar->m_nValue != 0`, retail's inlined `ConVar::GetInt`):
	 *  `ming_xiao_pickup`, shipped "1" — the search runs. */
	int32 MingXiaoPedestalCvar() const;
	/** The search itself, behind the cvar gate, so the recovered rule is measurable. `RadiusUnits` is
	 *  retail's 257.0 starting radius, which shrinks to each accepted winner's distance. */
	FElysiumEntity* FindNearestPedestal(float RadiusUnits, int32& OutTask);
	/** SEAM for `0x10398890` — the aim point `0x10398b20` hands its caller: the pedestal's origin pushed
	 *  `_DAT_10463584` along `m_vecForward` and `_DAT_1049ae40` along `m_vecRight`, added for task 4 and
	 *  subtracted otherwise, with `m_vecForward` copied out beside it. Both cells live past `.data`'s
	 *  raw size and are filled at runtime, so their values are **unrecovered**; the two SIGNS and the
	 *  forward copy are the recovered half and are ported. */
	void MingXiaoPedestalAimPoint(const FVector& PedestalOriginUnits, int32 Task,
		FVector& OutAimPointUnits, FVector& OutForward) const;
	/** SEAM for `CBaseEntity::GetVelocity(&vel, &angvel)` (slot 199), which `0x10398b20` uses to require
	 *  a pedestal be stationary. `FElysiumEntity::Velocity` is CENTIMETRES per second; this answers
	 *  SOURCE units, which is what the 0.1 tolerance is in. */
	FVector EntityVelocityUnits(const FElysiumEntity& Entity) const;

	/** SEAM for `CNPC_VMingXiao::0x10398000(this, i)` — "is limb `i` (0..5) still attached". No port
	 *  system stands Ming Xiao's severable limbs, so this answers **false** for every index, which
	 *  leaves the arm equal to the base formula — retail's own answer for an intact boss. */
	bool MingXiaoLimbPresent(int32 LimbIndex) const;

	// From `ElysiumNpcConditions10.inl`.
	/** `CNPC_VMingXiao::TaskFail` (`0x10394090`) — switches on `m_eThrowableObjectMode` (`+0x673c`):
	 *  modes 3 and 4 reset the motor's steering to 180.0 and touch nothing else; every other mode sets
	 *  the mode to 0 and releases `m_hThrowObject` (`+0x6718`). */

	// From `ElysiumNpcDamage.inl`.
	// `CNPC_VMingXiao`'s words this family touches and family Bosses did not declare.
	bool bMingXiaoHasTransformed = false;        // +0x6678 m_bHasTransformed (datamap)
	double MingXiaoSpitAttackTimer = 0.0;        // +0x66c0 m_flSpitAttackTimer (datamap)
	FVector MingXiaoPickupTargetPos = FVector::ZeroVector;    // +0x6720 m_vecPickupTargetPos, SOURCE
	FVector MingXiaoPickupSavedForward = FVector::ZeroVector;  // +0x672c m_vecPickupSavedForward
	FElysiumEntityHandle MingXiaoPhysicsAnimlink;              // +0x6738 m_hPhysicsAnimlink (datamap)
	bool TestOneHitbox(int32 HitboxSetIndex, const FVector& RayStartUnits, const FVector& RayEndUnits,
		uint32 Mask) const;
	/** `0x103990c0` — `CNPC_VMingXiao`'s throw release, the largest body in this family (1,074 bytes).
	 *  In retail's order: remove and clear `m_hPhysicsAnimlink` (+0x6738) if it resolves; then, ONLY
	 *  with a live enemy (slot 167), take the held object's centre (`+0x370`), solve a lead point at
	 *  the enemy through `thunk_FUN_102c36d0` with the gravity cvar `DAT_1093bbcc`, add the enemy's own
	 *  per-frame position delta (`piVar7[0xa0..0xa2] - piVar7[0x9d..0x9f]`) scaled by `_DAT_104454d0`
	 *  = 0.5 to the lead's Z, take the yaw of the lead direction and — when `UTIL_AngleDiff` against
	 *  this NPC's own yaw leaves the `[-20, +20]` cone — re-aim the XY at exactly `yaw -/+ 20` degrees,
	 *  normalize, then scale by a speed that is `1000.0` when
	 *  `DAT_1093bc14 + DAT_1093bbcc * distanceSquared` is at or below `_DAT_10447ee0` = 1000 and that
	 *  same sum otherwise, with `DAT_1093b9fc * distanceSquared` added to the Z afterwards. It applies
	 *  the result through the ragdoll element (`+0x428`) or the physics object (`+0xa0` then `+0x9c`),
	 *  and then — unconditionally, on EVERY path including the no-enemy one — clears `m_hThrowObject`
	 *  (+0x6718), re-arms the collision ignore at 0.75 s and sets the throwable mode to 0.
	 *
	 *  The three `DAT_1093…` cvar cells live past `.data`'s raw size and no corpus function constructs
	 *  them, so their names and defaults are **unrecovered**; the seam below answers 0.0f, which is an
	 *  unconstructed cvar's own answer and which makes the speed take the `<= 1000` arm. */
	void LaunchRagdollTowardTarget();
	/** SEAM for `DAT_1093bbcc` (the gravity/quadratic term), `DAT_1093bc14` (the constant term) and
	 *  `DAT_1093b9fc` (the Z term) of that speed. **Unrecovered**; all answer 0.0f. */
	float MingXiaoThrowCvar(int32 Which) const;
	/** The pure speed rule, so the two arms are measurable: `Quadratic * DistSq + Constant`, answering
	 *  1000.0 when that sum is at or below 1000.0 and the sum itself otherwise. */
	static float MingXiaoThrowSpeed(float DistanceSquared, float Quadratic, float Constant);
	/** `0x103937d0` — `CNPC_VMingXiao`'s melee/throw swing task. 29c mapped three distinct retail
	 *  bodies onto the one target name `MingXiaoThrowAttack`; they are three behaviours and land as
	 *  three methods. This one: reset the navigator's path (`thunk_FUN_102e0b40(m_pNavigator)`), resolve
	 *  `m_hMeleeWeapon`'s owner (+0xa0), run the pre-attack hook (`+0x610`), and TaskFail `0x1f` when
	 *  there is no active weapon. Otherwise ask the weapon for the activity that matches the requested
	 *  one (`+0x5a4`), run `+0x5e0`, and choose a melee sequence through slot 331; a refusal or a
	 *  negative activity fails the task (`thunk_FUN_10289ee0`) and a success sets the activity
	 *  (`+0x4dc`). Either way it then stamps `m_rflAttackTimers[tentacle]` (+0x66c4) with
	 *  `curtime + FUN_103983d0(...)` — family Bosses owns both that array and that curve. */
	void MingXiaoThrowAttack(int32 TaskId, int32 Tentacle, TFunctionRef<float(int32)> TuningField);
	/** `0x10396bc0` — `CNPC_VMingXiao`'s pickup search, `SelectSchedule`'s grab arm. Answers 0 unless
	 *  `m_hThrowObject` (+0x6718) is DEAD and both `curtime >= +0x66d4` and `curtime >= +0x66d8`; then
	 *  it clears condition 9, draws `RandomInt` against the tuning record's `+8` cell and, only on a
	 *  draw below it, runs family Bosses' pedestal search (`0x10398b20`) and stores its answer. With a
	 *  live object it starts ignoring that object's collision, sets the throwable mode to 1, stamps the
	 *  selector trace with line `0xbc3` and answers schedule `0x167`; otherwise 0. */
	int32 MingXiaoFindThrowObject(int32 PedestalCvarDraw, int32 PedestalCvarCeiling);
	/** `0x10398fd0` — `CNPC_VMingXiao`'s throw cleanup, which is `0x103990c0`'s tail on its own: remove
	 *  and clear `m_hPhysicsAnimlink`, clear `m_hThrowObject`, re-arm the 0.75 s collision ignore and
	 *  set the throwable mode to 0. */
	void MingXiaoThrowCleanup();
	/** `0x10398d90` — `m_eThrowableObjectMode = value` (+0x673c, family Bosses' member). Thirteen
	 *  bytes; three of this family's bodies and three of Bosses' call it. */
	void ThrowableObjectMode(int32 Mode);
	/** `0x10397000` — `CNPC_VMingXiao`'s slot-166 `CanStandOn(CBaseEntity*)` override. 29c named the
	 *  target `FElysiumNpcMingXiao::SeveredTentacles`, which is ALREADY family Squad's member array for
	 *  `m_rhSeveredTentacles` (+0x66a8); the body lands under the fuller name and the report says so.
	 *  Walks indices 0..5 of BOTH `m_rhProxies`
	 *  (+0x668c) and `m_rhSeveredTentacles` (+0x66a8) — family Squad's two arrays, read through their
	 *  owner — testing each RESOLVED entity pointer against the candidate, and answers FALSE on the
	 *  first match. On a full miss a non-null candidate is asked its own `IsStandable` (slot 164) and a
	 *  false there answers false; a NULL candidate skips that test and answers TRUE. Slot 166's
	 *  Troika-line body is family Motor's `CanStandOn`, which this does not call. */
	bool SeveredTentaclesCanStandOn(const FElysiumEntity* Candidate) const;
	/** Slot 166 `CanStandOn(CBaseEntity*)`: `0x10397000`, the body above, as the class's override
	 *  (story 5 commit B wired it: the body stood ported under its helper name and no dispatch reached
	 *  it, so a Ming Xiao answered the entity-chain body). */
	virtual bool CanStandOn(FElysiumEntity* Other) override { return SeveredTentaclesCanStandOn(Other); }
	/** `0x10399fe0` — `CNPC_VMingXiao`'s slot-100 `TestHitboxes` override. Refuses without a model,
	 *  without `m_bHasTransformed` (+0x6678) and with fewer than 7 hitbox sets; then tests hitbox set 0
	 *  and, on a miss, sets 1..6 — each gated by family Bosses' `IsTentacleConnected(index)`
	 *  (`0x10398000`), whose index runs 0..6 across seven iterations while the set index advances by
	 *  `0xc` from `0xc` to `0x48`. Slot 100's Troika body is generated, so this species arm lands under
	 *  its own name and the report says so. */
	bool TestHitboxesMingXiao(const FVector& RayStartUnits, const FVector& RayEndUnits, uint32 Mask);

	static FMingXiaoPlayback MingXiaoPlaybackScalar(int32 Activity, bool bDisciplineArm,
		int32 TentacleCount, TFunctionRef<float(int32)> TuningField);

	// From `ElysiumNpcGeometry.inl`.
	/** `FUN_10397e00` — one severed tentacle moved; tell the owner's OTHER severed tentacles where it
	 *  is. Walks `m_rhSeveredTentacles[6]` (`+0x66a8`, family **Squad**'s member), skips an unresolved
	 *  handle and skips `Moved` itself, and hands each survivor `Moved`'s own `GetAbsOrigin()`. A null
	 *  `Moved` does nothing, which is retail's first test.
	 *
	 *  The name is 29c's overlay target. `0x1039ef60` is the entry point above it: it resolves the
	 *  moved tentacle's `m_hMingXiao` (`+0x665c`) and calls this ON THE OWNER, which is why the scatter
	 *  centre handed out is the MOVED entity's position and not this NPC's. */
	void NotifyOwnedCopiesOfOwnerMove(FElysiumEntity* Moved);
	/** `FUN_103998d0` — `CNPC_VMingXiao::CoordinateTroops`'s severed-tentacle half, which the same
	 *  overlay row names. Named by address because the behaviour is not the one above: it scatters ONE
	 *  tentacle away from ME, and only when every gate holds —
	 *
	 *    * the tentacle's `m_iForcedSchedule` (`+0x65c8`) is neither `0x163` nor `0x165`;
	 *    * its `m_ePhase` (`+0x6670`) is exactly 2;
	 *    * the distance from me to it is at most `_DAT_1046dcd0` = 128 Source units;
	 *    * the 2-D dot of the unit direction with `m_vecForward` (`+0x6290`) is at least
	 *      `_DAT_10449260`, which is a **DOUBLE** and reads **0.25** — read as a float that cell is
	 *      0.0 and the gate would admit the whole forward half-plane. */
	void FUN_103998d0(FElysiumEntity* Tentacle);
	/** The gate above as a pure rule, so the 128 and the 0.25 are measurable without a world.
	 *  `DeltaCm` is the tentacle's origin minus mine; `Forward` is `m_vecForward`. */
	static bool ScatterTentacleGate(const FVector& DeltaCm, const FVector& Forward);

	// From `ElysiumNpcMotor.inl`.
	// +0x6750 `CNPC_VMingXiao::m_bBlockedByFriend` — the one-field state `0x1039aaf0` writes and
	// `0x1039ab10` reads. Census name only; no other retail body in layers 0–9 touches it.
	bool bBlockedByFriend = false;
	/** `0x1039aaf0` / `0x1039ab10` — `CNPC_VMingXiao::m_bBlockedByFriend`'s setter and getter. */
	void SetBlockedByFriend(bool bBlocked);
	bool BlockedByFriend() const;
	/** `CNPC_VMingXiao::MaxYawSpeed` `0x10394930` — the tuning record's +0x48 in the 0x112a–0x112d band
	 *  and +0x44 elsewhere, read through the record seam. */
	static float MaxYawSpeedMingXiao(int32 Activity, TFunctionRef<float(int32)> TuningField);

	/** `+0x66f4 CNPC_VMingXiao::m_rflRegrowTimers[6]` — the six tentacle regrow stamps
	 *  `CNPC_VMingXiao::Save` brackets the archive with. SIX is the loop bound in both bodies
	 *  (`iVar1 = 6`), and `ElysiumNpcKernelShape.cpp` gives the array a 4-byte stride. */
	static constexpr int32 MingXiaoRegrowTimerCount = 6;
	double MingXiaoRegrowTimers[MingXiaoRegrowTimerCount] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

	// From `ElysiumNpcSquad.inl`.
	int32 CoordinateTentacleId = 0;  // +0x6740 CNPC_VMingXiao::m_iCoordinateTentacleID (datamap)
	FElysiumEntityHandle Proxies[6];  // +0x668c CNPC_VMingXiao::m_rhProxies[6] (datamap)
	FElysiumEntityHandle SeveredTentacles[6];  // +0x66a8 CNPC_VMingXiao::m_rhSeveredTentacles[6]
	/** `CNPC_VMingXiao::CoordinateTroops` (`0x10399610`) — one severed tentacle and one proxy per
	 *  call, round-robin over six. */
	void CoordinateTroops();

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcBosses.inl`.
	/** SEAM for `0x10397930`, the sever: `m_rhSeveredTentacles[id] = -1`, `m_rbProxyRegistered[id] = 0`,
	 *  `m_rhProxies[id] = -1`, `m_rflHitPoints[id] = Tuning[4]`, `m_rflAttackTimers[id] = curtime +
	 *  _DAT_1044e664`, `m_rflRegrowTimers[id] = curtime + 1.0` and a bodygroup set. `m_rhProxies` and
	 *  `m_rhSeveredTentacles` are family Squad's members; `0x10397930` is no family's row, so the two
	 *  writes this substrate CAN make are made and the rest is recorded. */
	void SeverTentacle(int32 TentacleId);

	// --- 0019/8 lane L07, Conditions19 ---------------------------------------------------------
	/** `+0x6680 CNPC_VMingXiao::m_hRangedWeapon` (datamap). Its writer is `Spawn`'s residue, so the
	 *  spit arm of `0x10394e40` stays closed until it lands. */
	FElysiumEntityHandle MingXiaoRangedWeapon;
	/** SEAM for the weapon's slot 364 `WeaponLOSCondition(ownerPos, targetPos, false)` (`0x1024f330`,
	 *  `RET 0xc`). No weapon-side line test stands on the kernel; answers true, the clear line. */
	bool MingXiaoWeaponLosCondition(FElysiumEntity* Weapon, const FVector& FromCm, const FVector& ToCm);

	// --- 0019/8 Damage19 (lane L09): `OnTakeDamage_Alive` `0x10395ae0`'s helpers -------------
	/** `FUN_10395650(this, info, weapon)` — the hit-to-tentacle index. A `weapon` whose slot-360 word
	 *  carries `0x18000` defers to `FUN_103952b0`; otherwise `m_LastHitGroup` (`+0x1594`) maps
	 *  1->-2, 4->1, 5->0, 6->5, 7->4, 8->3, 9->2, anything else -> -1. */
	int32 MingXiaoHitTentacleIndex(const FElysiumEntity* Weapon);
	/** `FUN_103952b0` — the melee arm of the same map: the hitgroup's own tentacle when it is still
	 *  connected (`0x10398000`), falling through to its neighbours; a hitgroup outside 1 and 4..9
	 *  draws `RandomInt(0,99)` against the tuning record's `+0x2c` cell and walks 1,0,3,2,5,4. */
	int32 MingXiaoMeleeTentacleIndex();
	/** `FUN_101e8da0(0x10739d08)+0x2c`, the melee-spread chance `0x103952b0` draws against: the
	 *  `Ming_Xiao_Info/General` "MeleeTentacleHitPercent" row (`0x101e7405`, int, default 20), read
	 *  through `Select19MingXiaoTuningField`. */
	int32 MingXiaoMeleeSpreadChance() const;

	// --- 0019/8 Boss19 (lane L12): the head's death, tentacle and damage helpers ---------------
	//
	// No slot holds any of these. Callers: `Event_Killed` `0x10395ba0` (the death and the two
	// sweeps), `OnTakeDamage_Alive` `0x10395ae0` (the damage router), both other lanes' rows.
	// Bodies in `ElysiumNpcMingXiao.cpp`; walked prose in `docs/vtmb/npc-ai/story8/Boss19.md`.

	/** `+0x6744 CNPC_VMingXiao::m_bPlayedDeathAnim` (walked) — the latch `0x10395ce0` tests. */
	bool bMingXiaoPlayedDeathAnim = false;
	/** `+0x6748 CNPC_VMingXiao::m_flIdealRange` — written by the spawner from `0x103986b0`, read by
	 *  `ResolveTaskDistance` (`0x10392a10`). */
	float MingXiaoIdealRange = 0.f;
	/** `+0x6714 CNPC_VMingXiao::m_eLastLostTentacle` — the index the spawner is handed. */
	int32 MingXiaoLastLostTentacle = 0;
	/** `+0x66dc CNPC_VMingXiao::m_rflHitPoints[6]` — each tentacle's remaining hit points. */
	float MingXiaoHitPoints[6] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };

	/** `0x10395c70` — the death entry: stamp `NPC_VMingXiao.cpp:0x916`, install `0x16e` (proxy) or
	 *  `0x16d` FORCED past the `IsAlive` gate, then `m_lifeState` = 1, `m_bPlayedDeathAnim` = 1 and
	 *  `m_bInvincible` = 1, in that order. The checklist's `MingXiaoEnterDeath`. */
	void MingXiaoEnterDeath();
	/** `0x10395ce0` — `if (!m_bPlayedDeathAnim) MingXiaoEnterDeath();` (a jump), what the sweep
	 *  `0x10397f00` calls on each proxy. The ledger's (and lane L11's) `BeginDefeatSequenceOnce`. */
	void BeginDefeatSequenceOnce();
	/** `0x10397e90` — every live handle in `m_rhSeveredTentacles[6]` (`+0x66a8`) starts its death
	 *  (`0x1039ea60`). Six, hard-coded; no handle is cleared. */
	void MingXiaoKillTentacles();
	/** `0x10397f00` — every live handle in `m_rhProxies[6]` (`+0x668c`) starts its death
	 *  (`0x10395ce0`). */
	void MingXiaoKillSpawnedBodies();
	/** `0x10397410` — tentacle `TentacleIndex` is lost: install `0x16c`, release a held throwable,
	 *  burst particles, search a clear spot around the limb's bone and have the map's
	 *  `TentacleGenerator` maker make the crawling tentacle there, link it back and mark the limb
	 *  severed. Answers the new tentacle, or null. Retail's argument is a `float` register used as the
	 *  integer index throughout. */
	FElysiumNpc* MingXiaoSpawnTentacle(int32 TentacleIndex);
	/** `0x10395750` — the hitgroup damage router `OnTakeDamage_Alive` runs on its packet copy:
	 *  `TentacleIndex` is `0x10395650`'s answer (`-2` the head, `-1` none, `0..5` a limb). The
	 *  signature is lane L09's `MingXiaoApplyTentacleDamageSeam`'s, so the call binds as is. */
	void MingXiaoApplyTentacleDamage(int32 TentacleIndex, FElysiumTakeDamageInfo& Info,
		const FElysiumEntity* Weapon);
	/** `0x103986b0` — the ideal range from the four attack limbs still connected: 400 for each of
	 *  0 and 1, 300 for each of 2 and 3, averaged over twice the count (retail adds 2 per limb and
	 *  one term per limb); 300 when none is. Not a row; three lines the spawner calls. */
	float MingXiaoIdealRangeFromLimbs() const;
	/** `0x10398680` — the limb's bone name, from the six-entry table at `0x106433ac`. Its guard
	 *  `(i < 0) && (5 < i)` can never hold (retail defect, reproduced): an out-of-range index reads
	 *  past the table, which the port refuses by answering the first entry (NAMED CRASH GUARD). */
	static const TCHAR* MingXiaoLimbBoneName(int32 TentacleIndex);
	/** SEAM for `0x10398630` — `LookupBone(name)` (bone 0 when it fails) then
	 *  `GetBonePosition(bone)`: no bone sampler reaches the kernel, so this answers the origin, the
	 *  root bone's own frame. SOURCE units. */
	FVector MingXiaoLimbBonePositionUnits(int32 TentacleIndex) const;
	/** SEAM for the particle dispatches `0x102c42a0` (at an entity's attachment) and `0x102c41d0`
	 *  (at a point and angles): visual-only, recorded with their arguments. */
	struct FMingXiaoParticleRequest
	{
		FString Effect;
		FString Attachment;
		FVector PositionUnits = FVector::ZeroVector;
	};
	TArray<FMingXiaoParticleRequest> MingXiaoParticleRequests;
	/** Slot 360 (`+0x5a0`) on the damaging WEAPON, a `CBaseCombatWeapon` word the router masks with
	 *  `0x18000`: answered from the weapon record's family (melee -> `0x18000`), the reading L09's
	 *  `0x10395650` port makes of the same bits. */
	uint32 MingXiaoWeaponSlot360(const FElysiumEntity* Weapon) const;
	/** SEAM for `0x1023e4b0(this, amount)` — a 25-slot global per-entity queue of (handle, int
	 *  amount) records with a cvar-timed expiry, which the router feeds when the head is not
	 *  invincible. Its consumer is unrecovered; the amounts are recorded here. */
	TArray<int32> MingXiaoQueuedBodyDamage;
	/** The `TentacleGenerator` maker's name (`0x10390d00` binds the static maker reference
	 *  `DAT_1093bb9c` to it). */
	static const TCHAR* MingXiaoTentacleMakerName();
	// --- 0019/8 Spawn19 (lane L08): words and helpers the family's bodies need (three searches each
	// in the L08 report) ---
	FElysiumEntityHandle MingXiaoParentMingZhao;   // +0x6670 CNPC_VMingXiao::m_hParentMingZhao (datamap)
	/** SEAM for `CBaseCombatCharacter::GetBestMeleeWeapon` (`0x10336f20`). No inventory pick stands for
	 *  an NPC here; answers null, retail's own "no melee weapon" arm (`m_hMeleeWeapon = -1`). */
	FElysiumEntity* Spawn19BestMeleeWeapon();

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual void Event_Killed(void* Arg0) override;
	virtual int32 OnTakeDamage_Alive(void* Arg0) override;
	virtual void NPCThink() override;
	virtual void GatherConditions() override;
	virtual int32 PreSelectSchedule() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;
	// --- Lane L05 (story 8 RunTask19)
	// (`+0x6680 m_hRangedWeapon`, the weapon `TASKS 0x14f..0x152` switch back to, is L07's
	// `MingXiaoRangedWeapon` above.)
	/** `FUN_1039aa20` `0x1039aa20` -- `TASK 0x14b`'s whole arm. **SEAM**: counted. */
	int32 MingXiaoTask14bCalls = 0;
	void MingXiaoTask14b();
	/** `FUN_10398db0` `0x10398db0` -- the tentacle GRAB (the `phys_animlink` on
	 *  `Bip01_[RL]_ThrowingTenticle5`, then mode 3) `TASK 0x158` runs outside mode 3. **SEAM**: counted
	 *  on family Misc19's `MingXiaoGrabCalls`, the one recorder for this address (L05 integration). */
	void MingXiaoTentacleGrab();

	// --- 0019/8 L04 (StartTask19 species): private helpers ---
	// `+0x6680 m_hRangedWeapon`, which `StartTask` `0x10392d80` task `0x155` switches to, is lane
	// L07's `MingXiaoRangedWeapon` above.
	/** SEAMS for the `StartTask` helpers no row of this lane owns: `0x1039a750` (the transform),
	 *  `0x10398630` (a tentacle's bone position) and the `0x102c4310`/`0x102c42a0` emitters.
	 *  (`0x10395ce0` is `BeginDefeatSequenceOnce` above, lane L12's body.) */
	void FUN_1039a750();
	bool MingXiaoTentacleBonePosition(int32 Tentacle, FVector& OutCm) const;
	void MingXiaoEmitter(const TCHAR* Name, const TCHAR* Attachment);
	int32 Fun1039a750Calls = 0;
	TArray<FString> MingXiaoEmitters;
};
