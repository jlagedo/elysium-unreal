// The schedule/task kernel, asserted against a recording runner with no world and no engine.
//
// The kernel is deliberately content-free — task bodies reach the world through
// `IElysiumScheduleRunner` — so the whole task program of a recovered schedule can be run and
// asserted here. The behaviour under test is `docs/vtmb/npc-ai-reverse-engineering.md` ->
// "The idle branch, decided" (the two idle schedules) and "Door-obstruction schedule selection".
//
// Three shapes worth holding onto while reading:
//
//   - a task runs across thinks. `Tick` answers "still running", and the caller waits the delay the
//     kernel hands back rather than re-asking every frame.
//   - `TASK_WAIT_PVS` is the only task that is not on a clock. It holds indefinitely, which is the
//     throttle the idle schedule exists to apply to an NPC nobody can see.
//   - a failed task goes to the schedule's fail schedule, and a schedule with none simply ends.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Substrate/ElysiumSchedule.h"

static constexpr EAutomationTestFlags GElysiumScheduleTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One line per task body call, so a program's shape is asserted rather than its side effects.
	struct FRecordingRunner final : IElysiumScheduleRunner
	{
		TArray<FString> Calls;

		// Knobs a test turns to drive each branch.
		bool bVisible = true;
		float IdleClipSeconds = 2.f;
		float ActivitySeconds = 1.f;
		bool bIdleAvailable = true;
		bool bActivityResolves = true;
		bool bMotor = true;
		// `WAIT_RANDOM` draws through the NPC schedule stream in the real runner; here it is fixed
		// so a duration is a literal in the test rather than a seed to reverse-engineer.
		float RandomFraction = 1.f;

		virtual float RunSpecialIdleActivity(double Now) override
		{
			Calls.Add(TEXT("SpecialIdleActivity"));
			return bIdleAvailable ? IdleClipSeconds : -1.f;
		}
		virtual bool IsBodyVisible() const override { return bVisible; }
		virtual float PlayActivity(const FString& Activity) override
		{
			Calls.Add(FString::Printf(TEXT("SetActivity %s"), *Activity));
			return bActivityResolves ? ActivitySeconds : -1.f;
		}
		virtual bool FaceSavePosition() override
		{
			Calls.Add(TEXT("FaceSavePosition"));
			return bMotor;
		}
		virtual bool StepAwayFromSavePosition(float DistanceCm) override
		{
			Calls.Add(FString::Printf(TEXT("StepAway %.0f"), DistanceCm));
			return bMotor;
		}
		virtual float RandomSeconds(float Max) override { return Max * RandomFraction; }
		virtual void RecordScheduleEvent(const FString& Row) override
		{
			Calls.Add(FString::Printf(TEXT("trace: %s"), *Row));
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
		double Delay = 0.0;
		while (Thinks < MaxThinks && ElysiumSchedule::Tick(State, Runner, Now, Delay))
		{
			++Thinks;
			Now += FMath::Max(0.01, Delay);
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
	double Delay = 0.0;

	ElysiumSchedule::Start(State, EElysiumScheduleId::IdleDisposition, Runner);

	// Long past the stance clip's end, so the only thing still holding is the PVS task.
	for (int32 i = 0; i < 60; ++i)
	{
		TestTrue(TEXT("an unseen NPC's idle schedule stays open"),
			ElysiumSchedule::Tick(State, Runner, Now, Delay));
		Now += FMath::Max(0.01, Delay);
	}
	const int32 SelectionsWhileUnseen = Runner.Calls.FilterByPredicate([](const FString& C)
	{
		return C == TEXT("SpecialIdleActivity");
	}).Num();
	TestEqual(TEXT("and selects exactly once — the throttle is the whole point"),
		SelectionsWhileUnseen, 1);
	TestTrue(TEXT("an unseen NPC is polled slowly rather than every think"), Now >= 25.0);

	// Coming back into view releases it, and the schedule ends so the NPC selects again.
	Runner.bVisible = true;
	TestFalse(TEXT("becoming visible completes the wait and ends the schedule"),
		ElysiumSchedule::Tick(State, Runner, Now, Delay));
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
		double Delay = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::IdleDisposition, Runner);
		TestFalse(TEXT("a body with no stance machine ends its idle schedule"),
			ElysiumSchedule::Tick(State, Runner, Now, Delay));
		TestTrue(TEXT("and the trace names the task that failed"),
			Runner.Saw(TEXT("TASK_SPECIAL_IDLE_ACTIVITY failed")));
		TestFalse(TEXT("nothing is left running"), State.IsRunning());
	}

	// --- A failing task WITH a fail schedule falls through to it ------------------------------
	// Cover that cannot be held must still leave the NPC out of the doorway, which is what
	// `SCHED_TROIKA_TAKE_COVER_HINT_DOOR`'s fall-through to backing away is for.
	{
		FRecordingRunner Runner;
		Runner.bActivityResolves = false;   // the cover pose will not resolve
		FElysiumScheduleState State;
		double Now = 0.0;
		double Delay = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::TakeCoverHintDoor, Runner);
		ElysiumSchedule::Tick(State, Runner, Now, Delay);
		TestTrue(TEXT("unheld cover falls through to backing away from the door"),
			Runner.Saw(TEXT("SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE")));
		TestEqual(TEXT("and that is the schedule now running"), State.Current,
			EElysiumScheduleId::BackAwayFromDoorNe);
	}

	// --- An unregistered schedule is refused by name, not silently skipped --------------------
	{
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		TestFalse(TEXT("an unregistered schedule does not start"),
			ElysiumSchedule::Start(State, EElysiumScheduleId::None, Runner));
		TestTrue(TEXT("and says so rather than idling quietly"), Runner.Saw(TEXT("refused schedule")));
		TestFalse(TEXT("leaving nothing half-started"), State.IsRunning());
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
		double Delay = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::BackAwayFromDoorNe, Runner);
		TestFalse(TEXT("a bodiless NPC cannot back away, and says so"),
			ElysiumSchedule::Tick(State, Runner, Now, Delay));
		TestTrue(TEXT("the trace names the motor task that failed"),
			Runner.Saw(TEXT("TASK_FACE_SAVEPOSITION failed")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
