#pragma once

#include "Substrate/ElysiumNpc.h"

// `CNPC_VBaseBoss` (primary vtable `0x104bd2cc`). No factory builds it by classname: it is
// abstract, and only its subclasses are spawned.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcBaseBoss : public FElysiumNpc
{
public:
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_VBaseBoss");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void DrawDebugStatOverlays() override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcBosses.inl`.
	/** SEAM for `0x10366400` — family **Species**' row over `CNPC_VBaseBoss::m_BlacklistedEntities`
	 *  (+0x665c), the store MingXiao blacklists a thrown object in for 20 s. `0x10398b20` skips a
	 *  pedestal that is still on it. There is no boss blacklist here; answers false, which admits every
	 *  pedestal — the permissive arm, and stated as such. */
	bool BossBlacklistHolds(const FElysiumEntity* Candidate) const;

	// From `ElysiumNpcDebug.inl`.
	/** `CNPC_VBaseBoss::DrawDebugStatOverlays` (`0x10366290`) — `"Dist to player: %.3f"` then
	 *  `0x102775e0`. Thirty-six bytes, and the tail call is to the BASE and not to the Troika line. */
	void BossDrawDebugStatOverlays();

	// From `ElysiumNpcPositions.inl`.
	static FEnemySightCandidates EnemySightCandidatesOf(const FVector& BoxMinCm, const FVector& BoxMaxCm,
		const FVector& ExtentsCm, float RandomZCm);

	// From `ElysiumNpcSpecies.inl`.
	// `CNPC_VBaseBoss::m_BlacklistedEntities`, `+0x665c` with its allocation count at `+0x6660`, grow
	// size at `+0x6664`, element count at `+0x6668` and the `CUtlMemory` element mirror at `+0x666c`.
	// Rows are the 8-byte `{EHANDLE, float expiry}` pair family **Bosses** already declared as
	// `FBlacklistedEntity` for `CNPC_VHengeyokai`'s copy at `+0x66a4`; that struct is reused rather
	// than restated. Werewolf locks a hint in it and MingXiao skips a thrown object for 20 s.
	TArray<FBlacklistedEntity> BossBlacklist;
	/** `0x103662d0` / `0x10366400` / `0x10366490` — `CNPC_VBaseBoss::m_BlacklistedEntities`'s add,
	 *  test-and-expire and index-of. */
	void FUN_103662d0(const FElysiumEntityHandle& Entity, float Seconds);
	bool FUN_10366400(const FElysiumEntity* Candidate);
	int32 FUN_10366490(const FElysiumEntity* Candidate) const;
	bool FUN_103c1b10();

	// From `FElysiumNpcWerewolf` (the move manifest's corrected owner).
	/** `CNPC_VBaseBoss::EnemyCouldSeeHull` `0x10366510`, the boss branch of slot 617. NOT the generated
	 *  slot: 617 on the Troika line is `CNPCMaker::MakeNPC`'s index and carries another body entirely,
	 *  so this is the branch answer standing beside it. */
	bool EnemyCouldSeeHull(const FVector& OriginCm, bool bSkipViewCone, bool bUseHitbox,
		const FVector& ExtentsCm);

	// From `FElysiumNpcWerewolf` (the move manifest's corrected owner).
	/** Slot 362 `FInViewCone(const Vector&)` on the ENEMY. **SEAM**: no port body answers a view cone
	 *  for an arbitrary point, so it answers false and each candidate is refused unless the caller
	 *  passed `bSkipViewCone` — which is what `UpdateConditionCanTeleport`, its one recovered caller,
	 *  passes when the player is inside `_DAT_10457ac4`. */
	static bool EnemyInViewCone(const FElysiumEntity& Enemy, const FVector& PointCm);
};
