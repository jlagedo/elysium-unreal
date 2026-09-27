#pragma once

// Story 5 step 5: the file-scope locals of `ElysiumNpcAnim.cpp` that its staying bodies share with
// bodies moved to `FElysiumNpcBase`. Qualified at every use (`NpcKernelAnimShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelAnimShared
{
	// The NPC's `m_NPCState` in RETAIL's ordinals — `{0 NONE, 1 IDLE, 2 COMBAT, 3 ALERT, 4 SCRIPT,
	// 6 PRONE, 7 DEAD}`. `CanPlaySequence` and the base `ShouldMaintainActivity` both compare raw
	// numbers, so the mapping has to happen before the comparison rather than after it.
	inline int32 AnimRetailNpcState(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		default:                         return 0;
		}
	}
	// `_DAT_104493d0`, the epsilon `AddSceneEvent`, `ProcessGestureSceneEvent` and the Troika loud
	// expression all add before a divide or a deadline. Read out of the pinned image's `.rdata` as a
	// double; every use narrows it to float exactly as retail's `FADD double ptr` then `FSTP float`
	// does.
	inline constexpr double GAnimTimeEpsilon = 0.0001;
	inline constexpr int32 GAnimActDisposition = 0xf1;        // ACT_DISPOSITION, the whole-request retry
}
