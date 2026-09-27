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
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelMotorShared
{
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
	inline constexpr float GStepHeightBase = ElysiumNpcTunables::StepHeightBase;   // `0x101a6b40` / `0x101a6b60`
	// `FUN_10280790`'s slack and its apex scale. Both are qword loads the x87 widens, so they are
	// doubles in `.rdata` and floats at the comparison.
	inline constexpr float GJumpLegalSlack = static_cast<float>(ElysiumNpcTunables::TenthDouble);
	inline constexpr float GMaxJumpSpeedTroika = ElysiumNpcTunables::MaxJumpSpeedTroika;   // `0x101aa670`
	// Slots 521/522/523 — the movement tunables. `CAI_BaseNPCTroika` is the row every spawnable
	// species answers with: step height from the base body slot 522 carries (`0x101a6b40`), jump
	// speed from Troika's own override of 523 (`0x101aa670`), jump legality from the base's 521
	// (`0x10280880`). `CAI_BaseNPC` is the branch answer for the non-Troika line, whose 523 returns
	// the SAME constant as its step height. `CAI_TestHull`'s five answers are its own class's
	// overrides (`FElysiumNpcTestHull`, story 5 fold A1), not a row here.
	inline constexpr FElysiumNpcBase::FJumpTunableSpecies GJumpTunableSpecies[] =
	{
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x101aa670"), NpcKernelMotorShared::GStepHeightBase, GMaxJumpSpeedTroika,
			80.0f, 250.0f, 160.0f },
		{ TEXT("CAI_BaseNPC"), TEXT("0x101a6b60"), NpcKernelMotorShared::GStepHeightBase, NpcKernelMotorShared::GStepHeightBase,
			80.0f, 250.0f, 160.0f },
	};
}
