#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcCombat10.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelCombat10Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcWitness.h"

namespace NpcKernelCombat10Shared
{
	// The `CVStatList_t` list types the `+0x13bc`/`+0x13c0` scan looks for.
	inline constexpr int32 GStatListTypeSheet = 0;      // the character sheet — the one this runtime stands
	inline constexpr int32 GStatWounds = 0x0f;        // ElysiumSlot::Health (15) — damage TAKEN
	inline constexpr int32 GStatMaxHealth = 0x11;     // ElysiumSlot::MaxHealth (17)
	inline double Combat10Now(const FElysiumEntity& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}
}
