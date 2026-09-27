#pragma once

#include "Substrate/ElysiumNpcVampireBoss.h"

// `CNPC_VSabbatLeader` (primary vtable `0x104c3d64`), built by `npc_VSabbatLeader` factory
// `0x103a5ae0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcSabbatLeader : public FElysiumNpcVampireBoss
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VSabbatLeader", FElysiumNpcVampireBoss)

	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 SelectScheduleMeleeCombat(int32 Unused) override;
	virtual void TaskFail(int32 Reason) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual void OnVictimHitByMe(FElysiumEntity* Victim) override;
	virtual bool OkToInterruptForMelee() override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	// Slot 620 (`0x103aa5e0`): introduced here.
	virtual void FootstepSound();
	// Slot 621 (`0x103aa7a0`): introduced here.
	virtual void AttackSound();
	virtual int32 Restore(void* Archive) override;
	virtual bool HandleInteraction(int32 Interaction, void* Data, FElysiumEntity* Other) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `FElysiumNpcWerewolf` (the move manifest's corrected owner).
	/** `CNPC_VSabbatLeader::CheckStuck` `0x103ab580`. */
	void CheckStuck();

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcDamage.inl`.
	/** `0x103ab110` — `CNPC_VSabbatLeader::SpawnBloodPoolEmitter(string_t name, CBaseEntity* orient)`.
	 *  Takes this NPC's own origin (slot 217, `+0x364`), replaces its Z either with `orient`'s origin Z
	 *  when one is given or with `thunk_FUN_101d08e0(origin, z)`'s answer when it is not — retail's
	 *  floor-drop lookup — creates the named emitter there and starts it (`+0x3c4`). The create's
	 *  failure arm dereferences a null pointer in retail; this port refuses instead and says so. */
	void SpawnBloodPoolEmitter(const FString& Name, const FElysiumEntity* OrientTo);

	// From `ElysiumNpcFacing.inl`.
	// `CNPC_VSabbatLeader::PlayerIsFacingMe` `0x103aaf50`.
	bool PlayerIsFacingMe() const;

	// From `ElysiumNpcHints.inl`.
	/** `CNPC_VVampireBoss::DistToHintCenterLine2D_3` (`0x103c6680`) — the squared-then-rooted distance
	 *  from `Point` to the line through `LineStart` along `LineDir`. */
	static float DistToHintCenterLine2D_3(const FVector& LineStart, const FVector& LineDir,
		const FVector& Point);
	/** `CNPC_VVampireBoss::DistToHintCenterLine2D_2` (`0x103c6570`) — the same, with the line taken from
	 *  the hint's own origin and facing, flattened to 2D. */
	static float DistToHintCenterLine2D(const FHintWords& Hint, const FVector& PointCm);

	// From `ElysiumNpcLifecycle19.inl`.
	/** SabbatLeader words Damage / Misc / Schedule already carry some of; these are the rest.
	 *  `bSabbatLeaderActivated` (`+0x66b8`) is family State19's. */
	int32 SabbatLeaderRouteFailCount = 0;        // +0x66bc — Schedule already has FailureType at +0x66c0
	bool bSabbatLeaderLargeSplash = false;
	bool bSabbatLeaderParticleSpawned = false;
	int32 SabbatLeaderJumpBloodBalance = 0;
	bool bSabbatLeaderLastAttackWasNova = false;
	bool bSabbatLeaderTrackPlayer = false;

	// From `ElysiumNpcMaintain19.inl`.
	/** `CNPC_VSabbatLeader::TaskFail` (`0x103a9400`). True means its flip path returned without the
	 *  Troika chain and the caller must stop. */
	bool SabbatLeaderTaskFail(int32 Reason);

	// From `ElysiumNpcMisc.inl`.
	int32 SabbatLeaderRoarAttackCount = 0;   // +0x66e0 CNPC_VSabbatLeader::m_RoarAttackCount

	// From `ElysiumNpcMotor.inl`.
	/** `thunk_FUN_102c4cc0(this, out, from, to)` — retail's jump-arc solver, which
	 *  `SetJumpVelocityTowardPlayer` (`0x103aad40`) feeds the lead position and then assigns straight to
	 *  `SetAbsVelocity`. **SEAM**: no solver here; answers false and the velocity is left alone. */
	bool SolveJumpArc(const FVector& FromUnits, const FVector& ToUnits, FVector& OutVelocityUnits) const;
	/** `CNPC_VVampireBoss::DistToHintCenterLine2D_2(hint, pos)` — the squared 2-D distance from a
	 *  position to a hint's centre line that `PlayerInNoJumpZone` (`0x103a9e70`) thresholds at 100.0.
	 *  **SEAM**: no hint geometry here; answers false and the zone test finds nobody inside. */
	bool DistToHintCenterLine2DSqr(int32 HintNode, const FVector& PositionUnits, float& OutSqr) const;
	/** `CNPC_VSabbatLeader::SetJumpVelocityTowardPlayer` `0x103aad40`. */
	void SetJumpVelocityTowardPlayer();
	/** `CNPC_VSabbatLeader::PlayerInNoJumpZone` `0x103a9e70`. */
	bool PlayerInNoJumpZone() const;

	// From `ElysiumNpcPositions.inl`.
	/** `CNPC_VSabbatLeader::SelectTeleportArchway` `0x103a9540` — the type-`0x3e82` node whose flat
	 *  distance to the player clears `_DAT_104c3cbc` and whose yaw is closest to the player's own. */
	static int32 SelectTeleportArchwayRule(TArrayView<const FHintWords> Nodes, const FVector& PlayerCm,
		float PlayerYaw);
	/** `CNPC_VSabbatLeader::SelectDiveOutPoint` `0x103a9ad0` — type `0x3e85`, the same flat-distance
	 *  gate at `_DAT_104c3cfc` and the score `|yawDelta| + flatDistance`. */
	static int32 SelectDiveOutPointRule(TArrayView<const FHintWords> Nodes, const FVector& PlayerCm,
		float PlayerYaw);
	/** `CNPC_VSabbatLeader::SelectDiveInPoint` `0x103a9760` — `SelectDiveOutPoint` plus two gates: the
	 *  leader must already be `_DAT_104c3d00` from the player, and the node must lie in the hemisphere
	 *  AWAY from him. Answers `INDEX_NONE` for a leader who is too close, which is retail's early out. */
	static int32 SelectDiveInPointRule(TArrayView<const FHintWords> Nodes, const FVector& PlayerCm,
		float PlayerYaw, const FVector& SelfCm);
	int32 SelectTeleportArchway() const;
	int32 SelectDiveInPoint() const;
	int32 SelectDiveOutPoint() const;

	// From `ElysiumNpcSchedule.inl`.
	/** `CNPC_VSabbatLeader::FlipFailureType` (`0x103a9d00`): `m_FailureType = 1 - m_FailureType`. */
	void FlipFailureType();
	/** `CNPC_VSabbatLeader::m_FailureType`, `+0x66c0`. */
	int32 FailureType = 0;

	// From `ElysiumNpcSpeciesMisc10.inl`.
	/** `CNPC_VSabbatLeader::CheckForJumpCondition` (`0x103a9d90`), three arms in retail's order:
	 *  `0x103c67f0(this, DAT_104c3cc8)` — `curtime - m_flLastAttackTime (+0x5d9c)` STRICTLY greater than
	 *  **8.0** — then a health loss since the mark of `_DAT_104c3cc4` = **0.0666667** or more, then
	 *  `PlayerDamagedEnoughThisRound`. */
	bool CheckForJumpCondition();
	/** `CNPC_VSabbatLeader::UpdateBloodSplash` (`0x103aa960`): nothing at all while `m_bDiving`
	 *  (`+0x66d5`) is set — not even the level copy — otherwise, with a water level above 0 and either a
	 *  previous level of 0 or `m_fLastSplashTime + _DAT_104c3cdc` (**0.25** s) already past, spawn
	 *  `bloodsplash_emitter` and, ONLY on the dry-to-wet edge, `bloodbigsplash_emitter` too, then stamp
	 *  the splash time. The previous level is copied on every non-diving pass, so a dive freezes it and
	 *  leaving one re-fires the big splash. */
	void SabbatLeaderUpdateBloodSplash();
	int32 SabbatLastWaterLevel = 0;      // +0x66c4 `m_nLastWaterLevel`
	double SabbatLastSplashTime = 0.0;   // +0x66c8 `m_fLastSplashTime`
	bool bSabbatDiving = false;          // +0x66d5 `m_bDiving`
	/** `CNPC_VSabbatLeader::RecordPlayerHealth` (`0x103aaa80`) — snapshot `m_hClosestPlayer`'s type-0
	 *  stat `0x0f` into `m_LastPlayerHealth` (`+0x66cc`). Stat `0x0f` is the accumulated WOUND counter,
	 *  not current health, so this records the player's damage TOTAL at the start of a round. */
	void RecordPlayerHealth();
	int32 SabbatLastPlayerHealth = 0;    // +0x66cc `m_LastPlayerHealth`
	/** `CNPC_VSabbatLeader::PlayerDamagedEnoughThisRound` (`0x103aabc0`) — false with no live
	 *  `m_hClosestPlayer`; otherwise true only when `_DAT_104c3ce0` (**2.0**, read at file offset
	 *  `0x4c3ce0` of the pinned image) `<= (float)(stat0x0f - m_LastPlayerHealth)`. */
	bool PlayerDamagedEnoughThisRound() const;

	// From `ElysiumNpcState19.inl`.
	/** `CNPC_VSabbatLeader::m_bActivated` (`+0x66b8`). Default 0, so an unactivated leader is IDLE. */
	bool bSabbatLeaderActivated = false;

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;
	virtual void UpdatePresenceEffect() override;
	virtual int32 OnTakeDamage_Alive(void* Arg0) override;
	virtual void RunAI(bool Arg0) override;
	virtual void GatherConditions() override;
	virtual int32 PreSelectSchedule() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;
};
