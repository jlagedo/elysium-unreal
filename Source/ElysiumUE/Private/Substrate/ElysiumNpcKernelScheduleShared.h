#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcKernelSchedule.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelScheduleShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

namespace NpcKernelScheduleShared
{
	// `_DAT_10451acc` — the height-difference threshold the melee height-diff timer arms above, the
	// pooled 64.0f: an enemy within 64 units of this NPC's own height counts as level and the timer
	// is held at -1.0.
	inline constexpr float GScheduleMeleeHeightDiffUnits = ElysiumNpcTunables::SixtyFour;
	// The melee height-diff timer's "unarmed" value, `0xbf800000` = -1.0f, written as an absolute
	// curtime in retail and carried as a double here.
	inline constexpr double GScheduleMeleeTimerUnarmed = -1.0;
	// Slot 308 `HasUsableRangedWeapon`, the split every melee selector turns on. The generated slot
	// is a stub answering false; the port already carries the fact through the item catalogue, and
	// the brief's rule is to read the port's member rather than a stub, so this is the real answer
	// and slot 308 is named beside it.
	inline bool HasUsableRangedWeaponPort(const FElysiumNpc& Npc)
	{
		return ElysiumNpcCond::WeaponCapability(Npc) == ElysiumNpcCond::ECapability::Ranged;
	}
	// The height-difference timer every melee selector runs, byte-identical in all six bodies:
	//
	//   armed = false;
	//   if (m_flEnemyHeightDiff <= _DAT_10451acc)      m_flMeleeHeightDiffTimer = -1.0f;
	//   else if (m_flMeleeHeightDiffTimer == -1.0f)    m_flMeleeHeightDiffTimer = curtime +
	//                                                      RandomFloat(3.0, 4.0);
	//   else if (m_flMeleeHeightDiffTimer <= curtime)  armed = true;
	//
	// `CNPC_VSabbatLeader` runs only the first two arms and never reads the answer, which is
	// reproduced by discarding it there.
	inline bool TickMeleeHeightDiffTimer(FElysiumNpc& Npc, double Now)
	{
		if (Npc.ScheduleHost.EnemyHeightDiffUnits <= GScheduleMeleeHeightDiffUnits)
		{
			Npc.MeleeHeightDiffTimer = GScheduleMeleeTimerUnarmed;
			return false;
		}
		if (Npc.MeleeHeightDiffTimer == GScheduleMeleeTimerUnarmed)
		{
			Npc.MeleeHeightDiffTimer = Now
				+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(3.0f, 4.0f);
			return false;
		}
		return Npc.MeleeHeightDiffTimer <= Now;
	}
}
