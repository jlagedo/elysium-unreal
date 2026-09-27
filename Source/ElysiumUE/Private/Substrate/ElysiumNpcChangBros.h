#pragma once

#include "Substrate/ElysiumNpcVampireBoss.h"

// `CNPC_VChangBros` (primary vtable `0x104adabc`), built by `npc_VChangBros` factory `0x1036a0a0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcChangBros : public FElysiumNpcVampireBoss
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VChangBros", FElysiumNpcVampireBoss)

	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual int32 Restore(void* Archive) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 SelectScheduleMeleeCombat(int32 Unused) override;
	virtual void TaskFail(int32 Reason) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcDamage.inl`.
	// `CNPC_VChangBros`'s single centre emitter.
	FElysiumEntityHandle ChangCenterEmitter;     // +0x66f4 m_hCenterEmitter (datamap)
	/** `0x1036e8c0` — `CNPC_VChangBros::KillCenterEmitter`: the same stop-then-0.1 s-fade pair on the
	 *  single `m_hCenterEmitter` (+0x66f4), and it too leaves the handle standing. */
	void KillCenterEmitter();
	/** `0x1036dd20` — `CNPC_VChangBros::SpawnEnergyBall`. Builds a spawn point by taking the muzzle
	 *  attachment's basis (slot 219, `+0x36c` -> `AngleVectors` `0x10139610`) and pushing this NPC's
	 *  origin along it by the retail offset triple `(_DAT_104ada24, _DAT_104ada28, _DAT_104ada2c)` =
	 *  `(50, 40, -10)` — forward 50, right 40, up -10 — then creates `item_w_chang_energy_ball` there
	 *  and, only when `m_hClosestPlayer` resolves, fires it at that player through the projectile's own
	 *  `+0x5d0` with speed `DAT_104ada30` = 800. */
	FElysiumEntityHandle SpawnEnergyBall();

	// From `ElysiumNpcFacing.inl`.
	// +0x66d0 m_fFacingTime (`CNPC_VChangBros`) — the curtime stamp `UpdateFacingTimer` resets, and the
	// clock `GetFacingTimeToTeleport`'s answer is measured against. Carried as double like every other
	// stamp in this runtime.
	double FacingTime = 0.0;
	// `CNPC_VChangBros::GetFacingTimeToTeleport` `0x1036dc60`.
	float GetFacingTimeToTeleport() const;
	// `CNPC_VChangBros::UpdateFacingTimer` `0x1036d600`.
	void UpdateFacingTimer();

	// From `ElysiumNpcHints.inl`.
	/** `CNPC_VChangBros::CheckJumpPathToHintNode` (`0x1036df50`) — may this brother jump to the hint
	 *  without crossing the closest player, or the other brother, or a sector-4 endpoint? */
	bool CheckJumpPathToHintNode(const FHintWords& Hint) const;
	/** `CNPC_VVampireBoss::DistToSegment` (`0x103c6b70`) — the point-to-segment distance
	 *  `CheckJumpPathToHintNode` tests against `_DAT_104ada34`. Verbatim, including the degenerate arm,
	 *  which answers `0.0` and NOT the distance to the endpoint. */
	static float DistToSegment(const FVector& A, const FVector& B, const FVector& P);
	/** SEAM for `CNPC_VChangBros::GetSector(pos)` — the map-authored sector index the jump-path check
	 *  compares against 4. No sector partition exists on this substrate. Answers 0, which is "not
	 *  sector 4" and so does not block a jump retail would have allowed. */
	int32 JumpPathSector(const FVector& PositionCm) const;

	// From `ElysiumNpcLifecycle19.inl`.
	void ChangBrosNPCInit();            // `0x1036b050`

	// From `ElysiumNpcMisc.inl`.
	/** `CNPC_VChangBros::StoreArenaCenter` (`0x1036e400`) — walk the global `CAI_Hint` list for the
	 *  first node of type `0x4651`, take its origin into `m_vArenaCenter` (`+0x66dc`, family
	 *  **Positions**' `ChangArenaCenter`) and raise `m_bCenterStored` (`+0x66e8`). */
	void StoreArenaCenter();

	// From `ElysiumNpcMotor.inl`.
	// +0x66cc `CNPC_VChangBros::m_fLastJumpTime` (`FIELD_TIME`) — the stamp `CheckForJumpAttack`
	// (`0x1036c8d0`) measures both itself and every squad sibling against. An absolute curtime stamp,
	// carried as double like every other stamp in this runtime.
	double LastJumpTime = 0.0;
	/** `CNPC_VChangBros::GetSector(pos)` — the sector id `CheckForJumpAttack` compares against 4 for
	 *  both the player and itself. **SEAM**: this substrate has no sector partition; answers 4, which
	 *  is the value that CLOSES the gate, so the jump attack is refused rather than allowed on a guess. */
	int32 ChangBrosSector(const FVector& PositionUnits) const;
	/** `CNPC_VChangBros::CheckForJumpAttack` `0x1036c8d0`. */
	bool CheckForJumpAttack();
	/** `CNPC_VChangBros::SetupSuperJump` `0x1036e160`. */
	void SetupSuperJump(float Enabled);

	// From `ElysiumNpcPositions.inl`.
	// `CNPC_VChangBros`'s teleport words and its arena centre.
	FVector ChangLastTeleportPosition = FVector::ZeroVector;    // +0x66bc m_vLastTeleportPosition
	double ChangLastTeleportTime = 0.0;                         // +0x66c8 m_fLastTeleportTime
	FVector ChangArenaCenter = FVector::ZeroVector;             // +0x66dc m_vArenaCenter
	bool bChangCenterStored = false;                            // +0x66e8 m_bCenterStored
	/** `CNPC_VChangBros::SelectTeleportNode` `0x1036cce0` — types 18000 / `0x4653` / `0x4652`, the
	 *  clearance gate at `DAT_104ad9f4`, the nearest node kept, and a same-sector node PREFERRED over
	 *  it. `Sector` is `GetSector`. */
	static int32 SelectTeleportNodeChangRule(TArrayView<const FHintWords> Nodes, const FVector& PlayerCm,
		TFunctionRef<bool(const FVector&, float)> Clear, TFunctionRef<int32(const FVector&)> Sector);
	int32 SelectTeleportNodeChang();
	/** `CNPC_VChangBros::PositionClearForTeleport` `0x1036d350`. */
	bool PositionClearForTeleportChang(const FVector& PositionCm, float ClearanceCm) const;
	/** `CNPC_VChangBros::GetSector` `0x1036e580` — 0 with no stored centre, else 1..4. */
	int32 GetSector(const FVector& PositionCm) const;
	/** `CNPC_VChangBros::SectorIsInPit` `0x1036b6b0`. */
	static bool SectorIsInPit(int32 Sector);
	/** `CNPC_VChangBros::GetTeleportPosition` `0x1036d270` — the cooldown-gated getter the OTHER brother
	 *  reads through the squad list. */
	bool GetTeleportPosition(FVector& OutPositionCm) const;
	//
	// Slot 530 `IsUnreachable(FElysiumEntity*)` is the generated virtual and carries the base body; this
	// is the Chang brothers' override of it, which answers from the arena's sectors first.
	bool IsUnreachableChang(FElysiumEntity* Unreachable);   // `0x1036e6f0`

	// From `ElysiumNpcSpecies.inl`.
	/** `0x1036c7f0` — `CNPC_VChangBros::SetChangType`, a plain setter over family Squad's `ChangType`. */
	void FUN_1036c7f0(int32 InChangType);

	/** `_DAT_104ada44` — **2.3f**, read at file offset `0x4ada44`. */
	static constexpr float ChangBrosJumpGravity = 2.3f;
	/** `0x10630e54` and `0x10630e3c`, the two emitter-name literals. */
	static const TCHAR* ChangPowerupEmitterName();
	static const TCHAR* ChangSpineEmitterName();

	// From `ElysiumNpcSpeciesMisc10.inl`.
	/** `CNPC_VChangBros::CheckForTeleport` (`0x1036cab0`), three arms in retail's order: `m_ChangType`
	 *  (`+0x66b8`, family Squad's `ChangType`) equal to 1 answers false outright; else a health loss of
	 *  `_DAT_104ad9f8` = **0.1** or more since the mark answers true; else, only for type 0 and only
	 *  with a live other brother, `curtime - m_fFacingTime` strictly greater than
	 *  `GetFacingTimeToTeleport()` answers true. */
	bool CheckForTeleport();
	/** `CNPC_VChangBros::CheckForUnited` (`0x1036cbd0`): false with no other brother, and false while
	 *  `curtime` has not passed `m_fLastUnitedAttackTime + _DAT_104ada48` (**30.0** s). Past both it is
	 *  true UNLESS BOTH brothers' `GetCurrHealthPercent` are below `_DAT_104ada4c` (**0.5**) — one
	 *  healthy brother is enough. Note the sense: the percent RISES with damage, so "below 0.5" is the
	 *  HEALTHY half, and two healthy brothers refuse the united attack. */
	bool CheckForUnited();
	double ChangLastUnitedAttackTime = 0.0;   // +0x66ec `m_fLastUnitedAttackTime`
	/** `CNPC_VChangBros::SelectLedgeNode` (`0x1036cfa0`) — type `0x4653` again, but the FARTHEST
	 *  reachable ledge rather than the nearest: the incumbent is seeded `-FLT_MAX` and replaced on a
	 *  STRICTLY GREATER distance, the opposite comparison from `CNPC_VSheriffMan::SelectLedgeNode`
	 *  (`0x103b0ab0`) that family Positions' `SelectLedgeNodeRule` carries. `CheckJumpPathToHintNode`
	 *  gates every candidate and the distance is the full 3-D one from the NPC's own origin.
	 *
	 *  Split the way family Positions split `SelectLedgeNodeRule`/`SelectLedgeNode`: the PURE rule (the
	 *  type filter and the farthest-wins comparison) is a static over an already-accepted list, and the
	 *  member applies the jump-path gate while it gathers. Retail interleaves the two, and the answer is
	 *  identical because a tie keeps the incumbent either way — but the gate is a SEAM here
	 *  (`JumpPathSector` answers 4, which closes it for every node, retail's own refusal for a brother
	 *  in sector 4), so the comparison has to be assertable without it. */
	static int32 ChangBrosSelectLedgeNodeRule(TArrayView<const FHintWords> Nodes,
		const FVector& MeasureFromCm);
	int32 ChangBrosSelectLedgeNode() const;

	// From `ElysiumNpcSquad.inl`.
	int32 ChangType = 0;  // +0x66b8 CNPC_VChangBros::m_ChangType (datamap)
	/** `CNPC_VChangBros::GetOtherBrother` (`0x1036e2f0`) — the paired brother, found by walking my
	 *  squad for another `CNPC_VChangBros`. */
	FElysiumNpcChangBros* GetOtherBrother() const;
	/** `CNPC_VChangBros::ReadyForUnited` (`0x1036e820`) — am I running schedule `0x15a` or `0x15b`? */
	bool ReadyForUnited() const;
	/** `CNPC_VChangBros::SelectUnitedNode` (`0x1036d100`) — the hint node the twins meet at. */
	FElysiumEntity* SelectUnitedNode() const;

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual void NPCThink() override;
	virtual void GatherConditions() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;
};
