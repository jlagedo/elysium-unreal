#include "Substrate/ElysiumSchedule.h"

#include "ElysiumAnimationIntent.h"        // the one `ACT_*` vocabulary the death ladder's rungs come off
#include "ElysiumWorldServices.h"          // IElysiumNpcMotor — the reachability query TASK_MOVE_AWAY_PATH asks
#include "Substrate/ElysiumNpcGait.h"      // the authored travel speed a retreat step commands
#include "Substrate/ElysiumNpcLog.h"       // the one `npc_*` log category a refused registration reports on

namespace
{
	// The registered numbers, decoded from their registration sites
	// (`docs/vtmb/npc-ai-reverse-engineering.md`). Carried so a trace row cites the binary.
	struct FScheduleMeta
	{
		int32 Number;
		const TCHAR* Name;
	};

	const FScheduleMeta& MetaFor(EElysiumScheduleId Id)
	{
		static const FScheduleMeta None{ 0, TEXT("SCHED_NONE") };
		static const FScheduleMeta IdleDisposition{ 0x6b, TEXT("SCHED_TROIKA_IDLE_DISPOSITION") };
		static const FScheduleMeta AlertLook{ 0x4f, TEXT("SCHED_TROIKA_ALERT_LOOK_AROUND_NI") };
		static const FScheduleMeta BackAway{ 0x91, TEXT("SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE") };
		static const FScheduleMeta BackAwayWait{ 0x96,
			TEXT("SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT_NE") };
		static const FScheduleMeta TakeCover{ 0x9c, TEXT("SCHED_TROIKA_TAKE_COVER_HINT_DOOR") };

		// The combat families. Two of them carry number 0: the survey names
		// `SCHED_TROIKA_MELEE_ATTACK1_SWING` and `CHASE_ENEMY_FAILED` and decodes their contents and
		// their callers, but not their registration sites — so the NAME is the identity for those
		// two and a trace row says `(0x0)`. Replace the number, not the row, when one is decoded.
		static const FScheduleMeta MeleeAttack1{ 0xdc, TEXT("SCHED_TROIKA_MELEE_ATTACK1") };
		static const FScheduleMeta MeleeAttack1Nr{ 0xdd, TEXT("SCHED_TROIKA_MELEE_ATTACK1_NR") };
		static const FScheduleMeta MeleeSwing{ 0, TEXT("SCHED_TROIKA_MELEE_ATTACK1_SWING") };
		static const FScheduleMeta MeleeDodge{ 0xd5, TEXT("SCHED_TROIKA_MELEE_DODGE") };
		static const FScheduleMeta MeleePreblock{ 0xd6, TEXT("SCHED_TROIKA_MELEE_PREBLOCK") };
		static const FScheduleMeta MeleeKick{ 0xdb, TEXT("SCHED_TROIKA_MELEE_KICK") };
		static const FScheduleMeta MeleeStepback{ 0xd3, TEXT("SCHED_TROIKA_MELEE_STEPBACK") };
		static const FScheduleMeta MeleeIdle{ 0xc7, TEXT("SCHED_TROIKA_MELEE_IDLE") };
		static const FScheduleMeta MeleeAdvance{ 0xca, TEXT("SCHED_TROIKA_MELEE_ADVANCE") };
		static const FScheduleMeta MeleeCircle{ 0xe0, TEXT("SCHED_TROIKA_MELEE_CIRCLE") };
		static const FScheduleMeta Chase{ 0xb1, TEXT("SCHED_TROIKA_CHASE_ENEMY") };
		static const FScheduleMeta ChaseFailed{ 0, TEXT("SCHED_TROIKA_CHASE_ENEMY_FAILED") };
		static const FScheduleMeta RangeAttack1{ 0xec, TEXT("SCHED_TROIKA_RANGE_ATTACK1") };
		static const FScheduleMeta RunAway{ 0xb9, TEXT("SCHED_TROIKA_RUN_AWAY") };
		static const FScheduleMeta SmallFlinch{ 0x14, TEXT("SCHED_SMALL_FLINCH") };
		static const FScheduleMeta AlertSmallFlinch{ 0x07, TEXT("SCHED_ALERT_SMALL_FLINCH") };
		static const FScheduleMeta TakeCoverOrigin{ 0x19, TEXT("SCHED_TAKE_COVER_FROM_ORIGIN") };

		// The scripted-director family. CHOSEN, NOT RECOVERED — the NAMES, and only the names. The
		// survey states that `aiscripted_schedule`'s "move/follow variants use internal schedule IDs
		// 9 or 19, while a special NPC-type branch uses `0x22`" and never says which program takes
		// which number, so neither number is claimed here — 0, the same posture the two combat
		// programs with undecoded registration sites already take. The spellings are this runtime's
		// own and are what `ChangeSchedule`/`StartSchedule` would have to name to reach them; no
		// shipped script names either, which is the honest consequence of not having the real ones.
		static const FScheduleMeta ScriptedMove{ 0, TEXT("SCHED_SCRIPTED_MOVE_TO_GOAL") };
		static const FScheduleMeta ScriptedFollow{ 0, TEXT("SCHED_SCRIPTED_FOLLOW_PATH") };

		// The death program. Its TASK is recovered by number (`TASK_PLAY_DEATH_SEQUENCE` 0x149) and
		// its selection is recovered in prose — "death sound/solid-body policy leads to the death
		// schedule" (`docs/vtmb/combat-and-damage.md`) — but the schedule's own registration site is
		// not decoded, so it takes 0 and the NAME is the identity, the same posture the two combat
		// programs with undecoded registration sites already take.
		static const FScheduleMeta Die{ 0, TEXT("SCHED_DIE") };

		// The post-feed trance. Its number IS decoded, from its one and only producer:
		// `CBaseCombatCharacter::FeedInterrupt` (`0x1033a9e0`) calls `SetSchedule(0xfb)` on a
		// surviving victim. No other `SetSchedule(0xfb)` site exists in `vampire.dll`, and no script,
		// `disciplinetgt` record or vdata file in the shipped install names the string.
		static const FScheduleMeta Mesmerized{ 0xfb, TEXT("SCHED_TROIKA_MESMERIZED") };

		switch (Id)
		{
		case EElysiumScheduleId::IdleDisposition:        return IdleDisposition;
		case EElysiumScheduleId::AlertLookAroundNi:      return AlertLook;
		case EElysiumScheduleId::BackAwayFromDoorNe:     return BackAway;
		case EElysiumScheduleId::BackAwayFromDoorWaitNe: return BackAwayWait;
		case EElysiumScheduleId::TakeCoverHintDoor:      return TakeCover;
		case EElysiumScheduleId::MeleeAttack1:           return MeleeAttack1;
		case EElysiumScheduleId::MeleeAttack1Nr:         return MeleeAttack1Nr;
		case EElysiumScheduleId::MeleeAttack1Swing:      return MeleeSwing;
		case EElysiumScheduleId::MeleeDodge:             return MeleeDodge;
		case EElysiumScheduleId::MeleePreblock:          return MeleePreblock;
		case EElysiumScheduleId::MeleeKick:              return MeleeKick;
		case EElysiumScheduleId::MeleeStepback:          return MeleeStepback;
		case EElysiumScheduleId::MeleeIdle:              return MeleeIdle;
		case EElysiumScheduleId::MeleeAdvance:           return MeleeAdvance;
		case EElysiumScheduleId::MeleeCircle:            return MeleeCircle;
		case EElysiumScheduleId::ChaseEnemy:             return Chase;
		case EElysiumScheduleId::ChaseEnemyFailed:       return ChaseFailed;
		case EElysiumScheduleId::RangeAttack1:           return RangeAttack1;
		case EElysiumScheduleId::RunAway:                return RunAway;
		case EElysiumScheduleId::SmallFlinch:            return SmallFlinch;
		case EElysiumScheduleId::AlertSmallFlinch:       return AlertSmallFlinch;
		case EElysiumScheduleId::TakeCoverFromOrigin:    return TakeCoverOrigin;
		case EElysiumScheduleId::Die:                    return Die;
		case EElysiumScheduleId::Mesmerized:             return Mesmerized;
		case EElysiumScheduleId::ScriptedMoveToGoal:     return ScriptedMove;
		case EElysiumScheduleId::ScriptedFollowPath:     return ScriptedFollow;
		default:                                        return None;
		}
	}

	FElysiumTaskStep Step(EElysiumTask Task, float Param = 0.f)
	{
		FElysiumTaskStep Out;
		Out.Task = Task;
		Out.Param = Param;
		return Out;
	}

	FElysiumTaskStep ActivityStep(const TCHAR* Activity)
	{
		FElysiumTaskStep Out;
		Out.Task = EElysiumTask::SetActivity;
		Out.Activity = Activity;
		return Out;
	}

	// Retail's step-back distance for the near-door schedules. The schedules step back repeatedly
	// rather than pathing to a point, which is why this is a per-task distance and not a goal.
	constexpr float DoorStepBackCm = 64.f;
}

int32 ElysiumScheduleNumber(EElysiumScheduleId Id) { return MetaFor(Id).Number; }
const TCHAR* ElysiumScheduleName(EElysiumScheduleId Id) { return MetaFor(Id).Name; }

const TCHAR* ElysiumTaskName(EElysiumTask Task)
{
	switch (Task)
	{
	case EElysiumTask::SpecialIdleActivity:      return TEXT("TASK_SPECIAL_IDLE_ACTIVITY");
	case EElysiumTask::WaitPvs:                  return TEXT("TASK_WAIT_PVS");
	case EElysiumTask::SetActivity:              return TEXT("TASK_SET_ACTIVITY");
	case EElysiumTask::Wait:                     return TEXT("TASK_WAIT");
	case EElysiumTask::WaitRandom:               return TEXT("TASK_WAIT_RANDOM");
	case EElysiumTask::FaceSavePosition:         return TEXT("TASK_FACE_SAVEPOSITION");
	case EElysiumTask::MoveAwayFromSavePosition: return TEXT("TASK_MOVE_AWAY_PATH");
	case EElysiumTask::SetFailSchedule:          return TEXT("TASK_SET_FAIL_SCHEDULE");
	case EElysiumTask::StopMoving:               return TEXT("TASK_STOP_MOVING");
	case EElysiumTask::SetToleranceDistance:     return TEXT("TASK_SET_TOLERANCE_DISTANCE");
	case EElysiumTask::GetPathToEnemy:           return TEXT("TASK_GET_PATH_TO_ENEMY");
	case EElysiumTask::RunPath:                  return TEXT("TASK_RUN_PATH");
	case EElysiumTask::WaitForMovement:          return TEXT("TASK_WAIT_FOR_MOVEMENT");
	case EElysiumTask::FaceEnemy:                return TEXT("TASK_FACE_ENEMY");
	case EElysiumTask::AnnounceAttack:           return TEXT("TASK_ANNOUNCE_ATTACK");
	case EElysiumTask::MeleeAttack1:             return TEXT("TASK_MELEE_ATTACK1");
	case EElysiumTask::RangeAttack1:             return TEXT("TASK_RANGE_ATTACK1");
	case EElysiumTask::SetSchedule:              return TEXT("TASK_SET_SCHEDULE");
	case EElysiumTask::Remember:                 return TEXT("TASK_REMEMBER");
	case EElysiumTask::MakeOblivious:            return TEXT("TASK_MAKE_OBLIVIOUS");
	case EElysiumTask::SetNpcFlag:               return TEXT("TASK_SET_NPC_FLAG");
	case EElysiumTask::PlayDeathSequence:        return TEXT("TASK_PLAY_DEATH_SEQUENCE");
	case EElysiumTask::GetPathToGoal:            return TEXT("TASK_GET_PATH_TO_GOAL");
	}
	return TEXT("TASK_?");
}

namespace
{
	// The one storage. Non-const only so the test-only interrupt-mask scope below can borrow a
	// program for the length of a case; nothing in the runtime writes it.
	TArray<FElysiumSchedule>& ElysiumScheduleRegistryStorage();
}

const FElysiumSchedule* ElysiumScheduleFor(EElysiumScheduleId Id)
{
	for (const FElysiumSchedule& Schedule : ElysiumScheduleRegistryStorage())
	{
		if (Schedule.Id == Id)
		{
			return &Schedule;
		}
	}
	return nullptr;
}

namespace
{
TArray<FElysiumSchedule>& ElysiumScheduleRegistryStorage()
{
	// Built once. These are the recovered task lists verbatim; the operands are retail's own.
	static TArray<FElysiumSchedule> Registry = []
	{
		TArray<FElysiumSchedule> Out;

		// `TASK_SPECIAL_IDLE_ACTIVITY 5; TASK_WAIT_PVS 0`. The 5 is the activity operand retail
		// passes and the stance machine ignores -- it selects from the disposition table, not from
		// an activity -- so it is carried for fidelity rather than read.
		//
		// The three door-obstruction masks below are left EMPTY on purpose. The interrupt-condition
		// census counts masks across the whole 691-schedule corpus but names none of those three
		// programs' own masks, and an invented mask is a behavioural change wearing a compiled
		// schedule's name. Empty is also a real recovered posture
		// (`FElysiumSchedule::Interrupts`), so the wrong answer there is silent rather than loud:
		// fill one in only from a decoded registration site.
		//
		// CHOSEN, NOT RECOVERED -- the two idle programs' masks, and only those two. Retail's own
		// registration sites for `0x6b` and `0x4f` are not decoded either, but an empty mask on
		// them is not a neutral default: it is the mask under which the enemy transaction's
		// starvation gate refuses every acquisition, so a standing NPC could not enter combat until
		// its idle happened to finish. The census is the evidence for the shape of the answer --
		// `NEW_ENEMY` is declared by 332 of the 691 schedules, `HEAVY_DAMAGE` by 279,
		// `LIGHT_DAMAGE` by 224, `ENEMY_DEAD` by 182 and `HEAR_DANGER`/`HEAR_COMBAT` by 54/29 --
		// so the four commonest stimuli plus the hear family are what an ordinary idle admits.
		// Nothing narrower would let live acquisition happen at all, and nothing wider is
		// defensible from a census. Replace this with the decoded mask, not with an empty one.
		//
		// The four law conditions used to be CHOSEN into this mask by the same argument. They are
		// no longer here, and they are still interrupts: `CAI_BaseNPCTroika::BuildScheduleTestBits`
		// (`0x102ad140`, ported on the runner) overlays them onto EVERY schedule of a non-busy,
		// non-investigating NPC each think -- the flee levels always, the attack levels only with no
		// committed enemy in idle or alert -- together with `COND_INVESTIGATE_LEVEL`, `COMFORT` and
		// `HEAR_FLINCH`. The decoded rule replaces the chosen mark, and it is narrower than the mark
		// was: an NPC with an enemy did not get the attack-level interrupts in retail.
		const FElysiumNpcConditions IdleInterrupts = FElysiumNpcConditions::Of({
			EElysiumNpcCond::NewEnemy, EElysiumNpcCond::EnemyDead,
			EElysiumNpcCond::LightDamage, EElysiumNpcCond::HeavyDamage,
			EElysiumNpcCond::HearCombat, EElysiumNpcCond::HearDanger,
			EElysiumNpcCond::HearPlayer, EElysiumNpcCond::HearWorld });

		FElysiumSchedule& Idle = Out.AddDefaulted_GetRef();
		Idle.Id = EElysiumScheduleId::IdleDisposition;
		Idle.Tasks = { Step(EElysiumTask::SpecialIdleActivity, 5.f), Step(EElysiumTask::WaitPvs) };
		Idle.Interrupts = IdleInterrupts;

		// `SET_ACTIVITY ACT_ALERT_FIDGET_LOOKAROUND; WAIT 3; WAIT_RANDOM 3; SET_ACTIVITY ACT_IDLE;
		//  WAIT_RANDOM 2`.
		FElysiumSchedule& Alert = Out.AddDefaulted_GetRef();
		Alert.Id = EElysiumScheduleId::AlertLookAroundNi;
		Alert.Tasks = {
			ActivityStep(TEXT("ACT_ALERT_FIDGET_LOOKAROUND")),
			Step(EElysiumTask::Wait, 3.f),
			Step(EElysiumTask::WaitRandom, 3.f),
			ActivityStep(TEXT("ACT_IDLE")),
			Step(EElysiumTask::WaitRandom, 2.f),
		};
		Alert.Interrupts = IdleInterrupts;

		// The near-door reaction: face what blocked you, then step back repeatedly. `_NE` is the
		// no-enemy variant; its two enemy-carrying siblings (0x90 / 0x94) are not registered, and
		// the selector records that by name when an NPC with an enemy reaches this policy.
		FElysiumSchedule& BackAway = Out.AddDefaulted_GetRef();
		BackAway.Id = EElysiumScheduleId::BackAwayFromDoorNe;
		BackAway.Tasks = {
			Step(EElysiumTask::FaceSavePosition),
			Step(EElysiumTask::MoveAwayFromSavePosition, DoorStepBackCm),
			Step(EElysiumTask::MoveAwayFromSavePosition, DoorStepBackCm),
			Step(EElysiumTask::WaitRandom, 1.f),
		};

		// Already clear of the obstruction: wait facing the saved position instead of backing away.
		FElysiumSchedule& BackAwayWait = Out.AddDefaulted_GetRef();
		BackAwayWait.Id = EElysiumScheduleId::BackAwayFromDoorWaitNe;
		BackAwayWait.Tasks = {
			Step(EElysiumTask::FaceSavePosition),
			Step(EElysiumTask::Wait, 2.f),
			Step(EElysiumTask::WaitRandom, 2.f),
		};

		// A claimed cover hint. Reaching the hint is the hint machinery's job; what the schedule
		// owns is the pose held once there.
		FElysiumSchedule& Cover = Out.AddDefaulted_GetRef();
		Cover.Id = EElysiumScheduleId::TakeCoverHintDoor;
		Cover.Tasks = {
			ActivityStep(TEXT("ACT_IDLE")),
			Step(EElysiumTask::Wait, 2.f),
			Step(EElysiumTask::WaitRandom, 2.f),
		};
		// Cover that cannot be held falls back to backing away, which is what leaves an NPC out of
		// the doorway either way.
		Cover.FailSchedule = EElysiumScheduleId::BackAwayFromDoorNe;

		return Out;
	}();

	return Registry;
}
}   // namespace

bool ElysiumScheduleIdFromName(const FString& Name, EElysiumScheduleId& OutId)
{
	OutId = EElysiumScheduleId::None;
	if (Name.IsEmpty())
	{
		return false;
	}
	for (const FElysiumSchedule& Schedule : ElysiumScheduleRegistryStorage())
	{
		if (Name.Equals(ElysiumScheduleName(Schedule.Id), ESearchCase::IgnoreCase))
		{
			OutId = Schedule.Id;
			return true;
		}
	}
	return false;
}

void ElysiumSchedule::Register(FElysiumSchedule&& Program)
{
	if (!Program.IsValid())
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("Elysium: refused a schedule registration with no id or no tasks"));
		return;
	}
	TArray<FElysiumSchedule>& Registry = ElysiumScheduleRegistryStorage();
	for (FElysiumSchedule& Existing : Registry)
	{
		if (Existing.Id == Program.Id)
		{
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("Elysium: schedule %s (0x%x) was registered twice; the later program wins"),
				ElysiumScheduleName(Program.Id), ElysiumScheduleNumber(Program.Id));
			Existing = MoveTemp(Program);
			return;
		}
	}
	Registry.Add(MoveTemp(Program));
}

#if WITH_DEV_AUTOMATION_TESTS
ElysiumSchedule::FInterruptMaskScope::FInterruptMaskScope(EElysiumScheduleId Id,
	const FElysiumNpcConditions& Mask)
	: Target(Id)
{
	for (FElysiumSchedule& Schedule : ElysiumScheduleRegistryStorage())
	{
		if (Schedule.Id == Id)
		{
			Previous = Schedule.Interrupts;
			Schedule.Interrupts = Mask;
			bInstalled = true;
			return;
		}
	}
}

ElysiumSchedule::FInterruptMaskScope::~FInterruptMaskScope()
{
	if (!bInstalled)
	{
		return;
	}
	for (FElysiumSchedule& Schedule : ElysiumScheduleRegistryStorage())
	{
		if (Schedule.Id == Target)
		{
			Schedule.Interrupts = Previous;
			return;
		}
	}
}

ElysiumSchedule::FTaskActivityScope::FTaskActivityScope(EElysiumScheduleId Id, int32 TaskIndex,
	const FString& Activity)
	: Target(Id)
	, Index(TaskIndex)
{
	for (FElysiumSchedule& Schedule : ElysiumScheduleRegistryStorage())
	{
		if (Schedule.Id == Id && Schedule.Tasks.IsValidIndex(TaskIndex))
		{
			Previous = Schedule.Tasks[TaskIndex].Activity;
			Schedule.Tasks[TaskIndex].Activity = Activity;
			bInstalled = true;
			return;
		}
	}
}

ElysiumSchedule::FTaskActivityScope::~FTaskActivityScope()
{
	if (!bInstalled)
	{
		return;
	}
	for (FElysiumSchedule& Schedule : ElysiumScheduleRegistryStorage())
	{
		if (Schedule.Id == Target && Schedule.Tasks.IsValidIndex(Index))
		{
			Schedule.Tasks[Index].Activity = Previous;
			return;
		}
	}
}
#endif

namespace
{
	// Start one task. Returns its first result, so a task that completes immediately (a wait of
	// zero, a PVS test that already passes) does not cost a whole think.
	EElysiumTaskResult BeginTask(const FElysiumTaskStep& Step, FElysiumScheduleState& State,
		IElysiumScheduleRunner& Runner, double Now)
	{
		switch (Step.Task)
		{
		case EElysiumTask::SpecialIdleActivity:
		{
			const float Seconds = Runner.RunSpecialIdleActivity(Now);
			if (Seconds < 0.f)
			{
				return EElysiumTaskResult::Failed;
			}
			// The clip's own length is the task's duration: retail re-requests `ACT_DISPOSITION`
			// only once the current sequence has finished, so the task is "hold until this pose is
			// done" rather than a fixed wait.
			State.TaskEndsAt = Now + static_cast<double>(FMath::Max(0.25f, Seconds));
			return EElysiumTaskResult::Running;
		}
		case EElysiumTask::WaitPvs:
			return Runner.IsBodyVisible() ? EElysiumTaskResult::Complete : EElysiumTaskResult::Running;

		case EElysiumTask::SetActivity:
		{
			const float Seconds = Runner.PlayActivity(Step.Activity);
			if (Seconds < 0.f)
			{
				// Troika `StartTask` arm 0x102a1c0f sets the ideal activity and its one-second
				// watchdog. It has neither a TaskComplete nor a TaskFail branch; `RunTask`
				// 0x102aad1f completes when current reaches ideal or that watchdog expires.
				// Keep the body's negative availability result visible without turning it into a
				// schedule failure.
				Runner.RecordScheduleEvent(FString::Printf(
					TEXT("TASK_SET_ACTIVITY %s unresolved (PlayActivity=%.3f); completing (0x102a1c0f)"),
					*Step.Activity, Seconds));
			}
			// Troika stamps m_flWaitFinished to curtime + 1.0 in StartTask. RunTask then completes when
			// current sequence equals ideal, or when this watchdog expires; neither path TaskFails.
			State.TaskEndsAt = Now + 1.0;
			// `MaintainSchedule` invokes RunTask immediately after a still-running StartTask in the same
			// loop iteration. Mirror that first probe here, so a body already standing on the resolved
			// identity advances this think; a miss stays running until a later phase or the watchdog.
			return Runner.IsIdealActivityCurrent() ? EElysiumTaskResult::Complete
				: EElysiumTaskResult::Running;
		}
		case EElysiumTask::Wait:
			State.TaskEndsAt = Now + static_cast<double>(FMath::Max(0.f, Step.Param));
			return Step.Param > 0.f ? EElysiumTaskResult::Running : EElysiumTaskResult::Complete;

		case EElysiumTask::WaitRandom:
		{
			const float Seconds = Runner.RandomSeconds(FMath::Max(0.f, Step.Param));
			State.TaskEndsAt = Now + static_cast<double>(Seconds);
			return Seconds > 0.f ? EElysiumTaskResult::Running : EElysiumTaskResult::Complete;
		}
		case EElysiumTask::FaceSavePosition:
			return Runner.FaceSavePosition() ? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTask::MoveAwayFromSavePosition:
			return Runner.StepAwayFromSavePosition(Step.Param)
				? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		// The combat vocabulary.
		case EElysiumTask::SetFailSchedule:
			// Bookkeeping, not work: it redirects this run's failure route and completes.
			State.FailScheduleOverride = Step.Target;
			return EElysiumTaskResult::Complete;

		case EElysiumTask::SetToleranceDistance:
			State.ToleranceUnits = Step.Param;
			Runner.SetGoalTolerance(Step.Param);
			return EElysiumTaskResult::Complete;

		case EElysiumTask::StopMoving:
			return Runner.BeginStopMovingTask();

		case EElysiumTask::Remember:
			Runner.RememberFact(Step.Param);
			return EElysiumTaskResult::Complete;

		// The incapacitation vocabulary. Both are `StartTask`-only in retail and complete here for
		// the same reason `TASK_SET_ACTIVITY` does: the arm does its write and calls `TaskComplete`.
		case EElysiumTask::MakeOblivious:
			// The operand is the compiler's float: `TRUE`/`ON` -> 1.0, `FALSE`/`OFF` -> 0.0. Retail
			// compares against 0.0 exactly and takes the clear branch on equality.
			Runner.MakeOblivious(Step.Param != 0.f);
			return EElysiumTaskResult::Complete;

		case EElysiumTask::SetNpcFlag:
			Runner.SetNpcFlag(Step.Flag);
			return EElysiumTaskResult::Complete;

		case EElysiumTask::GetPathToEnemy:
			return Runner.GetPathToEnemy(State.ToleranceUnits)
				? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTask::GetPathToGoal:
			return Runner.GetPathToScriptedGoal()
				? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTask::RunPath:
			Runner.RunPath();
			return EElysiumTaskResult::Complete;

		case EElysiumTask::WaitForMovement:
		{
			// Sampled on the first ask too: a body already standing on its goal must not cost the
			// schedule a whole think before the attack task that follows it can run.
			const EElysiumMoveWatch Watch = Runner.WaitForMovement();
			return Watch == EElysiumMoveWatch::Arrived ? EElysiumTaskResult::Complete
				: (Watch == EElysiumMoveWatch::Failed ? EElysiumTaskResult::Failed
					: EElysiumTaskResult::Running);
		}
		case EElysiumTask::FaceEnemy:
			// A turn-in-place completes the task rather than holding it. Retail's own melee approach
			// puts `TASK_STOP_MOVING` after the face and transfers straight to the swing, so the
			// program does not wait on alignment -- and the swing's own opponent acquisition is what
			// decides whether the NPC was pointed at anything.
			return Runner.FaceEnemy() ? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTask::AnnounceAttack:
			return Runner.AnnounceAttack(Step.Param)
				? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTask::MeleeAttack1:
			return Runner.MeleeAttack1() ? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTask::RangeAttack1:
			return Runner.RangeAttack1() ? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTask::SetSchedule:
			// Handled by the caller: a transfer replaces the running program, which is a change to
			// the state this function only advances.
			return EElysiumTaskResult::Complete;

		case EElysiumTask::PlayDeathSequence:
		{
			// The recovered ladder, in order: "try the argument as an activity, then `ACT_DIESIMPLE`,
			// then `ACT_IDLE`, and pass the surviving choice to `SetIdealActivity`"
			// (`docs/vtmb/animation_and_movers.md`). Walked here rather than in the leaf so it is a
			// kernel rule a content-free case can drive; the leaf only answers "can this body play
			// that, and for how long".
			// The two fixed rungs come off the one activity vocabulary rather than being respelled
			// here: `ElysiumAnimIntent::ActivityName` is where this runtime's `ACT_*` literals live.
			const TCHAR* const DieSimple =
				ElysiumAnimIntent::ActivityName(EElysiumAnimActivityCode::DieSimple);
			const TCHAR* const Idle =
				ElysiumAnimIntent::ActivityName(EElysiumAnimActivityCode::Idle);
			const FString Argument = Step.Activity;
			const TCHAR* const Rungs[] = { *Argument, DieSimple, Idle };
			float Seconds = -1.f;
			const TCHAR* Chosen = nullptr;
			for (const TCHAR* Rung : Rungs)
			{
				if (Rung == nullptr || *Rung == TEXT('\0'))
				{
					continue;   // the program named no argument — a rung that is not there, not a miss
				}
				Seconds = Runner.PlayDeathActivity(FString(Rung));
				if (Seconds >= 0.f)
				{
					Chosen = Rung;
					break;
				}
			}
			// Verbose, not a warning: `ACT_DIESIMPLE` is absent from the ENTIRE shipped corpus, so the
			// ladder falling to `ACT_IDLE` is retail's own outcome on every body rather than a
			// resolution that went wrong. An authored absence is not a failure.
			Runner.RecordScheduleEvent(FString::Printf(
				TEXT("TASK_PLAY_DEATH_SEQUENCE arg='%s' -> %s"),
				Argument.IsEmpty() ? TEXT("(none)") : *Argument,
				Chosen != nullptr ? Chosen : TEXT("(nothing resolved)")));
			UE_LOG(LogElysiumNpcEnt, Verbose,
				TEXT("TASK_PLAY_DEATH_SEQUENCE: argument '%s', ACT_DIESIMPLE, ACT_IDLE -> %s"),
				Argument.IsEmpty() ? TEXT("(none)") : *Argument,
				Chosen != nullptr ? Chosen : TEXT("(nothing resolved)"));
			if (Chosen == nullptr)
			{
				// A body whose vocabulary carries none of the three. The program still COMPLETES —
				// the ragdoll handoff that follows it is what death is, and a failed task would send
				// a corpse to a fail schedule instead.
				return EElysiumTaskResult::Complete;
			}
			// CHOSEN, NOT RECOVERED — how long the task holds. The ladder's floor is retail saying
			// "this body has no death performance", and its ragdoll supersedes the choice at once, so
			// waiting out an idle would be waiting on a clip that is not a death. A rung ABOVE the
			// floor is a real death clip and is played through, which is what leaves the handoff the
			// clip's own last frame.
			if (FCString::Stricmp(Chosen, Idle) == 0 || Seconds <= 0.f)
			{
				return EElysiumTaskResult::Complete;
			}
			State.TaskEndsAt = Now + static_cast<double>(Seconds);
			return EElysiumTaskResult::Running;
		}
		}
		return EElysiumTaskResult::Failed;
	}

	// Re-ask a task that reported Running. Only the timed tasks, the PVS hold and the movement watch
	// get here.
	EElysiumTaskResult ContinueTask(const FElysiumTaskStep& Step, FElysiumScheduleState& State,
		IElysiumScheduleRunner& Runner, double Now)
	{
		if (Step.Task == EElysiumTask::WaitPvs)
		{
			return Runner.IsBodyVisible() ? EElysiumTaskResult::Complete : EElysiumTaskResult::Running;
		}
		if (Step.Task == EElysiumTask::StopMoving) return Runner.StopMovingTask();
		if (Step.Task == EElysiumTask::WaitForMovement)
		{
			const EElysiumMoveWatch Watch = Runner.WaitForMovement();
			return Watch == EElysiumMoveWatch::Arrived ? EElysiumTaskResult::Complete
				: (Watch == EElysiumMoveWatch::Failed ? EElysiumTaskResult::Failed
					: EElysiumTaskResult::Running);
		}
		if (Step.Task == EElysiumTask::SetActivity)
		{
			return Runner.IsIdealActivityCurrent() || Now >= State.TaskEndsAt
				? EElysiumTaskResult::Complete : EElysiumTaskResult::Running;
		}
		return Now >= State.TaskEndsAt ? EElysiumTaskResult::Complete : EElysiumTaskResult::Running;
	}

}

bool ElysiumSchedule::Start(FElysiumScheduleState& State, EElysiumScheduleId Id,
	IElysiumScheduleRunner& Runner)
{
	const FElysiumSchedule* Schedule = ElysiumScheduleFor(Id);
	if (Schedule == nullptr || !Schedule->IsValid())
	{
		// A schedule this runtime does not carry is refused by name rather than skipped, so the
		// trace says which one and the NPC selects again instead of silently idling.
		Runner.RecordScheduleEvent(FString::Printf(TEXT("refused schedule %s (%d): not registered"),
			ElysiumScheduleName(Id), ElysiumScheduleNumber(Id)));
		Runner.TaskFail(0x05);
		return false;
	}
	// Everything retail's `CAI_BaseNPC::SetSchedule` (`0x10280e50`) does besides installing the
	// program, in its order. All three producers reach it here rather than at their own call sites,
	// which is what keeps a script's `ChangeSchedule`, a discipline's `AI_Schedule` and the feed's
	// mesmerize install behaving identically.
	//
	// 1. The schedule-change virtual (slot 435). It releases the bits and the obliviousness the
	//    PREVIOUS program was holding, which is how an incapacitating schedule unwinds without
	//    carrying teardown tasks of its own.
	Runner.OnScheduleChange();
	// The old task is visible to the callback, including HitInfo expiry's TaskComplete.
	State.Clear();
	State.Current = Id;
	// 2. Zero the gathered conditions. A stimulus standing at the instant of install is destroyed;
	//    only one the next pass re-observes can interrupt the new program.
	Runner.ClearConditions();
	// 3. `m_bDidMaintainSchedule = false` -- `State.Clear()` above already did it, and it is restated
	//    here because it is a rule of the install rather than a side effect of clearing state.
	State.bDidMaintainSchedule = false;
	Runner.RecordScheduleEvent(FString::Printf(TEXT("schedule %s (0x%x)"),
		ElysiumScheduleName(Id), ElysiumScheduleNumber(Id)));
	Runner.DebugScheduleInstalled(Id);
	return true;
}

FElysiumNpcConditions ElysiumSchedule::EffectiveInterrupts(const FElysiumScheduleState& State,
	IElysiumScheduleRunner& Runner)
{
	// No installed program is no mask. Retail's testers both read `m_ScheduleTestBits` only after
	// checking `m_pSchedule != NULL` (`+0x5c38`), so an NPC between programs lists nothing rather
	// than inheriting whatever the last program cached.
	const FElysiumSchedule* Active = ElysiumScheduleFor(State.Current);
	if (Active == nullptr)
	{
		return FElysiumNpcConditions();
	}
	FElysiumNpcConditions Mask = Active->Interrupts;
	Runner.BuildScheduleTestBits(Mask);
	return Mask;
}

bool ElysiumSchedule::MaskHasCondition(const FElysiumScheduleState& State,
	IElysiumScheduleRunner& Runner, EElysiumNpcCond Cond)
{
	return EffectiveInterrupts(State, Runner).Has(Cond);
}

bool ElysiumSchedule::HasInterruptCondition(const FElysiumScheduleState& State,
	IElysiumScheduleRunner& Runner, const FElysiumNpcConditions& Conditions, EElysiumNpcCond Cond)
{
	return Conditions.Has(Cond) && MaskHasCondition(State, Runner, Cond);
}

bool ElysiumSchedule::Tick(FElysiumScheduleState& State, IElysiumScheduleRunner& Runner, double Now,
	const FElysiumNpcConditions* Conditions, bool bReduced)
{
	if (!State.IsRunning())
	{
		return false;
	}
	// A navigator can invoke TaskFail outside StartTask/RunTask. Its condition is the
	// routing authority, independent of whether the current task would otherwise keep running.
	if (Conditions && Conditions->Has(EElysiumNpcCond::TaskFailed))
	{
		const FElysiumSchedule* Active = ElysiumScheduleFor(State.Current);
		const EElysiumScheduleId Fail = State.FailScheduleOverride != EElysiumScheduleId::None
			? State.FailScheduleOverride : Active ? Active->FailSchedule : EElysiumScheduleId::None;
		if (Fail == EElysiumScheduleId::None || !Start(State, Fail, Runner))
		{
			State.Clear();
			return false;
		}
	}

	// The interrupt check runs at the TOP of the tick, before any task work: a schedule aborted by
	// a new condition must not first advance the task that the condition invalidated. An interrupt
	// ends the program and hands the NPC back to selection -- it deliberately does NOT go through
	// `FailSchedule`, which is task failure's route (see `FElysiumSchedule::Interrupts`).
	if (Conditions != nullptr)
	{
		if (const FElysiumSchedule* Active = ElysiumScheduleFor(State.Current))
		{
			// `DELAY_INTERRUPTS`, exactly as `CAI_BaseNPC::IsScheduleValid` (`0x10280ff0`) spells it:
			// the flag is ANDed with `!m_bDidMaintainSchedule`, so it buys ONE think of immunity and is
			// re-armed by every install. Nothing is latched -- a condition suppressed here is simply
			// not consulted this pass, and a stimulus that persists is re-gathered and fires next
			// think.
			const bool bDelayed = Active->bDelayInterrupts && !State.bDidMaintainSchedule;
			// The effective mask: the authored one plus the runner's per-NPC overlay
			// (`IElysiumScheduleRunner::BuildScheduleTestBits`). Shared with the condition sweeps
			// that consult the same mask -- see `ElysiumSchedule::EffectiveInterrupts`.
			const FElysiumNpcConditions Mask = ElysiumSchedule::EffectiveInterrupts(State, Runner);
			const FElysiumNpcConditions Firing = bDelayed
				? FElysiumNpcConditions() : Mask.Intersection(*Conditions);
			if (bDelayed)
			{
				Runner.RecordScheduleEvent(FString::Printf(
					TEXT("schedule %s (0x%x) DELAY_INTERRUPTS: interrupts held for this think"),
					ElysiumScheduleName(State.Current), ElysiumScheduleNumber(State.Current)));
			}
			if (!Firing.IsEmpty())
			{
				Runner.RecordScheduleEvent(FString::Printf(
					TEXT("schedule %s (0x%x) interrupted by %s"), ElysiumScheduleName(State.Current),
					ElysiumScheduleNumber(State.Current), *Firing.Describe()));
				State.Clear();
				return false;
			}
		}
	}

	// Bounded rather than looping to completion: a schedule whose every task completes instantly
	// would otherwise run the whole program inside one think, and a fail-schedule chain could
	// bounce between two programs forever.
	//
	// The bound is retail's: `MaintainSchedule` (`0x102817c0`, at `0x1028190e`) iterates at most
	// 10 times per call, and continues only while tasks keep COMPLETING (`0x1028212e`) -- so it is a
	// cap on how many tasks may complete in one think, which is exactly what this loop is. Retail
	// re-tests `IsScheduleValid` inside each iteration; this kernel tests once at the top, and the
	// two are equivalent because the only thing that could change the answer mid-loop is an install
	// (`TASK_SET_SCHEDULE`, a fail route), and every install both re-arms the delay window and zeroes
	// the conditions -- so a re-test after one can never fire.
	// ...and at most ONCE on a reduced pass. A reduced think is the AI clock declining to think;
	// letting a chain of instantly-completing tasks run ten deep inside one would spend the whole
	// decision budget the reduction exists to save.
	const int32 MaintainScheduleBound = bReduced ? 1 : 10;
	for (int32 Guard = 0; Guard < MaintainScheduleBound; ++Guard)
	{
		const FElysiumSchedule* Schedule = ElysiumScheduleFor(State.Current);
		if (Schedule == nullptr || !Schedule->Tasks.IsValidIndex(State.TaskIndex))
		{
			// Ran off the end: the schedule completed.
			if (Schedule) Runner.ScheduleDone();
			else Runner.TaskFail(0x05);
			State.Clear();
			return false;
		}

		const FElysiumTaskStep& Step = Schedule->Tasks[State.TaskIndex];
		EElysiumTaskResult Result;
		if (State.bTaskCompletedExternally)
		{
			State.bTaskCompletedExternally = false;
			Result = EElysiumTaskResult::Complete;
		}
		else if (!State.bTaskStarted)
		{
			State.bTaskStarted = true;
			Runner.TaskStarting();
			Result = BeginTask(Step, State, Runner, Now);
		}
		else
		{
			Result = ContinueTask(Step, State, Runner, Now);
		}

		if (Result == EElysiumTaskResult::Running)
		{
			// The pass ran, so the delay window closes -- `MaintainSchedule`'s common exit
			// (`0x102821ae`, the single store at `0x10282342`) sets `m_bDidMaintainSchedule = 1`.
			// Retail has two other returns and neither stores: the `ai_step` debug return, which a
			// shipping session cannot reach, and the "Missing or invalid schedule" error, which is
			// only reachable AFTER the loop's own `SetSchedule` has re-armed the flag or left no
			// schedule at all. So this is the only exit that matters here too: every other path out
			// of this function clears the state or installs a program, and both re-arm the window.
			State.bDidMaintainSchedule = true;
			return true;
		}
		if (Result == EElysiumTaskResult::Complete)
		{
			if (Step.Task == EElysiumTask::SetSchedule)
			{
				// `TASK_SET_SCHEDULE` is the transfer the melee approach uses to hand the NPC to the
				// terminal swing. It is NOT a failure and NOT a return to selection: the program is
				// replaced in place and keeps running this same think, which is what makes
				// "face, stop, then swing" one uninterruptible decision rather than three.
				const EElysiumScheduleId Next = Step.Target;
				if (!ElysiumSchedule::Start(State, Next, Runner))
				{
					State.Clear();
					return false;
				}
				continue;
			}
			++State.TaskIndex;
			State.bTaskStarted = false;
			if (State.TaskIndex == Schedule->Tasks.Num())
			{
				Runner.ScheduleDone();
				State.Clear();
				return false;
			}
			continue;
		}

		// Failed.
		int32 Reason = Runner.TaskFailureReason();
		if (Reason == 0)
		{
			switch (Step.Task)
			{
			case EElysiumTask::GetPathToEnemy: case EElysiumTask::FaceEnemy: Reason = 0x06; break;
			case EElysiumTask::MeleeAttack1: case EElysiumTask::RangeAttack1: Reason = 0x03; break;
			case EElysiumTask::SpecialIdleActivity: Reason = 0x15; break;
			default: Reason = 0x0c; break;
			}
		}
		Runner.TaskFail(Reason);
		Runner.RecordScheduleEvent(FString::Printf(TEXT("task %s failed in %s"),
			ElysiumTaskName(Step.Task), ElysiumScheduleName(State.Current)));
		// `TASK_SET_FAIL_SCHEDULE` wins over the program's declared route when it ran: retail's own
		// chase sets `CHASE_ENEMY_FAILED` from inside the program rather than at its registration.
		const EElysiumScheduleId Fail = State.FailScheduleOverride != EElysiumScheduleId::None
			? State.FailScheduleOverride : Schedule->FailSchedule;
		if (Fail == EElysiumScheduleId::None || !ElysiumSchedule::Start(State, Fail, Runner))
		{
			State.Clear();
			return false;
		}
	}

	// 0x102821ae: reaching the bounded loop's end preserves the task position for next think.
	State.bDidMaintainSchedule = true;
	return true;
}

// `TASK_MOVE_AWAY_PATH`.

const TCHAR* ElysiumSchedule::RetreatResultName(ERetreat Result)
{
	switch (Result)
	{
	case ERetreat::Moving:        return TEXT("moving");
	case ERetreat::NoMotor:       return TEXT("no motor");
	case ERetreat::Degenerate:    return TEXT("no direction to retreat in");
	case ERetreat::Unprojectable: return TEXT("off the navmesh");
	case ERetreat::NotARetreat:   return TEXT("projected point is no longer a retreat");
	case ERetreat::MotorRefused:  return TEXT("the body refused the path");
	}
	return TEXT("unknown");
}

ElysiumSchedule::ERetreat ElysiumSchedule::StepAwayFromSavePosition(IElysiumNpcMotor* Motor,
	const FVector& Origin, const FVector& SavePosition, float DistanceCm, FVector& OutDestination)
{
	if (Motor == nullptr)
	{
		return ERetreat::NoMotor;
	}
	// A step back, not a path to a goal: retail's near-door schedules repeat a short retreat rather
	// than choosing a destination, which is what keeps the NPC out of the swing without it walking
	// off somewhere.
	FVector Away = Origin - SavePosition;
	Away.Z = 0.0;
	if (Away.IsNearlyZero())
	{
		return ERetreat::Degenerate;
	}
	Away.Normalize();
	const FVector Desired = Origin + Away * static_cast<double>(DistanceCm);

	// The extrapolated point is a guess about the world, so the world is asked (S11) instead of the
	// guess being handed straight to MoveTo.
	FVector Destination = Desired;
	if (!Motor->ProjectToNavigable(Desired, Destination))
	{
		return ERetreat::Unprojectable;
	}
	OutDestination = Destination;

	// The re-test, in the horizontal plane the retreat was computed in.
	const double StandingDistance = FVector::Dist2D(Origin, SavePosition);
	const double ProjectedDistance = FVector::Dist2D(Destination, SavePosition);
	if (ProjectedDistance <= StandingDistance + RetreatMarginCm)
	{
		return ERetreat::NotARetreat;
	}

	// A retreat is a walk backwards out of the swing, so it commands the body's own authored walk
	// like every other travel request. Naming no speed is not an option the motor has: it clamps to
	// 1 cm/s, and the step never completes.
	return Motor->MoveTo(Destination, /*AcceptanceRadiusCm=*/16.f,
		ElysiumNpcGait::TravelSpeed(Motor, EElysiumNpcGaitKind::Walk),
		/*bAllowPartialPath=*/false, EElysiumNpcGaitKind::Walk)
		? ERetreat::Moving : ERetreat::MotorRefused;
}
