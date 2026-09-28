#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcState19_2.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelState19_2Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelState19_2Shared
{
	inline bool State19_2HasInterrupt(FElysiumNpc& Npc, EElysiumNpcCond Cond)
	{
		return ElysiumSchedule::HasInterruptCondition(
			Npc.Schedule, Npc, Npc.Cognition.Conditions, Cond);
	}
	// Retail additionally stamps `m_SelectIdealStateTrace`'s `__FILE__`/`__LINE__` pair
	// (`+0x1b3c`/`+0x1b40`) at every one of these sites. The shape map calls that pair ABSENT; the
	// mind's transition trace carries the same account, so only the retail LINE is recorded here,
	// as the arm's name.
	inline void State19_2Stamp(FElysiumNpc& Npc, int32 RetailId, int32 Line)
	{
		Npc.WriteIdealStateRetail(RetailId);
		Npc.RecordScheduleEvent(FString::Printf(TEXT("SelectIdealState :%d -> %d"),
			Line, RetailId));
	}
	inline int32 State19_2Rand99()
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99);
	}
	inline int32 State19_2ChainAnimal(FElysiumNpc& Npc)
	{
		return Npc.AnimalSelectIdealState();
	}
	inline bool State19_2HasCondition(const FElysiumNpc& Npc, EElysiumNpcCond Cond)
	{
		return Npc.Cognition.Conditions.Has(Cond);
	}
	// The direct calls retail's species bodies make into the body they replace (story 5 step 3:
	// `step3/direct-calls.tsv`) — the Troika line's `0x102ad660`, the human line's `0x103851e0` and
	// the animal line's `0x1035fe80`.
	inline int32 State19_2ChainTroika(FElysiumNpc& Npc)
	{
		return Npc.TroikaSelectIdealState();
	}
	inline FElysiumEntity* State19_2Resolve(FElysiumNpc& Npc, const FElysiumEntityHandle& Handle)
	{
		return Npc.World != nullptr && Handle.IsSet() ? Npc.World->Resolve(Handle) : nullptr;
	}
	/** Slot 474 `GetBestSound` (`0x102b4520` on the Troika line) — `&m_BestSound`, so never null for
	 *  any class this port stands. The type word retail reads is `CSound +0x4`, this runtime's
	 *  `FElysiumGameSoundEvent::TypeMask`, and the two numberings are the same SOUND_* bits. */
	inline const FElysiumGameSoundEvent* State19_2BestSound(FElysiumNpc& Npc)
	{
		return static_cast<const FElysiumGameSoundEvent*>(Npc.GetBestSound());
	}
	inline int32 State19_2ChainHuman(FElysiumNpc& Npc)
	{
		return Npc.HumanSelectIdealState();
	}
}
