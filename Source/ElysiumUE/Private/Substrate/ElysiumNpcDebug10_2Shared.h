#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcDebug10_2.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelDebug10_2Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"

namespace NpcKernelDebug10_2Shared
{
	inline constexpr int32 GDebug10_2BitWeaponRings = 0x20000000;// 0x1029cd68
	inline constexpr TCHAR GDebug10_2Circle[] = TEXT("NDebugOverlay::Circle");
	inline constexpr TCHAR GDebug10_2Text[] = TEXT("NDebugOverlay::Text");
}
