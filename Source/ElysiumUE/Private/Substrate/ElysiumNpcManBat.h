#pragma once

#include "Substrate/ElysiumNpcVampire.h"

// `CNPC_VManBat` (primary vtable `0x104bbaec`), built by `npc_VManBat` factory `0x10389c50`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcManBat : public FElysiumNpcVampire
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VManBat", FElysiumNpcVampire)

	// The pickup chain's row for this class: the attach body `0x1038f430`, the release body `0x1038f790`,
	// the carrier bone, the `m_hPickupTarget` offset and the collision-ignore re-arm.
	static const FPickupSpecies& PickupRow();
	/** `0x1038f430` — the attach: the shared head, the element at `ElementKey` (`param_2`, no cast,
	 *  no decode), the shared tail, then `m_hPhysicsAnimlink = link`, `CARRYING_BODY` on
	 *  (`0x1038f600`), `m_bPickupTargetBreakable = IsBreakable(param_1)` and `SetBreakable(param_1,
	 *  false)`. */
	bool AttachPickupAnimlink(FElysiumEntity* Carried, int32 ElementKey);
	/** `0x1038f790` — the release: the shared throw toward `m_hClosestPlayer` (`+0x628c`; the
	 *  argument is ignored), restoring the carried thing's breakable latch after it, then
	 *  `StartIgnoringCollision(carried)` (`0x102c4380`), the re-arm (`0x102c43b0`, 2.0 s),
	 *  `m_hPickupTarget = -1` AFTER the re-arm, and `CARRYING_BODY` off. */
	void ReleasePickupAnimlink(const FElysiumEntity* AimTarget);

	// The constructor `0x10389cc0`: its hull store (`docs/vtmb/data/class_hulls.json`).
	FElysiumNpcManBat();

	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 GetUsedHullBits() override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcBosses.inl`.
	// `CNPC_VManBat`'s own words.
	bool bManBatReachedMoveGoal = false;   // +0x6664 m_bReachedMoveGoal
	int32 ManBatMoveGoalNodeMode = 0;      // +0x6668 m_iMoveGoalNodeMode, DECODED
	int32 ManBatMoveGoalNodeId = 0;        // +0x6674 m_iMoveGoalNodeID
	double ManBatFlapTimer = 0.0;          // +0x6678 m_flFlapTimer, an absolute curtime deadline
	FElysiumEntityHandle ManBatFlyNode;    // +0x6688 m_pFlyNode — a raw `CBaseEntity*` in retail
	FElysiumEntityHandle ManBatPickupTarget;      // +0x668c m_hPickupTarget
	FElysiumEntityHandle ManBatPhysicsAnimlink;   // +0x6690 m_hPhysicsAnimlink
	bool bManBatPickupTargetBreakable = false;    // +0x6694 m_bPickupTargetBreakable
	FElysiumEntityHandle ManBatFlyByTarget;       // +0x66ac m_hFlyByTarget
	/** SEAM for `0x102d1af0` with `(20000, 0, 15000.0, 0, 0)` — the hint search `0x1038b370` runs while
	 *  it has `m_iMoveGoalNodeMode` temporarily forced to 2. Family **Hints** owns `FindHintNear` over
	 *  the same absent store; this is its `FElysiumEntity*`-answering form, because `0x1038b370` reads
	 *  the found hint's ORIGIN. Answers null, which is the arm that zeroes the output velocity. */
	FElysiumEntity* ManBatFindMoveGoalHint(int32 HintType, float RadiusUnits);
	TArray<FTeleportEmitterPlacement> TeleportEmitterPlacements;
	void PlaceNamedEmitter(const TCHAR* Name, const FVector& PositionUnits);
	/** SEAM for `thunk_FUN_101cf5c0(this, &position)` — the teleport `0x1038b370` performs once both
	 *  emitters are placed. Recorded in SOURCE units; the entity is not moved, because the flight body
	 *  is a velocity producer and moving the body from inside it would be a second answer to where this
	 *  NPC is. */
	bool bManBatTeleportRequested = false;
	FVector ManBatTeleportPositionUnits = FVector::ZeroVector;
	/** SEAM for `CBaseEntity::CalcAbsoluteVelocity` (the `+0x268 & 0x1000` dirty-velocity arm) and for
	 *  `m_vecAbsVelocity` (+0x3bc), which `0x1038b370` reads as the animation-driven velocity. This
	 *  runtime carries `FElysiumEntity::Velocity` in CENTIMETRES per second; this answers it in SOURCE
	 *  units, which is what every constant in that body is in. */
	FVector AbsVelocityUnits() const;
	/** SEAM for `thunk_FUN_102f1a20(m_pNavigator, &position, 0x2400b)` — the navigator's "is this
	 *  destination reachable" probe `0x1038b370` runs before it will chase a fly-by target, and for the
	 *  two `m_Collision` reads (`+0x270` slots 4 and 8, the OBB mins and maxs) it offsets that position
	 *  by. Family Motor states the same navigator gap. Answers false, which is retail's REFUSAL arm —
	 *  `TaskFail(0x1a)` and the plain velocity fallback. */
	bool NavigatorCanReach(const FVector& PositionUnits) const;
	TArray<FPhysicsTraceEntityCall> PhysicsTraceEntityCalls;
	/** `0x1038b370` — `CNPC_VManBat`'s velocity producer, the biggest body in this family (2,274 bytes).
	 *  Three shapes, chosen by `m_pFlyNode` and the decoded `m_iMoveGoalNodeMode`: the animation-driven
	 *  velocity steered by the obstacle probe; the fly-node / fly-by-target homing with its 700 and 500
	 *  unit speeds, its acceleration clamp and its overspeed latch on `m_bReachedMoveGoal`; and the
	 *  `sheriff_teleport_emitter` teleport the stationary watchdog fires. `Interval` is retail's
	 *  `param_2` and `OutVelocityUnits` its `param_1`, both SOURCE units. */
	void FUN_1038b370(float Interval, FVector& OutVelocityUnits);
	static FManBatStationaryWatch& ManBatStationaryWatch();
	/** `0x1038bec0` — the hull probe `0x1038b370` steers by. Sweeps this NPC's own collision box from
	 *  `GetAbsOrigin()` along `DirUnits * Speed * 0.1` with mask `0x202400b`; a fraction under 1.0
	 *  answers the straight-up steer `(0, 0, 1)` and, unless `m_Activity` (+0x0fec) is already `0x22`,
	 *  stamps `m_flFlapTimer` with `curtime`; a clear sweep answers `vec3_origin` and false. */
	bool FUN_1038bec0(const FVector& DirUnits, float Speed, FVector& OutSteerUnits);
	/** `0x1038e640`, `0x1038e670`, `0x1038e6a0` and `0x1038e6e0` — four bodies that are one behaviour:
	 *  `SetIdealActivity(act)` then `m_flFlapTimer = curtime + T`. The table below is the whole of the
	 *  difference between them. */
	void SetFlapActivity(int32 InActivityNumber, float Seconds);
	static const FFlapActivity* FlapActivityRows(int32& OutCount);
	static const FFlapActivity* FlapActivityOf(const TCHAR* Body);
	/** `0x1038fb20` — `CNPC_VManBat`'s slot 102 `Physics_TraceEntity`. The recovered concern is the
	 *  FILTER: the body builds the ordinary `CTraceFilterSimple` and then overwrites its vtable pointer
	 *  with `vftable_CTraceFilterManBatNoIBeamEntity` before sweeping. Slot 102's Troika-line body
	 *  (`0x100ab450`) is another story's, so this lands as a named method. */
	void PhysicsTraceEntityManBat(FElysiumEntity* Entity, const FVector& StartUnits,
		const FVector& EndUnits, uint32 Mask);
	/** The `DAT_1093b7cc` cvar `0x1038b370` multiplies by the think interval to get its per-axis
	 *  acceleration clamp (retail's inlined `ConVar::GetFloat`): `manbat_delta`, shipped "600.0". */
	float ManBatAccelerationCvar() const;

	// From `ElysiumNpcDamage.inl`.
	/** `0x1038f2c0` — `CNPC_VManBat::ThrowModel(const char* model, const char* parentName)`, named by
	 *  its own `DevMsg` literal `"ManBat is throwing model %s"`. Creates a `prop_physics` at this NPC's
	 *  origin without spawning it, sets its model, spawns it, looks a bone up by the SAME string, makes
	 *  a corpse-shaped ragdoll from it (`thunk_FUN_10157da0`), removes the template prop, then arms the
	 *  ragdoll's think at `curtime + 20.0` (`_DAT_1044eb0c`), optionally parents/owns it to `parentName`
	 *  and finally attaches it through family Bosses' ManBat animlink arm before storing its handle in
	 *  `m_hPickupTarget` (+0x668c, Bosses' `ManBatPickupTarget`). */
	bool ThrowModel(const FString& ModelName, const FString& ThrowParentName);

	/** `+0x6670` — `CNPC_VManBat`'s SCRAMBLED mode word. Retail never reads it plainly: every reader
	 *  runs the XOR/AND ladder at `1038e49b`..`1038e4b9` and then `0x1042fbf0` over it. Declared by
	 *  retail offset, as family Hints declares the Werewolf's species words. */
	uint32 ManBatHintModeWord = 0;    // +0x6670 CNPC_VManBat (walked)
	/** `+0x6674` — the plain index the four `%d` templates print. */
	int32 ManBatHintIndex = 0;        // +0x6674 CNPC_VManBat (walked)
	/** The descramble, verbatim: the caller-side ladder at `1038e49b` and then `0x1042fbf0`, whose whole
	 *  body is one more XOR/AND fold. Answers retail's `EAX` before the `DEC`/`CMP 7` range test. */
	static uint32 ManBatHintMode(uint32 ScrambledWord);
	/** The five name templates, by decoded mode. Mode 1 is copied RAW (a byte loop at `1038e4f7`, no
	 *  `sprintf`), 2 and 4 share one format, 3 and 8 have their own, and 5/6/7 and everything outside
	 *  1..8 take the default — the jump table at `0x1038e5c0` sends 5, 6 and 7 to the default label.
	 *  Every string is the pinned image's, read at its `.rdata` address. */
	static FString ManBatHintName(uint32 Mode, int32 Index);
	/** `CNPC_VManBat::FValidateHintType` (`0x1038e480`), 317 bytes — slot 566's `CNPC_VManBat` arm.
	 *  The built name is matched against the hint's `m_iName` (`+0x26c`) with `__strcmpi`, or with
	 *  `__strnicmp` over `strlen-1` characters when the template's last character is `*`. Retail's
	 *  EMPTY-NAME arm is reproduced as retail wrote it: a zero-length template compares the hint's NAME
	 *  POINTER against zero, so an unnamed hint matches and a named one does not. */
	bool ManBatValidateHintType(const FHintWords& Hint) const;

	// From `ElysiumNpcLifecycle19.inl`.
	static constexpr int32 HullIndexManBat = 0x14;       // `0x1038b070`
	static constexpr double ManBatFlapDelaySeconds = 2.3;    // `_DAT_104bc690`
	/** `CNPC_VManBat` species words. `ManBatFlapTimer` and `bHasPlayedFlyBySound` already exist. */
	bool bManBatHasScaredMinions = false;        // +0x66b0
	double ManBatFlyTimer = 0.0;                 // +0x667c

	// From `ElysiumNpcMisc.inl`.
	float ManBatSlowedExpire = 0.f;          // +0x6684 CNPC_VManBat::m_flSlowedExpire
	/** `0x1038f290` — is `CNPC_VManBat`'s slow effect still running? `m_flSlowedExpire` (`+0x6684`)
	 *  strictly above `_DAT_1044fab0`, the shared `0.0` DOUBLE (`docs/vtmb/footsteps.md:319`, the
	 *  2-D speed gate). A flag test spelled as a float compare, not a deadline against curtime. */
	bool SlowedExpire() const;

	// From `ElysiumNpcSounds.inl`.
	// `CNPC_VManBat::m_bHasPlayedFlyBySound` (`+0x66b8`, `FIELD_BOOLEAN`). A SPECIES word: `+0x66b8` is
	// claimed by four different classes in the census and this is CNPC_VManBat's reading of it.
	bool bHasPlayedFlyBySound = false;
	// `FUN_10390040` (`0x10390040`), whose whole body is `this->m_bHasPlayedFlyBySound = false`. It has
	// no caller in the image — the fly-by sound that would set it is unrecovered — so this is the state
	// reset and nothing more.
	void ClearHasPlayedFlyBySound();

	// From `ElysiumNpcSpeciesMisc10.inl`.
	/** `CNPC_VManBat::StartScreechCone` (`0x1038e9c0`), 1283 bytes, no vtable slot; one direct caller,
	 *  `StartTask` (`0x1038c390`), once with the resolved enemy and once with null. Retail name
	 *  unrecovered; named for what it does. */
	void ManBatStartScreechCone(FElysiumEntity* Target);
	/** `CNPC_VManBat::ReleaseSlowedEntity` (`0x1038f020`), the slowed-grab teardown: `RunAI`
	 *  (`0x1038e990`) calls it with `force = 0` and `Event_Killed` (`0x1038e8c0`) with `force = 1`. */
	void ManBatReleaseSlowedEntity(bool bForce);
	FElysiumEntityHandle ManBatSlowedEntity;     // +0x6680 `m_hSlowedEntity`
	FElysiumEntityHandle ManBatPlayerEmitter;    // +0x669c `Manbat_player_emitter`
	FElysiumEntityHandle ManBatHudEmitter;       // +0x66a0 `HUD_Manbat_emitter`
	FElysiumEntityHandle ManBatScreechCone;      // +0x66a4 `Manbat_screechcone_emitter`
	FElysiumEntityHandle ManBatBlastEmitter;     // +0x66a8 `Manbat_blast_player`
	TArray<FPushEntityCall> PushEntityCalls;
	/** SEAM for `player->+0x2454` bit 0 — the word the ManBat cone ORs on and its teardown clears. The
	 *  retail field has no name in the corpus and no port counterpart; it is carried here so both
	 *  bodies' writes are observable and paired. */
	bool bPlayerScreechConeBit = false;

	// --- Story 0019/8, family Script19 (`ElysiumNpcScript19Species.cpp`) ---------------------------
	/** `0x1038b1a0` (`ManBatOverrideMoveFly`, name coined), slot 525's body at navigator state 2: the
	 *  `manbat_stun` bail into schedule `0x15b`, else the interval clamp, the `0x1038b370` velocity
	 *  through `SetAbsVelocity`, and -- outside four activities -- the motor yaw, the velocity pitch and
	 *  the wing/turn selector once `m_flFlapTimer` is due. `Interval` is seconds. */
	void ManBatOverrideMoveFly(float Interval);
	/** `0x1038e720` -- the wing/turn selector over the flight velocity (SOURCE units, this world's
	 *  axes): refuses five activities; `vel.z >= 30` flaps (`0x1038e640`); a yaw turn in `[30, 330]`
	 *  takes `0x1038e6a0` below 180 and `0x1038e6e0` above; otherwise activity `0x24` flaps and anything
	 *  else glides (`0x1038e670`). Its callers are `0x1038b1a0` and RunTask `0x1038daa6` (`TASK 0x15c`). */
	void ManBatWingTurnSelect(const FVector& VelocityUnits);
	/** `cvar_manbat_stun` (`0x1093b858`, "manbat_stun", default "0", `FUN_1038ae50`): its `m_nValue`
	 *  (`+0x2c`). SEAM: the kernel ConVar table (`ElysiumNpcKernelTunables.h`, generated) has no row
	 *  for it; the value lives here, and `0x1038b1a0` clears it through `ConVar::SetValue(0)`. */
	static int32& ManBatStunConVar();
	/** `CNPC_VManBat`'s own schedule `0x15b` (`InputManBatStun 0x1038fa50` pushes the same id), the
	 *  stun program `0x1038b1a0` installs through `0x102ae750`. */
	static constexpr int32 ManBatStunScheduleRetailId = 0x15b;
	/** `UTIL_VecToYaw` (`0x101d2c70`) and `UTIL_VecToPitch` (`0x101d2ce0`) over a vector in this
	 *  world's axes (Source's Y is this world's -Y). */
	static float ManBatVecToYaw(const FVector& PortVector);
	static float ManBatVecToPitch(const FVector& PortVector);

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual void Event_Killed(void* Arg0) override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;
	virtual int32 OnTakeDamage_Alive(void* Arg0) override;
	virtual void RunAI(bool Arg0) override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;
	// --- Lane L05 (story 8 RunTask19): what `CNPC_VManBat::RunTask` `0x1038d130` reads that no port
	// member carried. Each is a SEAM answering retail's "nothing" arm; the three admitting searches
	// are in `pass-i/L05-report.md`.
	/** `+0x66b4 m_flCoastTimer` (absolute curtime) -- `TASK 0x163` waits it out. No writer ported. */
	double ManBatCoastTimer = 0.0;
	/** `+0x6698 m_iMaxScriptNode` -- the last scripted fly node `TASK 0x15c` walks to. No writer ported. */
	int32 ManBatMaxScriptNode = 0;
	/** `FUN_1038c170(this, arg)` `0x1038c170` -- leave flight. Counted. */
	int32 ManBatLeaveFlightCalls = 0;
	void ManBatLeaveFlight(int32 Arg);
	/** `FUN_1038fe30` `0x1038fe30` -- the fly-by sound (reads `m_bHasPlayedFlyBySound`). Counted. */
	int32 ManBatFlyBySoundCalls = 0;
	void ManBatFlyBySound();
	// (`FUN_1038e720(this, vel)`, the flap/glide pick `TASK 0x15c` runs, is `ManBatWingTurnSelect`
	// above -- the L05 integration dropped this lane's recording seam.)
	/** `0x100f7b20("Spotlight *", GetOrigin(), 0, 0, 0)` -- the nearest entity by wildcard name. The
	 *  port's `FindByName` has no nearest or `*` form; answers null. */
	FElysiumEntity* ManBatNearestSpotlight() const;
	/** The spotlight kill `TASK 0x15b` runs on a match: skin fade 0 then `FadeToSkin(2)`,
	 *  `CPropSwitch::InputLock`, the `"Spotlightbeam %d"` search and `0x101c4dd0` on the beam. Counted. */
	int32 ManBatSpotlightsKilled = 0;
	void ManBatKillSpotlight(FElysiumEntity& Light);
	/** The active weapon's slot 326 (`+0x518`, `PrimaryAttack`) `TASK 0x15a` fires. Counted. */
	int32 ManBatWeaponAttacks = 0;
	virtual bool OverrideMove(float Arg0) override;

	// --- 0019/8 L04 (StartTask19 species): private helpers ---
	/** The words `StartTask` writes on every flight arm: the secure mode word (both the encoded
	 *  `+0x6670` and the port's decoded carrier) and `m_iMoveGoalNodeID` (both port carriers). */
	void ManBatSetMode(int32 Mode);
	void ManBatSetMoveGoalNodeId(int32 NodeId);
	/** `0x102d1af0(this, 20000, type, 5000.0, 0, 0)` into `m_pFlyNode` (+0x6688). */
	FElysiumEntity* ManBatFindFlyNode(int32 HintType);
	/** `CBaseEntity::SetAbsVelocity` in SOURCE units. */
	void ManBatSetAbsVelocityUnits(const FVector& VelocityUnits);
	/** SEAMS for the ManBat helpers `StartTask` calls and no row of this lane owns: `0x1038e720` (the
	 *  flap selector), `0x1038c250`, `0x1038f660`, `0x1038c170`, `0x1038fc80`, `0x1038fd40`. */
	void FUN_1038e720(const FVector& VelocityUnits);
	void FUN_1038c250(FElysiumEntity* FlyNode);
	void FUN_1038f660();
	void FUN_1038c170(int32 Mode);
	void FUN_1038fc80();
	void FUN_1038fd40();
	TArray<FVector> ManBatFlapSelectorCalls;
	TArray<int32> ManBatFlightSwitchCalls;
	int32 Fun1038c250Calls = 0;
	int32 Fun1038f660Calls = 0;
	int32 Fun1038fc80Calls = 0;
	int32 Fun1038fd40Calls = 0;
};
