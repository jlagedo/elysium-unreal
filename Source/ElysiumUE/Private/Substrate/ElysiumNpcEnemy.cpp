#include "Substrate/ElysiumNpcEnemy.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Debug/ElysiumNpcDebugLogging.h"

namespace
{
	FString NpcEnemyClassnameOf(const FElysiumEntity& Entity)
	{
		return Entity.Def ? Entity.Def->Classname : FString();
	}

	// One candidate, scored once. Everything the arbitration compares is resolved up front so the
	// comparison itself is the recovered rule and nothing else.
	struct FNpcEnemyCandidate
	{
		FElysiumEntityHandle Handle;
		int32 Priority = 0;
		int32 DistanceUnits = 0;   // truncated squared Source distance, as retail compares it
		bool bVisible = false;
		bool bReachable = true;
	};

	// CHOSEN, NOT RECOVERED (the value, not the rule): reachability. Step 1 of the recovered
	// arbitration is "a reachable candidate beats an unreachable candidate", and this runtime has no
	// reachability query — `IElysiumNpcMotor` carries projection and movement but no "can this body
	// path to that actor". Every candidate therefore answers reachable, which collapses step 1 to a
	// no-op rather than removing it: the comparison below still runs it, so the day a path query
	// lands it is one function that changes. Reporting "unreachable" instead would be worse — it
	// would make every candidate equal at step 1 AND set `ENEMY_UNREACHABLE`'s downstream branches
	// off a value nothing measured.
	bool NpcEnemyIsReachable(const FElysiumNpc&, const FElysiumEntity&)
	{
		return true;
	}

	// Is this candidate currently seen? The sight path tracks only the player, so any other
	// candidate is unseen — the same deliberate scope as `ElysiumNpcConditions.cpp`'s seen set, and
	// the reason step 4 of the arbitration is exercisable at all today.
	bool NpcEnemyIsCandidateVisible(const FElysiumNpc& Npc, const FElysiumEntityHandle& Handle)
	{
		// BestEnemy's visibility tie-break is this Look pass's actual seen set, not the two-second
		// closest-player cache. That also makes NPC/object candidates follow the same admission.
		const FElysiumEntity* Candidate = Npc.World ? Npc.World->Resolve(Handle) : nullptr;
		return Npc.Senses.Sighted().Contains(Handle)
			|| (Candidate && FElysiumNpcSenses::IsVisible(Npc, *Candidate, Npc.World->NowSeconds()));
	}

	// The recovered arbitration, as a strict "does Candidate displace Incumbent".
	bool NpcEnemyDisplaces(const FNpcEnemyCandidate& Candidate, const FNpcEnemyCandidate& Incumbent)
	{
		// 1. Reachability class.
		if (Candidate.bReachable != Incumbent.bReachable)
		{
			return Candidate.bReachable;
		}
		// 2. Larger IRelationPriority.
		if (Candidate.Priority != Incumbent.Priority)
		{
			return Candidate.Priority > Incumbent.Priority;
		}
		// 4. Visibility modifies the distance comparison. A visible candidate can displace a farther
		//    UNSEEN incumbent; a closer unseen candidate displaces only an unseen incumbent.
		if (Candidate.bVisible != Incumbent.bVisible)
		{
			// At equal priority/reachability a visible candidate displaces an unseen incumbent even
			// when it is farther; an unseen candidate never displaces a visible incumbent.
			return Candidate.bVisible;
		}
		// 3. At equal priority and the same visibility class, smaller integer distance wins. Ties go
		//    to the incumbent, which is insertion order — the first eligible entity in world order
		//    holds the slot.
		return Candidate.DistanceUnits < Incumbent.DistanceUnits;
	}
}

int32 ElysiumNpcEnemy::RelationPriority(const FElysiumNpc& Npc, const FElysiumEntity& Candidate)
{
	return Npc.Relationships.ResolvePriority(Candidate.Handle, NpcEnemyClassnameOf(Candidate));
}

bool ElysiumNpcEnemy::RememberDamage(FElysiumNpc& Npc, const FElysiumDmg& Dmg, double Now)
{
	FElysiumEntityWorld* World = Npc.World;
	const FElysiumEntity* Attacker = World ? World->Resolve(Dmg.Source) : nullptr;
	if (Attacker == nullptr || Attacker->AsCombatCharacter() == nullptr || Attacker->Handle == Npc.Handle)
	{
		return false;
	}
	FElysiumEntity* Inflictor = World->Resolve(Dmg.Inflictor);
	FElysiumItem* HeldItem = Inflictor ? Inflictor->AsItem() : nullptr;
	FElysiumWeapon* HeldWeapon = HeldItem ? HeldItem->AsWeapon() : nullptr;
	FVector HeldPosition = FVector::ZeroVector;
	const bool bHeldPosition = HeldWeapon && HeldWeapon->HeldSourcePosition(HeldPosition);
	const bool bGenericInflictorPosition = Inflictor != nullptr && HeldWeapon == nullptr
		&& !Inflictor->IsInert();
	const bool bHasPosition = Dmg.bHasAttackPosition || bHeldPosition || bGenericInflictorPosition;
	if (!bHasPosition)
	{
		return false; // identity is known, but held/projectile position is not yet available
	}
	// Weapon_Equip -> weapon slot 298 sets MoveType FOLLOW, aim/owner = character; PhysicsFollow
	// copies the aim entity's absolute origin with zero offset. This is a held-item rule only.
	const FVector AttackPosition = bHeldPosition ? HeldPosition
		: (bGenericInflictorPosition ? Inflictor->Origin : Dmg.AttackPosition);
	const bool bVisible = FElysiumNpcSenses::IsInViewCone(Npc, Attacker->Origin)
		&& (World->Embodiment() == nullptr
			|| World->Embodiment()->QueryLineOfSight(Npc.EyePosition(), Attacker->EyePosition()));
	if (bVisible)
	{
		return false;
	}
	const FElysiumEntityHandle Current = Npc.Senses.Memory.Enemy;
	const FElysiumEntity* CurrentEntity = World->Resolve(Current);
	if (CurrentEntity != nullptr && Npc.EnemyMemory.Find(Dmg.Source) == nullptr
		&& !Npc.Cognition.Conditions.Has(EElysiumNpcCond::SeeEnemy))
	{
		Npc.EnemyMemory.UpdateAtPosition(Npc, Current, AttackPosition, Now);
		return true;
	}
	if (Npc.EnemyMemory.Find(Dmg.Source) != nullptr)
	{
		Npc.EnemyMemory.UpdateAtPosition(Npc, Dmg.Source, AttackPosition, Now);
	}
	else
	{
		Npc.EnemyMemory.UpdatePositionOnly(AttackPosition, Now);
	}
	return true;
}

bool ElysiumNpcEnemy::ShouldChooseNewEnemy(const FElysiumNpc& Npc, const FElysiumNpcConditions& Cond)
{
	const FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	if (!Memory.Enemy.IsSet() || Npc.World == nullptr)
	{
		return true;
	}
	const FElysiumEntity* Enemy =
		ElysiumNpcCond::ResolveEnemyHandle(*Npc.World, Memory.Enemy);
	if (Enemy == nullptr || Enemy->IsInert())
	{
		return true;   // the actor is dead, or gone; this is the same pass that noticed it
	}
	if (Npc.EnemyMemory.IsEluded(Memory.Enemy))
	{
		return true;
	}
	// `SEE_FEAR` is deliberately absent — see the header.
	return Cond.Has(EElysiumNpcCond::SeeHate)
		|| Cond.Has(EElysiumNpcCond::SeeDislike)
		|| Cond.Has(EElysiumNpcCond::SeeNemesis)
		|| Cond.Has(EElysiumNpcCond::EnemyDead);
}

EElysiumNpcCond ElysiumNpcEnemy::RequiredInterrupt(const FElysiumNpc& Npc)
{
	const FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	if (Memory.Enemy.IsSet() && Npc.World != nullptr)
	{
		const FElysiumEntity* Enemy =
			ElysiumNpcCond::ResolveEnemyHandle(*Npc.World, Memory.Enemy);
		if (Enemy == nullptr || Npc.EnemyMemory.IsEluded(Memory.Enemy))
		{
			return EElysiumNpcCond::LostEnemy;   // eluded, or the handle went null
		}
		if (Enemy->IsInert())
		{
			return EElysiumNpcCond::EnemyDead;   // still an actor, just not a fightable one
		}
	}
	return EElysiumNpcCond::NewEnemy;   // ordinary replacement, and the no-enemy-yet case
}

bool ElysiumNpcEnemy::IsScheduleInterested(const FElysiumNpc& Npc, EElysiumNpcCond Required)
{
	if (!Npc.Schedule.IsRunning())
	{
		// Recovered from ChooseEnemy (0x10279dd0): when the active-schedule pointer is null, it
		// seeds all three interrupt answers true before the replacement predicates run. The port's
		// spawn/idle gap is therefore interested, rather than a permissive fallback invented to
		// compensate for missing installation.
		return true;
	}
	const FElysiumSchedule* Active = ElysiumScheduleFor(Npc.Schedule.Current);
	if (Active == nullptr)
	{
		return false;
	}
	// ChooseEnemy (0x10279dd0) accepts ordinary NEW_ENEMY beside the exceptional interrupt: a
	// marked-eluded handle tests NEW_ENEMY || LOST_ENEMY, and a dead one NEW_ENEMY || ENEMY_DEAD.
	if (Required == EElysiumNpcCond::LostEnemy)
	{
		return Active->Interrupts.Has(EElysiumNpcCond::NewEnemy)
			|| Active->Interrupts.Has(EElysiumNpcCond::LostEnemy);
	}
	if (Required == EElysiumNpcCond::EnemyDead)
	{
		return Active->Interrupts.Has(EElysiumNpcCond::NewEnemy)
			|| Active->Interrupts.Has(EElysiumNpcCond::EnemyDead);
	}
	return Active->Interrupts.Has(EElysiumNpcCond::NewEnemy);
}

FElysiumEntityHandle ElysiumNpcEnemy::BestEnemy(const FElysiumNpc& Npc)
{
	FElysiumEntityWorld* World = Npc.World;
	if (World == nullptr)
	{
		return FElysiumEntityHandle::Invalid();
	}
	const FElysiumNpcMemory& Memory = Npc.Senses.Memory;

	bool bHaveBest = false;
	FNpcEnemyCandidate Best;

	// Candidate discovery walks only this NPC's CAI_Memory records. A hostile world actor that has
	// never been observed is absent here and cannot be selected.
	for (const FElysiumNpcEnemyMemoryRecord& Record : Npc.EnemyMemory.Records())
	{
		FElysiumEntity* Entity = World->Resolve(Record.Handle);
		if (Entity == nullptr || Entity->Handle == Npc.Handle || Record.bEluded)
		{
			continue;   // never self
		}
		// "a living actor": the player and the registered combat characters are the actors this
		// runtime carries. A logic entity or a prop is not an enemy no matter what row names it.
		if (Entity->AsCombatCharacter() == nullptr || Entity->IsInert())
		{
			continue;
		}
		EElysiumRelationship Relation = EElysiumRelationship::Neutral;
		int32 Priority = 0;
		if (!Npc.Relationships.ResolveRow(Entity->Handle, NpcEnemyClassnameOf(*Entity), Relation, Priority))
		{
			Priority = 5;   // IRelationPriority's non-null default
		}
		if (Relation != EElysiumRelationship::Hate && Relation != EElysiumRelationship::Fear)
		{
			continue;   // only D_HT and D_FR are eligible
		}

		FNpcEnemyCandidate Candidate;
		Candidate.Handle = Entity->Handle;
		Candidate.Priority = Priority;
		Candidate.DistanceUnits = FMath::TruncToInt(
			FVector::DistSquared(Npc.Origin, Entity->Origin) / (ElysiumMove::U * ElysiumMove::U));
		Candidate.bVisible = NpcEnemyIsCandidateVisible(Npc, Entity->Handle);
		Candidate.bReachable = NpcEnemyIsReachable(Npc, *Entity);

		if (!bHaveBest)
		{
			Best = Candidate;
			bHaveBest = true;
			continue;
		}
		if (Candidate.bReachable != Best.bReachable)
		{
			if (Candidate.bReachable)
			{
				Best = Candidate;
			}
			continue;
		}
		if (Candidate.Priority != Best.Priority)
		{
			if (Candidate.Priority > Best.Priority)
			{
				// 0x102744e6's higher-priority arm replaces the actor/priority/distance but leaves
				// the visibility latch untouched. The next equal-priority candidate observes that
				// stale byte, which is a retail selection quirk rather than a clean lexicographic key.
				const bool bPriorVisibleLatch = Best.bVisible;
				Best = Candidate;
				Best.bVisible = bPriorVisibleLatch;
			}
			continue;
		}
		if (NpcEnemyDisplaces(Candidate, Best))
		{
			Best = Candidate;
		}
	}
	return bHaveBest ? Best.Handle : FElysiumEntityHandle::Invalid();
}

void ElysiumNpcEnemy::SetEnemy(FElysiumNpc& Npc, const FElysiumEntityHandle& NewEnemy)
{
	FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	const FElysiumEntityHandle Old = Memory.Enemy;
	if (Old.IsSet())
	{
		Memory.LastEnemy = Old;   // the old handle goes through the last-enemy path first
	}
	Memory.Enemy = NewEnemy;

	// "forgets the previous LOS claim": the debounce, its occlusion flag and the edge latch all
	// belong to ONE acquisition episode, so a new enemy starts a new one. Without this the found
	// edge for the new target would be swallowed by the previous target's latch.
	Memory.EnemyLosFailures = 0;
	Memory.bEnemyOccluded = false;
	Memory.bEnemyLosLatched = false;
	Memory.EnemyLastLosTime = -1.0;
	// SEAM (comment only): a non-null enemy is also registered with retail's response system, which
	// drives idle/combat speech selection. No response system exists here, so nothing is registered
	// and nothing pretends to be.

	Npc.RecordScheduleEvent(FString::Printf(TEXT("SetEnemy: %s -> %s"),
		Old.IsSet() ? *Old.ToString() : TEXT("(none)"),
		NewEnemy.IsSet() ? *NewEnemy.ToString() : TEXT("(none)")));
	if (Npc.World != nullptr && Old != NewEnemy)
	{
		ElysiumNpcDebugLogging::EnemyChoice(Npc, *Npc.World, Old, NewEnemy);
	}
}

bool ElysiumNpcEnemy::ChooseEnemy(FElysiumNpc& Npc, FElysiumNpcConditions& Cond, double Now)
{
	FElysiumEntityWorld* World = Npc.World;
	if (World == nullptr)
	{
		return false;
	}
	FElysiumNpcMemory& Memory = Npc.Senses.Memory;

	// The recovered implementation order lists the interrupt gate ahead of `ShouldChooseNewEnemy`.
	// Both are side-effect-free predicates and a search needs both, so which runs first cannot
	// change the outcome — only the diagnostics. Stickiness runs first here so the gate's
	// starvation record describes a search that was actually wanted, rather than firing on every
	// pass of an NPC that was never going to look.
	if (!ShouldChooseNewEnemy(Npc, Cond))
	{
		return false;   // a living, non-eluded current enemy with none of the trigger conditions
	}

	// The interrupt-interest gate, BEFORE any search. This is the starvation rule.
	const EElysiumNpcCond Required = RequiredInterrupt(Npc);
	if (!IsScheduleInterested(Npc, Required))
	{
		const int32 ScheduleNumber = ElysiumScheduleNumber(Npc.Schedule.Current);
		if (Npc.Cognition.StarvedScheduleNumber != ScheduleNumber)
		{
			// One record per NPC per schedule: a different program starving selection is a
			// different fact, and the same one repeating every think is not.
			Npc.Cognition.StarvedScheduleNumber = ScheduleNumber;
			Npc.RecordScheduleEvent(FString::Printf(
				TEXT("enemy selection skipped: %s (0x%x) does not interrupt on %s"),
				ElysiumScheduleName(Npc.Schedule.Current), ScheduleNumber,
				ElysiumNpcCondName(Required)));

			// The retail warning, and the ONE arm it belongs to. The recovered text names "a null
			// enemy under such a schedule", and the schedule gate reaches that state only through
			// `LOST_ENEMY`: a committed enemy that went null while the running program refuses to
			// hear about it. The never-had-an-enemy case reaches the same gate with a null handle
			// but is the ordinary steady state of every civilian in the corpus — warning on it
			// would report the game working as a fault, and one map's cast would bury the real one.
			if (Required == EElysiumNpcCond::LostEnemy
				&& ElysiumNpcCond::ResolveEnemyHandle(*World, Memory.Enemy) == nullptr)
			{
				UE_LOG(LogElysiumNpcEnt, Warning,
					TEXT("%s lost its enemy to a null handle and the active schedule %s (0x%x) does "
						"not interrupt on LOST_ENEMY — selection is skipped and the schedule keeps "
						"ownership"),
					*Npc.DebugString(), ElysiumScheduleName(Npc.Schedule.Current), ScheduleNumber);
			}
		}
		return false;
	}
	// A pass that got through the gate clears the latch: the next refusal, even by the same
	// schedule, is a fresh episode rather than one already reported.
	Npc.Cognition.StarvedScheduleNumber = -1;
	const FElysiumEntityHandle Old = Memory.Enemy;
	const FElysiumEntity* OldEntity = ElysiumNpcCond::ResolveEnemyHandle(*World, Old);
	const bool bOldWentNull = Old.IsSet() && OldEntity == nullptr;
	const bool bOldDead = OldEntity != nullptr && OldEntity->IsInert();
	const bool bOldEluded = Old.IsSet() && Npc.EnemyMemory.IsEluded(Old);

	const FElysiumEntityHandle New = BestEnemy(Npc);
	if (New == Old)
	{
		// The same actor survived arbitration. Nothing transitions, so nothing fires; the committed
		// conditions gathered after this still describe it.
		return false;
	}

	if (bOldDead)
	{
		Cond.Set(EElysiumNpcCond::EnemyDead);
	}
	if ((bOldWentNull || bOldEluded) && !New.IsSet())
	{
		Cond.Set(EElysiumNpcCond::LostEnemy);
		// The remembered target kind decides which surface fires. These are the LOST-THE-ACTOR
		// outputs, distinct from the lost-LINE-OF-SIGHT pair: losing sight neither clears the
		// enemy nor forgets the player, and this transaction does both.
		static const FName OnLostPlayer(TEXT("OnLostPlayer"));
		static const FName OnLostEnemy(TEXT("OnLostEnemy"));
		const bool bWasPlayer = World->PlayerHandle().IsSet() && Old == World->PlayerHandle();
		Npc.FireOutput(bWasPlayer ? OnLostPlayer : OnLostEnemy, Old);
		Npc.RecordScheduleEvent(FString::Printf(TEXT("%s: %s (%s)"),
			bWasPlayer ? TEXT("OnLostPlayer") : TEXT("OnLostEnemy"), *Old.ToString(),
			bOldEluded ? TEXT("eluded") : TEXT("went null")));
		// SEAM (comment only): retail also emits a lost-enemy sound hook here. The game-sound bus
		// carries no recovered category for it, so nothing is emitted rather than a guessed one.
	}

	SetEnemy(Npc, New);

	// "clears stale had-enemy/player memory": the closest-player cache belongs to the sight pass and
	// is not hostility admission, so it is left alone; what a replacement invalidates is the
	// last-seen record the previous target owned, which no longer describes the committed one.
	if (bOldWentNull || bOldEluded || bOldDead)
	{
		for (int32 i = 0; i < static_cast<int32>(FElysiumNpcMemory::ESeen::Count); ++i)
		{
			if (Memory.LastSeen[i].IsSet() && Memory.LastSeen[i] == Old)
			{
				Memory.LastSeen[i] = FElysiumEntityHandle::Invalid();
				Memory.LastSeenTime[i] = -1.0;
			}
		}
	}

	// "sets or clears NEW_ENEMY".
	if (New.IsSet())
	{
		Cond.Set(EElysiumNpcCond::NewEnemy);
	}
	else
	{
		Cond.Clear(EElysiumNpcCond::NewEnemy);
	}

	// SEAM (comment only): retail also vacates an occupied strategy slot on the transition. Slots
	// are a squad-coordination store this runtime does not carry.
	(void)Now;
	return true;
}

void ElysiumNpcEnemy::GatherConditions(FElysiumNpc& Npc, double Now)
{
	FElysiumNpcConditions& Cond = Npc.Cognition.Conditions;
	const double Previous = Npc.Cognition.GatheredAt;
	// Retail's `0x1026ec30` zeroes nothing: each lane below clears and re-sets its own bits, and a
	// bit no lane owns stands until `SetSchedule` (`0x10280e50`) zeroes the word. This runtime's
	// lanes only set, so the pass rebuilds the sensed lanes from a cleared word — a port structure,
	// not a retail step — and the two bits the schedule kernel writes from outside the gather
	// (`TaskFail 0x10273fc0` → `TASK_FAILED 0x5c`, `ScheduleDone` → `SCHEDULE_DONE 0x5d`) are
	// carried across it. `MaintainSchedule` reads `TASK_FAILED` on the pass AFTER the failure
	// (`IsScheduleValid 0x10280ff0`), and a gather that dropped it would re-run the failed task.
	const bool bTaskFailed = Cond.Has(EElysiumNpcCond::TaskFailed);
	const bool bScheduleDone = Cond.Has(EElysiumNpcCond::ScheduleDone);
	Cond.Reset();
	if (bTaskFailed) Cond.Set(EElysiumNpcCond::TaskFailed);
	if (bScheduleDone) Cond.Set(EElysiumNpcCond::ScheduleDone);

	// 1. Senses and the hostile-category conditions.
	ElysiumNpcCond::GatherBump(Npc, Previous, Cond);
	ElysiumNpcCond::GatherDamage(Npc, Previous, Cond);
	ElysiumNpcCond::GatherHearing(Npc, Previous, Cond);
	ElysiumNpcCond::GatherSight(Npc, Now, Cond);

	// `CAI_BaseNPCTroika::GatherConditions`' (`0x102b27f0`) three consecutive sweeps, in retail's own
	// order: see-unknown (`0x102b15c0`), comfort (`0x102b1a20`), then sound (`0x102b1cd0`). All three
	// run after `GatherSight`: the see-unknown sweep reads the `SEE_UNKNOWN` condition that just
	// landed, and the sound sweep's `SEE_SOUND_SOURCE` tail asks whether a sight condition already
	// stands for the committed sound's owner.
	ElysiumNpcCond::GatherSeeUnknown(Npc, Now, Cond);
	ElysiumNpcCond::GatherComfort(Npc, Now, Cond);
	ElysiumNpcCond::GatherSounds(Npc, Now, Cond);

	// The player-law lanes join step 1.
	// The recovered pass "clears and recomputes `COND_INVESTIGATE_LEVEL` plus four law conditions"
	// as part of condition gathering, and the direct-player lane consumes the player-LOS latch
	// `GatherSight` has just run against — so it lands at the TAIL of step 1, after sight and before
	// the enemy transaction.
	//
	// Before `ChooseEnemy` for a reason: the attack arm's consequence is a `D_HT` relationship row,
	// and a row installed by a previous pass's schedule selection has to be arbitrated by the
	// ordinary transaction below rather than a pass later. This call itself submits nothing, touches
	// no player state, and never mutates Masquerade or spawns police — that is schedule selection's
	// half, and keeping the two apart is the recovered split.
	ElysiumNpcWitness::GatherLawConditions(Npc, Now, Cond);

	// 2. RefreshMemories before ChooseEnemy. Invalid/dead record entries leave this NPC's store;
	// the separately committed handle stays in place for the went-null/dead transaction below.
	if (Npc.World)
	{
		Npc.EnemyMemory.Refresh(*Npc.World, Now);
	}
	if (Npc.Senses.Memory.Enemy.IsSet() && Npc.World)
	{
		const FElysiumEntity* Enemy = ElysiumNpcCond::ResolveEnemyHandle(*Npc.World,
			Npc.Senses.Memory.Enemy);
		if (Enemy != nullptr && Enemy->IsInert())
		{
			Cond.Set(EElysiumNpcCond::EnemyDead);
		}
	}

	// 3. ChooseEnemy — the gate, the stickiness test, the search and the transition outputs.
	ElysiumNpcEnemy::ChooseEnemy(Npc, Cond, Now);

	// 4. The new-enemy-condition repair (`0x1026fb40`).
	//    CHOSEN, NOT RECOVERED (the body, not the position): the survey names this stage and its
	//    address but decodes no body. The one invariant the surrounding transaction depends on is
	//    reproduced — `NEW_ENEMY` cannot stand with no enemy to be new — and nothing else is
	//    invented. Decompiling `0x1026fb40` settles what else it repairs.
	if (!Npc.Senses.Memory.Enemy.IsSet())
	{
		Cond.Clear(EElysiumNpcCond::NewEnemy);
	}

	// 5. The committed enemy's own conditions, gathered LAST so they describe the enemy this pass
	//    chose rather than the one it replaced. The recovered order names the two halves together —
	//    "gather range, LOS, facing and attack conditions for that committed enemy" — and they are
	//    kept two functions because the LOS half reads only the debounce latch while the attack half
	//    reads the weapon, and a headless case wants to drive either alone.
	ElysiumNpcCond::GatherCommittedEnemy(Npc, Cond);
	ElysiumNpcCond::GatherAttackConditions(Npc, Now, Cond);

	Npc.Cognition.GatheredAt = Now;
}
