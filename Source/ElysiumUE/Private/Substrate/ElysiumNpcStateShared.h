#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcState.cpp` that its staying bodies share with
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
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

namespace NpcKernelState19Shared
{
	inline bool State19HasCondition(const FElysiumNpcBase& Npc, EElysiumNpcCond Cond)
	{
		return Npc.Cognition.Conditions.Has(Cond);
	}
	// Retail additionally stamps `m_SelectIdealStateTrace`'s `__FILE__`/`__LINE__` pair
	// (`+0x1b3c`/`+0x1b40`) at every one of these sites. The shape map calls that pair ABSENT; the
	// mind's transition trace carries the same account, so only the retail LINE is recorded here,
	// as the arm's name.
	inline void State19StampIdeal(FElysiumNpcBase& Npc, int32 RetailId, int32 Line)
	{
		Npc.WriteIdealStateRetail(RetailId);
		Npc.RecordScheduleEvent(FString::Printf(TEXT("SelectIdealState :%d -> %d"),
			Line, RetailId));
	}
	inline int32 GState19CopCensus = 0;
	inline EElysiumNpcState State19TypedFromRetail(int32 RetailId, EElysiumNpcState Fallback)
	{
		switch (RetailId)
		{
		case 1: return EElysiumNpcState::Idle;
		case 2: return EElysiumNpcState::Combat;
		case 3: return EElysiumNpcState::Alert;
		case 4: return EElysiumNpcState::Scripted;
		case 6: return EElysiumNpcState::Prone;
		case 7: return EElysiumNpcState::Dead;
		default: return Fallback;
		}
	}
	inline bool State19HasInterrupt(FElysiumNpcBase& Npc, EElysiumNpcCond Cond)
	{
		return ElysiumSchedule::HasInterruptCondition(
			Npc.Schedule, Npc, Npc.Cognition.Conditions, Cond);
	}
}
