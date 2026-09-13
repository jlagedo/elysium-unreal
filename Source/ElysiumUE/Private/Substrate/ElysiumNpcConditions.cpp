#include "Substrate/ElysiumNpcConditions.h"

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
			const EElysiumNpcCond Cond =
				static_cast<EElysiumNpcCond>((WordIndex << 6) + static_cast<int32>(BitIndex));
			if (!Out.IsEmpty())
			{
				Out.AppendChar(TEXT('|'));
			}
			Out.Append(ElysiumNpcCondName(Cond));
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

void ElysiumNpcCond::AccumulateDamage(FElysiumNpcMemory& Memory, int32 CommittedDamage, double Now)
{
	if (CommittedDamage <= 0)
	{
		return;
	}
	// Recovered: the window is RESET when it expires, not decayed — a hit one second and one frame
	// after the previous one starts a fresh window carrying only itself.
	if (Memory.RepeatedDamageWindowStart < 0.0
		|| (Now - Memory.RepeatedDamageWindowStart) > RepeatedDamageWindowSeconds)
	{
		Memory.RepeatedDamageWindowStart = Now;
		Memory.RepeatedDamageAccumulated = 0;
	}
	Memory.RepeatedDamageAccumulated += CommittedDamage;
}

void ElysiumNpcCond::GatherBump(const FElysiumNpc& Npc, double PreviousGatherTime,
	FElysiumNpcConditions& Out)
{
	// The same one-pass reconstruction `GatherDamage` uses below, against the same clock. Retail
	// raises the bit inside the player's touch (`0x10147690`) and clears it at the end of the next
	// non-reduced `RunAI`; this runtime rebuilds the whole set each full pass, so the life is
	// expressed as "the bump is newer than the last pass that read one".
	const double LastBump = Npc.Senses.Memory.LastBumpTime;
	if (LastBump >= 0.0 && LastBump > PreviousGatherTime)
	{
		Out.Set(EElysiumNpcCond::WasBumped);
	}
}

void ElysiumNpcCond::GatherDamage(const FElysiumNpc& Npc, double PreviousGatherTime,
	FElysiumNpcConditions& Out)
{
	const FElysiumNpcMemory& Memory = Npc.Senses.Memory;

	// The edge: retail sets these inside the damage transaction and clears them when the decision
	// pass that read them ends, so a packet is decision input exactly once. This runtime has no
	// mid-pass hook to set a bit in, so the same one-pass life is reconstructed from the commit's
	// own timestamp against the previous pass's. That is a mechanism difference with no behavioural
	// one: the condition is live for the first pass after the hit and gone for the second.
	const bool bNewPacket = Memory.LastDamageTime >= 0.0 && Memory.LastDamageTime > PreviousGatherTime;
	if (bNewPacket && Memory.LastDamageAmount > 0)
	{
		Out.Set(EElysiumNpcCond::LightDamage);
		// See `HeavyDamageFraction` — the predicate itself is unrecovered. A body with no Source
		// health ceiling (a record that never seeded a sheet) cannot answer the question at all, so
		// it takes light damage only rather than guessing a pool.
		if (Npc.MaxHealth > 0
			&& static_cast<float>(Memory.LastDamageAmount)
				>= HeavyDamageFraction * static_cast<float>(Npc.MaxHealth))
		{
			Out.Set(EElysiumNpcCond::HeavyDamage);
		}
	}

	// `REPEATED_DAMAGE` is the window's own answer, not an edge: while the accumulated sum inside a
	// live one-second window is over 15 percent of Source max health the condition stands.
	if (Npc.MaxHealth > 0 && Memory.RepeatedDamageWindowStart >= 0.0
		&& Memory.LastDamageTime >= 0.0
		&& (Memory.LastDamageTime - Memory.RepeatedDamageWindowStart) <= RepeatedDamageWindowSeconds
		&& static_cast<float>(Memory.RepeatedDamageAccumulated)
			> RepeatedDamageFraction * static_cast<float>(Npc.MaxHealth))
	{
		Out.Set(EElysiumNpcCond::RepeatedDamage);
	}
}

void ElysiumNpcCond::GatherHearing(const FElysiumNpc& Npc, double PreviousGatherTime,
	FElysiumNpcConditions& Out)
{
	if (!Npc.IsOblivious()) Out |= Npc.Senses.HeardConditions;
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
	if (Npc.Senses.Memory.Enemy.IsSet() && Npc.Senses.Memory.Enemy == Candidate.Handle)
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
		// Troika's `OnLooked` classifies D_HT by its raw IRelationPriority: negative is DISLIKE,
		// 0..10 HATE, and 11+ NEMESIS.  The relationship store deliberately retains D_HT as its
		// disposition, so this is the one place the priority expands it into the three conditions.
		// Only actual HATE and FEAR take the base `UpdateEnemyMemory` write.
		const int32 Priority = Npc.Relationships.ResolvePriority(Handle, NpcCondClassnameOf(*Target));
		FElysiumNpcMemory::ESeen Slot = FElysiumNpcMemory::ESeen::Count;
		switch (Relation)
		{
		case EElysiumRelationship::Hate:
			// 0x1026a3e0 diverts D_HT under D_CALM into an arm that rejects D_CALM.
			if (Npc.NpcFlags.Has(EElysiumNpcFlag2::D_CALM)) break;
			if (Priority < 0)
			{
				Out.Set(EElysiumNpcCond::SeeDislike);
				Slot = FElysiumNpcMemory::ESeen::Dislike;
			}
			else if (Priority <= 10)
			{
				Out.Set(EElysiumNpcCond::SeeHate);
				Slot = FElysiumNpcMemory::ESeen::Hate;
			}
			else
			{
				Out.Set(EElysiumNpcCond::SeeNemesis);
				Slot = FElysiumNpcMemory::ESeen::Nemesis;
			}
			break;
		case EElysiumRelationship::Fear:
			if (Npc.NpcFlags.Has(EElysiumNpcFlag2::D_CALM)) break;
			Out.Set(EElysiumNpcCond::SeeFear);
			Slot = FElysiumNpcMemory::ESeen::Fear;
			break;
		default:
			break;   // D_LI and D_NU raise no sight condition
		}
		if (Slot != FElysiumNpcMemory::ESeen::Count)
		{
			Memory.LastSeen[static_cast<int32>(Slot)] = Handle;
			Memory.LastSeenTime[static_cast<int32>(Slot)] = Now;
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
	// Retail's outer `GatherConditions` (`0x102b27f0`) clears every condition this sweep owns before
	// the sense pass runs; `Cond.Reset()` at the top of `ElysiumNpcEnemy::GatherConditions` already
	// does that for the whole pass, so nothing here needs its own unconditional clear except the
	// mid-function retraction below, which retail performs inside this very function.
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
			Npc.Schedule.bTaskCompletedExternally = true;
		}
	}

	// `GetScheduleId(m_pSchedule->id)` through slot 447 compared against `SCHED_TROIKA_COMFORT`.
	bool IsRunningComfortSchedule(const FElysiumNpc& Npc)
	{
		return Npc.Schedule.IsRunning()
			&& ElysiumScheduleNumber(Npc.Schedule.Current) == ElysiumNpcCond::ComfortScheduleNumber;
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
			if (const FElysiumNpc* ComforterNpc = Nearest->AsNpc();
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
		const FElysiumEntity* FlankEnemy = World ? ResolveEnemyHandle(*World, Memory.Enemy) : nullptr;
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
	const FElysiumEntity* Source = World ? ResolveEnemyHandle(*World, Memory.BestSoundSource) : nullptr;

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
	const FElysiumEntity* Enemy = World ? ResolveEnemyHandle(*World, Memory.Enemy) : nullptr;

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
	const auto LastSeen = [&](FElysiumNpcMemory::ESeen Slot) -> const FElysiumEntity*
	{
		return World ? ResolveEnemyHandle(*World, Memory.Seen(Slot)) : nullptr;
	};
	const FSourceArm Chain[] = {
		{ ClosestPlayer,                                    EElysiumNpcCond::SeePlayer  },
		{ Enemy,                                            EElysiumNpcCond::SeeEnemy   },
		{ LastSeen(FElysiumNpcMemory::ESeen::Hate),         EElysiumNpcCond::SeeHate    },
		{ LastSeen(FElysiumNpcMemory::ESeen::Fear),         EElysiumNpcCond::SeeFear    },
		{ LastSeen(FElysiumNpcMemory::ESeen::Dislike),      EElysiumNpcCond::SeeDislike },
		{ LastSeen(FElysiumNpcMemory::ESeen::Nemesis),      EElysiumNpcCond::SeeNemesis },
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

void ElysiumNpcCond::GatherCommittedEnemy(const FElysiumNpc& Npc, FElysiumNpcConditions& Out)
{
	const FElysiumEntityWorld* World = Npc.World;
	const FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	if (World == nullptr || !Memory.Enemy.IsSet())
	{
		return;
	}
	const FElysiumEntity* Enemy = ResolveEnemyHandle(*World, Memory.Enemy);
	if (Enemy == nullptr)
	{
		// The handle is gone entirely rather than dead. That is `LOST_ENEMY`, and it belongs to the
		// enemy transaction that ran before this step — asserting anything about a committed enemy
		// that is not there is exactly the plausible fallback the recovered gate refuses.
		return;
	}
	if (Enemy->IsInert())
	{
		// Dead, or hidden — the two collapse here for the same reason the senses debounce collapses
		// them: an entity the world has taken off the board is not fightable, and VtMB carries no
		// hidden-NPC state distinct from removed. The senses debounce deliberately declines to
		// infer this (it is enemy SELECTION's transaction), which is why it lands here.
		Out.Set(EElysiumNpcCond::EnemyDead);
		return;
	}

	// The debounce's own two answers, straight off the latch senses maintains: below ten
	// consecutive failures the committed enemy retains `HAVE_ENEMY_LOS`; at ten it flips.
	Out.Set(Memory.bEnemyOccluded ? EElysiumNpcCond::EnemyOccluded : EElysiumNpcCond::HaveEnemyLos);

	// `SEE_ENEMY` is the fresh Look admission answer, never closest-player cache replay.
	if (Npc.Senses.Sighted().Contains(Memory.Enemy))
	{
		Out.Set(EElysiumNpcCond::SeeEnemy);
	}

	// SEAM (comment only, never set): `ENEMY_UNREACHABLE` (0x59). It needs a path query — "can this
	// body reach that actor" — and `IElysiumNpcMotor` carries no reachability verb yet. The door
	// obstruction selector's step 2 and the melee selector's take-cover branch both gate on it, and
	// both currently decline for exactly this reason. Never set a plausible default here: an
	// unreachable enemy the NPC can in fact reach re-routes the whole combat branch.
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
	bool NpcCondFacesTarget(const FElysiumNpc& Npc, const FVector& TargetCm)
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

	// Does the mode this press would use still have a round to spend, counting the reserve?
	// `NO_PRIMARY_AMMO` is EMPTY MAGAZINE AND EMPTY RESERVE: a magazine that can be refilled is a
	// reload, not an ammunition failure, and the two select different schedules.
	bool NpcCondOutOfAmmo(const FElysiumNpc& Npc, const FElysiumWeapon& Weapon,
		const FElysiumWeaponMode& Mode)
	{
		if (Mode.AmmoCost <= 0)
		{
			return false;   // a mode that spends nothing can never be out
		}
		if (Weapon.MagazineCount >= Mode.AmmoCost)
		{
			return false;
		}
		const FElysiumItemDef* Record = Weapon.Data();
		const FString& AmmoType = Record != nullptr ? Record->AmmoType : Mode.AmmoType;
		return Npc.Inventory.Reserve(AmmoType) < Mode.AmmoCost;
	}
}

void ElysiumNpcCond::GatherAttackConditions(const FElysiumNpc& Npc, double Now,
	FElysiumNpcConditions& Out)
{
	const FElysiumEntityWorld* World = Npc.World;
	const FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	if (World == nullptr || !Memory.Enemy.IsSet())
	{
		return;
	}
	const FElysiumEntity* Enemy = ResolveEnemyHandle(*World, Memory.Enemy);
	if (Enemy == nullptr || Enemy->IsInert())
	{
		// `ENEMY_DEAD` / `LOST_ENEMY` already describe this; range against a corpse is not a fact.
		return;
	}

	// SEAM (plumbed, never set): `SHOULD_DODGE` (0x0c), `SHOULD_BLOCK` (0x0d), `SHOULD_STEPBACK`
	// (0x0e) and `SHOULD_KICK` (0x0f). The NOTICE that would feed them is real and lands below
	// (`NoticeMeleeAttack` writes the attacker into memory with the recovered five-second life), but
	// the policy turning a noticed incoming attack into ONE of these four is not decoded — nothing in
	// the survey names the ratings, timers or randomisation that choose between dodging, blocking,
	// kicking and stepping back. Raising any of them from the notice alone would make every NPC
	// dodge, which is a behaviour, not a gap. The melee selector's four branches exist and are
	// driven by injection in `Elysium.Substrate.NpcCombat.MeleeSelectorOrder`.

	const FElysiumWeapon* Weapon = NpcCondActiveWeapon(Npc);
	const FElysiumWeaponMode* Mode = Weapon != nullptr
		? Weapon->ModeFor(FElysiumWeapon::EIntent::Primary) : nullptr;

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

	// --- The ranged bands -------------------------------------------------------------------------
	if (Weapon != nullptr && Mode != nullptr && NpcCondOutOfAmmo(Npc, *Weapon, *Mode))
	{
		Out.Set(EElysiumNpcCond::NoPrimaryAmmo);
	}

	// The far edge is the mode's own authored `Range`. A mode that authors none cannot answer, so
	// the NPC is never too far for it — inventing a default range would silently make every
	// unranged mode a chase trigger.
	const double RangeCm = Mode != nullptr
		? static_cast<double>(Mode->Range) * ElysiumMove::U : 0.0;
	if (RangeCm > 0.0 && DistanceCm > RangeCm)
	{
		Out.Set(EElysiumNpcCond::TooFarToAttack);
	}
	// CHOSEN, NOT RECOVERED: the NEAR edge. `TOO_CLOSE_TO_ATTACK` (0x5f) is a decoded condition with
	// no decoded threshold — the ranged selector tests it and no recovered body says what makes a
	// shot too close. The melee reach is taken as that edge, because an enemy already inside this
	// NPC's own swing distance is the case the condition's consumers (back off, run away) describe.
	const bool bTooClose = DistanceCm < MeleeReachCm;
	if (bTooClose)
	{
		Out.Set(EElysiumNpcCond::TooCloseToAttack);
	}

	// The line-of-FIRE occlusion arm, taken from the eye's own debounce latch — the same term
	// `GatherCommittedEnemy` reports as `ENEMY_OCCLUDED`. CHOSEN, NOT RECOVERED: retail traces from
	// the weapon, and this runtime has one visibility query and one latch, so the two conditions
	// agree here where retail's could disagree. What is NOT done is the converse — see the
	// `WEAPON_THROUGH_WALL` seam in this function's declaration.
	if (Memory.bEnemyOccluded)
	{
		Out.Set(EElysiumNpcCond::WeaponSightOccluded);
	}

	const bool bHasAmmo = Weapon != nullptr && Mode != nullptr
		&& (Mode->AmmoCost <= 0 || Weapon->MagazineCount >= Mode->AmmoCost);
	if (bReady && bHasAmmo && !bTooClose && !Memory.bEnemyOccluded
		&& !Out.Has(EElysiumNpcCond::TooFarToAttack))
	{
		Out.Set(EElysiumNpcCond::CanRangeAttack1);
	}
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

// --- Ideal state — the two layers, kept two ---

namespace
{
	bool NpcCondHasAnyHear(const FElysiumNpcConditions& Cond)
	{
		return Cond.Has(EElysiumNpcCond::HearCombat) || Cond.Has(EElysiumNpcCond::HearPlayer)
			|| Cond.Has(EElysiumNpcCond::HearWorld) || Cond.Has(EElysiumNpcCond::HearDanger);
	}

	bool NpcCondHasDamage(const FElysiumNpcConditions& Cond)
	{
		return Cond.Has(EElysiumNpcCond::LightDamage) || Cond.Has(EElysiumNpcCond::HeavyDamage);
	}
}

EElysiumNpcState ElysiumNpcCond::SelectIdealStateBase(const FIdealStateInput& In,
	const FElysiumNpcConditions& Cond, bool& bOutCombatWithoutEnemy)
{
	bOutCombatWithoutEnemy = false;
	switch (In.Current)
	{
	case EElysiumNpcState::Idle:
		// CHOSEN, NOT RECOVERED (the promotion, not the fallback): the survey states the escalation
		// "idle to alert to combat" and that the concrete human selectors enter their weapon/combat
		// policy only for state 2, but no recovered body names the exact idle->combat test. A
		// committed enemy is taken as that test, because case 2's own recovered fallback proves the
		// converse — combat without an enemy is the error case, so an enemy is what combat means.
		if (In.bHasEnemy)
		{
			return EElysiumNpcState::Combat;
		}
		// Recovered, and load-bearing: `case 1` promotes idle -> alert on `COND_LIGHT_DAMAGE`,
		// `COND_HEAVY_DAMAGE` and the whole hear family with NO `m_bNoAlertState` test.
		if (NpcCondHasDamage(Cond) || NpcCondHasAnyHear(Cond))
		{
			return EElysiumNpcState::Alert;
		}
		return EElysiumNpcState::Idle;

	case EElysiumNpcState::Alert:
		if (In.bHasEnemy)
		{
			return EElysiumNpcState::Combat;
		}
		// SEAM (comment only): retail's alert -> idle relaxation is a timeout on the last state
		// change, and neither the member nor its interval is recovered. An NPC therefore stays alert
		// once promoted rather than decaying on an invented clock; the alert schedule it runs is an
		// ordinary lookaround, so the visible cost of holding the state is nil.
		return EElysiumNpcState::Alert;

	case EElysiumNpcState::Combat:
		if (!In.bHasEnemy)
		{
			// Recovered verbatim: `case 2` emits `Combat state with no enemy` and falls back to
			// state 3. The emission is reported rather than logged here so the entity name is in
			// hand at the log site.
			bOutCombatWithoutEnemy = true;
			return EElysiumNpcState::Alert;
		}
		return EElysiumNpcState::Combat;

	default:
		// Scripted, Prone and Dead are owned by their own transactions; the ideal-state pass leaves
		// them alone rather than competing with the body arbiter or the death path.
		return In.Current;
	}
}

EElysiumNpcState ElysiumNpcCond::SelectIdealState(const FIdealStateInput& In,
	const FElysiumNpcConditions& Cond, bool& bOutCombatWithoutEnemy)
{
	// The Troika layer's OWN damage and sense promotions. `no_alert_state` skips these — and only
	// these.
	if (!In.bNoAlertState && In.Current == EElysiumNpcState::Idle && !In.bHasEnemy
		&& (NpcCondHasDamage(Cond) || NpcCondHasAnyHear(Cond)))
	{
		FIdealStateInput Promoted = In;
		Promoted.Current = EElysiumNpcState::Alert;
		// Even having promoted, the body still ends in the unconditional base tail call below —
		// which is why this is written as a rewrite of the input rather than an early return.
		return SelectIdealStateBase(Promoted, Cond, bOutCombatWithoutEnemy);
	}
	// The unconditional tail: `return CAI_BaseNPC::SelectIdealState(this)`. Under `no_alert_state`
	// this is the ONLY layer that runs, and it promotes anyway.
	return SelectIdealStateBase(In, Cond, bOutCombatWithoutEnemy);
}
