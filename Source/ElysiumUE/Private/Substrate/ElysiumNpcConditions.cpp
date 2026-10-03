#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumLocalIdSpace.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"                  // ElysiumMove::U — the one Source-unit conversion
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"                        // the see-unknown sweep's two one-shot rolls
#include "Substrate/ElysiumGameSound.h"       // the raw CSound type words the flank test branches on
#include "Substrate/ElysiumItemClasses.h"      // FElysiumItem — the active weapon's record
#include "Substrate/ElysiumItemTable.h"        // FElysiumItemDef / FElysiumWeaponMode
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcLog.h"           // the `npc_*` category the mode refusal reports on
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"      // the running program's interrupt mask, the sweep's gate
#include "Substrate/ElysiumWeaponClasses.h"    // FElysiumWeapon — the reach, cone and deadlines

FElysiumNpcConditions FElysiumNpcConditions::ToGlobalOrdinals(const FElysiumLocalIdSpace* Space) const
{
	FElysiumNpcConditions Out;
	if (Space == nullptr) return Out;
	for (int32 Id = 0; Id < NumOrdinals; ++Id)
	{
		if (HasOrdinal(Id))
		{
			const int32 Global = Space->LocalToGlobal(Id);
			if (Global != INDEX_NONE) Out.SetOrdinal(Global - ElysiumScheduleId::GlobalBase);
		}
	}
	return Out;
}

FElysiumNpcConditions FElysiumNpcConditions::ToLocalOrdinals(const FElysiumLocalIdSpace* Space) const
{
	FElysiumNpcConditions Out;
	if (Space == nullptr) return Out;
	for (int32 Ordinal = 0; Ordinal < NumOrdinals; ++Ordinal)
	{
		if (HasOrdinal(Ordinal))
		{
			Out.SetOrdinal(Space->GlobalToLocal(ElysiumScheduleId::GlobalBase + Ordinal));
		}
	}
	return Out;
}

namespace
{
	// The seen set for one decision pass.
	//
	// CAI_Senses::Look supplies a fresh list, distinct from closest-player LOS cache. Its admission
	// already applies player/NPC/object cadence and the non-player D_HT/D_FR gate; conditions only
	// classify the observation and must not replay cache flags.
	void NpcCondBuildSeenSet(const FElysiumNpc& Npc, TArray<FElysiumEntityHandle>& Out)
	{
		Out = Npc.Senses.Sighted();
	}

	FString NpcCondClassnameOf(const FElysiumEntity& Entity)
	{
		return Entity.Def ? Entity.Def->Classname : FString();
	}
}

const FElysiumEntity* ElysiumNpcCond::ResolveEnemyHandle(const FElysiumEntityWorld& World,
	const FElysiumEntityHandle& Handle)
{
	if (!Handle.IsSet() || Handle.Epoch != World.GetEpoch())
	{
		return nullptr;
	}
	const TArray<TUniquePtr<FElysiumEntity>>& List = World.Entities();
	return List.IsValidIndex(Handle.Index) ? List[Handle.Index].Get() : nullptr;
}

const TCHAR* ElysiumNpcCondName(EElysiumNpcCond Cond)
{
	switch (Cond)
	{
	case EElysiumNpcCond::None:                  return TEXT("COND_NONE");
	case EElysiumNpcCond::SeeUnknown:            return TEXT("SEE_UNKNOWN");
	case EElysiumNpcCond::LostUnknown:           return TEXT("LOST_UNKNOWN");
	case EElysiumNpcCond::IgnoreUnknown:         return TEXT("IGNORE_UNKNOWN");
	case EElysiumNpcCond::UnknownRunTimer:       return TEXT("UNKNOWN_RUN_TIMER");
	case EElysiumNpcCond::UnknownAdvancing:      return TEXT("UNKNOWN_ADVANCING");
	case EElysiumNpcCond::UnknownHolding:        return TEXT("UNKNOWN_HOLDING");
	case EElysiumNpcCond::UnknownRetreating:     return TEXT("UNKNOWN_RETREATING");
	case EElysiumNpcCond::InvestigateSight:      return TEXT("INVESTIGATE_SIGHT");
	case EElysiumNpcCond::SeePlayer:             return TEXT("SEE_PLAYER");
	case EElysiumNpcCond::TaskFailed:            return TEXT("TASK_FAILED");
	case EElysiumNpcCond::ScheduleDone:          return TEXT("SCHEDULE_DONE");
	case EElysiumNpcCond::HearBulletImpact:      return TEXT("HEAR_BULLET_IMPACT");
	case EElysiumNpcCond::HearPhysicsDanger:     return TEXT("HEAR_PHYSICS_DANGER");
	case EElysiumNpcCond::HearThumper:           return TEXT("HEAR_THUMPER");
	case EElysiumNpcCond::HearBugbait:           return TEXT("HEAR_BUGBAIT");
	case EElysiumNpcCond::Smell:                 return TEXT("SMELL");
	case EElysiumNpcCond::Provoked:              return TEXT("PROVOKED");
	case EElysiumNpcCond::GiveWay:               return TEXT("GIVE_WAY");
	case EElysiumNpcCond::StopBackup:            return TEXT("STOP_BACKUP");
	case EElysiumNpcCond::ShouldDodge:           return TEXT("SHOULD_DODGE");
	case EElysiumNpcCond::ShouldBlock:           return TEXT("SHOULD_BLOCK");
	case EElysiumNpcCond::ShouldStepback:        return TEXT("SHOULD_STEPBACK");
	case EElysiumNpcCond::ShouldKick:            return TEXT("SHOULD_KICK");
	case EElysiumNpcCond::Knockback:             return TEXT("KNOCKBACK");
	case EElysiumNpcCond::WaitingAttackTime:     return TEXT("WAITING_ATTACK_TIME");
	case EElysiumNpcCond::HitByDoor:             return TEXT("HIT_BY_DOOR");
	case EElysiumNpcCond::WasBumped:             return TEXT("WAS_BUMPED");
	case EElysiumNpcCond::WeaponThroughWall:     return TEXT("WEAPON_THROUGH_WALL");
	case EElysiumNpcCond::NoPrimaryAmmo:         return TEXT("NO_PRIMARY_AMMO");
	case EElysiumNpcCond::SeeHate:               return TEXT("SEE_HATE");
	case EElysiumNpcCond::SeeDislike:            return TEXT("SEE_DISLIKE");
	case EElysiumNpcCond::LostEnemy:             return TEXT("LOST_ENEMY");
	case EElysiumNpcCond::EnemyOccluded:         return TEXT("ENEMY_OCCLUDED");
	case EElysiumNpcCond::HaveEnemyLos:          return TEXT("HAVE_ENEMY_LOS");
	case EElysiumNpcCond::LightDamage:           return TEXT("LIGHT_DAMAGE");
	case EElysiumNpcCond::HeavyDamage:           return TEXT("HEAVY_DAMAGE");
	case EElysiumNpcCond::RepeatedDamage:        return TEXT("REPEATED_DAMAGE");
	case EElysiumNpcCond::CanRangeAttack1:       return TEXT("CAN_RANGE_ATTACK1");
	case EElysiumNpcCond::CanRangeAttack2:       return TEXT("CAN_RANGE_ATTACK2");
	case EElysiumNpcCond::CanMeleeAttack1:       return TEXT("CAN_MELEE_ATTACK1");
	case EElysiumNpcCond::CanMeleeAttack2:       return TEXT("CAN_MELEE_ATTACK2");
	case EElysiumNpcCond::NewEnemy:              return TEXT("NEW_ENEMY");
	case EElysiumNpcCond::EnemyDead:             return TEXT("ENEMY_DEAD");
	case EElysiumNpcCond::EnemyUnreachable:      return TEXT("ENEMY_UNREACHABLE");
	case EElysiumNpcCond::SeeNemesis:            return TEXT("SEE_NEMESIS");
	case EElysiumNpcCond::TooCloseToAttack:      return TEXT("TOO_CLOSE_TO_ATTACK");
	case EElysiumNpcCond::TooFarToAttack:        return TEXT("TOO_FAR_TO_ATTACK");
	case EElysiumNpcCond::WeaponBlockedByFriend: return TEXT("WEAPON_BLOCKED_BY_FRIEND");
	case EElysiumNpcCond::WeaponSightOccluded:   return TEXT("WEAPON_SIGHT_OCCLUDED");
	case EElysiumNpcCond::SeeEnemy:              return TEXT("SEE_ENEMY");
	case EElysiumNpcCond::SeeFear:               return TEXT("SEE_FEAR");
	case EElysiumNpcCond::HearCombat:            return TEXT("HEAR_COMBAT");
	case EElysiumNpcCond::HearPlayer:            return TEXT("HEAR_PLAYER");
	case EElysiumNpcCond::HearWorld:             return TEXT("HEAR_WORLD");
	case EElysiumNpcCond::HearDanger:            return TEXT("HEAR_DANGER");
	case EElysiumNpcCond::CriminalFleeLevel:       return TEXT("CRIMINAL_FLEE_LEVEL");
	case EElysiumNpcCond::CriminalAttackLevel:     return TEXT("CRIMINAL_ATTACK_LEVEL");
	case EElysiumNpcCond::SupernaturalFleeLevel:   return TEXT("SUPERNATURAL_FLEE_LEVEL");
	case EElysiumNpcCond::SupernaturalAttackLevel: return TEXT("SUPERNATURAL_ATTACK_LEVEL");
	case EElysiumNpcCond::InvestigateLevel:        return TEXT("INVESTIGATE_LEVEL");
	case EElysiumNpcCond::Comfort:                 return TEXT("COMFORT");
	case EElysiumNpcCond::SquadSeeEnemy:           return TEXT("SQUAD_SEE_ENEMY");
	case EElysiumNpcCond::HearFlinch:              return TEXT("HEAR_FLINCH");
	case EElysiumNpcCond::NpcFreeze:               return TEXT("NPC_FREEZE");
	case EElysiumNpcCond::InvestigateSound:        return TEXT("INVESTIGATE_SOUND");
	case EElysiumNpcCond::HearFlankSound:          return TEXT("HEAR_FLANK_SOUND");
	case EElysiumNpcCond::SeeSoundSource:          return TEXT("SEE_SOUND_SOURCE");
	// Retail 0x77; its human-readable name is unrecovered, so the number is the name.
	case EElysiumNpcCond::CanTeleport:             return TEXT("COND_0x77_CAN_TELEPORT");
	// Story 29c-1, family Schedule.
	case EElysiumNpcCond::TooFarForMelee:          return TEXT("TOO_FAR_FOR_MELEE");
	case EElysiumNpcCond::InterruptTime:           return TEXT("INTERRUPT_TIME");
	case EElysiumNpcCond::PassOut:                 return TEXT("PASS_OUT");
	case EElysiumNpcCond::OnFire:                  return TEXT("ON_FIRE");
	case EElysiumNpcCond::ShouldCharge:            return TEXT("SHOULD_CHARGE");
	case EElysiumNpcCond::EnemyBlocked:            return TEXT("ENEMY_BLOCKED");
	case EElysiumNpcCond::SeeCorpseFriend:         return TEXT("SEE_CORPSE_FRIEND");
	case EElysiumNpcCond::FloatingOffGround:       return TEXT("FLOATING_OFF_GROUND");
	// Story 29c-1, family Conditions.
	case EElysiumNpcCond::TooCloseForRanged:       return TEXT("TOO_CLOSE_FOR_RANGED");
	case EElysiumNpcCond::BeingAttacked:           return TEXT("BEING_ATTACKED");
	case EElysiumNpcCond::DetectedAttack:          return TEXT("DETECTED_ATTACK");
	case EElysiumNpcCond::PlayerSnarlRange:        return TEXT("PLAYER_SNARL_RANGE");
	case EElysiumNpcCond::DogCombatLatch:          return TEXT("DOG_COMBAT_LATCH");
	case EElysiumNpcCond::DogAlertSound:           return TEXT("DOG_ALERT_SOUND");
	case EElysiumNpcCond::DogBark:                 return TEXT("DOG_BARK");
	case EElysiumNpcCond::DogIdleFromAlert:        return TEXT("DOG_IDLE_FROM_ALERT");
	case EElysiumNpcCond::DogIdleFromAlert2:       return TEXT("DOG_IDLE_FROM_ALERT2");
	case EElysiumNpcCond::DogCombatLatch2:         return TEXT("DOG_COMBAT_LATCH2");
	case EElysiumNpcCond::WerewolfDead:            return TEXT("WEREWOLF_DEAD");
	case EElysiumNpcCond::ExtendedBlockedByFriend: return TEXT("EXTENDED_BLOCKED_BY_FRIEND");
	case EElysiumNpcCond::EnemyTooFar:             return TEXT("ENEMY_TOO_FAR");
	case EElysiumNpcCond::NotFacingAttack:         return TEXT("NOT_FACING_ATTACK");
	case EElysiumNpcCond::WeaponHasLos:            return TEXT("WEAPON_HAS_LOS");
	case EElysiumNpcCond::WeaponPlayerInSpread:    return TEXT("WEAPON_PLAYER_IN_SPREAD");
	case EElysiumNpcCond::WeaponPlayerNearTarget:  return TEXT("WEAPON_PLAYER_NEAR_TARGET");
	// Story 29c-1, family Dialogue.
	case EElysiumNpcCond::ShouldInteract:          return TEXT("SHOULD_INTERACT");
	case EElysiumNpcCond::CrosswalkWalk:           return TEXT("CROSSWALK_WALK");
	case EElysiumNpcCond::CrosswalkDontWalk:       return TEXT("CROSSWALK_DONTWALK");
	}
	return TEXT("COND_?");
}

EElysiumNpcCond FElysiumNpcConditions::FirstSet() const
{
	for (int32 WordIndex = 0; WordIndex < NumWords; ++WordIndex)
	{
		if (Words[WordIndex] != 0)
		{
			const uint32 BitIndex = static_cast<uint32>(FMath::CountTrailingZeros64(Words[WordIndex]));
			return static_cast<EElysiumNpcCond>((WordIndex << 6) + static_cast<int32>(BitIndex));
		}
	}
	return EElysiumNpcCond::None;
}

FString FElysiumNpcConditions::Describe() const
{
	FString Out;
	for (int32 WordIndex = 0; WordIndex < NumWords; ++WordIndex)
	{
		uint64 Remaining = Words[WordIndex];
		while (Remaining != 0)
		{
			const uint32 BitIndex = static_cast<uint32>(FMath::CountTrailingZeros64(Remaining));
			Remaining &= Remaining - 1;
			const int32 Ordinal = (WordIndex << 6) + static_cast<int32>(BitIndex);
			const EElysiumNpcCond Cond = static_cast<EElysiumNpcCond>(Ordinal);
			if (!Out.IsEmpty())
			{
				Out.AppendChar(TEXT('|'));
			}
			// A parsed interrupt mask may carry an ordinal this runtime has no enumerator for --
			// the corpus registers 164 condition names and `EElysiumNpcCond` spells fewer. Naming
			// the number is what keeps such a bit legible in a trace instead of reading as the
			// same `COND_?` as every other one.
			const TCHAR* const Name = ElysiumNpcCondName(Cond);
			if (FCString::Strcmp(Name, TEXT("COND_?")) == 0)
			{
				Out.Appendf(TEXT("COND_%d"), Ordinal);
			}
			else
			{
				Out.Append(Name);
			}
		}
	}
	return Out.IsEmpty() ? FString(TEXT("(none)")) : Out;
}

// --- Producers ---

bool ElysiumNpcCond::IsCombatSoundCategory(const FString& FoldedCategory)
{
	return FoldedCategory.Contains(TEXT("gunshot"))
		|| FoldedCategory.Contains(TEXT("explosion"))
		|| FoldedCategory.Contains(TEXT("impact"))
		|| FoldedCategory == TEXT("npc_take_damage");
}

bool ElysiumNpcCond::IsHearFamily(EElysiumNpcCond Cond)
{
	return Cond == EElysiumNpcCond::HearCombat
		|| Cond == EElysiumNpcCond::HearPlayer
		|| Cond == EElysiumNpcCond::HearWorld
		|| Cond == EElysiumNpcCond::HearDanger || Cond == EElysiumNpcCond::HearFlinch
		|| Cond == EElysiumNpcCond::HearBulletImpact || Cond == EElysiumNpcCond::HearPhysicsDanger
		|| Cond == EElysiumNpcCond::HearThumper || Cond == EElysiumNpcCond::HearBugbait;
}

bool ElysiumNpcCond::ShouldInvestigate(const FElysiumNpc& Npc, const FElysiumEntity& Candidate,
	bool bCombatMode)
{
	// 1. `if ((m_bfAINPCFlags & 0x4000080) != 0) return false;` -- the first line of the body.
	if (Npc.NpcFlags.Has(EElysiumNpcFlag::DONT_INVESTIGATE)
		|| Npc.NpcFlags.Has(EElysiumNpcFlag::IN_FLEE_SCHED))
	{
		return false;
	}
	// 2. `stay_entrenched` and 4. the follower boss -- both named on the declaration, neither
	//    carried by this substrate yet.
	// 3. is folded into the reference parameter.
	// 5. The committed enemy is always of interest.
	if (Npc.BaseMemory.Enemy.IsSet() && Npc.BaseMemory.Enemy == Candidate.Handle)
	{
		return true;
	}
	// 6. The mode switch. The player test is retail's cached `CBasePlayer*` at `+0xa8`.
	const FElysiumEntity* Player = Npc.World ? Npc.World->FindPlayer() : nullptr;
	const bool bIsPlayer = Player != nullptr && Player == &Candidate;
	const EElysiumRelationship Relation =
		Npc.Relationships.Resolve(Candidate.Handle, NpcCondClassnameOf(Candidate));
	const int32 Mode = bCombatMode ? Npc.InvestigateModeCombat : Npc.InvestigateMode;
	switch (static_cast<EElysiumInvestigateMode>(Mode))
	{
	case EElysiumInvestigateMode::Never:             return false;
	case EElysiumInvestigateMode::HatedPlayers:      return bIsPlayer && Relation == EElysiumRelationship::Hate;
	case EElysiumInvestigateMode::NonNeutralPlayers: return bIsPlayer && Relation != EElysiumRelationship::Neutral;
	case EElysiumInvestigateMode::AnyPlayer:         return bIsPlayer;
	case EElysiumInvestigateMode::Hated:             return Relation == EElysiumRelationship::Hate;
	case EElysiumInvestigateMode::NonNeutral:        return Relation != EElysiumRelationship::Neutral;
	case EElysiumInvestigateMode::Anything:          return true;
	default:
		// Retail's `DevWarning("Hey FOO!!!  I don't recognize your investigate mode!")`, then false.
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s: unrecognised investigate mode %d (%s)"), *Npc.DebugString(), Mode,
			bCombatMode ? TEXT("investigate_mode_combat") : TEXT("investigate_mode"));
		return false;
	}
}

void ElysiumNpcCond::GatherSight(FElysiumNpc& Npc, double Now, FElysiumNpcConditions& Out)
{
	FElysiumEntityWorld* World = Npc.World;
	if (World == nullptr)
	{
		return;
	}
	// Retail raises `SEE_HATE`/`SEE_FEAR` inside the sense pass -- `CAI_Senses::Look`, under
	// `CAI_BaseNPC::PerformSensing` (`0x1026e4f0`), which `m_iIsOblivious` gates whole. This runtime
	// raises them here, from the LOS memory the sense pass wrote, and that memory does not go stale
	// on its own: an oblivious body that stopped sensing with `bPlayerVisible` set would otherwise keep
	// re-raising a sighting it is no longer having, and the enemy transaction would drag a
	// mesmerized victim into combat off it. So the gate retail applies to the producer is applied
	// to the classification. `DONT_INVESTIGATE` is deliberately NOT tested here: it gates the
	// interest predicate (`ShouldInvestigate`), not the sightings themselves.
	if (Npc.IsOblivious())
	{
		return;
	}

	TArray<FElysiumEntityHandle> Seen;
	NpcCondBuildSeenSet(Npc, Seen);

	FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	if (Npc.Senses.bSeeUnknownThisPass)
	{
		Out.Set(EElysiumNpcCond::SeeUnknown);
	}
	for (const FElysiumEntityHandle& Handle : Seen)
	{
		const FElysiumEntity* Target = World->Resolve(Handle);
		if (Target == nullptr || Target->IsInert() || Handle == Npc.Handle)
		{
			continue;
		}
		const EElysiumRelationship Relation = Npc.Relationships.Resolve(Handle, NpcCondClassnameOf(*Target));
		// OnLooked 0x1026a2c0 skips the current unknown BEFORE the player/relationship arms.
		const FElysiumEntityHandle Skip = Npc.NpcFlags.Has(EElysiumNpcFlag::IGNORE_UNKNOWN)
			? Memory.LastSeeUnknown : Memory.BestSeeUnknown;
		if (Handle == Skip) continue;
		if (Handle == World->PlayerHandle())
		{
			Out.Set(EElysiumNpcCond::SeePlayer);
			// `0x1017ff40(player, this, relation)`: the player's per-relation "last assessed by an
			// NPC" stamp, written for EVERY relation type before the D_HT/D_FR arms below. Retail
			// gates it on `m_bIsBCCTargetable`, a flag with no port field and no recovered clearer,
			// so it reads true here.
			if (FElysiumPlayer* SeenPlayer = World->FindPlayer())
			{
				int32 RelationIndex = 4;   // D_NU
				switch (Relation)
				{
				case EElysiumRelationship::Hate:    RelationIndex = 1; break;
				case EElysiumRelationship::Fear:    RelationIndex = 2; break;
				case EElysiumRelationship::Like:    RelationIndex = 3; break;
				case EElysiumRelationship::Neutral: RelationIndex = 4; break;
				}
				SeenPlayer->LastSeenByNpcTime[RelationIndex] = Now;
			}
		}
		if (Handle == World->PlayerHandle() && Relation == EElysiumRelationship::Hate
			&& (!Npc.Def || !Npc.Def->Classname.Equals(TEXT("npc_VRat"), ESearchCase::IgnoreCase)))
		{
			if (FElysiumPlayer* Player = World->FindPlayer())
			{
				Player->LastHostileAssessment = Npc.Handle;
				Player->LastHostileAssessmentTime = Now;
			}
		}
		// `1026a3d1`: **the gate this body was missing.** Everything from here down — `SEE_ENEMY`
		// and the whole relation switch — runs ONLY when the relation is not `D_NU` (4). A neutral
		// entity is seen, is stamped on the player's per-relation surface above, and raises nothing
		// else. Story 29d, family Senses10.
		if (Relation == EElysiumRelationship::Neutral)
		{
			continue;
		}
		// `1026a3dd`: **the second gate this body was missing.** `SEE_ENEMY` (`0x46`) is raised HERE,
		// for the committed enemy (slot 0x29c) when it is the entity this iteration is looking at —
		// so it is subject to the skip-entity exclusion and the `D_NU` gate above. The port raised it
		// unconditionally from `GatherCommittedEnemy`, off `Sighted().Contains(Enemy)`.
		if (Npc.BaseMemory.Enemy.IsSet() && Handle == Npc.BaseMemory.Enemy)
		{
			Out.Set(EElysiumNpcCond::SeeEnemy);
		}
		// Troika's `OnLooked` classifies D_HT by its raw IRelationPriority: negative is DISLIKE,
		// 0..10 HATE, and 11+ NEMESIS.  The relationship store deliberately retains D_HT as its
		// disposition, so this is the one place the priority expands it into the three conditions.
		// Only actual HATE and FEAR take the base `UpdateEnemyMemory` write.
		//
		// `1026a48a`: retail's `default:` arm — relation `D_ER` (0) — prints
		// `DevWarning(2, "%s can't assess %s")` and raises nothing. `FElysiumRelationships::Resolve`
		// never answers `D_ER`, so that arm is UNREACHABLE here; it is recorded rather than
		// simulated, because manufacturing a `D_ER` would be inventing a relation the store has no
		// value for.
		const int32 Priority = Npc.Relationships.ResolvePriority(Handle, NpcCondClassnameOf(*Target));
		FElysiumNpcBaseMemory::ESeen Slot = FElysiumNpcBaseMemory::ESeen::Count;
		switch (Relation)
		{
		case EElysiumRelationship::Hate:
			// 0x1026a3e0 diverts D_HT under D_CALM into an arm that rejects D_CALM.
			if (Npc.NpcFlags.Has(EElysiumNpcFlag2::D_CALM)) break;
			if (Priority < 0)
			{
				Out.Set(EElysiumNpcCond::SeeDislike);
				Slot = FElysiumNpcBaseMemory::ESeen::Dislike;
			}
			else if (Priority <= 10)
			{
				Out.Set(EElysiumNpcCond::SeeHate);
				Slot = FElysiumNpcBaseMemory::ESeen::Hate;
			}
			else
			{
				Out.Set(EElysiumNpcCond::SeeNemesis);
				Slot = FElysiumNpcBaseMemory::ESeen::Nemesis;
			}
			break;
		case EElysiumRelationship::Fear:
			if (Npc.NpcFlags.Has(EElysiumNpcFlag2::D_CALM)) break;
			Out.Set(EElysiumNpcCond::SeeFear);
			Slot = FElysiumNpcBaseMemory::ESeen::Fear;
			break;
		default:
			break;   // D_LI and D_NU raise no sight condition
		}
		if (Slot != FElysiumNpcBaseMemory::ESeen::Count)
		{
			Npc.BaseMemory.LastSeen[static_cast<int32>(Slot)] = Handle;
			Npc.BaseMemory.LastSeenTime[static_cast<int32>(Slot)] = Now;
			if (Relation == EElysiumRelationship::Hate || Relation == EElysiumRelationship::Fear)
			{
				// A sighting is the admission write; hearing and cached sight flags never manufacture
				// a BestEnemy candidate.
				Npc.EnemyMemory.Update(Npc, Handle, Now);
			}
		}
	}
}

void ElysiumNpcCond::GatherSeeUnknown(FElysiumNpc& Npc, double Now, FElysiumNpcConditions& Out)
{
	FElysiumEntityWorld* World = Npc.World;
	FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	// Retail's outer `GatherConditions` (`0x102b27f0`, `FElysiumNpc::GatherConditions`) clears every
	// condition this sweep owns (`0x102b2863..0x102b28ab`) before the sense pass runs, so nothing here
	// needs its own unconditional clear except the mid-function retraction below, which retail
	// performs inside this very function.
	if (!Out.Has(EElysiumNpcCond::SeeUnknown))
	{
		// `FUN_1028e360`: no best-see-unknown handle at all, or one that no longer resolves, answers
		// "lost" immediately; a still-resolving handle arms the 1.5 s grace on the first miss and
		// answers "lost" only once it elapses.
		if (Memory.BestSeeUnknown.IsSet())
		{
			const FElysiumEntity* Best = World ? ResolveEnemyHandle(*World, Memory.BestSeeUnknown) : nullptr;
			if (Best != nullptr)
			{
				if (Memory.SeeUnknownGraceUntil < 0.0)
				{
					Memory.SeeUnknownGraceUntil = Now + SeeUnknownGraceSeconds;
					return;
				}
				if (Now < Memory.SeeUnknownGraceUntil)
				{
					return;
				}
				Memory.BestSeeUnknown = FElysiumEntityHandle::Invalid();
				// Retail dereferences an unresolved `m_hLastSeeUnknown` (`0x1028e411`, `MOV EDX,[ECX]`
				// with `ECX = 0`) and crashes; the port keeps the last recorded position instead, the
				// one divergence in this arm.
				const FElysiumEntity* Last = World ? ResolveEnemyHandle(*World, Memory.LastSeeUnknown) : nullptr;
				if (Last != nullptr)
				{
					Memory.LastSeeUnknownPosition = Last->Origin;
				}
			}
		}
		if (ElysiumSchedule::MaskHasCondition(Npc.Schedule, Npc, EElysiumNpcCond::LostUnknown))
		{
			Out.Set(EElysiumNpcCond::LostUnknown);
		}
		return;
	}

	// Seeing it again resets the grace sentinel unconditionally, whether or not anything below finds
	// a player to classify.
	Memory.SeeUnknownGraceUntil = -1.0;

	// Player-only by construction: retail reads the target's cached `CBasePlayer*` at `+0xa8`, which
	// is null for anything else.
	if (World == nullptr || Memory.BestSeeUnknown != World->PlayerHandle())
	{
		return;
	}
	FElysiumPlayer* Player = World->FindPlayer();
	if (Player == nullptr)
	{
		return;
	}

	if (!Player->IsInStealthPosture())
	{
		// Clearly visible: the one-shot roll picks ATTACK_UNKNOWN off the repeat-sightings ramp,
		// clamped at 100%; the flag then drives UNKNOWN_RUN_TIMER every pass it stands, fresh roll
		// or not.
		if (!Npc.NpcFlags.Has(EElysiumNpcFlag::MADE_INITIAL_RESPONSE))
		{
			Npc.NpcFlags.Set(EElysiumNpcFlag::MADE_INITIAL_RESPONSE);
			const int32 Chance = FMath::Min(100, (Memory.SeeUnknownRepeatSightings + 5) * 20);
			if (ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99) < Chance)
			{
				Npc.NpcFlags.Set(EElysiumNpcFlag::ATTACK_UNKNOWN);
			}
			else
			{
				Npc.NpcFlags.Clear(EElysiumNpcFlag::ATTACK_UNKNOWN);
			}
		}
		if (Npc.NpcFlags.Has(EElysiumNpcFlag::ATTACK_UNKNOWN))
		{
			Out.Set(EElysiumNpcCond::UnknownRunTimer);
		}
		if (ShouldInvestigate(Npc, *Player, false))
		{
			Out.Set(EElysiumNpcCond::InvestigateSight);
		}
		return;
	}

	// Hidden, crouched-unseen, or grappled: the one-shot roll instead picks IGNORE_UNKNOWN off the
	// same ramp run the other way, unless `full_investigate` forces every sighting to be answered.
	if (!Npc.NpcFlags.Has(EElysiumNpcFlag::MADE_INITIAL_RESPONSE))
	{
		Npc.NpcFlags.Set(EElysiumNpcFlag::MADE_INITIAL_RESPONSE);
		const int32 Chance = FMath::Max(0, 50 - Memory.SeeUnknownRepeatSightings * 20);
		if (Npc.FullInvestigate == 0
			&& ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99) < Chance)
		{
			Npc.NpcFlags.Set(EElysiumNpcFlag::IGNORE_UNKNOWN);
			Npc.NpcFlags.Clear(EElysiumNpcFlag::FINISHED_IGNORE_UNKNOWN);
		}
		else
		{
			Npc.NpcFlags.Clear(EElysiumNpcFlag::IGNORE_UNKNOWN);
		}
	}

	// The 2-D closing speed: the player's velocity dotted with the normalized direction from the
	// player toward this NPC. Positive is the player closing in; retail normalizes only the
	// direction, never the velocity. Converted to Source units per second, the threshold's own.
	FVector Direction(Npc.Origin.X - Player->Origin.X, Npc.Origin.Y - Player->Origin.Y, 0.0);
	Direction = Direction.GetSafeNormal();
	const double ClosingSpeed = (Player->Velocity.X * Direction.X + Player->Velocity.Y * Direction.Y)
		/ ElysiumMove::U;

	if (Npc.NpcFlags.Has(EElysiumNpcFlag::IGNORE_UNKNOWN))
	{
		if (ClosingSpeed > UnknownClosingSpeedThreshold && Now >= Memory.SeeUnknownStartTimer)
		{
			Out.Set(EElysiumNpcCond::UnknownAdvancing);
			Out.Set(EElysiumNpcCond::InvestigateSight);
			return;
		}
		// The retraction: retail clears SEE_UNKNOWN mid-sweep here, overriding what the sense pass
		// raised earlier in this very same pass.
		Out.Clear(EElysiumNpcCond::SeeUnknown);
		if (Npc.NpcFlags.Has(EElysiumNpcFlag::LOOKED_AT_UNKNOWN)
			|| Npc.NpcFlags.Has(EElysiumNpcFlag::FINISHED_IGNORE_UNKNOWN))
		{
			return;
		}
		Out.Set(EElysiumNpcCond::IgnoreUnknown);
		return;
	}

	Out.Set(EElysiumNpcCond::InvestigateSight);
	if (ClosingSpeed < UnknownClosingSpeedThreshold)
	{
		Out.Set(EElysiumNpcCond::UnknownRetreating);
	}
	else if (ClosingSpeed <= UnknownClosingSpeedThreshold)
	{
		// Retail quirk: reachable only on exact float equality with the threshold, so effectively
		// dead. Reproduced rather than fixed.
		Out.Set(EElysiumNpcCond::UnknownHolding);
	}
	else
	{
		Out.Set(EElysiumNpcCond::UnknownAdvancing);
	}
}

namespace
{
	// `CAI_BaseNPC::TaskComplete(false)` (`0x10273e80`): `m_ScheduleState.fTaskStatus = COMPLETE`
	// unless `COND_TASK_FAILED` already stands.
	void CompleteTaskUnlessFailed(FElysiumNpc& Npc, const FElysiumNpcConditions& Conditions)
	{
		if (!Conditions.Has(EElysiumNpcCond::TaskFailed))
		{
			Npc.Schedule.TaskStatus = EElysiumTaskStatus::Complete;
		}
	}

	// `GetScheduleId(m_pSchedule->id)` through slot 447 compared against `SCHED_TROIKA_COMFORT`.
	bool IsRunningComfortSchedule(const FElysiumNpc& Npc)
	{
		return Npc.Schedule.IsRunning()
			&& Npc.IdSpace(EElysiumIdCategory::Schedule)->GlobalToLocal(Npc.Schedule.Current)
				== ElysiumNpcCond::ComfortScheduleNumber;
	}
}

void ElysiumNpcCond::GatherComfort(FElysiumNpc& Npc, double Now, FElysiumNpcConditions& Out)
{
	// The clock first, then the re-arm, then the idle test (`0x102b1a30` / `0x102b1a67` /
	// `0x102b1a60`): a due NPC re-arms and draws in every state.
	if (Now < Npc.NextComfortCheckTime)
	{
		return;
	}
	Npc.NextComfortCheckTime = Now + ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
		.FRandRange(static_cast<float>(ComfortSweepMinSeconds), static_cast<float>(ComfortSweepMaxSeconds));

	FElysiumEntityWorld* World = Npc.World;
	if (Npc.GetMind().State() == EElysiumNpcState::Idle && World != nullptr)
	{
		// 1. The nearest comfort-list member, self skipped. `FCOM` / `TEST AH,0x41` / `JP` at
		//    `0x102b1af6` keeps a candidate at `distance <= best`: 1024 itself qualifies and a tie goes
		//    to the later entry. Retail dereferences a stale handle and faults; story 8 removes a
		//    character from the list on death and removal, so `Resolve` skipping one never differs.
		FElysiumCombatCharacter* Nearest = nullptr;
		double NearestDistance = ComfortRangeUnits;
		for (const FElysiumEntityHandle& CandidateHandle : World->ComfortTargets())
		{
			FElysiumEntity* Candidate = World->Resolve(CandidateHandle);
			if (Candidate == nullptr || Candidate->Handle == Npc.Handle)
			{
				continue;
			}
			FElysiumCombatCharacter* Character = Candidate->AsCombatCharacter();
			if (Character == nullptr)
			{
				continue;
			}
			const double Distance = FVector::Distance(Npc.Origin, Candidate->Origin);
			if (Distance <= NearestDistance)
			{
				NearestDistance = Distance;
				Nearest = Character;
			}
		}

		if (Nearest != nullptr && !Npc.IsBusyWithDiscipline() && Npc.Schedule.IsRunning())
		{
			// 2. Already comforting: nothing while the same comforter is nearest, the task completed
			//    when a different one is.
			if (IsRunningComfortSchedule(Npc))
			{
				if (World->Resolve(Npc.GetTarget()) != Nearest)
				{
					CompleteTaskUnlessFailed(Npc, Out);
				}
				return;
			}

			// 3. A comforter that is an NPC (`+0x94`, the entity's cached `CAI_BaseNPC*`) must answer
			//    to the same connected squad as this one; a player comforter skips the test.
			if (const FElysiumNpcBase* ComforterNpc = Nearest->AsNpcBase();
				ComforterNpc != nullptr && ComforterNpc->ConnectedSquad() != Npc.ConnectedSquad())
			{
				return;
			}

			// 4. The comforter's own cap, `m_iComfortingCount >= 3` (`+0xe94`), no fallback.
			if (Nearest->ComfortingCount >= ComfortMaxComfortedTargets)
			{
				return;
			}
			++Nearest->ComfortingCount;
			Out.Set(EElysiumNpcCond::Comfort);
			Npc.SetTarget(Nearest->Handle);
			return;
		}
	}

	// 5. The tail (`0x102b1c0f`), reached when not idle, with no candidate, busy, or with no schedule:
	//    a running comfort schedule has its task completed.
	if (IsRunningComfortSchedule(Npc))
	{
		CompleteTaskUnlessFailed(Npc, Out);
	}
}

void ElysiumNpcCond::GatherSounds(FElysiumNpc& Npc, double Now, FElysiumNpcConditions& Out)
{
	FElysiumEntityWorld* World = Npc.World;
	FElysiumNpcMemory& Memory = Npc.Senses.Memory;

	// 1. The two clears the sweep owns, before the gate and before anything else.
	Out.Clear(EElysiumNpcCond::InvestigateSound);
	Out.Clear(EElysiumNpcCond::HearFlankSound);

	// The mask tester the sweep uses is `0x10269c70` -- the running program's mask ONLY, no
	// condition-set read. See `ElysiumSchedule::MaskHasCondition`.
	//
	// Resolved once for the whole sweep rather than per gate, which is also what retail does:
	// `CacheInterruptConditions` (`0x1026a0f0`) builds `m_ScheduleTestBits` once per think and every
	// tester reads the cached word. Nothing between these gates can install a schedule.
	const FElysiumNpcConditions Mask = ElysiumSchedule::EffectiveInterrupts(Npc.Schedule, Npc);
	const auto MaskLists = [&Mask](EElysiumNpcCond Cond) { return Mask.Has(Cond); };

	// 2. The gate on the whole six-arm body.
	const FElysiumGameSoundEvent* Winner = nullptr;
	if (Memory.NextInvestigateSoundTime <= Now)
	{
		// 3. The six arms, in retail's own evaluation order. The last one that passes claims the
		//    winner, which is why the order below is the effective priority
		//    combat > bullet impact > player > danger > physics danger > world -- and why it is NOT
		//    the same order `CommitBestSound` ranks by.
		struct FArm
		{
			EElysiumNpcCond Condition;
			const FElysiumGameSoundEvent* Record;
			bool bCombatMode;
			bool bSkipPredicate;   // `HEAR_DANGER` alone
		};
		const FArm Arms[] = {
			{ EElysiumNpcCond::HearWorld,         &Memory.LastSoundWorld,         false, false },
			{ EElysiumNpcCond::HearPhysicsDanger, &Memory.LastSoundPhysicsDanger, false, false },
			{ EElysiumNpcCond::HearDanger,        &Memory.LastSoundDanger,        false, true  },
			{ EElysiumNpcCond::HearPlayer,        &Memory.LastSoundPlayer,        false, false },
			{ EElysiumNpcCond::HearBulletImpact,  &Memory.LastSoundBulletImpact,  true,  false },
			{ EElysiumNpcCond::HearCombat,        &Memory.LastSoundCombat,        true,  false },
		};
		for (const FArm& Arm : Arms)
		{
			if (!Out.Has(Arm.Condition))
			{
				continue;
			}
			if (!Arm.bSkipPredicate && !MaskLists(Arm.Condition))
			{
				// The predicate's candidate is the sound's OWNER. Retail resolves the record's
				// handle and hands the result -- possibly null -- straight to `ShouldInvestigate`,
				// whose third line rejects null. An ownerless sound (a door, or an
				// `ambient_generic` inserting a null-owner type) therefore cannot be investigated
				// unless the running program already lists its condition.
				const FElysiumEntity* Owner = World
					? ResolveEnemyHandle(*World, Arm.Record->Source) : nullptr;
				if (Owner == nullptr || !ShouldInvestigate(Npc, *Owner, Arm.bCombatMode))
				{
					continue;
				}
			}
			Out.Set(EElysiumNpcCond::InvestigateSound);
			Winner = Arm.Record;
		}

		// 4. `HEAR_FLANK_SOUND`. All four terms, in retail's order. The enemy test is a RESOLVED
		//    POINTER comparison -- `GetEnemy()` (slot 167) against the record owner's resolved
		//    entity -- not a handle compare: a committed handle whose entity is gone is still
		//    `IsSet()`, and retail's `GetEnemy()` answers null for it.
		const FElysiumEntity* FlankEnemy = World ? ResolveEnemyHandle(*World, Npc.BaseMemory.Enemy) : nullptr;
		const FElysiumEntity* WinnerOwner = (World && Winner != nullptr)
			? ResolveEnemyHandle(*World, Winner->Source) : nullptr;
		if (MaskLists(EElysiumNpcCond::HearFlankSound) && Winner != nullptr
			&& FlankEnemy != nullptr && FlankEnemy == WinnerOwner)
		{
			// Where the sound IS, per `FUN_101b99d0(record)`: for raw types `0x10` BULLET_IMPACT and
			// `0x400` PHYSICS_DANGER with a resolvable owner it returns the OWNER's live
			// `GetAbsOrigin()`, and only otherwise the record's stored origin (`record + 0x20`).
			// So for those two arms retail asks "is my enemy behind me now", not "is the bullet hole
			// behind me" -- and the flank gate above has already established the owner IS my enemy.
			const bool bUseOwnerOrigin = WinnerOwner != nullptr
				&& (Winner->TypeMask == ElysiumGameSounds::BulletImpact
					|| Winner->TypeMask == ElysiumGameSounds::PhysicsDanger);
			const FVector SoundAt = bUseOwnerOrigin ? WinnerOwner->Origin : Winner->Position;
			// Retail measures from `GetAbsOrigin` (slot 217), not the eye, and against the cached
			// `m_vecForward` (+0x6290). Source angles carry the inverse Unreal yaw, the same
			// construction `IsInViewCone` uses.
			const float PitchRadians = FMath::DegreesToRadians(static_cast<float>(Npc.Angles.X));
			const float YawRadians = FMath::DegreesToRadians(-static_cast<float>(Npc.Angles.Y));
			const FVector Forward(FMath::Cos(PitchRadians) * FMath::Cos(YawRadians),
				FMath::Cos(PitchRadians) * FMath::Sin(YawRadians), -FMath::Sin(PitchRadians));
			// `< 0.0f`, strictly: a sound exactly abeam does not flank.
			if (FVector::DotProduct(SoundAt - Npc.Origin, Forward) < 0.0)
			{
				Out.Set(EElysiumNpcCond::HearFlankSound);
			}
		}
	}

	// 5. The `SEE_SOUND_SOURCE` tail. Outside the gate, and over `m_hBestSoundSource` -- the source
	//    the LAST commit chose, not this sweep's winner.
	if (!MaskLists(EElysiumNpcCond::SeeSoundSource))
	{
		Out.Clear(EElysiumNpcCond::SeeSoundSource);
		return;
	}
	// Retail compares RESOLVED POINTERS throughout this tail, not handles, which is what makes the
	// dead-handle arms below reachable at all.
	// `ResolveEnemyHandle`, not `Resolve`: retail dereferences an EHANDLE here, which answers with a
	// dead actor as readily as a live one. `Resolve` collapses dead into null for the script
	// contract, and that would silently take the null gate below.
	const FElysiumEntity* Source = World ? ResolveEnemyHandle(*World, Npc.BaseMemory.BestSoundSource) : nullptr;

	// `FUN_102b8cd0(this, source)`: true only when the source is non-null and my `IRelationType`
	// to it is NEITHER `D_HT` (1) NOR `D_FR` (2). False and the tail returns WITHOUT touching the
	// condition, so a previously raised `SEE_SOUND_SOURCE` stands.
	//
	// The polarity is retail's and it is worth stating plainly, because it reads backwards: the
	// tail runs only for a source I do NOT hate and do NOT fear, which leaves the `SEE_ENEMY` rung
	// live only for a committed enemy I relate to as `D_LI`/`D_NU`.
	if (Source == nullptr)
	{
		return;
	}
	{
		const EElysiumRelationship Relation =
			Npc.Relationships.Resolve(Source->Handle, NpcCondClassnameOf(*Source));
		if (Relation == EElysiumRelationship::Hate || Relation == EElysiumRelationship::Fear)
		{
			return;
		}
	}

	const FElysiumEntity* ClosestPlayer = World ? ResolveEnemyHandle(*World, Memory.ClosestPlayer) : nullptr;
	const FElysiumEntity* Enemy = World ? ResolveEnemyHandle(*World, Npc.BaseMemory.Enemy) : nullptr;

	// The comparison chain. First match wins and demands its own sight condition; a match whose
	// condition is unset, or no match at all, falls through past the loop.
	//
	// The last four rungs are `m_hLastSeenHateEnt` / `Fear` / `Dislike` / `NemesisEnt`
	// (+0x5b68/6c/70/74). They are LIVE: `CAI_BaseNPC::OnLooked` (`0x1026a2c0`) writes one of them
	// on every assessed sighting -- case `D_HT` splits by `IRelationPriority` into
	// Dislike (`< 0`) / Hate (`< 0xb`) / Nemesis, and case `D_FR` writes Fear behind the same
	// `flags2 & 0x10000` gate -- and `FUN_1027c300` resets all four to `0xffffffff` at spawn.
	// `Memory.LastSeen[]` is this runtime's copy of exactly those four, written by `GatherSight`
	// from the same priority split, so it is the correct comparand.
	//
	// In practice the three `D_HT`-derived rungs need the source's relation to have CHANGED away
	// from `D_HT` since the sighting, because step 1 above rejects a hated source outright; Fear
	// likewise. Narrow, but not unreachable, and the chain is reproduced whole.
	struct FSourceArm
	{
		const FElysiumEntity* Comparand;
		EElysiumNpcCond Requires;
	};
	const auto LastSeen = [&](FElysiumNpcBaseMemory::ESeen Slot) -> const FElysiumEntity*
	{
		return World ? ResolveEnemyHandle(*World, Npc.BaseMemory.Seen(Slot)) : nullptr;
	};
	const FSourceArm Chain[] = {
		{ ClosestPlayer,                                    EElysiumNpcCond::SeePlayer  },
		{ Enemy,                                            EElysiumNpcCond::SeeEnemy   },
		{ LastSeen(FElysiumNpcBaseMemory::ESeen::Hate),         EElysiumNpcCond::SeeHate    },
		{ LastSeen(FElysiumNpcBaseMemory::ESeen::Fear),         EElysiumNpcCond::SeeFear    },
		{ LastSeen(FElysiumNpcBaseMemory::ESeen::Dislike),      EElysiumNpcCond::SeeDislike },
		{ LastSeen(FElysiumNpcBaseMemory::ESeen::Nemesis),      EElysiumNpcCond::SeeNemesis },
	};
	for (const FSourceArm& Arm : Chain)
	{
		// A rung whose handle has never been written resolves to null, and `Source` cannot be null
		// here -- step 1 returned on that. So an unwritten rung simply never matches.
		if (Source != Arm.Comparand)
		{
			continue;
		}
		if (Out.Has(Arm.Requires))
		{
			Out.Set(EElysiumNpcCond::SeeSoundSource);
		}
		else
		{
			Out.Clear(EElysiumNpcCond::SeeSoundSource);
		}
		return;
	}

	// The stranger arm: something I have no standing sight condition about. Rate limited, and while
	// the limit stands the condition is left ALONE rather than cleared.
	if (Now < Memory.NextSeeSoundSourceTime)
	{
		return;
	}
	if (FElysiumNpcSenses::IsInViewCone(Npc, *Source)
		&& FElysiumNpcSenses::IsVisible(Npc, *Source, Now))
	{
		Out.Set(EElysiumNpcCond::SeeSoundSource);
	}
	else
	{
		Out.Clear(EElysiumNpcCond::SeeSoundSource);
	}
	Memory.NextSeeSoundSourceTime = Now + SeeSoundSourceCadenceSeconds;
}

// --- Weapon capability ---

const TCHAR* ElysiumNpcCond::CapabilityName(ECapability Capability)
{
	switch (Capability)
	{
	case ECapability::Melee:  return TEXT("melee");
	case ECapability::Ranged: return TEXT("ranged");
	default:                  return TEXT("unarmed");
	}
}

int32 ElysiumNpcCond::CapabilityBits(ECapability Capability)
{
	switch (Capability)
	{
	case ECapability::Melee:  return MeleeCapabilityBits;
	case ECapability::Ranged: return RangedCapabilityBits;
	default:                  return 0;
	}
}

namespace
{
	// The active weapon controller, or null. One resolution, shared by the capability answer and
	// every attack condition derived from it.
	FElysiumWeapon* NpcCondActiveWeapon(const FElysiumCombatCharacter& Char)
	{
		// `Active` is a const read that answers a mutable item, which is what the controller is.
		FElysiumItem* Item = Char.Inventory.Active(Char);
		return Item != nullptr ? Item->AsWeapon() : nullptr;
	}
}

ElysiumNpcCond::ECapability ElysiumNpcCond::WeaponCapability(const FElysiumNpc& Npc)
{
	return WeaponCapability(static_cast<const FElysiumCombatCharacter&>(Npc));
}

ElysiumNpcCond::ECapability ElysiumNpcCond::WeaponCapability(const FElysiumCombatCharacter& Char)
{
	const FElysiumWeapon* Weapon = NpcCondActiveWeapon(Char);
	const FElysiumItemDef* Record = Weapon != nullptr ? Weapon->Data() : nullptr;
	if (Record == nullptr || !Record->IsControllableWeapon())
	{
		// No weapon at all, or an active item whose record is not one of the three wielded families.
		// Retail's fists are a real `weapon_melee` record, so this is the state of a character the
		// item catalogue could not arm — a headless world, or a `vdata` set with no `item_w_fists`.
		return ECapability::Unarmed;
	}
	return Record->Type == EElysiumItemType::WeaponMelee ? ECapability::Melee : ECapability::Ranged;
}

// --- Attack conditions ---

namespace
{
	// The attack cone, as a dot product against the NPC's facing. This is the SWING's cone
	// (`FindEntityFOV`'s 30-degree half-angle), deliberately not the observer's much wider
	// perception cone — an NPC can see an enemy it is in no position to hit.
	//
	// The entity's `Angles.Y` is the NEGATED Unreal yaw the motor is driven with, the same frame
	// `FElysiumNpcSenses::IsInViewCone` reads it in.
	bool NpcCondFacesTarget(const FElysiumNpcBase& Npc, const FVector& TargetCm)
	{
		FVector To = TargetCm - Npc.Origin;
		To.Z = 0.0;
		if (To.IsNearlyZero())
		{
			// Standing on the target: there is no direction to be facing, and every cone contains it.
			return true;
		}
		To.Normalize();
		const double YawRadians = FMath::DegreesToRadians(-Npc.Angles.Y);
		const FVector Forward(FMath::Cos(YawRadians), FMath::Sin(YawRadians), 0.0);
		const double ConeDot =
			FMath::Cos(FMath::DegreesToRadians(
				static_cast<double>(ElysiumWeapons::MeleeConeHalfAngleDegrees)));
		return FVector::DotProduct(Forward, To) >= ConeDot;
	}
}

bool ElysiumNpcCond::WerewolfZoneSuppressesMelee(const FElysiumNpc& Npc, FElysiumNpcConditions& Out)
{
	const FElysiumEntityWorld* World = Npc.World;
	const FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	if (World == nullptr || !Npc.BaseMemory.Enemy.IsSet())
	{
		return false;
	}
	const FElysiumEntity* Enemy = ResolveEnemyHandle(*World, Npc.BaseMemory.Enemy);
	if (Enemy == nullptr || Enemy->IsInert())
	{
		return false;
	}
	// `CNPC_VWerewolf::GatherAttackConditions` (`0x103d02b0`, slot 561) — the ONE species override of
	// the gather, and a SUPPRESSION rather than an addition. With the werewolf standing in a zone the
	// two bits of `m_iZoneFlags` (`+0x66e8`) name — `0x4` or `0x100` — and a live enemy whose ORIGIN Z
	// differs from the werewolf's by strictly more than `_DAT_10462950` = **40.0** Source units, it
	// CLEARS `CAN_MELEE_ATTACK1` (0x51) and `CAN_MELEE_ATTACK2` (0x52) and RETURNS: the base gather
	// (`CAI_BaseNPC::GatherAttackConditions` 0x1026dd10) never runs that pass. Otherwise it is a plain
	// forward. Answers whether the suppression ran (the base gather must not).
	//
	// Story 29c's row read `thunk_FUN_10269b50` as "force"; it is `ClearCondition` — so the arm takes
	// melee away from a werewolf on a different floor of a zoned room rather than granting it.
	//
	// The Z terms are read fresh from both origins, as retail reads them off `GetAbsOrigin` (`+0x364`)
	// rather than off the cached `m_flEnemyHeightDiff`.
	{
		constexpr double WerewolfZoneHeightUnits = static_cast<double>(ElysiumNpcTunables::Forty);
		constexpr uint32 WerewolfZoneMeleeSuppressBits = 0x4u | 0x100u;
		const double HeightDeltaUnits =
			FMath::Abs(Npc.Origin.Z - Enemy->Origin.Z) / ElysiumMove::U;
		if ((Npc.WerewolfHintFlags & WerewolfZoneMeleeSuppressBits) != 0
			&& HeightDeltaUnits > WerewolfZoneHeightUnits)
		{
			Out.Clear(EElysiumNpcCond::CanMeleeAttack1);
			Out.Clear(EElysiumNpcCond::CanMeleeAttack2);
			return true;
		}
	}
	return false;
}

void ElysiumNpcCond::GatherAttackConditions(const FElysiumNpcBase& Npc, double Now,
	FElysiumNpcConditions& Out)
{
	const FElysiumEntityWorld* World = Npc.World;
	// The weapon-sight occlusion is slot 481's LOS debounce (`+0x5b98`, `0x10270b20`) at its limit.
	const bool bEnemyOccluded = Npc.BaseMemory.EnemyOccludedCheck >= ElysiumNpcSense::EnemyLosFailureLimit;
	if (World == nullptr || !Npc.BaseMemory.Enemy.IsSet())
	{
		return;
	}
	const FElysiumEntity* Enemy = ResolveEnemyHandle(*World, Npc.BaseMemory.Enemy);
	if (Enemy == nullptr || Enemy->IsInert())
	{
		// `ENEMY_DEAD` / `LOST_ENEMY` already describe this; range against a corpse is not a fact.
		return;
	}

	// `CNPC_VWerewolf::GatherAttackConditions` (`0x103d02b0`, slot 561) is `FElysiumNpcWerewolf`'s
	// override (story 5 step 3): `WerewolfZoneSuppressesMelee` below, ahead of a direct call here.

	// SEAM (plumbed, never set): `SHOULD_DODGE` (0x0c), `SHOULD_BLOCK` (0x0d), `SHOULD_STEPBACK`
	// (0x0e) and `SHOULD_KICK` (0x0f). The NOTICE that would feed them is real and lands below
	// (`NoticeMeleeAttack` writes the attacker into memory with the recovered five-second life), but
	// the policy turning a noticed incoming attack into ONE of these four is not decoded — nothing in
	// the survey names the ratings, timers or randomisation that choose between dodging, blocking,
	// kicking and stepping back. Raising any of them from the notice alone would make every NPC
	// dodge, which is a behaviour, not a gap. The melee selector's four branches exist and are
	// driven by injection in `Elysium.Substrate.NpcCombat.MeleeSelectorOrder`.

	const FElysiumWeapon* Weapon = NpcCondActiveWeapon(Npc);

	// `WAITING_ATTACK_TIME` (0x2f) — the recovery deadline the weapon controller owns. An unarmed
	// NPC has no deadline to wait on, which is the honest answer rather than a permanent hold.
	const bool bReady = Weapon == nullptr || Now >= Weapon->NextPrimaryAttackTime;
	if (!bReady)
	{
		Out.Set(EElysiumNpcCond::WaitingAttackTime);
	}

	const double DistanceCm = FVector::Dist(Npc.EyePosition(), Enemy->EyePosition());
	const double MeleeReachCm =
		static_cast<double>(ElysiumWeapons::MeleeReachSourceUnits) * ElysiumMove::U;
	const bool bFacing = NpcCondFacesTarget(Npc, Enemy->Origin);

	const ECapability Capability = WeaponCapability(Npc);
	if (Capability != ECapability::Ranged)
	{
		// --- The melee band -----------------------------------------------------------------------
		if (DistanceCm > MeleeReachCm)
		{
			// CHOSEN, NOT RECOVERED: melee's own `TOO_FAR_TO_ATTACK` edge is the swing's reach. No
			// decoded body states a separate melee band, and any other number would let the selector
			// choose an attack the weapon's acquisition then refuses (or hold an NPC out of a swing
			// it could land). Replace the constant, not the shape.
			Out.Set(EElysiumNpcCond::TooFarToAttack);
		}
		else if (bFacing && bReady)
		{
			Out.Set(EElysiumNpcCond::CanMeleeAttack1);
		}
		return;
	}

	// --- The ranged band: the active weapon's slot 365 (0018 story 8, findings R3) ----------------
	// `0x1026dd10`'s weapon arm (`bits_CAP_WEAPON_RANGE_ATTACK1` 0x2000 with an active weapon) hands
	// `(enemy, dot, d)` to `CBaseCombatWeapon 0x1024f670` (`CALL [EDX+0x5b4]` at `0x1026de87`) and
	// raises the ONE condition it answers. The item text's `Range` key feeds none of it.
	if (Weapon == nullptr)
	{
		return;
	}
	// `d` is the gather's own second argument, `0x10270890` in `GatherEnemyConditions` (`10270ef2`):
	// 3-D between the two origins with the vertical term replaced by the bounding-box gap. The port's
	// gather measures it itself (see `FElysiumNpcBase::GatherAttackConditions`), through the same body.
	const float DistanceUnits = Npc.Conditions19EnemyDistanceUnits(*Enemy);
	// `dot` (`1026dd6c..1026ddfe`): `enemy.origin - my.origin` (slot 217 both), Z zeroed
	// (`1026ddbf`), normalised by `0x10137220` (`v *= 1 / (|v| + FLT_EPSILON)`), dotted with slot
	// 368 `BodyDirection2D` (`+0x5c0`). The two vectors share this runtime's frame, so the axis
	// reflection cancels. A zero delta dots to 0 and fails the 0.5 facing test, as retail's does.
	FVector ToEnemyUnits = (Enemy->GetAbsOrigin() - Npc.GetAbsOrigin()) / static_cast<double>(ElysiumMove::U);
	ToEnemyUnits.Z = 0.0;
	ToEnemyUnits *= 1.0 / (ToEnemyUnits.Size() + static_cast<double>(ElysiumNpcTunables::FloatEpsilon));
	const float Dot = static_cast<float>(FVector::DotProduct(Npc.BodyDirection2D(), ToEnemyUnits));
	const int32 Answer = Weapon->RangeAttack1Conditions(Enemy, Dot, DistanceUnits, Now);
	if (Answer == static_cast<int32>(EElysiumNpcCond::CanRangeAttack1))
	{
		// `1026ded9..1026df45`: 0x4f is kept only if slot 562 `WeaponLOSCondition` passes from the
		// eye to the enemy's eye or to its body target, each with `bSetConditions = 1` — its weapon
		// slot 364 (`0x1024f330`) raises `WEAPON_SIGHT_OCCLUDED` (0x66), or `WEAPON_BLOCKED_BY_FRIEND`
		// (0x63) on a friendly hit; both failing raises no 0x4f. CHOSEN, NOT RECOVERED here: the two traces
		// are stood in for by the eye's debounce latch (`+0x5b98` at its limit), so the pair agrees
		// with `ENEMY_OCCLUDED` where retail's muzzle traces could disagree. See the
		// `WEAPON_THROUGH_WALL` seam in this function's declaration.
		Out.Set(bEnemyOccluded ? EElysiumNpcCond::WeaponSightOccluded : EElysiumNpcCond::CanRangeAttack1);
		return;
	}
	// Every other answer, 0 included (`1026df61 PUSH EBP` — retail's `SetCondition(COND_NONE)`).
	Out.Set(static_cast<EElysiumNpcCond>(Answer));
}

// --- The incoming-attack notice ---

bool ElysiumNpcCond::NoticeMeleeAttack(FElysiumNpc& Victim, const FElysiumEntityHandle& Attacker,
	const FVector& AttackerOrigin, double Now)
{
	if (!Attacker.IsSet() || Attacker == Victim.Handle || Victim.IsInert())
	{
		return false;
	}
	FElysiumNpcMemory& Memory = Victim.Senses.Memory;
	const double AcceptanceCm =
		static_cast<double>(MeleeNoticeAcceptanceUnits) * ElysiumMove::U;
	const bool bNear = FVector::Dist(Victim.Origin, AttackerOrigin) <= AcceptanceCm;
	// The visibility route, at the senses' single-observer scope: the only actor this NPC tracks sight
	// of is the player.
	const bool bVisible = Memory.bPlayerVisible && Memory.ClosestPlayer.IsSet()
		&& Memory.ClosestPlayer == Attacker;
	if (!bNear && !bVisible)
	{
		return false;
	}
	Memory.DetectedAttackAttacker = Attacker;
	Memory.DetectedAttackTime = Now;
	return true;
}

bool ElysiumNpcCond::HasDetectedAttack(const FElysiumNpc& Npc, double Now)
{
	const FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	return Memory.DetectedAttackAttacker.IsSet() && Memory.DetectedAttackTime >= 0.0
		&& (Now - Memory.DetectedAttackTime) <= DetectedAttackRetentionSeconds;
}

// --- Ideal state ---
//
// Story 29e, family State19 replaced this file's two-layer summary with slot 461's retail bodies.
// `FElysiumNpcBase::BaseSelectIdealState` (`0x1026f660`), `FElysiumNpc::TroikaSelectIdealState`
// (`0x102ad660`) and the twelve species arms live in `Substrate/ElysiumNpcState.cpp` and
// `ElysiumNpcState_2.cpp`.
