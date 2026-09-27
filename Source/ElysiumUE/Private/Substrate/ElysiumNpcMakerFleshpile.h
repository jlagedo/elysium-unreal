#pragma once

#include "Substrate/ElysiumNpcMaker.h"

class FElysiumNpcAndreiBlood;

// `CNPCMaker_Fleshpile` (primary vtable `0x1049ffe4`), built by the `npc_maker_fleshpile` factory —
// story 5 fold A4. The Andrei fight's runner spawner (`hw_jewelry_1`, `hw_warrens_3`).
//
// Its think does NOT spawn. `0x1034c8b0` only re-arms `m_flNextThink = freq + curtime`; the runners
// come from `CNPC_VAndreiBlood::StartTask` `0x1035d1b0` case `0x154` (the summon), which finds the
// nearest `npc_maker_fleshpile` within 1024 units and calls its slot 617 `MakeNPC(0)`
// (`SummonRunnerNear`). Its `MakeNPC` (`0x1034c2d0`) is a body of its own — no map-data replay, no
// live/total counters, a different copy set — gated on Andrei's runner budget
// (`CNPC_VAndreiBlood::m_iActiveRunnerCount` `+0x66b8`, cap 2), which its `DeathNotice`
// (`0x1034c8e0`) gives back when a runner dies. Both reach Andrei through the file static
// `DAT_10938040`, the port's `FElysiumNpc::FleshpileAndreiSingleton()`.
//
// Fifteen own vtable bodies (the vtable diff against `CNPCMaker`): slot 5 is the deleting destructor;
// the rest are overrides below. Its datamap (`0x106250f0`) carries no word, only the think's
// FUNCTIONTABLE row.
class FElysiumNpcMakerFleshpile : public FElysiumNpcMaker
{
public:
	static constexpr const TCHAR* RetailClassName = TEXT("CNPCMaker_Fleshpile");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;

	// Slot 82 `0x1034bdc0` — `&datamap_CNPCMaker_Fleshpile`.
	virtual void* GetDataDescMap() override;
	// Slot 103 `0x1034c020` — `CNPCMaker::Spawn` byte for byte but for the installed think.
	virtual void Spawn() override;
	// Slot 104 `0x1034c180` — the model half without the overlay, the base NPC chain, and the child
	// class precached unconditionally (no emptiness check, no removal).
	virtual void Precache() override;
	// Slot 130 `0x1034c260` — re-find Andrei into `DAT_10938040`, then the Troika `OnRestore`
	// `0x102998c0`, DIRECT.
	virtual void OnRestore(bool bFromLoad) override;
	// Slot 139 `0x1034c8e0` — the runner budget, then `CNPCMaker::DeathNotice` `0x1034bc90`, DIRECT.
	virtual void DeathNotice(FElysiumEntity* Child) override;
	// Slots 362-365 `0x1034bf10` / `0x1034bef0` / `0x1034bf50` / `0x1034bf30` — false, each the base
	// maker's body byte for byte at this class's own address.
	virtual bool FInViewCone(const FVector& PointCm) override;
	virtual bool FInViewCone(FElysiumEntity* Candidate) override;
	virtual bool FInAimCone(const FVector& TargetCm) override;
	virtual bool FInAimCone(FElysiumEntity* AimTarget) override;
	// Slots 370/371 `0x1034be90` / `0x1034bec0` — forwards through 368/369.
	virtual FVector HeadDirection2D() override;
	virtual FVector HeadDirection3D() override;
	// Slot 617 `0x1034c2d0`.
	virtual FElysiumNpc* MakeNPC(bool bBypass) override;
	// Slots 619/620 `0x1034bf70` / `0x1034bf90` — `RET 4`.
	virtual void ChildPreSpawn(FElysiumNpc* Child) override;
	virtual void ChildPostSpawn(FElysiumNpc* Child) override;

	// The installed think: `0x1034c8b0` when `InstalledThink` is `Fleshpile`, else the base's.
	virtual void Think() override;
	// `0x1034c8b0` — `m_flNextThink = m_flSpawnFrequency + curtime`, and nothing else.
	void FleshpileMakerThink();

	/** `DAT_10938040` as `0x1034c2d0` fills it: the cached `npc_VAndreiBlood`, or — when nothing is
	 *  cached — `FindEntityByClassname(NULL, "npc_VAndreiBlood")` cast to `CNPC_VAndreiBlood`
	 *  (`0x10625234`). **Named modernization:** retail never clears the cache and would read a
	 *  removed Andrei through a stale pointer; the port re-finds when the cached handle no longer
	 *  resolves. */
	FElysiumNpcAndreiBlood* FleshpileOwner() const;
	/** `DAT_10938040` as `0x1034c8e0` reads it: the cached Andrei, never filled here. */
	FElysiumNpcAndreiBlood* CachedFleshpileOwner() const;

	/** The maker half of `CNPC_VAndreiBlood::StartTask` `0x1035d1b0` case `0x154` (the summon):
	 *  `FindEntityByClassnameNearest("npc_maker_fleshpile", origin, 1024.0)` (`0x100f7d50`), the RTTI
	 *  cast to `CNPCMaker_Fleshpile` (`0x1062520c`), then slot 617 `MakeNPC(0)`.
	 *
	 *  SEAM: Andrei's `StartTask` is unported (census row `CNPC_VAndreiBlood#442`, `rule`), so no
	 *  schedule runs task `0x154` and nothing calls this yet. It stands so the task's port has one
	 *  call to make, and the fleshpile's think stays the re-arm it is. */
	static FElysiumNpc* SummonRunnerNear(FElysiumEntityWorld& InWorld, const FVector& AndreiOriginCm);
};
