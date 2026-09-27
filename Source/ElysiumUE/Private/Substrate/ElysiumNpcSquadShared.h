#pragma once

// Story 5 step 5: the file-scope locals of `ElysiumNpcSquad.cpp` that its staying bodies share with
// bodies moved to `FElysiumNpcBase`. Qualified at every use (`NpcKernelSquadShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

namespace NpcKernelSquadShared
{
	// The slot-546 rows no C++ class carries yet: the Troika line, which carries no id space at
	// all, and the controller line (`CNPC_VFrenzyShadow`, `CNPC_VPlayerController` sharing
	// `CNPC_VVampire`'s body and space, `CNPC_VWolfMorph`), deferred to the controller fold (step 7).
	// Every introduced species holds its own row in its `SquadSlotName` override (story 5 step 4).
	// Columns: the retail class, the body that fills slot 546 for it (checkable against
	// `docs/vtmb/npc-kernel/slots.md`) and its own `CAI_ClassScheduleIdSpace`. The three range
	// fields take the struct's defaults, which ARE the recovered state every one of the 56 spaces
	// was left in by `0x102ea090(isRoot = false)`.
	inline constexpr FElysiumNpcBase::FSquadSlotSpecies GNpcKernelSquadSlotSpecies[] = {
		// The Troika line itself: `CAI_BaseNPC::SquadSlotName` looks the id up directly.
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x101a6c00"), TEXT("") },
		{ TEXT("CNPC_VFrenzyShadow"), TEXT("0x10375440"), TEXT("0x1093ae28") },
		{ TEXT("CNPC_VPlayerController"), TEXT("0x103c4a80"), TEXT("0x1093d2a4") },
		{ TEXT("CNPC_VWolfMorph"), TEXT("0x103dc950"), TEXT("0x1094028c") },
	};
}
