#include "Substrate/ElysiumNpcCombatSchedules.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcSenses.h"

namespace
{
	using ECond = EElysiumNpcCond;
	using EId = EElysiumScheduleId;
	using ETask = EElysiumTask;

	FElysiumTaskStep Step(ETask Task, float Param = 0.f)
	{
		FElysiumTaskStep Out;
		Out.Task = Task;
		Out.Param = Param;
		return Out;
	}

	FElysiumTaskStep ActivityStep(const TCHAR* Activity)
	{
		FElysiumTaskStep Out;
		Out.Task = ETask::SetActivity;
		Out.Activity = Activity;
		return Out;
	}

	FElysiumTaskStep ScheduleStep(ETask Task, EId Target)
	{
		FElysiumTaskStep Out;
		Out.Task = Task;
		Out.Target = Target;
		return Out;
	}

	// Recovered verbatim from `SCHED_TROIKA_CHASE_ENEMY`: "tolerance 24". Source units; the one
	// conversion to centimetres happens in the runner, at the motor call.
	constexpr float ChaseToleranceUnits = 24.f;

	// CHOSEN, NOT RECOVERED: how far one step of a retreat covers. `TASK_MOVE_AWAY_PATH` takes a
	// distance operand and no recovered combat program states one, so the near-door schedules' own
	// 64 cm step is doubled — a retreat from a swing wants more ground than a retreat from a door,
	// and a step short enough to stay inside the attacker's reach would not be a retreat at all.
	constexpr float RetreatStepCm = 128.f;

	// CHOSEN, NOT RECOVERED — the minimal interrupt mask every program below whose registered mask
	// the survey does not decode carries.
	//
	// Empty is a real recovered posture and is used where the survey states it (the swing). It is
	// the WRONG default everywhere else: a program with no interrupts owns its NPC until it
	// completes, so a chase-adjacent program with an empty mask would keep an NPC walking at a
	// corpse. These four are the top of the interrupt census — `NEW_ENEMY` on 332 of the 691
	// schedules, `HEAVY_DAMAGE` on 279, `ENEMY_DEAD` on 182, `LOST_ENEMY` on 119 — which is the
	// evidence for the shape of an ordinary combat mask and not for any one program's own.
	FElysiumNpcConditions MinimalCombatMask()
	{
		return FElysiumNpcConditions::Of({ ECond::NewEnemy, ECond::EnemyDead, ECond::LostEnemy,
			ECond::HeavyDamage });
	}

	// --- The programs ---

	void RegisterCombatSchedules()
	{
		// --- The melee approach and its terminal swing --------------------------------------------
		// Recovered: "`SCHED_TROIKA_MELEE_ATTACK1`/`_NR` set melee-idle as failure, face, stop, then
		// transfer to `SCHED_TROIKA_MELEE_ATTACK1_SWING`. The ordinary form can still abort on
		// too-far-for-melee; both admit enemy death/loss, damage, being attacked, dodge and block
		// before transfer."
		//
		// `DETECTED_ATTACK` ("being attacked") is in that recovered list and is NOT in these masks:
		// the survey names the condition in the census and decodes no identity for it, so it has no
		// number to put in a bitset. The notice it would be derived from is written
		// (`ElysiumNpcCond::NoticeMeleeAttack`); the condition joins these two masks when the
		// identity is decoded.
		const FElysiumNpcConditions ApproachInterrupts = FElysiumNpcConditions::Of({
			ECond::EnemyDead, ECond::LostEnemy, ECond::LightDamage, ECond::HeavyDamage,
			ECond::ShouldDodge, ECond::ShouldBlock });
		{
			FElysiumSchedule S;
			S.Id = EId::MeleeAttack1;
			S.Tasks = {
				ScheduleStep(ETask::SetFailSchedule, EId::MeleeIdle),
				Step(ETask::FaceEnemy),
				Step(ETask::StopMoving),
				ScheduleStep(ETask::SetSchedule, EId::MeleeAttack1Swing),
			};
			S.Interrupts = ApproachInterrupts;
			S.Interrupts.Set(ECond::TooFarToAttack);   // "can still abort on too-far-for-melee"
			ElysiumSchedule::Register(MoveTemp(S));
		}
		{
			// The no-turn variant: the same program without the face.
			FElysiumSchedule S;
			S.Id = EId::MeleeAttack1Nr;
			S.Tasks = {
				ScheduleStep(ETask::SetFailSchedule, EId::MeleeIdle),
				Step(ETask::StopMoving),
				ScheduleStep(ETask::SetSchedule, EId::MeleeAttack1Swing),
			};
			S.Interrupts = ApproachInterrupts;
			ElysiumSchedule::Register(MoveTemp(S));
		}
		{
			// Recovered verbatim, contents and mask both: "The swing schedule has exactly
			// `TASK_ANNOUNCE_ATTACK 1 -> TASK_MELEE_ATTACK1 0` and no interrupts, so once that
			// terminal attack task owns the NPC it is not reevaluated as a fresh attack choice each
			// tick." The EMPTY mask here is the recovered posture and not an unfilled default.
			FElysiumSchedule S;
			S.Id = EId::MeleeAttack1Swing;
			S.Tasks = { Step(ETask::AnnounceAttack, 1.f), Step(ETask::MeleeAttack1, 0.f) };
			// A swing with no weapon behind it fails the terminal task; melee-idle is where the
			// approach that reached this program already declared its failures go.
			S.FailSchedule = EId::MeleeIdle;
			ElysiumSchedule::Register(MoveTemp(S));
		}

		// --- The two defensive reactions ----------------------------------------------------------
		// Recovered: "`SCHED_TROIKA_MELEE_PREBLOCK` stops then runs `TASK_MELEE_PREBLOCK`; melee
		// dodge stops, runs `TASK_MELEE_DODGE`, stops again and runs `TASK_MELEE_DODGE_ATTACK`. Both
		// use melee-idle as their failure schedule and interrupt only on lost enemy."
		//
		// CHOSEN, NOT RECOVERED (the task substitution, not the shape): `TASK_MELEE_DODGE`,
		// `TASK_MELEE_DODGE_ATTACK` and `TASK_MELEE_PREBLOCK` are class-local task identities whose
		// bodies are not decoded. Each is expressed as the activity it must put on the body, which
		// is the one thing all three certainly do; the reaction ARITHMETIC they may also carry is
		// the opposed record's, and that already lives in the weapon controller. A model whose bank
		// has no such activity fails the task and takes the declared melee-idle failure.
		{
			FElysiumSchedule S;
			S.Id = EId::MeleeDodge;
			S.Tasks = {
				Step(ETask::StopMoving),
				ActivityStep(TEXT("ACT_MELEE_DODGE")),
				Step(ETask::StopMoving),
				ActivityStep(TEXT("ACT_MELEE_DODGE_ATTACK")),
			};
			S.FailSchedule = EId::MeleeIdle;
			S.Interrupts = FElysiumNpcConditions::Of({ ECond::LostEnemy });
			ElysiumSchedule::Register(MoveTemp(S));
		}
		{
			FElysiumSchedule S;
			S.Id = EId::MeleePreblock;
			// `ACT_PREBLOCK` is the activity the Human family's preblock virtual requests
			// (`docs/vtmb/animation_and_movers.md` → the Human family row: "preblock/block/heavy
			// block/left reaction/right reaction → `ACT_PREBLOCK`, `ACT_BLOCK`, `ACT_BLOCK_HEAVY`,
			// `ACT_BLOCKED_REACTION_LEFT`, `ACT_BLOCKED_REACTION_RIGHT`"), and the same spelling the
			// player's compact action 13 routes to (`docs/vtmb/combat-and-damage.md` § "Block and
			// stagger reactions"). The weapon ladder is what reaches the armed variants the corpus
			// actually carries — `ACT_PREBLOCK_KATANA` and its four siblings on 155 stems — so the
			// task states the base activity and the translation supplies the weapon.
			S.Tasks = { Step(ETask::StopMoving), ActivityStep(TEXT("ACT_PREBLOCK")) };
			S.FailSchedule = EId::MeleeIdle;
			S.Interrupts = FElysiumNpcConditions::Of({ ECond::LostEnemy });
			ElysiumSchedule::Register(MoveTemp(S));
		}

		// --- Kick and step-back -------------------------------------------------------------------
		// CHOSEN, NOT RECOVERED (contents). The survey names both programs and the binary choice
		// between them — "When kick and step-back are both requested, a binary random choice selects
		// `SCHED_TROIKA_MELEE_KICK` (`0xdb`) or `SCHED_TROIKA_MELEE_STEPBACK` (`0xd3`); either
		// condition outranks an ordinary attack" — and decodes neither program's task list. Each is
		// composed to the intent its name states, from the recovered vocabulary only. The kick's
		// activity is `ACT_KICK`, which `docs/vtmb/combat-and-damage.md` names as the melee
		// secondary's own capability-gated request.
		{
			FElysiumSchedule S;
			S.Id = EId::MeleeKick;
			S.Tasks = { Step(ETask::StopMoving), Step(ETask::FaceEnemy), ActivityStep(TEXT("ACT_KICK")) };
			S.FailSchedule = EId::MeleeIdle;
			S.Interrupts = MinimalCombatMask();
			ElysiumSchedule::Register(MoveTemp(S));
		}
		{
			// The step-back retreats from `SavePosition`, which the selector stamps with the enemy's
			// origin — the same `TASK_MOVE_AWAY_PATH` the near-door schedules use, projected and
			// re-tested through the motor rather than walked at a guessed point.
			FElysiumSchedule S;
			S.Id = EId::MeleeStepback;
			S.Tasks = {
				Step(ETask::StopMoving),
				Step(ETask::MoveAwayFromSavePosition, RetreatStepCm),
				Step(ETask::WaitForMovement),
			};
			S.FailSchedule = EId::MeleeIdle;
			S.Interrupts = MinimalCombatMask();
			ElysiumSchedule::Register(MoveTemp(S));
		}

		// --- Standing in melee, advancing, circling ------------------------------------------------
		// CHOSEN, NOT RECOVERED (contents). Recovered intent, verbatim: "Remaining branches can
		// switch to ranged (`0xe9`), take cover on unreachable enemy (`0x17`), idle in melee
		// (`0xc7`), circle (`0xe0`/`0xe1`), use class helpers, advance (`0xca`/`0xcb`), or
		// slow-advance (`0xd1`/`0xd2`) according to facing, distance, reachability and per-NPC
		// timers."
		{
			FElysiumSchedule S;
			S.Id = EId::MeleeIdle;
			S.Tasks = {
				Step(ETask::StopMoving),
				Step(ETask::FaceEnemy),
				ActivityStep(TEXT("ACT_IDLE")),
				Step(ETask::WaitRandom, 1.f),
			};
			S.Interrupts = MinimalCombatMask();
			// The whole point of standing in melee is to notice the moment the swing is available
			// again, so this one program admits the attack conditions on top of the minimal set.
			S.Interrupts.Set(ECond::CanMeleeAttack1);
			S.Interrupts.Set(ECond::TooFarToAttack);
			ElysiumSchedule::Register(MoveTemp(S));
		}
		{
			// The melee approach. Its task list is the chase's, because "advance on the enemy" and
			// "chase the enemy" are the same five recovered tasks — what differs in retail is the
			// per-NPC timer policy that chooses between advance, slow-advance and chase, and that
			// policy is not decoded.
			FElysiumSchedule S;
			S.Id = EId::MeleeAdvance;
			S.Tasks = {
				ScheduleStep(ETask::SetFailSchedule, EId::MeleeIdle),
				Step(ETask::SetToleranceDistance, ChaseToleranceUnits),
				Step(ETask::GetPathToEnemy),
				Step(ETask::RunPath),
				Step(ETask::WaitForMovement),
			};
			S.Interrupts = MinimalCombatMask();
			S.Interrupts.Set(ECond::CanMeleeAttack1);
			S.Interrupts.Set(ECond::EnemyUnreachable);
			ElysiumSchedule::Register(MoveTemp(S));
		}
		{
			// Circling, reduced. A true circle wants a destination off the enemy's flank, and this
			// runtime has one spatial query (project a named point) rather than an environment
			// query — asking for "somewhere good to stand" is exactly the decision K13 keeps in the
			// substrate. What survives is the observable half: stop closing, keep facing, hold. The
			// strafe joins here when the cover/flank node reader lands.
			FElysiumSchedule S;
			S.Id = EId::MeleeCircle;
			S.Tasks = {
				Step(ETask::StopMoving),
				Step(ETask::FaceEnemy),
				ActivityStep(TEXT("ACT_IDLE")),
				Step(ETask::WaitRandom, 1.f),
			};
			S.Interrupts = MinimalCombatMask();
			S.Interrupts.Set(ECond::CanMeleeAttack1);
			S.Interrupts.Set(ECond::TooFarToAttack);
			ElysiumSchedule::Register(MoveTemp(S));
		}

		// --- The chase, decoded ------------------------------------------------------------------
		// Recovered verbatim: "`SCHED_TROIKA_CHASE_ENEMY` sets fail schedule `CHASE_ENEMY_FAILED`,
		// tolerance 24, gets a path to the enemy, forces relaxed locomotion, runs it and waits for
		// movement. It interrupts on a new, dead, unreachable, occluded or lost enemy; any newly
		// available melee/ranged attack; too-close, task-failed or better-weapon conditions.
		// Pathing cannot monopolize an attack-ready NPC."
		{
			FElysiumSchedule S;
			S.Id = EId::ChaseEnemy;
			S.Tasks = {
				ScheduleStep(ETask::SetFailSchedule, EId::ChaseEnemyFailed),
				Step(ETask::SetToleranceDistance, ChaseToleranceUnits),
				Step(ETask::GetPathToEnemy),
				// "forces relaxed locomotion, runs it" — one recovered task in this vocabulary: the
				// locomotion the run path puts on the body. A separate movement-activity task is not
				// invented for the half of that sentence this runtime's motor already owns.
				Step(ETask::RunPath),
				Step(ETask::WaitForMovement),
			};
			// The recovered list, as far as the condition registry carries identities for it.
			S.Interrupts = FElysiumNpcConditions::Of({
				ECond::NewEnemy, ECond::EnemyDead, ECond::EnemyUnreachable, ECond::EnemyOccluded,
				ECond::LostEnemy, ECond::CanMeleeAttack1, ECond::CanMeleeAttack2,
				ECond::CanRangeAttack1, ECond::CanRangeAttack2, ECond::TooCloseToAttack });
			// CHOSEN, NOT RECOVERED (two absentees, marked rather than approximated): "task-failed"
			// is not an interrupt in this kernel — a failed task goes to the fail schedule, which is
			// the route this program already declares, and adding a condition for it would make one
			// event take two exits. "Better weapon" has no decoded condition identity and no weapon-
			// switch domain to raise it; the ranged selector's own switch branch is skipped for the
			// same reason.
			ElysiumSchedule::Register(MoveTemp(S));
		}
		{
			// CHOSEN, NOT RECOVERED (contents). The survey names `CHASE_ENEMY_FAILED` as the chase's
			// declared failure route and decodes nothing of it. The minimum a failure route has to
			// do is stop the body it could not path and hand the NPC back to selection, which is
			// what running off the end of a program does.
			FElysiumSchedule S;
			S.Id = EId::ChaseEnemyFailed;
			S.Tasks = { Step(ETask::StopMoving), Step(ETask::WaitRandom, 1.f) };
			S.Interrupts = MinimalCombatMask();
			ElysiumSchedule::Register(MoveTemp(S));
		}

		// --- The ranged attack, decoded -----------------------------------------------------------
		// Recovered: "`SCHED_TROIKA_RANGE_ATTACK1` sets ignore-failure, stops, resolves a prior
		// botch, faces, announces, runs `TASK_RANGE_ATTACK1`, applies a zero base plus up-to-one-
		// second finish wait, resolves botch, then waits for attack time. It can be interrupted by
		// new/dead enemy, light/heavy damage, occlusion, no primary ammo or too-close-to-attack."
		//
		// Two steps of that program are absent and marked rather than guessed: the two botch
		// resolutions (`Botch_Table` is parsed on the weapon mode and has no recovered consumer —
		// the task would have nothing to resolve against) and the trailing wait-for-attack-time
		// (the deadline it waits on is the weapon controller's, and this runtime's selector already
		// reads it as `WAITING_ATTACK_TIME` on the next pass). "Sets ignore-failure" is expressed as
		// no fail schedule at all, which is this kernel's own "end and reselect".
		{
			FElysiumSchedule S;
			S.Id = EId::RangeAttack1;
			S.Tasks = {
				Step(ETask::StopMoving),
				Step(ETask::FaceEnemy),
				Step(ETask::AnnounceAttack, 1.f),
				Step(ETask::RangeAttack1, 0.f),
				Step(ETask::Wait, 0.f),
				Step(ETask::WaitRandom, 1.f),
			};
			S.Interrupts = FElysiumNpcConditions::Of({
				ECond::NewEnemy, ECond::EnemyDead, ECond::LightDamage, ECond::HeavyDamage,
				ECond::EnemyOccluded, ECond::NoPrimaryAmmo, ECond::TooCloseToAttack });
			ElysiumSchedule::Register(MoveTemp(S));
		}

		// --- Running away -------------------------------------------------------------------------
		// CHOSEN, NOT RECOVERED (contents). The survey names `0xb9` as one of the routes the ranged
		// selector takes and decodes no task list. The retreat is `TASK_MOVE_AWAY_PATH` away from
		// `SavePosition`, which the selector stamps with the enemy's origin — the projected,
		// re-tested retreat the near-door family already uses, so an unprojectable direction fails
		// the task by name instead of walking the NPC into geometry.
		{
			FElysiumSchedule S;
			S.Id = EId::RunAway;
			S.Tasks = {
				Step(ETask::StopMoving),
				Step(ETask::MoveAwayFromSavePosition, RetreatStepCm),
				Step(ETask::RunPath),
				Step(ETask::WaitForMovement),
			};
			// CHOSEN, NOT RECOVERED: the failure route. `SCHED_TROIKA_MELEE_IDLE` is the one
			// registered "stop, face the enemy and hold" terminal, and holding facing an enemy is a
			// better answer than dropping to a disposition stance for an NPC that just failed to
			// leave.
			S.FailSchedule = EId::MeleeIdle;
			S.Interrupts = MinimalCombatMask();
			ElysiumSchedule::Register(MoveTemp(S));
		}

		// --- The three damage schedules -----------------------------------------------------------
		// Recovered: "The base idle/combat selectors may choose `SMALL_FLINCH` (`0x14`): remember
		// the flinched state, stop, and execute `TASK_SMALL_FLINCH`. The alert selector chooses
		// `TAKE_COVER_FROM_ORIGIN` (`0x19`) when the attack origin lies within its recovered facing
		// test, otherwise `ALERT_SMALL_FLINCH` (`0x07`) when a usable flinch sequence exists, or
		// merely `ALERT_FACE`."
		//
		// CHOSEN, NOT RECOVERED (the task substitution): `TASK_SMALL_FLINCH`'s body is not decoded.
		// It is expressed as the activity it must place, so "when a usable flinch sequence exists"
		// becomes a real test rather than an assumption — a model whose bank has no flinch fails the
		// task and the NPC selects again, which is the recovered fall-through.
		{
			FElysiumSchedule S;
			S.Id = EId::SmallFlinch;
			S.Tasks = {
				Step(ETask::Remember, 1.f),
				Step(ETask::StopMoving),
				ActivityStep(TEXT("ACT_SMALL_FLINCH")),
				Step(ETask::WaitRandom, 0.5f),
			};
			S.Interrupts = MinimalCombatMask();
			ElysiumSchedule::Register(MoveTemp(S));
		}
		{
			FElysiumSchedule S;
			S.Id = EId::AlertSmallFlinch;
			S.Tasks = {
				Step(ETask::StopMoving),
				ActivityStep(TEXT("ACT_SMALL_FLINCH")),
				Step(ETask::WaitRandom, 0.5f),
			};
			S.Interrupts = MinimalCombatMask();
			ElysiumSchedule::Register(MoveTemp(S));
		}
		{
			// Registered, and deliberately NOT selected by anything yet. The recovered alert branch
			// reaches it through "a recovered facing test" whose threshold the survey does not
			// state, and the faithful cover behaviour is a claim on an authored `info_node_cover_*`
			// entity — an entity read this runtime does not carry yet.
			// What is registered is the reduced form:
			// leave the attack origin by the projected retreat. It exists so the alert selector has
			// somewhere to land the moment either half is recovered, rather than needing a program
			// invented at that point.
			FElysiumSchedule S;
			S.Id = EId::TakeCoverFromOrigin;
			S.Tasks = {
				Step(ETask::StopMoving),
				Step(ETask::MoveAwayFromSavePosition, RetreatStepCm),
				Step(ETask::RunPath),
				Step(ETask::WaitForMovement),
			};
			S.FailSchedule = EId::SmallFlinch;
			S.Interrupts = MinimalCombatMask();
			ElysiumSchedule::Register(MoveTemp(S));
		}

		// --- The death program ---------------------------------------------------------------------
		// Selected from the death commit itself and from nowhere else: "death sound/solid-body policy
		// leads to the death schedule" (`docs/vtmb/combat-and-damage.md` -> "NPC and player death
		// transaction"). It lives beside the combat families because death is what a fight produces,
		// and because `FElysiumNpc::OnKilled` is the one selector that names it.
		//
		// CHOSEN, NOT RECOVERED — the program's CONTENTS. Only the task identity is recovered
		// (`TASK_PLAY_DEATH_SEQUENCE`, and its ladder); the registration site listing the program's
		// steps is not decoded, so the one recovered task is the whole program rather than a shape
		// invented around it. It names no argument for the same reason: the operand is a task column
		// this runtime has no decoded activity-index table for, and inventing one would resolve a rung
		// the corpus does not author.
		//
		// The EMPTY interrupt mask is load-bearing rather than an unfilled default: nothing may take
		// the body back off a corpse, and a stimulus arriving mid-death must not return this NPC to
		// selection. The mind is `Dead` by the time this runs, which refuses every acquisition anyway;
		// the empty mask is the same statement made by the program.
		{
			FElysiumSchedule S;
			S.Id = EId::Die;
			S.Tasks = { Step(ETask::PlayDeathSequence) };
			ElysiumSchedule::Register(MoveTemp(S));
		}
	}

	struct FElysiumCombatScheduleRegistrar
	{
		FElysiumCombatScheduleRegistrar() { RegisterCombatSchedules(); }
	};
	FElysiumCombatScheduleRegistrar GElysiumCombatScheduleRegistrar;

	// --- Selector helpers ---

	// Stamp the position a retreat leaves. `TASK_MOVE_AWAY_PATH` reads `m_vSavePosition`, and the
	// door-obstruction selector stamps it the same way for the same task.
	void StampEnemyAsSavePosition(FElysiumNpc& Npc)
	{
		const FElysiumEntity* Enemy = Npc.World
			? ElysiumNpcCond::ResolveEnemyHandle(*Npc.World, Npc.Senses.Memory.Enemy) : nullptr;
		if (Enemy != nullptr)
		{
			Npc.SavePosition = Enemy->Origin;
		}
	}

	// The retained binary draw. Drawn once and kept until it is consumed or its retention expires,
	// which is what "the selector retains timer state, so repeated ticks do not independently reroll
	// every action" means for this one decision.
	FElysiumNpcCombatSelector::EMeleeReaction DrawKickOrStepback(FElysiumNpc& Npc, double Now)
	{
		FElysiumNpcCombatSelector& State = Npc.CombatSelector;
		if (State.DrawnReaction != FElysiumNpcCombatSelector::EMeleeReaction::None
			&& Now < State.DrawnReactionExpiresAt)
		{
			return State.DrawnReaction;
		}
		const bool bKick =
			ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 1) != 0;
		State.DrawnReaction = bKick ? FElysiumNpcCombatSelector::EMeleeReaction::Kick
			: FElysiumNpcCombatSelector::EMeleeReaction::Stepback;
		State.DrawnReactionExpiresAt =
			Now + FElysiumNpcCombatSelector::ReactionRetentionSeconds;
		return State.DrawnReaction;
	}
}

// --- The melee selector (`0x10385e40`) ---

EElysiumScheduleId ElysiumNpcCombat::SelectMeleeSchedule(FElysiumNpc& Npc, double Now)
{
	const FElysiumNpcConditions& Cond = Npc.Cognition.Conditions;

	// 1. Scripted combat-mode and weapon-switch gates. SKIPPED, not refused: neither has a domain in
	//    this runtime — no scripted combat mode is settable and no weapon-switch policy exists — so
	//    there is nothing to decline. A branch with no producer is silent by design; the pending
	//    inputs and the seams that would feed it are named where they belong.
	//
	// 2. Door and class helpers. The door helper is reached through the BASE composition below
	//    rather than duplicated here: a selector returning zero falls through to
	//    `CAI_BaseNPCTroika::SelectSchedule`, which is where `SelectDoorObstructionSchedule` already
	//    lives. Calling it twice would not change the answer and would hide which layer produced it.
	//    The per-class helpers (police, civilian, vampire, animal, boss) are 49 recovered custom
	//    handler bodies with no decoded contents; none is registered.

	// 3. `SHOULD_DODGE` returns `SCHED_TROIKA_MELEE_DODGE` (0xd5).
	if (Cond.Has(EElysiumNpcCond::ShouldDodge))
	{
		return EElysiumScheduleId::MeleeDodge;
	}
	// 4. `SHOULD_BLOCK` returns `SCHED_TROIKA_MELEE_PREBLOCK` (0xd6).
	if (Cond.Has(EElysiumNpcCond::ShouldBlock))
	{
		return EElysiumScheduleId::MeleePreblock;
	}
	// 5. Kick and step-back. "When kick and step-back are both requested, a binary random choice
	//    selects `SCHED_TROIKA_MELEE_KICK` (0xdb) or `SCHED_TROIKA_MELEE_STEPBACK` (0xd3); either
	//    condition outranks an ordinary attack." The coin is drawn only for the BOTH case, which is
	//    what the sentence describes; one condition alone selects its own program.
	{
		const bool bKick = Cond.Has(EElysiumNpcCond::ShouldKick);
		const bool bStepback = Cond.Has(EElysiumNpcCond::ShouldStepback);
		if (bKick && bStepback)
		{
			const bool bChoseKick = DrawKickOrStepback(Npc, Now)
				== FElysiumNpcCombatSelector::EMeleeReaction::Kick;
			if (!bChoseKick)
			{
				StampEnemyAsSavePosition(Npc);
			}
			return bChoseKick ? EElysiumScheduleId::MeleeKick : EElysiumScheduleId::MeleeStepback;
		}
		if (bKick)
		{
			return EElysiumScheduleId::MeleeKick;
		}
		if (bStepback)
		{
			StampEnemyAsSavePosition(Npc);
			return EElysiumScheduleId::MeleeStepback;
		}
	}

	// 6. "A usable `CAN_MELEE_ATTACK1` then chooses `SCHED_TROIKA_MELEE_ATTACK1` (0xdc) or its
	//    no-turn variant (0xdd)."
	//
	//    CHOSEN, NOT RECOVERED: which of the two. Nothing decoded states the discriminator. The
	//    turning form is taken, because its extra `TASK_FACE_ENEMY` is a no-op for a body already
	//    aligned — and `CAN_MELEE_ATTACK1` requires alignment — so choosing it can only ever cost a
	//    turn an NPC did not need, while choosing the no-turn form for an NPC that did need one
	//    would swing at nothing. `MeleeAttack1Nr` stays registered so the decoded discriminator has
	//    somewhere to land.
	if (Cond.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		return EElysiumScheduleId::MeleeAttack1;
	}

	// 7. Switch to ranged (0xe9) and take cover on an unreachable enemy (0x17). Both SKIPPED: the
	//    first needs a weapon-switch policy this runtime has no domain for, and the second gates on
	//    `ENEMY_UNREACHABLE`, which has no producer (the reachability query the motor seam does not
	//    carry — see `ElysiumNpcCond::GatherCommittedEnemy`).

	// 8. "advance (0xca/0xcb) or slow-advance (0xd1/0xd2) according to facing, distance,
	//    reachability and per-NPC timers." CHOSEN, NOT RECOVERED: distance is the discriminator this
	//    runtime can answer, and the slow-advance pair is not registered — the per-NPC timers that
	//    would choose it are named and undecoded.
	if (Cond.Has(EElysiumNpcCond::TooFarToAttack))
	{
		return EElysiumScheduleId::MeleeAdvance;
	}

	// 9. "circle (0xe0/0xe1)" and "idle in melee (0xc7)". CHOSEN, NOT RECOVERED: an NPC inside its
	//    own reach and still on its attack recovery circles; one that is in reach, off recovery and
	//    merely not aligned holds. Both are the doc's own two remaining melee options and the split
	//    is the only observable difference between them this runtime can state.
	if (Cond.Has(EElysiumNpcCond::WaitingAttackTime))
	{
		return EElysiumScheduleId::MeleeCircle;
	}
	return EElysiumScheduleId::MeleeIdle;
}

// --- The ranged selector (`0x10386560`) ---

EElysiumScheduleId ElysiumNpcCombat::SelectRangedSchedule(FElysiumNpc& Npc, double Now)
{
	const FElysiumNpcConditions& Cond = Npc.Cognition.Conditions;
	(void)Now;   // the ranged selector's own per-NPC timers are undecoded; see step 2

	// 1. "The ranged selector first honors a pending switch to melee (0xe3) and weapon/timing
	//    setup." SKIPPED: there is no weapon-switch domain to raise a pending switch, and the setup
	//    branch's contents are not decoded.
	//
	// 2. "A weapon protruding through a wall selects back-away (0xb8)." SKIPPED: `WEAPON_THROUGH_WALL`
	//    (0x3c) has no producer — it is a muzzle-space term this runtime cannot answer (see
	//    `ElysiumNpcCond::GatherAttackConditions`) — so the branch would be unreachable and 0xb8 is
	//    not registered.
	//
	// 3. "Its class helpers then own dodge, door, cover and chase decisions." The door helper is the
	//    base composition's, exactly as in the melee selector; the rest are undecoded class bodies.

	// 4. "no ammo ... instead route through reload/hide". SKIPPED by name: no reload/hide program is
	//    registered, because `TASK_RELOAD` and the hide destination are both undecoded. The
	//    condition is still load-bearing — `CAN_RANGE_ATTACK1` requires a payable magazine, so an
	//    NPC out of ammunition falls through to the spacing arms below instead of dry-firing at its
	//    enemy every think.

	// 5. The attack-ready branch (`0xec`-`0xf0`). CHOSEN, NOT RECOVERED: only `SCHED_TROIKA_RANGE_
	//    ATTACK1` (0xec) is decoded of the five; the shoot, step-back and forced-range variants are
	//    named without contents, and what selects between them is not stated.
	if (Cond.Has(EElysiumNpcCond::CanRangeAttack1))
	{
		return EElysiumScheduleId::RangeAttack1;
	}

	// 6. "occlusion ... instead route through reload/hide, move-for-clear-shot (0xbb/0xbd),
	//    wait-for-clear-shot (0xbc), cover, chase (0xb1) or run-away (0xb9)". CHOSEN, NOT RECOVERED:
	//    the chase is the one member of that recovered set whose program IS decoded, and closing on
	//    an enemy it cannot see is a clear-shot move by another name. The three unregistered
	//    move-for-shot programs land here when their contents are decoded.
	if (Cond.Has(EElysiumNpcCond::EnemyOccluded) || Cond.Has(EElysiumNpcCond::WeaponSightOccluded))
	{
		return EElysiumScheduleId::ChaseEnemy;
	}

	// 7. "excessive distance ... chase (0xb1)". Recovered routing, decoded program.
	if (Cond.Has(EElysiumNpcCond::TooFarToAttack))
	{
		return EElysiumScheduleId::ChaseEnemy;
	}

	// 8. Too close to shoot: "run-away (0xb9)". CHOSEN, NOT RECOVERED among the listed routes, for
	//    the same reason as step 6 — it is the registered member of the recovered set, and backing
	//    off is what the condition describes.
	if (Cond.Has(EElysiumNpcCond::TooCloseToAttack))
	{
		StampEnemyAsSavePosition(Npc);
		return EElysiumScheduleId::RunAway;
	}

	// 9. In range, unoccluded and merely on the attack timer. The selector declines, which is the
	//    recovered composition rule and not a gap: `WAITING_ATTACK_TIME` is what the base branch's
	//    idle then holds through, and the next pass with the deadline passed reaches step 5.
	return EElysiumScheduleId::None;
}
