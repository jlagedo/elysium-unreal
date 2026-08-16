#include "Substrate/ElysiumNpcEnemy.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"

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
		int32 DistanceUnits = 0;   // integer, as retail compares it
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

	// Is this candidate currently seen? Cycle 4's sight path tracks only the player, so any other
	// candidate is unseen — the same deliberate scope as `ElysiumNpcConditions.cpp`'s seen set, and
	// the reason step 4 of the arbitration is exercisable at all today.
	bool NpcEnemyIsCandidateVisible(const FElysiumNpc& Npc, const FElysiumEntityHandle& Handle)
	{
		const FElysiumEntityWorld* World = Npc.World;
		if (World == nullptr || !World->PlayerHandle().IsSet() || Handle != World->PlayerHandle())
		{
			return false;
		}
		return Npc.Senses.Memory.bPlayerLos;
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
	if (Memory.bEnemyEluded)
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
		if (Enemy == nullptr || Memory.bEnemyEluded)
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
		// CHOSEN, NOT RECOVERED: retail's NPC always owns a schedule, so "no active schedule" is a
		// state the recovered gate never sees. This runtime idles between selections with none, and
		// treating that as uninterested would starve enemy selection permanently — nothing would
		// ever acquire a first enemy. An NPC running no program is therefore interested in
		// everything.
		return true;
	}
	const FElysiumSchedule* Active = ElysiumScheduleFor(Npc.Schedule.Current);
	return Active != nullptr && Active->Interrupts.Has(Required);
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

	// Candidate discovery walks the entity list in world order, which is insertion order: the
	// arbitration's ties therefore resolve to whichever eligible entity the map declared first.
	for (const TUniquePtr<FElysiumEntity>& Entry : World->Entities())
	{
		FElysiumEntity* Entity = Entry.Get();
		if (Entity == nullptr || Entity->Handle == Npc.Handle)
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
		if (Memory.bEnemyEluded && Memory.Enemy.IsSet() && Entity->Handle == Memory.Enemy)
		{
			// The eluded marker excludes its own target. SEAM: retail's enemy-memory component
			// carries one eluded bit PER remembered actor; this runtime carries the committed
			// enemy's only, because nothing else has a producer for one. A second eluded candidate
			// is therefore eligible here where retail might refuse it.
			continue;
		}

		FNpcEnemyCandidate Candidate;
		Candidate.Handle = Entity->Handle;
		Candidate.Priority = Priority;
		Candidate.DistanceUnits = FMath::TruncToInt(FVector::Dist(Npc.Origin, Entity->Origin));
		Candidate.bVisible = NpcEnemyIsCandidateVisible(Npc, Entity->Handle);
		Candidate.bReachable = NpcEnemyIsReachable(Npc, *Entity);

		if (!bHaveBest || NpcEnemyDisplaces(Candidate, Best))
		{
			Best = Candidate;
			bHaveBest = true;
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
	Memory.bEnemyEluded = false;

	// SEAM (comment only): a non-null enemy is also registered with retail's response system, which
	// drives idle/combat speech selection. No response system exists here, so nothing is registered
	// and nothing pretends to be.

	Npc.RecordScheduleEvent(FString::Printf(TEXT("SetEnemy: %s -> %s"),
		Old.IsSet() ? *Old.ToString() : TEXT("(none)"),
		NewEnemy.IsSet() ? *NewEnemy.ToString() : TEXT("(none)")));
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
	const bool bOldEluded = Old.IsSet() && Memory.bEnemyEluded;

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
	if (bOldWentNull || bOldEluded)
	{
		Cond.Set(EElysiumNpcCond::LostEnemy);
		// The remembered target kind decides which surface fires. These are the LOST-THE-ACTOR
		// outputs, distinct from cycle 4's lost-LINE-OF-SIGHT pair: losing sight neither clears the
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
	Cond.Reset();

	// 1. Senses and the hostile-category conditions.
	ElysiumNpcCond::GatherDamage(Npc, Previous, Cond);
	ElysiumNpcCond::GatherHearing(Npc, Previous, Cond);
	ElysiumNpcCond::GatherSight(Npc, Now, Cond);

	// ===================== Cycle 10c — the player-law lanes join step 1 =========================
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
	// =============================================================================================

	// 2. The enemy-memory refresh: the recovered pass updates its records BEFORE `ChooseEnemy` runs,
	//    which is what lets the stickiness test see a death in the same pass that noticed it. The
	//    handle itself is deliberately left in place — whether it went null, or resolves to a dead
	//    actor, is the distinction `ChooseEnemy` owns, and clearing it here would erase it.
	if (Npc.Senses.Memory.Enemy.IsSet() && Npc.World)
	{
		const FElysiumEntity* Enemy =
			ElysiumNpcCond::ResolveEnemyHandle(*Npc.World, Npc.Senses.Memory.Enemy);
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
