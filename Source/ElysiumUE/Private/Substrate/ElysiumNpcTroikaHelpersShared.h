#pragma once

// Story 5 step 5: the file-scope locals of `ElysiumNpcTroikaHelpers.cpp` that its staying bodies share with
// bodies moved to `FElysiumNpcBase`. Qualified at every use (`NpcKernelTroikaHelpersShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumStanceTypes.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelTroikaHelpersShared
{
	// `DAT_10937cf2` — slot 334's global "a discipline is off cooldown" byte. A retail GLOBAL and
	// ported as one: one flag for the whole level, not one per NPC.
	inline bool GTroikaDisciplineReadyFlag = false;
}
