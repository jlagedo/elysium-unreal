#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcKernelSpeciesMisc10_2.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelSpeciesMisc10_2Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"

namespace NpcKernelSpeciesMisc10_2Shared
{
	inline double SpeciesMisc10_2Now(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}
	// The stat ids, in retail's numbering. `0x0f` is the accumulated WOUND counter (family
	// Combat10's `GStatWounds`), which is why `Set(0x0f, 0)` is a full heal.
	inline constexpr int32 GStatWounds = 0x0f;
}
