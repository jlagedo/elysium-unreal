#include "Substrate/ElysiumNpc.h"

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


	// `_DAT_10483aac` = **512.0f** and `_DAT_1049dfe4` = **262144.0f** (= 512^2), both read out of
	// the pinned `vampire.dll`'s `.rdata` (the corpus holds neither cell). `CNPC_VPedestrian`'s two
	// distance terms are therefore the same 512 units, one against `m_flPlayerDist` and one against
	// a squared separation.
	constexpr float GState19_2PedestrianFleeUnits = 512.f;
	constexpr float GState19_2PedestrianFleeUnitsSq = 262144.f;

	bool State19_2HasInterrupt(FElysiumNpc& Npc, EElysiumNpcCond Cond)
	{
		return ElysiumSchedule::HasInterruptCondition(
			Npc.Schedule, Npc, Npc.Cognition.Conditions, Cond);
	}

	bool State19_2HasCondition(const FElysiumNpc& Npc, EElysiumNpcCond Cond)
	{
		return Npc.Cognition.Conditions.Has(Cond);
	}

	// Retail additionally stamps `m_SelectIdealStateTrace`'s `__FILE__`/`__LINE__` pair
	// (`+0x1b3c`/`+0x1b40`) at every one of these sites. The shape map calls that pair ABSENT; the
	// mind's transition trace carries the same account, so only the retail LINE is recorded here,
	// as the arm's name.
	void State19_2Stamp(FElysiumNpc& Npc, int32 RetailId, int32 Line)
	{
		Npc.WriteIdealStateRetail(RetailId);
		Npc.RecordScheduleEvent(FString::Printf(TEXT("SelectIdealState :%d -> %d"),
			Line, RetailId));
	}

	int32 State19_2Rand99()
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99);
	}

	int32 State19_2ChainTroika(FElysiumNpc& Npc)
	{
		const FElysiumNpc::FSpeciesDispatchScope Scope(Npc, GState19_2Slot461);
		return Npc.TroikaSelectIdealState();
	}

	int32 State19_2ChainHuman(FElysiumNpc& Npc)
	{
		return Npc.HumanSelectIdealState();
	}

	int32 State19_2ChainAnimal(FElysiumNpc& Npc)
	{
		return Npc.AnimalSelectIdealState();
	}

	FElysiumEntity* State19_2Resolve(FElysiumNpc& Npc, const FElysiumEntityHandle& Handle)
	{
		return Npc.World != nullptr && Handle.IsSet() ? Npc.World->Resolve(Handle) : nullptr;
	}

	/** Slot 474 `GetBestSound` (`0x102b4520` on the Troika line) — `&m_BestSound`, so never null for
	 *  any class this port stands. The type word retail reads is `CSound +0x4`, this runtime's
	 *  `FElysiumGameSoundEvent::TypeMask`, and the two numberings are the same SOUND_* bits. */
	const FElysiumGameSoundEvent* State19_2BestSound(FElysiumNpc& Npc)
	{
		return static_cast<const FElysiumGameSoundEvent*>(Npc.GetBestSound());
	}

	/** `CNPC_VGuard1::vfunc461`'s repeated law arm (`0x1037d2a4` and thirteen siblings): the
	 *  interrupt stands AND the closest player IS that channel's offender. The guard latches its
	 *  hate (`0x1037e2d0`), re-reads `m_hClosestPlayer` and hands it to slot 596, then answers. */
	bool State19_2Guard1LawArm(FElysiumNpc& Npc, EElysiumNpcCond Cond,
		ElysiumNpcWitness::EChannel Channel, int32 IdealRetail, int32 Line)
	{
		if (!State19_2HasInterrupt(Npc, Cond))
		{
			return false;
		}
		FElysiumEntity* const Closest = State19_2Resolve(Npc, Npc.Senses.Memory.ClosestPlayer);
		FElysiumEntity* const Offender =
			State19_2Resolve(Npc, Npc.Witness.Channel(Channel).Offender);
		if (Closest != Offender)
		{
			return false;
		}
		Npc.Guard1HatePlayer();
		Npc.Slot596(Closest);
		State19_2Stamp(Npc, IdealRetail, Line);
		return true;
	}

	/** `CNPC_VCop::vfunc461`'s repeated law arm (`0x103727ec` and three siblings): slot 597 with the
	 *  literal 10, then slot 596, both on `m_hClosestPlayer`. Unlike Guard1's there is no offender
	 *  compare — the cop's crazy state acts on the interrupt alone. */
	void State19_2CopLawArm(FElysiumNpc& Npc, int32 IdealRetail, int32 Line)
	{
		FElysiumEntity* const Closest = State19_2Resolve(Npc, Npc.Senses.Memory.ClosestPlayer);
		Npc.Slot597(Closest, 10);
		Npc.Slot596(Closest);
		State19_2Stamp(Npc, IdealRetail, Line);
	}

	/** `CNPC_VPedestrian::vfunc461`'s witness block (`0x103a2f1e`, repeated verbatim at
	 *  `0x103a3068`): the pedestrian that flees a gunshot first REPORTS it —
	 *  `RecordCriminalWitness` (`0x1028ea60`) with the active weapon's crime level and the player's
	 *  own origin, then `PlayerCriminalIncident` (`0x1017f2a0`) with the level read back out of
	 *  `m_iPLCriminalLevelWitnessed` and the location just stored. Retail's whole block is inside
	 *  `if (GetActiveWeapon() != 0)`.
	 *
	 *  SEAM: the level is `weaponData +0x3c4`, resolved by `0x102517e0`. This runtime's weapon
	 *  record carries no crime-level column, so `PedestrianWeaponCrimeLevel` answers `-1` — "the
	 *  player is holding nothing this pedestrian would report" — and the arm's observable half (the
	 *  flee promote and slot 596) runs anyway, exactly as retail's does for an unarmed player. */
	void State19_2PedestrianWitnessWeapon(FElysiumNpc& Npc, FElysiumEntity* Closest)
	{
		if (Closest == nullptr || Npc.PedestrianWeaponCrimeLevel < 0)
		{
			return;
		}
		Npc.RecordCriminalWitness(Npc.PedestrianWeaponCrimeLevel, Closest->Origin, Closest);
		++Npc.PedestrianCrimeReports;
		FElysiumPlayer* const Player = Npc.World != nullptr ? Npc.World->FindPlayer() : nullptr;
		if (Player != nullptr && static_cast<FElysiumEntity*>(Player) == Closest)
		{
			const FElysiumNpcWitnessChannel& Criminal =
				Npc.Witness.Channel(ElysiumNpcWitness::EChannel::Criminal);
			ElysiumLaw::PlayerCriminalIncident(*Player, Criminal.Level, Npc.Handle,
				Criminal.Location);
		}
	}
}

int32 FElysiumNpc::HumanSelectIdealState()
{
	SelectIdealStateSelector = 0x14;
	const int32 State = NpcStateRetail();
	if (State != 2)
	{
		if (State == 0xb)
		{
			if (State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy))
			{
				State19_2Stamp(*this, 2, 0x3ec);
				return IdealStateRetail();
			}
			if (State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
			{
				State19_2Stamp(*this, 2, 0x3f1);
				return IdealStateRetail();
			}
			if (ShouldGoToIdleState())
			{
				State19_2Stamp(*this, 1, 0x3f6);
				return IdealStateRetail();
			}
		}
		return State19_2ChainTroika(*this);
	}

	if ((ScheduleHost.MemoryBits & 0x2) != 0
		&& (State19_2HasCondition(*this, EElysiumNpcCond::SeeEnemy)
			|| State19_2HasCondition(*this, EElysiumNpcCond::NewEnemy)))
	{
		ScheduleHost.MemoryBits &= ~0x2u;
		Cognition.Conditions.Set(EElysiumNpcCond::ScheduleDone);
	}

	if (GetEnemy() != nullptr && !State19_2HasInterrupt(*this, EElysiumNpcCond::LostEnemy))
	{
		return State19_2ChainTroika(*this);
	}

	if (State19_2HasCondition(*this, EElysiumNpcCond::SeeEnemy))
	{
		State19_2Stamp(*this, bNoAlertState ? 1 : 3, bNoAlertState ? 0x3a3 : 0x3a7);
	}
	else
	{
		const FElysiumEntity* const Boss = World != nullptr
			? World->Resolve(FollowerBoss) : nullptr;
		if (Boss != nullptr)
		{
			State19_2Stamp(*this, bNoAlertState ? 1 : 3, bNoAlertState ? 0x3b0 : 0x3b4);
		}
		else if (!HuntConVarIsCommand && HuntConVarRawWord != 0.f)
		{
			State19_2Stamp(*this, 0xb, 0x3bc);
		}
		else
		{
			State19_2Stamp(*this, bNoAlertState ? 1 : 3, bNoAlertState ? 0x3c2 : 0x3c6);
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
			if (State19_2HasCondition(*this, EElysiumNpcCond::SeeEnemy))
			{
				bTook = true;
				State19_2Stamp(*this, bNoAlertState ? 1 : 3, bNoAlertState ? 0x519 : 0x51d);
			}
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s ***Combat state with no enemy!"),
				*DebugString());
			if (bTook)
			{
				return IdealStateRetail();
			}
		}
		else if (State19_2HasInterrupt(*this, EElysiumNpcCond::LostEnemy))
		{
			State19_2Stamp(*this, bNoAlertState ? 1 : 3, bNoAlertState ? 0x529 : 0x52d);
			return IdealStateRetail();
		}
	}
	else if (State == 8)
	{
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::LostEnemy))
		{
			State19_2Stamp(*this, bNoAlertState ? 1 : 3, bNoAlertState ? 0x53c : 0x540);
			return IdealStateRetail();
		}
	}
	else if (State == 0xa)
	{
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::SeeFear))
		{
			State19_2Stamp(*this, 8, 0x54d);
			return IdealStateRetail();
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			if (State19_2Rand99() < 30)
			{
				State19_2Stamp(*this, 2, 0x555);
			}
			else
			{
				State19_2Stamp(*this, 8, 0x55a);
			}
			return IdealStateRetail();
		}
	}
	return State19_2ChainTroika(*this);
}

int32 FElysiumNpc::SabbatLeaderSelectIdealState()
{
	SelectIdealStateSelector = 0x1f;
	if (bSabbatLeaderActivated)
	{
		if (NpcStateRetail() == 2)
		{
			const FElysiumEntity* const Closest = World != nullptr
				? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
			if (Closest != nullptr)
			{
				return IdealStateRetail();
			}
		}
		else
		{
			if (GetEnemy() != nullptr)
			{
				Mind.WriteIdealStateRetail(2);
				return IdealStateRetail();
			}
			FElysiumEntity* const Closest = World != nullptr
				? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
			if (Closest != nullptr)
			{
				ElysiumNpcEnemy::SetEnemy(*this, Closest->Handle);
				Mind.WriteIdealStateRetail(2);
				return IdealStateRetail();
			}
		}
	}
	Mind.WriteIdealStateRetail(1);
	return IdealStateRetail();
}

int32 FElysiumNpc::HumanCombatPatrolSelectIdealState()
{
	SelectIdealStateSelector = 0x15;
	if (NpcStateRetail() == 1)
	{
		if (!bNoAlertState
			&& (State19_2HasCondition(*this, EElysiumNpcCond::LightDamage)
				|| State19_2HasCondition(*this, EElysiumNpcCond::HeavyDamage)
				|| State19_2HasCondition(*this, EElysiumNpcCond::RepeatedDamage)))
		{
			State19_2Stamp(*this, 3, 0x1c5);
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::SeeCorpseFriend))
		{
			FElysiumEntity* const Closest = World != nullptr
				? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
			if (Closest != nullptr)
			{
				const FString Map = !SelectIdealStateMapNameOverride.IsEmpty()
					? SelectIdealStateMapNameOverride
					: (World != nullptr ? World->MapName() : FString());
				if (FCString::Strncmp(*Map, TEXT("la_empire_2"), 12) == 0)
				{
					Slot597(Closest, 5);
				}
			}
			State19_2Stamp(*this, 0xb, 0x1d1);
			return IdealStateRetail();
		}
	}
	return State19_2ChainHuman(*this);
}

// `CNPC_VTzimisce::vfunc461` (`0x103bd690`) — slot 461. The base ladder rewritten around the
// Tzimisce's hunt state: idle takes SEE_UNKNOWN through slot 586 and accepts sound type **4** as
// well as the base's 1/8/0x10; combat with no enemy (or a dead one) falls to HUNT `0xb` rather
// than ALERT; and alert's hear arm answers HUNT. Everything it does not name chains Troika.
int32 FElysiumNpc::TzimisceSelectIdealState()
{
	SelectIdealStateSelector = 0x26;
	switch (NpcStateRetail())
	{
	case 1:
		// `103bd6c4`: NEW_ENEMY alone — the base pairs it with SEE_ENEMY, this body does not.
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy))
		{
			State19_2Stamp(*this, 2, 0xd42);
			break;
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::LightDamage))
		{
			Cognition.bCondTookDamage = false;
			++SelectIdealStateMotorResets;
			State19_2Stamp(*this, 3, 0xd4c);
			break;
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::HeavyDamage))
		{
			Cognition.bCondTookDamage = false;
			++SelectIdealStateMotorResets;
			State19_2Stamp(*this, 3, 0xd56);
			break;
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::SeeUnknown))
		{
			// `103bd7ff`: slot 586 `GetBestSeeUnknown()`. A handle that does not resolve refuses
			// the arm outright — the body chains Troika rather than falling to the hear ladder.
			FElysiumEntity* const Unknown = State19_2Resolve(*this, GetBestSeeUnknown());
			if (Unknown == nullptr)
			{
				return State19_2ChainTroika(*this);
			}
			++SelectIdealStateMotorResets;
			State19_2Stamp(*this, 3, 0xd67);
			break;
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearWorld)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearPlayer)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearThumper)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact))
		{
			const FElysiumGameSoundEvent* const Sound = State19_2BestSound(*this);
			if (Sound == nullptr)
			{
				return State19_2ChainTroika(*this);
			}
			++SelectIdealStateMotorResets;
			const uint32 Type = Sound->TypeMask;
			// `103bd8ba`: 1, **4**, 8, 0x10 — the extra `4` (SOUND_PLAYER) is this class's own.
			if (Type != 1u && Type != 4u && Type != 8u && Type != 0x10u)
			{
				return State19_2ChainTroika(*this);
			}
			State19_2Stamp(*this, 3, 0xd84);
			break;
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::Smell))
		{
			State19_2Stamp(*this, 3, 0xd8b);
			break;
		}
		return State19_2ChainTroika(*this);

	case 2:
		// `103bd90d`: the provocation memory bit, the same pair `CNPC_VHuman` runs.
		if ((ScheduleHost.MemoryBits & 0x2) != 0
			&& (State19_2HasCondition(*this, EElysiumNpcCond::SeeEnemy)
				|| State19_2HasCondition(*this, EElysiumNpcCond::NewEnemy)))
		{
			ScheduleHost.MemoryBits &= ~0x2u;
			Cognition.Conditions.Set(EElysiumNpcCond::ScheduleDone);
		}
		// `103bd95c`: no enemy OR `ENEMY_DEAD` — and the fallback is HUNT, not ALERT, unless the
		// Tzimisce can still see one.
		if (GetEnemy() == nullptr || State19_2HasCondition(*this, EElysiumNpcCond::EnemyDead))
		{
			if (State19_2HasCondition(*this, EElysiumNpcCond::SeeEnemy))
			{
				State19_2Stamp(*this, 3, 0xdcd);
			}
			else
			{
				State19_2Stamp(*this, 0xb, 0xdd2);
			}
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s ***Combat state with no enemy!"),
				*DebugString());
			return IdealStateRetail();
		}
		if (!State19_2HasInterrupt(*this, EElysiumNpcCond::LostEnemy))
		{
			return State19_2ChainTroika(*this);
		}
		State19_2Stamp(*this, 0xb, 0xddb);
		break;

	case 3:
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			State19_2Stamp(*this, 2, 0xd9a);
			break;
		}
		// `103bda2e`: four hear conditions, and the answer is HUNT `0xb`, not ALERT.
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearPlayer)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact))
		{
			State19_2Stamp(*this, 0xb, 0xda2);
			if (State19_2BestSound(*this) != nullptr)
			{
				++SelectIdealStateMotorResets;
			}
			return IdealStateRetail();
		}
		if (ShouldGoToIdleState())
		{
			State19_2Stamp(*this, 1, 0xdaf);
			break;
		}
		return State19_2ChainTroika(*this);

	case 0xb:
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy))
		{
			State19_2Stamp(*this, 2, 0xdf2);
			break;
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			State19_2Stamp(*this, 2, 0xdf7);
			break;
		}
		if (ShouldGoToIdleState())
		{
			State19_2Stamp(*this, 1, 0xdfc);
			break;
		}
		return State19_2ChainTroika(*this);

	default:
		return State19_2ChainTroika(*this);
	}
	return IdealStateRetail();
}

int32 FElysiumNpc::DogSelectIdealState()
{
	SelectIdealStateSelector = 0xd;
	const int32 State = NpcStateRetail();
	if (State == 1)
	{
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::SeeFear))
		{
			State19_2Stamp(*this, 8, 0x30f);
			if (State19_2Rand99() < 0x32)
			{
				Cognition.Conditions.Set(EElysiumNpcCond::DogBark);
			}
			return IdealStateRetail();
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy))
		{
			State19_2Stamp(*this, 2, 0x31d);
			if (State19_2Rand99() < 0x32)
			{
				Cognition.Conditions.Set(EElysiumNpcCond::DogBark);
			}
			return IdealStateRetail();
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::LightDamage)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HeavyDamage))
		{
			Cognition.bCondTookDamage = false;
			return State19_2ChainAnimal(*this);
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::SeeUnknown))
		{
			// `10374602`: a tail JMP to `CNPC_VAnimal` (`0x1035fe80`), not to Troika.
			Cognition.Conditions.Set(EElysiumNpcCond::DogBark);
			return State19_2ChainAnimal(*this);
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearWorld)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearPlayer)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearThumper)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::Smell))
		{
			return State19_2ChainAnimal(*this);
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::PlayerSnarlRange))
		{
			State19_2Stamp(*this, 3, 0x347);
			Cognition.Conditions.Set(EElysiumNpcCond::DogBark);
			// `103746b5`: the snarl arm snapshots `+0x667c` into `+0x6680` and stamps `+0x6678`
			// with `gpGlobals->curtime` before the tail JMP to `CNPC_VAnimal` (`103746d3`).
			DogSnarlPrevValue = DogSnarlSourceWord;
			DogSnarlTime = World != nullptr ? World->NowSeconds() : 0.0;
			return State19_2ChainAnimal(*this);
		}
	}
	else if (State == 3)
	{
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::SeeFear))
		{
			State19_2Stamp(*this, 8, 0x357);
			return IdealStateRetail();
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			State19_2Stamp(*this, 2, 0x35d);
			return IdealStateRetail();
		}
		// `10374436`..`1037447a`: five guards that all land on `FUN_103747c0(1)`, whose whole body
		// is `return 0`, so each is simply "go to `CNPC_VAnimal` now" — but they run BEFORE the
		// three arms below and so take priority over them.
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearPlayer)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact)
			|| ShouldGoToIdleState())
		{
			return State19_2ChainAnimal(*this);
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::DogCombatLatch))
		{
			State19_2Stamp(*this, 2, 0x36d);
			return IdealStateRetail();
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::DogIdleFromAlert)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::DogIdleFromAlert2))
		{
			State19_2Stamp(*this, 1, 0x372);
			return IdealStateRetail();
		}
	}
	return State19_2ChainAnimal(*this);
}

// `CNPC_VGuard1::vfunc461` (`0x1037d290`) — slot 461. The law ladder: four channel arms per
// state, each "the interrupt stands AND the closest player IS that channel's offender", flee for
// the two FLEE levels and combat for the two ATTACK ones, with `INVESTIGATE_LEVEL` promoting to
// the guard's own investigate state `0xc`. Whatever it does not name chains `CNPC_VHuman`.
int32 FElysiumNpc::Guard1SelectIdealState()
{
	using EChannel = ElysiumNpcWitness::EChannel;
	SelectIdealStateSelector = 0x12;
	switch (NpcStateRetail())
	{
	case 1:
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::SupernaturalFleeLevel,
			EChannel::Supernatural, 8, 0x215))
		{
			return 8;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::CriminalFleeLevel,
			EChannel::Criminal, 8, 0x222))
		{
			return 8;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::SupernaturalAttackLevel,
			EChannel::Supernatural, 2, 0x22f))
		{
			return 2;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::CriminalAttackLevel,
			EChannel::Criminal, 2, 0x23c))
		{
			return 2;
		}
		// `1037d5d5`: the fifth law condition, with no offender compare at all.
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::InvestigateLevel))
		{
			State19_2Stamp(*this, 0xc, 0x243);
			return 0xc;
		}
		break;

	case 3:
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::SupernaturalFleeLevel,
			EChannel::Supernatural, 8, 0x251))
		{
			return 8;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::CriminalFleeLevel,
			EChannel::Criminal, 8, 0x25e))
		{
			return 8;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::SupernaturalAttackLevel,
			EChannel::Supernatural, 2, 0x26b))
		{
			return 2;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::CriminalAttackLevel,
			EChannel::Criminal, 2, 0x278))
		{
			return 2;
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::InvestigateLevel))
		{
			State19_2Stamp(*this, 0xc, 0x27f);
			return 0xc;
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			State19_2Stamp(*this, 2, 0x286);
			return 2;
		}
		// `1037d8e7`: four hear conditions, and `m_fHatesPlayer` decides HUNT `0xb` or ALERT.
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearPlayer)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact))
		{
			if (bGuard1HatesPlayer)
			{
				State19_2Stamp(*this, 0xb, 0x291);
			}
			else
			{
				State19_2Stamp(*this, 3, 0x296);
			}
			if (State19_2BestSound(*this) != nullptr)
			{
				++SelectIdealStateMotorResets;
			}
			return IdealStateRetail();
		}
		if (ShouldGoToIdleState())
		{
			State19_2Stamp(*this, 1, 0x2a5);
			return 1;
		}
		break;

	case 0xb:
		// `1037da3c`: a guard that hates the player leaves the hunt the moment it sees one.
		if (bGuard1HatesPlayer && State19_2HasInterrupt(*this, EElysiumNpcCond::SeePlayer))
		{
			Slot596(State19_2Resolve(*this, Senses.Memory.ClosestPlayer));
			State19_2Stamp(*this, 2, 0x2b1);
			return 2;
		}
		break;

	case 0xc:
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::SupernaturalFleeLevel,
			EChannel::Supernatural, 8, 0x2c1))
		{
			return 8;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::CriminalFleeLevel,
			EChannel::Criminal, 8, 0x2ce))
		{
			return 8;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::SupernaturalAttackLevel,
			EChannel::Supernatural, 2, 0x2db))
		{
			return 2;
		}
		if (State19_2Guard1LawArm(*this, EElysiumNpcCond::CriminalAttackLevel,
			EChannel::Criminal, 2, 0x2e8))
		{
			return 2;
		}
		// `1037dd4d`: the investigate state has no fall-through — it ends in IDLE and RETURNS,
		// so a Guard1 in state 0xc never reaches `CNPC_VHuman`.
		State19_2Stamp(*this, 1, 0x2ee);
		return 1;

	default:
		break;
	}
	return State19_2ChainHuman(*this);
}

// `CNPC_VPedestrian::vfunc461` (`0x103a2e30`) — slot 461. Every arm that takes answers FLEE (8);
// the body is a ladder of reasons to run. Only states 1, 3 and 8 are its own.
int32 FElysiumNpc::PedestrianSelectIdealState()
{
	const int32 State = NpcStateRetail();
	SelectIdealStateSelector = 0x1d;
	if (State != 1 && State != 3)
	{
		if (State != 8)
		{
			return State19_2ChainHuman(*this);
		}
		// `103a3450`: already fleeing — stay fleeing, with no test at all.
		State19_2Stamp(*this, 8, 0x375);
		return IdealStateRetail();
	}

	// `103a2e6b`: three damage interrupts, one answer. The attacker, not the closest player, is
	// what slot 596 is handed here.
	if (State19_2HasInterrupt(*this, EElysiumNpcCond::LightDamage)
		|| State19_2HasInterrupt(*this, EElysiumNpcCond::HeavyDamage)
		|| State19_2HasInterrupt(*this, EElysiumNpcCond::RepeatedDamage))
	{
		Cognition.bCondTookDamage = false;
		Slot596(State19_2Resolve(*this, Senses.Memory.LastDamageAttacker));
		State19_2Stamp(*this, 8, 0x308);
		return IdealStateRetail();
	}

	FElysiumEntity* const Closest = State19_2Resolve(*this, Senses.Memory.ClosestPlayer);
	const float PlayerDistUnits =
		static_cast<float>(Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U);

	// `103a2ecb`: the combat-sound ladder, gated on the BARE `HEAR_COMBAT`.
	if (State19_2HasCondition(*this, EElysiumNpcCond::HearCombat))
	{
		bool bTookTheClosestPlayerBranch = false;
		if (Closest != nullptr
			&& State19_2Resolve(*this, Senses.Memory.LastSoundCombat.Source) == Closest)
		{
			bTookTheClosestPlayerBranch = true;
			if (State19_2HasInterrupt(*this, EElysiumNpcCond::HearCombat)
				&& State19_2Rand99() < 0x32)
			{
				State19_2PedestrianWitnessWeapon(*this, Closest);
				Slot596(Closest);
				State19_2Stamp(*this, 8, 0x321);
				return IdealStateRetail();
			}
			if (PlayerDistUnits < GState19_2PedestrianFleeUnits && State19_2Rand99() < 0x4b)
			{
				State19_2PedestrianWitnessWeapon(*this, Closest);
				Slot596(Closest);
				State19_2Stamp(*this, 8, 0x338);
				return IdealStateRetail();
			}
			// `103a338d`: a miss here SKIPS the heard-entity ladder outright.
		}
		if (!bTookTheClosestPlayerBranch)
		{
			// `103a3299`: the same two arms against `m_hLastHeardEnt`, and neither of them
			// reports a crime or touches slot 596 — the pedestrian just runs.
			FElysiumEntity* const Heard =
				State19_2Resolve(*this, Senses.Memory.LastHeardSource);
			if (Heard != nullptr
				&& State19_2Resolve(*this, Senses.Memory.LastSoundCombat.Source) == Heard)
			{
				if (State19_2HasInterrupt(*this, EElysiumNpcCond::HearCombat)
					&& State19_2Rand99() < 0x32)
				{
					State19_2Stamp(*this, 8, 0x344);
					return IdealStateRetail();
				}
				const double SeparationUnitsSq =
					FVector::DistSquared(Origin, Heard->Origin)
						/ (ElysiumMove::U * ElysiumMove::U);
				if (SeparationUnitsSq < GState19_2PedestrianFleeUnitsSq
					&& State19_2Rand99() < 0x4b)
				{
					State19_2Stamp(*this, 8, 0x352);
					return IdealStateRetail();
				}
			}
		}
	}

	// `103a338d`: the bullet-impact tail, again on the BARE condition.
	if (!State19_2HasCondition(*this, EElysiumNpcCond::HearBulletImpact)
		|| State19_2Resolve(*this, Senses.Memory.LastSoundBulletImpact.Source) != Closest
		|| !(PlayerDistUnits < GState19_2PedestrianFleeUnits)
		|| State19_2Rand99() > 0x4a)
	{
		return State19_2ChainHuman(*this);
	}
	Slot596(Closest);
	State19_2Stamp(*this, 8, 0x366);
	return IdealStateRetail();
}

int32 FElysiumNpc::TestNpcSelectIdealState()
{
	SelectIdealStateSelector = 0x23;
	const int32 State = NpcStateRetail();
	if (State == 2)
	{
		if (GetEnemy() == nullptr)
		{
			State19_2Stamp(*this, 3, 0x267);
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s ***Combat state with no enemy!"),
				*DebugString());
			return IdealStateRetail();
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::LostEnemy))
		{
			Cognition.Conditions.Clear(EElysiumNpcCond::LostEnemy);
			State19_2Stamp(*this, 0xb, 0x271);
			return IdealStateRetail();
		}
	}
	else if (State == 0xb)
	{
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy))
		{
			State19_2Stamp(*this, 2, 0x284);
			return IdealStateRetail();
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			State19_2Stamp(*this, 2, 0x289);
			return IdealStateRetail();
		}
		if (ShouldGoToIdleState())
		{
			State19_2Stamp(*this, 1, 0x28e);
			return IdealStateRetail();
		}
	}
	return State19_2ChainHuman(*this);
}

int32 FElysiumNpc::ZombieSelectIdealState()
{
	SelectIdealStateSelector = 0x2b;
	const int32 State = NpcStateRetail();
	if (State == 1)
	{
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::SeeFear))
		{
			State19_2Stamp(*this, 8, 0x1d6);
			return IdealStateRetail();
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy))
		{
			State19_2Stamp(*this, 2, 0x1de);
			return IdealStateRetail();
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::LightDamage)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::HeavyDamage))
		{
			Cognition.bCondTookDamage = false;
		}
		(void)State19_2HasInterrupt(*this, EElysiumNpcCond::Smell);
		return State19_2ChainAnimal(*this);
	}
	if (State == 3)
	{
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::SeeFear))
		{
			State19_2Stamp(*this, 8, 0x206);
			return IdealStateRetail();
		}
		if (State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			State19_2Stamp(*this, 2, 0x20c);
			return IdealStateRetail();
		}
		(void)ShouldGoToIdleState();
		return State19_2ChainAnimal(*this);
	}
	return State19_2ChainAnimal(*this);
}

// `FUN_103723f0` — `CNPC_VCop::vfunc461`'s idle/alert pre-pass, and the only caller. Every one of
// its seven condition tests is the BARE form (`0x10269aa0`): a cop reacts to a gunshot or a hit
// whether or not its running program lists the condition as an interrupt. It answers `2` (COMBAT)
// when it takes and `0` when it does not, and `0` is what sends the cop on to the chain.
int32 FElysiumNpc::CopSelectIdealStatePrePass()
{
	FElysiumEntity* const Closest = State19_2Resolve(*this, Senses.Memory.ClosestPlayer);

	bool bSoundFromThePlayer = false;
	// `10372402`: INVESTIGATE_SOUND **and** BEING_ATTACKED, both, before either sound is looked at.
	if (State19_2HasCondition(*this, EElysiumNpcCond::InvestigateSound)
		&& State19_2HasCondition(*this, EElysiumNpcCond::BeingAttacked))
	{
		if (State19_2HasCondition(*this, EElysiumNpcCond::HearCombat)
			&& State19_2Resolve(*this, Senses.Memory.LastSoundCombat.Source) == Closest)
		{
			bSoundFromThePlayer = true;
		}
		// `1037249e`: this second test runs even after the first has hit — no `else`.
		if (State19_2HasCondition(*this, EElysiumNpcCond::HearBulletImpact)
			&& State19_2Resolve(*this, Senses.Memory.LastSoundBulletImpact.Source) == Closest)
		{
			bSoundFromThePlayer = true;
		}
	}

	bool bTake = bSoundFromThePlayer;
	// `1037252b`: `m_bCondTookDamage` is cleared whenever any damage condition stands, whether or
	// not the attacker turns out to be the closest player.
	if (State19_2HasCondition(*this, EElysiumNpcCond::LightDamage)
		|| State19_2HasCondition(*this, EElysiumNpcCond::HeavyDamage)
		|| State19_2HasCondition(*this, EElysiumNpcCond::RepeatedDamage))
	{
		Cognition.bCondTookDamage = false;
		if (State19_2Resolve(*this, Senses.Memory.LastDamageAttacker) == Closest)
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
	State19_2Stamp(*this, 2, 0x527);
	return 2;
}

// `CNPC_VCop::vfunc461` (`0x103726c0`) — slot 461. Two of its own arms and a chain: idle and
// alert run the pre-pass above and answer COMBAT when it takes, the crazy state `0xc` runs the
// four law conditions, and everything else goes to `CNPC_VHumanCombatPatrol` (`0x10387380`).
int32 FElysiumNpc::CopSelectIdealState()
{
	const int32 State = NpcStateRetail();
	SelectIdealStateSelector = 0xc;
	if (State == 1 || State == 3)
	{
		const int32 PrePass = CopSelectIdealStatePrePass();
		if (PrePass == 0)
		{
			return HumanCombatPatrolSelectIdealState();
		}
		// `1037299f`: the pre-pass's answer is written a second time as the ideal state.
		Mind.WriteIdealStateRetail(PrePass);
		return PrePass;
	}
	if (State != 0xc)
	{
		return HumanCombatPatrolSelectIdealState();
	}
	// `103726f4`: the crazy ladder. Unlike Guard1's there is no offender compare — the interrupt
	// alone decides, and the cop always acts on the closest player.
	if (State19_2HasInterrupt(*this, EElysiumNpcCond::SupernaturalFleeLevel))
	{
		State19_2CopLawArm(*this, 8, 0x5e1);
		return 8;
	}
	if (State19_2HasInterrupt(*this, EElysiumNpcCond::CriminalFleeLevel))
	{
		State19_2CopLawArm(*this, 8, 0x5f1);
		return 8;
	}
	if (State19_2HasInterrupt(*this, EElysiumNpcCond::SupernaturalAttackLevel))
	{
		State19_2CopLawArm(*this, 2, 0x601);
		return 2;
	}
	if (State19_2HasInterrupt(*this, EElysiumNpcCond::CriminalAttackLevel))
	{
		State19_2CopLawArm(*this, 2, 0x611);
		return 2;
	}
	// `1037298a`: nothing stands — the crazy state ends in IDLE and never reaches the chain.
	State19_2Stamp(*this, 1, 0x616);
	return 1;
}

int32 FElysiumNpc::WerewolfSelectIdealState()
{
	SelectIdealStateSelector = 0x29;
	GatherConditions();
	if (!IsAlive() || State19_2HasCondition(*this, EElysiumNpcCond::WerewolfDead))
	{
		State19_2Stamp(*this, 7, 0xaec);
		SetState(IdealStateRetail());
		return IdealStateRetail();
	}
	if (!WerewolfShouldPursueEnemy())
	{
		State19_2Stamp(*this, 9, 0xaf3);
		SetState(IdealStateRetail());
		return IdealStateRetail();
	}
	if (GetEnemy() == nullptr)
	{
		return State19_2ChainTroika(*this);
	}
	if (State19_2HasCondition(*this, EElysiumNpcCond::CanMeleeAttack1)
		|| State19_2HasCondition(*this, EElysiumNpcCond::CanMeleeAttack2))
	{
		State19_2Stamp(*this, 2, 0xafb);
	}
	else if (State19_2HasCondition(*this, EElysiumNpcCond::DogCombatLatch)
		|| State19_2HasCondition(*this, EElysiumNpcCond::EnemyUnreachable))
	{
		State19_2Stamp(*this, 0xb, 0xb00);
	}
	else
	{
		State19_2Stamp(*this, 2, 0xb05);
	}
	SetState(IdealStateRetail());
	return IdealStateRetail();
}
