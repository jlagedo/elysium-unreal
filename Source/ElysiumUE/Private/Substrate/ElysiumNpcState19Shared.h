#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcState19.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelState19Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

namespace NpcKernelState19Shared
{
	inline bool State19HasCondition(const FElysiumNpc& Npc, EElysiumNpcCond Cond)
	{
		return Npc.Cognition.Conditions.Has(Cond);
	}
	// Retail additionally stamps `m_SelectIdealStateTrace`'s `__FILE__`/`__LINE__` pair
	// (`+0x1b3c`/`+0x1b40`) at every one of these sites. The shape map calls that pair ABSENT; the
	// mind's transition trace carries the same account, so only the retail LINE is recorded here,
	// as the arm's name.
	inline void State19StampIdeal(FElysiumNpc& Npc, int32 RetailId, int32 Line)
	{
		Npc.WriteIdealStateRetail(RetailId);
		Npc.RecordScheduleEvent(FString::Printf(TEXT("SelectIdealState :%d -> %d"),
			Line, RetailId));
	}
	inline int32 GState19CopCensus = 0;
}
