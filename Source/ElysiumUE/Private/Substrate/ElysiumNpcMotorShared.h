#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcMotor.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelMotorShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelMotorShared
{
	// The slot 516 `MaxYawSpeed` ladders' constants (0019/6: restored as data -- the rate is how fast
	// an NPC turns, a player-visible timing; the turning itself is the mover's).
	inline constexpr float GYawDefault = ElysiumNpcTunables::FortyFive;   // `_DAT_1049949c`
	inline constexpr float GYawCrouch = ElysiumNpcTunables::Thirty;       // `_DAT_104492a8`
	inline constexpr float GYawRun = ElysiumNpcTunables::YawSpeedRun;     // `_DAT_1047a3ac`
	inline constexpr float GYawFloor = ElysiumNpcTunables::One;           // `_DAT_104454c0`, the turning arm's floor
	// Retail's ACT_IDLE (1), and the activity numbers the yaw ladders switch on.
	inline constexpr int32 GActIdle = 1;
	inline constexpr int32 GActIdleAngry = 5;
	inline constexpr int32 GActWalk = 9;
	inline constexpr int32 GActRun = 0x13;
	inline constexpr int32 GActCrouchIdle = 0x3b;
	inline constexpr int32 GActCrouchWalk = 0x3c;
	// `m_afMemory` (+0x5d8c) bit 0x2000 -- the "turning" tag the turn ladder writes (the Facing
	// family's `bTagsTurnMemory`) and the Troika / Dog / Tzimisce yaw ladders branch on first.
	inline constexpr uint32 GMemoryTurning = 0x2000;
	// This world's centimetres into retail's Source units. `Origin` is Unreal's axes, where
	// `bsp.source_to_unreal` negated Y; every retail body reads `GetAbsOrigin()`, so the sign comes
	// back here and stays back for the whole body.
	inline FVector SourceOf(const FVector& Cm)
	{
		return FVector(Cm.X / ElysiumMove::U, -Cm.Y / ElysiumMove::U, Cm.Z / ElysiumMove::U);
	}
	inline constexpr float GStepHeightBase = ElysiumNpcTunables::StepHeightBase;   // `0x101a6b40` / `0x101a6b60`
	// `FUN_10280790`'s slack and its apex scale. Both are qword loads the x87 widens, so they are
	// doubles in `.rdata` and floats at the comparison.
	inline constexpr float GJumpLegalSlack = static_cast<float>(ElysiumNpcTunables::TenthDouble);
	inline constexpr float GMaxJumpSpeedTroika = ElysiumNpcTunables::MaxJumpSpeedTroika;   // `0x101aa670`
	// `CAI_BaseNPC::IsJumpLegal` `0x10280880`'s three immediates, pushed right to left into
	// `FUN_10280790`: `10280892 PUSH 0x42a00000` (rise 80), `1028088d PUSH 0x437a0000` (drop 250),
	// `10280888 PUSH 0x43200000` (distance 160). The per-class movement tunables are overrides since
	// story 5 (the test hull's 40 / 40 / 1024s on `FElysiumNpcTestHull`); commit B collapsed the
	// class-keyed row table these sat in.
	inline constexpr float GJumpLegalRise = 80.0f;
	inline constexpr float GJumpLegalDrop = 250.0f;
	inline constexpr float GJumpLegalDistance = 160.0f;
}
