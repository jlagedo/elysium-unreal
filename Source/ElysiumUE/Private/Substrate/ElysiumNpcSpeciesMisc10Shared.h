#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcSpeciesMisc10.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelSpeciesMisc10Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Misc/FileHelper.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"

namespace NpcKernelSpeciesMisc10Shared
{
	// `+0xa8 m_pPlayer`, `CBaseEntity`'s player self-downcast cache: non-null on exactly the player.
	inline bool SpeciesMisc10IsPlayer(const FElysiumNpc& Npc, const FElysiumEntity* Candidate)
	{
		return Candidate != nullptr && Npc.World != nullptr
			&& Candidate->Handle == Npc.World->PlayerHandle();
	}
	inline double SpeciesMisc10Now(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}
}
