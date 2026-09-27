#pragma once

// Story 5 step 6: the file-scope locals of `ElysiumNpcBaseEntityChain.cpp` that its staying bodies share with
// bodies moved up the entity chain. Qualified at every use (`NpcBaseEntityChainShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

namespace NpcBaseEntityChainShared
{
	// `_DAT_104454c0` — the image's shared `1.0f` (`docs/vtmb/animation_and_movers.md` line 659
	// reads the same word as `1.0f`; `docs/vtmb/npc-ai/shape.md` line 1029 "clamped up to
	// `_DAT_104454c0 = 1.0`").
	inline constexpr float GChainOne = 1.0f;
}
