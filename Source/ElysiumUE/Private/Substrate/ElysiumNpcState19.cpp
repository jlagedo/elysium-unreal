#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcState19Shared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// Story 29e, family **State19** — `SetState`, slot 460's base body, slot 461's Troika line and
// dispatcher. Species SelectIdealState bodies live in `ElysiumNpcState19_2.cpp`.

namespace
{

	int32 State19Rand99(FElysiumNpc& Npc)
	{
		(void)Npc;
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99);
	}
}

int32 FElysiumNpc::CopCensusCount()
{
	return NpcKernelState19Shared::GState19CopCensus;
}

void FElysiumNpc::SetCopCensusCount(int32 Value)
{
	NpcKernelState19Shared::GState19CopCensus = Value;
}

// =================================================================================================
// `0x1026e340` — `CAI_BaseNPC::SetState`.
// =================================================================================================

// =================================================================================================
// Slot 460 base — `CAI_BaseNPC::PreSelectIdealState` `0x1026f590`.
// =================================================================================================

// =================================================================================================
// Slot 461 base — `CAI_BaseNPC::SelectIdealState` `0x1026f660`.
// =================================================================================================

// =================================================================================================
// Slot 461 Troika — `CAI_BaseNPCTroika::SelectIdealState` `0x102ad660`.
// =================================================================================================

int32 FElysiumNpc::TroikaSelectIdealState()
{
	SelectIdealStateSelector = 2;
	switch (NpcStateRetail())
	{
	case 1:
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			// `102ad68c`: one roll in twenty dispatches vtable `+0x7bc` — slot 495
			// `SurprisedSound` (`0x10294660`), which family Sounds10 already stands.
			if (State19Rand99(*this) < 0x14)
			{
				++SelectIdealStateSlot495Calls;
				SurprisedSound();
			}
			NpcKernelState19Shared::State19StampIdeal(*this, 2, 0x44f7);
			return 2;
		}
		if (!bNoAlertState)
		{
			if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::LightDamage))
			{
				Cognition.bCondTookDamage = false;
				NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x451b);
				return 3;
			}
			if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HeavyDamage))
			{
				Cognition.bCondTookDamage = false;
				NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x4522);
				return 3;
			}
		}
		if (NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::SupernaturalFleeLevel))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 8, 0x452b);
			NpcFlags.Set(EElysiumNpcFlag::INITIAL_FLEE);
			return 8;
		}
		if (NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::CriminalFleeLevel))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 8, 0x4532);
			NpcFlags.Set(EElysiumNpcFlag::INITIAL_FLEE);
			return 8;
		}
		if (!bNoAlertState)
		{
			if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::InvestigateSound)
				|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::InvestigateSight)
				|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::IgnoreUnknown)
				|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::SeeUnknown)
				|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearDanger)
				|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearPlayer))
			{
				NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x453e);
				return 3;
			}
			if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearCombat)
				|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact))
			{
				NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x4545);
				return 3;
			}
			if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearWorld))
			{
				NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x454b);
				return 3;
			}
			if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearFlinch))
			{
				NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x4551);
				return 3;
			}
			if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::DetectedAttack))
			{
				NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x4557);
				return 3;
			}
		}
		break;
	case 3:
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 2, 0x4562);
			return 2;
		}
		if (NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::SupernaturalFleeLevel))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 8, 0x456b);
			NpcFlags.Set(EElysiumNpcFlag::INITIAL_FLEE);
			return 8;
		}
		if (NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::CriminalFleeLevel))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 8, 0x4573);
			NpcFlags.Set(EElysiumNpcFlag::INITIAL_FLEE);
			return 8;
		}
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::InvestigateSight)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::SeeUnknown)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::IgnoreUnknown))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x457c);
			return 3;
		}
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearPlayer))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x4585);
			return 3;
		}
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x458e);
			return 3;
		}
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearWorld))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x4596);
			return 3;
		}
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::InvestigateSound))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x459d);
			return 3;
		}
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HearFlinch))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x45a4);
			return 3;
		}
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::DetectedAttack))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x45ab);
			return 3;
		}
		if (ShouldGoToIdleState())
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 1, 0x45b1);
			return 1;
		}
		break;
	case 4:
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::TaskFailed)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::LightDamage)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::HeavyDamage)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			Cognition.bCondTookDamage = false;
			++SelectIdealStateScriptExitCalls;
		}
		break;
	case 8:
		NpcKernelState19Shared::State19StampIdeal(*this, 8, 0x45dd);
		return 8;
	case 0xb:
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 2, 0x45be);
			return 2;
		}
		if (NpcKernelState19Shared::State19HasInterrupt(*this, EElysiumNpcCond::DetectedAttack))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 0xb, 0x45c4);
			return 0xb;
		}
		break;
	case 0xe:
		if (NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::LightDamage)
			|| NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::HeavyDamage)
			|| NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::RepeatedDamage))
		{
			Cognition.bCondTookDamage = false;
			FElysiumEntity* const Enemy = GetEnemy();
			FElysiumEntity* const Attacker = World != nullptr
				? World->Resolve(BaseMemory.LastDamageAttacker) : nullptr;
			if (Attacker == Enemy)
			{
				NpcKernelState19Shared::State19StampIdeal(*this, 2, 0x45f0);
				return 2;
			}
		}
		if (NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::DetectedAttack))
		{
			FElysiumEntity* const Enemy = GetEnemy();
			FElysiumEntity* const Attacker = World != nullptr
				? World->Resolve(Senses.Memory.DetectedAttackAttacker) : nullptr;
			if (Attacker == Enemy)
			{
				NpcKernelState19Shared::State19StampIdeal(*this, 2, 0x45f9);
				return 2;
			}
		}
		break;
	default:
		break;
	}
	return BaseSelectIdealState();
}

// =================================================================================================
// Slot 461 dispatcher.
// =================================================================================================

int32 FElysiumNpc::SelectIdealStateRetail()
{
	// Slot 461 on the Troika line (`0x102ad660`). The species bodies are overrides of this method on
	// their C++ classes (story 5 step 3); each that chains calls its recovered owner's body directly
	// (`TroikaSelectIdealState`, `HumanSelectIdealState`, `AnimalSelectIdealState`, …).
	return TroikaSelectIdealState();
}

EElysiumNpcState FElysiumNpc::SelectIdealState()
{
	LastSelectIdealStateRetail = SelectIdealStateRetail();
	return NpcKernelState19Shared::State19TypedFromRetail(LastSelectIdealStateRetail, Mind.IdealState());
}
