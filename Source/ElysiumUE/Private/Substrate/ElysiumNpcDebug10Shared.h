#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcDebug10.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelDebug10Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelDebug10Shared
{
	inline constexpr int32 GDebug10BitText = 0x1;             // 0x1029d4ff `TEST AL,0x1`
}
