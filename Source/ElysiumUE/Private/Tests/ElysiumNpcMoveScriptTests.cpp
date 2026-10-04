#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "Visual/ElysiumNpcMoveScript.h"

// Retail's velocity script `0x102630b0` and turn script `0x102627e0` on fixture paths (spec 0002
// V4b, brief B1 item 6). Source units, units per second, seconds. No crowd follower, no body.
namespace ElysiumNpcMoveScriptTests
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask
	| EAutomationTestFlags::ProductFilter;

bool Near(float A, float B, float Tolerance = 1.e-3f)
{
	return FMath::IsNearlyEqual(A, B, Tolerance);
}

// Two turn entries `Total` seconds apart on the +X axis (the line's yaw is 0).
TArray<ElysiumNpcMoveScript::FEntry> TurnPair(float YawFrom, float YawTo, float Total)
{
	TArray<ElysiumNpcMoveScript::FEntry> Turn;
	ElysiumNpcMoveScript::FEntry From;
	From.Yaw = YawFrom;
	From.Time = Total;
	ElysiumNpcMoveScript::FEntry To;
	To.Yaw = YawTo;
	To.Elapsed = Total;
	To.Location = FVector(100.0, 0.0, 0.0);
	Turn.Add(From);
	Turn.Add(To);
	return Turn;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMoveScriptVelocityTest,
	"Elysium.Arm.NpcKernelMotor.VelocityScript", ElysiumNpcMoveScriptTests::Flags)
bool FElysiumNpcMoveScriptVelocityTest::RunTest(const FString&)
{
	using ElysiumNpcMoveScriptTests::Near;
	namespace Script = ElysiumNpcMoveScript;

	// --- A straight leg from standing: ideal 50, so accel = 100 (`0x104493c0`). ---
	{
		const TArray<FVector> Waypoints = { FVector(1000.0, 0.0, 0.0) };
		Script::FInput Input;
		Input.IdealSpeed = 50.0f;
		Input.Waypoints = Waypoints;
		TArray<Script::FEntry> Entries;
		Script::BuildVelocityScript(Input, Entries);

		// Pass 4's two inserted points: speed-up ends at ideal^2 / (2 accel) = 12.5, braking
		// starts 12.5 before the goal.
		TestEqual(TEXT("body, two cruise points, the goal"), Entries.Num(), 4);
		if (Entries.Num() == 4)
		{
			TestTrue(TEXT("it starts at the body's speed"), Near(Entries[0].MaxVelocity, 0.0f));
			TestTrue(TEXT("it cruises at the ideal"), Near(Entries[1].MaxVelocity, 50.0f)
				&& Near(Entries[2].MaxVelocity, 50.0f));
			TestTrue(TEXT("the last waypoint's speed is 0"), Near(Entries[3].MaxVelocity, 0.0f));
			TestTrue(TEXT("the speed-up covers ideal^2 / (2 (ideal + 50))"),
				Near(static_cast<float>(Entries[1].Location.X), 12.5f));
			TestTrue(TEXT("the braking covers the same distance"),
				Near(static_cast<float>(Entries[2].Location.X), 987.5f));
			TestTrue(TEXT("each ramp takes ideal / accel"), Near(Entries[0].Time, 0.5f)
				&& Near(Entries[2].Time, 0.5f));
			TestTrue(TEXT("it reaches the goal in finite time"), Near(Entries[3].Elapsed, 20.5f));
			TestEqual(TEXT("the inserted points carry no waypoint"), Entries[1].Waypoint,
				static_cast<int32>(INDEX_NONE));
			TestEqual(TEXT("the goal carries its waypoint"), Entries[3].Waypoint, 0);
		}
		// The read 0.1 s in: the body accelerates at ideal + 50.
		TestTrue(TEXT("0.1 s from standing is accel * 0.1"),
			Near(Script::SampleSpeed(Entries, 0.1f, 0.0f), 10.0f));
	}

	// --- An ideal of 0 runs at 50.0 (`0x102630b0`). ---
	{
		const TArray<FVector> Waypoints = { FVector(1000.0, 0.0, 0.0) };
		Script::FInput Input;
		Input.IdealSpeed = 0.0f;
		Input.Waypoints = Waypoints;
		TArray<Script::FEntry> Entries;
		Script::BuildVelocityScript(Input, Entries);
		TestTrue(TEXT("slot 248 answering 0 cruises at 50.0"),
			Entries.Num() == 4 && Near(Entries[1].MaxVelocity, 50.0f));
	}

	// --- A 90-degree corner slows to ideal * 0.2 (`0x10449198`). ---
	{
		const TArray<FVector> Waypoints = { FVector(1000.0, 0.0, 0.0), FVector(1000.0, 1000.0, 0.0) };
		Script::FInput Input;
		Input.IdealSpeed = 50.0f;
		Input.Waypoints = Waypoints;
		TArray<Script::FEntry> Entries;
		Script::BuildVelocityScript(Input, Entries);
		const Script::FEntry* Corner = Entries.FindByPredicate(
			[](const Script::FEntry& Entry) { return Entry.Waypoint == 0; });
		TestNotNull(TEXT("the corner keeps its entry"), Corner);
		if (Corner != nullptr)
		{
			TestTrue(TEXT("dot 0 + 0.2, times the ideal"), Near(Corner->MaxVelocity, 10.0f));
		}
	}

	// --- Pass 3 is retail's `dv > 0`: a slowing pair that does not fit its segment stays. ---
	{
		// At the ideal, straight on through a waypoint to a goal 1 unit past it: braking 50 -> 0
		// needs 12.5 units and has 1. The SDK's backward pass would lower the speed before the
		// goal to sqrt(2 * 100 * 1); retail's never fires on a slowing pair.
		const TArray<FVector> Waypoints = { FVector(100.0, 0.0, 0.0), FVector(101.0, 0.0, 0.0) };
		Script::FInput Input;
		Input.Speed = 50.0f;
		Input.IdealSpeed = 50.0f;
		Input.Waypoints = Waypoints;
		TArray<Script::FEntry> Entries;
		Script::BuildVelocityScript(Input, Entries);
		TestTrue(TEXT("at least the body and the goal"), Entries.Num() >= 2);
		if (Entries.Num() >= 2)
		{
			const Script::FEntry& BeforeGoal = Entries[Entries.Num() - 2];
			TestTrue(TEXT("the speed before the goal is left at the ideal"),
				Near(BeforeGoal.MaxVelocity, 50.0f));
			TestTrue(TEXT("so the last unit is crossed in 1 / 25 s"), Near(BeforeGoal.Time, 0.04f));
			TestTrue(TEXT("and the goal's speed is still 0"), Near(Entries.Last().MaxVelocity, 0.0f));
		}
	}

	// --- The read (`0x102646c0`): a = interval / flElapsed[i], not over the segment's own time. ---
	{
		TArray<Script::FEntry> Entries;
		Entries.SetNum(3);
		Entries[0].MaxVelocity = 0.0f;
		Entries[1].MaxVelocity = 10.0f;
		Entries[1].Elapsed = 1.0f;
		Entries[2].MaxVelocity = 30.0f;
		Entries[2].Elapsed = 3.0f;
		// i = 2, a = 2 / 3: (1/3) * 10 + (2/3) * 30. The segment's own time would give 20.
		TestTrue(TEXT("the divisor is flElapsed[i]"),
			Near(Script::SampleSpeed(Entries, 2.0f, 7.0f), 70.0f / 3.0f));
		TestTrue(TEXT("an interval past the script's end keeps the current speed"),
			Near(Script::SampleSpeed(Entries, 5.0f, 7.0f), 7.0f));
		TestTrue(TEXT("a one-entry script keeps the current speed"),
			Near(Script::SampleSpeed(TConstArrayView<Script::FEntry>(Entries.GetData(), 1), 0.1f, 7.0f), 7.0f));
	}

	// --- The trapezoid step and its clamp (`0x10264680`, `0x10264916`). ---
	{
		const Script::FStep Free = Script::Step(10.0f, 20.0f, 0.1f, 100.0f);
		TestTrue(TEXT("(|v| + speed) * interval * 0.5"), Near(Free.Distance, 1.5f));
		TestFalse(TEXT("inside the remaining distance: not cut"), Free.bClamped);
		TestTrue(TEXT("the interval is spent"), Near(Free.RemainingInterval, 0.0f));

		const Script::FStep Cut = Script::Step(10.0f, 20.0f, 0.1f, 0.5f);
		TestTrue(TEXT("cut to the remaining distance"), Cut.bClamped && Near(Cut.Distance, 0.5f));
		TestTrue(TEXT("the unused interval is interval * (1 - maxDist / dist)"),
			Near(Cut.RemainingInterval, 0.1f * (1.0f - 0.5f / 1.5f)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMoveScriptTurnTest,
	"Elysium.Arm.NpcKernelMotor.TurnScript", ElysiumNpcMoveScriptTests::Flags)
bool FElysiumNpcMoveScriptTurnTest::RunTest(const FString&)
{
	using ElysiumNpcMoveScriptTests::Near;
	using ElysiumNpcMoveScriptTests::TurnPair;
	namespace Script = ElysiumNpcMoveScript;

	// `0x1013d450` = UTIL_ApproachAngle(target, value, speed): from `value` toward `target`.
	TestTrue(TEXT("0.8 of a 90-degree corner, from the inbound yaw"),
		Near(Script::ApproachAngle(90.0f, 0.0f, 72.0f), 72.0f));
	TestTrue(TEXT("a delta inside the speed lands on the target"),
		Near(Script::ApproachAngle(90.0f, 80.0f, 72.0f), 90.0f));
	TestTrue(TEXT("the short way round"), Near(Script::ApproachAngle(350.0f, 10.0f, 5.0f), 5.0f));

	// `0x10262c20`, the insert rule. 30 degrees at 150 per second is 0.2 s.
	{
		// Both turns fit: two entries, both from entry i (`0x10262ea0(i, tIn)`, `(i, tOut)`).
		TArray<Script::FEntry> Turn = TurnPair(30.0f, 30.0f, 1.0f);
		TestEqual(TEXT("two entries"), Script::InsertTurnPair(Turn, 0, 1), 2);
		TestEqual(TEXT("four in the script"), Turn.Num(), 4);
		if (Turn.Num() == 4)
		{
			TestTrue(TEXT("both carry the line's yaw"), Near(Turn[1].Yaw, 0.0f) && Near(Turn[2].Yaw, 0.0f));
			TestTrue(TEXT("the ends keep theirs"), Near(Turn[0].Yaw, 30.0f) && Near(Turn[3].Yaw, 30.0f));
		}
	}
	{
		// Only the turn in: one entry at tIn. The durations are swapped against the SDK: the old
		// entry keeps time - t, the new one gets t.
		TArray<Script::FEntry> Turn = TurnPair(30.0f, 0.0f, 1.0f);
		TestEqual(TEXT("one entry"), Script::InsertTurnPair(Turn, 0, 1), 1);
		TestTrue(TEXT("at tIn"), Turn.Num() == 3 && Near(Turn[1].Elapsed, 0.2f)
			&& Near(Turn[1].Time, 0.2f) && Near(Turn[0].Time, 0.8f) && Near(Turn[1].Yaw, 0.0f));
	}
	{
		// Only the turn out: one entry at T - tOut.
		TArray<Script::FEntry> Turn = TurnPair(0.0f, 30.0f, 1.0f);
		TestEqual(TEXT("one entry"), Script::InsertTurnPair(Turn, 0, 1), 1);
		TestTrue(TEXT("at T - tOut"), Turn.Num() == 3 && Near(Turn[1].Elapsed, 0.8f)
			&& Near(Turn[1].Time, 0.8f) && Near(Turn[0].Time, 0.2f) && Near(Turn[1].Yaw, 0.0f));
	}
	{
		// Both turns, 0.4 s together, in a 0.45 s segment: inside it but not under 0.8 of it.
		TArray<Script::FEntry> Turn = TurnPair(30.0f, 30.0f, 0.45f);
		TestEqual(TEXT("none"), Script::InsertTurnPair(Turn, 0, 1), 0);
		TestEqual(TEXT("the script is untouched"), Turn.Num(), 2);
	}

	// `0x102627e0` on a straight leg, facing along it. Nothing turns, and `0x10262c20` still
	// inserts one entry (tIn < 0.01 and tOut = 0 <= 0.8 T: "one at T - tOut", here at the goal's
	// own time) -- as read.
	{
		const TArray<FVector> Waypoints = { FVector(1000.0, 0.0, 0.0) };
		Script::FInput Input;
		Input.IdealSpeed = 50.0f;
		Input.Waypoints = Waypoints;
		TArray<Script::FEntry> Velocity;
		TArray<Script::FEntry> Turn;
		Script::BuildVelocityScript(Input, Velocity);
		Script::BuildTurnScript(Input, Velocity, Turn);
		TestEqual(TEXT("entry 0, the inserted entry, the goal"), Turn.Num(), 3);
		TestTrue(TEXT("the path's own direction"), Near(Script::SampleYaw(Turn, 0.1f, 0.0f), 0.0f));
		TestTrue(TEXT("the goal's time is the velocity script's"),
			Turn.Num() == 3 && Near(Turn[2].Elapsed, 20.5f) && Near(Turn[1].Elapsed, 20.5f)
				&& Near(Turn[0].Time, 0.0f));
	}
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS
