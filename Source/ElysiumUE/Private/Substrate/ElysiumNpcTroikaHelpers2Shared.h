#pragma once

// Story 5 step 5: the file-scope locals of `ElysiumNpcTroikaHelpers2.cpp` that its staying bodies share with
// bodies moved to `FElysiumNpcBase`. Qualified at every use (`NpcKernelTroikaHelpers2Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelTroikaHelpers2Shared
{
	inline constexpr float TroikaSharedOne = ElysiumNpcTunables::One;
}
