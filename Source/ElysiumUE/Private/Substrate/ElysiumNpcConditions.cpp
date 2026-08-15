#include "Substrate/ElysiumNpcConditions.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"

namespace
{
	// The seen set for one decision pass.
	//
	// SCOPE, DELIBERATE: cycle 4's sight path tracks exactly one candidate — the player
	// (`FElysiumNpcSenses::TickSight` resolves `SetClosestPlayer` and nothing else), because that is
	// the only observer transaction the stealth recovery closes. NPC-vs-NPC sight has no recovered
	// admission rule here yet, so this runtime does not invent one. Everything downstream of this
	// function already takes a LIST of handles and joins each through the relationship table, so
	// admitting other observers later is an addition HERE and nowhere else.
	void NpcCondBuildSeenSet(const FElysiumNpc& Npc, TArray<FElysiumEntityHandle>& Out)
	{
		const FElysiumNpcMemory& Memory = Npc.Senses.Memory;
		if (Memory.bPlayerLos && Memory.ClosestPlayer.IsSet())
		{
			Out.Add(Memory.ClosestPlayer);
		}
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
	case EElysiumNpcCond::ShouldDodge:           return TEXT("SHOULD_DODGE");
	case EElysiumNpcCond::ShouldBlock:           return TEXT("SHOULD_BLOCK");
	case EElysiumNpcCond::ShouldStepback:        return TEXT("SHOULD_STEPBACK");
	case EElysiumNpcCond::ShouldKick:            return TEXT("SHOULD_KICK");
	case EElysiumNpcCond::Knockback:             return TEXT("KNOCKBACK");
	case EElysiumNpcCond::WaitingAttackTime:     return TEXT("WAITING_ATTACK_TIME");
	case EElysiumNpcCond::HitByDoor:             return TEXT("HIT_BY_DOOR");
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

// ================================================================================================
// Producers
// ================================================================================================

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
		|| Cond == EElysiumNpcCond::HearDanger;
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
	const FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	if (Memory.LastHeardTime < 0.0 || Memory.LastHeardTime <= PreviousGatherTime)
	{
		return;
	}
	// SEAM (comment only, never set): `HEAR_DANGER` is named in the interrupt census (54 schedules)
	// but nothing in the recovered sound material says which categories raise it, and the volume
	// table carries no danger level. An invented mapping would silently interrupt 54 schedules.
	if (IsCombatSoundCategory(Memory.LastHeardCategory.ToLower()))
	{
		Out.Set(EElysiumNpcCond::HearCombat);
		return;
	}
	const FElysiumEntityHandle PlayerHandle = Npc.World
		? Npc.World->PlayerHandle() : FElysiumEntityHandle::Invalid();
	if (Memory.LastHeardSource.IsSet() && PlayerHandle.IsSet() && Memory.LastHeardSource == PlayerHandle)
	{
		Out.Set(EElysiumNpcCond::HearPlayer);
		return;
	}
	Out.Set(EElysiumNpcCond::HearWorld);
}

void ElysiumNpcCond::GatherSight(FElysiumNpc& Npc, double Now, FElysiumNpcConditions& Out)
{
	FElysiumEntityWorld* World = Npc.World;
	if (World == nullptr)
	{
		return;
	}
	TArray<FElysiumEntityHandle> Seen;
	NpcCondBuildSeenSet(Npc, Seen);

	FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	for (const FElysiumEntityHandle& Handle : Seen)
	{
		const FElysiumEntity* Target = World->Resolve(Handle);
		if (Target == nullptr || Target->IsInert() || Handle == Npc.Handle)
		{
			continue;
		}
		const EElysiumRelationship Relation = Npc.Relationships.Resolve(Handle, NpcCondClassnameOf(*Target));
		// SEAM (comment only, never set): `SEE_DISLIKE` (0x43's neighbour, 0x45) and `SEE_NEMESIS`
		// (0x5b) have no producer, because the recovered `D_*` token set is exactly
		// hate/fear/like/neutral — neither "dislike" nor "nemesis" is a relation this table can
		// hold. Whatever raises them is a second classification the survey has not decoded, so the
		// two conditions are gathered by nobody and their last-seen memory slots stay unwritten.
		// `ShouldChooseNewEnemy` still tests both, exactly as retail does.
		FElysiumNpcMemory::ESeen Slot = FElysiumNpcMemory::ESeen::Count;
		switch (Relation)
		{
		case EElysiumRelationship::Hate:
			Out.Set(EElysiumNpcCond::SeeHate);
			Slot = FElysiumNpcMemory::ESeen::Hate;
			break;
		case EElysiumRelationship::Fear:
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
		}
	}
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
		// Dead, or hidden — the two collapse here for the same reason cycle 4's debounce collapses
		// them: an entity the world has taken off the board is not fightable, and VtMB carries no
		// hidden-NPC state distinct from removed. The senses debounce deliberately declines to
		// infer this (it is enemy SELECTION's transaction), which is why it lands here.
		Out.Set(EElysiumNpcCond::EnemyDead);
		return;
	}

	// The debounce's own two answers, straight off the latch cycle 4 maintains: below ten
	// consecutive failures the committed enemy retains `HAVE_ENEMY_LOS`; at ten it flips.
	Out.Set(Memory.bEnemyOccluded ? EElysiumNpcCond::EnemyOccluded : EElysiumNpcCond::HaveEnemyLos);

	// `SEE_ENEMY` is the ADMISSION answer rather than the tracking one: it stands when the committed
	// enemy is in this pass's seen set. Today that set holds only the player (`NpcCondBuildSeenSet`),
	// so an NPC enemy never raises it — the same scope mark, one function up.
	if (Memory.Enemy == World->PlayerHandle() && Memory.bPlayerLos)
	{
		Out.Set(EElysiumNpcCond::SeeEnemy);
	}

	// SEAM (comment only, never set): `ENEMY_UNREACHABLE` (0x59). It needs a path query — "can this
	// body reach that actor" — and `IElysiumNpcMotor` carries no reachability verb yet. The door
	// obstruction selector's step 2 and the melee selector's take-cover branch both gate on it, and
	// both currently decline for exactly this reason. Never set a plausible default here: an
	// unreachable enemy the NPC can in fact reach re-routes the whole combat branch.
}

// ================================================================================================
// Ideal state — the two layers, kept two
// ================================================================================================

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
