#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcHints.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelHintsShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelHintsShared
{
	inline EElysiumNpcCond HintsCond(int32 RetailCondition)
	{
		return static_cast<EElysiumNpcCond>(RetailCondition);
	}
	// `_DAT_104454c4` — the shared 0.0f float constant of `vampire.dll` (1,328 readers, no writer;
	// `0x102961a0` compares a squared length against it to produce a "non-zero length" bool, and
	// `0x1026a910`'s whole body is `FLD [0x104454c4] / RET 4`). It is what `GetHintDelay` answers
	// and the Z scale `DistToHintCenterLine2D_3` multiplies the line direction's Z by — which is why
	// that body is a 2D distance despite carrying a Z term.
	inline constexpr float GHintsZero = ElysiumNpcTunables::Zero;
}
