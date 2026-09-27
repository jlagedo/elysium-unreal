#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcBosses.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelBossesShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

namespace NpcKernelBossesShared
{
	// Retail's `.rdata`, one line per constant. Distances are SOURCE units.
	inline constexpr float BossesZero = ElysiumNpcTunables::Zero;   // also `_DAT_1044fab0`, the double zero
	// `0x1038b370`'s watchdog triple is `_DAT_1093b8b8..c0` plus `_DAT_1093b8c4` — FILE STATICS, one
	// per level rather than one per NPC, installed by the `atexit`-registered initializer at the top
	// of the body. Ported as a static for exactly that reason.
	inline FElysiumNpc::FManBatStationaryWatch GManBatWatch;
}
