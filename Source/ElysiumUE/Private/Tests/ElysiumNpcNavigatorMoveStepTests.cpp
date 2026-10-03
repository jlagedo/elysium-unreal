#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcNavigator.h"
#include "Substrate/ElysiumScriptedCharacter.h"
#include "Tests/ElysiumNpcTestFixture.h"

// 0018 story 5 lane I -- `CAI_Navigator::Move 0x102eff40` as `FElysiumNpcBase::NavigatorMoveStep`: the
// entry gates in retail's order, the arrival arms (navigator slot 16, `OnNavComplete`,
// `AdvancePath 0x102f0400`), the NPC-blocker hold `0x102ef3e0` (0.25 s hold, 3.0 s window) with the
// port's head-leg re-issue once it has run (at any think cadence), the goal-tolerance completion `0x102ef760`, and the
// failure tail `0x102f0169` (stale mark unless `-3`, then `OnNavFailed(0x0c)`), each decided from the
// body's scripted move facts (R3).

static constexpr EAutomationTestFlags GMoveStepTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	constexpr float GMoveStepU = ElysiumMove::U;
	// The goal: a location 300 units ahead, with a 10-unit goal tolerance (`path+0x28`).
	const FVector GMoveStepGoalCm(300.0 * ElysiumMove::U, 0.0, 0.0);
	constexpr float GMoveStepToleranceUnits = 10.f;
	// The head leg's recorded pedestrian multiplier: a re-issue must hand this back, never a new draw.
	constexpr int32 GMoveStepLegPedestrian = 7;
	constexpr float GMoveStepLegSpeedCmPerSecond = 150.f;

	// A motor that reports only what a case scripts: the move facts and the legacy status. It moves
	// nothing; `Feet` is where `Sample` says the body stands (the entity record's origin).
	struct FMoveStepMotor final : IElysiumNpcMotor
	{
		FElysiumNpcMoveFacts Facts;
		bool bReportsFacts = true;
		EElysiumNpcMoveStatus Status = EElysiumNpcMoveStatus::Moving;
		FVector Feet = FVector::ZeroVector;
		int32 StopCalls = 0;
		// Every `MoveTo` the substrate made, and whether this body accepts them.
		bool bAcceptMoves = true;
		TArray<FElysiumNpcMoveRequest> MoveRequests;
		virtual bool MoveTo(const FElysiumNpcMoveRequest& Request) override
		{
			MoveRequests.Add(Request);
			return bAcceptMoves;
		}
		virtual void Face(float, float) override {}
		virtual void Stop() override { ++StopCalls; }
		virtual void Teleport(const FVector&, float) override {}
		virtual void SetEnabled(bool) override {}
		virtual void SetFrozen(bool) override {}
		virtual void SetIgnoreCharacterCollision(bool) override {}
		virtual EElysiumNpcMoveStatus Sample(FVector& OutFeetOrigin, float& OutYawDegrees) override
		{
			OutFeetOrigin = Feet;
			OutYawDegrees = 0.f;
			return Status;
		}
		virtual void SampleTransform(FVector& OutFeetOrigin, float& OutYawDegrees) const override
		{
			OutFeetOrigin = Feet;
			OutYawDegrees = 0.f;
		}
		virtual FElysiumLocomotionSample SampleLocomotion() const override { return FElysiumLocomotionSample(); }
		virtual bool SampleMoveFacts(FElysiumNpcMoveFacts& Out) const override
		{
			if (!bReportsFacts)
			{
				return false;
			}
			Out = Facts;
			return true;
		}

		// A request in flight, `Units` from its destination.
		void Walking(float RemainingUnits)
		{
			Facts = FElysiumNpcMoveFacts();
			Facts.bRequestAlive = true;
			Facts.RemainingDistance2DCm = RemainingUnits * GMoveStepU;
			Status = EElysiumNpcMoveStatus::Moving;
		}
		// The follower ended the request with `Code`, naming `Blocker` (unset: nothing named).
		void Ended(EElysiumNpcMoveResultCode Code, const FElysiumEntityHandle& Blocker, float RemainingUnits)
		{
			Facts = FElysiumNpcMoveFacts();
			Facts.bRequestEnded = true;
			Facts.ResultCode = Code;
			Facts.BlockingEntity = Blocker;
			Facts.RemainingDistance2DCm = RemainingUnits * GMoveStepU;
			Status = Code == EElysiumNpcMoveResultCode::Success
				? EElysiumNpcMoveStatus::Reached : EElysiumNpcMoveStatus::Failed;
		}
	};

	// `Motor` is `FElysiumScriptedCharacter`'s protected word; the fixture's NPC is handed the scripted
	// motor through a pointer to that member.
	struct FMoveStepMotorSlot : FElysiumScriptedCharacter
	{
		static IElysiumNpcMotor* FElysiumScriptedCharacter::* Member() { return &FMoveStepMotorSlot::Motor; }
	};

	// Who was handed `InPass` (`CPathCorner::InputInPass 0x10147d50`), and by which activator.
	TArray<FElysiumEntityHandle> GMoveStepInPassActivators;

	// A suite-local path corner: only its `InPass` input is observed.
	class FMoveStepTestCorner final : public FElysiumEntity
	{
	};

	TUniquePtr<FElysiumEntity> MakeMoveStepTestCorner()
	{
		return MakeUnique<FMoveStepTestCorner>();
	}

	FElysiumClassRegistrar GRegMoveStepTestCorner(TEXT("elysium_test_movestep_corner"), ElysiumBaseClassName(),
		&MakeMoveStepTestCorner,
		[](FElysiumClassDesc& Desc)
		{
			Desc.Input(FName(TEXT("InPass")), [](FElysiumEntity& Self, const FElysiumInputArgs& Args)
			{
				(void)Self;
				GMoveStepInPassActivators.Add(Args.Activator);
			});
		});

	FElysiumNpcWorldBuilder MoveStepWorld(const TCHAR* Map, uint32 Seed)
	{
		FElysiumNpcWorldBuilder Builder(Map, Seed);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		Builder.AddNpc(TEXT("guard"));
		Builder.AddNpc(TEXT("blocker"), FVector(1000.0, 0.0, 0.0));
		Builder.AddEntity(TEXT("elysium_test_movestep_corner"), TEXT("corner"), FVector(300.0 * ElysiumMove::U, 0.0, 0.0));
		return Builder;
	}

	// A guard walking a type-4 goal whose head waypoint is the goal, `m_bShouldMove` set, and a
	// blocker NPC beside it. The scripted motor outlives the world (declared first).
	struct FMoveStepRig
	{
		FMoveStepMotor Scripted;
		FElysiumNpcWorldFixture F;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Blocker = nullptr;

		FMoveStepRig(const TCHAR* Map, uint32 Seed)
			: F(MoveStepWorld(Map, Seed))
		{
			Guard = F.Npc(TEXT("guard"));
			Blocker = F.Npc(TEXT("blocker"));
			FElysiumNpcWorldFixture::Quiet({ Guard, Blocker });
			FElysiumNpcWorldFixture::PrepareForKernelDrive(Guard);
			if (Guard != nullptr)
			{
				Guard->*FMoveStepMotorSlot::Member() = &Scripted;
				Arm();
			}
		}
		~FMoveStepRig()
		{
			if (Guard != nullptr)
			{
				Guard->*FMoveStepMotorSlot::Member() = nullptr;
			}
		}
		bool Ready() const { return Guard != nullptr && Blocker != nullptr; }

		// The state every case starts a step from.
		void Arm()
		{
			Guard->BaseScheduleHost.bShouldMove = true;
			Guard->BaseScheduleHost.FailureReason = 0;
			Guard->BaseScheduleHost.MoveWaitFinished = 0.0;
			Guard->Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
			FElysiumNpcNavigator& Nav = Guard->Navigator;
			Nav.GoalType = 4;
			Nav.bHasHeadWaypoint = true;
			Nav.bHeadIsGoal = true;
			Nav.GoalPosCm = GMoveStepGoalCm;
			Nav.GoalToleranceCm = GMoveStepToleranceUnits * GMoveStepU;
			Nav.NavType = 0;
			Nav.bPaused = false;
			Nav.TargetEntity = FElysiumEntityHandle::Invalid();
			Nav.LastOutcome = FElysiumNpcNavOutcome();
			Nav.bNavFailed = false;
			Scripted.bReportsFacts = true;
			Scripted.Feet = FVector::ZeroVector;
			Guard->Origin = FVector::ZeroVector;
			// The head leg, issued (and recorded) the way the navigator issues every leg.
			Scripted.bAcceptMoves = true;
			FElysiumNpcMoveRequest Leg;
			Leg.DestinationCm = GMoveStepGoalCm;
			Leg.AcceptanceToleranceCm = 0.0625f * GMoveStepU;
			Leg.SpeedCmPerSecond = GMoveStepLegSpeedCmPerSecond;
			Leg.GaitKind = EElysiumNpcGaitKind::Walk;
			Leg.PedestrianCostMultiplier = GMoveStepLegPedestrian;
			Guard->NavIssueLeg(Leg);
			Scripted.Walking(300.f);
		}
		double Now() const { return F.World.NowSeconds(); }
	};
}

// --- The entry gates ---------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepPausedTest,
	"Elysium.Arm.Navigator.MoveStep.Gates.Paused", GMoveStepTestFlags)
bool FElysiumMoveStepPausedTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_paused"), 18601);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	// A step that would fail if it ran: the body gave the route up, nothing named, far from the goal.
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, FElysiumEntityHandle(), 300.f);
	R.Guard->Navigator.bPaused = true;
	R.Guard->Navigator.bNavFailed = true;
	const int32 Stale = R.Guard->NavMoveStep.StaleMarkCalls;
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("0x102effab paused: no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
	TestTrue(TEXT("...nav+0x1c untouched"), R.Guard->Navigator.bNavFailed);
	TestEqual(TEXT("...no stale mark"), R.Guard->NavMoveStep.StaleMarkCalls, Stale);
	TestTrue(TEXT("...no outcome"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepShouldMoveTest,
	"Elysium.Arm.Navigator.MoveStep.Gates.ShouldMoveClear", GMoveStepTestFlags)
bool FElysiumMoveStepShouldMoveTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_shouldmove"), 18602);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, FElysiumEntityHandle(), 300.f);
	R.Guard->BaseScheduleHost.bShouldMove = false;

	// Ground nav: motor slot 10 (the velocity stop, a seam), no failure.
	const int32 Stops = R.Guard->NavMoveStep.VelocityStops;
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("0x102f0198 m_bShouldMove == 0: no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
	TestEqual(TEXT("...motor slot 10 reached"), R.Guard->NavMoveStep.VelocityStops, Stops + 1);
	TestTrue(TEXT("...the route stands"), R.Guard->Navigator.IsGoalActive());

	// Climb: the navigator's motor slot 5 and `SetNavType(0)`.
	R.Guard->Navigator.NavType = 3;
	const int32 Climbs = R.Guard->NavMoveStep.ClimbMotorResets;
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("climb: motor slot 5 reached"), R.Guard->NavMoveStep.ClimbMotorResets, Climbs + 1);
	TestEqual(TEXT("...and the nav type is ground again"), R.Guard->Navigator.GetNavType(), 0);
	TestEqual(TEXT("...no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepNoGoalTest,
	"Elysium.Arm.Navigator.MoveStep.Gates.NoGoalType", GMoveStepTestFlags)
bool FElysiumMoveStepNoGoalTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_nogoal"), 18603);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	// Slot 525: the base `0x1027da90` declines, so the step goes on to the goal test.
	TestFalse(TEXT("0x1027da90 the base OverrideMove declines"), R.Guard->OverrideMove(0.1f));
	R.Guard->Navigator.GoalType = 0;
	const int32 Warnings = R.Guard->NavMoveStep.NoRouteWarnings;
	const int32 Stale = R.Guard->NavMoveStep.StaleMarkCalls;
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("0x102f0081 the no-route Warning"), R.Guard->NavMoveStep.NoRouteWarnings, Warnings + 1);
	TestEqual(TEXT("...OnNavFailed(0x0d)"), R.Guard->BaseScheduleHost.FailureReason, 0x0d);
	TestTrue(TEXT("...recorded as a failure"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::Failed);
	TestEqual(TEXT("...with its code"), R.Guard->Navigator.LastOutcome.FailCode, 0x0d);
	TestEqual(TEXT("...no stale mark"), R.Guard->NavMoveStep.StaleMarkCalls, Stale);
	TestTrue(TEXT("...nav+0x1c set"), R.Guard->Navigator.bNavFailed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepNoHeadTest,
	"Elysium.Arm.Navigator.MoveStep.Gates.NoHeadWaypoint", GMoveStepTestFlags)
bool FElysiumMoveStepNoHeadTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_nohead"), 18604);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	R.Guard->Navigator.bHasHeadWaypoint = false;
	const int32 Warnings = R.Guard->NavMoveStep.NoRouteWarnings;
	const int32 Stale = R.Guard->NavMoveStep.StaleMarkCalls;
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("0x102f00bc no head waypoint: OnNavFailed(0x0c)"), R.Guard->BaseScheduleHost.FailureReason, 0x0c);
	TestEqual(TEXT("...no warning"), R.Guard->NavMoveStep.NoRouteWarnings, Warnings);
	TestEqual(TEXT("...no stale mark"), R.Guard->NavMoveStep.StaleMarkCalls, Stale);
	TestEqual(TEXT("...the goal type stands"), R.Guard->Navigator.GetGoalType(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepMoveWaitTest,
	"Elysium.Arm.Navigator.MoveStep.Gates.MoveWait", GMoveStepTestFlags)
bool FElysiumMoveStepMoveWaitTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_movewait"), 18605);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, FElysiumEntityHandle(), 300.f);
	R.Guard->BaseScheduleHost.MoveWaitFinished = R.Now() + 5.0;
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("0x102f00cd curtime < m_flMoveWaitFinished: a silent wait"), R.Guard->BaseScheduleHost.FailureReason, 0);
	TestTrue(TEXT("...no outcome"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::None);
	TestEqual(TEXT("...no pass dispatched"), R.Guard->NavMoveStep.Passes, 0);
	return true;
}

// --- Arrival -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepArrivedGoalTest,
	"Elysium.Arm.Navigator.MoveStep.Arrived.GoalWaypoint", GMoveStepTestFlags)
bool FElysiumMoveStepArrivedGoalTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_arrived"), 18606);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;

	// Walking, 0.07 units out: outside the 0.0625 radius (`0x10451f78`), nothing ends.
	R.Scripted.Walking(0.07f);
	R.Guard->NavigatorMoveStep();
	TestTrue(TEXT("0.07 units is not reached"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::None);
	TestTrue(TEXT("...the route stands"), R.Guard->Navigator.IsGoalActive());
	TestTrue(TEXT("...m_bShouldMove stands"), R.Guard->BaseScheduleHost.bShouldMove);

	// 0.05 units out, the request still alive: reached, on the goal waypoint -> OnNavComplete.
	R.Guard->Navigator.BlockerEntity = R.Blocker->Handle;
	R.Guard->Navigator.BlockerHoldUntil = R.Now() + 1.0;
	R.Guard->Navigator.BlockerForgetAt = R.Now() + 2.0;
	R.Scripted.Walking(0.05f);
	R.Guard->NavigatorMoveStep();
	TestTrue(TEXT("OnNavComplete"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::Arrived);
	TestTrue(TEXT("...nav+0x1c = 1"), R.Guard->Navigator.bNavFailed);
	TestFalse(TEXT("...TaskMovementComplete cleared m_bShouldMove (0x10273ec9)"), R.Guard->BaseScheduleHost.bShouldMove);
	TestEqual(TEXT("...the goal type does not outlive the arrival"), R.Guard->Navigator.GetGoalType(), 0);
	TestFalse(TEXT("...nor the head waypoint"), R.Guard->Navigator.IsGoalActive());
	TestFalse(TEXT("0x102eeb70 nav+0x54 := -1"), R.Guard->Navigator.BlockerEntity.IsSet());
	TestEqual(TEXT("0x102eeb70 nav+0x58 := -1.0"), R.Guard->Navigator.BlockerHoldUntil, -1.0);
	TestEqual(TEXT("0x102eeb70 nav+0x60 := -1.0"), R.Guard->Navigator.BlockerForgetAt, -1.0);
	TestEqual(TEXT("...no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);

	// The follower's own Success end (its arrival floor): reached, whatever distance it reports.
	R.Arm();
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Success, FElysiumEntityHandle(), 0.5f);
	R.Guard->NavigatorMoveStep();
	TestTrue(TEXT("a Success end is an arrival"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::Arrived);
	TestEqual(TEXT("...no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepArrivedNonGoalTest,
	"Elysium.Arm.Navigator.MoveStep.Arrived.NonGoalHead", GMoveStepTestFlags)
bool FElysiumMoveStepArrivedNonGoalTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_nongoal"), 18607);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	// The goal-less install's single waypoint (`0x102ed430`): the head is not the goal. Arrival there
	// runs `AdvancePath 0x102f0400`, whose pop flags the last waypoint as the goal when none follows
	// ("Force end of route without goal"), and the route completes -- never a failure.
	R.Guard->Navigator.bHeadIsGoal = false;
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Success, FElysiumEntityHandle(), 0.f);
	R.Guard->NavigatorMoveStep();
	TestTrue(TEXT("0x102f0400 then the forced end completes the route"),
		R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::Arrived);
	TestEqual(TEXT("...no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
	TestTrue(TEXT("...inside the 16-pass cap"), R.Guard->NavMoveStep.Passes >= 1 && R.Guard->NavMoveStep.Passes <= 16);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepLastCornerTest,
	"Elysium.Arm.Navigator.MoveStep.Arrived.LastCornerInPass", GMoveStepTestFlags)
bool FElysiumMoveStepLastCornerTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_lastcorner"), 18615);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	FElysiumEntity* Corner = R.F.World.FindByName(TEXT("corner"));
	if (!TestNotNull(TEXT("corner"), Corner)) return false;
	GMoveStepInPassActivators.Reset();
	// A type-3 chain walking its last corner (`m_pGoalEnt`), whose waypoint is the goal.
	R.Guard->Navigator.GoalType = 3;
	R.Guard->Navigator.bHeadIsGoal = true;
	R.Guard->NavHeadCorner = FElysiumEntityHandle::Invalid();
	R.Guard->BaseScheduleHost.GoalEnt = Corner->Handle;
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Success, FElysiumEntityHandle(), 0.f);
	R.Guard->NavigatorMoveStep();
	// `OnNavComplete` -> `TaskMovementComplete 0x10273ec0`: `IsGoalActive` -> `AdvancePath` (the goal
	// corner's `InPass`), then `ClearGoal`.
	TestTrue(TEXT("OnNavComplete: the arrival completes"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::Arrived);
	TestEqual(TEXT("0x10273f3a AdvancePath handed the last corner InPass once"), GMoveStepInPassActivators.Num(), 1);
	if (GMoveStepInPassActivators.Num() == 1)
	{
		TestTrue(TEXT("...with the NPC as activator"), GMoveStepInPassActivators[0] == R.Guard->Handle);
	}
	TestEqual(TEXT("0x10273f46 ClearGoal: the goal type is 0"), R.Guard->Navigator.GetGoalType(), 0);
	TestFalse(TEXT("...and no head waypoint stands"), R.Guard->Navigator.IsGoalActive());
	TestEqual(TEXT("...no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
	return true;
}

// --- Blocked -----------------------------------------------------------------------------------------

namespace
{
	// The body's side of the hold, as it can produce it: the follower ends the leg `Blocked` naming the
	// NPC; the hold arms (0.25 s) and holds on the ended request; once it has run inside the 3.0 s
	// window the substrate re-issues the recorded leg. Answers the time the hold was armed.
	double MoveStepHoldThenReissue(FAutomationTestBase& Test, FMoveStepRig& R)
	{
		const FElysiumEntityHandle Blocker = R.Blocker->Handle;
		const int32 Arms = R.Guard->NavMoveStep.BlockerHoldArms;
		const int32 Reissues = R.Guard->NavMoveStep.BlockerHoldReissues;
		const int32 Moves = R.Scripted.MoveRequests.Num();

		// First contact: the follower gave up against an NPC. `0x102ef3e0` arms: no fail.
		const double T0 = R.Now();
		R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, Blocker, 300.f);
		R.Guard->NavigatorMoveStep();
		Test.TestEqual(TEXT("0x102ef49a the hold arms"), R.Guard->NavMoveStep.BlockerHoldArms, Arms + 1);
		Test.TestEqual(TEXT("...nothing fails"), R.Guard->BaseScheduleHost.FailureReason, 0);
		Test.TestTrue(TEXT("...nav+0x51 = 1"), R.Guard->Navigator.bBlockerHold);
		Test.TestTrue(TEXT("...nav+0x54 = the blocker"), R.Guard->Navigator.BlockerEntity == Blocker);
		Test.TestEqual(TEXT("...nav+0x58 = curtime + 0.25"), R.Guard->Navigator.BlockerHoldUntil, T0 + 0.25, 1e-4);
		Test.TestEqual(TEXT("...nav+0x60 = curtime + 3.0"), R.Guard->Navigator.BlockerForgetAt, T0 + 3.0, 1e-4);
		Test.TestEqual(TEXT("...and nothing is re-issued while the hold stands"), R.Scripted.MoveRequests.Num(), Moves);

		// Inside the 0.25 s, the request still ended: held.
		R.F.Advance(T0 + 0.1);
		R.Guard->NavigatorMoveStep();
		Test.TestEqual(TEXT("inside 0.25 s the hold stands: no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
		Test.TestEqual(TEXT("...it did not re-arm"), R.Guard->NavMoveStep.BlockerHoldArms, Arms + 1);
		Test.TestEqual(TEXT("...and re-issued nothing"), R.Scripted.MoveRequests.Num(), Moves);

		// Past it, inside the window: the recorded leg is handed back, unchanged -- no new draw.
		R.F.Advance(T0 + 0.3);
		R.Guard->NavigatorMoveStep();
		Test.TestEqual(TEXT("the hold has run: nothing fails yet"), R.Guard->BaseScheduleHost.FailureReason, 0);
		Test.TestEqual(TEXT("...the head leg is re-issued once"), R.Scripted.MoveRequests.Num(), Moves + 1);
		Test.TestEqual(TEXT("...counted"), R.Guard->NavMoveStep.BlockerHoldReissues, Reissues + 1);
		Test.TestTrue(TEXT("...the armed hold has spent its re-issue"), R.Guard->Navigator.bBlockerHoldReissued);
		Test.TestEqual(TEXT("...and the hold did not re-arm"), R.Guard->NavMoveStep.BlockerHoldArms, Arms + 1);
		if (R.Scripted.MoveRequests.Num() == Moves + 1)
		{
			const FElysiumNpcMoveRequest& Again = R.Scripted.MoveRequests.Last();
			Test.TestEqual(TEXT("...to the same destination"), Again.DestinationCm, GMoveStepGoalCm);
			Test.TestEqual(TEXT("...at the same arrival radius"), Again.AcceptanceToleranceCm, 0.0625f * GMoveStepU);
			Test.TestEqual(TEXT("...at the same speed"), Again.SpeedCmPerSecond, GMoveStepLegSpeedCmPerSecond);
			Test.TestTrue(TEXT("...in the same gait"), Again.GaitKind == TOptional<EElysiumNpcGaitKind>(EElysiumNpcGaitKind::Walk));
			Test.TestEqual(TEXT("...with the multiplier drawn at the build (0x102fe9f0 draws once)"),
				Again.PedestrianCostMultiplier, GMoveStepLegPedestrian);
		}
		Test.TestTrue(TEXT("...no outcome"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::None);
		return T0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepNpcHoldResumesTest,
	"Elysium.Arm.Navigator.MoveStep.Blocked.NpcHoldResumes", GMoveStepTestFlags)
bool FElysiumMoveStepNpcHoldResumesTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_hold_resume"), 18608);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	const double T0 = MoveStepHoldThenReissue(*this, R);

	// The blocker walked away: the re-issued leg walks on, as retail's re-probe does.
	R.F.Advance(T0 + 0.4);
	R.Scripted.Walking(250.f);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("the re-issued leg walks: no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
	TestTrue(TEXT("...the route stands"), R.Guard->NavIsGoalActive());

	// ...and arrives.
	R.F.Advance(T0 + 0.5);
	R.Scripted.Walking(0.05f);
	R.Guard->NavigatorMoveStep();
	TestTrue(TEXT("OnNavComplete: the walk completes"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::Arrived);
	TestEqual(TEXT("...never a failure"), R.Guard->BaseScheduleHost.FailureReason, 0);
	TestFalse(TEXT("0x102eeb70 the re-issue mark is reset with the blocker memory"), R.Guard->Navigator.bBlockerHoldReissued);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepNpcHoldExhaustedTest,
	"Elysium.Arm.Navigator.MoveStep.Blocked.NpcHoldExhaustedFails", GMoveStepTestFlags)
bool FElysiumMoveStepNpcHoldExhaustedTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_hold"), 18616);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	const FElysiumEntityHandle Blocker = R.Blocker->Handle;
	const int32 Stale = R.Guard->NavMoveStep.StaleMarkCalls;
	const double T0 = MoveStepHoldThenReissue(*this, R);
	const int32 Arms = R.Guard->NavMoveStep.BlockerHoldArms;
	const int32 Moves = R.Scripted.MoveRequests.Num();
	const int32 MoverTests = R.Guard->NavMoveStep.MoverFollowTests;

	// The re-issued leg walks a little, then the same NPC blocks it again inside the 3.0 s window: the
	// exhausted hold. S4 declines (seam), the `-3` stands, `0x102ef760` finds the goal far -> `0x0c`,
	// no stale mark.
	R.F.Advance(T0 + 0.4);
	R.Scripted.Walking(280.f);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("the re-issued leg walks"), R.Guard->BaseScheduleHost.FailureReason, 0);
	R.F.Advance(T0 + 1.0);
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, Blocker, 280.f);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("a second Blocked inside the window fails 0x0c"), R.Guard->BaseScheduleHost.FailureReason, 0x0c);
	TestEqual(TEXT("...with no second hold"), R.Guard->NavMoveStep.BlockerHoldArms, Arms);
	TestEqual(TEXT("...and no second re-issue"), R.Scripted.MoveRequests.Num(), Moves);
	TestEqual(TEXT("...after S4 0x102efde0 was asked"), R.Guard->NavMoveStep.MoverFollowTests, MoverTests + 1);
	TestEqual(TEXT("...with no stale mark (CMP EAX,-3)"), R.Guard->NavMoveStep.StaleMarkCalls, Stale);
	TestTrue(TEXT("...recorded as NPC-blocked"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::NpcBlocked);
	TestTrue(TEXT("...naming the blocker"), R.Guard->Navigator.LastOutcome.Blocker == Blocker);
	TestFalse(TEXT("OnNavFailed's 0x102eeb70 forgot the blocker"), R.Guard->Navigator.BlockerEntity.IsSet());
	TestTrue(TEXT("OnNavFailed does not clear the path"), R.Guard->Navigator.IsGoalActive());
	TestTrue(TEXT("...and the kernel's 0x102ee6a0 reader sees it standing"), R.Guard->NavIsGoalActive());
	TestTrue(TEXT("slot 153 0x10280300 -> 0x102ee680: the goal type stands, still 'moving'"), R.Guard->IsMoving());

	// A body that refuses the re-issue: the hold's run ends in the `-3` at once.
	R.Arm();
	const double T1 = R.Now();
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, Blocker, 300.f);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("refused re-issue: the hold arms"), R.Guard->BaseScheduleHost.FailureReason, 0);
	R.Scripted.bAcceptMoves = false;
	R.F.Advance(T1 + 0.3);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("...a refused re-issue is the exhausted hold: 0x0c"), R.Guard->BaseScheduleHost.FailureReason, 0x0c);
	TestTrue(TEXT("...as NPC-blocked"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::NpcBlocked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepWindowTest,
	"Elysium.Arm.Navigator.MoveStep.Blocked.SecondContactWindow", GMoveStepTestFlags)
bool FElysiumMoveStepWindowTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_window"), 18609);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	const FElysiumEntityHandle Blocker = R.Blocker->Handle;

	// Contact, the hold, the re-issue, then the walk resumes with no fail or completion between.
	const double T0 = MoveStepHoldThenReissue(*this, R);
	R.F.Advance(T0 + 0.5);
	R.Scripted.Walking(250.f);
	R.Guard->NavigatorMoveStep();
	TestTrue(TEXT("the memory survives a walking pass"), R.Guard->Navigator.BlockerEntity == Blocker);

	// A second contact with the same NPC inside the 3.0 s window: no hold, the NPC block fails at once.
	R.F.Advance(T0 + 1.0);
	const int32 Arms = R.Guard->NavMoveStep.BlockerHoldArms;
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, Blocker, 250.f);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("inside 3.0 s a second contact gets no hold"), R.Guard->NavMoveStep.BlockerHoldArms, Arms);
	TestEqual(TEXT("...and fails 0x0c"), R.Guard->BaseScheduleHost.FailureReason, 0x0c);

	// Past the window the same NPC arms a fresh hold, with a fresh re-issue.
	R.Arm();
	const double T1 = MoveStepHoldThenReissue(*this, R);
	R.F.Advance(T1 + 0.5);
	R.Scripted.Walking(250.f);
	R.Guard->NavigatorMoveStep();
	R.F.Advance(T1 + 3.5);
	const int32 ArmsLater = R.Guard->NavMoveStep.BlockerHoldArms;
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, Blocker, 250.f);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("curtime - nav+0x60 > -0.001: the window ran out, the hold re-arms"),
		R.Guard->NavMoveStep.BlockerHoldArms, ArmsLater + 1);
	TestEqual(TEXT("...and nothing fails"), R.Guard->BaseScheduleHost.FailureReason, 0);
	TestFalse(TEXT("...and the fresh hold may re-issue again"), R.Guard->Navigator.bBlockerHoldReissued);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepHoldRetailCadenceTest,
	"Elysium.Arm.Navigator.MoveStep.Blocked.NpcHoldRetailCadence", GMoveStepTestFlags)
bool FElysiumMoveStepHoldRetailCadenceTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_hold_fast"), 18617);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	const FElysiumEntityHandle Blocker = R.Blocker->Handle;
	const int32 Stale = R.Guard->NavMoveStep.StaleMarkCalls;
	const int32 Moves = R.Scripted.MoveRequests.Num();

	// Thinks 0.1 s apart (`0x10292de0` in view). Contact at T0: `0x102ef49a` arms.
	const double T0 = R.Now();
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, Blocker, 300.f);
	R.Guard->NavigatorMoveStep();
	const int32 Arms = R.Guard->NavMoveStep.BlockerHoldArms;
	TestEqual(TEXT("T0: the hold arms, nothing fails"), R.Guard->BaseScheduleHost.FailureReason, 0);
	TestEqual(TEXT("...nav+0x58 = curtime + 0.25"), R.Guard->Navigator.BlockerHoldUntil, T0 + 0.25, 1e-4);
	TestEqual(TEXT("...nav+0x60 = curtime + 3.0"), R.Guard->Navigator.BlockerForgetAt, T0 + 3.0, 1e-4);

	// +0.1 and +0.2: `curtime - nav+0x58 <= -0.001`, held; the same end is one contact, never re-armed.
	for (const double At : { 0.1, 0.2 })
	{
		R.F.Advance(T0 + At);
		R.Guard->NavigatorMoveStep();
		TestEqual(TEXT("inside 0.25 s: held, no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
		TestEqual(TEXT("...no re-arm"), R.Guard->NavMoveStep.BlockerHoldArms, Arms);
		TestEqual(TEXT("...no re-issue"), R.Scripted.MoveRequests.Num(), Moves);
	}

	// +0.3, the first think past the hold: retail's re-probe, the head leg handed back. The window
	// stands, so its words are untouched.
	R.F.Advance(T0 + 0.3);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("+0.3: the leg is re-issued"), R.Scripted.MoveRequests.Num(), Moves + 1);
	TestEqual(TEXT("...no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
	TestEqual(TEXT("...the probe dated at this curtime"), R.Guard->Navigator.BlockerProbeAt, T0 + 0.3, 1e-4);
	TestEqual(TEXT("...nav+0x58 untouched"), R.Guard->Navigator.BlockerHoldUntil, T0 + 0.25, 1e-4);
	TestEqual(TEXT("...nav+0x60 untouched"), R.Guard->Navigator.BlockerForgetAt, T0 + 3.0, 1e-4);

	// The follower pushes against the same NPC (no ground closed) and gives the leg up ~5 s later, past
	// the window's clock: the verdict is the +0.3 probe's, inside the window -> `-3` -> `0x0c`.
	double At = 0.4;
	for (; At < 5.3; At += 0.1)
	{
		R.F.Advance(T0 + At);
		R.Scripted.Walking(300.f);
		R.Guard->NavigatorMoveStep();
	}
	TestEqual(TEXT("the re-issued leg walks: no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
	R.F.Advance(T0 + At);
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, Blocker, 299.5f);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("the probe still blocked by the same NPC: 0x0c"), R.Guard->BaseScheduleHost.FailureReason, 0x0c);
	TestTrue(TEXT("...as NPC-blocked (-3)"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::NpcBlocked);
	TestEqual(TEXT("...with no second hold"), R.Guard->NavMoveStep.BlockerHoldArms, Arms);
	TestEqual(TEXT("...no second re-issue"), R.Scripted.MoveRequests.Num(), Moves + 1);
	TestEqual(TEXT("...no stale mark (CMP EAX,-3)"), R.Guard->NavMoveStep.StaleMarkCalls, Stale);

	// The -0.001 edge of the hold test (double 0x10497530).
	R.Arm();
	const int32 EdgeMoves = R.Scripted.MoveRequests.Num();
	const double T1 = R.Now();
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, Blocker, 300.f);
	R.Guard->NavigatorMoveStep();
	R.F.Advance(T1 + 0.2485);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("curtime - nav+0x58 = -0.0015 <= -0.001: held"), R.Scripted.MoveRequests.Num(), EdgeMoves);
	R.F.Advance(T1 + 0.2495);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("curtime - nav+0x58 = -0.0005 > -0.001: the hold has run, re-issued"),
		R.Scripted.MoveRequests.Num(), EdgeMoves + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepHoldSlowCadenceTest,
	"Elysium.Arm.Navigator.MoveStep.Blocked.NpcHoldSlowCadence", GMoveStepTestFlags)
bool FElysiumMoveStepHoldSlowCadenceTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_hold_slow"), 18618);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	const FElysiumEntityHandle Blocker = R.Blocker->Handle;
	const int32 Stale = R.Guard->NavMoveStep.StaleMarkCalls;
	const int32 Moves = R.Scripted.MoveRequests.Num();
	const int32 Reissues = R.Guard->NavMoveStep.BlockerHoldReissues;

	// Thinks 15 s apart (the Normal law `0x10290b60` out of the player's PVS: x10, cap 16 s). Think 1:
	// the body's `Blocked` end naming an NPC -> the hold arms.
	const double T0 = R.Now();
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, Blocker, 300.f);
	R.Guard->NavigatorMoveStep();
	const int32 Arms = R.Guard->NavMoveStep.BlockerHoldArms;
	TestEqual(TEXT("think 1: Blocked -> hold, no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
	TestTrue(TEXT("...the hold stands on the ended request"), R.Guard->Navigator.bBlockerHoldStanding);
	TestEqual(TEXT("...nothing re-issued yet"), R.Scripted.MoveRequests.Num(), Moves);

	// Think 2, +15 s, the same ended request re-read: no new contact (no re-arm), the hold and the
	// window have both run -> the re-probe goes out with a fresh window at this curtime.
	R.F.Advance(T0 + 15.0);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("think 2: no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
	TestEqual(TEXT("...no re-arm on the same end"), R.Guard->NavMoveStep.BlockerHoldArms, Arms);
	TestEqual(TEXT("...the head leg re-issued"), R.Scripted.MoveRequests.Num(), Moves + 1);
	TestEqual(TEXT("...counted"), R.Guard->NavMoveStep.BlockerHoldReissues, Reissues + 1);
	TestEqual(TEXT("...nav+0x60 = curtime + nav+0x64 (a fresh window)"),
		R.Guard->Navigator.BlockerForgetAt, T0 + 18.0, 1e-4);
	TestEqual(TEXT("...the served hold's nav+0x58 untouched"), R.Guard->Navigator.BlockerHoldUntil, T0 + 0.25, 1e-4);

	// Think 3, +30 s: the re-issued leg ended Blocked by the same NPC with no ground closed -- the
	// probe's verdict, judged at its +15 s curtime inside the fresh window: the exhausted hold.
	R.F.Advance(T0 + 30.0);
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, Blocker, 300.f);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("think 3: Blocked again -> 0x0c"), R.Guard->BaseScheduleHost.FailureReason, 0x0c);
	TestTrue(TEXT("...as NPC-blocked (-3)"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::NpcBlocked);
	TestEqual(TEXT("...no re-arm"), R.Guard->NavMoveStep.BlockerHoldArms, Arms);
	TestEqual(TEXT("...no second re-issue"), R.Scripted.MoveRequests.Num(), Moves + 1);
	TestEqual(TEXT("...no stale mark"), R.Guard->NavMoveStep.StaleMarkCalls, Stale);
	TestFalse(TEXT("0x102eeb70 cleared the probe words"),
		R.Guard->Navigator.bBlockerHoldStanding || R.Guard->Navigator.bBlockerHoldReissued);

	// A re-issued leg that walked on (closed >= 1.0 unit) and met the same NPC 15 s later is a new
	// contact at that curtime: the window has lapsed, so it arms a fresh hold rather than failing.
	R.Arm();
	const double T1 = R.Now();
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, Blocker, 300.f);
	R.Guard->NavigatorMoveStep();
	R.F.Advance(T1 + 15.0);
	R.Guard->NavigatorMoveStep();
	const int32 ArmsWalked = R.Guard->NavMoveStep.BlockerHoldArms;
	R.F.Advance(T1 + 30.0);
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, Blocker, 120.f);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("a leg that walked on and was blocked past the window re-arms"),
		R.Guard->NavMoveStep.BlockerHoldArms, ArmsWalked + 1);
	TestEqual(TEXT("...and nothing fails"), R.Guard->BaseScheduleHost.FailureReason, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepSteerTest,
	"Elysium.Arm.Navigator.MoveStep.Blocked.NpcWhileWalking", GMoveStepTestFlags)
bool FElysiumMoveStepSteerTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_steer"), 18610);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	// An NPC named in the way while the request stands: the crowd steers round it (the named
	// modernization for the local navigator's steer and `PrependLocalAvoidance`): keep walking.
	R.Scripted.Walking(200.f);
	R.Scripted.Facts.BlockingEntity = R.Blocker->Handle;
	const int32 Arms = R.Guard->NavMoveStep.BlockerHoldArms;
	const int32 Simplify = R.Guard->NavMoveStep.SimplifyPasses;
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("steering first: no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
	TestEqual(TEXT("0x102efd50 the MoveNormal gate's simplify pass ran once per pass"),
		R.Guard->NavMoveStep.SimplifyPasses, Simplify + R.Guard->NavMoveStep.Passes);
	TestEqual(TEXT("...no hold armed"), R.Guard->NavMoveStep.BlockerHoldArms, Arms);
	TestTrue(TEXT("...no outcome"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::None);
	TestTrue(TEXT("...the route stands"), R.Guard->Navigator.IsGoalActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepToleranceTest,
	"Elysium.Arm.Navigator.MoveStep.Blocked.WithinGoalTolerance", GMoveStepTestFlags)
bool FElysiumMoveStepToleranceTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_tolerance"), 18611);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	// Blocked by the world 5 units from the goal, tolerance 10: `0x102ef760` completes it.
	R.Scripted.Feet = GMoveStepGoalCm - FVector(5.0 * GMoveStepU, 0.0, 0.0);
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, FElysiumEntityHandle(), 5.f);
	const int32 Stale = R.Guard->NavMoveStep.StaleMarkCalls;
	R.Guard->NavigatorMoveStep();
	TestTrue(TEXT("0x102ef760 dist < path+0x28 + 0.1: OnNavComplete"),
		R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::Arrived);
	TestEqual(TEXT("...not a failure"), R.Guard->BaseScheduleHost.FailureReason, 0);
	TestEqual(TEXT("...and no stale mark"), R.Guard->NavMoveStep.StaleMarkCalls, Stale);

	// Blocked by the goal's own target entity (motor code 4): completes too.
	R.Arm();
	R.Guard->Navigator.TargetEntity = R.Blocker->Handle;   // type 4: `0x102ecc40` answers path+0x30
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, R.Blocker->Handle, 300.f);
	R.Guard->NavigatorMoveStep();
	TestTrue(TEXT("motor code 4: blocked by the move target completes"),
		R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::Arrived);
	TestEqual(TEXT("...no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepWorldBlockTest,
	"Elysium.Arm.Navigator.MoveStep.Blocked.WorldFailsWithStaleMark", GMoveStepTestFlags)
bool FElysiumMoveStepWorldBlockTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_world"), 18612);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	R.Scripted.Ended(EElysiumNpcMoveResultCode::Blocked, FElysiumEntityHandle(), 300.f);
	const int32 Stale = R.Guard->NavMoveStep.StaleMarkCalls;
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("0x102f0180 OnNavFailed(0x0c)"), R.Guard->BaseScheduleHost.FailureReason, 0x0c);
	TestEqual(TEXT("0x102f016e the 4.0 s stale mark first"), R.Guard->NavMoveStep.StaleMarkCalls, Stale + 1);
	TestTrue(TEXT("...recorded as a failure"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::Failed);
	TestTrue(TEXT("OnNavFailed does not clear the path: the head stands"), R.Guard->Navigator.IsGoalActive());
	TestTrue(TEXT("...as the kernel's 0x102ee6a0 reader sees it"), R.Guard->NavIsGoalActive());
	TestEqual(TEXT("...and the goal type"), R.Guard->Navigator.GetGoalType(), 4);
	return true;
}

// --- OnNavFailed and the motor without facts ---------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepOnNavFailedTest,
	"Elysium.Arm.Navigator.MoveStep.OnNavFailed.ResetsBlockerMemory", GMoveStepTestFlags)
bool FElysiumMoveStepOnNavFailedTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_onnavfailed"), 18613);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	FElysiumNpcNavigator& Nav = R.Guard->Navigator;
	Nav.BlockerEntity = R.Blocker->Handle;
	Nav.BlockerHoldUntil = R.Now() + 0.25;
	Nav.BlockerForgetAt = R.Now() + 3.0;
	Nav.bBlockerHoldReissued = true;
	Nav.bBlockerHoldStanding = true;
	Nav.BlockerProbeAt = R.Now();
	const int32 LocalNavResets = R.Guard->NavMoveStep.LocalNavResets;
	R.Guard->NavOnNavFailed(0x0c);
	TestFalse(TEXT("0x102eeb70 nav+0x54 := -1"), Nav.BlockerEntity.IsSet());
	TestEqual(TEXT("0x102eeb70 then the local navigator's reset 0x1000b550"), R.Guard->NavMoveStep.LocalNavResets, LocalNavResets + 1);
	TestFalse(TEXT("...and the port's hold re-issue mark"), Nav.bBlockerHoldReissued);
	TestFalse(TEXT("...and its standing mark"), Nav.bBlockerHoldStanding);
	TestEqual(TEXT("...and its probe time"), Nav.BlockerProbeAt, -1.0);
	TestEqual(TEXT("0x102eeb70 nav+0x58 := -1.0"), Nav.BlockerHoldUntil, -1.0);
	TestEqual(TEXT("0x102eeb70 nav+0x60 := -1.0"), Nav.BlockerForgetAt, -1.0);
	TestEqual(TEXT("TaskFail(0x0c)"), R.Guard->BaseScheduleHost.FailureReason, 0x0c);
	TestTrue(TEXT("nav+0x1c = 1"), Nav.bNavFailed);
	TestTrue(TEXT("the head waypoint stands"), Nav.IsGoalActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveStepLegacyMotorTest,
	"Elysium.Arm.Navigator.MoveStep.LegacyMotor.StatusFallback", GMoveStepTestFlags)
bool FElysiumMoveStepLegacyMotorTest::RunTest(const FString&)
{
	FMoveStepRig R(TEXT("movestep_legacy"), 18614);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	// A motor that reports no facts: its own status stands for them.
	R.Scripted.bReportsFacts = false;
	R.Scripted.Status = EElysiumNpcMoveStatus::Reached;
	R.Guard->NavigatorMoveStep();
	TestTrue(TEXT("Reached is an arrival"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::Arrived);

	R.Arm();
	R.Scripted.bReportsFacts = false;
	R.Scripted.Status = EElysiumNpcMoveStatus::Failed;
	const int32 Stale = R.Guard->NavMoveStep.StaleMarkCalls;
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("Failed is a given-up route: 0x0c"), R.Guard->BaseScheduleHost.FailureReason, 0x0c);
	TestEqual(TEXT("...with the stale mark (nothing named, not -3)"), R.Guard->NavMoveStep.StaleMarkCalls, Stale + 1);

	R.Arm();
	R.Scripted.bReportsFacts = false;
	R.Scripted.Status = EElysiumNpcMoveStatus::Moving;
	R.Guard->NavigatorMoveStep();
	TestTrue(TEXT("Moving concludes nothing"), R.Guard->Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::None);
	TestEqual(TEXT("...no fail"), R.Guard->BaseScheduleHost.FailureReason, 0);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
