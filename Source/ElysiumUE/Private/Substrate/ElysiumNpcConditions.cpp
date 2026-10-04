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
#include "Visual/ElysiumNpcClips.h"            // FElysiumNpcClip — the melee band's sequence descriptors

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
	// Two registered conditions with no enumerator, which their producers push by number:
	// `HINT_INVALID` 0x29 (`ElysiumNpcThink.cpp`, `0x10293164`) and `BEHIND_ENEMY` 0x57
	// (`Cond19BehindEnemy`, `0x102710b4`). Names read off the base registrar `FUN_102c8ce0`
	// (`docs/vtmb/npc-ai/conditions-and-states.md` § "The base condition table"; `COND_BEHIND_ENEMY`
	// is its string `0x106024a4`). Compared rather than switched on: a case label outside the
	// enumerators is a compiler warning.
	if (Cond == static_cast<EElysiumNpcCond>(0x29))
	{
		return TEXT("HINT_INVALID");
	}
	if (Cond == static_cast<EElysiumNpcCond>(0x56))
	{
		// Its sibling, pushed by number at `0x10271092` (`Cond19EnemyFacingMe`); the base table's
		// row `56 ENEMY_FACING_ME`. Unnamed until spec 0002 V5a: no gather raised it while the
		// player's slot 363 was a stub.
		return TEXT("ENEMY_FACING_ME");
	}
	if (Cond == static_cast<EElysiumNpcCond>(0x57))
	{
		return TEXT("BEHIND_ENEMY");
	}
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

	// `1026a2c6..1026a2cf`: `OnLooked`'s FIRST call, `ClearConditions(0x105c979c, 6)` (`0x10269bd0`),
	// on every pass -- before the skip entity is resolved and before the kept list is walked, so a
	// sighting that is no longer in the list drops on this pass. The table, read out of retail's
	// `.rdata`: `43 45 46 44 5b 5a`. In the same gather each is raised again only by this loop
	// (SEE_PLAYER `1026a392`, SEE_ENEMY `1026a3de`, SEE_DISLIKE / SEE_NEMESIS / SEE_HATE `1026a474`,
	// SEE_FEAR `1026a4e5`) and, for SEE_ENEMY alone, by slot 481 `GatherEnemyConditions` (`10270cfb`:
	// below the occlusion limit, in the cone and passing `QuerySeeEntity`). Q-H3, story V13.
	static const EElysiumNpcCond SeeTable[] = {
		EElysiumNpcCond::SeeHate,     // 0x43
		EElysiumNpcCond::SeeDislike,  // 0x45
		EElysiumNpcCond::SeeEnemy,    // 0x46
		EElysiumNpcCond::SeeFear,     // 0x44
		EElysiumNpcCond::SeeNemesis,  // 0x5b
		EElysiumNpcCond::SeePlayer,   // 0x5a
	};
	for (const EElysiumNpcCond Cond : SeeTable)
	{
		Out.Clear(Cond);
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
			Npc.TraceTaskDone();   // the AI trace's `taskdone` (debug output only, behind its sink)
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
	// The capability bits `GatherAttackConditions 0x1026dd10` tests on slot 513's word (`+0x804`).
	constexpr int32 GGatherCapWeaponRange1 = 0x2000;    // 0x1026de3a TEST EBP,0x2000
	constexpr int32 GGatherCapInnateRange1 = 0x20000;   // 0x1026de8f TEST EBP,0x20000
	constexpr int32 GGatherCapWeaponMelee1 = 0x8000;    // 0x1026df6d TEST EBP,0x8000
	constexpr int32 GGatherCapInnateMelee1 = 0x80000;   // 0x1026dfa5 TEST EBP,0x80000
	// `CWeaponMelee`'s slot 367 `0x103eac30`: `0x103ea7e0(0x4b, enemy, dot, dist)`.
	constexpr int32 GGatherActMeleeAttack1 = 0x4b;
	// `0x7f7fffff`, the extended timer's idle sentinel (`0x1026dfdd`, `0x1026e02c`), carried as the
	// double the port's stamp is (spawn writes the same value, `ElysiumNpcBaseLifecycle2.cpp`).
	constexpr double GGatherFltMax = static_cast<double>(TNumericLimits<float>::Max());

	// The five cells of `0x103ea7e0` (packet S4 item b). Every one is already a tunables row under
	// the name of its first reader; the address is the same cell.
	constexpr double GMeleeBandDotMin = ElysiumNpcTunables::MeleeDotMin;         // 0x104492d0 f64 0.7
	constexpr float GMeleeBandEnvelopeHalf = ElysiumNpcTunables::Half;           // 0x104454d0 f32 0.5
	constexpr float GMeleeBandFarScale = ElysiumNpcTunables::CoverLeanSetScale;  // 0x1049ae90 f32 1.2
	constexpr float GMeleeBandFarFloorUnits = ElysiumNpcTunables::Melee1OuterBand;   // 0x1044ddb0 f32 256.0
	constexpr double GMeleeBandCloseScale = ElysiumNpcTunables::QuarterDouble;   // 0x10449260 f64 0.25
}

int32 ElysiumNpcCond::MeleeWeaponBand(const FMeleeWeaponBandQuery& Query)
{
	// `0x103ea7e0`, read off the listing (the decompilation drops the `0x51` return at
	// `0x103ea8e5`). `0x103ea7f0..0x103ea80a`: no owner, or an owner with no model, answers 0. The
	// model test has no counterpart: a body with no clips hands an empty sequence list, which
	// answers 0 below as well (but after the `0x51` arm, which retail would not reach).
	if (Query.Owner == nullptr)                                                  // 0x103ea7f9
	{
		return 0;
	}
	// `0x103ea82f..0x103ea83e`: the target's `+0x9c`, non-null exactly for a combat character.
	FElysiumCombatCharacter* const TargetCharacter =
		Query.Target != nullptr ? Query.Target->AsCombatCharacter() : nullptr;

	// `ready` (`0x103ea84e..0x103ea899`): the three stamps strictly behind `curtime` (`FLD stamp /
	// FCOMP curtime / TEST AH,5 / JP not-ready`, so an equal stamp is NOT ready), then the target's
	// slot 327 (`+0x51c`) when it is a combat character.
	bool bReady = false;
	if (Query.WeaponNextPrimaryAttackTime < Query.Now                            // 0x103ea84e +0x730
		&& Query.WeaponNextSecondaryAttackTime < Query.Now                       // 0x103ea862 +0x734
		&& Query.OwnerNextAttackTime < Query.Now)                                // 0x103ea876 owner +0x1564
	{
		bReady = true;                                                           // 0x103ea888
		if (TargetCharacter != nullptr)
		{
			bReady = TargetCharacter->Slot327();                                 // 0x103ea893
		}
	}

	// The `0x51` arm (`0x103ea89d..0x103ea8e8`): `dot > 0.7` (`AND 0x4100 / JNZ`, strict), a combat
	// character target, `ready`, and the owner's slot 331 `ChooseMeleeAttackSequence 0x10347180`
	// `(weapon, target, translated activity, &out)` true with `out >= 0` (`out` seeded -1,
	// `0x103ea8cb`). The slot is CALLED and has no stand-in here: its port body is a counting stub
	// answering false until lane V11-3 (`stories/v11/brief-V11-3-slot-331.md`), so the weapon arm
	// raises no `0x51` before V11's wave. No record of V5a / V4a / V4b observes it (no NPC enters
	// melee before V11, triage N3).
	if (static_cast<double>(Query.Dot) > GMeleeBandDotMin && TargetCharacter != nullptr && bReady)
	{
		int32 OutSequence = INDEX_NONE;                                          // 0x103ea8cb
		if (Query.Owner->ChooseMeleeAttackSequence(Query.Weapon, TargetCharacter,
				Query.TranslatedActivity, &OutSequence)                          // 0x103ea8d3 slot 331 (+0x52c)
			&& OutSequence >= 0)                                                 // 0x103ea8e1
		{
			return static_cast<int32>(EElysiumNpcCond::CanMeleeAttack1);         // 0x103ea8e8 0x51
		}
	}

	// The band's three numbers over the activity's sequences (`0x103ea902..0x103ea9f7`). The baked
	// values are CENTIMETRES and `dist` / 256.0 are SOURCE units: the CLIP side is converted, here.
	const float CmPerUnit = static_cast<float>(ElysiumMove::U);
	float Lo = 100000.0f;                                                        // 0x103ea904 0x47c35000
	float Hi = -100000.0f;                                                       // 0x103ea90c 0xc7c35000
	float MeanSum = 0.0f;                                                        // 0x103ea914
	int32 EnvelopeCount = 0;                                                     // 0x103ea91c
	for (const FElysiumNpcClip& Sequence : Query.Sequences)
	{
		// `(target CC || seqdesc+0x10 > 0) && seqdesc+0x2c4 > 0` (`0x103ea976..0x103ea989`): `+0x10`
		// is the clip's weight, `+0x2c4` its swing-record count.
		if ((TargetCharacter == nullptr && Sequence.Weight < 1) || Sequence.Swings.Num() < 1)
		{
			continue;
		}
		// `+0x2cc`. A descriptor that states no low edge is baked as -1 ("unstated"); the file's own
		// word for those is taken as 0.0 here. DOUBTFUL, reported: the raw `+0x2cc` of an unstated
		// descriptor is not re-read in the corpus.
		const float LowReachUnits = (Sequence.HasLowReach() ? Sequence.LowReachCm : 0.0f) / CmPerUnit;
		const float ReachUnits = Sequence.ReachCm / CmPerUnit;                   // +0x2d0
		if (LowReachUnits < Lo)                                                  // 0x103ea98b..0x103ea9a2
		{
			Lo = LowReachUnits;
		}
		if (ReachUnits > Hi)                                                     // 0x103ea9a6..0x103ea9bf
		{
			Hi = ReachUnits;
		}
		// The `+0x2bc` records at `+0x2c0`, 24-byte stride: `(rec[0] + rec[3]) * 0.5`, the two
		// corners' first (reach) axis (`0x103ea9db..0x103ea9f2`).
		EnvelopeCount += Sequence.Envelopes.Num();                               // 0x103ea9d7
		for (const FElysiumMeleeEnvelope& Envelope : Sequence.Envelopes)
		{
			MeanSum += static_cast<float>(Envelope.Max.X + Envelope.Min.X) / CmPerUnit
				* GMeleeBandEnvelopeHalf;                                        // 0x103ea9e4
		}
	}
	if (EnvelopeCount == 0)                                                      // 0x103eaa01 / 0x103eaa03
	{
		return 0;
	}
	// `mean`, clamped into `[lo, hi]`, the upper edge first (`0x103eaa09..0x103eaa3f`).
	float Mean = MeanSum / static_cast<float>(EnvelopeCount);                    // 0x103eaa0d
	if (Mean > Hi)                                                               // 0x103eaa15
	{
		Mean = Hi;
	}
	else if (Mean < Lo)                                                          // 0x103eaa30
	{
		Mean = Lo;
	}
	// The far limit, `max(hi * 1.2, 256.0)` (`0x103eaa43..0x103eaa60`).
	float FarUnits = Hi * GMeleeBandFarScale;                                    // 0x103eaa47
	if (GMeleeBandFarFloorUnits > FarUnits)                                      // 0x103eaa53
	{
		FarUnits = GMeleeBandFarFloorUnits;
	}
	if (Query.DistUnits > FarUnits)                                              // 0x103eaa66..0x103eaa76
	{
		return static_cast<int32>(EElysiumNpcCond::TooFarForMelee);              // 0x103eaa7b 9
	}
	if (Query.DistUnits > Hi)                                                    // 0x103eaa8a..0x103eaa9c
	{
		return static_cast<int32>(EElysiumNpcCond::TooFarToAttack);              // 0x103eaaa1 0x60
	}
	if (static_cast<double>(Query.Dot) < GMeleeBandDotMin)                       // 0x103eaab0..0x103eaac2
	{
		return static_cast<int32>(EElysiumNpcCond::NotFacingAttack);             // 0x103eaac7 0x61
	}
	if (Query.DistUnits < Lo)                                                    // 0x103eaad6..0x103eaae6
	{
		return static_cast<int32>(EElysiumNpcCond::TooCloseToAttack);            // 0x103eaaeb 0x5f
	}
	if (TargetCharacter != nullptr && bReady)                                    // 0x103eaafa / 0x103eab02
	{
		// `mean * 0.25 > dist` -> 0x5f, else 0x60 (`0x103eab06..0x103eab2a`).
		return static_cast<double>(Mean) * GMeleeBandCloseScale > static_cast<double>(Query.DistUnits)
			? static_cast<int32>(EElysiumNpcCond::TooCloseToAttack)              // 0x103eab20 0x5f
			: static_cast<int32>(EElysiumNpcCond::TooFarToAttack);               // 0x103eab2a 0x60
	}
	return 0;                                                                    // 0x103eab39
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

void ElysiumNpcCond::GatherAttackConditions(FElysiumNpcBase& Npc, double Now)
{
	const FElysiumEntityWorld* World = Npc.World;
	FElysiumNpcConditions& Out = Npc.Cognition.Conditions;
	// A NULL guard only. Retail has no test here: its caller (`GatherEnemyConditions 0x10270b20`,
	// `0x102711dc`) hands a live `GetEnemy()`, already past its own `IsAlive` return (`0x10270e5a`).
	// The port's old "inert enemy" return went with V5a-1: a hidden, living enemy reaches retail's
	// gather and gets the top clear, and the return skipped it.
	if (World == nullptr || !Npc.BaseMemory.Enemy.IsSet())
	{
		return;
	}
	FElysiumEntity* const Enemy =
		const_cast<FElysiumEntity*>(ResolveEnemyHandle(*World, Npc.BaseMemory.Enemy));
	if (Enemy == nullptr)
	{
		return;
	}

	// `CNPC_VWerewolf::GatherAttackConditions` (`0x103d02b0`, slot 561) is `FElysiumNpcWerewolf`'s
	// override (story 5 step 3): `WerewolfZoneSuppressesMelee` above, ahead of a direct call here.

	// SEAM (plumbed, never set): `SHOULD_DODGE` (0x0c), `SHOULD_BLOCK` (0x0d), `SHOULD_STEPBACK`
	// (0x0e) and `SHOULD_KICK` (0x0f) are not this body's: `RefreshCombatConditions 0x102b2570`
	// owns the last two, and the policy behind the first two is not decoded.

	// `dot` (`1026dd6c..1026ddfe`): `enemy.origin - my.origin` (slot 217 both), Z zeroed
	// (`1026ddbf`), normalised by `0x10137220` (`v *= 1 / (|v| + FLT_EPSILON)`), dotted with slot
	// 368 `BodyDirection2D` (`+0x5c0`). The two vectors share this runtime's frame, so the axis
	// reflection cancels. A zero delta dots to 0 and fails every facing test, as retail's does.
	FVector ToEnemyUnits = (Enemy->GetAbsOrigin() - Npc.GetAbsOrigin()) / static_cast<double>(ElysiumMove::U);
	ToEnemyUnits.Z = 0.0;
	ToEnemyUnits *= 1.0 / (ToEnemyUnits.Size() + static_cast<double>(ElysiumNpcTunables::FloatEpsilon));
	const float Dot = static_cast<float>(FVector::DotProduct(Npc.BodyDirection2D(), ToEnemyUnits));
	// `d` is the gather's own second argument, `0x10270890` in `GatherEnemyConditions` (`10270ef2`):
	// 3-D between the two origins with the vertical term replaced by the bounding-box gap. The port's
	// gather measures it itself (see `FElysiumNpcBase::GatherAttackConditions`), through the same body.
	const float DistanceUnits = Npc.Conditions19EnemyDistanceUnits(*Enemy);

	// --- The top clear ----------------------------------------------------------------------------
	// `0x1026de02 CALL [EDX+0x8c0]`: slot 560 `ClearAttackConditions 0x1026dc80`, before anything is
	// set. Its eleven words, never the band words 0x08 / 0x5f / 0x60 / 0x09.
	Npc.ClearAttackConditions();                                                 // 0x1026de02

	// `0x1026de0c`: slot 513 `CapabilitiesGet` (`+0x804`), kept for both arms. Retail's word is
	// `m_afCapability | activeWeapon->slot360()`; the port's slot 360 is a seam answering 0
	// (`ActiveWeaponCapabilityWord`), so the WEAPON's two bits are read through
	// `WeaponCapability` (the item record's family) and OR-ed in at each test: `Ranged` stands for
	// the weapon's `0x2000`, `Melee` for its `0x8000` (the melee class word is `0x40018000`).
	const int32 Caps = Npc.CapabilitiesGet();                                    // 0x1026de0c
	// `0x1026de18..0x1026de30`: the enemy's slot 197 `BodyTarget` (`+0x314`) `(&target [ESP+0x28],
	// owner slot 217 (+0x364)(), bNoisy = 1, 0)`, EVERY pass and unconditionally -- before the
	// capability test `0x1026de3a`, whether or not any arm reads the point. Being noisy it draws every
	// gather: an NPC enemy (`0x102789c0`) two `RandomFloat(0, 0.5)`, the player (`0x10174e60`) one
	// `RandomFloat(0.5, 1.0)`. Kept for slot 562's first test below.
	FVector WeaponLosTargetCm = Enemy->BodyTarget(Npc.GetAbsOrigin(), true, false);   // 0x1026de30
	const ECapability Capability = WeaponCapability(Npc);
	FElysiumWeapon* const Weapon = NpcCondActiveWeapon(Npc);                     // GetActiveWeapon 0x10007e19
	const FElysiumNpc* const Troika = Npc.AsNpc();
	// `m_flNextAttack (+0x1564)`, a combat-character word the port declares on the Troika leaf
	// (`ElysiumNpcConditionsBodies.inl`); a base-only body has none and answers 0. No writer yet.
	const double OwnerNextAttackTime = Troika != nullptr ? Troika->NextAttackTime : 0.0;

	// --- The ranged arm, `0x1026de3a..0x1026ded7` ---------------------------------------------------
	bool bRangedAnswered = false;
	int32 RangedAnswer = 0;
	if (((Caps & GGatherCapWeaponRange1) != 0 || Capability == ECapability::Ranged)   // 0x1026de3a caps & 0x2000
		&& Weapon != nullptr)                                                    // 0x1026de44 / 0x1026de4b
	{
		// `0x10252410(weapon, 0)`: true when `curtime >= weapon[+0x730]`; false -> 0x2f.
		if (!(Now >= Weapon->NextPrimaryAttackTime))                             // 0x1026de58 / 0x1026de5f
		{
			Out.Set(EElysiumNpcCond::WaitingAttackTime);                         // 0x1026de70 0x2f
		}
		// The weapon's slot 365 (`+0x5b4`, `CBaseCombatWeapon 0x1024f670`) `(enemy, dot, dist)`. The
		// item text's `Range` key feeds none of it.
		RangedAnswer = Weapon->RangeAttack1Conditions(Enemy, Dot, DistanceUnits, Now);   // 0x1026de87
		bRangedAnswered = true;
	}
	else if ((Caps & GGatherCapInnateRange1) != 0)                               // 0x1026de8f caps & 0x20000
	{
		if (Now < OwnerNextAttackTime)                                           // 0x1026dea1..0x1026deb1 +0x1564
		{
			Out.Set(EElysiumNpcCond::WaitingAttackTime);                         // 0x1026dec2 0x2f
		}
		RangedAnswer = Npc.RangeAttack1Conditions(Dot, DistanceUnits);           // 0x1026ded1 slot 553 (+0x8a4)
		bRangedAnswered = true;
	}
	// Neither bit: no ranged answer, straight to the melee arm (`0x1026de95 JZ 0x1026df6d`).

	if (bRangedAnswered)
	{
		if (RangedAnswer == static_cast<int32>(EElysiumNpcCond::CanRangeAttack1))   // 0x1026ded9 CMP EBP,0x4f
		{
			// `0x1026dede..0x1026df45`: slot 562 `WeaponLOSCondition` (`+0x8c8`) `(owner slot 217
			// (+0x364)(), &target, bSetConditions = 1)`, first to the enemy's slot 197 `BodyTarget`
			// point taken above (`0x1026de30`); on failure slot 560 AGAIN (`0x1026df00`) and a second
			// test to the enemy's slot 193 `EyePosition` (`+0x304`, `0x1026df0f`); either passing sets
			// 0x4f, both failing sets nothing (`0x1026df45 JZ 0x1026df69`).
			//
			// The slot's own bodies raise 0x66 / 0x63 / 0x42 / 0x64 on `Cognition.Conditions`, which is
			// `Out` (spec 0002 V5a-3; the eye's occlusion latch `+0x5b98` stood here before and is no
			// longer read by this body). The second clear is retail's and wipes whatever the first
			// test raised (0x63 and 0x2f included) before the second test raises its own.
			bool bWeaponLos =
				Npc.WeaponLOSCondition(Npc.GetAbsOrigin(), WeaponLosTargetCm, true);   // 0x1026dee4..0x1026def2 slot 562, first test
			if (!bWeaponLos)
			{
				Npc.ClearAttackConditions();                                     // 0x1026df00 slot 560 again
				WeaponLosTargetCm = Enemy->EyePosition();                        // 0x1026df0f slot 193 (+0x304) -> [ESP+0x28]
				bWeaponLos =
					Npc.WeaponLOSCondition(Npc.GetAbsOrigin(), WeaponLosTargetCm, true);   // 0x1026df3d slot 562, second test
			}
			if (bWeaponLos)
			{
				Out.Set(EElysiumNpcCond::CanRangeAttack1);                       // 0x1026df52 / 0x1026df64 0x4f
			}
		}
		else
		{
			// Every other answer, 0 included (`0x1026df61 PUSH EBP`: `SetCondition(COND_NONE)`).
			Out.Set(static_cast<EElysiumNpcCond>(RangedAnswer));                 // 0x1026df64
		}
	}

	// --- The melee arm, `0x1026df6d..0x1026dfcb`: always run AFTER the ranged one -------------------
	if (((Caps & GGatherCapWeaponMelee1) != 0 || Capability == ECapability::Melee)   // 0x1026df6d caps & 0x8000
		&& Weapon != nullptr)                                                    // 0x1026df77 / 0x1026df7e
	{
		// The weapon's slot 367 (`+0x5bc`) `(enemy, dot, dist)`. Two bodies fill it: `CWeaponMelee
		// 0x103eac30` -> `0x103ea7e0(0x4b, …)`, the band; and `CBaseCombatWeapon 0x1024f750`, whose
		// whole body is `return 0` (every other weapon class). The item record's melee family
		// stands for the `CWeaponMelee` line.
		int32 MeleeAnswer = 0;                                                   // 0x1024f750
		if (Capability == ECapability::Melee)
		{
			// `0x103ea950 GetSequencesForActivity(owner, translated activity, …)`: the wielder's
			// sequences. OWED by `ElysiumNpcBaseAnim.inl` / `ElysiumNpcAnim.cpp` (V5a-1's report): the
			// accessor translates the activity (weapon `+0x5a4`, owner `+0x5e0`) through the name-keyed
			// resolver and hands every clip of the body's vocabulary carrying the result.
			TArray<FElysiumNpcClip> Sequences;
			Npc.MeleeSequencesForActivity(GGatherActMeleeAttack1, Sequences);
			FMeleeWeaponBandQuery Query;
			Query.Owner = &Npc;                                                  // 0x103ea7f0 the weapon's owner
			Query.Weapon = Weapon;
			Query.WeaponNextPrimaryAttackTime = Weapon->NextPrimaryAttackTime;       // +0x730
			Query.WeaponNextSecondaryAttackTime = Weapon->NextSecondaryAttackTime;   // +0x734
			Query.OwnerNextAttackTime = OwnerNextAttackTime;                     // owner +0x1564
			Query.Target = Enemy;
			// `0x103ea81c` weapon `+0x5a4`, then `0x103ea827` owner `+0x5e0` (slot 376
			// `NPC_TranslateActivity`). SEAM: the weapon's half has no number-space body in this
			// runtime (`TranslateActivityNumber`'s seam; the ladder is name-keyed and lives in the
			// accessor above), so slot 331 is handed the owner's translation of the raw activity.
			Query.TranslatedActivity = Npc.NPC_TranslateActivity(GGatherActMeleeAttack1);
			Query.Dot = Dot;
			Query.DistUnits = DistanceUnits;
			Query.Now = Now;
			Query.Sequences = Sequences;
			MeleeAnswer = MeleeWeaponBand(Query);                                // 0x1026df9d -> 0x103eac30 -> 0x103ea7e0
		}
		Out.Set(static_cast<EElysiumNpcCond>(MeleeAnswer));                      // 0x1026dfcb
	}
	else if ((Caps & GGatherCapInnateMelee1) != 0)                               // 0x1026dfa5 caps & 0x80000
	{
		// Slot 555 `MeleeAttack1Conditions 0x1026d9a0` (`+0x8ac`) `(dot, dist)`.
		Out.Set(static_cast<EElysiumNpcCond>(Npc.MeleeAttack1Conditions(Dot, DistanceUnits)));   // 0x1026dfc2 / 0x1026dfcb
	}

	// --- The blocked-by-friend timers, `0x1026dfd0..0x1026e062` ------------------------------------
	if (Out.Has(EElysiumNpcCond::WeaponBlockedByFriend))                         // 0x1026dfd0 HasCondition(0x63)
	{
		if (Npc.ExtendedBlockedByFriendTimer == GGatherFltMax)                   // 0x1026dfdd +0x5b8c == FLT_MAX
		{
			Npc.ExtendedBlockedByFriendTimer =
				Now + static_cast<double>(ElysiumNpcTunables::TwoAndHalf);       // 0x1026dff2 _DAT_104629ec 2.5
		}
		Npc.WeaponBlockedByFriendTimer =
			Now + static_cast<double>(ElysiumNpcTunables::OneAndHalf);           // 0x1026e006 _DAT_1044f02c 1.5
	}
	else if (!(Now < Npc.WeaponBlockedByFriendTimer))                            // 0x1026e01a..0x1026e02a +0x5b88 <= curtime
	{
		Npc.ExtendedBlockedByFriendTimer = GGatherFltMax;                        // 0x1026e02c
	}
	if (Now > Npc.ExtendedBlockedByFriendTimer)                                  // 0x1026e03c..0x1026e04c +0x5b8c < curtime
	{
		Out.Set(EElysiumNpcCond::ExtendedBlockedByFriend);                       // 0x1026e05d 0x2e
	}

	// --- The tail, `0x1026e062..0x1026e107` ---------------------------------------------------------
	if (Now < Npc.WeaponBlockedByFriendTimer)                                    // 0x1026e068..0x1026e076 the block still held
	{
		Out.Set(EElysiumNpcCond::WeaponBlockedByFriend);                         // 0x1026e087 0x63
		Out.Clear(EElysiumNpcCond::CanRangeAttack2);                             // 0x1026e090 0x50
		Out.Clear(EElysiumNpcCond::CanRangeAttack1);                             // 0x1026e099 0x4f
		Out.Clear(EElysiumNpcCond::CanMeleeAttack2);                             // 0x1026e0a2 0x52
		Out.Clear(EElysiumNpcCond::CanMeleeAttack1);                             // 0x1026e107 0x51
	}
	else if (Out.Has(EElysiumNpcCond::CanRangeAttack2)                           // 0x1026e0ab 0x50
		|| Out.Has(EElysiumNpcCond::CanRangeAttack1)                             // 0x1026e0b8 0x4f
		|| Out.Has(EElysiumNpcCond::CanMeleeAttack2)                             // 0x1026e0c5 0x52
		|| Out.Has(EElysiumNpcCond::CanMeleeAttack1))                            // 0x1026e0d2 0x51
	{
		// An attack is open: the band words of this and of every earlier gather go.
		Out.Clear(EElysiumNpcCond::TooCloseForRanged);                           // 0x1026e0e3 0x08
		Out.Clear(EElysiumNpcCond::TooCloseToAttack);                            // 0x1026e0ec 0x5f
		Out.Clear(EElysiumNpcCond::TooFarToAttack);                              // 0x1026e0f5 0x60
		Out.Clear(EElysiumNpcCond::TooFarForMelee);                              // 0x1026e0fe 0x09
		Out.Clear(EElysiumNpcCond::WeaponBlockedByFriend);                       // 0x1026e107 0x63
	}
	// None standing: nothing (`0x1026e0dd JZ 0x1026e10c`).
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
