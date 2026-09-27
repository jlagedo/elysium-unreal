#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcGeometry.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelGeometryShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

namespace NpcKernelGeometryShared
{
	// The pooled half, read by both of this family's bodies that need one — `BodyTarget`'s plain
	// midpoint arm and `StandingOnPlayer`'s half-diagonal.
	inline constexpr float GRetailHalf = ElysiumNpcTunables::Half;
}
