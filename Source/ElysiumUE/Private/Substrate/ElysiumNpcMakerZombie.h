#pragma once

#include "Substrate/ElysiumNpcMaker.h"

// `CNPCMaker_Zombie` (primary vtable `0x104a0bbc`), built by the `npc_maker_zombie` factory — story 5
// fold A4. The graveyard and Giovanni zombie spawners.
//
// Fourteen own vtable bodies (the vtable diff against `CNPCMaker`): slot 5 is the deleting destructor;
// the rest are overrides below. Its datamap (`0x106253e8`) adds three words; its `Spawn` jitters the
// first think by `RandomFloat(1, 2)`, installs a NULL think when disabled and slams the maker's five
// police thresholds to 999999; its `CanMakeNPC` refuses beyond a Manhattan player distance; its
// `MakeNPC` calls the base and then hands the child `item_w_zombie_fists`, its AI spawn type and a
// spawn emitter. Its think `0x1034d2d0` is `MakerThink`'s code at its own address.
class FElysiumNpcMakerZombie : public FElysiumNpcMaker
{
public:
	static constexpr const TCHAR* RetailClassName = TEXT("CNPCMaker_Zombie");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;

	// --- `CNPCMaker_Zombie`'s own words (datamap `0x106253e8`) --------------------------------------
	int32 ZombieAiSpawnType = 0;    // +0x76d0 m_iZombieAISpawnType  Flag_ZombieAIType
	bool bShouldRagdoll = false;    // +0x76d4 m_bShouldRagdoll      should_ragdoll
	float RemoveDistance = 0.0f;    // +0x76d8 m_flRemoveDist        remove_distance — the one word
	                                //         `CanMakeNPC` compares, as a FLOAT

	// Slot 82 `0x1034c9f0` — `&datamap_CNPCMaker_Zombie`.
	virtual void* GetDataDescMap() override;
	// Slot 103 `0x1034cc60`.
	virtual void Spawn() override;
	// Slot 104 `0x1034cde0`.
	virtual void Precache() override;
	// Slots 362-365 `0x1034cb50` / `0x1034cb30` / `0x1034cb90` / `0x1034cb70` — false.
	virtual bool FInViewCone(const FVector& PointCm) override;
	virtual bool FInViewCone(FElysiumEntity* Candidate) override;
	virtual bool FInAimCone(const FVector& TargetCm) override;
	virtual bool FInAimCone(FElysiumEntity* AimTarget) override;
	// Slots 370/371 `0x1034cad0` / `0x1034cb00`.
	virtual FVector HeadDirection2D() override;
	virtual FVector HeadDirection3D() override;
	// Slot 617 `0x1034d140`.
	virtual FElysiumNpc* MakeNPC(bool bBypass) override;
	// Slot 618 `0x1034d0a0`.
	virtual bool CanMakeNPC(bool bBypass) override;
	// Slots 619/620 `0x1034cbb0` / `0x1034cbd0` — `RET 4`.
	virtual void ChildPreSpawn(FElysiumNpc* Child) override;
	virtual void ChildPostSpawn(FElysiumNpc* Child) override;

	// The installed think: `0x1034d2d0` when `InstalledThink` is `Zombie`, else the base's.
	virtual void Think() override;
	// `0x1034d2d0` — instruction for instruction `MakerThink` (`0x1034bbf0`): slot 617 `MakeNPC(0)`,
	// then the same three re-arms.
	void ZombieMakerThink();

	/** `0x1034cf20` — `SetZombieAIType(type, bPropagate)`: store `+0x76d0`; with `bPropagate`, walk
	 *  every entity named `m_ChildTargetName` whose Troika's parent (`+0x254`) is this maker and call
	 *  `CNPC_VZombie::SetZombieAIType` `0x103e0980` on it (no class test). Its only callers are
	 *  `0x1034d000` / `0x1034d050`, input-shaped bodies with no datamap row and no corpus caller —
	 *  unreachable; ported so the word has its writer. */
	void SetZombieAIType(int32 Type, bool bPropagate);

	// `1034cd58 MOV EAX,0xf423f` — 999999, the literal all five thresholds take.
	static constexpr int32 ZombieMakerPoliceLevel = 999999;
	// `1034cd25 PUSH 0x3f800000` / `1034cd20 PUSH 0x40000000` — the first-think jitter.
	static constexpr float ZombieSpawnJitterMin = 1.0f;
	static constexpr float ZombieSpawnJitterMax = 2.0f;

	/** SEAM for `thunk_FUN_10136580("item_w_zombie_fists")`, the entity factory: the class registry
	 *  answers once the item catalogue is installed, which a headless world does not do. The latch
	 *  forces the answer for a fixture (the registry is process-wide, so an earlier suite could
	 *  have installed the catalogue). */
	bool ZombieFistsItemExists() const;
#if WITH_DEV_AUTOMATION_TESTS
	void SetZombieFistsItemForTests(bool bExists) { ZombieFistsItemForTests = bExists; }
	void ClearZombieFistsItemForTests() { ZombieFistsItemForTests.Reset(); }
	TOptional<bool> ZombieFistsItemForTests;
#endif

	/** `"Zombies_spawning_emitter"` (`0x1062565c`) at the maker's origin and angles, 15 s
	 *  (`1034d249 PUSH 0x41700000`). SEAM: no Source particle emitters; the request is recorded. */
	struct FZombieSpawnEmitter
	{
		FVector Origin = FVector::ZeroVector;
		FVector Angles = FVector::ZeroVector;
		float LifetimeSeconds = 0.f;
	};
	TArray<FZombieSpawnEmitter> ZombieSpawnEmitters;

	/** What `MakeNPC` handed the child: the item it created and how many times. SEAM: no item
	 *  entity is stood for the fists at kernel level; the hand-over is recorded. */
	FString LastZombieFistsItem;
	int32 ZombieFistsEquips = 0;
};
