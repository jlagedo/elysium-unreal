#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcAnim10.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelAnim10Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "ElysiumAnimationIntent.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumReactions.h"
#include "Visual/ElysiumActionTables.h"

namespace NpcKernelAnim10Shared
{
	inline constexpr int32 GAnim10ActFidget = 3;           // ACT_FIDGET
	inline constexpr int32 GAnim10ActIdle = 1;             // ACT_IDLE
	inline constexpr int32 GAnim10ActWalk = 9;             // ACT_WALK
	inline constexpr int32 GAnim10ActRun = 0x13;           // ACT_RUN
	inline constexpr int32 GAnim10ActTzWalk2 = 0x1136;    // 4406
	inline constexpr int32 GAnim10ActWalkRelaxed = 0x16;   // the aggressive-clear rewrite of ACT_WALK
	inline constexpr int32 GAnim10ActRunRelaxed = 0x17;    // ... and of ACT_RUN
	inline constexpr int32 GAnim10CapNoAimGait = 0x40;
	inline constexpr uint32 GAnim10WeaponRangedAim = 0x6000;
	using EAnim10ConVar = ElysiumNpcTunables::EConVar;
	// Retail's `m_NPCState` ordinals, the same table families Anim, Conditions and Sounds carry.
	// **The retail enum is wider than this runtime's vocabulary** — 5, 6, 8, 9, 0xa, 0xb, 0xc, 0xd
	// and 0xe have no `EElysiumNpcState` member — so the arms of `0x1029e750` that name them are
	// ported and UNREACHABLE. Named at the body.
	inline int32 Anim10RetailNpcState(EElysiumNpcState State)
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
}
