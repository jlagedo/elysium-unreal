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
#include "Substrate/ElysiumScheduleCorpus.h"
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
		TArray<FString> Calls;
		TArray<int32> FailureReasons;
		int32 CompletedSchedules = 0;
		const FElysiumScheduleState* ObservedState = nullptr;
		TArray<int32> OutgoingSchedules;
		// `TASK_WAIT_FOR_MOVEMENT`'s scripted outcome: still travelling, arrived, or failed.
		enum class EMove : uint8 { Moving, Arrived, Failed };
		EMove MovementResult = EMove::Failed;
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
		TMap<int32, int32> Translations;
		virtual int32 TranslateSchedule(int32 Id) override
		{
			Calls.Add(FString::Printf(TEXT("TranslateSchedule 0x%x"), Id));
			const int32* Translated = Translations.Find(Id);
			return Translated ? *Translated : Id;
		}
		virtual void ScheduleDone() override
		{
			++CompletedSchedules;
			// `NextScheduledTask 0x10280f40` raises condition 0x5d before the next iteration's
			// `IsScheduleValid 0x10280ff0`; the recording runner must expose that live write.
			Conditions.Set(EElysiumNpcCond::ScheduleDone);
		}
		virtual bool HasMaintenanceCondition(EElysiumNpcCond Cond) const override
		{
			// `102819d5` and the later `10281f50` read the NPC's live condition set, including writes
			// made by StartTask/RunTask after an install cleared the incoming pass snapshot.
			return Conditions.Has(Cond);
		}

		// Knobs a test turns to drive each branch.
		bool bVisible = true;
		float IdleClipSeconds = 2.f;
		bool bIdleAvailable = true;
		bool bIdealActivityCurrent = true;
		// `WAIT_RANDOM` draws through the NPC schedule stream in the real runner; here it is fixed
		// so a duration is a literal in the test rather than a seed to reverse-engineer.
		float RandomFraction = 1.f;

		// A body that asks for `ClearSchedule` from inside its `TASK_SET_ACTIVITY` arm, the shape of
		// the task-body callers.
		bool bClearFromActivityTask = false;
		// `WAIT_RANDOM` draws `RandomFloat(0.1, arg)`; here it is fixed so a duration is a literal.
		float RandomSeconds(float Max) const { return 0.1f + (Max - 0.1f) * RandomFraction; }

		// --- The scripted task body (slots 442 / 444) ---------------------------------------------
		//
		// Story 8 wave 2: the kernel reaches task bodies only through `StartTaskForMaintenance` /
		// `RunTaskForMaintenance` (the retail `StartTask` / `RunTask` dispatch at `0x10281e10` /
		// `0x1028202c`). The kernel's contract -- WHEN it asks, what a status it reads back does, the
		// fail route, the loop bounds -- is what these cases assert, so the body here is a small
		// script keyed on the task's retail name, standing for the retail arms whose own semantics
		// the `NpcKernelStartTask19` / `NpcKernelRunTask19` suites assert on a real NPC. A task the
		// script does not name completes on its start.
		static FString TaskName(const FElysiumScheduleStep& Step)
		{
			return FElysiumScheduleCorpus::Get().TaskOps().NameOf(Step.TaskId);
		}
		static void Complete(FElysiumScheduleState& State) { State.TaskStatus = EElysiumTaskStatus::Complete; }
		virtual void StartTaskForMaintenance(FElysiumScheduleState& State, const FElysiumScheduleStep& Step,
			double Now) override
		{
			const FString Name = TaskName(Step);
			if (Name == TEXT("TASK_SPECIAL_IDLE_ACTIVITY"))
			{
				Calls.Add(TEXT("SpecialIdleActivity"));
				if (!bIdleAvailable)
				{
					TaskFail(0x15);
					return;
				}
				State.TaskEndsAt = Now + IdleClipSeconds;
			}
			else if (Name == TEXT("TASK_SET_ACTIVITY"))
			{
				const FString* Activity =
					FElysiumScheduleCorpus::Get().Activities().NameOf(static_cast<int32>(Step.Data));
				Calls.Add(FString::Printf(TEXT("SetActivity %s"), Activity != nullptr ? **Activity : TEXT("?")));
				if (bClearFromActivityTask)
				{
					bRequestClear = true;
				}
				State.TaskEndsAt = Now + 1.0;   // the Troika arm's one-second watchdog
			}
			else if (Name == TEXT("TASK_WAIT"))
			{
				State.TaskEndsAt = Now + Step.Data;
			}
			else if (Name == TEXT("TASK_WAIT_RANDOM"))
			{
				State.TaskEndsAt = Now + RandomSeconds(Step.Data);
			}
			else if (Name == TEXT("TASK_WAIT_PVS") || Name == TEXT("TASK_WAIT_FOR_MOVEMENT"))
			{
				// Their start arms do nothing; the run arms decide.
			}
			else if (Name == TEXT("TASK_MAKE_OBLIVIOUS"))
			{
				Calls.Add(FString::Printf(TEXT("MakeOblivious %s"), Step.Data != 0.f ? TEXT("TRUE") : TEXT("FALSE")));
				if (Step.Data != 0.f) { AddOblivious(); } else { RemoveOblivious(); }
				Complete(State);
			}
			else if (Name == TEXT("TASK_SET_NPC_FLAG"))
			{
				const uint32 EncodedFlag = Step.RawWord();
				if ((EncodedFlag & 0x80000000u) != 0)
				{
					Flags.Set(static_cast<EElysiumNpcFlag2>(EncodedFlag & 0x7fffffffu));
				}
				else
				{
					const EElysiumNpcFlag Flag = static_cast<EElysiumNpcFlag>(EncodedFlag);
					Calls.Add(FString::Printf(TEXT("SetNpcFlag %s"), FElysiumNpcFlags::LexToString(Flag)));
					Flags.Set(Flag);
				}
				Complete(State);
			}
			else
			{
				Complete(State);
			}
		}
		virtual void RunTaskForMaintenance(FElysiumScheduleState& State, const FElysiumScheduleStep& Step,
			double Now) override
		{
			const FString Name = TaskName(Step);
			if (Name == TEXT("TASK_SET_ACTIVITY"))
			{
				if (bIdealActivityCurrent || Now >= State.TaskEndsAt) { Complete(State); }
			}
			else if (Name == TEXT("TASK_WAIT_PVS"))
			{
				if (bVisible) { Complete(State); }
			}
			else if (Name == TEXT("TASK_WAIT_FOR_MOVEMENT"))
			{
				if (MovementResult == EMove::Arrived) { Complete(State); }
				else if (MovementResult == EMove::Failed) { TaskFail(0x0c); }
			}
			else if (Now >= State.TaskEndsAt)
			{
				Complete(State);
			}
		}

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

		virtual void RecordScheduleEvent(const FString& Row) override
		{
			Calls.Add(FString::Printf(TEXT("trace: %s"), *Row));
		}

		// The incapacitation verbs and the two install rules, recorded rather than simulated: the
		// kernel owns WHEN they fire, and that ordering is what these tests assert.
		FElysiumNpcFlags Flags;
		// The NPC's `m_iIsOblivious` beside the combat character's flag words, written the way
		// `FElysiumNpcBase::AddOblivious` / `RemoveOblivious` write the pair.
		int32 ObliviousCount = 0;
		bool IsOblivious() const { return ObliviousCount > 0; }
		void AddOblivious()
		{
			Flags.Set(EElysiumNpcFlag2::MADE_OBLIVIOUS);
			++ObliviousCount;
		}
		void RemoveOblivious()
		{
			ObliviousCount = FMath::Max(0, ObliviousCount - 1);
			Flags.Clear(EElysiumNpcFlag2::MADE_OBLIVIOUS);
		}
		int32 ConditionClears = 0;
		virtual void ClearConditions() override
		{
			++ConditionClears;
			Conditions.Reset();
			Calls.Add(TEXT("ClearConditions"));
		}
		virtual void OnScheduleChange(int32) override
		{
			if (ObservedState) OutgoingSchedules.Add(ObservedState->Current);
			Calls.Add(TEXT("OnScheduleChange"));
			Flags.BeginScheduleChange();
			if (Flags.ApplyScheduleChangeMasks())
			{
				RemoveOblivious();
			}
			Flags.FinishScheduleChange();
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

		ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION, Runner);
		TestTrue(TEXT("a body with no stance machine fails its idle task and the pass ends"),
			ElysiumSchedule::Tick(State, Runner, Now));
		TestTrue(TEXT("and the trace names the task that failed"),
			Runner.FailureReasons.Contains(0x15));
		TestEqual(TEXT("the failed program stands until the next pass"), State.Current,
			ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION));
		TestTrue(TEXT("the next pass routes the failure"),
			ElysiumSchedule::Tick(State, Runner, Now + 0.1, &Runner.Conditions));
		TestEqual(TEXT("base FAIL (0x43) is the route when none was set"), State.Current,
			ElysiumScheduleGlobalId(ElysiumSched::FAIL));
	}

	// --- An unregistered schedule installs IDLE_STAND (`SetSchedule(int)` 0x102cc1f0) -----------
	{
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		TestTrue(TEXT("an unregistered schedule still installs a program"),
			ElysiumSchedule::Start(State, ElysiumScheduleId::None, Runner));
		TestEqual(TEXT("and that program is base IDLE_STAND"), State.Current,
			ElysiumScheduleGlobalId(ElysiumSched::IDLE_STAND));
		TestTrue(TEXT("the trace names the miss"), Runner.Saw(TEXT("GetScheduleOfType(): No CASE")));
		TestTrue(TEXT("a miss is not a task failure"), Runner.FailureReasons.IsEmpty());
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
		ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_MESMERIZED, Runner));

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

	// Fixture correction from the retail loop: `10281980` advances the completed last task and
	// raises SCHEDULE_DONE; that same iteration reaches `10281be5 SetSchedule(NULL)`, whose
	// `10280e53` slot-435 call releases these bits before the missing-schedule return.
	TestFalse(TEXT("D_IS_BUSY is released by the loop's replacement install"),
		Runner.Flags.Has(EElysiumNpcFlag::D_IS_BUSY));
	TestFalse(TEXT("the obliviousness refcount is released with it"), Runner.IsOblivious());

	// A later real install leaves the already released state clear.
	ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI, Runner);
	TestFalse(TEXT("D_IS_BUSY released by the next install"),
		Runner.Flags.Has(EElysiumNpcFlag::D_IS_BUSY));
	TestFalse(TEXT("NO_DIALOG released by the next install"),
		Runner.Flags.Has(EElysiumNpcFlag::NO_DIALOG));
	TestFalse(TEXT("DONT_INVESTIGATE released by the next install"),
		Runner.Flags.Has(EElysiumNpcFlag::DONT_INVESTIGATE));
	TestFalse(TEXT("and the obliviousness refcount is released with them"),
		Runner.IsOblivious());
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

		ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_MESMERIZED, Runner);
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
		ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI, Runner);
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
		ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE, Runner);
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
		ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE, Runner);
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
	TestTrue(TEXT("an absent program installs IDLE_STAND"), ElysiumSchedule::Start(State, ElysiumScheduleId::None, Runner));
	TestTrue(TEXT("a missing schedule is not a TaskFail"), Runner.FailureReasons.IsEmpty());
	TestEqual(TEXT("the source reason table names follower failure"), FString(ElysiumTaskFailureName(0x29)), FString(TEXT("NPC had no follower boss")));
	Runner.bIdleAvailable = false;
	ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION, Runner);
	Runner.AddOblivious();
	Runner.Flags.Set(EElysiumNpcFlag::NO_DIALOG);
	TestTrue(TEXT("failed activity runs the failure transaction and ends the pass"), ElysiumSchedule::Tick(State, Runner, 1.0));
	TestEqual(TEXT("the kernel invokes TaskFail and keeps the program for the route"), Runner.FailureReasons.Last(), 0x15);
	ElysiumSchedule::Tick(State, Runner, 1.1, &Runner.Conditions);
	TestEqual(TEXT("...which the next pass takes into FAIL"), State.Current, ElysiumScheduleGlobalId(ElysiumSched::FAIL));
	TestFalse(TEXT("failure releases NO_DIALOG"), Runner.Flags.Has(EElysiumNpcFlag::NO_DIALOG));
	TestFalse(TEXT("failure clears the bookkeeping bit"), Runner.Flags.Has(EElysiumNpcFlag2::MADE_OBLIVIOUS));
	TestTrue(TEXT("retail failure retains the oblivious refcount"), Runner.IsOblivious());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleCompletionHostTest,
	"Elysium.Substrate.Schedule.CompletionHost", GElysiumScheduleTestFlags)
bool FElysiumScheduleCompletionHostTest::RunTest(const FString&)
{
	FRecordingRunner Runner;
	FElysiumScheduleState State;
	Runner.ObservedState = &State;
	ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION, Runner);
	// Troika's actual three-step path program replaces the deleted synthetic TARGET_CHASE.
	ElysiumSchedule::Start(State, 0x46, Runner);
	TestTrue(TEXT("schedule change observes the outgoing program"),
		Runner.OutgoingSchedules.Last()
			== ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION));
	Runner.MovementResult = FRecordingRunner::EMove::Arrived;
	TestFalse(TEXT("an immediately arrived goal finishes the program"), ElysiumSchedule::Tick(State, Runner, 0.0));
	TestEqual(TEXT("schedule done is emitted once at the last completion"), Runner.CompletedSchedules, 1);
	Runner.MovementResult = FRecordingRunner::EMove::Moving;
	ElysiumSchedule::Start(State, 0x46, Runner);
	TestTrue(TEXT("a continuing path yields at the maintenance bound"), ElysiumSchedule::Tick(State, Runner, 1.0));
	TestTrue(TEXT("the bounded pass retains the path program"), State.Current == ElysiumScheduleGlobalId(0x46));
	TestTrue(TEXT("the bounded pass closes DELAY_INTERRUPTS"), State.bDidMaintainSchedule);
	FElysiumNpcConditions Failed = FElysiumNpcConditions::Of({EElysiumNpcCond::TaskFailed});
	const int32 PreviousFailures = Runner.FailureReasons.Num();
	TestTrue(TEXT("external TASK_FAILED routes a still-running program into FAIL"), ElysiumSchedule::Tick(State, Runner, 2.0, &Failed));
	TestEqual(TEXT("the external route installs base FAIL"), State.Current, ElysiumScheduleGlobalId(ElysiumSched::FAIL));
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
		// `SCHED_TROIKA_IDLE_DISPOSITION` declares NO `TASK_SET_FAIL_SCHEDULE`, so base `FAIL` is
		// its route; its first task is `TASK_SPECIAL_IDLE_ACTIVITY`, which the runner refuses. The
		// door program used to drive this and cannot any more: retail's text for it opens by naming
		// its own fail route (`SCHED_TROIKA_BACK_AWAY_FROM_DOOR_RUN_NE`), which is the whole point
		// of the task.
		FRecordingRunner Runner;
		Runner.bIdleAvailable = false;
		FElysiumScheduleState State;

		ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION, Runner);
		TestTrue(TEXT("the failing pass ends with a program still installed"),
			ElysiumSchedule::Tick(State, Runner, 0.0));
		TestEqual(TEXT("...the one that failed, not its route (0x10273fc0 leaves the status word)"),
			State.Current, ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION));
		TestTrue(TEXT("TaskFail raised TASK_FAILED"),
			Runner.Conditions.Has(EElysiumNpcCond::TaskFailed));
		TestFalse(TEXT("nothing of FAIL ran on the failing pass"),
			Runner.Saw(TEXT("SetActivity ACT_IDLE")));
		TestTrue(TEXT("the failing pass is the 0x102821ae exit: m_bDidMaintainSchedule closes"),
			State.bDidMaintainSchedule);

		TestTrue(TEXT("the next pass installs the route and runs it"),
			ElysiumSchedule::Tick(State, Runner, 0.1, &Runner.Conditions));
		TestEqual(TEXT("that program is FAIL"), State.Current, ElysiumScheduleGlobalId(ElysiumSched::FAIL));
		TestTrue(TEXT("the route's id went through slot 440"),
			Runner.Saw(TEXT("TranslateSchedule 0x43")));
		TestTrue(TEXT("FAIL's STOP_MOVING and SET_ACTIVITY ran on the routing pass"),
			Runner.Saw(TEXT("SetActivity ACT_IDLE")));
		TestEqual(TEXT("and it now waits its one second"), State.TaskIndex, 2);
		TestEqual(TEXT("WAIT 1 has no random term"), State.TaskEndsAt, 1.1, 1e-9);
		TestEqual(TEXT("conditions cleared once for the idle program, once for FAIL"),
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
		ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION, Runner);
		State.FailScheduleOverride = ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI;
		Runner.bIdleAvailable = false;
		ElysiumSchedule::Tick(State, Runner, 0.0);
		ElysiumSchedule::Tick(State, Runner, 0.1, &Runner.Conditions);
		TestEqual(TEXT("m_failSchedule answers before the base FAIL"), State.Current,
			ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI));
	}

	// --- Slot 440 on the route, and not on the miss arm ------------------------------------------
	// `SetSchedule(int)` (`0x102cc1f0`) translates the id `GetFailSchedule` answered; its own miss
	// fallback pushes the literal 1 straight to `GetScheduleOfType` (`0x102cc229`).
	{
		FRecordingRunner Runner;
		Runner.Translations.Add(ElysiumSched::IDLE_STAND, ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI);
		FElysiumScheduleState State;
		ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION, Runner);
		State.FailScheduleOverride = ElysiumSched::IDLE_STAND;
		Runner.bIdleAvailable = false;
		ElysiumSchedule::Tick(State, Runner, 0.0);
		ElysiumSchedule::Tick(State, Runner, 0.1, &Runner.Conditions);
		TestEqual(TEXT("a fail schedule of Idle_Stand lands where the class's slot 440 sends it"),
			State.Current, ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI));

		FRecordingRunner Miss;
		Miss.Translations.Add(ElysiumSched::IDLE_STAND, ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI);
		FElysiumScheduleState MissState;
		ElysiumSchedule::Start(MissState, ElysiumScheduleId::None, Miss);
		TestEqual(TEXT("the miss arm's IDLE_STAND is untranslated"), MissState.Current,
			ElysiumScheduleGlobalId(ElysiumSched::IDLE_STAND));
		TestFalse(TEXT("...and never asked slot 440"), Miss.Saw(TEXT("TranslateSchedule")));
	}

	// --- The two base programs, decoded ----------------------------------------------------------
	{
		const FElysiumScheduleProgram* Fail = ElysiumScheduleFor(ElysiumScheduleGlobalId(ElysiumSched::FAIL));
		const FElysiumScheduleProgram* IdleStand = ElysiumScheduleFor(ElysiumScheduleGlobalId(ElysiumSched::IDLE_STAND));
		if (TestNotNull(TEXT("FAIL is registered"), Fail)
			&& TestNotNull(TEXT("IDLE_STAND is registered"), IdleStand))
		{
			TestEqual(TEXT("FAIL is retail 0x43"), ElysiumSched::FAIL, 0x43);
			TestEqual(TEXT("IDLE_STAND is retail 1"),
				ElysiumSched::IDLE_STAND, 1);
			TestTrue(TEXT("FAIL is interrupted by CAN_MELEE_ATTACK1"),
				Fail->Interrupts.Has(EElysiumNpcCond::CanMeleeAttack1));
			TestFalse(TEXT("FAIL is not interrupted by NEW_ENEMY"),
				Fail->Interrupts.Has(EElysiumNpcCond::NewEnemy));
			TestEqual(TEXT("IDLE_STAND waits five seconds"), IdleStand->Tasks[2].Data, 5.f);
		}
	}

	// --- TASK_WAIT_RANDOM's floor is 0.1 ---------------------------------------------------------
	{
		FRecordingRunner Runner;
		Runner.RandomFraction = 0.f;
		FElysiumScheduleState State;
		// Retail's `SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT_NE`: SET_ACTIVITY ACT_MIDCRUNCH_IDLE;
		// FACE_SAVEPOSITION; WAIT 120; WAIT_RANDOM 60. (The port's hand-typed version of this
		// program had three tasks and waited two seconds; the real one waits two MINUTES, which is
		// the kind of number a transcription does not invent.)
		ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT_NE, Runner);
		ElysiumSchedule::Tick(State, Runner, 0.0);        // SET_ACTIVITY, held by its watchdog
		ElysiumSchedule::Tick(State, Runner, 1.0);        // the watchdog expires; FACE_SAVEPOSITION
		TestEqual(TEXT("TASK_WAIT 120 is two minutes, as authored"), State.TaskEndsAt, 120.0, 1e-6);
		TestTrue(TEXT("WAIT_RANDOM at its lowest draw still starts running"),
			ElysiumSchedule::Tick(State, Runner, 120.0));
		TestEqual(TEXT("and holds the 0.1 s floor"), State.TaskEndsAt, 120.1, 1e-6);
	}

	// --- ClearSchedule from inside a task ---------------------------------------------------------
	{
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		Runner.ObservedState = &State;
		ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI, Runner);
		State.FailScheduleOverride = ElysiumSched::SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE;
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
			ElysiumScheduleId::None);
		// Fixture correction: ClearSchedule itself leaves both alone, then the same-think second
		// iteration reaches `10281be5 SetSchedule(NULL)`. Base `0x10280e50` clears the six condition
		// words and `m_failSchedule` before `0x1028226c` returns on the second null selection.
		TestEqual(TEXT("the replacement SetSchedule clears conditions once"),
			Runner.ConditionClears, ClearsBefore + 1);
		TestEqual(TEXT("the replacement SetSchedule clears m_failSchedule"),
			State.FailScheduleOverride, ElysiumScheduleId::None);
		TestEqual(TEXT("and is not a schedule completion"), Runner.CompletedSchedules, 0);
	}

	// --- A request raised outside a task step is honoured before any task work ------------------
	{
		FRecordingRunner Runner;
		FElysiumScheduleState State;
		ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI, Runner);
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
		ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI, Runner);
		TestFalse(TEXT("the install consumed the stale request"), Runner.bRequestClear);
		TestTrue(TEXT("the installed program runs"), ElysiumSchedule::Tick(State, Runner, 0.0));
		TestEqual(TEXT("...and is still installed"), State.Current, ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI));
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
		Guard->TranslateSchedule(ElysiumSched::IDLE_STAND), ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION);
	TestEqual(TEXT("0x6b translates to itself"),
		Guard->TranslateSchedule(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION), ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION);
	TestEqual(TEXT("FAIL has no Troika row"),
		Guard->TranslateSchedule(ElysiumSched::FAIL), ElysiumSched::FAIL);
	TestEqual(TEXT("a Troika id with no row is identity"),
		Guard->TranslateSchedule(ElysiumSched::SCHED_TROIKA_MELEE_IDLE), ElysiumSched::SCHED_TROIKA_MELEE_IDLE);
	// `102b1335 ADD EAX,0x132`: with `D_MILDLY_CRAZY` set the answer is the number `0x132`, and
	// this port's registry holds no program for it. Retail hands the number to slot 446
	// `SetSchedule` (`0x102cc1f0`), whose miss arm `DevMsg`s "No CASE for Schedule Type %d!" and
	// installs the literal 1 IDLE_STAND UNTRANSLATED (`102cc227 PUSH 0x1` / `102cc229 CALL
	// [EAX+0x6f8]`). The earlier IDLE_DISPOSITION answer here was port-invented.
	Guard->NpcFlags.Set(EElysiumNpcFlag2::D_MILDLY_CRAZY);
	TestEqual(TEXT("D_MILDLY_CRAZY preserves the loaded 0x132"),
		Guard->TranslateSchedule(ElysiumSched::IDLE_STAND), 0x132);
	TestEqual(TEXT("...and the raw number slot 440 answered is 0x132 (102b1335)"),
		Guard->LastTranslateScheduleRetail, 0x132);
	Guard->SetFrenziedWord(0x100);
	TestEqual(TEXT("the frenzied pre-table preserves the loaded 0xc9"),
		Guard->TranslateSchedule(ElysiumSched::SCHED_TROIKA_MELEE_IDLE), 0xc9);
	TestEqual(TEXT("...and the raw number is 0xc9 (102b120c MOV EAX,0xc9)"),
		Guard->LastTranslateScheduleRetail, 0xc9);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
