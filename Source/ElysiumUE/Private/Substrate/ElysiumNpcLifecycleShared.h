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

// `NpcKernelLifecycleShared::GSearchTimerCycles` (`DAT_1093d638` / `DAT_1093d63c`, the werewolf
// search-timer pair) went with `StartSearchTimer` / `ReportSearchTimer` (dead, 0019/6); the includes above are what the includers still take from this header.
