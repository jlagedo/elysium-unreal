#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcDamage2.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelDamage2Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

namespace NpcKernelDamage2Shared
{
	// Retail's `.rdata`, one line per constant. Distances are SOURCE units.
	inline constexpr float ThrowIgnoreCollisionSeconds = 0.75f;   // `0x102c43b0`'s argument
}
