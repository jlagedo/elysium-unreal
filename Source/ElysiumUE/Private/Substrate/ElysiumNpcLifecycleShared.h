#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcLifecycle.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelLifecycleShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"

namespace NpcKernelLifecycleShared
{
	// `DAT_1093d638` / `DAT_1093d63c` — `CNPC_VWerewolf`'s search-timer pair. FILE STATICS in retail,
	// shared by every werewolf on the map, which is the recovered fact and not an accident.
	inline uint64 GSearchTimerCycles = 0;
}
