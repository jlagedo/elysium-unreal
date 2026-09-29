#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcCombat10_2.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelCombat10_2Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

namespace NpcKernelCombat10_2Shared
{
	// The three roll thresholds, all literals in the bodies.
	inline constexpr int32 GDodgeRollThreshold = 0x4b;      // 75 — `0x102b7f40` and the two inlined twins
	inline constexpr int32 GMeleeSwitchRollThreshold = 0x19;  // 25 — the `0xe8` arm
	// Retail activity `ACT_…` 0x10, the weighted sequence every dodge test asks for.
	inline constexpr int32 GDodgeActivity = 0x10;
	inline int32 RangedRoll()
	{
		// `(**(code **)(*DAT_1070b244 + 8))(0, 99)` — `RandomInt(0, 99)`.
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99);
	}
	inline bool HasUsableMeleeWeaponPort(const FElysiumNpc& Npc)
	{
		// Slot 307 `HasUsableMeleeWeapon` (`vt+0x4cc`). The generated slot is a stub answering false;
		// the port carries the fact through the item catalogue, so the capability word is the answer
		// and the retail slot is named beside it — the same reading
		// `ElysiumNpcSchedule.cpp`'s `HasUsableRangedWeaponPort` takes for slot 308.
		return ElysiumNpcCond::WeaponCapability(Npc) == ElysiumNpcCond::ECapability::Melee;
	}
	inline FElysiumEntity* RangedEnemy(FElysiumNpc& Npc)
	{
		// Slot 167 `GetEnemy` (`vt+0x29c`).
		return Npc.World != nullptr
			? const_cast<FElysiumEntity*>(
				ElysiumNpcCond::ResolveEnemyHandle(*Npc.World, Npc.BaseMemory.Enemy))
			: nullptr;
	}
	// Slot 599 on `GetEnemy()` — the pair every selector runs together.
	inline bool EnemySlot599(FElysiumNpc& Npc)
	{
		// SEAM as family Schedule's: the ledger types slot 599's parameter `int`, so the generated
		// signature cannot carry the enemy pointer retail passes and the port passes 0. Named rather
		// than hidden, and the enemy is resolved anyway because retail's `GetEnemy()` call is an
		// observable dispatch.
		(void)RangedEnemy(Npc);
		return Npc.Slot599(0);
	}
	// `_DAT_10463584` — **15.0**, read out of the pinned `vampire.dll` at file offset `0x463584`.
	// The amount `0x102b7cf0`'s `0x8f` arm advances `m_flNextDodgeTime` (`+0x65a4`) by.
	inline constexpr float GTauntTimerAdvance = ElysiumNpcTunables::Fifteen;
}
