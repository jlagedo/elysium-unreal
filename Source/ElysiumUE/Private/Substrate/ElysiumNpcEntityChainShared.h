#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcEntityChain.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelEntityChainShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

namespace NpcKernelEntityChainShared
{
	// `_DAT_104454c4` — the image's shared `0.0f` (1,328 readers, no writer;
	// `docs/vtmb/npc-ai/shape.md` § slot 568).
	inline constexpr float GChainZero = 0.0f;
	// Retail's `SolidType_t` values the two standability bodies test by number.
	inline constexpr int32 GChainSolidBsp = 1;       // SOLID_BSP
	inline constexpr int32 GChainSolidVPhysics = 6;  // SOLID_VPHYSICS
}
