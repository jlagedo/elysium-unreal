#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcDebug2.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelDebug2Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelDebug2Shared
{
	// The overlay entry points, by retail name, so a captured line names the call it stands for.
	inline const TCHAR* const GNpcKernelDebug2Box = TEXT("NDebugOverlay::Box");
}
