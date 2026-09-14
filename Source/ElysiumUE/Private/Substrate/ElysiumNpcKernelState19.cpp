#include "Substrate/ElysiumNpc.h"

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
	constexpr int32 GState19Slot460 = 460;
	constexpr int32 GState19Slot461 = 461;


	int32 GState19CopCensus = 0;

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

	bool State19HasCondition(const FElysiumNpc& Npc, EElysiumNpcCond Cond)
	{
		return Npc.Cognition.Conditions.Has(Cond);
	}

	/** Slot 474 `GetBestSound`. On the Troika line that is `0x102b4520` = `&m_BestSound`, so it
	 *  never answers null; the type word retail reads at `CSound +0x4` is this runtime's
	 *  `FElysiumGameSoundEvent::TypeMask`, and the two numberings are the same SOUND_* bits
	 *  (`ElysiumGameSound.h`: Combat 1, World 2, Player 4, Danger 8, BulletImpact 0x10). */
	const FElysiumGameSoundEvent* State19BestSound(FElysiumNpc& Npc)
	{
		return static_cast<const FElysiumGameSoundEvent*>(Npc.GetBestSound());
	}

	// Retail additionally stamps `m_SelectIdealStateTrace`'s `__FILE__`/`__LINE__` pair
	// (`+0x1b3c`/`+0x1b40`) at every one of these sites. The shape map calls that pair ABSENT; the
	// mind's transition trace carries the same account, so only the retail LINE is recorded here,
	// as the arm's name.
	void State19StampIdeal(FElysiumNpc& Npc, int32 RetailId, int32 Line)
	{
		Npc.WriteIdealStateRetail(RetailId);
		Npc.RecordScheduleEvent(FString::Printf(TEXT("SelectIdealState :%d -> %d"),
			Line, RetailId));
	}

	int32 State19Rand99(FElysiumNpc& Npc)
	{
		(void)Npc;
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99);
	}

	struct FState19SelectArm
	{
		const TCHAR* Address = nullptr;
		int32 (FElysiumNpc::*Body)() = nullptr;
		int32 SelectorTag = -1;
		int32 (FElysiumNpc::*Chain)() = nullptr;
	};
}

int32 FElysiumNpc::CopCensusCount()
{
	return GState19CopCensus;
}

void FElysiumNpc::SetCopCensusCount(int32 Value)
{
	GState19CopCensus = Value;
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
		&& State19HasCondition(*this, EElysiumNpcCond::NewEnemy)
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
			State19StampIdeal(*this, 2, 0x136a);
			break;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::LightDamage))
		{
			Cognition.bCondTookDamage = false;
			++SelectIdealStateMotorResets;
			State19StampIdeal(*this, 3, 0x1373);
			break;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::HeavyDamage))
		{
			Cognition.bCondTookDamage = false;
			++SelectIdealStateMotorResets;
			State19StampIdeal(*this, 3, 0x137c);
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
			State19StampIdeal(*this, 3, 0x1397);
			break;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::Smell))
		{
			State19StampIdeal(*this, 3, 0x139e);
		}
		break;
	case 2:
		if (GetEnemy() == nullptr)
		{
			State19StampIdeal(*this, 3, 0x13cc);
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
			State19StampIdeal(*this, 2, 0x13ac);
			break;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| State19HasInterrupt(*this, EElysiumNpcCond::HearCombat))
		{
			State19StampIdeal(*this, 3, 0x13b2);
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
			State19StampIdeal(*this, 1, 0x13c0);
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
		State19StampIdeal(*this, 7, 0x13dd);
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
			State19StampIdeal(*this, 2, 0x44f7);
			return 2;
		}
		if (!bNoAlertState)
		{
			if (State19HasInterrupt(*this, EElysiumNpcCond::LightDamage))
			{
				Cognition.bCondTookDamage = false;
				State19StampIdeal(*this, 3, 0x451b);
				return 3;
			}
			if (State19HasInterrupt(*this, EElysiumNpcCond::HeavyDamage))
			{
				Cognition.bCondTookDamage = false;
				State19StampIdeal(*this, 3, 0x4522);
				return 3;
			}
		}
		if (State19HasCondition(*this, EElysiumNpcCond::SupernaturalFleeLevel))
		{
			State19StampIdeal(*this, 8, 0x452b);
			NpcFlags.Set(EElysiumNpcFlag::INITIAL_FLEE);
			return 8;
		}
		if (State19HasCondition(*this, EElysiumNpcCond::CriminalFleeLevel))
		{
			State19StampIdeal(*this, 8, 0x4532);
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
				State19StampIdeal(*this, 3, 0x453e);
				return 3;
			}
			if (State19HasInterrupt(*this, EElysiumNpcCond::HearCombat)
				|| State19HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact))
			{
				State19StampIdeal(*this, 3, 0x4545);
				return 3;
			}
			if (State19HasInterrupt(*this, EElysiumNpcCond::HearWorld))
			{
				State19StampIdeal(*this, 3, 0x454b);
				return 3;
			}
			if (State19HasInterrupt(*this, EElysiumNpcCond::HearFlinch))
			{
				State19StampIdeal(*this, 3, 0x4551);
				return 3;
			}
			if (State19HasInterrupt(*this, EElysiumNpcCond::DetectedAttack))
			{
				State19StampIdeal(*this, 3, 0x4557);
				return 3;
			}
		}
		break;
	case 3:
		if (State19HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| State19HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			State19StampIdeal(*this, 2, 0x4562);
			return 2;
		}
		if (State19HasCondition(*this, EElysiumNpcCond::SupernaturalFleeLevel))
		{
			State19StampIdeal(*this, 8, 0x456b);
			NpcFlags.Set(EElysiumNpcFlag::INITIAL_FLEE);
			return 8;
		}
		if (State19HasCondition(*this, EElysiumNpcCond::CriminalFleeLevel))
		{
			State19StampIdeal(*this, 8, 0x4573);
			NpcFlags.Set(EElysiumNpcFlag::INITIAL_FLEE);
			return 8;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::InvestigateSight)
			|| State19HasInterrupt(*this, EElysiumNpcCond::SeeUnknown)
			|| State19HasInterrupt(*this, EElysiumNpcCond::IgnoreUnknown))
		{
			State19StampIdeal(*this, 3, 0x457c);
			return 3;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| State19HasInterrupt(*this, EElysiumNpcCond::HearPlayer))
		{
			State19StampIdeal(*this, 3, 0x4585);
			return 3;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| State19HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact))
		{
			State19StampIdeal(*this, 3, 0x458e);
			return 3;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::HearWorld))
		{
			State19StampIdeal(*this, 3, 0x4596);
			return 3;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::InvestigateSound))
		{
			State19StampIdeal(*this, 3, 0x459d);
			return 3;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::HearFlinch))
		{
			State19StampIdeal(*this, 3, 0x45a4);
			return 3;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::DetectedAttack))
		{
			State19StampIdeal(*this, 3, 0x45ab);
			return 3;
		}
		if (ShouldGoToIdleState())
		{
			State19StampIdeal(*this, 1, 0x45b1);
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
		State19StampIdeal(*this, 8, 0x45dd);
		return 8;
	case 0xb:
		if (State19HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| State19HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			State19StampIdeal(*this, 2, 0x45be);
			return 2;
		}
		if (State19HasInterrupt(*this, EElysiumNpcCond::DetectedAttack))
		{
			State19StampIdeal(*this, 0xb, 0x45c4);
			return 0xb;
		}
		break;
	case 0xe:
		if (State19HasCondition(*this, EElysiumNpcCond::LightDamage)
			|| State19HasCondition(*this, EElysiumNpcCond::HeavyDamage)
			|| State19HasCondition(*this, EElysiumNpcCond::RepeatedDamage))
		{
			Cognition.bCondTookDamage = false;
			FElysiumEntity* const Enemy = GetEnemy();
			FElysiumEntity* const Attacker = World != nullptr
				? World->Resolve(Senses.Memory.LastDamageAttacker) : nullptr;
			if (Attacker == Enemy)
			{
				State19StampIdeal(*this, 2, 0x45f0);
				return 2;
			}
		}
		if (State19HasCondition(*this, EElysiumNpcCond::DetectedAttack))
		{
			FElysiumEntity* const Enemy = GetEnemy();
			FElysiumEntity* const Attacker = World != nullptr
				? World->Resolve(Senses.Memory.DetectedAttackAttacker) : nullptr;
			if (Attacker == Enemy)
			{
				State19StampIdeal(*this, 2, 0x45f9);
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
	if (SpeciesDispatchingSlot == GState19Slot461)
	{
		return TroikaSelectIdealState();
	}

		EElysiumNpcState SpeciesIdeal = Mind.State();
		if (SelectIdealStateForSpecies(SpeciesIdeal))
		{
			Mind.WriteIdealStateRetail(
				SpeciesIdeal == EElysiumNpcState::Alert ? 3
				: SpeciesIdeal == EElysiumNpcState::Combat ? 2
				: SpeciesIdeal == EElysiumNpcState::Dead ? 7
				: 1);
			return IdealStateRetail();
		}

		if (IsRetailClass(TEXT("CNPC_VAndreiBlood")))
		{
			const EElysiumNpcState Andrei = CNPC_VAndreiBlood_vfunc461();
			Mind.WriteIdealStateRetail(Andrei == EElysiumNpcState::Alert ? 2 : 1);
			return IdealStateRetail();
		}

		const FElysiumNpcClassSlot* Override =
			ElysiumNpcKernelClass::OverrideOf(RetailClass(), GState19Slot461);
		if (Override != nullptr && Override->Address != nullptr)
		{
			static const FState19SelectArm Arms[] = {
				{ TEXT("0x1035fe80"), &FElysiumNpc::AnimalSelectIdealState },
				{ TEXT("0x103851e0"), &FElysiumNpc::HumanSelectIdealState },
				{ TEXT("0x103a7450"), &FElysiumNpc::SabbatLeaderSelectIdealState },
				{ TEXT("0x103bd690"), &FElysiumNpc::TzimisceSelectIdealState },
				{ TEXT("0x103743c0"), &FElysiumNpc::DogSelectIdealState },
				{ TEXT("0x1037d290"), &FElysiumNpc::Guard1SelectIdealState },
				{ TEXT("0x10387380"), &FElysiumNpc::HumanCombatPatrolSelectIdealState },
				{ TEXT("0x103a2e30"), &FElysiumNpc::PedestrianSelectIdealState },
				{ TEXT("0x103b4ff0"), &FElysiumNpc::TestNpcSelectIdealState },
				{ TEXT("0x103df5f0"), &FElysiumNpc::ZombieSelectIdealState },
				{ TEXT("0x103726c0"), &FElysiumNpc::CopSelectIdealState },
				{ TEXT("0x103d0820"), &FElysiumNpc::WerewolfSelectIdealState },
				{ TEXT("0x10361060"), nullptr, 6,    &FElysiumNpc::HumanSelectIdealState },
				{ TEXT("0x10363b40"), nullptr, 0xe,  &FElysiumNpc::HumanSelectIdealState },
				{ TEXT("0x103674a0"), nullptr, 7,    &FElysiumNpc::HumanSelectIdealState },
				{ TEXT("0x1036b500"), nullptr, 0xa,  &FElysiumNpc::HumanSelectIdealState },
				{ TEXT("0x10370320"), nullptr, 0xb,  &FElysiumNpc::HumanSelectIdealState },
				{ TEXT("0x10378b60"), nullptr, 0x11, &FElysiumNpc::HumanSelectIdealState },
				{ TEXT("0x10380100"), nullptr, 0x13, &FElysiumNpc::HumanSelectIdealState },
				{ TEXT("0x1039fe10"), nullptr, 0x1b, &FElysiumNpc::HumanSelectIdealState },
				{ TEXT("0x103aeac0"), nullptr, 0x21, &FElysiumNpc::HumanSelectIdealState },
				{ TEXT("0x103b2450"), nullptr, 0x22, &FElysiumNpc::HumanSelectIdealState },
				{ TEXT("0x10388ab0"), nullptr, 0x17, &FElysiumNpc::HumanCombatPatrolSelectIdealState },
				{ TEXT("0x103dd780"), nullptr, 0x2a, &FElysiumNpc::HumanCombatPatrolSelectIdealState },
			};
			for (const FState19SelectArm& Arm : Arms)
			{
				if (FCString::Strcmp(Arm.Address, Override->Address) != 0)
				{
					continue;
				}
				const FSpeciesDispatchScope Scope(*this, GState19Slot461);
				if (Arm.SelectorTag >= 0)
				{
					SelectIdealStateSelector = Arm.SelectorTag;
					return (this->*Arm.Chain)();
				}
				return (this->*Arm.Body)();
			}
		}
	return TroikaSelectIdealState();
}

EElysiumNpcState FElysiumNpc::SelectIdealState()
{
	LastSelectIdealStateRetail = SelectIdealStateRetail();
	return State19TypedFromRetail(LastSelectIdealStateRetail, Mind.IdealState());
}

// =================================================================================================
// Slot 460 species — Camera `0x10368f80`, Dog `0x10374d80` / `0x10374e50`.
// =================================================================================================

int32 FElysiumNpc::CameraPreSelectIdealState()
{
	SelectIdealStateSelector = 9;
	if (SquadDisconnected < 1 && SquadWord() != 0)
	{
		if (State19HasCondition(*this, EElysiumNpcCond::NewEnemy)
			|| NpcFlags.Has(EElysiumNpcFlag2::SQUAD_NEW_ENEMY))
		{
			NpcFlags.Clear(EElysiumNpcFlag2::SQUAD_NEW_ENEMY);
			if (GetEnemy() != nullptr)
			{
				++SelectIdealStateSquadNewEnemyCalls;
			}
		}
	}
	State19StampIdeal(*this, 3, 0x19c);
	return 3;
}

void FElysiumNpc::DogCombatShortCircuit()
{
	Slot596(World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr);
	State19StampIdeal(*this, 2, 0x52a);
	Slot600(GetEnemy());
	if (SquadDisconnected < 1 && SquadWord() != 0 && GetEnemy() != nullptr)
	{
		++SelectIdealStateSquadNewEnemyCalls;
	}
}

int32 FElysiumNpc::DogPreSelectIdealState()
{
	if (State19HasCondition(*this, EElysiumNpcCond::DogCombatLatch)
		|| State19HasCondition(*this, EElysiumNpcCond::DogCombatLatch2))
	{
		DogCombatShortCircuit();
		return IdealStateRetail();
	}
	if (State19HasCondition(*this, EElysiumNpcCond::DogAlertSound))
	{
		State19StampIdeal(*this, 3, 0x517);
		const FSpeciesDispatchScope Scope(*this, GState19Slot460);
		return PreSelectIdealStateRetail();
	}
	if (State19HasCondition(*this, EElysiumNpcCond::DogIdleFromAlert)
		&& IdealStateRetail() == 3)
	{
		State19StampIdeal(*this, 1, 0x51d);
	}
	const FSpeciesDispatchScope Scope(*this, GState19Slot460);
	return PreSelectIdealStateRetail();
}

bool FElysiumNpc::BachOnStateChange(int32 OldRetail, int32 NewRetail)
{
	if (!bCanFightYet && (NewRetail == 2 || NewRetail == 3))
	{
		SetState(OldRetail);
		return true;
	}
	return false;
}

void FElysiumNpc::CopOnStateChange(int32 OldRetail, int32 NewRetail)
{
	FElysiumEntity* const Enemy = GetEnemy();
	if (Enemy != nullptr && NewRetail == 2)
	{
		Slot597(Enemy, 10);
	}
	if (OldRetail == 2)
	{
		// `10371c69`: the pursuit release. `0x1017f6e0` is the counter half — it DECREMENTS the
		// pursued player's `+0x1d10`, and on the zero crossing runs `0x10370630` and `0x1017f9c0`.
		// `CopSlot597Prologue` (family SpeciesMisc10) is the matching increment; without this the
		// counter only ever goes up.
		if (FElysiumEntity* const Pursuit = CopPursuitPlayer())
		{
			Slot598(Pursuit);
			CopPursuitHandle = FElysiumEntityHandle::Invalid();
			RemoveCopInPursuit();
		}
		if (Enemy != nullptr)
		{
			Slot598(Enemy);
		}
	}
	switch (NewRetail)
	{
	case 2:
		bWasEverInCombat = true;
		break;
	case 3:
		if (FElysiumItem* const Active = Inventory.Active(*this))
		{
			if (FElysiumWeapon* const Weapon = Active->AsWeapon())
			{
				Weapon->Unhide(this);
			}
		}
		[[fallthrough]];
	case 1:
	case 8:
		if (Enemy != nullptr)
		{
			Slot598(Enemy);
		}
		// `10371d32`: `m_bWasEverInCombat` AND a closest player that actually resolves. Retail
		// jumps past the call on every failed term — it never dispatches slot 598 with null here.
		if (bWasEverInCombat)
		{
			if (FElysiumEntity* const Closest = World != nullptr
				? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr)
			{
				Slot598(Closest);
			}
		}
		break;
	default:
		break;
	}
	if (NewRetail == 1)
	{
		SetForceFrequentThink(false);
		CopHumanCombatantOnStateChange(NewRetail);
	}
	else
	{
		// `10371dd4`: the census decrement is gated on `+0x6672`, family SaveRestore10's
		// `bCopCountedSecond` — the same word, not a second copy of it.
		if (bCopCountedSecond)
		{
			--GState19CopCensus;
			bCopCountedSecond = false;
		}
		SetForceFrequentThink(true);
		CopHumanCombatantOnStateChange(NewRetail);
	}
}

// `CNPC_VHumanCombatant::OnStateChange` (`0x103871c0`), the shared tail BOTH of `0x10371c20`'s
// arms chain with `(old, new)` — read from the listing (`10371db3 CMP EBP,0x1` then `PUSH EBP`),
// because the C mis-renders the idle tail's argument as the literal 1. Its weapon half is
// UNCONDITIONAL: no census class list, unlike `FElysiumNpc::ApplyStateWeaponVisibility`, which is
// the same switch behind `ClassHolstersOnState()`. The Troika body under it is already run by
// `FElysiumNpc::OnStateChange`'s own tail, so only the weapon half lands here.
void FElysiumNpc::CopHumanCombatantOnStateChange(int32 NewRetail)
{
	++CopHolsterDrawCalls;
	FElysiumItem* const Active = Inventory.Active(*this);
	FElysiumWeapon* const Weapon = Active != nullptr ? Active->AsWeapon() : nullptr;
	if (Weapon == nullptr)
	{
		// `103871d1` / `103871f6`: both arms are guarded by `GetActiveWeapon()`.
		return;
	}
	if (NewRetail == 1)
	{
		// `+0x108` — `CBaseEntity::Hide` (slot 66, `0x1009d2a0`).
		Weapon->Hide(this);
	}
	else if (NewRetail == 2 || NewRetail == 3 || NewRetail == 0xb)
	{
		// `+0x10c` — `Unhide`.
		Weapon->Unhide(this);
	}
}
