#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcSpeciesLifecycle10.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelSpeciesLifecycle10Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

namespace NpcKernelSpeciesLifecycle10Shared
{
	// `DAT_1093fac4`, the process-wide debug word beside the `werewolf_show_debug` ConVar — the same
	// shape family SaveRestore10 gave `DAT_1093acac`/`DAT_1093acb0`. The ConVar itself is the table's.
	inline int32 GWerewolfShowDebug = 0;
	// `CBaseEntity::m_pPlayer` (`+0xa8`), the self-downcast cache that is non-null on exactly the
	// player. This runtime has no such cache: "is the player" is `Handle == World->PlayerHandle()`,
	// which is the reading families Senses and Debug10 already made (`IsPlayerRecord`,
	// `CameraSecurityQuerySeeEntity`) and is repeated as a function rather than as a third reading.
	inline bool SpeciesLifecycle10IsPlayer(const FElysiumNpc& Npc, const FElysiumEntity* Candidate)
	{
		return Candidate != nullptr && Npc.World != nullptr
			&& Candidate->Handle == Npc.World->PlayerHandle();
	}
}
