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

		// `TASK_MOVE_AWAY_PATH` runs the REAL rule (11.14) rather than a double of it: the runner
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
			FVector Destination = FVector::ZeroVector;
			LastRetreat = ElysiumSchedule::StepAwayFromSavePosition(bMotor ? &Motor : nullptr,
				Origin, SavePosition, DistanceCm, Destination);
			Calls.Add(FString::Printf(TEXT("StepAway %.0f -> %s"), DistanceCm,
				ElysiumSchedule::RetreatResultName(LastRetreat)));
			return LastRetreat == ElysiumSchedule::ERetreat::Moving;
		}
		virtual float RandomSeconds(float Max) override { return Max * RandomFraction; }

		// LIFE5 — the death ladder's one rung. `PlayableDeathActivities` is this body's vocabulary:
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
		double Delay = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::BackAwayFromDoorNe, Runner);
		int32 Thinks = 0;
		while (Thinks < 8 && ElysiumSchedule::Tick(State, Runner, Now, Delay))
		{
			++Thinks;
			Now += FMath::Max(0.01, Delay);
		}
		TestFalse(TEXT("the schedule ends rather than repeating a retreat that is not one"),
			State.IsRunning());
		TestTrue(TEXT("the trace names the failed move task"),
			Runner.Saw(TEXT("TASK_MOVE_AWAY_PATH failed")));
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
		double Delay = 0.0;

		TestTrue(TEXT("the death schedule is registered"),
			ElysiumSchedule::Start(State, EElysiumScheduleId::Die, Runner));
		TestTrue(TEXT("starting it traces its name"), Runner.Saw(TEXT("SCHED_DIE")));

		TestFalse(TEXT("the program ends inside its first think"),
			ElysiumSchedule::Tick(State, Runner, Now, Delay));

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
		double Delay = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::Die, Runner);
		TestTrue(TEXT("a real death clip holds the task open for its own length"),
			ElysiumSchedule::Tick(State, Runner, Now, Delay));

		const TArray<FString> Rungs = DeathRungs(Runner);
		if (TestEqual(TEXT("the ladder stops at the first rung that resolves"), Rungs.Num(), 1))
		{
			TestEqual(TEXT("and that rung is ACT_DIESIMPLE"), Rungs[0],
				FString(TEXT("DeathActivity ACT_DIESIMPLE")));
		}
		TestEqual(TEXT("the hold is the resolved clip's own length"), Delay,
			static_cast<double>(Runner.DeathClipSeconds), 1e-6);

		Now += Runner.DeathClipSeconds;
		TestFalse(TEXT("and the program ends when the clip does"),
			ElysiumSchedule::Tick(State, Runner, Now, Delay));
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
		double Delay = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::Die, Runner);
		ElysiumSchedule::Tick(State, Runner, Now, Delay);

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
		double Delay = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::Die, Runner);
		ElysiumSchedule::Tick(State, Runner, Now, Delay);

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
		double Delay = 0.0;

		ElysiumSchedule::Start(State, EElysiumScheduleId::Die, Runner);
		TestFalse(TEXT("the program ends"), ElysiumSchedule::Tick(State, Runner, Now, Delay));
		TestFalse(TEXT("and it ended by completing, not by failing"),
			Runner.Saw(TEXT("TASK_PLAY_DEATH_SEQUENCE failed")));
		TestTrue(TEXT("the trace says nothing resolved"),
			Runner.Saw(TEXT("-> (nothing resolved)")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
