#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcPositions2.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelPositions2Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelShape.h"

namespace NpcKernelPositions2Shared
{
	inline constexpr float GPositionsTailRetailOne = ElysiumNpcTunables::One;
	inline constexpr float GPositionsTailU = ElysiumMove::U;
}
