#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcSenses10.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelSenses10Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "ElysiumSessionSubsystem.h"

namespace NpcKernelSenses10Shared
{
	inline constexpr int32 GD_HT = 1;
	// The cop and hunter class statics. STATIC IN RETAIL — `DAT_1093ac3c` / `_DAT_1093aca8` are one
	// grudge every cop in the map shares, and `DAT_1093b650` / `_DAT_1093b658` are the hunter's.
	inline FElysiumEntityHandle GCopSuspect;
	inline double GCopSuspectExpiry = 0.0;
	inline FElysiumEntityHandle GHunterSuspect;
	inline double GHunterSuspectExpiry = 0.0;
	inline double NowOf(const FElysiumNpcBase& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}
	inline bool IsPlayerRecord(const FElysiumNpc& Npc, const FElysiumEntity* Candidate)
	{
		// `+0x00a8 m_pPlayer`, `CBaseEntity`'s self-downcast cache: non-null on exactly the player.
		return Candidate != nullptr && Npc.World != nullptr
			&& Candidate->Handle == Npc.World->PlayerHandle();
	}
	inline constexpr int32 GD_FR = 2;
}
