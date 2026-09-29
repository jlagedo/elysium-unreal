#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcNavigator.h"
#include "Tests/ElysiumNpcTestFixture.h"

// 0018 story 5 lane A -- `CAI_Navigator` / `CAI_Path` as the object the kernel reads and writes:
// the getters' no-goal answers (R1), what `SetGoal` `0x102ecd20` writes onto the path, the reset
// `0x102f28a0` / `0x1030bb30`, the deferred-route window with `nav+0x44 == 0`, the pedestrian byte
// and its per-request cost multiplier, the request's arrival radius (`0x10451f78`), and the route
// sample the navigator takes from the body's move facts.

static constexpr EAutomationTestFlags GNavigatorTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	constexpr float GNavU = ElysiumMove::U;
	// `m_afMemory` bit `0x20`, the deferred-route flag `0x102f28a0` clears.
	constexpr uint32 GNavMemoryPathFailed = 0x20;
	// A goal the recording motor accepts: a location 300 cm ahead.
	const FVector GNavDestCm(300.0, 0.0, 0.0);

	// A guard with a `model` key: without one the leaf builds no body, and without a body the
	// recording services build no motor.
	FElysiumNpcWorldBuilder NavigatorWorld(const TCHAR* Map, uint32 Seed)
	{
		FElysiumNpcWorldBuilder Builder(Map, Seed);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		FElysiumEntityDef& Def = Builder.AddNpc(TEXT("guard"));
		Def.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
		return Builder;
	}

	FElysiumNpcBase::FStartTaskNavGoal LocationGoal(int32 Type)
	{
		FElysiumNpcBase::FStartTaskNavGoal Goal;
		Goal.Type = Type;
		Goal.DestCm = GNavDestCm;
		Goal.bDestSet = true;
		return Goal;
	}
}

// --- The getters' no-goal answers ------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNavigatorNoGoalTest,
	"Elysium.Substrate.Navigator.Getters.NoGoal", GNavigatorTestFlags)
bool FElysiumNavigatorNoGoalTest::RunTest(const FString&)
{
	// The object alone: the path constructor's values (`0x1030bec0`).
	FElysiumNpcNavigator Nav;
	TestFalse(TEXT("0x102ee680 IsGoalSet: type 0 is no goal"), Nav.IsGoalSet());
	TestFalse(TEXT("0x102ee6a0 IsGoalActive: no head waypoint"), Nav.IsGoalActive());
	TestEqual(TEXT("0x102ee620 GetGoalType is 0"), Nav.GetGoalType(), 0);
	TestEqual(TEXT("0x102ee140 the goal position is (0,0,0), never 'none'"), Nav.GetGoalPos(), FVector::ZeroVector);
	TestEqual(TEXT("0x102ee3f0 the movement activity is 1 (ACT_IDLE), not 'unwritten'"), Nav.GetMovementActivity(), 1);
	TestEqual(TEXT("0x1027d990 the nav type is 0 (ground)"), Nav.GetNavType(), 0);
	TestEqual(TEXT("0x102ee1a0 the goal tolerance is 0"), Nav.GetGoalTolerance(), 0.f);
	TestEqual(TEXT("0x102ee640 the goal flags are 0"), Nav.GetGoalFlags(), 0);
	TestFalse(TEXT("0x102ee2e0 is the PAUSED byte and it is clear"), Nav.IsPaused());
	TestFalse(TEXT("0x102ee160 the target is unset (NULL)"), Nav.GetTarget().IsSet());
	TestFalse(TEXT("0x102ee660 no head, no goal bit"), Nav.CurWaypointIsGoal());
	// The goal position subtracts the target offset (`path+0x34`), and the reset zeroes both.
	Nav.GoalPosCm = FVector(10.0, 20.0, 30.0);
	Nav.TargetOffsetCm = FVector(1.0, 2.0, 3.0);
	TestEqual(TEXT("0x102ee140 is goalPos minus the target offset"), Nav.GetGoalPos(), FVector(9.0, 18.0, 27.0));

	// The kernel's readers of it.
	FElysiumNpcWorldFixture F(NavigatorWorld(TEXT("navigator_getters"), 18501));
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	TestEqual(TEXT("NavGoalState (0x102ee620) is the goal type, 0"), Guard->NavGoalState(), 0);
	FVector Goal(1.0, 2.0, 3.0);
	TestTrue(TEXT("NavGoalPosition (0x102ee140) always answers"), Guard->NavGoalPosition(Goal));
	TestEqual(TEXT("...with (0,0,0) after a reset"), Goal, FVector::ZeroVector);
	TestEqual(TEXT("NavigatorPathType (0x102ee620) is the goal type: 0, not the old -1"), Guard->NavigatorPathType(), 0);
	TestEqual(TEXT("NavigatorNavType (0x1027d990) is the nav type: 0"), Guard->NavigatorNavType(), 0);
	TestFalse(TEXT("NavigatorIsGoalSet (0x102ee680)"), Guard->NavigatorIsGoalSet());
	TestFalse(TEXT("NavigatorIsPaused (0x102ee2e0)"), Guard->NavigatorIsPaused());
	TestFalse(TEXT("NavIsGoalSet answers the paused byte (0x102ee2e0), not a goal"), Guard->NavIsGoalSet());
	return true;
}

// --- SetGoal ---------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNavigatorSetGoalTest,
	"Elysium.Substrate.Navigator.SetGoal.WritesTheWords", GNavigatorTestFlags)
bool FElysiumNavigatorSetGoalTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(NavigatorWorld(TEXT("navigator_setgoal"), 18502),
		[](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	FElysiumRecordingNpcMotor* Motor = F.Services.NpcMotors[0].Get();

	FElysiumNpcBase::FStartTaskNavGoal Goal = LocationGoal(4);
	Goal.MovementActivity = 0x13;           // ACT_RUN
	Goal.ToleranceUnits = 10.f;
	Goal.GoalFlags = 8;
	Goal.Target = Guard->Handle;
	TestTrue(TEXT("the recording motor accepts the goal"), Guard->StartTaskSetGoal(Goal, 0));

	const FElysiumNpcNavigator& Nav = Guard->Navigator;
	TestEqual(TEXT("0x1030ba50 path+0x5c: the goal type"), Nav.GetGoalType(), 4);
	TestTrue(TEXT("0x102ee680 IsGoalSet"), Nav.IsGoalSet());
	TestEqual(TEXT("path+0x60 = goal[9], its only writer"), Nav.GetGoalFlags(), 8);
	TestEqual(TEXT("0x102ecec7 path+0x28: the tolerance, centimetres"), Nav.GetGoalTolerance(), 10.f * GNavU, 1e-3f);
	TestEqual(TEXT("0x102ee250 path+0x2c: the movement activity"), Nav.GetMovementActivity(), 0x13);
	TestTrue(TEXT("path+0x4c: the goal position"), Nav.GetGoalPos().Equals(GNavDestCm, 1e-3));
	TestTrue(TEXT("path+0x30: the target handle"), Nav.GetTarget() == Guard->Handle);
	FVector HullMins = FVector::ZeroVector;
	FVector HullMaxs = FVector::ZeroVector;
	Guard->RetailHullExtents(Guard->PathingHullKind, FElysiumNpcBase::EElysiumHullExtents::Full, HullMins, HullMaxs);
	TestEqual(TEXT("0x102ececa path+0x40: the pathing hull * 0.5"), Nav.WaypointToleranceCm,
		static_cast<float>(HullMaxs.X - HullMins.X) * 0.5f * GNavU, 1e-3f);
	TestTrue(TEXT("an accepted request to the goal: a head waypoint stands"), Nav.IsGoalActive());
	TestTrue(TEXT("...and it is the goal's own (wp+0x28 & 8)"), Nav.CurWaypointIsGoal());

	// The kernel's readers see the same object.
	TestEqual(TEXT("NavGoalState"), Guard->NavGoalState(), 4);
	FVector GoalUnits = FVector::ZeroVector;
	TestTrue(TEXT("NavGoalPosition"), Guard->NavGoalPosition(GoalUnits));
	TestTrue(TEXT("...in Source units"), GoalUnits.Equals(GNavDestCm / GNavU, 1e-3));
	TestEqual(TEXT("NavigatorPathType"), Guard->NavigatorPathType(), 4);
	TestTrue(TEXT("NavigatorIsGoalSet"), Guard->NavigatorIsGoalSet());

	// The request the goal became (R3: the arrival radius is the waypoint constant, NOT path+0x28).
	const FElysiumNpcMoveRequest& Request = Motor->LastMoveRequest;
	TestTrue(TEXT("the request goes to the goal"), Request.DestinationCm.Equals(GNavDestCm, 1e-3));
	TestEqual(TEXT("0x10451f78 the arrival radius is 0.0625 units"), Request.AcceptanceToleranceCm, 0.0625f * GNavU, 1e-4f);
	TestTrue(TEXT("...and is not the path's goal tolerance"),
		!FMath::IsNearlyEqual(Request.AcceptanceToleranceCm, Nav.GetGoalTolerance(), 1e-3f));
	TestTrue(TEXT("ACT_RUN names the run fan"), Request.GaitKind.IsSet() && *Request.GaitKind == EElysiumNpcGaitKind::Run);
	TestTrue(TEXT("a goal is refused, not partially walked"), Request.PartialPath == EElysiumNpcPartialPath::Refuse);
	TestEqual(TEXT("a type-4 goal draws no pedestrian multiplier"), Request.PedestrianCostMultiplier, 0);

	// Goal flag 2 clears the target and the offset (`0x102ecd7b`).
	Guard->Navigator.TargetOffsetCm = FVector(4.0, 5.0, 6.0);
	FElysiumNpcBase::FStartTaskNavGoal Flagged = LocationGoal(4);
	TestTrue(TEXT("the goal is accepted"), Guard->StartTaskSetGoal(Flagged, 2));
	TestFalse(TEXT("flag 2 cleared path+0x30"), Guard->Navigator.GetTarget().IsSet());
	TestEqual(TEXT("flag 2 cleared path+0x34..0x3c"), Guard->Navigator.TargetOffsetCm, FVector::ZeroVector);
	return true;
}

// --- The reset -------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNavigatorResetTest,
	"Elysium.Substrate.Navigator.Reset.Values", GNavigatorTestFlags)
bool FElysiumNavigatorResetTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(NavigatorWorld(TEXT("navigator_reset"), 18503),
		[](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	FElysiumNpcBase::FStartTaskNavGoal Goal = LocationGoal(4);
	Goal.MovementActivity = 0x13;
	Goal.ToleranceUnits = 10.f;
	Goal.GoalFlags = 8;
	Goal.Target = Guard->Handle;
	TestTrue(TEXT("the goal is accepted"), Guard->StartTaskSetGoal(Goal, 0));
	Guard->Navigator.TargetOffsetCm = FVector(1.0, 2.0, 3.0);
	Guard->Navigator.bPedestrian = true;
	Guard->Navigator.RouteSearchTime = 2.f;
	Guard->BaseScheduleHost.MemoryBits |= GNavMemoryPathFailed;

	// `ClearGoal` `0x102ee270` -> `0x102f28a0` -> `0x1030bb30`.
	const int32 ClearsBefore = Guard->StartTaskNav.ClearGoalCalls;
	Guard->StartTaskClearGoal();
	const FElysiumNpcNavigator& Nav = Guard->Navigator;
	TestEqual(TEXT("ClearGoal ran once"), Guard->StartTaskNav.ClearGoalCalls, ClearsBefore + 1);
	TestEqual(TEXT("path+0x5c := 0"), Nav.GetGoalType(), 0);
	TestFalse(TEXT("IsGoalSet"), Nav.IsGoalSet());
	TestEqual(TEXT("goalPos and offset back to the origin"), Nav.GetGoalPos(), FVector::ZeroVector);
	TestEqual(TEXT("...each of them"), Nav.TargetOffsetCm, FVector::ZeroVector);
	TestEqual(TEXT("path+0x2c := 1"), Nav.GetMovementActivity(), 1);
	TestEqual(TEXT("path+0x28 := 0"), Nav.GetGoalTolerance(), 0.f);
	TestFalse(TEXT("path+0x1 (pedestrian) cleared"), Nav.bPedestrian);
	TestFalse(TEXT("the waypoint list emptied"), Nav.IsGoalActive());
	TestFalse(TEXT("...and its goal bit with it"), Nav.CurWaypointIsGoal());
	TestEqual(TEXT("nav+0x40 := 0 (the route search time)"), Nav.RouteSearchTime, 0.f);
	TestEqual(TEXT("nav+0x44 := 0"), Nav.RouteRetryInterval, 0.f);
	TestEqual(TEXT("nav+0x48 := 0"), Nav.RouteGiveUpTime, 0.0);
	TestEqual(TEXT("nav+0x4c := 0"), Nav.RouteRetryTime, 0.0);
	TestEqual(TEXT("memory bit 0x20 cleared"), Guard->BaseScheduleHost.MemoryBits & GNavMemoryPathFailed, 0u);
	// `0x1030bb30` clears the goal flags (`path+0x60`), the target handle (`path+0x30`) and the paused
	// byte (`path+0x10`) too (review V1a #1, read off the listing).
	TestEqual(TEXT("path+0x60 := 0"), Nav.GetGoalFlags(), 0);
	TestFalse(TEXT("path+0x30 := -1"), Nav.GetTarget().IsSet());
	TestFalse(TEXT("path+0x10 := 0"), Nav.IsPaused());

	// `SetGoal` flag bit 1 is the other road into the reset, and it runs BEFORE the goal's own words:
	// a standing activity does not survive it.
	Guard->Navigator.MovementActivity = 9;
	Guard->Navigator.GoalToleranceCm = 77.f;
	FElysiumNpcBase::FStartTaskNavGoal Second = LocationGoal(4);
	TestTrue(TEXT("the goal is accepted"), Guard->StartTaskSetGoal(Second, 1));
	TestEqual(TEXT("SetGoal flag 1 reset the activity to 1 (0x102ecd74)"), Guard->Navigator.GetMovementActivity(), 1);
	TestEqual(TEXT("...and the type the goal then wrote stands"), Guard->Navigator.GetGoalType(), 4);
	TestTrue(TEXT("...with a head waypoint from the new request"), Guard->Navigator.IsGoalActive());

	// The path reset on the object alone.
	FElysiumNpcNavigator Loose;
	Loose.GoalType = 8;
	Loose.bPedestrian = true;
	Loose.bHasHeadWaypoint = true;
	Loose.bHeadIsGoal = true;
	Loose.MovementActivity = 0x13;
	Loose.GoalToleranceCm = 5.f;
	Loose.GoalPosCm = FVector(1.0, 1.0, 1.0);
	Loose.ResetPath();
	TestEqual(TEXT("ResetPath: type"), Loose.GetGoalType(), 0);
	TestEqual(TEXT("ResetPath: activity"), Loose.GetMovementActivity(), 1);
	TestFalse(TEXT("ResetPath: pedestrian"), Loose.bPedestrian);
	TestFalse(TEXT("ResetPath: head"), Loose.IsGoalActive());
	return true;
}

// --- The deferred-route window, nav+0x44 == 0 ------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNavigatorRetryWindowTest,
	"Elysium.Substrate.Navigator.RetryWindow.NoIntervalRetriesEveryThink", GNavigatorTestFlags)
bool FElysiumNavigatorRetryWindowTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(NavigatorWorld(TEXT("navigator_retry"), 18504),
		[](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	FElysiumRecordingNpcMotor* Motor = F.Services.NpcMotors[0].Get();
	FElysiumNpcNavigator& Nav = Guard->Navigator;

	// A search window of 2 s (`TASK_SET_ROUTE_SEARCH_TIME`) and a route the body refuses.
	Motor->bAcceptMoves = false;
	Nav.RouteSearchTime = 2.f;
	Guard->BaseScheduleHost.MemoryBits &= ~GNavMemoryPathFailed;
	const double T0 = F.World.NowSeconds();
	TestFalse(TEXT("the first search fails"), Guard->NavBuildRoute(true, GNavDestCm));
	TestTrue(TEXT("0x102f1f12 the deferred-route bit is set"), (Guard->BaseScheduleHost.MemoryBits & GNavMemoryPathFailed) != 0);
	TestEqual(TEXT("nav+0x44 is 0: retail never writes it"), Nav.RouteRetryInterval, 0.f);
	TestEqual(TEXT("0x102f1f27 nav+0x4c := curtime + 0"), Nav.RouteRetryTime, T0, 1e-6);
	TestEqual(TEXT("0x102f1f36 nav+0x48 := curtime + nav+0x40"), Nav.RouteGiveUpTime, T0 + 2.0, 1e-6);
	TestFalse(TEXT("a refused search leaves no head waypoint"), Nav.IsGoalActive());

	// The compare is strict (`nav+0x4c < curtime`): the failing instant does not retry.
	Motor->bAcceptMoves = true;
	const int32 MovesBefore = F.Services.Count(TEXT("NpcMotor MoveTo"));
	TestFalse(TEXT("0x102f1f73 the same instant does not retry"), Guard->NavBuildRoute(true, GNavDestCm));
	TestEqual(TEXT("...and issued no request"), F.Services.Count(TEXT("NpcMotor MoveTo")), MovesBefore);

	// The next think does, with no interval to wait out.
	F.Advance(T0 + 0.5);
	TestTrue(TEXT("0x102f1f80 the next think retries and finds the route"), Guard->NavBuildRoute(true, GNavDestCm));
	TestEqual(TEXT("0x102f1f8d the bit is cleared by the retry"), Guard->BaseScheduleHost.MemoryBits & GNavMemoryPathFailed, 0u);
	TestTrue(TEXT("...and the accepted request stands as the head"), Nav.IsGoalActive());

	// Past nav+0x48 the next build fails the task with 0xc (`0x102f1f5a`).
	Motor->bAcceptMoves = false;
	const double T1 = F.World.NowSeconds();
	TestFalse(TEXT("a second refused search opens a new window"), Guard->NavBuildRoute(true, GNavDestCm));
	TestTrue(TEXT("...with the bit set again"), (Guard->BaseScheduleHost.MemoryBits & GNavMemoryPathFailed) != 0);
	Guard->BaseScheduleHost.FailureReason = 0;
	F.Advance(T1 + 3.0);
	TestFalse(TEXT("past the give-up time the build fails"), Guard->NavBuildRoute(true, GNavDestCm));
	TestEqual(TEXT("0x102f1f5a OnNavFailed(0xc): FAIL_NO_ROUTE"), Guard->BaseScheduleHost.FailureReason, 0xc);
	return true;
}

// --- The pedestrian byte and the per-request multiplier --------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNavigatorPedestrianTest,
	"Elysium.Substrate.Navigator.Pedestrian.ByteAndMultiplier", GNavigatorTestFlags)
bool FElysiumNavigatorPedestrianTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(NavigatorWorld(TEXT("navigator_pedestrian"), 18505),
		[](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	FElysiumRecordingNpcMotor* Motor = F.Services.NpcMotors[0].Get();

	// A type-8 goal (interesting place, pedestrian) sets path+0x1 and prices each request.
	TSet<int32> Seen;
	for (int32 Round = 0; Round < 40; ++Round)
	{
		Guard->StartTaskClearGoal();
		FElysiumNpcBase::FStartTaskNavGoal Goal = LocationGoal(8);
		TestTrue(TEXT("a type-8 goal is accepted"), Guard->StartTaskSetGoal(Goal, 0));
		if (!TestTrue(TEXT("path+0x1 is set for a type-8 goal"), Guard->Navigator.bPedestrian))
		{
			return false;
		}
		const int32 Multiplier = Motor->LastMoveRequest.PedestrianCostMultiplier;
		if (!TestTrue(FString::Printf(TEXT("the multiplier %d is RandomInt(5, 10)"), Multiplier),
			Multiplier >= 5 && Multiplier <= 10))
		{
			return false;
		}
		Seen.Add(Multiplier);
	}
	TestTrue(TEXT("the multiplier is drawn per request, not fixed"), Seen.Num() > 1);

	// The path reset clears the byte; a type-9 goal (the ANIMAL place) never sets it.
	Guard->StartTaskClearGoal();
	TestFalse(TEXT("the reset cleared path+0x1"), Guard->Navigator.bPedestrian);
	FElysiumNpcBase::FStartTaskNavGoal Animal = LocationGoal(9);
	TestTrue(TEXT("a type-9 goal is accepted"), Guard->StartTaskSetGoal(Animal, 0));
	TestFalse(TEXT("path+0x1 stays clear for type 9"), Guard->Navigator.bPedestrian);
	TestEqual(TEXT("...and the request carries no multiplier"), Motor->LastMoveRequest.PedestrianCostMultiplier, 0);
	TestEqual(TEXT("NavigatorPathType reads 9"), Guard->NavigatorPathType(), 9);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
