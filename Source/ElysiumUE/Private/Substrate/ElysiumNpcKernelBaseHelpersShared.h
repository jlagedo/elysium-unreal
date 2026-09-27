#pragma once

// Story 5 step 5: the file-scope locals of `ElysiumNpcKernelBaseHelpers.cpp` that its staying bodies share with
// bodies moved to `FElysiumNpcBase`. Qualified at every use (`NpcKernelBaseHelpersShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcThinkCadence.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelBaseHelpersShared
{
	// The band both melee-condition bodies share with the two ranged ones.
	inline constexpr float GDatAttackBandUnits = ElysiumNpcTunables::SixtyFour;
}
