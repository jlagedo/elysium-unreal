#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcDamage.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelDamageShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumDecalSubsystem.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumReactions.h"

namespace NpcKernelDamageShared
{
	inline constexpr int32 HitGroupHead = 1;
	// Retail's `.rdata`, one line per constant. Distances are SOURCE units.
	inline constexpr float DamageZero = ElysiumNpcTunables::Zero;
	// `_DAT_1070ba40`/`44`/`48`. ONE per level, as retail's file-static triple is.
	inline FVector GDeathThrowImpulse = FVector::ZeroVector;
}
