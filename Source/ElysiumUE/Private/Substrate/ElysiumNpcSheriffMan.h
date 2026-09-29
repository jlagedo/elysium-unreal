#pragma once

#include "Substrate/ElysiumNpcVampireBoss.h"

// `CNPC_VSheriffMan` (primary vtable `0x104c619c`), built by `npc_VSheriffMan` factory
// `0x103ada30`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcSheriffMan : public FElysiumNpcVampireBoss
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VSheriffMan", FElysiumNpcVampireBoss)

	// The constructor `0x103ae3e0`: its hull store (`docs/vtmb/data/class_hulls.json`).
	FElysiumNpcSheriffMan();

	virtual void NPCInit() override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 SelectScheduleMeleeCombat(int32 Unused) override;
	virtual void TaskFail(int32 Reason) override;
	virtual int32 SelectScheduleRangedCombat(int32 Arg) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual int32 GetUsedHullBits() override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual void OnPostRestore(FElysiumEntityWorld& InWorld) override;   // `CNPC_VSheriffMan::vfunc127`'s load-side half

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcDamage.inl`.
	TArray<FHideAndUnsolidifyCall> HideAndUnsolidifyCalls;
	void HideAndUnsolidifyWeapon(const FElysiumEntityHandle& Weapon);
	/** `0x103b10f0` — `CNPC_VSheriffMan::KillSheriff`. Two halves, in order. First: find the entities
	 *  named `logic_zap_player` and `sheriff`, RTTI-cast the first to `CLogicRelay`, and fire its
	 *  `Trigger` input **only when BOTH resolve** — the named `sheriff` is a presence test and nothing
	 *  more, it is never used. Second: take this NPC's active weapon and, if it has one, raise
	 *  `m_fEffects |= 0x20` (`EF_NODRAW`) and add solid flag `4` (`FSOLID_NOT_SOLID`) to its collision
	 *  before relinking it — the sword goes invisible and non-solid rather than being removed. */
	void KillSheriff();

	// From `ElysiumNpcLifecycle2.inl`.
	static constexpr int32 HullIndexSheriffMan = 0x15;   // `0x103ae6c0`
	static constexpr float SheriffManJumpGravity = ElysiumNpcTunables::SheriffManJumpGravity;      // `_DAT_104c6148`
	/** SheriffMan flags written before the VampireBoss chain. `SheriffLastTeleportPosition` exists. */
	bool bSheriffTeleporting = false;            // +0x66e4
	bool bSheriffDead = false;                   // +0x66e5
	bool bSheriffActivated = false;              // +0x66e6

	// From `ElysiumNpcMotor.inl`.
	void SheriffManSetupJump(float Enabled);

	// From `ElysiumNpcPositions.inl`.
	// `CNPC_VSheriffMan`'s teleport and arena-height words.
	FVector SheriffLastTeleportPosition = FVector::ZeroVector;  // +0x66d4 m_vLastTeleportPosition
	double SheriffLastTeleportTime = 0.0;                       // +0x66e0 m_fLastTeleportTime
	FElysiumEntityHandle SheriffTeleportSwarm;                  // +0x66d0 m_hTeleportSwarm
	bool bSheriffLedgeHeightStored = false;                     // +0x66e7, set by CacheFloorHeights
	float SheriffCenterFloorZ = 0.f;                            // +0x66e8, the centre node's Z
	float SheriffLedgeFloorZ = 0.f;                             // +0x66ec, the ledge node's Z
	/** `CNPC_VSheriffMan::SelectCenterNode` `0x103b0930` — the type-`0x4651` node nearest the player. */
	static int32 SelectCenterNodeRule(TArrayView<const FHintWords> Nodes, const FVector& PlayerCm);
	/** `CNPC_VSheriffMan::SelectLedgeNode` `0x103b0ab0` — the type-`0x4653` node nearest `MeasureFromCm`,
	 *  which retail picks per its `char` argument: `'\0'` measures from the PLAYER, anything else from
	 *  the NPC. The gate is the closest player either way. */
	static int32 SelectLedgeNodeRule(TArrayView<const FHintWords> Nodes, const FVector& MeasureFromCm);
	/** `CNPC_VSheriffMan::SelectTeleportNode` `0x103b0630` — types 17000 / `0x4653` / `0x4652`, a
	 *  distance whose Z is multiplied by `_DAT_10450564 = 100.0` before the root, the clearance gate at
	 *  `DAT_104c6124`, and the two-term score. `Clear` is `PositionClearForTeleport`. */
	static FTeleportNodePick SelectTeleportNodeSheriffRule(TArrayView<const FHintWords> Nodes,
		const FVector& PlayerCm, float PlayerYaw, TFunctionRef<bool(const FVector&, float)> Clear);
	// The member entry points. Each gathers the candidate list from the hint seams — which answer an
	// empty list — and returns the winning hint node index, `INDEX_NONE` for none. The two that cache
	// write their species words on the way out, exactly where retail writes them.
	int32 SelectCenterNode() const;
	int32 SelectLedgeNode(bool bMeasureFromSelf) const;
	int32 SelectTeleportNodeSheriff();
	/** `CNPC_VSheriffMan::PositionClearForTeleport` `0x103b0c70`. */
	bool PositionClearForTeleportSheriff(const FVector& PositionCm, float ClearanceCm) const;
	/** `CNPC_VSheriffMan::CacheFloorHeights` `0x103b1510`. */
	void CacheFloorHeights();
	/** `CNPC_VSheriffMan::CategorizeHeight` `0x103b1790` — 0 for the centre reference, 1 for the ledge. */
	void CategorizeHeight(float Zcm, int32& OutCategory) const;
	/** `CNPC_VSheriffMan::CategorizeHeights` `0x103b1680` — the player's height, then its own. */
	void CategorizeHeights(int32& OutPlayer, int32& OutSelf) const;

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual int32 OnTakeDamage_Alive(void* Arg0) override;
	virtual void NPCThink() override;
	virtual void RunAI(bool Arg0) override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;

	// --- 0019/8 L04 (StartTask19 species): private helpers ---
	/** SEAMS for the calls `StartTask` `0x103aec70` makes that no port body answers: the named emitters
	 *  (`0x102c41b0`), `CBaseCombatCharacter::ChooseBestMeleeWeapon` (`0x1000600a`), the weapon's
	 *  show-and-solidify half, the stand hull trace's `startsolid`, and
	 *  `MatchOriginAnglesToAnimation("bip01", 1, 1)`. See the definitions. */
	void SheriffCreateEmitter(const TCHAR* Name, const FVector& PositionUnits);
	void SheriffChooseBestMeleeWeapon();
	void ShowAndSolidifyWeapon(const FElysiumEntityHandle& Weapon);
	bool SheriffStandTraceStartSolid() const;
	TArray<FTeleportEmitterPlacement> SheriffEmitterPlacements;
	TArray<FHideAndUnsolidifyCall> ShowAndSolidifyCalls;
	TArray<FMatchOriginAnglesCall> SheriffMatchOriginAnglesCalls;
	int32 ChooseBestMeleeWeaponCalls = 0;
};
