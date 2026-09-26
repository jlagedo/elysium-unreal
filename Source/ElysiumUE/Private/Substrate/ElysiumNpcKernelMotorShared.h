#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcKernelMotor.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelMotorShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelMotorShared
{
	inline constexpr float GTestHullTunable = ElysiumNpcTunables::Forty;   // `0x102d72b0` / `0x102d72d0`
	// The yaw-speed ladder's constants. `GYawFloor` is also the clamp every "turning" arm ends on.
	inline constexpr float GYawDefault = ElysiumNpcTunables::FortyFive;
	inline constexpr float GYawCrouch = ElysiumNpcTunables::Thirty;
	// Retail activity numbers the yaw ladders switch on. Named so the switch reads like the binary.
	inline constexpr int32 GActIdle = 1;
	inline constexpr int32 GActIdleAngry = 5;
	inline constexpr int32 GActRun = 0x13;
	inline constexpr int32 GActCrouchIdle = 0x3b;
	inline constexpr int32 GActCrouchWalk = 0x3c;
	// `m_afMemory` (+0x5d8c) bit 0x2000 — the "turning" tag the turn ladder writes (the Facing
	// family's `bTagsTurnMemory`) and all three of the yaw-speed overrides branch on.
	inline constexpr uint32 GMemoryTurning = 0x2000;
	// This world's centimetres into retail's Source units. `Origin` is Unreal's axes, where
	// `bsp.source_to_unreal` negated Y; every retail body reads `GetAbsOrigin()`, so the sign comes
	// back here and stays back for the whole body.
	inline FVector SourceOf(const FVector& Cm)
	{
		return FVector(Cm.X / ElysiumMove::U, -Cm.Y / ElysiumMove::U, Cm.Z / ElysiumMove::U);
	}
}
