#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcLifecycle19.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelLifecycle19Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

namespace NpcKernelLifecycle19Shared
{
	inline constexpr uint32 GFlOnGround = 1u;
	inline double Lifecycle19Now(const FElysiumNpcBase& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}
	inline constexpr float GFltMax = 3.402823466e+38f;
	inline FElysiumEntity* Lifecycle19ResolveHandle(const FElysiumNpcBase& Npc, const FElysiumEntityHandle& Handle)
	{
		if (Npc.World == nullptr || !Handle.IsSet())
		{
			return nullptr;
		}
		return Npc.World->Resolve(Handle);
	}
}
