#pragma once

#include "CoreMinimal.h"
#include "ElysiumRng.h"

// The engine's uniform integer draw as the NPC kernel makes it: `(*DAT_1070b244)->RandomInt(min,
// max)` -- vstdlib's `IUniformRandomStream`, vtable slot 2 (`+8`), both ends inclusive -- on the one
// stream every NPC body here draws from (`EElysiumRngStream::NpcSchedule`), so the NPC's draws stay
// one reproducible sequence. The kernel families' private copies of this one line were hoisted here
// (0018 story 4); a new draw site calls this, never a copy.
namespace ElysiumNpcEngineRandom
{
	inline int32 RandomInt(int32 Min, int32 Max)
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(Min, Max);
	}
}
