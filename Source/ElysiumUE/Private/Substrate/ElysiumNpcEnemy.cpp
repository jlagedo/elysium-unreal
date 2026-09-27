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

	// The candidate struct and the three scoring helpers this file used to arbitrate with moved into
	// story 29d's slot-478 body (`FElysiumNpc::FBestEnemyState`,
	// `FElysiumNpcBase::BestEnemyCandidateVisible`, `FElysiumNpcBase::BestEnemyDistanceKey` and the slot-530
	// `IsUnreachable` dispatch). The rule they carried is unchanged and is now beside the three
	// gates retail applies in front of it.
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
	const FElysiumEntityHandle Current = Npc.BaseMemory.Enemy;
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

bool ElysiumNpcEnemy::ShouldChooseNewEnemy(const FElysiumNpcBase& Npc, const FElysiumNpcConditions& Cond)
{
	if (!Npc.BaseMemory.Enemy.IsSet() || Npc.World == nullptr)
	{
		return true;
	}
	const FElysiumEntity* Enemy =
		ElysiumNpcCond::ResolveEnemyHandle(*Npc.World, Npc.BaseMemory.Enemy);
	// `0x10279d33`: slot 158 `IsAlive` (`vt+0x278`) on the enemy — `m_lifeState`, which the port
	// spells as the death latch or `Kill`'s terminal flag (`FElysiumEntity::IsAlive`). The port's
	// old test was `IsInert` (dead-or-HIDDEN), which missed a dying enemy whose death transaction
	// had run and counted a hidden one as dead (corrected in the story 8 L11 integration).
	if (Enemy == nullptr || !const_cast<FElysiumEntity*>(Enemy)->IsAlive()) // 0x10279d23 / 0x10279d3b
	{
		return true;   // the actor is dead, or gone; this is the same pass that noticed it
	}
	if (Npc.EnemyMemory.IsEluded(Npc.BaseMemory.Enemy))
	{
		return true;
	}
	// `SEE_FEAR` is deliberately absent — see the header.
	return Cond.Has(EElysiumNpcCond::SeeHate)
		|| Cond.Has(EElysiumNpcCond::SeeDislike)
		|| Cond.Has(EElysiumNpcCond::SeeNemesis)
		|| Cond.Has(EElysiumNpcCond::EnemyDead);
}

FElysiumEntityHandle ElysiumNpcEnemy::BestEnemy(const FElysiumNpc& Npc)
{
	// `BestEnemy` (`0x102743c0`) is SLOT 478, and story 29d's family Senses10 carries its body —
	// including the three gates this walk did not have (`FL_NOTARGET`, `m_bIsBCCTargetable` and
	// slot 479 `IsValidEnemy`) and the one species arm over it (`CNPC_VFrenzyShadow#478`,
	// `0x103766d0`). This dispatches the slot rather than keeping a second copy of the arbitration.
	FElysiumEntity* Best = const_cast<FElysiumNpc&>(Npc).BestEnemy();
	return Best != nullptr ? Best->Handle : FElysiumEntityHandle::Invalid();
}

// `SetEnemy` (`0x10279a50`), 213 bytes, whole and in retail's order (story 8, lane L11).
//
// The body the port used to carry transferred the old handle unconditionally, never cleared the
// attack conditions, left the discipline notify a comment and reset the port's own LOS-debounce
// episode (`EnemyLosFailures` / `bEnemyLosLatched` / `EnemyLastLosTime`) — a port-invented step
// retail does not have here. All corrected; the LOS episode belongs to the gather pass that owns
// it (`CAI_BaseNPC::GatherEnemyConditions` `0x10270b20`, lane L07).
void ElysiumNpcEnemy::SetEnemy(FElysiumNpcBase& Npc, const FElysiumEntityHandle& NewEnemy)
{
	FElysiumEntityWorld* const World = Npc.World;
	const FElysiumEntityHandle OldHandle = Npc.BaseMemory.Enemy;
	// `0x10279a5a`-`0x10279a7f`: `m_hEnemy` (`+0x5ce0`) resolved through the handle table; `-1`
	// (`0x10279a63`) or a stale serial (`0x10279a7d`) is a null old enemy.
	FElysiumEntity* const Old = World != nullptr ? World->Resolve(OldHandle) : nullptr;
	// Retail's argument is an entity pointer; this runtime's callers hand a handle. A set handle is
	// retail's non-null argument (it writes the handle as given at `0x10279b01`); the pointer the
	// identity test compares is that handle resolved.
	FElysiumEntity* const New =
		(World != nullptr && NewEnemy.IsSet()) ? World->Resolve(NewEnemy) : nullptr;
	if (Old != New)                                                        // 0x10279a8b
	{
		// `0x10279a96`-`0x10279ab7`: only a LIVE old handle (not -1, serial matching at 0x10279ab2
		// JNZ, table entry non-null) reaches the last-enemy helper; the re-resolve at
		// `0x10279ab9`-`0x10279add` of the same handle (its -1 test 0x10279ac2 JZ and serial test
		// 0x10279ad9 JNZ, both to the null argument) cannot answer null in between, so the port has
		// no separate line for it (second judge, packet row 0x10279a50).
		if (Old != nullptr)
		{
			Npc.SetLastEnemy(Old);                                         // 0x10279ae4 -> 0x10279b70
		}
		// Slot 560 `ClearAttackConditions` on EVERY change, even from a null old enemy.
		Npc.ClearAttackConditions();                                       // 0x10279aed
	}
	if (NewEnemy.IsSet())                                                  // 0x10279af5
	{
		Npc.BaseMemory.Enemy = NewEnemy;                                   // 0x10279afb / 0x10279b01
		// `thunk_FUN_101e3d70(&DAT_10739a4c, this)` — the discipline manager's break-on-notice
		// sweep, run on EVERY non-null write, an unchanged enemy included. SEAM (counted, strips
		// nothing): see `SetEnemyDisciplineStripCalls`.
		++Npc.SetEnemyDisciplineStripCalls;                                // 0x10279b0c
	}
	else
	{
		Npc.BaseMemory.Enemy = FElysiumEntityHandle::Invalid();            // 0x10279b16
	}

	// Debug-layer observability only (no retail word): the enemy-choice log line.
	if (World != nullptr && OldHandle != Npc.BaseMemory.Enemy)
	{
		ElysiumNpcDebugLogging::EnemyChoice(Npc, *World, OldHandle, Npc.BaseMemory.Enemy);
	}
}

// `CAI_BaseNPC::ChooseEnemy` (`0x10279dd0`), 1068 bytes, whole and in retail's order (story 8,
// lane L11). The scope-trace frame and the `CVProfile` scope (`"CAI_Enemies_ChooseEnemy"`,
// `0x10279e53`, `0x10279f5d`-`0x10279fa4`, `0x1027a105`-`0x1027a1e9`) are profiler bookkeeping
// with no observable and stay absent. The scope-trace frame's entity-name pick (`0x10279dda` JZ:
// a null `this` names "NULL ENTITY"; `0x10279de4` JNZ: a null `+0x26c` classname falls back to
// the empty string) feeds only that trace record, so the port has no line for it.
//
// Replaces the port's guess, which ran the stickiness test BEFORE the gate, had no went-null
// fall-through, fired the lost outputs with the old enemy as activator, invented a last-seen
// scrub, never cleared or set `m_afMemory`, never vacated the squad slot and never played
// `LostEnemySound`. It answers retail's value: whether an enemy is now held.
bool ElysiumNpcEnemy::ChooseEnemy(FElysiumNpcBase& Npc)
{
	FElysiumNpcConditions& Cond = Npc.Cognition.Conditions;
	// `0x10279e5d`: slot 167 `GetEnemy()` — the const overload, `m_hEnemy` resolved.
	FElysiumEntity* const Current = static_cast<const FElysiumNpcBase&>(Npc).GetEnemy();
	// `0x10279e65`: `m_afMemory` (`+0x5d8c`) at entry. `bHadEnemy` is bit `0x8000` (a non-player
	// enemy) or `0x10000` (the player) — the pair `0x1027a0ff` below writes.
	const uint32 MemoryAtEntry = Npc.BaseScheduleHost.MemoryBits;
	const bool bHadEnemy = (MemoryAtEntry & 0x18000u) != 0;               // 0x10279e6b / 0x10279e76
	bool bWentNull = false;
	bool bLostOrEluded = false;
	if (bHadEnemy && Current == nullptr)                                   // 0x10279e85 / 0x10279e89
	{
		bWentNull = true;                                                  // 0x10279e8b
		bLostOrEluded = true;                                              // 0x10279e90
	}
	else if (Current != nullptr)                                           // 0x10279eeb
	{
		// `0x10279ef2`: slot 541 `GetEnemies()`, then `IsEluded` (`0x102e0210`) for the enemy.
		const FElysiumNpcEnemyMemory* const Enemies =
			static_cast<const FElysiumNpcEnemyMemory*>(Npc.GetEnemies());
		if (Enemies != nullptr && Enemies->IsEluded(Current->Handle))      // 0x10279efa / 0x10279f01
		{
			bLostOrEluded = true;                                          // 0x10279e90
		}
	}
	// `0x10279e97`-`0x10279eaa`: slot 158 `IsAlive` on the current enemy (the call at 0x10279e9d).
	const bool bDead = Current != nullptr && !Current->IsAlive();

	// `0x10279eb9`: with NO running schedule (`m_pSchedule` `+0x5c38` null) all three interrupt
	// answers are FORCED true; otherwise each is `ConditionInterruptsCurrentSchedule`
	// (`0x10269c70`, the mask alone — `ElysiumSchedule::MaskHasCondition`).
	bool bNewInterrupts = true;
	bool bLostInterrupts = true;
	bool bDeadInterrupts = true;
	if (Npc.Schedule.IsRunning())
	{
		bNewInterrupts = ElysiumSchedule::MaskHasCondition(Npc.Schedule, Npc,
			EElysiumNpcCond::NewEnemy);                                    // 0x10279ebf (0x54)
		bLostInterrupts = ElysiumSchedule::MaskHasCondition(Npc.Schedule, Npc,
			EElysiumNpcCond::LostEnemy);                                   // 0x10279ecc (0x47)
		bDeadInterrupts = ElysiumSchedule::MaskHasCondition(Npc.Schedule, Npc,
			EElysiumNpcCond::EnemyDead);                                   // 0x10279ed9 (0x58)
	}

	// The gate. A dead enemy whose death the program listens for skips it outright.
	if (!bDead || !bDeadInterrupts)                                        // 0x10279f18 / 0x10279f1c
	{
		if (bWentNull)                                                     // 0x10279f28
		{
			// The went-null case only WARNS and falls through to the choice: `DevMsg(2, …)` when
			// neither NEW_ENEMY nor LOST_ENEMY interrupts a running program.
			// 0x10279fb8 JNZ (NEW_ENEMY interrupts), 0x10279fc0 JNZ (LOST_ENEMY interrupts),
			// 0x10279fca JZ (no running schedule): each skips the warning.
			if (!bNewInterrupts && !bLostInterrupts && Npc.Schedule.IsRunning()) // 0x10279fb8/fc0/fca
			{
				UE_LOG(LogElysiumNpcEnt, Verbose,                          // 0x10279fd7
					TEXT("WARNING: AI enemy went NULL but schedule (%s) is not interested"),
					ElysiumScheduleName(Npc.Schedule.Current));
			}
		}
		// 0x10279f30 JNZ (NEW_ENEMY interrupts -> choose), 0x10279f3c JZ (LOST_ENEMY does not
		// interrupt -> keep), 0x10279f44 JNZ (lost or eluded -> choose).
		else if (!bNewInterrupts && (!bLostInterrupts || !bLostOrEluded))  // 0x10279f30/f3c/f44
		{
			// The running program keeps ownership: nothing is chosen, nothing is written.
			// 0x10279f5d JZ / 0x10279f67 JZ (at the root node with the profiler disabled ->
			// straight to the exit at 0x1027a1e9), the node ExitScope call 0x10279f73 and its
			// 0x10279f7b JZ (no pop to the parent node): the CVProfile scope exit, no observable.
			return Current != nullptr;                                     // 0x10279f5d .. 0x10279fb5
		}
	}

	// `0x10279fe5`: slot 480 `ShouldChooseNewEnemy`. A refusal keeps the current enemy as the
	// candidate; so does a `BestEnemy` answer that IS the current enemy. Either way the change work
	// still runs when the enemy went null (`0x1027a00d`-`0x1027a013`, the saved byte), the one case
	// where old and new are both null.
	FElysiumEntity* Chosen = Current;
	bool bChange = false;
	if (!Npc.ShouldChooseNewEnemy())                                       // 0x10279fed
	{
		bChange = bWentNull;                                               // 0x1027a00d / 0x1027a013
	}
	else
	{
		// Slot 478 `BestEnemy`, then `0x102707d0` — a summoned body (its Troika answering
		// `Classify() == 3`) hands the hate to its owner.
		Chosen = FElysiumNpcBase::SummonerRedirect(Npc.BestEnemy());       // 0x10279ff3 / 0x10279ffc
		bChange = Chosen != Current || bWentNull;                          // 0x1027a003 / 0x1027a005
	}
	if (!bChange)
	{
		return Chosen != nullptr;                                          // 0x1027a105
	}

	// The change work.
	Npc.BaseScheduleHost.MemoryBits &= 0xfffe7fffu;                        // 0x1027a019 / 0x1027a027
	// The OLD enemy dead (slot 158 false) SETS `ENEMY_DEAD`; an alive one changes nothing, and
	// nothing here clears 0x58 (second judge, packet row 0x10279dd0).
	// 0x1027a02d JZ (no old enemy), the slot 158 `IsAlive` call 0x1027a033, 0x1027a03b JNZ (alive).
	if (Current != nullptr && !Current->IsAlive())                         // 0x1027a02d/a033/a03b
	{
		// `(*DAT_10924a6c)->vfunc1()` (`0x1027a045`) is the `ent_trace_conditions` ConVar touch
		// every `SetCondition` site makes; it has no observable.
		Cond.Set(EElysiumNpcCond::EnemyDead);                              // 0x1027a04c
	}
	if (Chosen == nullptr)                                                 // 0x1027a053
	{
		Cond.Clear(EElysiumNpcCond::NewEnemy);                             // 0x1027a059 (0x10269b50)
	}
	else
	{
		// 0x1027a068: the `ent_trace_conditions` ConVar touch (`(*DAT_10924a6c)->vfunc1()`), no observable.
		Cond.Set(EElysiumNpcCond::NewEnemy);                               // 0x1027a06f
	}
	SetEnemy(Npc, Chosen != nullptr ? Chosen->Handle : FElysiumEntityHandle::Invalid()); // 0x1027a077
	if (bHadEnemy)                                                         // 0x1027a07c / 0x1027a082
	{
		Npc.VacateSquadSlot();                                             // 0x1027a086 -> 0x1028ae60
		Npc.BaseScheduleHost.MemoryBits &= 0xfffdffffu;                    // 0x1027a08b
	}
	if (Chosen == nullptr)                                                 // 0x1027a097
	{
		if (bLostOrEluded)                                                 // 0x1027a09f
		{
			// 0x1027a0a9: the `ent_trace_conditions` ConVar touch, no observable.
			Cond.Set(EElysiumNpcCond::LostEnemy);                          // 0x1027a0b0
			Npc.LostEnemySound();                                          // 0x1027a0b9, slot 493
		}
		// The ENTRY word's player bit picks the output; retail fires it with `this` as both
		// activator and caller (`0x100cd660(&output, this, this, 0)`).
		static const FName OnLostPlayer(TEXT("OnLostPlayer"));
		static const FName OnLostEnemy(TEXT("OnLostEnemy"));
		if ((MemoryAtEntry & 0x10000u) != 0)                               // 0x1027a0c5
		{
			Npc.FireOutput(OnLostPlayer, Npc.Handle);                      // 0x1027a0cd, +0x5ecc
		}
		else
		{
			Npc.FireOutput(OnLostEnemy, Npc.Handle);                       // 0x1027a0da, +0x5e84
		}
	}
	else
	{
		// `0x1027a0e1`-`0x1027a0ff`: the new enemy's `+0xa8` `m_pPlayer` set -> `0x10000`, else
		// `0x8000`. This runtime's player is the one `FElysiumEntityWorld::PlayerHandle()` names.
		const bool bIsPlayer = Npc.World != nullptr && Chosen->Handle == Npc.World->PlayerHandle();
		Npc.BaseScheduleHost.MemoryBits |= bIsPlayer ? 0x10000u : 0x8000u;
	}
	// 0x1027a11a..0x1027a1c0: the CVProfile scope exit (0x1027a11a JZ / 0x1027a122 JZ at the root
	// with the profiler disabled, 0x1027a135 JNZ / 0x1027a13e JZ the node's recursion count, the RDTSC
	// accumulate, 0x1027a1a4 JZ / 0x1027a1a9 JZ / 0x1027a1ae JNZ the break-on-budget test before
	// the 0x1027a1b2 call, 0x1027a1c0 JNZ the pop to the parent node), no observable.
	return Chosen != nullptr;                                              // 0x1027a105 .. 0x1027a1fb
}

// STORY8-TWIN: replaced by 0x102b27f0 / 0x1026ec30 / 0x10270b20 (the slot-433 virtual
// `GatherConditions()` and slot 481) at wave 2, when L13 rewires `ElysiumNpc.cpp` RunConditionPass
// and `ElysiumNpcMaintain19.cpp` to slot 433. The live loop's only condition pass until then.
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
	if (Npc.BaseMemory.Enemy.IsSet() && Npc.World)
	{
		const FElysiumEntity* Enemy = ElysiumNpcCond::ResolveEnemyHandle(*Npc.World,
			Npc.BaseMemory.Enemy);
		if (Enemy != nullptr && Enemy->IsInert())
		{
			Cond.Set(EElysiumNpcCond::EnemyDead);
		}
	}

	// 3. ChooseEnemy (`0x10279dd0`), retail's body (story 8 L11).
	ElysiumNpcEnemy::ChooseEnemy(Npc);

	// 4. The new-enemy-condition repair (`0x1026fb40`).
	//    CHOSEN, NOT RECOVERED (the body, not the position): the survey names this stage and its
	//    address but decodes no body. The one invariant the surrounding transaction depends on is
	//    reproduced — `NEW_ENEMY` cannot stand with no enemy to be new — and nothing else is
	//    invented. Decompiling `0x1026fb40` settles what else it repairs.
	if (!Npc.BaseMemory.Enemy.IsSet())
	{
		Cond.Clear(EElysiumNpcCond::NewEnemy);
	}

	// 5. The committed enemy's own conditions, gathered LAST so they describe the enemy this pass
	//    chose rather than the one it replaced. The recovered order names the two halves together —
	//    "gather range, LOS, facing and attack conditions for that committed enemy" — and they are
	//    kept two functions because the LOS half reads only the debounce latch while the attack half
	//    reads the weapon, and a headless case wants to drive either alone.
	ElysiumNpcCond::GatherCommittedEnemy(Npc, Cond);
	// Slot 561 THROUGH THE VTABLE, as `CAI_BaseNPC::GatherEnemyConditions` (`0x10270b20`) makes it:
	// `(this->*vtable[0x8c4])(GetEnemy(), flDist)`. A species class's override (`CNPC_VBach`
	// `0x10363db0`, `CNPC_VWerewolf` `0x103d02b0`) runs here; the base body is
	// `ElysiumNpcCond::GatherAttackConditions` (story 5 step 3 — the pass used to call the base body
	// directly, so Bach's block never ran from it).
	{
		FElysiumEntity* const EnemyEntity = Npc.World != nullptr && Npc.BaseMemory.Enemy.IsSet()
			? Npc.World->Resolve(Npc.BaseMemory.Enemy)
			: nullptr;
		const float DistanceUnits = EnemyEntity != nullptr
			? static_cast<float>(FVector::Dist(Npc.Origin, EnemyEntity->Origin) / ElysiumMove::U)
			: 0.f;
		Npc.GatherPassNow = Now;
		Npc.GatherAttackConditions(EnemyEntity, DistanceUnits);
		Npc.GatherPassNow = -1.0;
	}

	Npc.Cognition.GatheredAt = Now;
}
