#include "Substrate/ElysiumSchedule.h"

#include "ElysiumWorldServices.h"          // IElysiumNpcMotor — the reachability query TASK_MOVE_AWAY_PATH asks
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
				return EElysiumTaskResult::Failed;
			}
			// `TASK_SET_ACTIVITY` sets the pose and completes; the schedule's own WAIT steps are
			// what hold it. Playing and completing in one step is the faithful shape.
			return EElysiumTaskResult::Complete;
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

		// --- The combat vocabulary --------------------------------------------------------------
		case EElysiumTask::SetFailSchedule:
			// Bookkeeping, not work: it redirects this run's failure route and completes.
			State.FailScheduleOverride = Step.Target;
			return EElysiumTaskResult::Complete;

		case EElysiumTask::SetToleranceDistance:
			State.ToleranceUnits = Step.Param;
			return EElysiumTaskResult::Complete;

		case EElysiumTask::StopMoving:
			Runner.StopMoving();
			return EElysiumTaskResult::Complete;

		case EElysiumTask::Remember:
			Runner.RememberFact(Step.Param);
			return EElysiumTaskResult::Complete;

		case EElysiumTask::GetPathToEnemy:
			return Runner.GetPathToEnemy(State.ToleranceUnits)
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
		if (Step.Task == EElysiumTask::WaitForMovement)
		{
			const EElysiumMoveWatch Watch = Runner.WaitForMovement();
			return Watch == EElysiumMoveWatch::Arrived ? EElysiumTaskResult::Complete
				: (Watch == EElysiumMoveWatch::Failed ? EElysiumTaskResult::Failed
					: EElysiumTaskResult::Running);
		}
		return Now >= State.TaskEndsAt ? EElysiumTaskResult::Complete : EElysiumTaskResult::Running;
	}

	// How long before this task is worth asking again.
	double DelayFor(const FElysiumTaskStep& Step, const FElysiumScheduleState& State, double Now)
	{
		if (Step.Task == EElysiumTask::WaitPvs)
		{
			// Visibility is not on a clock, so it is polled -- slowly, because an NPC nobody can
			// see is exactly the one whose think budget this task exists to protect.
			return 0.5;
		}
		if (Step.Task == EElysiumTask::WaitForMovement)
		{
			// A travelling body is sampled on the patrol executor's own cadence: the entity's origin
			// is written from the motor at every sample, so a slower poll would leave the logical
			// position visibly behind the body it is chasing an enemy with.
			return 0.05;
		}
		return FMath::Max(0.05, State.TaskEndsAt - Now);
	}
}

bool ElysiumSchedule::Start(FElysiumScheduleState& State, EElysiumScheduleId Id,
	IElysiumScheduleRunner& Runner)
{
	State.Clear();
	const FElysiumSchedule* Schedule = ElysiumScheduleFor(Id);
	if (Schedule == nullptr || !Schedule->IsValid())
	{
		// A schedule this runtime does not carry is refused by name rather than skipped, so the
		// trace says which one and the NPC selects again instead of silently idling.
		Runner.RecordScheduleEvent(FString::Printf(TEXT("refused schedule %s (%d): not registered"),
			ElysiumScheduleName(Id), ElysiumScheduleNumber(Id)));
		return false;
	}
	State.Current = Id;
	Runner.RecordScheduleEvent(FString::Printf(TEXT("schedule %s (0x%x)"),
		ElysiumScheduleName(Id), ElysiumScheduleNumber(Id)));
	return true;
}

bool ElysiumSchedule::Tick(FElysiumScheduleState& State, IElysiumScheduleRunner& Runner, double Now,
	double& OutNextThinkDelay, const FElysiumNpcConditions* Conditions)
{
	OutNextThinkDelay = 0.25;
	if (!State.IsRunning())
	{
		return false;
	}

	// The interrupt check runs at the TOP of the tick, before any task work: a schedule aborted by
	// a new condition must not first advance the task that the condition invalidated. An interrupt
	// ends the program and hands the NPC back to selection -- it deliberately does NOT go through
	// `FailSchedule`, which is task failure's route (see `FElysiumSchedule::Interrupts`).
	if (Conditions != nullptr)
	{
		if (const FElysiumSchedule* Active = ElysiumScheduleFor(State.Current))
		{
			const FElysiumNpcConditions Firing = Active->Interrupts.Intersection(*Conditions);
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
	for (int32 Guard = 0; Guard < 16; ++Guard)
	{
		const FElysiumSchedule* Schedule = ElysiumScheduleFor(State.Current);
		if (Schedule == nullptr || !Schedule->Tasks.IsValidIndex(State.TaskIndex))
		{
			// Ran off the end: the schedule completed.
			State.Clear();
			return false;
		}

		const FElysiumTaskStep& Step = Schedule->Tasks[State.TaskIndex];
		EElysiumTaskResult Result;
		if (!State.bTaskStarted)
		{
			State.bTaskStarted = true;
			Result = BeginTask(Step, State, Runner, Now);
		}
		else
		{
			Result = ContinueTask(Step, State, Runner, Now);
		}

		if (Result == EElysiumTaskResult::Running)
		{
			OutNextThinkDelay = DelayFor(Step, State, Now);
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
				if (Next == EElysiumScheduleId::None || !ElysiumSchedule::Start(State, Next, Runner))
				{
					State.Clear();
					return false;
				}
				continue;
			}
			++State.TaskIndex;
			State.bTaskStarted = false;
			continue;
		}

		// Failed.
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

	// The guard tripped: something completes instantly and re-enters. End the schedule rather than
	// spinning, and say so.
	Runner.RecordScheduleEvent(TEXT("schedule exceeded its per-think task budget"));
	State.Clear();
	return false;
}

// ================================================================================================
// `TASK_MOVE_AWAY_PATH` (11.14)
// ================================================================================================

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

	return Motor->MoveTo(Destination, /*AcceptanceRadiusCm=*/16.f, /*SpeedCmPerSecond=*/0.f)
		? ERetreat::Moving : ERetreat::MotorRefused;
}
