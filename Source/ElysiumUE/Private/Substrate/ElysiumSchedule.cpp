#include "Substrate/ElysiumSchedule.h"

#include "ElysiumAnimationIntent.h"        // the one `ACT_*` vocabulary the death ladder's rungs come off
#include "ElysiumWorldServices.h"          // IElysiumNpcMotor — the reachability query TASK_MOVE_AWAY_PATH asks
#include "Substrate/ElysiumNpcGait.h"      // the authored travel speed a retreat step commands
#include "Substrate/ElysiumNpcLog.h"       // the one `npc_*` log category a refused registration reports on
#include "Substrate/ElysiumScheduleCorpus.h"   // the loaded programs, the id spaces and the activity names
#include "Substrate/ElysiumScheduleNumbers.h"  // the handful of local ids this kernel itself has to name
#include "ElysiumStub.h"                   // the miss arm's tally row, so an unported program stays listed

namespace
{
	/** The one registry: the corpus, loaded on first touch. */
	const FElysiumScheduleCorpus& Corpus()
	{
		FElysiumScheduleCorpus& Loaded = FElysiumScheduleCorpus::Get();
		Loaded.EnsureLoaded();
		return Loaded;
	}

	/** The space a runner with no class of its own runs: `CAI_BaseNPCTroika`'s, which is what
	 *  `ClassScheduleIdSpace()` falls back to for every class with no slot-580 body. */
	const FElysiumLocalIdSpace* FallbackScheduleSpace()
	{
		return Corpus().SpaceFor(TEXT("CAI_BaseNPCTroika"), EElysiumIdCategory::Schedule);
	}
}

const FElysiumScheduleProgram* ElysiumScheduleFor(int32 GlobalId)
{
	if (!ElysiumScheduleId::IsSet(GlobalId))
	{
		return nullptr;
	}
	return Corpus().Manager().FindById(GlobalId);
}

int32 ElysiumScheduleGlobalId(int32 LocalId)
{
	const FElysiumLocalIdSpace* Space = FallbackScheduleSpace();
	return Space != nullptr ? Space->LocalToGlobal(LocalId) : INDEX_NONE;
}

const TCHAR* ElysiumScheduleName(int32 GlobalId)
{
	const FElysiumScheduleProgram* Program = ElysiumScheduleFor(GlobalId);
	return Program != nullptr ? *Program->Name : TEXT("SCHED_?");
}

FString ElysiumScheduleLabel(int32 GlobalId, const IElysiumScheduleRunner* Runner)
{
	if (!ElysiumScheduleId::IsSet(GlobalId))
	{
		return TEXT("SCHED_NONE");
	}
	const int32 Local = Runner != nullptr ? Runner->LocalScheduleId(GlobalId)
		: (FallbackScheduleSpace() != nullptr ? FallbackScheduleSpace()->GlobalToLocal(GlobalId)
			: INDEX_NONE);
	if (Local == INDEX_NONE)
	{
		return FString::Printf(TEXT("%s (%d)"), ElysiumScheduleName(GlobalId), GlobalId);
	}
	return FString::Printf(TEXT("%s (0x%x)"), ElysiumScheduleName(GlobalId), Local);
}

FString ElysiumTaskOperandLabel(const FElysiumScheduleStep& Step)
{
	const FElysiumScheduleCorpus& Loaded = Corpus();
	switch (Loaded.TaskOps().Find(Step.TaskId))
	{
	case EElysiumTaskOp::SetActivity:
	case EElysiumTaskOp::PlayDeathSequence:
	{
		const FString* Name = Loaded.Activities().NameOf(static_cast<int32>(Step.Data));
		return Name != nullptr ? FString::Printf(TEXT("ACTIVITY:%s"), **Name) : FString();
	}
	case EElysiumTaskOp::SetFailSchedule:
	case EElysiumTaskOp::SetSchedule:
	{
		const int32 Local = static_cast<int32>(Step.Data);
		const FElysiumLocalIdSpace* Space = FallbackScheduleSpace();
		const int32 Global = Space != nullptr ? Space->LocalToGlobal(Local) : INDEX_NONE;
		return FString::Printf(TEXT("SCHEDULE:%s (0x%x)"), ElysiumScheduleName(Global), Local);
	}
	case EElysiumTaskOp::SetNpcFlag:
		return FString::Printf(TEXT("NPCFlag:0x%x"), Step.RawWord());
	case EElysiumTaskOp::Remember:
		return FString::Printf(TEXT("Memory:0x%x"),
			static_cast<uint32>(static_cast<int32>(Step.Data)));
	default:
		break;
	}
	return FMath::IsNearlyZero(Step.Data) ? FString() : FString::Printf(TEXT("%.3f"), Step.Data);
}

// `GetScheduleOfType` (`0x102cc260`) and slot 447 (`0x101a6620`) for a runner that carries no class.
int32 IElysiumScheduleRunner::ResolveScheduleId(int32 Id) const
{
	if (ElysiumScheduleId::IsGlobal(Id) && Id != ElysiumScheduleId::Sentinel)
	{
		return Id;
	}
	const FElysiumLocalIdSpace* Space = FallbackScheduleSpace();
	return Space != nullptr ? Space->LocalToGlobal(Id) : INDEX_NONE;
}

int32 IElysiumScheduleRunner::LocalScheduleId(int32 GlobalId) const
{
	const FElysiumLocalIdSpace* Space = FallbackScheduleSpace();
	return Space != nullptr ? Space->GlobalToLocal(GlobalId) : INDEX_NONE;
}

namespace
{
	/** The op this runtime runs for a step, or `Unknown` -- the coverage meter's row. */
	EElysiumTaskOp OpOf(const FElysiumScheduleStep& Step)
	{
		return Corpus().TaskOps().Find(Step.TaskId);
	}

	/** The retail name the step's identity was registered under. Always available, op or no op:
	 *  naming an unported task is the whole point of carrying the identity rather than an enum. */
	const TCHAR* TaskNameOf(const FElysiumScheduleStep& Step)
	{
		const FString& Name = Corpus().TaskOps().NameOf(Step.TaskId);
		return Name.IsEmpty() ? TEXT("TASK_?") : *Name;
	}

	/** `Activity:`'s operand, back as the name `PlayActivity` takes.
	 *
	 *  The two-word task record stores the activity's id in the data word (`0x1025d760` over
	 *  `DAT_1090fbe0`), and this is the reverse lookup that makes it a string again -- the one
	 *  consumer any of the three interning registries has. */
	FString ActivityOf(const FElysiumScheduleStep& Step)
	{
		const FString* Name = Corpus().Activities().NameOf(static_cast<int32>(Step.Data));
		return Name != nullptr ? *Name : FString();
	}

	/** A schedule operand: the class-LOCAL id the parser stored, exactly as retail stores it. */
	int32 ScheduleOperandOf(const FElysiumScheduleStep& Step)
	{
		return static_cast<int32>(Step.Data);
	}

	/** `TASK_FAILED`'s reason where the runner named none (`0x10288780`'s table). One body, called
	 *  from both the StartTask and the RunTask failure arms, which used to carry it twice. */
	int32 TaskFailureReasonFor(EElysiumTaskOp Op, int32 RunnerReason)
	{
		if (RunnerReason != 0)
		{
			return RunnerReason;
		}
		switch (Op)
		{
		case EElysiumTaskOp::GetPathToEnemy:
		case EElysiumTaskOp::FaceEnemy:           return 0x06;
		case EElysiumTaskOp::MeleeAttack1:
		case EElysiumTaskOp::RangeAttack1:        return 0x03;
		case EElysiumTaskOp::SpecialIdleActivity: return 0x15;
		default:                                  return 0x0c;
		}
	}

	// Start one task. Returns its first result, so a task that completes immediately (a wait of
	// zero, a PVS test that already passes) does not cost a whole think.
	EElysiumTaskResult BeginTask(const FElysiumScheduleStep& Step, FElysiumScheduleState& State,
		IElysiumScheduleRunner& Runner, double Now)
	{
		switch (OpOf(Step))
		{
		case EElysiumTaskOp::SpecialIdleActivity:
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
		case EElysiumTaskOp::WaitPvs:
			return Runner.WaitPvs() ? EElysiumTaskResult::Complete : EElysiumTaskResult::Running;

		case EElysiumTaskOp::SetActivity:
		{
			const FString Activity = ActivityOf(Step);
			const float Seconds = Runner.PlayActivity(Activity);
			if (Seconds < 0.f)
			{
				// Troika `StartTask` arm 0x102a1c0f sets the ideal activity and its one-second
				// watchdog. It has neither a TaskComplete nor a TaskFail branch; `RunTask`
				// 0x102aad1f completes when current reaches ideal or that watchdog expires.
				// Keep the body's negative availability result visible without turning it into a
				// schedule failure.
				Runner.RecordScheduleEvent(FString::Printf(
					TEXT("TASK_SET_ACTIVITY %s unresolved (PlayActivity=%.3f); completing (0x102a1c0f)"),
					*Activity, Seconds));
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
		case EElysiumTaskOp::Wait:
			State.TaskEndsAt = Now + static_cast<double>(FMath::Max(0.f, Step.Data));
			return Step.Data > 0.f ? EElysiumTaskResult::Running : EElysiumTaskResult::Complete;

		case EElysiumTaskOp::WaitRandom:
		{
			// `m_flWaitFinished = curtime + RandomFloat(0.1, arg)`: the operand goes to the draw as
			// authored, so `WAIT_RANDOM 0.00` still holds up to 0.1 s.
			const float Seconds = Runner.RandomSeconds(Step.Data);
			State.TaskEndsAt = Now + static_cast<double>(Seconds);
			return Seconds > 0.f ? EElysiumTaskResult::Running : EElysiumTaskResult::Complete;
		}
		case EElysiumTaskOp::FaceSavePosition:
			return Runner.FaceSavePosition() ? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTaskOp::MoveAwayFromSavePosition:
			return Runner.StepAwayFromSavePosition(Step.Data)
				? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		// The combat vocabulary.
		case EElysiumTaskOp::SetFailSchedule:
			// Bookkeeping, not work: it redirects this run's failure route and completes. The
			// operand is stored LOCAL, which is what `m_failSchedule` holds.
			State.FailScheduleOverride = ScheduleOperandOf(Step);
			return EElysiumTaskResult::Complete;

		case EElysiumTaskOp::SetToleranceDistance:
			State.ToleranceUnits = Step.Data;
			Runner.SetGoalTolerance(Step.Data);
			return EElysiumTaskResult::Complete;

		case EElysiumTaskOp::StopMoving:
			return Runner.BeginStopMovingTask();

		case EElysiumTaskOp::Remember:
			// `Memory:` resolves through the signed-converted path, so the word comes back out of
			// the float the same way it went in.
			Runner.RememberFact(static_cast<uint32>(static_cast<int32>(Step.Data)));
			return EElysiumTaskResult::Complete;

		case EElysiumTaskOp::MakeOblivious:
			// The operand is the compiler's float: `TRUE`/`ON` -> 1.0, `FALSE`/`OFF` -> 0.0. Retail
			// compares against 0.0 exactly and takes the clear branch on equality.
			Runner.MakeOblivious(Step.Data != 0.f);
			return EElysiumTaskResult::Complete;

		case EElysiumTaskOp::SetNpcFlag:
			// One of the three prefixes that store the raw 32-bit word rather than a converted
			// float, which is why this reads `RawWord` and its neighbours read `Data`.
			Runner.SetNpcFlag(static_cast<EElysiumNpcFlag>(Step.RawWord()));
			return EElysiumTaskResult::Complete;

		case EElysiumTaskOp::GetPathToEnemy:
			return Runner.GetPathToEnemy(State.ToleranceUnits)
				? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTaskOp::GetPathToGoal:
			return Runner.GetPathToScriptedGoal()
				? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTaskOp::FindCoverFromEnemy:
			return Runner.FindCoverFromEnemy()
				? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTaskOp::RunPath:
			Runner.RunPath();
			return EElysiumTaskResult::Complete;

		case EElysiumTaskOp::WaitForMovement:
		{
			// Sampled on the first ask too: a body already standing on its goal must not cost the
			// schedule a whole think before the attack task that follows it can run.
			const EElysiumMoveWatch Watch = Runner.WaitForMovement();
			return Watch == EElysiumMoveWatch::Arrived ? EElysiumTaskResult::Complete
				: (Watch == EElysiumMoveWatch::Failed ? EElysiumTaskResult::Failed
					: EElysiumTaskResult::Running);
		}
		case EElysiumTaskOp::FaceEnemy:
			// A turn-in-place completes the task rather than holding it. Retail's own melee approach
			// puts `TASK_STOP_MOVING` after the face and transfers straight to the swing, so the
			// program does not wait on alignment -- and the swing's own opponent acquisition is what
			// decides whether the NPC was pointed at anything.
			return Runner.FaceEnemy() ? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTaskOp::AnnounceAttack:
			return Runner.AnnounceAttack(Step.Data)
				? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTaskOp::MeleeAttack1:
			return Runner.MeleeAttack1() ? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTaskOp::RangeAttack1:
			return Runner.RangeAttack1() ? EElysiumTaskResult::Complete : EElysiumTaskResult::Failed;

		case EElysiumTaskOp::SetSchedule:
			// Handled by the caller: a transfer replaces the running program, which is a change to
			// the state this function only advances.
			return EElysiumTaskResult::Complete;

		case EElysiumTaskOp::PlayDeathSequence:
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
			const FString Argument = ActivityOf(Step);
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

		case EElysiumTaskOp::Unknown:
			break;
		}

		// The corpus named a task this runtime carries no body for. This is the coverage meter's
		// dynamic half: the identity is named, the stub is tallied, and the task fails by name --
		// which is exactly what the old closed enum could not do, because a task outside it could
		// not be represented at all.
		Runner.RecordScheduleEvent(FString::Printf(TEXT("task %s has no body in this runtime"),
			TaskNameOf(Step)));
		ElysiumStub::Fired(TEXT("schedule-task"), TaskNameOf(Step), FString(),
			FString::Printf(TEXT("%d"), Step.TaskId),
			TEXT("the story that builds the task body; the step fails by name"));
		return EElysiumTaskResult::Failed;
	}

	// Re-ask a task that reported Running. Only the timed tasks, the PVS hold and the movement watch
	// get here.
	EElysiumTaskResult ContinueTask(const FElysiumScheduleStep& Step, FElysiumScheduleState& State,
		IElysiumScheduleRunner& Runner, double Now)
	{
		switch (OpOf(Step))
		{
		case EElysiumTaskOp::WaitPvs:
			return Runner.WaitPvs() ? EElysiumTaskResult::Complete : EElysiumTaskResult::Running;
		case EElysiumTaskOp::StopMoving:
			return Runner.StopMovingTask();
		case EElysiumTaskOp::WaitForMovement:
		{
			const EElysiumMoveWatch Watch = Runner.WaitForMovement();
			return Watch == EElysiumMoveWatch::Arrived ? EElysiumTaskResult::Complete
				: (Watch == EElysiumMoveWatch::Failed ? EElysiumTaskResult::Failed
					: EElysiumTaskResult::Running);
		}
		case EElysiumTaskOp::SetActivity:
			return Runner.IsIdealActivityCurrent() || Now >= State.TaskEndsAt
				? EElysiumTaskResult::Complete : EElysiumTaskResult::Running;
		default:
			break;
		}
		return Now >= State.TaskEndsAt ? EElysiumTaskResult::Complete : EElysiumTaskResult::Running;
	}

	// The fail route, `0x10281730`: `GetFailSchedule` (slot 439, `0x1028abe0`, no override on any of
	// 79 classes) answers `m_failSchedule ? m_failSchedule : 0x43 FAIL`, and `SetSchedule(int)`
	// (`0x102cc1f0`) then runs slot 440 on it before the lookup. Both numbers are class-LOCAL, which
	// is why nothing here translates to a global: `Start` does that.
	//
	// A program declares no fail route of its own. That used to be a field on the port's schedule
	// type and it was an invention: no retail schedule text carries one, and the only way a route is
	// redirected is `TASK_SET_FAIL_SCHEDULE` running from inside the program -- which is exactly what
	// retail's chase does when it sets `CHASE_ENEMY_FAILED`.
	int32 FailScheduleFor(const FElysiumScheduleState& State, IElysiumScheduleRunner& Runner)
	{
		const int32 Fail = State.FailScheduleOverride != ElysiumScheduleId::None
			? State.FailScheduleOverride : ElysiumSched::FAIL;
		return Runner.TranslateSchedule(Fail);
	}
}

bool ElysiumSchedule::Start(FElysiumScheduleState& State, int32 Id,
	IElysiumScheduleRunner& Runner)
{
	// `GetScheduleOfType` (`0x102cc260`) translates first: a number below 1e9, and -1, goes through
	// slot 580's schedule space; a global id is already the answer.
	const int32 GlobalId = Runner.ResolveScheduleId(Id);
	const FElysiumScheduleProgram* Schedule = ElysiumScheduleFor(GlobalId);
	if (Schedule == nullptr || Schedule->Tasks.IsEmpty())
	{
		// `SetSchedule(int)` (`0x102cc1f0`) → `GetScheduleOfType` (`0x102cc260`) misses: retail
		// DevMsgs and installs base schedule 1 in its place. No TaskFail; the NPC stands.
		//
		// In retail the miss is a shipped defect; here it is more often a program this runtime has
		// not registered yet, and standing in `IDLE_STAND` would hide that behind a retail-shaped
		// trace row. The tally keeps the unported program on the work list.
		Runner.RecordScheduleEvent(FString::Printf(
			TEXT("GetScheduleOfType(): No CASE for 0x%x (global %d); installing IDLE_STAND"),
			Id, GlobalId));
		ElysiumStub::Fired(TEXT("schedule"),
			FString::Printf(TEXT("SetSchedule(0x%x)"), Id), FString(),
			FString::Printf(TEXT("%d"), GlobalId),
			TEXT("the corpus text that carries the program; IDLE_STAND stands in"));
		if (Id == ElysiumSched::IDLE_STAND)
		{
			return false;   // the fallback itself is missing: a build defect, not a retail path
		}
		return Start(State, ElysiumSched::IDLE_STAND, Runner);
	}
	Install(State, GlobalId, Runner);
	return true;
}

void ElysiumSchedule::Install(FElysiumScheduleState& State, int32 GlobalId,
	IElysiumScheduleRunner& Runner)
{
	// A clear asked for before this install is superseded by it: retail's `ClearSchedule` then
	// `SetSchedule` leaves the new program standing, so the request cannot outlive the install.
	Runner.TakeClearScheduleRequest();
	// A null pointer is a real input to `CAI_BaseNPC::SetSchedule(CAI_Schedule*)`; the selection
	// loop uses it to clear the outgoing program before trying the selector once more.
	const FElysiumScheduleProgram* Schedule = ElysiumScheduleFor(GlobalId);
	check(GlobalId == ElysiumScheduleId::None
		|| (Schedule != nullptr && !Schedule->Tasks.IsEmpty()));
	// Everything retail's `CAI_BaseNPC::SetSchedule` (`0x10280e50`) does besides installing the
	// program, in its order. All three producers reach it here rather than at their own call sites,
	// which is what keeps a script's `ChangeSchedule`, a discipline's `AI_Schedule` and the feed's
	// mesmerize install behaving identically.
	//
	// 1. The schedule-change virtual (slot 435). It releases the bits and the obliviousness the
	//    PREVIOUS program was holding, which is how an incapacitating schedule unwinds without
	//    carrying teardown tasks of its own.
	Runner.OnScheduleChange(GlobalId);                                   // 0x10280e53
	// The old task is visible to the callback, including HitInfo expiry's TaskComplete.
	State.Clear();
	State.Current = Schedule != nullptr ? GlobalId : ElysiumScheduleId::None;
	const double Now = Runner.ScheduleTime();
	State.ScheduleStartedAt = Now;                                      // 0x10280e69, +0x5c48
	State.TaskStartedAt = Now;                                          // 0x10280e73, +0x5c4c
	// 2. Zero the gathered conditions. A stimulus standing at the instant of install is destroyed;
	//    only one the next pass re-observes can interrupt the new program.
	Runner.ClearConditions();
	// 3. `m_bDidMaintainSchedule = false` -- `State.Clear()` above already did it, and it is restated
	//    here because it is a rule of the install rather than a side effect of clearing state.
	State.bDidMaintainSchedule = false;
	if (Schedule != nullptr)
	{
		Runner.RecordScheduleEvent(FString::Printf(TEXT("schedule %s"),
			*ElysiumScheduleLabel(GlobalId, &Runner)));
		Runner.DebugScheduleInstalled(GlobalId);
	}
}

void ElysiumSchedule::ClearSchedule(FElysiumScheduleState& State, IElysiumScheduleRunner& Runner)
{
	// `0x10280d30`, in order: zero `+0x5c38..+0x5c4c` (program, schedule id, task index, status, both
	// stamps), clear `PRESERVE_PATH`, dispatch slot 435 with NULL. The conditions are untouched, and
	// so is `m_failSchedule` (`+0x5c54`): a `TASK_FAILED` still standing on the next pass routes to
	// whatever `TASK_SET_FAIL_SCHEDULE` last wrote, not to base `FAIL`.
	const int32 Cleared = State.Current;
	const int32 KeptFailSchedule = State.FailScheduleOverride;
	State.Clear();
	State.FailScheduleOverride = KeptFailSchedule;
	Runner.ClearPreservePath();
	Runner.OnScheduleChange(ElysiumScheduleId::None);
	Runner.RecordScheduleEvent(FString::Printf(TEXT("ClearSchedule: %s cleared"),
		*ElysiumScheduleLabel(Cleared, &Runner)));
}

FElysiumNpcConditions ElysiumSchedule::EffectiveInterrupts(const FElysiumScheduleState& State,
	IElysiumScheduleRunner& Runner)
{
	// No installed program is no mask. Retail's testers both read `m_ScheduleTestBits` only after
	// checking `m_pSchedule != NULL` (`+0x5c38`), so an NPC between programs lists nothing rather
	// than inheriting whatever the last program cached.
	const FElysiumScheduleProgram* Active = ElysiumScheduleFor(State.Current);
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

void IElysiumScheduleRunner::NextScheduledTaskForMaintenance(FElysiumScheduleState& State)
{
	State.TaskStatus = EElysiumTaskStatus::New;
	++State.TaskIndex;
	const FElysiumScheduleProgram* Schedule = ElysiumScheduleFor(State.Current);
	if (Schedule == nullptr || !Schedule->Tasks.IsValidIndex(State.TaskIndex))
	{
		ScheduleDone();
	}
}

bool ElysiumSchedule::Tick(FElysiumScheduleState& State, IElysiumScheduleRunner& Runner, double Now,
	const FElysiumNpcConditions* Conditions, bool bReduced)
{
	// A clear requested outside a task is consumed before any more work.
	if (Runner.TakeClearScheduleRequest())
	{
		ElysiumSchedule::ClearSchedule(State, Runner);
		// Retail's outside caller has already executed ClearSchedule before it enters this body;
		// the null program therefore reaches the invalid/reselect arm in this same pass.
	}
	auto HasCondition = [&Runner, &Conditions](EElysiumNpcCond Cond)
	{
		return (Conditions != nullptr && Conditions->Has(Cond))
			|| Runner.HasMaintenanceCondition(Cond);
	};

	// The listing converts _DAT_1049a148 (8.0 ms) to cycles once, then compares after every
	// completed task. FPlatformTime supplies the same monotonic budget without exporting RDTSC.
	const double StartedAt = FPlatformTime::Seconds();                    // 0x10281907
	constexpr double CycleBudgetSeconds = 0.008;                         // _DAT_1049a148 = 8.0 ms
	const int32 MaintainScheduleBound = bReduced ? 1 : 10;                // 0x1028190e
	for (int32 Guard = 0; Guard < MaintainScheduleBound; ++Guard)
	{
		bool bCompletedScheduleAtTop = false;
		if (State.IsRunning() && State.TaskStatus == EElysiumTaskStatus::Complete)
		{
			Runner.NextScheduledTaskForMaintenance(State);                   // 0x10281980
			const FElysiumScheduleProgram* Advanced = ElysiumScheduleFor(State.Current);
			bCompletedScheduleAtTop = Advanced == nullptr
				|| !Advanced->Tasks.IsValidIndex(State.TaskIndex);
			if (Runner.IsAiStepMode())                                      // 0x10281987
			{
				Runner.AdvanceAiStepDebugIndex();                              // 0x102821f5
				return State.IsRunning();                                      // 0x10282269
			}
			if (Runner.TakeExternalExecutorReturn())
			{
				// The port's out-of-table executor resumes at the caller boundary. This is the old
				// ScheduleDone handoff moved to the exact completion edge; ordinary schedules do not
				// take it and continue through retail's reselect block below.
				Install(State, ElysiumScheduleId::None, Runner);
				return false;
			}
		}

		const bool bTaskFailed = HasCondition(EElysiumNpcCond::TaskFailed);
		const bool bScheduleDone = bCompletedScheduleAtTop
			|| HasCondition(EElysiumNpcCond::ScheduleDone);
		bool bScheduleValid = State.IsRunning();                            // 0x10280ff0
		if (bScheduleValid && Runner.IsSpecialNavigation())                 // 0x10281023
		{
			if (bTaskFailed || bScheduleDone)
			{
				Runner.MarkSpecialNavigationScheduleEnd();                     // 0x10281045
				bScheduleValid = false;
			}
		}
		else if (bScheduleValid && Runner.ConsumeChooseNewSchedule())       // 0x10281075
		{
			bScheduleValid = false;
		}
		else if (bScheduleValid)
		{
			const FElysiumScheduleProgram* Active = ElysiumScheduleFor(State.Current);
			const bool bDelayed = Active != nullptr
				&& Active->HasFlag(ElysiumScheduleFlags::DelayInterrupts)
				&& !State.bDidMaintainSchedule;                               // 0x102819c7
			if (bDelayed)
			{
				Runner.RecordScheduleEvent(FString::Printf(
					TEXT("schedule %s DELAY_INTERRUPTS: interrupts held for this think"),
					*ElysiumScheduleLabel(State.Current, &Runner)));
			}
			else if (Conditions != nullptr && Active != nullptr)
			{
				// `IsScheduleValid` (`0x10280ff0`) evaluates BOTH masks:
				//
				//     (conds & m_ScheduleTestBits) | (~conds & m_ScheduleInvertedTestBits)
				//
				// so a `!COND_*` interrupt fires on the ABSENCE of its condition. The per-NPC overlay is
				// not part of the inverted word -- `SetScheduleTestBits` (`0x10269eb0`) ORs into the
				// normal one only -- which is why the inverted mask is read off the program directly.
				FElysiumNpcConditions Firing =
					EffectiveInterrupts(State, Runner).Intersection(*Conditions);
				Firing |= Active->InvertedInterrupts.Difference(*Conditions);
				if (!Firing.IsEmpty())
				{
					Runner.RecordScheduleEvent(FString::Printf(
						TEXT("schedule %s interrupted by %s"),
						*ElysiumScheduleLabel(State.Current, &Runner),
						*Firing.Describe()));
					bScheduleValid = false;                                     // 0x10281340
				}
			}
			if (bTaskFailed || bScheduleDone)
			{
				bScheduleValid = false;                                      // 0x10281213
			}
		}

		const bool bStateMismatchAtEntry = Runner.ScheduleStateDiffersFromIdeal();
		if (!bScheduleValid || bStateMismatchAtEntry)                       // 0x102819da
		{
			Runner.PrepareScheduleReselect();                                // 0x102819f4
			const bool bDoorBlocks = Runner.ConsumeBlockedDoorForSchedule(Now); // 0x10281a04
			const bool bStateMismatch = Runner.ScheduleStateDiffersFromIdeal();
			if (!bTaskFailed || bStateMismatch || bDoorBlocks)                // 0x10281a6c
			{
				Runner.CommitIdealStateForSchedule();                           // 0x10281b5a
				int32 IdealRetail = 0;
				const int32 Selected =
					Runner.SelectScheduleForMaintenance(Now, IdealRetail);         // 0x10281b89
				// `m_IdealSchedule` (`+0x5c3c`) is retail's GLOBAL stamp: `0x10280de0` translates a
				// number below 1e9 through slot 580 before storing it. The selectors answer local
				// numbers, so the translate belongs here.
				Runner.SetIdealScheduleForMaintenance(
					Runner.ResolveScheduleId(IdealRetail));                        // 0x102814d0
				if (Runner.TakeExternalExecutorReturn())
				{
					// Patrol and interesting-place schedules live outside this registry. Their selector
					// answer is represented by the owning executor, so hand the null answer to the caller
					// at the same selection edge instead of treating it as a registry miss.
					Install(State, ElysiumScheduleId::None, Runner);                  // 0x10281be5 adapter
					return false;
				}
				if (Selected != ElysiumScheduleId::None)
				{
					Start(State, Selected, Runner);                               // 0x10281be5
				}
				else
				{
					// A selector with no opinion installs nothing, which is the null program retail's
					// `SetSchedule(CAI_Schedule*)` takes and the selection loop's own way of clearing.
					Install(State, ElysiumScheduleId::None, Runner);
				}
			}
			else
			{
				const int32 Fail = FailScheduleFor(State, Runner);               // 0x10281ab8
				Runner.SetIdealScheduleForMaintenance(
					Runner.ResolveScheduleId(Fail));                               // 0x10281ac1
				Start(State, Fail, Runner);                                      // 0x10281730
			}
			// SetSchedule cleared the live condition set, so the old pass snapshot is spent.
			Conditions = nullptr;
		}

		if (!State.IsRunning())                                             // 0x10281c1d
		{
			int32 IdealRetail = 0;
			const int32 Selected =
				Runner.SelectScheduleForMaintenance(Now, IdealRetail);          // 0x10281c46
			Runner.SetIdealScheduleForMaintenance(Runner.ResolveScheduleId(IdealRetail));
			if (Runner.TakeExternalExecutorReturn())
			{
				Install(State, ElysiumScheduleId::None, Runner);                  // 0x10281ca6 adapter
				return false;
			}
			if (Selected != ElysiumScheduleId::None)
			{
				Start(State, Selected, Runner);                                 // 0x10281ca6
			}
		}

		const FElysiumScheduleProgram* Schedule = ElysiumScheduleFor(State.Current);
		if (Schedule == nullptr || Schedule->Tasks.IsEmpty())               // 0x10281ce4
		{
			Runner.MissingSchedule();                                        // 0x1028226c
			return false;                                                     // 0x10282336
		}

		if (State.TaskStatus == EElysiumTaskStatus::New)                    // 0x10281cf7
		{
			if (State.TaskIndex == 0)
			{
				int32 Local = Runner.LocalScheduleIdForStart(State.Current);     // 0x10281d17
				if (Local == INDEX_NONE)
				{
					Local = State.Current;   // no space holds it: the global id is the only number there is
				}
				Runner.MaintenanceOnStartSchedule(Local);                       // 0x10281d29
			}
			const FElysiumScheduleStep& Step = Schedule->Tasks[State.TaskIndex];
			Runner.DebugTaskStart(Step);                                      // 0x10281d5c
			State.TaskStatus = EElysiumTaskStatus::Running;                    // 0x10281d91
			Runner.TaskStarting();                                            // +0x5c50 = 0
			State.TaskStartedAt = Now;                                        // 0x10281da2, +0x5c4c
			const EElysiumTaskResult Result = BeginTask(Step, State, Runner, Now); // 0x10281e10
			const bool bClearedByStartTask = Runner.TakeClearScheduleRequest();
			if (bClearedByStartTask)
			{
				ClearSchedule(State, Runner);
			}
			else if (Result == EElysiumTaskResult::Complete)
			{
				if (OpOf(Step) == EElysiumTaskOp::SetSchedule)
				{
					Start(State, ScheduleOperandOf(Step), Runner);
					Conditions = nullptr;
					continue;
				}
				State.TaskStatus = EElysiumTaskStatus::Complete;
			}
			else if (Result == EElysiumTaskResult::Failed)
			{
				Runner.TaskFail(TaskFailureReasonFor(OpOf(Step), Runner.TaskFailureReason()));
				Runner.RecordScheduleEvent(FString::Printf(TEXT("task %s failed in %s"),
					TaskNameOf(Step), *ElysiumScheduleLabel(State.Current, &Runner)));
			}
			const bool bRunning = State.TaskStatus != EElysiumTaskStatus::Complete
				&& State.TaskStatus != EElysiumTaskStatus::RunningMovement;
			if (bRunning && !HasCondition(EElysiumNpcCond::TaskFailed))
			{
				Runner.MaintenanceStartTaskOverlay();                           // 0x10281e89
			}
		}

		Runner.MaintainActivity();                                         // 0x10281eee
		if (State.TaskStatus != EElysiumTaskStatus::Complete
			&& State.TaskStatus != EElysiumTaskStatus::New)                   // 0x10281f26
		{
			const bool bRunning = State.TaskStatus != EElysiumTaskStatus::RunningMovement;
			if (!bRunning || HasCondition(EElysiumNpcCond::TaskFailed))
			{
				break;                                                          // 0x102821ae
			}
			const FElysiumScheduleStep& Step = Schedule->Tasks[State.TaskIndex];
			const EElysiumTaskResult Result = ContinueTask(Step, State, Runner, Now); // 0x1028202c
			const bool bClearedByRunTask = Runner.TakeClearScheduleRequest();
			if (bClearedByRunTask)
			{
				ClearSchedule(State, Runner);
			}
			else if (Result == EElysiumTaskResult::Complete)
			{
				if (OpOf(Step) == EElysiumTaskOp::SetSchedule)
				{
					Start(State, ScheduleOperandOf(Step), Runner);
					Conditions = nullptr;
					continue;
				}
				State.TaskStatus = EElysiumTaskStatus::Complete;
			}
			else if (Result == EElysiumTaskResult::Failed)
			{
				Runner.TaskFail(TaskFailureReasonFor(OpOf(Step), Runner.TaskFailureReason()));
				Runner.RecordScheduleEvent(FString::Printf(TEXT("task %s failed in %s"),
					TaskNameOf(Step), *ElysiumScheduleLabel(State.Current, &Runner)));
			}
			const bool bStillRunning = State.TaskStatus != EElysiumTaskStatus::Complete
				&& State.TaskStatus != EElysiumTaskStatus::RunningMovement;
			if (bStillRunning && !HasCondition(EElysiumNpcCond::TaskFailed))
			{
				if (Runner.MaintenanceIsCurTaskContinuousMove())                // 0x102820dc
				{
					Runner.RememberContinuousMove();                               // 0x102820e6
				}
				Runner.RunTaskOverlay();                                         // 0x102820f2
			}
			if (State.TaskStatus != EElysiumTaskStatus::Complete)
			{
				break;                                                          // 0x10282137
			}
		}

		if (State.TaskStatus != EElysiumTaskStatus::Complete
			&& State.TaskStatus != EElysiumTaskStatus::New)
		{
			break;
		}
		if ((FPlatformTime::Seconds() - StartedAt) > CycleBudgetSeconds)    // 0x10282179
		{
			break;
		}
	}

	Runner.MaintainActivity();                                           // 0x102821ae
	if (Runner.IsAiStepMode())
	{
		Runner.FreezeForAiStep();                                          // 0x102821c2
	}
	State.bDidMaintainSchedule = true;                                   // 0x10282342
	return State.IsRunning();
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

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	/** A borrow scope's target: the loaded program a class-local number names. */
	FElysiumScheduleProgram* BorrowTarget(int32 LocalId, int32& OutGlobalId)
	{
		const FElysiumLocalIdSpace* Space = FallbackScheduleSpace();
		OutGlobalId = Space != nullptr ? Space->LocalToGlobal(LocalId) : INDEX_NONE;
		return FElysiumScheduleCorpus::Get().MutableProgram(OutGlobalId);
	}
}

ElysiumSchedule::FInterruptMaskScope::FInterruptMaskScope(int32 LocalId,
	const FElysiumNpcConditions& Mask)
{
	if (FElysiumScheduleProgram* Program = BorrowTarget(LocalId, Target))
	{
		Previous = Program->Interrupts;
		Program->Interrupts = Mask;
		bInstalled = true;
	}
}

ElysiumSchedule::FInterruptMaskScope::~FInterruptMaskScope()
{
	if (!bInstalled)
	{
		return;
	}
	if (FElysiumScheduleProgram* Program = FElysiumScheduleCorpus::Get().MutableProgram(Target))
	{
		Program->Interrupts = Previous;
	}
}

ElysiumSchedule::FTaskActivityScope::FTaskActivityScope(int32 LocalId, int32 TaskIndex,
	const FString& Activity)
	: Index(TaskIndex)
{
	FElysiumScheduleProgram* Program = BorrowTarget(LocalId, Target);
	if (Program != nullptr && Program->Tasks.IsValidIndex(TaskIndex))
	{
		Previous = Program->Tasks[TaskIndex].Data;
		// The word is an ACTIVITY id, so the name is interned first -- exactly what a parse does
		// with an `ACTIVITY:` operand.
		Program->Tasks[TaskIndex].Data = static_cast<float>(
			const_cast<FElysiumSymbolRegistry&>(FElysiumScheduleCorpus::Get().Activities())
				.Intern(Activity));
		bInstalled = true;
	}
}

ElysiumSchedule::FTaskActivityScope::~FTaskActivityScope()
{
	if (!bInstalled)
	{
		return;
	}
	FElysiumScheduleProgram* Program = FElysiumScheduleCorpus::Get().MutableProgram(Target);
	if (Program != nullptr && Program->Tasks.IsValidIndex(Index))
	{
		Program->Tasks[Index].Data = Previous;
	}
}
#endif
