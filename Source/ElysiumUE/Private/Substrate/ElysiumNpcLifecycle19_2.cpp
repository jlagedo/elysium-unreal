#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumWeaponClasses.h"

// Story 29e, family **Lifecycle19** — species `NPCInit` arms. Each chains the body it replaces
// through a direct call (retail's non-virtual thunk), never through the slot dispatcher.

void FElysiumNpc::FrenzyShadowNPCInit()
{
	// `10375ca4` `Weapon_Create` then `10375cb6 OR [EDI+0x19c],0x40` — the NODRAW bit is ORed into
	// whatever `Weapon_Create` answered, with NO null test: a create that fails faults here. Item
	// creation is deferred to `ResolveLoadout` in this port (see `SpawnEquipLoadout`), so the write
	// lands when a weapon is already carried and is counted as retail's fault when none is.
	++SpawnEquipRequests;
	++FrenzyShadowWeaponFlagOrs;
	{
		FElysiumItem* const Active = Inventory.Active(*this);
		FElysiumWeapon* const Weapon = Active != nullptr ? Active->AsWeapon() : nullptr;
		if (Weapon != nullptr)
		{
			Weapon->Hide(this);                                          // m_fEffects |= EF_NODRAW
		}
		else
		{
			++FrenzyShadowNullWeaponFaults;                              // 10375cb6, retail faults
		}
	}
	PlayerControllerNPCInit();
	WriteIdealStateRetail(0xb);
	SetState(0xb);                                                       // 1026e340(0xb)
	SetFrenziedWord(FrenzyShadowFrenziedFlags);
	Senses.bCanPerformSenses = true;
	FrenzyShadowHostileEnemyCount = 0;
	bFrenzyShadowFailedGrapple = false;
	NpcSpeedScale = FrenzyShadowSpeedScale;
	bNavIgnorePhysicsProps = true;
	FrenzyShadowHostileRecount();                                        // tail JMP 10376c10
}

void FElysiumNpc::PlayerControllerNPCInit()
{
	TroikaNPCInit();
	Senses.Memory.NextFleeSoundTime = 0.0;
	NpcKernelLifecycle19_2Shared::Lifecycle19_2LawNever(*this);
	SeedCriminalLevelWitnessed();
	InvestigateMode = 6;
	InvestigateModeCombat = 6;
	SetForceFrequentThink(true);                                         // slot 416
	const FElysiumEntityHandle Owner = GetOwnerEntity();
	if (!Owner.IsSet())
	{
		FriendPlayer = FElysiumEntityHandle::Invalid();
	}
	else
	{
		FriendPlayer = Owner;
	}
	SetFrenziedWord(0);
	Senses.bCanPerformSenses = false;
	bIsBccTargetable = false;
}

void FElysiumNpc::WolfMorphNPCInit()
{
	PlayerControllerNPCInit();
	WriteIdealStateRetail(WolfMorphRetailState);
	SetActivity(WolfMorphActivity);
}
