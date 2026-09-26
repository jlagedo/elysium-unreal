#pragma once

#include "Substrate/ElysiumNpcVampireBoss.h"

// `CNPC_VAsianVampire` (primary vtable `0x104a935c`), built by `npc_VAsianVampire` factory
// `0x103602f0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcAsianVampire : public FElysiumNpcVampireBoss
{
public:
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_VAsianVampire");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 SelectScheduleMeleeCombat(int32 Unused) override;
	virtual void TaskFail(int32 Reason) override;
	virtual int32 SelectScheduleRangedCombat(int32 Arg) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual int32 Restore(void* Archive) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcKernelGeometry.inl`.
	/** The body: is the closest player's collision box overlapping mine in XY? Retail compares the 2-D
	 *  distance between the two origins against `0.5 * |playerMaxs.xy - playerMins.xy|` plus
	 *  `0.5 * |myMaxs.xy - myMins.xy|` — the two half-diagonals of the XY footprints, NOT their radii,
	 *  and `_DAT_104454d0` is 0.5. Answers false with no closest player, which is retail's own arm. */
	bool StandingOnPlayer() const;
	/** The rule behind it, so the threshold is measurable without a world. All four extents are the
	 *  collideable's OBB mins/maxs in CENTIMETRES; only X and Y are read. */
	static bool StandingOnPlayerOverlap(const FVector& MyOriginCm, const FVector& OtherOriginCm,
		const FVector& MyMinsCm, const FVector& MyMaxsCm, const FVector& OtherMinsCm,
		const FVector& OtherMaxsCm);

	// From `ElysiumNpcKernelHints.inl`.
	/** `CNPC_VAsianVampire::AddHintToStoredJumpPositions` (`0x10361990`) — push the hint's origin into
	 *  the two-slot ring at `m_vLastJumpPosition`, then advance and wrap `m_iLastJumpPositionIdx`. */
	void AddHintToStoredJumpPositions(const FHintWords& Hint);

	// From `ElysiumNpcKernelLifecycle19.inl`.
	static constexpr float AsianVampireJumpGravity = 2.f;    // `_DAT_104a9300`
	/** `CNPC_VAsianVampire::m_bPathBlocked` (`+0x66d4`): cleared by `NPCInit` `0x10360ce0`, raised by
	 *  `TaskFail` `0x10362390` on failure codes 12..15. (`m_bSuppressRanged` `+0x66e8` is family
	 *  Schedule's `bSuppressRanged`.) */
	bool bAsianVampirePathBlocked = false;

	// From `ElysiumNpcKernelMotor.inl`.
	// +0x66b8 `CNPC_VAsianVampire::m_vLastJumpPosition[2]` (six floats) and +0x66d0
	// `m_iLastJumpPositionIdx` — the two-entry ring `IsPosNearStoredJumpPositions` (`0x103618a0`) walks.
	// SOURCE units, as every retail position word here is.
	FVector LastJumpPosition[2] = { FVector::ZeroVector, FVector::ZeroVector };
	int32 LastJumpPositionIdx = 0;
	// +0x66d8 `CNPC_VAsianVampire::m_fMovedTimeStamp` and +0x66dc `m_vMovedPosition` — the stationary
	// watchdog `UpdateMovedTimeStamp` (`0x10362540`) stamps and `StationaryForTooLong` (`0x10362670`)
	// reads. The stamp is an absolute curtime, the position SOURCE units.
	double MovedTimeStamp = 0.0;
	FVector MovedPosition = FVector::ZeroVector;
	/** `CNPC_VAsianVampire::PositionClearForTeleport(pos, 150.0)` (`_DAT_104a9320`) — the clearance test
	 *  `SelectJumpbaseNode` filters hint nodes with. **SEAM**: answers false, so the search finds no
	 *  node rather than choosing one blind. */
	bool PositionClearForTeleport(const FVector& PositionUnits, float RadiusUnits) const;
	/** `CNPC_VAsianVampire::AddHintToStoredJumpPositions(hint)` — the ring write that pairs with
	 *  `IsPosNearStoredJumpPositions`. Ported: it stores the hint's origin at `m_iLastJumpPositionIdx`
	 *  and advances the index modulo 2. The hint's ORIGIN is the seam. */
	void AddHintToStoredJumpPositions(int32 HintNode);
	/** `CNPC_VAsianVampire::GetJumpSchedule` `0x10362430`. */
	int32 GetJumpSchedule() const;
	/** `CNPC_VAsianVampire::IsPosNearStoredJumpPositions` `0x103618a0`. */
	bool IsPosNearStoredJumpPositions(const FVector& PositionUnits) const;
	/** `CNPC_VAsianVampire::SelectJumpbaseNode` `0x10361730` — nearest teleport-clear hint of type
	 *  18000, then remember it. */
	int32 SelectJumpbaseNode();
	/** `CNPC_VAsianVampire::SetupJump` `0x10361a70` (rise constant 100.0) and `CNPC_VSheriffMan::SetupJump`
	 *  `0x103b1300` (400.0) — non-virtual per-class methods sharing one behaviour, `SetupJumpRise`. */
	void AsianVampireSetupJump(float Enabled);
	/** `CNPC_VAsianVampire::StationaryForTooLong` `0x10362670` and `UpdateMovedTimeStamp` `0x10362540`. */
	bool StationaryForTooLong() const;
	void UpdateMovedTimeStamp();

	// From `ElysiumNpcKernelPositions.inl`.
	/** `CNPC_VAsianVampire::SelectLedgeNode` `0x103615c0` — type `0x4653`, gated on
	 *  `PositionClearForTeleport(node, 150.0)` and scored by distance to the NPC's OWN origin (the only
	 *  selector of the eight that never asks for a player), then remembered. */
	static int32 SelectLedgeNodeAsianRule(TArrayView<const FHintWords> Nodes, const FVector& SelfCm,
		TFunctionRef<bool(const FVector&, float)> Clear);
	int32 SelectLedgeNodeAsian();
	/** `CNPC_VAsianVampire::PositionClearForTeleport` `0x103629d0`. */
	bool PositionClearForTeleportAsian(const FVector& PositionCm, float ClearanceCm) const;

	// From `ElysiumNpcKernelSchedule.inl`.
	/** `CNPC_VAsianVampire::m_bSuppressRanged`, `+0x66e8` — the two extra terms that class's
	 *  `SelectScheduleMeleeCombat` (`0x10361be0`) puts on its ranged arms. Its one writer is `NPCInit`
	 *  `0x10360ce0`, which sets it to 1. (`CNPC_VManBat::m_iMoveGoalNodeID` `+0x6674`, which the ManBat
	 *  `SelectSchedule` arms here write, is family Bosses' `ManBatMoveGoalNodeId`.) */
	bool bSuppressRanged = false;
	/** SEAM for `GetJumpSchedule` (`0x10361a80`, reached from `CNPC_VAsianVampire::
	 *  SelectScheduleMeleeCombat`'s `COND_ENEMY_UNREACHABLE` arm): the schedule that jumps to an
	 *  unreachable enemy. Answers 0 — no jump vocabulary exists here. */
	int32 GetJumpSchedule(FElysiumEntity* Enemy) const;

};
