#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcConditionsBodies.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelConditionsShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

namespace NpcKernelConditionsShared
{
	// This runtime's state vocabulary onto retail's `m_NPCState` ids — the same table
	// `FElysiumNpcBase::NpcStateFlags` uses, restated here as a free function because every body in this
	// file switches on the retail id rather than on the port enum. The retail states this runtime has
	// no member for (8 FLEE, 0xb HUNT, 0xe the criminal window) are unreachable through it, which is
	// why the rules below are written over the RAW id and the dispatch is the only place that maps.
	inline int32 CondRetailStateId(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		}
		return 0;
	}
	inline constexpr int32 GCondNavJump = 1;
	inline constexpr int32 GCondNavClimb = 3;
}
