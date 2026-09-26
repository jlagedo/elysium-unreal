#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcKernelLifecycle19_2.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelLifecycle19_2Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumWeaponClasses.h"

namespace NpcKernelLifecycle19_2Shared
{
	inline double Lifecycle19_2Now(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}
	inline void Lifecycle19_2LawNever(FElysiumNpc& Npc)
	{
		Npc.PlInvestigate = FElysiumNpc::LawThresholdNever;
		Npc.PlCriminalFlee = FElysiumNpc::LawThresholdNever;
		Npc.PlCriminalAttack = FElysiumNpc::LawThresholdNever;
		Npc.PlSupernaturalFlee = FElysiumNpc::LawThresholdNever;
		Npc.PlSupernaturalAttack = FElysiumNpc::LawThresholdNever;
	}
	inline void Lifecycle19_2HatePlayerClass(FElysiumNpc& Npc)
	{
		Npc.Relationships.SetClass(TEXT("player"), EElysiumRelationship::Hate, 10);
	}
}
