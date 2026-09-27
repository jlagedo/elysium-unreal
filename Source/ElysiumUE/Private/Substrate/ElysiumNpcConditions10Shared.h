#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcConditions10.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelConditions10Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "ElysiumWorldServices.h"

namespace NpcKernelConditions10Shared
{
	// `m_afMemory`'s top bit, the one the three `TaskFail` species arms clear
	// (`10379077`/`10380536`/`103ba376`: `AND dword ptr [ESI + 0x5d8c],0x7fffffff`).
	inline constexpr uint32 GCond10MemoryTopBit = 0x80000000u;
	// `m_NPCState == 2` — retail's COMBAT ordinal (`10379063 CMP dword ptr [ESI+0x5cc0],0x2`).
	inline constexpr int32 GCond10NpcStateCombat = 2;
	// The 0.75 s ignore-collision re-arm the Hengeyokai and Tzimisce arms pass to `0x102c43b0`
	// (`103805a6` / `103ba3e6`, `PUSH 0x3f400000`).
	inline constexpr float GCond10PickupReuseDelay = 0.75f;
	// `m_NPCState` in RETAIL's ordinals — the same mapping family Sounds recovered from
	// `0x1026e3e0`'s table.
	inline int32 Cond10RetailNpcState(EElysiumNpcState State)
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
	inline constexpr int32 GCond10_D_ER = 0;
	inline constexpr int32 GCond10_D_HT = 1;
	inline constexpr int32 GCond10_D_FR = 2;
	inline double Cond10Now(const FElysiumNpc& Npc)
	{
		// `gpGlobals->curtime`, `*(float*)(DAT_1070b228 + 0xc)`.
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}
	// The failure-code window both the AsianVampire and the ChangBros arms gate on:
	// `if (0xb < code && code < 0x10)`, i.e. 12..15 (`103623ca` / `1036d20a`).
	inline constexpr int32 GCond10PathFailFirst = 0xc;
	inline constexpr int32 GCond10PathFailLast = 0xf;
}
