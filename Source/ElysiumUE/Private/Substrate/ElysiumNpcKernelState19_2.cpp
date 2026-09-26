#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelState19_2Shared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29e, family **State19** — species SelectIdealState bodies. Unity-build names in this
// file are prefixed `State19_2` so they cannot collide with `ElysiumNpcKernelState19.cpp`.

namespace
{
	constexpr int32 GState19_2Slot461 = 461;


}

int32 FElysiumNpc::HumanSelectIdealState()
{
	SelectIdealStateSelector = 0x14;
	const int32 State = NpcStateRetail();
	if (State != 2)
	{
		if (State == 0xb)
		{
			if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy))
			{
				NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0x3ec);
				return IdealStateRetail();
			}
			if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
			{
				NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0x3f1);
				return IdealStateRetail();
			}
			if (ShouldGoToIdleState())
			{
				NpcKernelState19_2Shared::State19_2Stamp(*this, 1, 0x3f6);
				return IdealStateRetail();
			}
		}
		return NpcKernelState19_2Shared::State19_2ChainTroika(*this);
	}

	if ((ScheduleHost.MemoryBits & 0x2) != 0
		&& (NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::SeeEnemy)
			|| NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::NewEnemy)))
	{
		ScheduleHost.MemoryBits &= ~0x2u;
		Cognition.Conditions.Set(EElysiumNpcCond::ScheduleDone);
	}

	if (GetEnemy() != nullptr && !NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::LostEnemy))
	{
		return NpcKernelState19_2Shared::State19_2ChainTroika(*this);
	}

	if (NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::SeeEnemy))
	{
		NpcKernelState19_2Shared::State19_2Stamp(*this, bNoAlertState ? 1 : 3, bNoAlertState ? 0x3a3 : 0x3a7);
	}
	else
	{
		const FElysiumEntity* const Boss = World != nullptr
			? World->Resolve(FollowerBoss) : nullptr;
		if (Boss != nullptr)
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, bNoAlertState ? 1 : 3, bNoAlertState ? 0x3b0 : 0x3b4);
		}
		else if (!HuntConVarIsCommand && HuntConVarRawWord != 0.f)
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 0xb, 0x3bc);
		}
		else
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, bNoAlertState ? 1 : 3, bNoAlertState ? 0x3c2 : 0x3c6);
		}
	}
	UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s ***Combat state with no enemy!"), *DebugString());
	return IdealStateRetail();
}

int32 FElysiumNpc::AnimalSelectIdealState()
{
	SelectIdealStateSelector = 5;
	const int32 State = NpcStateRetail();
	if (State == 2)
	{
		if (GetEnemy() == nullptr)
		{
			bool bTook = false;
			if (NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::SeeEnemy))
			{
				bTook = true;
				NpcKernelState19_2Shared::State19_2Stamp(*this, bNoAlertState ? 1 : 3, bNoAlertState ? 0x519 : 0x51d);
			}
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s ***Combat state with no enemy!"),
				*DebugString());
			if (bTook)
			{
				return IdealStateRetail();
			}
		}
		else if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::LostEnemy))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, bNoAlertState ? 1 : 3, bNoAlertState ? 0x529 : 0x52d);
			return IdealStateRetail();
		}
	}
	else if (State == 8)
	{
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::LostEnemy))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, bNoAlertState ? 1 : 3, bNoAlertState ? 0x53c : 0x540);
			return IdealStateRetail();
		}
	}
	else if (State == 0xa)
	{
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeFear))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 8, 0x54d);
			return IdealStateRetail();
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			if (NpcKernelState19_2Shared::State19_2Rand99() < 30)
			{
				NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0x555);
			}
			else
			{
				NpcKernelState19_2Shared::State19_2Stamp(*this, 8, 0x55a);
			}
			return IdealStateRetail();
		}
	}
	return NpcKernelState19_2Shared::State19_2ChainTroika(*this);
}

// `FUN_103723f0` — `CNPC_VCop::vfunc461`'s idle/alert pre-pass, and the only caller. Every one of
// its seven condition tests is the BARE form (`0x10269aa0`): a cop reacts to a gunshot or a hit
// whether or not its running program lists the condition as an interrupt. It answers `2` (COMBAT)
// when it takes and `0` when it does not, and `0` is what sends the cop on to the chain.
int32 FElysiumNpc::CopSelectIdealStatePrePass()
{
	FElysiumEntity* const Closest = NpcKernelState19_2Shared::State19_2Resolve(*this, Senses.Memory.ClosestPlayer);

	bool bSoundFromThePlayer = false;
	// `10372402`: INVESTIGATE_SOUND **and** BEING_ATTACKED, both, before either sound is looked at.
	if (NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::InvestigateSound)
		&& NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::BeingAttacked))
	{
		if (NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::HearCombat)
			&& NpcKernelState19_2Shared::State19_2Resolve(*this, Senses.Memory.LastSoundCombat.Source) == Closest)
		{
			bSoundFromThePlayer = true;
		}
		// `1037249e`: this second test runs even after the first has hit — no `else`.
		if (NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::HearBulletImpact)
			&& NpcKernelState19_2Shared::State19_2Resolve(*this, Senses.Memory.LastSoundBulletImpact.Source) == Closest)
		{
			bSoundFromThePlayer = true;
		}
	}

	bool bTake = bSoundFromThePlayer;
	// `1037252b`: `m_bCondTookDamage` is cleared whenever any damage condition stands, whether or
	// not the attacker turns out to be the closest player.
	if (NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::LightDamage)
		|| NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::HeavyDamage)
		|| NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::RepeatedDamage))
	{
		Cognition.bCondTookDamage = false;
		if (NpcKernelState19_2Shared::State19_2Resolve(*this, Senses.Memory.LastDamageAttacker) == Closest)
		{
			bTake = true;
		}
	}
	if (!bTake)
	{
		return 0;
	}
	Slot597(Closest, 10);
	Slot596(Closest);
	NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0x527);
	return 2;
}

