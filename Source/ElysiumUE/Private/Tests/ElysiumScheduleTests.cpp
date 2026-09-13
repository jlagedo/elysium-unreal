// The schedule/task kernel, asserted against a recording runner with no world and no engine.
//
// The kernel is deliberately content-free — task bodies reach the world through
// `IElysiumScheduleRunner` — so the whole task program of a recovered schedule can be run and
// asserted here. The behaviour under test is `docs/vtmb/npc-ai/conditions-and-states.md` ->
// "The idle branch, decided" (the two idle schedules) and "Door-obstruction schedule selection".
//
// Three shapes worth holding onto while reading:
//
//   - a task runs across thinks. `Tick` answers "still running", and the caller waits the delay the
//     kernel hands back rather than re-asking every frame.
//   - `TASK_WAIT_PVS` is the only task that is not on a clock. It holds indefinitely, which is the
//     throttle the idle schedule exists to apply to an NPC nobody can see.
//   - a failed task ends its pass with the program installed; the next pass goes to the schedule's
//     fail schedule, and a schedule with none to base `FAIL` (0x43), through slot 440 (story 25).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumNpc.h"          // the Troika slot-440 arm, asked on a stood-up leaf
#include "Tests/ElysiumNpcTestFixture.h"   // the world that leaf stands in
#include "Tests/ElysiumTestServices.h"   // the recording motor TASK_MOVE_AWAY_PATH projects through

static constexpr EAutomationTestFlags GElysiumScheduleTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One line per task body call, so a program's shape is asserted rather than its side effects.
	struct FRecordingRunner final : IElysiumScheduleRunner
	{
		FRecordingRunner() { Motor.Calls = &Calls; }

		TArray<FString> Calls;
		TArray<int32> FailureReasons;
		int32 CompletedSchedules = 0;
		const FElysiumScheduleState* ObservedState = nullptr;
		TArray<EElysiumScheduleId> OutgoingSchedules;
		bool bPathAvailable = false;
		EElysiumMoveWatch MovementResult = EElysiumMoveWatch::Failed;
		virtual bool GetPathToScriptedGoal() override { return bPathAvailable; }
		virtual EElysiumMoveWatch WaitForMovement() override { return MovementResult; }
		// The one condition the kernel itself raises. `TaskFail` (`0x10273fc0`) sets `TASK_FAILED` and
		// the install (`SetSchedule`) zeroes it; a case passes `&Conditions` on the tick after a
		// failure, as the NPC's think passes its own gathered set.
		FElysiumNpcConditions Conditions;
		virtual void TaskFail(int32 Reason) override
		{
			FailureReasons.Add(Reason);
			Flags.OnTaskFail();
			Conditions.Set(EElysiumNpcCond::TaskFailed);
			Calls.Add(FString::Printf(TEXT("TaskFail %d"), Reason));
		}
		// Slot 440 as a table a case fills: the kernel's contract is only that the fail route's id
		// goes through it and the miss arm's literal 1 does not.
		TMap<EElysiumScheduleId, EElysiumScheduleId> Translations;
		virtual EElysiumScheduleId TranslateSchedule(EElysiumScheduleId Id) override
		{
			Calls.Add(FString::Printf(TEXT("TranslateSchedule %s"), ElysiumScheduleName(Id)));
			const EElysiumScheduleId* Translated = Translations.Find(Id);
			return Translated ? *Translated : Id;
		}
		virtual void ScheduleDone() override { ++CompletedSchedules; }

		// Knobs a test turns to drive each branch.
		bool bVisible = true;
		float IdleClipSeconds = 2.f;
		float ActivitySeconds = 1.f;
		bool bIdleAvailable = true;
		bool bActivityResolves = true;
		bool bIdealActivityCurrent = true;
		bool bMotor = true;
		// `WAIT_RANDOM` draws through the NPC schedule stream in the real runner; here it is fixed
		// so a duration is a literal in the test rather than a seed to reverse-engineer.
		float RandomFraction = 1.f;

		// `TASK_MOVE_AWAY_PATH` runs the REAL rule rather than a double of it: the runner
		// supplies the two positions and a recording motor, and
		// `ElysiumSchedule::StepAwayFromSavePosition` does the extrapolating, the projecting and the
		// re-testing. The NPC stands 200 cm out from what obstructed it, so a retreat is +X.
		FElysiumRecordingNpcMotor Motor;
		FVector Origin = FVector(200.0, 0.0, 0.0);
		FVector SavePosition = FVector::ZeroVector;
		ElysiumSchedule::ERetreat LastRetreat = ElysiumSchedule::ERetreat::Moving;

		virtual float RunSpecialIdleActivity(double Now) override
		{
			Calls.Add(TEXT("SpecialIdleActivity"));
			return bIdleAvailable ? IdleClipSeconds : -1.f;
		}
		virtual bool IsBodyVisible() const override { return bVisible; }
		// A body that asks for `ClearSchedule` from inside its `TASK_SET_ACTIVITY` arm, the shape of
		// the task-body callers.
		bool bClearFromActivityTask = false;
		virtual float PlayActivity(const FString& Activity) override
		{
			Calls.Add(FString::Printf(TEXT("SetActivity %s"), *Activity));
			if (bClearFromActivityTask)
			{
				bRequestClear = true;
			}
			return bActivityResolves ? ActivitySeconds : -1.f;
		}
		virtual bool IsIdealActivityCurrent() const override { return bIdealActivityCurrent; }
		virtual bool FaceSavePosition() override
		{
			Calls.Add(TEXT("FaceSavePosition"));
			return bMotor;
		}
		virtual bool StepAwayFromSavePosition(float DistanceCm) override
		{
			FVector Destination = FVector::ZeroVector;
			LastRetreat = ElysiumSchedule::StepAwayFromSavePosition(bMotor ? &Motor : nullptr,
				Origin, SavePosition, DistanceCm, Destination);
			Calls.Add(FString::Printf(TEXT("StepAway %.0f -> %s"), DistanceCm,
				ElysiumSchedule::RetreatResultName(LastRetreat)));
			return LastRetreat == ElysiumSchedule::ERetreat::Moving;
		}
		// `RandomFloat(0.1, Max)`: the fraction spans the retail range, floor included.
		virtual float RandomSeconds(float Max) override { return 0.1f + (Max - 0.1f) * RandomFraction; }

		// `ClearSchedule` from inside a task: the next task step the kernel runs asks and consumes it.
		bool bRequestClear = false;
		virtual bool TakeClearScheduleRequest() override
		{
			const bool bRequested = bRequestClear;
			bRequestClear = false;
			return bRequested;
		}
		virtual void ClearPreservePath() override
		{
			Calls.Add(TEXT("ClearPreservePath"));
			Flags.Clear(EElysiumNpcFlag::PRESERVE_PATH);
		}

		// The death ladder's one rung. `PlayableDeathActivities` is this body's vocabulary:
		// EMPTY but for `ACT_IDLE` is the shipped corpus, where `ACT_DIESIMPLE` resolves on zero
		// bodies. FString comparison is case-insensitive, like every other vocabulary key here.
		TSet<FString> PlayableDeathActivities = { FString(TEXT("ACT_IDLE")) };
		float DeathClipSeconds = 3.f;
		virtual float PlayDeathActivity(const FString& Activity) override
		{
			Calls.Add(FString::Printf(TEXT("DeathActivity %s"), *Activity));
			return PlayableDeathActivities.Contains(Activity) ? DeathClipSeconds : -1.f;
		}

		virtual void RecordScheduleEvent(const FString& Row) override
		{
			Calls.Add(FString::Printf(TEXT("trace: %s"), *Row));
		}

		// The incapacitation verbs and the two install rules, recorded rather than simulated: the
		// kernel owns WHEN they fire, and that ordering is what these tests assert.
		FElysiumNpcFlags Flags;
		int32 ConditionClears = 0;
		virtual void MakeOblivious(bool bOblivious) override
		{
			Calls.Add(FString::Printf(TEXT("MakeOblivious %s"),
				bOblivious ? TEXT("TRUE") : TEXT("FALSE")));
			if (bOblivious)
			{
				Flags.AddOblivious();
			}
			else
			{
				Flags.RemoveOblivious();
			}
		}
		virtual void SetNpcFlag(EElysiumNpcFlag Flag) override
		{
			Calls.Add(FString::Printf(TEXT("SetNpcFlag %s"), FElysiumNpcFlags::LexToString(Flag)));
			Flags.Set(Flag);
		}
		virtual void ClearConditions() override
		{
			++ConditionClears;
			Conditions.Reset();
			Calls.Add(TEXT("ClearConditions"));
		}
		virtual void OnScheduleChange() override
		{
			if (ObservedState) OutgoingSchedules.Add(ObservedState->Current);
			Calls.Add(TEXT("OnScheduleChange"));
			Flags.OnScheduleChange();
		}
		// The per-NPC overlay, as a list a case fills: the kernel's contract is only that it asks
		// before the test and honours what comes back.
		TArray<EElysiumNpcCond> OverlayAdds;
		virtual void BuildScheduleTestBits(FElysiumNpcConditions& InOutMask) override
		{
			Calls.Add(TEXT("BuildScheduleTestBits"));
			for (const EElysiumNpcCond Cond : OverlayAdds)
			{
				InOutMask.Set(Cond);
			}
		}

		bool Saw(const TCHAR* Needle) const
		{
			return Calls.ContainsByPredicate([Needle](const FString& C) { return C.Contains(Needle); });
		}
	};

	// Drive a schedule to completion, or until the step budget runs out. Returns the number of
	// thinks it took, which is the cadence assertion.
	int32 RunToEnd(FElysiumScheduleState& State, FRecordingRunner& Runner, double& Now,
		int32 MaxThinks = 200)
	{
		int32 Thinks = 0;
		while (Thinks < MaxThinks && ElysiumSchedule::Tick(State, Runner, Now))
		{
			++Thinks;
			Now = FMath::Max(Now + 0.01, State.TaskEndsAt);
		}
		return Thinks;
	}
}

// ============================================================================================
// `SCHED_TROIKA_ALERT_LOOK_AROUND_NI` is the schedule whose whole shape is its task order:
// `SET_ACTIVITY ACT_ALERT_FIDGET_LOOKAROUND; WAIT 3; WAIT_RANDOM 3; SET_ACTIVITY ACT_IDLE;
//  WAIT_RANDOM 2`. Both activities must play, in that order, with the waits actually holding.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleTaskOrderTest,
	"Elysium.Substrate.Schedule.TaskOrder", GElysiumScheduleTestFlags)
bool FElysiumScheduleTaskOrderTest::RunTest(const FString&)
{
	FRecordingRunner Runner;
	FElysiumScheduleState State;
	double Now = 0.0;

	TestTrue(TEXT("the alert lookaround schedule is registered"),
		ElysiumSchedule::Start(State, EElysiumScheduleId::AlertLookAroundNi, Runner));
	TestTrue(TEXT("starting one traces its retail number"),
		Runner.Saw(TEXT("SCHED_TROIKA_ALERT_LOOK_AROUND_NI")));

	RunToEnd(State, Runner, Now);

	// The two activities, in order, with nothing between them but waits.
	const TArray<FString> Activities = Runner.Calls.FilterByPredicate([](const FString& C)
	{
		return C.StartsWith(TEXT("SetActivity"));
	});
	if (TestEqual(TEXT("the schedule plays exactly two activities"), Activities.Num(), 2))
	{
		TestEqual(TEXT("the lookaround fidget is first"), Activities[0],
			FString(TEXT("SetActivity ACT_ALERT_FIDGET_LOOKAROUND")));
		TestEqual(TEXT("and it returns to idle after"), Activities[1],
			FString(TEXT("SetActivity ACT_IDLE")));
	}

	// WAIT 3 + WAIT_RANDOM 3 + WAIT_RANDOM 2, with the draw pinned to its maximum.
	TestTrue(TEXT("the authored waits actually hold the schedule open"), Now >= 8.0);
	TestFalse(TEXT("the schedule ends rather than looping"), State.IsRunning());
	return true;
}

// ============================================================================================
// `TASK_WAIT_PVS` is the throttle. It is the one task with no clock, so it holds until the body is
// visible however long that takes — and the schedule must not advance past it meanwhile. This is
// the branch nothing else exercises: the recording services default to visible, because a headless
// run has no renderer to answer the question.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleWaitPvsTest,
	"Elysium.Substrate.Schedule.WaitPvs", GElysiumScheduleTestFlags)
bool FElysiumScheduleWaitPvsTest::RunTest(const FString&)
{
	FRecordingRunner Runner;
	Runner.bVisible = false;
	Runner.IdleClipSeconds = 1.f;
	FElysiumScheduleState State;
	double Now = 0.0;

	ElysiumSchedule::Start(State, EElysiumScheduleId::IdleDisposition, Runner);

	// Long past the stance clip's end, so the only thing still holding is the PVS task. The step is
	// the case's own: the kernel proposes no cadence (retail's `MaintainSchedule` never informs the
	// think clocks), and how often an unseen body is asked is the NPC think cadence's answer, not
	// this task's.
	for (int32 i = 0; i < 60; ++i)
	{
		TestTrue(TEXT("an unseen NPC's idle schedule stays open"),
			ElysiumSchedule::Tick(State, Runner, Now));
		Now = FMath::Max(Now + 0.5, State.TaskEndsAt);
	}
	const int32 SelectionsWhileUnseen = Runner.Calls.FilterByPredicate([](const FString& C)
	{
		return C == TEXT("SpecialIdleActivity");
	}).Num();
	TestEqual(TEXT("and selects exactly once — the throttle is the whole point"),
		SelectionsWhileUnseen, 1);
	TestTrue(TEXT("...however long it is held open for"), Now >= 25.0);

	// Coming back into view releases it, and the schedule ends so the NPC selects again.
	Runner.bVisible = true;
	TestFalse(TEXT("becoming visible completes the wait and ends the schedule"),
		ElysiumSchedule::Tick(State, Runner, Now));
	TestFalse(TEXT("the state is cleared for the next selection"), State.IsRunning());
	return true;
}

// ============================================================================================
// A task that cannot run fails the schedule by name. `TASK_SPECIAL_IDLE_ACTIVITY` failing is the
// real case: a model carrying no `Stance_*` clips has no stance machine, and its idle schedule must
// end — reporting why — rather than holding the NPC in a program that will never advance.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleFailureTest,
	"Elysium.Substrate.Schedule.Failure", GElysiumScheduleTestFlags)
bool FElysiumScheduleFailureTest::RunTest(const FString&)
{
	// --- A failing task with no fail schedule ends the program -------------------------------
	{
		FRecordingRunner Runner;
		Runner.bIdleAvailable = false;
		FElysiumScheduleState State;
		double Now = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::IdleDisposition, Runner);
		TestTrue(TEXT("a body with no stance machine fails its idle task and the pass ends"),
			ElysiumSchedule::Tick(State, Runner, Now));
		TestTrue(TEXT("and the trace names the task that failed"),
			Runner.FailureReasons.Contains(0x15));
		TestEqual(TEXT("the failed program stands until the next pass"), State.Current,
			EElysiumScheduleId::IdleDisposition);
		TestTrue(TEXT("the next pass routes the failure"),
			ElysiumSchedule::Tick(State, Runner, Now + 0.1, &Runner.Conditions));
		TestEqual(TEXT("base FAIL (0x43) is the route when none was set"), State.Current,
			EElysiumScheduleId::Fail);
	}

	// --- A TASK_SET_ACTIVITY miss still advances the program -----------------------------------
	// Troika StartTask arm 0x102a1c0f has no TaskFail branch. Its RunTask arm waits for the current
	// sequence or a one-second watchdog. The cover program has a fail schedule, which makes it the
	// control: an unresolved activity must advance through the watchdog without taking it.
	{
		FRecordingRunner Runner;
		Runner.bActivityResolves = false;   // preserve the real negative return from PlayActivity
		Runner.bIdealActivityCurrent = false;
		FElysiumScheduleState State;
		double Now = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::TakeCoverHintDoor, Runner);
		TestTrue(TEXT("the unresolved activity starts without failing"),
			ElysiumSchedule::Tick(State, Runner, Now));
		TestTrue(TEXT("the trace records the body's unresolved activity"),
			Runner.Saw(TEXT("TASK_SET_ACTIVITY ACT_IDLE unresolved")));
		TestTrue(TEXT("the miss is not reported as a failed task"),
			Runner.FailureReasons.IsEmpty());
		TestEqual(TEXT("the original schedule holds at its activity before the watchdog"), State.Current,
			EElysiumScheduleId::TakeCoverHintDoor);
		TestEqual(TEXT("the unresolved activity is still the current task"), State.TaskIndex, 0);
		TestFalse(TEXT("the cover fail schedule is not installed"),
			Runner.Saw(TEXT("SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE")));

		Now = 0.5;
		TestTrue(TEXT("the miss waits until the one-second watchdog"),
			ElysiumSchedule::Tick(State, Runner, Now));
		TestEqual(TEXT("it remains on the activity before the deadline"), State.TaskIndex, 0);

		Now = 1.0;
		TestTrue(TEXT("the watchdog completes into the authored wait"),
			ElysiumSchedule::Tick(State, Runner, Now));
		TestEqual(TEXT("the original schedule advances to its wait"), State.Current,
			EElysiumScheduleId::TakeCoverHintDoor);
		TestEqual(TEXT("the wait is the second task after the watchdog"), State.TaskIndex, 1);
		TestFalse(TEXT("the watchdog still does not install the fail schedule"),
			Runner.Saw(TEXT("SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE")));
	}

	// --- An unregistered schedule installs IDLE_STAND (`SetSchedule(int)` 0x102cc1f0) -----------
	{
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		TestTrue(TEXT("an unregistered schedule still installs a program"),
			ElysiumSchedule::Start(State, EElysiumScheduleId::None, Runner));
		TestEqual(TEXT("and that program is base IDLE_STAND"), State.Current,
			EElysiumScheduleId::IdleStand);
		TestTrue(TEXT("the trace names the miss"), Runner.Saw(TEXT("GetScheduleOfType(): No CASE")));
		TestTrue(TEXT("a miss is not a task failure"), Runner.FailureReasons.IsEmpty());
	}
	return true;
}

// ============================================================================================
// The near-door reaction, whose task bodies are motor verbs. It must face what obstructed it before
// retreating — backing away from something the NPC is not looking at is the wrong shape — and it
// must fail rather than pretend when there is no motor to do either.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleDoorTest,
	"Elysium.Substrate.Schedule.DoorBackAway", GElysiumScheduleTestFlags)
bool FElysiumScheduleDoorTest::RunTest(const FString&)
{
	{
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		double Now = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::BackAwayFromDoorNe, Runner);
		RunToEnd(State, Runner, Now);

		const int32 FaceIndex = Runner.Calls.IndexOfByPredicate([](const FString& C)
		{
			return C == TEXT("FaceSavePosition");
		});
		const int32 FirstStep = Runner.Calls.IndexOfByPredicate([](const FString& C)
		{
			return C.StartsWith(TEXT("StepAway"));
		});
		TestTrue(TEXT("the NPC faces the obstruction first"),
			FaceIndex != INDEX_NONE && FirstStep != INDEX_NONE && FaceIndex < FirstStep);
		TestEqual(TEXT("and steps back repeatedly rather than pathing to a goal"),
			Runner.Calls.FilterByPredicate([](const FString& C)
			{
				return C.StartsWith(TEXT("StepAway"));
			}).Num(), 2);
	}
	{
		// No motor: the tasks fail rather than reporting a retreat that never happened.
		FRecordingRunner Runner;
		Runner.bMotor = false;
		FElysiumScheduleState State;
		double Now = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::BackAwayFromDoorNe, Runner);
		TestTrue(TEXT("a bodiless NPC cannot back away, and the pass ends on the failure"),
			ElysiumSchedule::Tick(State, Runner, Now));
		TestTrue(TEXT("the next pass routes it into FAIL"),
			ElysiumSchedule::Tick(State, Runner, Now + 0.1, &Runner.Conditions));
		TestEqual(TEXT("the route is base FAIL"), State.Current, EElysiumScheduleId::Fail);
		TestTrue(TEXT("the trace names the motor task that failed"),
			Runner.Saw(TEXT("TASK_FACE_SAVEPOSITION failed")));
		TestTrue(TEXT("...with the generic task-failure reason"),
			Runner.FailureReasons.Contains(0x0c));
	}
	return true;
}

// ============================================================================================
// 11.14 — `TASK_MOVE_AWAY_PATH` asks the world where the step back actually lands, then re-tests
// what came back. The re-test is the whole point: projection answers "where can someone stand",
// not "is this still away from the door", so a navigable point that is no longer a retreat has to
// fail the schedule rather than walk the NPC into the swing it was told to leave.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleRetreatProjectionTest,
	"Elysium.Substrate.Schedule.RetreatProjection", GElysiumScheduleTestFlags)
bool FElysiumScheduleRetreatProjectionTest::RunTest(const FString&)
{
	// --- Open floor: the projection returns the point, the re-test passes, the body moves --------
	{
		FElysiumRecordingNpcMotor Motor;
		FVector Destination = FVector::ZeroVector;
		const ElysiumSchedule::ERetreat Result = ElysiumSchedule::StepAwayFromSavePosition(
			&Motor, /*Origin*/ FVector(200.0, 0.0, 0.0), /*SavePosition*/ FVector::ZeroVector,
			/*DistanceCm*/ 100.f, Destination);
		TestEqual(TEXT("a projectable retreat moves"), Result, ElysiumSchedule::ERetreat::Moving);
		TestTrue(TEXT("and it steps directly away from what obstructed it"),
			Destination.Equals(FVector(300.0, 0.0, 0.0)));
		TestTrue(TEXT("the destination handed to the motor is the PROJECTED point"),
			Motor.RequestedFeet.Equals(Destination));
	}

	// --- The projection pulls the point back through the doorway: no longer a retreat ------------
	{
		FElysiumRecordingNpcMotor Motor;
		// The only navigable ground near the extrapolated point is BEHIND the NPC, closer to the
		// obstruction than it already stands.
		Motor.ProjectedOverride = FVector(150.0, 0.0, 0.0);
		FVector Destination = FVector::ZeroVector;
		const ElysiumSchedule::ERetreat Result = ElysiumSchedule::StepAwayFromSavePosition(
			&Motor, FVector(200.0, 0.0, 0.0), FVector::ZeroVector, 100.f, Destination);
		TestEqual(TEXT("a projection that gained no ground is refused"), Result,
			ElysiumSchedule::ERetreat::NotARetreat);
		TestFalse(TEXT("and no movement request is issued at all"), Motor.bMoving);
	}

	// --- A sideways slide is not a step back either ----------------------------------------------
	{
		FElysiumRecordingNpcMotor Motor;
		// Projected along the wall: navigable, and exactly as far from the obstruction as before.
		Motor.ProjectedOverride = FVector(200.0, 100.0, 0.0);
		FVector Destination = FVector::ZeroVector;
		const ElysiumSchedule::ERetreat Result = ElysiumSchedule::StepAwayFromSavePosition(
			&Motor, FVector(200.0, 0.0, 0.0), FVector::ZeroVector, 100.f, Destination);
		// 223.6 cm out against 200 standing: it gained ground, so it IS a retreat. The margin only
		// rejects a projection that gained less than `RetreatMarginCm`.
		TestEqual(TEXT("a slide that still gains ground remains a retreat"), Result,
			ElysiumSchedule::ERetreat::Moving);

		FElysiumRecordingNpcMotor Flat;
		Flat.ProjectedOverride = FVector(200.0, 10.0, 0.0);   // 200.25 cm out — inside the margin
		const ElysiumSchedule::ERetreat Refused = ElysiumSchedule::StepAwayFromSavePosition(
			&Flat, FVector(200.0, 0.0, 0.0), FVector::ZeroVector, 100.f, Destination);
		TestEqual(TEXT("...but one inside the margin has gained nothing"), Refused,
			ElysiumSchedule::ERetreat::NotARetreat);
	}

	// --- Nothing navigable there, and nothing to ask at all ---------------------------------------
	{
		FElysiumRecordingNpcMotor Motor;
		Motor.bProjectsToNavigable = false;
		FVector Destination = FVector(-1.0, -1.0, -1.0);
		const ElysiumSchedule::ERetreat Result = ElysiumSchedule::StepAwayFromSavePosition(
			&Motor, FVector(200.0, 0.0, 0.0), FVector::ZeroVector, 100.f, Destination);
		TestEqual(TEXT("an unprojectable retreat fails rather than pathing to a guess"), Result,
			ElysiumSchedule::ERetreat::Unprojectable);
		TestTrue(TEXT("and the out-parameter is left untouched"),
			Destination.Equals(FVector(-1.0, -1.0, -1.0)));

		TestEqual(TEXT("no motor is its own answer, distinct from an unprojectable point"),
			ElysiumSchedule::StepAwayFromSavePosition(nullptr, FVector(200.0, 0.0, 0.0),
				FVector::ZeroVector, 100.f, Destination),
			ElysiumSchedule::ERetreat::NoMotor);

		// Standing exactly on what obstructed it: there is no direction to leave in, and inventing
		// one would be arithmetic standing in for a decision.
		FElysiumRecordingNpcMotor Coincident;
		TestEqual(TEXT("a coincident save position is refused before the world is asked"),
			ElysiumSchedule::StepAwayFromSavePosition(&Coincident, FVector::ZeroVector,
				FVector::ZeroVector, 100.f, Destination),
			ElysiumSchedule::ERetreat::Degenerate);
	}

	// --- The refusal reaches the schedule's fail path ---------------------------------------------
	{
		FRecordingRunner Runner;
		Runner.Motor.ProjectedOverride = FVector(150.0, 0.0, 0.0);   // no longer a retreat
		FElysiumScheduleState State;
		double Now = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::BackAwayFromDoorNe, Runner);
		int32 Thinks = 0;
		while (Thinks < 8 && ElysiumSchedule::Tick(State, Runner, Now))
		{
			++Thinks;
			Now = FMath::Max(Now + 0.01, State.TaskEndsAt);
		}
		TestFalse(TEXT("the schedule ends rather than repeating a retreat that is not one"),
			State.IsRunning());
		TestTrue(TEXT("the trace names the failed move task"),
			Runner.Saw(TEXT("TASK_MOVE_AWAY_PATH failed")));
		TestTrue(TEXT("...with the generic task-failure reason"),
			Runner.FailureReasons.Contains(0x0c));
		TestTrue(TEXT("...and the runner recorded why the world refused it"),
			Runner.Saw(TEXT("no longer a retreat")));
		TestFalse(TEXT("the body was never asked to move"), Runner.Motor.bMoving);
	}
	return true;
}

// ============================================================================================
// `TASK_PLAY_DEATH_SEQUENCE` (0x149) and `SCHED_DIE`.
//
// The recovered ladder is "try the argument as an activity, then `ACT_DIESIMPLE`, then `ACT_IDLE`,
// and pass the surviving choice to `SetIdealActivity`"
// (`docs/vtmb/animation_and_movers.md` -> the `RunTask` activity table). Its outcome on the shipped
// game is the point of these cases: `ACT_DIESIMPLE` is absent from the ENTIRE 167-body corpus, and
// the registered program names no argument, so every death in VtMB resolves the floor rung.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleDeathLadderTest,
	"Elysium.Substrate.Schedule.DeathLadder", GElysiumScheduleTestFlags)
bool FElysiumScheduleDeathLadderTest::RunTest(const FString&)
{
	auto DeathRungs = [](const FRecordingRunner& Runner)
	{
		return Runner.Calls.FilterByPredicate([](const FString& C)
		{
			return C.StartsWith(TEXT("DeathActivity"));
		});
	};

	// --- The shipped corpus: no argument, no ACT_DIESIMPLE, so the floor rung answers -------------
	{
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		double Now = 0.0;

		TestTrue(TEXT("the death schedule is registered"),
			ElysiumSchedule::Start(State, EElysiumScheduleId::Die, Runner));
		TestTrue(TEXT("starting it traces its name"), Runner.Saw(TEXT("SCHED_DIE")));

		TestFalse(TEXT("the program ends inside its first think"),
			ElysiumSchedule::Tick(State, Runner, Now));

		const TArray<FString> Rungs = DeathRungs(Runner);
		if (TestEqual(TEXT("the ladder tries exactly two rungs"), Rungs.Num(), 2))
		{
			TestEqual(TEXT("ACT_DIESIMPLE is first — an absent argument is a rung that is not "
				"there, not a rung that missed"), Rungs[0],
				FString(TEXT("DeathActivity ACT_DIESIMPLE")));
			TestEqual(TEXT("and ACT_IDLE is the floor"), Rungs[1],
				FString(TEXT("DeathActivity ACT_IDLE")));
		}
		TestTrue(TEXT("the trace names what the ladder resolved"),
			Runner.Saw(TEXT("TASK_PLAY_DEATH_SEQUENCE arg='(none)' -> ACT_IDLE")));
		// The floor rung is retail saying "this body has no death performance", and its ragdoll
		// supersedes the choice at once — so the task does not wait out an idle.
		TestEqual(TEXT("the floor rung completes at once rather than holding for a clip"), Now, 0.0);
		TestFalse(TEXT("nothing is left running"), State.IsRunning());
	}

	// --- A body that DOES author ACT_DIESIMPLE stops at that rung ---------------------------------
	{
		FRecordingRunner Runner;
		Runner.PlayableDeathActivities.Add(TEXT("ACT_DIESIMPLE"));
		FElysiumScheduleState State;
		double Now = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::Die, Runner);
		TestTrue(TEXT("a real death clip holds the task open for its own length"),
			ElysiumSchedule::Tick(State, Runner, Now));

		const TArray<FString> Rungs = DeathRungs(Runner);
		if (TestEqual(TEXT("the ladder stops at the first rung that resolves"), Rungs.Num(), 1))
		{
			TestEqual(TEXT("and that rung is ACT_DIESIMPLE"), Rungs[0],
				FString(TEXT("DeathActivity ACT_DIESIMPLE")));
		}
		// The hold is the task's own deadline. It used to be read back through the kernel's
		// `OutNextThinkDelay`, which the think cadence retired -- retail's `MaintainSchedule` never
		// informs the think clocks.
		TestEqual(TEXT("the hold is the resolved clip's own length"), State.TaskEndsAt - Now,
			static_cast<double>(Runner.DeathClipSeconds), 1e-6);

		Now += Runner.DeathClipSeconds;
		TestFalse(TEXT("and the program ends when the clip does"),
			ElysiumSchedule::Tick(State, Runner, Now));
	}

	// --- The argument rung wins over both fixed ones ----------------------------------------------
	// The operand is installed for the length of this case: no registered program authors one,
	// because retail spells it as an activity-index number this runtime has no decoded table for.
	{
		const ElysiumSchedule::FTaskActivityScope Argument(EElysiumScheduleId::Die, 0,
			TEXT("ACT_DEATH_INTO"));
		TestTrue(TEXT("the death program has a task to carry the operand"), Argument.IsInstalled());

		FRecordingRunner Runner;
		Runner.PlayableDeathActivities.Add(TEXT("ACT_DIESIMPLE"));
		Runner.PlayableDeathActivities.Add(TEXT("ACT_DEATH_INTO"));
		FElysiumScheduleState State;
		double Now = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::Die, Runner);
		ElysiumSchedule::Tick(State, Runner, Now);

		const TArray<FString> Rungs = DeathRungs(Runner);
		if (TestEqual(TEXT("the argument is tried alone when it resolves"), Rungs.Num(), 1))
		{
			TestEqual(TEXT("and it is the argument, ahead of ACT_DIESIMPLE"), Rungs[0],
				FString(TEXT("DeathActivity ACT_DEATH_INTO")));
		}
	}

	// --- An argument that misses falls through the whole ladder in order --------------------------
	{
		const ElysiumSchedule::FTaskActivityScope Argument(EElysiumScheduleId::Die, 0,
			TEXT("ACT_DEATH_INTO"));
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		double Now = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::Die, Runner);
		ElysiumSchedule::Tick(State, Runner, Now);

		const TArray<FString> Rungs = DeathRungs(Runner);
		if (TestEqual(TEXT("all three rungs are tried"), Rungs.Num(), 3))
		{
			TestEqual(TEXT("argument first"), Rungs[0], FString(TEXT("DeathActivity ACT_DEATH_INTO")));
			TestEqual(TEXT("then ACT_DIESIMPLE"), Rungs[1],
				FString(TEXT("DeathActivity ACT_DIESIMPLE")));
			TestEqual(TEXT("then ACT_IDLE"), Rungs[2], FString(TEXT("DeathActivity ACT_IDLE")));
		}
	}

	// --- A body whose vocabulary carries none of the three still COMPLETES -------------------------
	// The ragdoll handoff that follows the program is what death is; failing the task would send a
	// corpse to a fail schedule instead.
	{
		FRecordingRunner Runner;
		Runner.PlayableDeathActivities.Reset();
		FElysiumScheduleState State;
		double Now = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::Die, Runner);
		TestFalse(TEXT("the program ends"), ElysiumSchedule::Tick(State, Runner, Now));
		TestTrue(TEXT("and it ended by completing, not by failing"),
			Runner.FailureReasons.IsEmpty());
		TestTrue(TEXT("the trace says nothing resolved"),
			Runner.Saw(TEXT("-> (nothing resolved)")));
	}
	return true;
}

// ============================================================================================
// `SCHED_TROIKA_MESMERIZED` (0xfb), the post-feed trance — the whole recovered program in order,
// and the duration its two WAIT steps produce.
//
// `vampire.dll 0x105e6f40`: MAKE_OBLIVIOUS TRUE; SET_NPC_FLAG D_IS_BUSY; SET_NPC_FLAG
// DONT_INVESTIGATE; SET_NPC_FLAG NO_DIALOG; SET_ACTIVITY ACT_DISPOSITION_MESMERIZED; WAIT 30;
// WAIT_RANDOM 120.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleMesmerizedTest,
	"Elysium.Substrate.Schedule.Mesmerized", GElysiumScheduleTestFlags)
bool FElysiumScheduleMesmerizedTest::RunTest(const FString&)
{
	FRecordingRunner Runner;
	Runner.RandomFraction = 1.f;   // the WAIT_RANDOM draw resolves to its whole 120
	FElysiumScheduleState State;
	double Now = 0.0;

	TestTrue(TEXT("the program is registered"),
		ElysiumSchedule::Start(State, EElysiumScheduleId::Mesmerized, Runner));

	const double Started = Now;
	const int32 Thinks = RunToEnd(State, Runner, Now);

	// The task order, verbatim.
	const TArray<FString> Expected = {
		TEXT("MakeOblivious TRUE"),
		TEXT("SetNpcFlag D_IS_BUSY"),
		TEXT("SetNpcFlag DONT_INVESTIGATE"),
		TEXT("SetNpcFlag NO_DIALOG"),
		TEXT("SetActivity ACT_DISPOSITION_MESMERIZED"),
	};
	int32 Cursor = 0;
	for (const FString& Want : Expected)
	{
		const int32 At = Runner.Calls.IndexOfByPredicate(
			[&Want](const FString& C) { return C == Want; });
		TestTrue(FString::Printf(TEXT("%s ran"), *Want), At != INDEX_NONE);
		TestTrue(FString::Printf(TEXT("%s ran in order"), *Want), At > Cursor);
		Cursor = At == INDEX_NONE ? Cursor : At;
	}

	// 30 flat plus a 0..120 draw. With the draw at its maximum the trance is 150 seconds; the
	// schedule then ENDS rather than looping, which is what returns the NPC to ordinary selection.
	TestFalse(TEXT("the program has ended"), State.IsRunning());
	TestEqual(TEXT("30 + WAIT_RANDOM(120) at full draw is 150 seconds"),
		static_cast<int32>(FMath::RoundToInt(Now - Started)), 150);
	TestTrue(TEXT("it held across thinks rather than completing in one"), Thinks >= 2);

	// It carries no teardown tasks: the three flags and the obliviousness are still set when the
	// program ends, and it is the NEXT install that releases them.
	TestTrue(TEXT("D_IS_BUSY survives the program's own end"),
		Runner.Flags.Has(EElysiumNpcFlag::D_IS_BUSY));
	TestTrue(TEXT("the victim is still oblivious"), Runner.Flags.IsOblivious());

	// ...and now the next schedule releases all of it, through `OnScheduleChange`.
	ElysiumSchedule::Start(State, EElysiumScheduleId::AlertLookAroundNi, Runner);
	TestFalse(TEXT("D_IS_BUSY released by the next install"),
		Runner.Flags.Has(EElysiumNpcFlag::D_IS_BUSY));
	TestFalse(TEXT("NO_DIALOG released by the next install"),
		Runner.Flags.Has(EElysiumNpcFlag::NO_DIALOG));
	TestFalse(TEXT("DONT_INVESTIGATE released by the next install"),
		Runner.Flags.Has(EElysiumNpcFlag::DONT_INVESTIGATE));
	TestFalse(TEXT("and the obliviousness refcount is released with them"),
		Runner.Flags.IsOblivious());
	return true;
}

// ============================================================================================
// `DELAY_INTERRUPTS` — `CAI_BaseNPC::IsScheduleValid` (`0x10280ff0`) ANDs the schedule's flag with
// `!m_bDidMaintainSchedule`, so a flagged program is immune for exactly ONE think after install and
// interruptible on every think after that. Nothing is latched or deferred.
//
// Driven on the mesmerized program, which is the only registered carrier of the flag, against its
// own decoded interrupt mask.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleDelayInterruptsTest,
	"Elysium.Substrate.Schedule.DelayInterrupts", GElysiumScheduleTestFlags)
bool FElysiumScheduleDelayInterruptsTest::RunTest(const FString&)
{
	const FElysiumNpcConditions Damage =
		FElysiumNpcConditions::Of({ EElysiumNpcCond::HeavyDamage });

	// 1. The damage condition standing on the very first think does NOT end the program.
	{
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		double Now = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::Mesmerized, Runner);
		TestTrue(TEXT("the install cleared the gathered conditions"), Runner.ConditionClears == 1);

		TestTrue(TEXT("the first think survives a condition in its own interrupt mask"),
			ElysiumSchedule::Tick(State, Runner, Now, &Damage));
		TestTrue(TEXT("...and says so"), Runner.Saw(TEXT("DELAY_INTERRUPTS")));
		TestTrue(TEXT("...and the program is still running"), State.IsRunning());

		// 2. The same condition on the NEXT think ends it. The window is one think, not a duration:
		//    no time passes between these two calls.
		TestFalse(TEXT("the second think is interrupted by the same condition"),
			ElysiumSchedule::Tick(State, Runner, Now, &Damage));
		TestFalse(TEXT("the program ended"), State.IsRunning());
		TestTrue(TEXT("and the trace names the firing condition"),
			Runner.Saw(TEXT("interrupted by")));
	}

	// 3. A program WITHOUT the flag is interrupted on its first think. Same conditions, same
	//    kernel — the flag is the only difference, which is what makes it the cause.
	{
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		double Now = 0.0;

		// The idle program's mask carries `HEAVY_DAMAGE` and it declares no flags.
		ElysiumSchedule::Start(State, EElysiumScheduleId::AlertLookAroundNi, Runner);
		TestFalse(TEXT("an unflagged program is interrupted on its first think"),
			ElysiumSchedule::Tick(State, Runner, Now, &Damage));
		TestFalse(TEXT("and it did not report a delay"), Runner.Saw(TEXT("DELAY_INTERRUPTS")));
	}
	return true;
}

// ============================================================================================
// The per-NPC interrupt overlay -- `BuildScheduleTestBits`. The mask a program runs against is
// the authored one PLUS whatever the runner adds each think, so a program whose authored mask is
// empty can still be interrupted by a condition the NPC's state makes an interrupt.
//
// `SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE` declares no interrupts at all, which makes it the clean
// control: the same condition, the same kernel, and the overlay is the only difference.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleTestBitsOverlayTest,
	"Elysium.Substrate.Schedule.TestBitsOverlay", GElysiumScheduleTestFlags)
bool FElysiumScheduleTestBitsOverlayTest::RunTest(const FString&)
{
	const FElysiumNpcConditions Law =
		FElysiumNpcConditions::Of({ EElysiumNpcCond::CriminalFleeLevel });

	// 1. Without an overlay the empty authored mask admits nothing.
	{
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		double Now = 0.0;
		ElysiumSchedule::Start(State, EElysiumScheduleId::BackAwayFromDoorNe, Runner);
		ElysiumSchedule::Tick(State, Runner, Now, &Law);   // arms the window
		TestTrue(TEXT("an empty authored mask is not interrupted by a law condition"),
			State.IsRunning() && ElysiumSchedule::Tick(State, Runner, Now, &Law));
		TestTrue(TEXT("...and the kernel asked the runner for its overlay each think"),
			Runner.Calls.FilterByPredicate(
				[](const FString& C) { return C == TEXT("BuildScheduleTestBits"); }).Num() >= 2);
	}

	// 2. With the overlay adding the law condition, the same program is interrupted by it.
	{
		FRecordingRunner Runner;
		Runner.OverlayAdds = { EElysiumNpcCond::CriminalFleeLevel };
		FElysiumScheduleState State;
		double Now = 0.0;
		ElysiumSchedule::Start(State, EElysiumScheduleId::BackAwayFromDoorNe, Runner);
		ElysiumSchedule::Tick(State, Runner, Now, &Law);   // arms the window
		TestFalse(TEXT("the overlaid mask is interrupted by the same condition"),
			State.IsRunning() && ElysiumSchedule::Tick(State, Runner, Now, &Law));
		TestTrue(TEXT("...and the trace names it"),
			Runner.Saw(TEXT("interrupted by CRIMINAL_FLEE_LEVEL")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleFailureDispatchTest,
	"Elysium.Substrate.Schedule.FailureDispatch", GElysiumScheduleTestFlags)
bool FElysiumScheduleFailureDispatchTest::RunTest(const FString&)
{
	FRecordingRunner Runner;
	FElysiumScheduleState State;
	TestTrue(TEXT("an absent program installs IDLE_STAND"), ElysiumSchedule::Start(State, EElysiumScheduleId::None, Runner));
	TestTrue(TEXT("a missing schedule is not a TaskFail"), Runner.FailureReasons.IsEmpty());
	TestEqual(TEXT("the source reason table names follower failure"), FString(ElysiumTaskFailureName(0x29)), FString(TEXT("NPC had no follower boss")));
	Runner.bIdleAvailable = false;
	ElysiumSchedule::Start(State, EElysiumScheduleId::IdleDisposition, Runner);
	Runner.Flags.AddOblivious();
	Runner.Flags.Set(EElysiumNpcFlag::NO_DIALOG);
	TestTrue(TEXT("failed activity runs the failure transaction and ends the pass"), ElysiumSchedule::Tick(State, Runner, 1.0));
	TestEqual(TEXT("the kernel invokes TaskFail and keeps the program for the route"), Runner.FailureReasons.Last(), 0x15);
	ElysiumSchedule::Tick(State, Runner, 1.1, &Runner.Conditions);
	TestEqual(TEXT("...which the next pass takes into FAIL"), State.Current, EElysiumScheduleId::Fail);
	TestFalse(TEXT("failure releases NO_DIALOG"), Runner.Flags.Has(EElysiumNpcFlag::NO_DIALOG));
	TestFalse(TEXT("failure clears the bookkeeping bit"), Runner.Flags.Has(EElysiumNpcFlag2::MADE_OBLIVIOUS));
	TestTrue(TEXT("retail failure retains the oblivious refcount"), Runner.Flags.IsOblivious());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleCompletionHostTest,
	"Elysium.Substrate.Schedule.CompletionHost", GElysiumScheduleTestFlags)
bool FElysiumScheduleCompletionHostTest::RunTest(const FString&)
{
	FRecordingRunner Runner;
	FElysiumScheduleState State;
	Runner.ObservedState = &State;
	ElysiumSchedule::Start(State, EElysiumScheduleId::IdleDisposition, Runner);
	ElysiumSchedule::Start(State, EElysiumScheduleId::ScriptedMoveToGoal, Runner);
	TestTrue(TEXT("schedule change observes the outgoing program"),
		Runner.OutgoingSchedules.Last() == EElysiumScheduleId::IdleDisposition);
	Runner.bPathAvailable = true;
	Runner.MovementResult = EElysiumMoveWatch::Arrived;
	TestFalse(TEXT("an immediately arrived goal finishes the program"), ElysiumSchedule::Tick(State, Runner, 0.0));
	TestEqual(TEXT("schedule done is emitted once at the last completion"), Runner.CompletedSchedules, 1);
	ElysiumSchedule::Start(State, EElysiumScheduleId::ScriptedFollowPath, Runner);
	TestTrue(TEXT("a continuing path yields at the maintenance bound"), ElysiumSchedule::Tick(State, Runner, 1.0));
	TestTrue(TEXT("the bounded pass retains the path program"), State.Current == EElysiumScheduleId::ScriptedFollowPath);
	TestTrue(TEXT("the bounded pass closes DELAY_INTERRUPTS"), State.bDidMaintainSchedule);
	FElysiumNpcConditions Failed = FElysiumNpcConditions::Of({EElysiumNpcCond::TaskFailed});
	const int32 PreviousFailures = Runner.FailureReasons.Num();
	TestTrue(TEXT("external TASK_FAILED routes a still-running program into FAIL"), ElysiumSchedule::Tick(State, Runner, 2.0, &Failed));
	TestEqual(TEXT("the external route installs base FAIL"), State.Current, EElysiumScheduleId::Fail);
	TestEqual(TEXT("routing an external failure does not duplicate TaskFail"), Runner.FailureReasons.Num(), PreviousFailures);
	return true;
}

// ============================================================================================
// Story 25 — the kernel's failure route and the random wait
// (`docs/vtmb/npc-ai/schedule-kernel.md` -> "The kernel's failure route and the base programs,
// walked"). A task failing inside the loop ends the pass with its program installed
// (`MaintainSchedule`'s `HasCondition(TASK_FAILED)` exit to `0x102821ae`); the next pass's top arm
// installs `GetFailSchedule`'s answer — through slot 440 — and keeps running it in the same loop;
// `FAIL` stands one second and holds on PVS; `TASK_WAIT_RANDOM` draws `RandomFloat(0.1, arg)`;
// `ClearSchedule` from a task leaves no program.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleFailRouteTest,
	"Elysium.Substrate.Schedule.FailRoute", GElysiumScheduleTestFlags)
bool FElysiumScheduleFailRouteTest::RunTest(const FString&)
{
	// --- The failing pass ends; the next installs FAIL, stands WAIT 1, then WAIT_PVS, then ends ---
	{
		FRecordingRunner Runner;
		Runner.bMotor = false;   // the door program's first task fails
		FElysiumScheduleState State;

		ElysiumSchedule::Start(State, EElysiumScheduleId::BackAwayFromDoorNe, Runner);
		TestTrue(TEXT("the failing pass ends with a program still installed"),
			ElysiumSchedule::Tick(State, Runner, 0.0));
		TestEqual(TEXT("...the one that failed, not its route (0x10273fc0 leaves the status word)"),
			State.Current, EElysiumScheduleId::BackAwayFromDoorNe);
		TestTrue(TEXT("TaskFail raised TASK_FAILED"),
			Runner.Conditions.Has(EElysiumNpcCond::TaskFailed));
		TestFalse(TEXT("nothing of FAIL ran on the failing pass"),
			Runner.Saw(TEXT("SetActivity ACT_IDLE")));
		TestTrue(TEXT("the failing pass is the 0x102821ae exit: m_bDidMaintainSchedule closes"),
			State.bDidMaintainSchedule);

		TestTrue(TEXT("the next pass installs the route and runs it"),
			ElysiumSchedule::Tick(State, Runner, 0.1, &Runner.Conditions));
		TestEqual(TEXT("that program is FAIL"), State.Current, EElysiumScheduleId::Fail);
		TestTrue(TEXT("the route's id went through slot 440"),
			Runner.Saw(TEXT("TranslateSchedule SCHED_FAIL")));
		TestTrue(TEXT("FAIL's STOP_MOVING and SET_ACTIVITY ran on the routing pass"),
			Runner.Saw(TEXT("SetActivity ACT_IDLE")));
		TestEqual(TEXT("and it now waits its one second"), State.TaskIndex, 2);
		TestEqual(TEXT("WAIT 1 has no random term"), State.TaskEndsAt, 1.1, 1e-9);
		TestEqual(TEXT("conditions cleared once for the door program, once for FAIL"),
			Runner.ConditionClears, 2);
		TestFalse(TEXT("the install zeroed TASK_FAILED (SetSchedule 0x10280e50)"),
			Runner.Conditions.Has(EElysiumNpcCond::TaskFailed));

		Runner.bVisible = false;
		TestTrue(TEXT("before one second FAIL holds"), ElysiumSchedule::Tick(State, Runner, 0.5));
		TestTrue(TEXT("past one second an unseen body holds on WAIT_PVS"),
			ElysiumSchedule::Tick(State, Runner, 1.1));
		TestEqual(TEXT("the PVS hold is FAIL's last task"), State.TaskIndex, 3);
		Runner.bVisible = true;
		TestFalse(TEXT("seen, FAIL completes and the NPC selects again"),
			ElysiumSchedule::Tick(State, Runner, 1.2));
		TestEqual(TEXT("FAIL completes as a schedule, not a failure"), Runner.CompletedSchedules, 1);
	}

	// --- TASK_SET_FAIL_SCHEDULE's operand still wins over FAIL -----------------------------------
	{
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		ElysiumSchedule::Start(State, EElysiumScheduleId::IdleDisposition, Runner);
		State.FailScheduleOverride = EElysiumScheduleId::AlertLookAroundNi;
		Runner.bIdleAvailable = false;
		ElysiumSchedule::Tick(State, Runner, 0.0);
		ElysiumSchedule::Tick(State, Runner, 0.1, &Runner.Conditions);
		TestEqual(TEXT("m_failSchedule answers before the base FAIL"), State.Current,
			EElysiumScheduleId::AlertLookAroundNi);
	}

	// --- Slot 440 on the route, and not on the miss arm ------------------------------------------
	// `SetSchedule(int)` (`0x102cc1f0`) translates the id `GetFailSchedule` answered; its own miss
	// fallback pushes the literal 1 straight to `GetScheduleOfType` (`0x102cc229`).
	{
		FRecordingRunner Runner;
		Runner.Translations.Add(EElysiumScheduleId::IdleStand, EElysiumScheduleId::AlertLookAroundNi);
		FElysiumScheduleState State;
		ElysiumSchedule::Start(State, EElysiumScheduleId::IdleDisposition, Runner);
		State.FailScheduleOverride = EElysiumScheduleId::IdleStand;
		Runner.bIdleAvailable = false;
		ElysiumSchedule::Tick(State, Runner, 0.0);
		ElysiumSchedule::Tick(State, Runner, 0.1, &Runner.Conditions);
		TestEqual(TEXT("a fail schedule of Idle_Stand lands where the class's slot 440 sends it"),
			State.Current, EElysiumScheduleId::AlertLookAroundNi);

		FRecordingRunner Miss;
		Miss.Translations.Add(EElysiumScheduleId::IdleStand, EElysiumScheduleId::AlertLookAroundNi);
		FElysiumScheduleState MissState;
		ElysiumSchedule::Start(MissState, EElysiumScheduleId::None, Miss);
		TestEqual(TEXT("the miss arm's IDLE_STAND is untranslated"), MissState.Current,
			EElysiumScheduleId::IdleStand);
		TestFalse(TEXT("...and never asked slot 440"), Miss.Saw(TEXT("TranslateSchedule")));
	}

	// --- The two base programs, decoded ----------------------------------------------------------
	{
		const FElysiumSchedule* Fail = ElysiumScheduleFor(EElysiumScheduleId::Fail);
		const FElysiumSchedule* IdleStand = ElysiumScheduleFor(EElysiumScheduleId::IdleStand);
		if (TestNotNull(TEXT("FAIL is registered"), Fail)
			&& TestNotNull(TEXT("IDLE_STAND is registered"), IdleStand))
		{
			TestEqual(TEXT("FAIL is retail 0x43"), ElysiumScheduleNumber(EElysiumScheduleId::Fail), 0x43);
			TestEqual(TEXT("IDLE_STAND is retail 1"),
				ElysiumScheduleNumber(EElysiumScheduleId::IdleStand), 1);
			TestTrue(TEXT("FAIL is interrupted by CAN_MELEE_ATTACK1"),
				Fail->Interrupts.Has(EElysiumNpcCond::CanMeleeAttack1));
			TestFalse(TEXT("FAIL is not interrupted by NEW_ENEMY"),
				Fail->Interrupts.Has(EElysiumNpcCond::NewEnemy));
			TestEqual(TEXT("IDLE_STAND waits five seconds"), IdleStand->Tasks[2].Param, 5.f);
		}
	}

	// --- TASK_WAIT_RANDOM's floor is 0.1 ---------------------------------------------------------
	{
		FRecordingRunner Runner;
		Runner.RandomFraction = 0.f;
		FElysiumScheduleState State;
		// `BACK_AWAY_FROM_DOOR_WAIT_NE`: FACE_SAVEPOSITION; WAIT 2; WAIT_RANDOM 2.
		ElysiumSchedule::Start(State, EElysiumScheduleId::BackAwayFromDoorWaitNe, Runner);
		ElysiumSchedule::Tick(State, Runner, 0.0);
		TestTrue(TEXT("WAIT_RANDOM at its lowest draw still starts running"),
			ElysiumSchedule::Tick(State, Runner, 2.0));
		TestEqual(TEXT("and holds the 0.1 s floor"), State.TaskEndsAt, 2.1, 1e-6);
	}

	// --- ClearSchedule from inside a task ---------------------------------------------------------
	{
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		Runner.ObservedState = &State;
		ElysiumSchedule::Start(State, EElysiumScheduleId::AlertLookAroundNi, Runner);
		State.FailScheduleOverride = EElysiumScheduleId::BackAwayFromDoorNe;
		Runner.Flags.Set(EElysiumNpcFlag::PRESERVE_PATH);
		Runner.bClearFromActivityTask = true;   // the program's first task body asks
		const int32 ClearsBefore = Runner.ConditionClears;
		TestFalse(TEXT("a task's ClearSchedule ends the tick with no program"),
			ElysiumSchedule::Tick(State, Runner, 0.0));
		TestTrue(TEXT("the task that asked did run"),
			Runner.Saw(TEXT("SetActivity ACT_ALERT_FIDGET_LOOKAROUND")));
		TestFalse(TEXT("nothing is installed"), State.IsRunning());
		TestFalse(TEXT("PRESERVE_PATH is cleared"), Runner.Flags.Has(EElysiumNpcFlag::PRESERVE_PATH));
		const int32 PreserveAt = Runner.Calls.IndexOfByKey(FString(TEXT("ClearPreservePath")));
		const int32 ChangeAt = Runner.Calls.FindLastByPredicate(
			[](const FString& C) { return C == TEXT("OnScheduleChange"); });
		TestTrue(TEXT("slot 435 runs after the PRESERVE_PATH clear"),
			PreserveAt != INDEX_NONE && ChangeAt > PreserveAt);
		TestEqual(TEXT("slot 435 is dispatched with no program"), Runner.OutgoingSchedules.Last(),
			EElysiumScheduleId::None);
		TestEqual(TEXT("ClearSchedule leaves the conditions alone"), Runner.ConditionClears, ClearsBefore);
		TestEqual(TEXT("...and m_failSchedule (+0x5c54 is outside +0x5c38..+0x5c4c)"),
			State.FailScheduleOverride, EElysiumScheduleId::BackAwayFromDoorNe);
		TestEqual(TEXT("and is not a schedule completion"), Runner.CompletedSchedules, 0);
	}

	// --- A request raised outside a task step is honoured before any task work ------------------
	{
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		ElysiumSchedule::Start(State, EElysiumScheduleId::AlertLookAroundNi, Runner);
		Runner.bRequestClear = true;
		TestFalse(TEXT("the next tick clears at its top"), ElysiumSchedule::Tick(State, Runner, 0.0));
		TestFalse(TEXT("nothing is installed"), State.IsRunning());
		TestFalse(TEXT("and no task of the cleared program ran"),
			Runner.Saw(TEXT("SetActivity ACT_ALERT_FIDGET_LOOKAROUND")));
	}

	// --- A request pending at an install is superseded by it ------------------------------------
	// Retail's `ClearSchedule` then `SetSchedule` leaves the new program standing.
	{
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		Runner.bRequestClear = true;
		ElysiumSchedule::Start(State, EElysiumScheduleId::AlertLookAroundNi, Runner);
		TestFalse(TEXT("the install consumed the stale request"), Runner.bRequestClear);
		TestTrue(TEXT("the installed program runs"), ElysiumSchedule::Tick(State, Runner, 0.0));
		TestEqual(TEXT("...and is still installed"), State.Current, EElysiumScheduleId::AlertLookAroundNi);
	}
	return true;
}

// ============================================================================================
// Story 25 — slot 440 on a Troika leaf. `CAI_BaseNPCTroika::TranslateSchedule` (`0x102b12f0`)
// sends `1 IDLE_STAND` and `0x6b` to `0x6b IDLE_DISPOSITION`, or to `0x132 LAUGHING` under
// `D_MILDLY_CRAZY` — the arm every `SET_FAIL_SCHEDULE Idle_Stand` program reaches. The frenzied
// pre-table (`0x102b11c0`) and the `0x132` target are named seams until 25b / 21a register them.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleTroikaTranslateTest,
	"Elysium.Substrate.Schedule.TroikaTranslate", GElysiumScheduleTestFlags)
bool FElysiumScheduleTroikaTranslateTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("__schedtranslate_test__"), 0x54524e53);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("the guard leaf constructs"), Guard))
	{
		return false;
	}
	TestEqual(TEXT("Idle_Stand translates to IDLE_DISPOSITION on a Troika leaf"),
		Guard->TranslateSchedule(EElysiumScheduleId::IdleStand), EElysiumScheduleId::IdleDisposition);
	TestEqual(TEXT("0x6b translates to itself"),
		Guard->TranslateSchedule(EElysiumScheduleId::IdleDisposition), EElysiumScheduleId::IdleDisposition);
	TestEqual(TEXT("FAIL has no Troika row"),
		Guard->TranslateSchedule(EElysiumScheduleId::Fail), EElysiumScheduleId::Fail);
	TestEqual(TEXT("a Troika id with no row is identity"),
		Guard->TranslateSchedule(EElysiumScheduleId::MeleeIdle), EElysiumScheduleId::MeleeIdle);
	// The seams: the answer stands in for the unregistered target rather than missing into IDLE_STAND.
	Guard->NpcFlags.Set(EElysiumNpcFlag2::D_MILDLY_CRAZY);
	TestEqual(TEXT("D_MILDLY_CRAZY's 0x132 is a seam answering IDLE_DISPOSITION until 21a"),
		Guard->TranslateSchedule(EElysiumScheduleId::IdleStand), EElysiumScheduleId::IdleDisposition);
	Guard->NpcFlags.SetFrenziedWord(0x100);
	TestEqual(TEXT("the frenzied pre-table's 0xc9 is a seam answering MELEE_IDLE until 25b"),
		Guard->TranslateSchedule(EElysiumScheduleId::MeleeIdle), EElysiumScheduleId::MeleeIdle);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
