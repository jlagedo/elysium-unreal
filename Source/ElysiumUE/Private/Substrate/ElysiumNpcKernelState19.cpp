#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelState19Shared.h"

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
// dispatcher. Species SelectIdealState bodies live in `ElysiumNpcKernelState19_2.cpp`.

namespace
{

	EElysiumNpcState State19TypedFromRetail(int32 RetailId, EElysiumNpcState Fallback)
	{
		switch (RetailId)
		{
		case 1: return EElysiumNpcState::Idle;
		case 2: return EElysiumNpcState::Combat;
		case 3: return EElysiumNpcState::Alert;
		case 4: return EElysiumNpcState::Scripted;
		case 6: return EElysiumNpcState::Prone;
		case 7: return EElysiumNpcState::Dead;
		default: return Fallback;
		}
	}

	double State19Now(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}

	bool State19HasInterrupt(FElysiumNpc& Npc, EElysiumNpcCond Cond)
	{
		return ElysiumSchedule::HasInterruptCondition(
			Npc.Schedule, Npc, Npc.Cognition.Conditions, Cond);
	}

	/** Slot 474 `GetBestSound`. On the Troika line that is `0x102b4520` = `&m_BestSound`, so it
	 *  never answers null; the type word retail reads at `CSound +0x4` is this runtime's
	 *  `FElysiumGameSoundEvent::TypeMask`, and the two numberings are the same SOUND_* bits
	 *  (`ElysiumGameSound.h`: Combat 1, World 2, Player 4, Danger 8, BulletImpact 0x10). */
	const FElysiumGameSoundEvent* State19BestSound(FElysiumNpc& Npc)
	{
		return static_cast<const FElysiumGameSoundEvent*>(Npc.GetBestSound());
	}

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

int32 FElysiumNpc::NpcStateRetail() const
{
	return Mind.NpcStateRetail();
}

int32 FElysiumNpc::IdealStateRetail() const
{
	return Mind.IdealStateRetail();
}

void FElysiumNpc::WriteNpcStateRetail(int32 RetailId)
{
	Mind.WriteNpcStateRetail(RetailId);
}

void FElysiumNpc::WriteIdealStateRetail(int32 RetailId)
{
	Mind.WriteIdealStateRetail(RetailId);
}

// =================================================================================================
// `0x1026e340` — `CAI_BaseNPC::SetState`.
// =================================================================================================

void FElysiumNpc::SetState(int32 NewRetail)
{
	const int32 OldAtEntry = NpcStateRetail();
	if (NewRetail != OldAtEntry)
	{
		Mind.StampLastStateChangeTime(State19Now(*this));
	}
	if (NewRetail == 1 && GetEnemy() != nullptr)
	{
		ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid());
		RecordScheduleEvent(TEXT("Stripped"));
	}
	const int32 OldAfterStrip = NpcStateRetail();
	Mind.WriteNpcStateRetail(NewRetail);
	Mind.WriteIdealStateRetail(NewRetail);
	if (OldAfterStrip != NewRetail)
	{
		LastOnStateChangeOldRetail = OldAtEntry;
		LastOnStateChangeNewRetail = NewRetail;
		LastStateChange = Mind.State();
		bStateChangeSeen = true;
		OnStateChange(State19TypedFromRetail(OldAtEntry, Mind.State()),
			State19TypedFromRetail(NewRetail, Mind.State()));
	}
}

// =================================================================================================
// Slot 460 base — `CAI_BaseNPC::PreSelectIdealState` `0x1026f590`.
// =================================================================================================

int32 FElysiumNpc::BasePreSelectIdealState()
{
	SelectIdealStateSelector = 1;
	if (SquadDisconnected < 1 && SquadWord() != 0
		&& (NpcStateRetail() == 1 || NpcStateRetail() == 3)
		&& NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::NewEnemy)
		&& GetEnemy() != nullptr)
	{
		FElysiumEntity* const Parent = World != nullptr ? World->Resolve(MoveParent) : nullptr;
		if (Parent != nullptr)
		{
			if (FElysiumNpc* const ParentNpc = Parent->AsNpc())
			{
				ParentNpc->NpcFlags.Set(EElysiumNpcFlag2::SQUAD_NEW_ENEMY);
			}
			else
			{
				++SelectIdealStateNonNpcParentFlagWrites;
			}
			return 0;
		}
		++SelectIdealStateSquadNewEnemyCalls;
	}
	return 0;
}

// =================================================================================================
// Slot 461 base — `CAI_BaseNPC::SelectIdealState` `0x1026f660`.
// =================================================================================================

int32 FElysiumNpc::BaseSelectIdealState()
{
	SelectIdealStateSelector = 1;
	switch (NpcStateRetail())
	{
	case 1:
		if (State19HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| State19HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 2, 0x136a);
			break;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::LightDamage))
		{
			Cognition.bCondTookDamage = false;
			++SelectIdealStateMotorResets;
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x1373);
			break;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::HeavyDamage))
		{
			Cognition.bCondTookDamage = false;
			++SelectIdealStateMotorResets;
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x137c);
			break;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| State19HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| State19HasInterrupt(*this, EElysiumNpcCond::HearWorld)
			|| State19HasInterrupt(*this, EElysiumNpcCond::HearPlayer)
			|| State19HasInterrupt(*this, EElysiumNpcCond::HearThumper)
			|| State19HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact))
		{
			// `1026f77a`: slot 474 `GetBestSound()`. On the Troika line (`0x102b4520`) that is
			// `&m_BestSound` and is never null, so this miss arm is retail's own dead branch for
			// every class this port stands; the type word retail reads is `CSound +0x4`.
			const FElysiumGameSoundEvent* const Sound = State19BestSound(*this);
			if (Sound == nullptr)
			{
				break;
			}
			++SelectIdealStateMotorResets;
			const uint32 SoundType = Sound->TypeMask;
			if (SoundType != 1u && SoundType != 8u && SoundType != 0x10u)
			{
				break;
			}
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x1397);
			break;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::Smell))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x139e);
		}
		break;
	case 2:
		if (GetEnemy() == nullptr)
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x13cc);
			// `105cc04c`, the one string all five no-enemy-combat arms share.
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s ***Combat state with no enemy!"),
				*DebugString());
			return IdealStateRetail();
		}
		return IdealStateRetail();
	case 3:
		if (State19HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| State19HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 2, 0x13ac);
			break;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| State19HasInterrupt(*this, EElysiumNpcCond::HearCombat))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x13b2);
			// `1026f8d1`: the ideal state is written BEFORE the sound is fetched, so this arm
			// promotes whether or not slot 474 answers; only the motor park is conditional.
			if (State19BestSound(*this) != nullptr)
			{
				++SelectIdealStateMotorResets;
			}
			// `1026f91b`: `thunk_FUN_101e3d70(&DAT_10739a4c, this)` runs UNCONDITIONALLY at the
			// end of this arm — the discipline manager's break-on-notice sweep, whose only other
			// caller is `SetEnemy` (`0x10279a50`). It walks the entity's discipline bitmask at
			// `+0xf34` and `RemoveEffect`s (`0x101e3af0`) every discipline whose record carries a
			// non-zero byte at `+0x32`.
			++SelectIdealStateDisciplineStripCalls;
			return IdealStateRetail();
		}
		if (ShouldGoToIdleState())
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 1, 0x13c0);
		}
		break;
	case 4:
		if (State19HasInterrupt(*this, EElysiumNpcCond::TaskFailed)
			|| State19HasInterrupt(*this, EElysiumNpcCond::LightDamage)
			|| State19HasInterrupt(*this, EElysiumNpcCond::HeavyDamage))
		{
			Cognition.bCondTookDamage = false;
			++SelectIdealStateScriptExitCalls;
			return IdealStateRetail();
		}
		return IdealStateRetail();
	case 7:
		NpcKernelState19Shared::State19StampIdeal(*this, 7, 0x13dd);
		break;
	default:
		return IdealStateRetail();
	}
	return IdealStateRetail();
}

// =================================================================================================
// Slot 461 Troika — `CAI_BaseNPCTroika::SelectIdealState` `0x102ad660`.
// =================================================================================================

int32 FElysiumNpc::TroikaSelectIdealState()
{
	SelectIdealStateSelector = 2;
	switch (NpcStateRetail())
	{
	case 1:
		if (State19HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| State19HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
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
			if (State19HasInterrupt(*this, EElysiumNpcCond::LightDamage))
			{
				Cognition.bCondTookDamage = false;
				NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x451b);
				return 3;
			}
			if (State19HasInterrupt(*this, EElysiumNpcCond::HeavyDamage))
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
			if (State19HasInterrupt(*this, EElysiumNpcCond::InvestigateSound)
				|| State19HasInterrupt(*this, EElysiumNpcCond::InvestigateSight)
				|| State19HasInterrupt(*this, EElysiumNpcCond::IgnoreUnknown)
				|| State19HasInterrupt(*this, EElysiumNpcCond::SeeUnknown)
				|| State19HasInterrupt(*this, EElysiumNpcCond::HearDanger)
				|| State19HasInterrupt(*this, EElysiumNpcCond::HearPlayer))
			{
				NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x453e);
				return 3;
			}
			if (State19HasInterrupt(*this, EElysiumNpcCond::HearCombat)
				|| State19HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact))
			{
				NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x4545);
				return 3;
			}
			if (State19HasInterrupt(*this, EElysiumNpcCond::HearWorld))
			{
				NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x454b);
				return 3;
			}
			if (State19HasInterrupt(*this, EElysiumNpcCond::HearFlinch))
			{
				NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x4551);
				return 3;
			}
			if (State19HasInterrupt(*this, EElysiumNpcCond::DetectedAttack))
			{
				NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x4557);
				return 3;
			}
		}
		break;
	case 3:
		if (State19HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| State19HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
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
		if (State19HasInterrupt(*this, EElysiumNpcCond::InvestigateSight)
			|| State19HasInterrupt(*this, EElysiumNpcCond::SeeUnknown)
			|| State19HasInterrupt(*this, EElysiumNpcCond::IgnoreUnknown))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x457c);
			return 3;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| State19HasInterrupt(*this, EElysiumNpcCond::HearPlayer))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x4585);
			return 3;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| State19HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x458e);
			return 3;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::HearWorld))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x4596);
			return 3;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::InvestigateSound))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x459d);
			return 3;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::HearFlinch))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x45a4);
			return 3;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::DetectedAttack))
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
		if (State19HasInterrupt(*this, EElysiumNpcCond::TaskFailed)
			|| State19HasInterrupt(*this, EElysiumNpcCond::LightDamage)
			|| State19HasInterrupt(*this, EElysiumNpcCond::HeavyDamage)
			|| State19HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| State19HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			Cognition.bCondTookDamage = false;
			++SelectIdealStateScriptExitCalls;
		}
		break;
	case 8:
		NpcKernelState19Shared::State19StampIdeal(*this, 8, 0x45dd);
		return 8;
	case 0xb:
		if (State19HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| State19HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			NpcKernelState19Shared::State19StampIdeal(*this, 2, 0x45be);
			return 2;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::DetectedAttack))
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
				? World->Resolve(Senses.Memory.LastDamageAttacker) : nullptr;
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
	return State19TypedFromRetail(LastSelectIdealStateRetail, Mind.IdealState());
}
