#pragma once

// Story 5 step 4: the file-scope locals of the former `ElysiumNpcKernelAnim10_2.cpp`, whose bodies all
// moved to their species classes; the classes that share a local read it here. Qualified at every use (`NpcKernelAnim10_2Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSchedule.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

namespace NpcKernelAnim10_2Shared
{
	inline FRandomStream& Anim10_2Rng()
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	}
	//
	// `DAT_10924a1c` is the melee-range ConVar every selector thresholds on,
	// `debug_melee_advance_combatmove_dist` "100". `MeleeRangeUnits()` (family TroikaHelpers) reads
	// it and is called here so the six bodies cannot drift.
	inline constexpr float GAnim10_2FarMargin = 200.0f;       // _DAT_104492b8
	inline constexpr float GAnim10_2HeightBand = ElysiumNpcTunables::SixtyFour;
	inline constexpr double GAnim10_2TimerUnarmed = -1.0;     // 0xbf800000
	inline constexpr float GAnim10_2RetryMin = 3.0f;          // RandomFloat(3.0, 4.0)
	inline constexpr float GAnim10_2RetryMax = 4.0f;
	inline constexpr int32 GAnim10_2RollFloor = 0x18;         // `CMP EAX,0x19; JL` — the roll must EXCEED 24
	// The height-difference retry stamp all six slot-604 bodies run, byte for byte:
	//
	//   armed = false;
	//   if (m_flEnemyHeightDiff <= 64.0)              m_flMeleeHeightDiffTimer = -1.0f;
	//   else if (m_flMeleeHeightDiffTimer == -1.0f)   m_flMeleeHeightDiffTimer = curtime +
	//                                                     RandomFloat(3.0, 4.0);
	//   else if (m_flMeleeHeightDiffTimer <= curtime) armed = true;
	//
	// Family Schedule spells the identical helper for the five bodies it ported; it is a file-static
	// there, so the six lines are repeated here with the same citation rather than a second reading
	// being made. The `<=` on the first arm and on the expiry are both read at `10386117`
	// (`AND EAX,0x4100; JNZ`) and `10386161` (`AND EAX,0x100; JNZ`).
	inline bool Anim10_2TickHeightDiffTimer(FElysiumNpc& Npc, double Now)
	{
		if (Npc.ScheduleHost.EnemyHeightDiffUnits <= GAnim10_2HeightBand)
		{
			Npc.MeleeHeightDiffTimer = GAnim10_2TimerUnarmed;
			return false;
		}
		if (Npc.MeleeHeightDiffTimer == GAnim10_2TimerUnarmed)
		{
			Npc.MeleeHeightDiffTimer =
				Now + NpcKernelAnim10_2Shared::Anim10_2Rng().FRandRange(GAnim10_2RetryMin, GAnim10_2RetryMax);
			return false;
		}
		return Npc.MeleeHeightDiffTimer <= Now;
	}
	// `(**(code **)(*(int *)this + 0x4d0))()` — slot 308 `HasUsableRangedWeapon`, the split every
	// arm of every melee selector turns on. The generated slot is a stub answering false; family
	// Schedule reads the port's own catalogue answer instead and names slot 308 beside it, and so
	// does this.
	inline bool Anim10_2HasRangedWeapon(const FElysiumNpc& Npc)
	{
		return ElysiumNpcCond::WeaponCapability(Npc) == ElysiumNpcCond::ECapability::Ranged;
	}
}
