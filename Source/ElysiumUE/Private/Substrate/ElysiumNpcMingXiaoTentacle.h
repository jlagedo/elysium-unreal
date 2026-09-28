#pragma once

#include "Substrate/ElysiumNpc.h"

// `CNPC_VMingXiaoTentacle` (primary vtable `0x104bdea4`), built by `npc_VMingXiaoTentacle` factory
// `0x1039af30`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcMingXiaoTentacle : public FElysiumNpc
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VMingXiaoTentacle", FElysiumNpc)

	// The constructor `0x1039afe0`: its hull store (`docs/vtmb/data/class_hulls.json`).
	FElysiumNpcMingXiaoTentacle();

	virtual void Slot21(FElysiumEntity* Attacker) override;
	virtual void Slot22(FElysiumEntity* Attacker) override;
	virtual void Slot23(FElysiumEntity* Attacker) override;
	virtual void OnRestore(bool bFromLoad) override;
	virtual void Precache() override;
	virtual int32 Save(void* Archive) override;
	virtual int32 Restore(void* Archive) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 PreSelectSchedule() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual bool ShouldIgnoreCollision(FElysiumEntity* Other) override;
	virtual bool NavIgnoreCollision(FElysiumEntity* Other) override;
	virtual bool CanStandOn(FElysiumEntity* Other) override;
	virtual const TCHAR* GetShortConditionName(int32 ConditionId) override;
	virtual int32 GetUsedHullBits() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// +0x6660 m_iTentacleID (`CNPC_VMingXiaoTentacle`): the tentacle's own index, which the head
	// (`CNPC_VMingXiao::m_iTentacleID` +0x6674, -1 on the head) reads through the tentacle. 0 is
	// retail's default: entity memory is zero-allocated and no tentacle body writes the word (only
	// the head's `Spawn` `0x103927a0` writes an `m_iTentacleID`, its own, to -1). The head's carrier
	// this was split from defaulted to -1 (story 5 step 4r record).
	int32 TentacleId = 0;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcGeometry.inl`.
	/** `+0x665c CNPC_VMingXiaoTentacle::m_hMingXiao` — the owner `0x1039ede0` resolves before it can
	 *  notify the owner's other severed tentacles. Family Bosses owns `+0x665c` as
	 *  `CNPC_VBaseBoss::m_BlacklistedEntities`; same situation as `m_vecScatterCenter` above. */
	FElysiumEntityHandle TentacleMingXiao;
	/** `0x1039ef60` — that entry point, so the owner walk can be entered the way retail enters it. */
	void NotifyOwnerOfMyMove();


	// From `ElysiumNpcLifecycle19.inl`.
	/** `DAT_1093bd34` — Ming Xiao tentacle cached handle, invalidated on restore. */
	static FElysiumEntityHandle& MingXiaoTentacleCache();

	// From `ElysiumNpcMotor.inl`.
	// +0x6688 `CNPC_VMingXiaoTentacle::m_bIgnoreCollision` — the tentacle's own gate on slots 68 and 69
	// (`0x1039eb50`, `0x1039eb90`).
	bool bIgnoreCollisionSpecies = false;
	/** `thunk_FUN_1039ede0(this)` — `CNPC_VMingXiaoTentacle`'s companion/head entity, which its slot 166
	 *  excludes from the standable test. **SEAM**: the tentacle proxy chain is the Squad family's
	 *  `Proxies[6]` and nothing links a head to it yet; answers null. */
	FElysiumEntity* MingXiaoTentacleCompanion() const;

	// From `ElysiumNpcPrecache10.inl`.
	int32 ModeIndexTentacleToGrub = 0;   // +0x6664 CNPC_VMingXiaoTentacle
	int32 ModeIndexGrub = 0;             // +0x6668 CNPC_VMingXiaoTentacle
	int32 ModeIndexGrubToProxy = 0;      // +0x666c CNPC_VMingXiaoTentacle

	/** `+0x6674 CNPC_VMingXiaoTentacle::m_flPhaseExpireTimer` — the tentacle's phase deadline. */
	double MingXiaoTentaclePhaseExpireTimer = 0.0;

	// From `ElysiumNpcSpecies.inl`.
	// `CNPC_VMingXiaoTentacle`'s cached coordinate point — the three floats the head writes onto a
	// severed tentacle when it re-aims it. `+0x668c` is `CNPC_VMingXiao::m_rhProxies` on the HEAD
	// (family Squad's `Proxies`) and `CNPC_VTzimisce::m_ePathMode` on a Tzimisce (family Motor's
	// `PathMode`); the tentacle is a third class at the same offset. SOURCE units, as retail stores it.
	FVector TentacleCoordinatePosUnits = FVector::ZeroVector;   // +0x668c/+0x6690/+0x6694 (walked)
	/** SEAM for `thunk_FUN_1039ede0(this)` — the MingXiao HEAD a `CNPC_VMingXiaoTentacle` forwards its
	 *  slots 21, 22 and 23 to. Family **Motor** already stands the same retail call
	 *  (`ElysiumNpcMotor.cpp:543`) and found the tentacle proxy chain absent; this answers null,
	 *  which is retail's "no companion" arm and the one that forwards nothing. */
	FElysiumEntity* MingXiaoTentacleHead() const;
	/** SEAM for `thunk_FUN_10397dd0(head, this, param)` — `CNPC_VMingXiao`'s per-tentacle notice, which
	 *  family **Damage** declared as the body that resets `m_flSpitAttackTimer`. Records the forward so
	 *  the three slots can be told apart, and reaches nothing. */
	int32 TentacleHeadForwards = 0;
	/** `0x1039ef90` — `CNPC_VMingXiaoTentacle`: cache a coordinate point and raise condition 0x78. */
	void FUN_1039ef90(const FVector& PositionUnits);
	/** `0x1039e800` / `0x1039e830` / `0x1039e860` — `CNPC_VMingXiaoTentacle`'s slots 21, 22 and 23. */
	void FUN_1039e800(FElysiumEntity* Arg);

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcGeometry.inl`.
	/** `+0x6670 CNPC_VMingXiaoTentacle::m_ePhase` — the phase word `0x103998d0` requires to read 2
	 *  before it will scatter a tentacle. What the OTHER phase values mean is **unrecovered**; only the
	 *  2 is a fact of this family's rows. */
	int32 TentaclePhase = 0;

	// --- 0019/8 lane L07, Conditions19 ---------------------------------------------------------
	/** `+0x6678 CNPC_VMingXiaoTentacle::m_flFailedEvadeTimer` (datamap, FIELD_TIME). No port writer
	 *  yet (its writers are the tentacle's schedule bodies), so 0 keeps the evade term open. */
	double MingXiaoTentacleFailedEvadeTimer = 0.0;

	// --- 0019/8 Damage19 (lane L09): the word `OnTakeDamage_Alive` `0x1039e890` zeroes -------
	double TentacleHideReadyTimer = 0.0;         // +0x6680 m_flHideReadyTimer (datamap, FIELD_TIME)
	// --- 0019/8 Spawn19 (lane L08): words and helpers the family's bodies need (three searches each
	// in the L08 report) ---
	double TentacleIgnoreCollisionTimer = 0.0; // +0x6684 m_flIgnoreCollisionTimer (datamap, FIELD_TIME)
	bool bTentacleHitGroundSound = false;      // +0x6698 m_bHitGroundSound (datamap)
	/** SEAM for `thunk_FUN_103979d0(head, this)` -- the Ming Xiao head's own handling of a dying
	 *  tentacle (`Event_Killed` `0x1039e919`). No port body; counted. */
	void Spawn19NotifyHeadOfDeath(FElysiumEntity* Head);
	int32 Spawn19HeadDeathNotices = 0;

	// --- 0019/8 Boss19 (lane L12) --------------------------------------------------------------
	/** `+0x6699 CNPC_VMingXiaoTentacle::m_bPlayedDeathAnim` (walked) — `0x1039ea60`'s latch. */
	bool bTentaclePlayedDeathAnim = false;
	/** `0x1039e970` — the tentacle's death entry: the program and trace line picked by `m_ePhase`
	 *  (`+0x6670`), installed FORCED, then `m_lifeState` = 1, `+0x6699` = 1, `m_bInvincible` = 1.
	 *  Called by `Event_Killed` `0x1039e900` (lane L08) and through `0x1039ea60`. */
	void MingXiaoTentacleEnterDeath();
	/** `0x1039ea60` — `if (!+0x6699) MingXiaoTentacleEnterDeath();`, a jump. The ledger's (and lane
	 *  L11's) `BeginTentacleDefeatOnce`. */
	void BeginTentacleDefeatOnce();
	/** `0x1039f310` — the tentacle's death sound: `EmitSound` through a `CPASAttenuationFilter` at
	 *  slot 222 `GetSoundEmissionOrigin`, channel 2, volume 1.0, attenuation 0.8, pitch 100, sample
	 *  `RandomInt(0, 0)` of the one-entry table at `0x106477cc`. */
	void MingXiaoTentacleDeathSound();
	/** SEAM for `0x10295460(this, activity, false)` — the activity's sequence, `-1` when the model
	 *  has none. The port's activity vocabulary is names, not retail ids; answers -1, as family
	 *  FrenzyShadow's `SequenceForActivity` does for the same call. */
	int32 TentacleSequenceForActivity(int32 RetailActivity) const;

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual void Event_Killed(void* Arg0) override;
	virtual int32 OnTakeDamage_Alive(void* Arg0) override;
	virtual void RunAI(bool Arg0) override;

	// --- RunAi19 (story 0019/8 lane L13a): what slot 432 `0x1039e3d0` reads and calls ----------
	// `m_flIgnoreCollisionTimer` (+0x6684) and `m_bHitGroundSound` (+0x6698), both `CNPC_VMingXiaoTentacle`
	// datamap words the species shape map still lists ABSENT (integrator: flip the two rows, and the
	// stale `+0x667c` row, to these members).
	double TentacleIgnoreCollisionTimer = 0.0;   // +0x6684 m_flIgnoreCollisionTimer (datamap)
	bool bTentacleHitGroundSound = false;        // +0x6698 m_bHitGroundSound (datamap)
	/** `0x1039f030` -- the landing thud: `EmitSound(CPASAttenuationFilter(slot 222, 0.8), entindex,
	 *  CHAN_BODY 4, table 0x106477c8[RandomInt(0, 0)], 1.0, 0.8, 0, 100)` -- `tentacle_hit_ground.wav`. */
	void TentacleHitGroundSound();
	/** `0x1039f1a0` -- the flop loop's start: the same emit on `CHAN_VOICE` 2 over the one-entry table
	 *  `0x106477cc`, `tentacle_flopping_loop.wav`, flags 0 (`0x1039f310` stops it at death). */
	void TentacleFloppingSound();
	virtual void GatherConditions() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;
	// --- Lane L05 (story 8 RunTask19)
	// `DAT_1093bd34`, the cursor `TASK 0x150` resumes its type-19000 hint walk from, is family
	// Lifecycle19's `MingXiaoTentacleCache()` (the same global, invalidated on restore).
	/** `FUN_1039ee20(this, &pos)` `0x1039ee20` -- the doubled-hull (X/Y) clearance test at a hint:
	 *  `IsAreaClear(pos, 0x202400b, 2*mins(15), 2*maxs(15))`. (`TASK 0x150`'s goal goes through lane
	 *  Script19's `Script19SetGoal`.) */
	bool TentacleHintClear(const FVector& PositionCm) const;

	// --- 0019/8 L04 (StartTask19 species): private helpers ---
	/** `+0x667c CNPC_VMingXiaoTentacle::m_flUpdateEvadeTimer` -- the evade re-arm deadline tasks `0x14f`
	 *  (`RandomFloat(1, 2)` ahead), `0x151` and `0x152` (600 ahead) write. Walked from `0x1039c4c0`. */
	double TentacleUpdateEvadeTimer = 0.0;
	// `+0x6680 m_flHideReadyTimer` (task `0x151`'s `RandomFloat(5, 15)` ahead) is the Damage19
	// member `TentacleHideReadyTimer` above.
	/** SEAM for `0x102c41d0(this, name, &position, 0, 0)` -- the named particle emitter; recorded. */
	void TentacleCreateEmitter(const TCHAR* Name, const FVector& PositionUnits);
	TArray<FTeleportEmitterPlacement> TentacleEmitterPlacements;
	/** SEAM for `0x1039f310` (task `0x14a`'s phase-change teardown); counted. */
	void FUN_1039f310();
	int32 Fun1039f310Calls = 0;
	/** SEAM for `0x1039ef10` (task `0x14e`); counted. */
	void FUN_1039ef10();
	int32 Fun1039ef10Calls = 0;
	// `0x1039ea60` (task `0x156`) is `BeginTentacleDefeatOnce` above, lane L12's body.
};
