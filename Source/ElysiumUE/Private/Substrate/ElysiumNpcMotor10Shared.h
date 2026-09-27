#pragma once

// Story 5 step 5: the file-scope locals of `ElysiumNpcMotor10.cpp` that its staying bodies share with
// bodies moved to `FElysiumNpcBase`. Qualified at every use (`NpcKernelMotor10Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"

namespace NpcKernelMotor10Shared
{
	inline constexpr int32 GMotor10AimrOk = 0;
	// This world's centimetres into retail's Source units, Y negated. The same two lines as
	// `ElysiumNpcMotor.cpp`'s `SourceOf`; both are file-local so neither family owns the
	// other's file.
	inline FVector Motor10SourceOf(const FVector& Cm)
	{
		return FVector(Cm.X / ElysiumMove::U, -Cm.Y / ElysiumMove::U, Cm.Z / ElysiumMove::U);
	}
	// The global used-hull mask `DAT_10610be8`. `.data`, initialised on disk to `0xffffffff` and
	// zeroed by `0x102f9900` at the start of every node-graph build; nothing in this runtime builds
	// one, so it stands at retail's post-clear value. File-static because it is global in retail.
	inline int32 GMotor10UsedHullBits = 0;
}
