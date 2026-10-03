#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumLocalIdSpace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcNavigator.h"
#include "Substrate/ElysiumScheduleNumbers.h"
#include "Tests/ElysiumNpcTestFixture.h"

// 0018 story 5 lane E -- goal type 3 (the path-corner chain) and goal flag 2 (the explicit node route).
//
// The chain: `DoFindPath` `0x102f2330` (case 3) lays it from `m_pGoalEnt` and copies the first corner's
// `speed` onto the walker; `AdvancePath` `0x102f0400` hands each reached corner the input `"InPass"`
// (so its `OnPass` fires with the NPC as activator), advances `m_pGoalEnt` to `GetNextTarget()` and
// re-lays the chain from it; the goal corner gets `InPass` too and nothing else. `wait` is never read.
//
// Flag 2: `SetGoal` `0x102ecd20` (test `0x102ecf27`, taken `0x102ecf2e`) -- nearest nodes, a route,
// none of the route build `0x102f1dc0`.

static constexpr EAutomationTestFlags GCornerChainTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	constexpr float GCornerU = ElysiumMove::U;
	constexpr int32 GCornerTaskPathCorner = 0x120;
	constexpr int32 GCornerTaskGetPathToEnemyClosest = 0xc7;
	constexpr int32 GCornerFailNoRoute = 0x0c;

	// What the suite-local corner saw: who was handed `InPass`, by whom, in order.
	struct FCornerLog
	{
		TArray<FString> Corners;
		TArray<FElysiumEntityHandle> Activators;
		TArray<FElysiumEntityHandle> Callers;
	};
	FCornerLog GCornerLog;

	// `CPathCorner`'s two observable behaviours, as a suite-local class (the retail class is
	// instantiated in no shipped map, so the port stands none): slot 172 `GetNextTarget` answers the
	// entity `target` names, and `InPass` (`CPathCorner::InputInPass 0x10147d50`) fires `OnPass` with the
	// input's activator.
	class FCornerChainTestCorner final : public FElysiumEntity
	{
	public:
		virtual FElysiumEntity* GetNextTarget() override
		{
			return World != nullptr && !Target.IsEmpty() ? World->FindByName(Target) : nullptr;
		}

		void InPass(const FElysiumInputArgs& Args)
		{
			GCornerLog.Corners.Add(TargetName);
			GCornerLog.Activators.Add(Args.Activator);
			GCornerLog.Callers.Add(Args.Caller);
			FireOutput(FName(TEXT("OnPass")), Args.Activator);
		}
	};

	TUniquePtr<FElysiumEntity> MakeCornerChainTestCorner()
	{
		return MakeUnique<FCornerChainTestCorner>();
	}

	FElysiumClassRegistrar GRegCornerChainTestCorner(TEXT("elysium_test_path_corner"), ElysiumBaseClassName(),
		&MakeCornerChainTestCorner,
		[](FElysiumClassDesc& Desc)
		{
			Desc.Input(FName(TEXT("InPass")), [](FElysiumEntity& Self, const FElysiumInputArgs& Args)
			{
				static_cast<FCornerChainTestCorner&>(Self).InPass(Args);
			});
		});

	const FVector GCorner0Cm(300.0, 0.0, 0.0);
	const FVector GCorner1Cm(300.0, 400.0, 0.0);
	const FVector GCorner2Cm(0.0, 400.0, 0.0);

	// A guard with a model (so the recording services build its motor), a counter, and the chain
	// `c0 -> c1 -> c2`: distinct `speed`s, and a `wait` on two of them that nothing may read.
	FElysiumNpcWorldBuilder CornerChainWorld(const TCHAR* Map, uint32 Seed)
	{
		FElysiumNpcWorldBuilder Builder(Map, Seed);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		Builder.AddNpc(TEXT("guard")).Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
		Builder.AddCounter(TEXT("passes"));
		{
			FElysiumEntityDef& C0 = Builder.AddEntity(TEXT("elysium_test_path_corner"), TEXT("c0"), GCorner0Cm);
			C0.Keys.Add(TEXT("target"), TEXT("c1"));
			C0.Keys.Add(TEXT("speed"), TEXT("111"));
			C0.Keys.Add(TEXT("wait"), TEXT("30"));
		}
		{
			FElysiumEntityDef& C1 = Builder.AddEntity(TEXT("elysium_test_path_corner"), TEXT("c1"), GCorner1Cm);
			C1.Keys.Add(TEXT("target"), TEXT("c2"));
			C1.Keys.Add(TEXT("speed"), TEXT("222"));
		}
		{
			FElysiumEntityDef& C2 = Builder.AddEntity(TEXT("elysium_test_path_corner"), TEXT("c2"), GCorner2Cm);
			C2.Keys.Add(TEXT("speed"), TEXT("333"));
			C2.Keys.Add(TEXT("wait"), TEXT("99"));
		}
		Builder.WireOutput(TEXT("c0"), TEXT("OnPass"), TEXT("passes"));
		Builder.WireOutput(TEXT("c1"), TEXT("OnPass"), TEXT("passes"));
		Builder.WireOutput(TEXT("c2"), TEXT("OnPass"), TEXT("passes"));
		return Builder;
	}

	int32 CornerChainGlobalTask(FElysiumNpc* Npc, int32 LocalTask)
	{
		const FElysiumLocalIdSpace* Space = Npc->IdSpace(EElysiumIdCategory::Task);
		return Space != nullptr ? Space->LocalToGlobal(LocalTask) : LocalTask;
	}

	bool CornerChainFailedWith(const FElysiumNpc* Npc, int32 Reason)
	{
		return Npc->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed)
			&& Npc->BaseScheduleHost.FailureReason == Reason;
	}
}

// --- The chain -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCornerChainThreeCornersTest,
	"Elysium.Arm.Navigator.CornerChain.ThreeCorners", GCornerChainTestFlags)
bool FElysiumCornerChainThreeCornersTest::RunTest(const FString&)
{
	GCornerLog = FCornerLog();
	FElysiumNpcWorldFixture F(CornerChainWorld(TEXT("corner_chain_three"), 18701),
		[](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumEntity* C0 = F.World.FindByName(TEXT("c0"));
	FElysiumEntity* C1 = F.World.FindByName(TEXT("c1"));
	FElysiumEntity* C2 = F.World.FindByName(TEXT("c2"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("c0"), C0) || !TestNotNull(TEXT("c1"), C1)
		|| !TestNotNull(TEXT("c2"), C2) || !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	FElysiumRecordingNpcMotor* Motor = F.Services.NpcMotors[0].Get();
	TestEqual(TEXT("the corner's speed key reached its m_flSpeed"), C0->AuthoredSpeed, 111.f);
	TestEqual(TEXT("...and the corner's target"), C0->Target, FString(TEXT("c1")));

	// ScheduledFollowPath 0x102801e0: type 3, m_pGoalEnt := c0, DoFindPath lays the chain.
	TestTrue(TEXT("0x102802a8 the chain is laid and the head leg accepted"),
		Guard->ScheduledFollowPath(ElysiumSched::IDLE_WALK, C0, 9));
	FElysiumNpcNavigator& Nav = Guard->Navigator;
	TestEqual(TEXT("0x1030ba50 the path type is 3"), Nav.GetGoalType(), 3);
	TestTrue(TEXT("0x102f2393 the first corner's speed is copied onto the walker"), Guard->AuthoredSpeed == 111.f);
	TestTrue(TEXT("a head waypoint stands"), Nav.IsGoalActive());
	TestFalse(TEXT("0x102f24ca the head is not the goal: the goal bit rides the LAST corner"), Nav.CurWaypointIsGoal());
	TestTrue(TEXT("the leg goes to the first corner"), Motor->RequestedFeet.Equals(GCorner0Cm, 0.01));
	TestEqual(TEXT("a non-goal corner is passed at the waypoint constant 0.0625 (0x10451f78), not path+0x40"),
		Motor->LastMoveRequest.AcceptanceToleranceCm, 0.0625f * GCornerU, 1e-4f);
	TestTrue(TEXT("...and that is not the path's waypoint tolerance"),
		Nav.WaypointToleranceCm > 0.0625f * GCornerU);
	TestTrue(TEXT("nothing has been passed yet"), GCornerLog.Corners.Num() == 0);
	TestTrue(TEXT("m_pGoalEnt is the first corner"), Guard->BaseScheduleHost.GoalEnt == C0->Handle);

	// Corner 0 reached (a non-goal arrival, what NavigatorMoveStep hands AdvancePath).
	TestTrue(TEXT("0x102f0400 a head still stands"), Guard->NavAdvancePath());
	TestEqual(TEXT("0x102f0451 InPass reached one corner"), GCornerLog.Corners.Num(), 1);
	TestEqual(TEXT("...c0"), GCornerLog.Corners[0], FString(TEXT("c0")));
	TestTrue(TEXT("...with the walking NPC as activator"), GCornerLog.Activators[0] == Guard->Handle);
	TestTrue(TEXT("...and the corner as caller"), GCornerLog.Callers[0] == C0->Handle);
	TestTrue(TEXT("0x102f04de m_pGoalEnt := c0->GetNextTarget()"), Guard->BaseScheduleHost.GoalEnt == C1->Handle);
	TestTrue(TEXT("0x102f2393 the re-find copies the NEW first corner's speed"), Guard->AuthoredSpeed == 222.f);
	TestTrue(TEXT("the next leg goes to c1"), Motor->RequestedFeet.Equals(GCorner1Cm, 0.01));
	TestFalse(TEXT("c1 is not the last corner"), Nav.CurWaypointIsGoal());
	TestEqual(TEXT("...and is passed at the same waypoint constant (0x10451f78)"),
		Motor->LastMoveRequest.AcceptanceToleranceCm, 0.0625f * GCornerU, 1e-4f);

	// Corner 1 reached: the chain now holds only the last corner, which is the goal.
	TestTrue(TEXT("a head still stands"), Guard->NavAdvancePath());
	TestEqual(TEXT("InPass reached the second corner"), GCornerLog.Corners.Num(), 2);
	TestEqual(TEXT("...c1"), GCornerLog.Corners[1], FString(TEXT("c1")));
	TestTrue(TEXT("...activator the NPC"), GCornerLog.Activators[1] == Guard->Handle);
	TestTrue(TEXT("m_pGoalEnt is the last corner"), Guard->BaseScheduleHost.GoalEnt == C2->Handle);
	TestTrue(TEXT("its speed is copied"), Guard->AuthoredSpeed == 333.f);
	TestTrue(TEXT("the leg goes to c2"), Motor->RequestedFeet.Equals(GCorner2Cm, 0.01));
	TestTrue(TEXT("0x102f24d1 the last corner is the goal"), Nav.CurWaypointIsGoal());
	TestEqual(TEXT("...arrived at within the goal waypoint radius (0x10451f78)"),
		Motor->LastMoveRequest.AcceptanceToleranceCm, 0.0625f * GCornerU, 1e-4f);

	// The goal corner reached (TaskMovementComplete -> AdvancePath, 0x10273ec0): InPass, and nothing else.
	TestTrue(TEXT("the goal branch pops nothing"), Guard->NavAdvancePath());
	TestEqual(TEXT("InPass reached the last corner too"), GCornerLog.Corners.Num(), 3);
	TestEqual(TEXT("...c2"), GCornerLog.Corners[2], FString(TEXT("c2")));
	TestTrue(TEXT("m_pGoalEnt is NOT advanced past the goal corner"), Guard->BaseScheduleHost.GoalEnt == C2->Handle);
	TestTrue(TEXT("no leg was issued"), Motor->RequestedFeet.Equals(GCorner2Cm, 0.01));
	TestTrue(TEXT("the head still stands and is the goal"), Nav.IsGoalActive() && Nav.CurWaypointIsGoal());
	TestFalse(TEXT("nothing failed"), Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));

	// `wait` (30, 99) was never read: the walk went corner to corner on the same clock.
	TestEqual(TEXT("no corner paused the walk"), F.World.NowSeconds(), 0.0);

	// Every OnPass is delivered through the event queue, once per corner.
	F.Advance(0.5);
	TestEqual(TEXT("OnPass fired once per corner"), F.Counter(TEXT("passes")), 3.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCornerChainSingleCornerTest,
	"Elysium.Arm.Navigator.CornerChain.SingleCornerIsGoal", GCornerChainTestFlags)
bool FElysiumCornerChainSingleCornerTest::RunTest(const FString&)
{
	GCornerLog = FCornerLog();
	FElysiumNpcWorldFixture F(CornerChainWorld(TEXT("corner_chain_single"), 18702),
		[](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumEntity* C2 = F.World.FindByName(TEXT("c2"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("c2"), C2)
		|| !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	FElysiumRecordingNpcMotor* Motor = F.Services.NpcMotors[0].Get();

	// `c2` has no next corner: a chain of one, whose head is the goal.
	TestTrue(TEXT("the goal is accepted"), Guard->ScheduledFollowPath(ElysiumSched::IDLE_WALK, C2, 9));
	TestTrue(TEXT("0x102f24c3 a one-corner chain's head is the goal"), Guard->Navigator.CurWaypointIsGoal());
	TestTrue(TEXT("the leg goes to the corner"), Motor->RequestedFeet.Equals(GCorner2Cm, 0.01));
	TestTrue(TEXT("the corner's speed is copied"), Guard->AuthoredSpeed == 333.f);
	TestEqual(TEXT("the goal waypoint radius"), Motor->LastMoveRequest.AcceptanceToleranceCm,
		0.0625f * GCornerU, 1e-4f);
	TestEqual(TEXT("nothing has been passed"), GCornerLog.Corners.Num(), 0);

	// Arrival at the goal: InPass, no advance, m_pGoalEnt stays.
	TestTrue(TEXT("the goal branch keeps the head"), Guard->NavAdvancePath());
	TestEqual(TEXT("InPass reached the goal corner"), GCornerLog.Corners.Num(), 1);
	TestTrue(TEXT("...with the NPC as activator"), GCornerLog.Activators[0] == Guard->Handle);
	TestTrue(TEXT("m_pGoalEnt stays on the last corner"), Guard->BaseScheduleHost.GoalEnt == C2->Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCornerChainRetargetedNextTest,
	"Elysium.Arm.Navigator.CornerChain.NullNextCornerEndsSilently", GCornerChainTestFlags)
bool FElysiumCornerChainRetargetedNextTest::RunTest(const FString&)
{
	GCornerLog = FCornerLog();
	FElysiumNpcWorldFixture F(CornerChainWorld(TEXT("corner_chain_null_next"), 18703),
		[](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumEntity* C0 = F.World.FindByName(TEXT("c0"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("c0"), C0)
		|| !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	TestTrue(TEXT("the chain is laid"), Guard->ScheduledFollowPath(ElysiumSched::IDLE_WALK, C0, 9));

	// `SetNextPathCorner` (`0x10147d10`) rewrites the corner's target: the next `GetNextTarget` is null.
	C0->Target.Empty();
	TestFalse(TEXT("0x102f0400 the re-find of a null corner leaves no head"), Guard->NavAdvancePath());
	TestEqual(TEXT("InPass still reached the corner passed"), GCornerLog.Corners.Num(), 1);
	TestFalse(TEXT("m_pGoalEnt is null (0x102f04de stores the null)"), Guard->BaseScheduleHost.GoalEnt.IsSet());
	TestFalse(TEXT("0x102f2614 DoFindPath answers 0 and nothing is failed: the path is just emptied"),
		Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	TestFalse(TEXT("no head stands"), Guard->Navigator.IsGoalActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCornerChainPathcornerTaskTest,
	"Elysium.Arm.Navigator.CornerChain.PathcornerTask_0x120", GCornerChainTestFlags)
bool FElysiumCornerChainPathcornerTaskTest::RunTest(const FString&)
{
	GCornerLog = FCornerLog();
	FElysiumNpcWorldFixture F(CornerChainWorld(TEXT("corner_chain_task"), 18704),
		[](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumEntity* C0 = F.World.FindByName(TEXT("c0"));
	FElysiumEntity* C1 = F.World.FindByName(TEXT("c1"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("c0"), C0) || !TestNotNull(TEXT("c1"), C1)
		|| !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	FElysiumNpcWorldFixture::PrepareForKernelDrive(Guard);
	FElysiumRecordingNpcMotor* Motor = F.Services.NpcMotors[0].Get();

	// `CAI_BaseNPC::StartTask` task `0x120` (`0x10285ff8`): m_target set, m_pGoalEnt is the first corner.
	Guard->Target = TEXT("c0");
	Guard->BaseScheduleHost.GoalEnt = C0->Handle;
	Guard->Schedule.TaskStatus = EElysiumTaskStatus::New;
	FElysiumScheduleStep Step;
	Step.TaskId = CornerChainGlobalTask(Guard, GCornerTaskPathCorner);
	Guard->FElysiumNpcBase::StartTaskSlot442(&Step);

	FElysiumNpcNavigator& Nav = Guard->Navigator;
	TestEqual(TEXT("0x102860d4 the goal record is type 3"), Guard->StartTaskNav.LastGoal.Type, 3);
	TestEqual(TEXT("...with goal flags 1"), Guard->StartTaskNav.LastGoal.GoalFlags, 1);
	TestEqual(TEXT("the path type is 3"), Nav.GetGoalType(), 3);
	TestEqual(TEXT("...and the goal flags word is 1 (path+0x60)"), Nav.GetGoalFlags(), 1);
	TestTrue(TEXT("the first corner's speed is copied"), Guard->AuthoredSpeed == 111.f);
	TestFalse(TEXT("the head is not the goal"), Nav.CurWaypointIsGoal());
	TestTrue(TEXT("the leg goes to the first corner"), Motor->RequestedFeet.Equals(GCorner0Cm, 0.01));
	TestFalse(TEXT("the task did not fail"), Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));

	TestTrue(TEXT("a corner passed advances the chain"), Guard->NavAdvancePath());
	TestTrue(TEXT("m_pGoalEnt is c1"), Guard->BaseScheduleHost.GoalEnt == C1->Handle);
	TestEqual(TEXT("InPass reached c0"), GCornerLog.Corners.Num(), 1);
	return true;
}

// --- Goal flag 2 ---------------------------------------------------------------------------------------

namespace
{
	// A guard and a foe `FoeXCm` away, the AI network holding a node at each when `bPlaces`.
	FElysiumNpcWorldBuilder GoalFlagWorld(const TCHAR* Map, uint32 Seed, bool bPlaces, double FoeXCm)
	{
		FElysiumNpcWorldBuilder Builder(Map, Seed);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		Builder.AddNpc(TEXT("guard")).Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
		Builder.AddNpc(TEXT("foe"), FVector(FoeXCm, 0.0, 0.0));
		if (bPlaces)
		{
			Builder.AddPlace(2, FVector::ZeroVector);
		}
		return Builder;
	}

	// Run Troika `StartTask` (`0x102a1910`) on one step, from a clean task status.
	void RunTroikaTask(FElysiumNpc* Npc, int32 LocalTask)
	{
		Npc->Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
		Npc->Schedule.TaskStatus = EElysiumTaskStatus::New;
		Npc->BaseScheduleHost.FailureReason = 0;
		FElysiumScheduleStep Step;
		Step.TaskId = CornerChainGlobalTask(Npc, LocalTask);
		Npc->FElysiumNpc::StartTaskSlot442(&Step);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGoalFlag2NoNetworkTest,
	"Elysium.Arm.Navigator.GoalFlag2.NoNetworkRefuses", GCornerChainTestFlags)
bool FElysiumGoalFlag2NoNetworkTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(GoalFlagWorld(TEXT("goal_flag2_no_network"), 18711, false, 400.0),
		[](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumNpc* Foe = F.Npc(TEXT("foe"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("foe"), Foe)
		|| !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Foe });
	FElysiumNpcWorldFixture::PrepareForKernelDrive(Guard);
	Guard->BaseMemory.Enemy = Foe->Handle;

	RunTroikaTask(Guard, GCornerTaskGetPathToEnemyClosest);                  // 0x102a4812
	TestEqual(TEXT("0x102a48f2 the goal record carries flags 2"), Guard->StartTask19LastGoal.GoalFlags, 2);
	TestEqual(TEXT("...on the one SetGoal body's record"), Guard->StartTaskNav.LastGoal.GoalFlags, 2);
	TestEqual(TEXT("0x102a4842 type 4, the enemy's origin"), Guard->StartTaskNav.LastGoal.Type, 4);
	TestTrue(TEXT("...at the enemy"), Guard->StartTaskNav.LastGoal.DestCm.Equals(Foe->Origin, 0.01));
	TestFalse(TEXT("0x102f3c3a an empty network refuses the node route"), Guard->StartTaskNav.bLastSetGoalResult);
	TestTrue(TEXT("0x102a4942 TaskFail(0x0c)"), CornerChainFailedWith(Guard, GCornerFailNoRoute));
	TestEqual(TEXT("0x102a4935 the DevWarning"), Guard->StartTaskNav.LastDevMessage,
		FString(TEXT("GetPathToEnemy failed!!\n")));
	TestEqual(TEXT("the route build 0x102f1dc0 never ran: no route search state moved"),
		Guard->BaseScheduleHost.MemoryBits & 0x20u, 0u);
	TestEqual(TEXT("no route was installed"), Guard->Navigator.PathNoGoalInstalls, 0);
	TestEqual(TEXT("path+0x60 is not written by a flag-2 goal (its store follows the flag-2 return)"),
		Guard->Navigator.GetGoalFlags(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGoalFlag2RoutesTest,
	"Elysium.Arm.Navigator.GoalFlag2.NodeRoute", GCornerChainTestFlags)
bool FElysiumGoalFlag2RoutesTest::RunTest(const FString&)
{
	// A node at the guard and one at the foe.
	FElysiumNpcWorldBuilder Builder = GoalFlagWorld(TEXT("goal_flag2_route"), 18712, true, 400.0);
	Builder.AddPlace(2, FVector(400.0, 0.0, 0.0));
	FElysiumNpcWorldFixture F(MoveTemp(Builder),
		[](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumNpc* Foe = F.Npc(TEXT("foe"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("foe"), Foe)
		|| !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Foe });
	FElysiumNpcWorldFixture::PrepareForKernelDrive(Guard);
	FElysiumRecordingNpcMotor* Motor = F.Services.NpcMotors[0].Get();
	Guard->BaseMemory.Enemy = Foe->Handle;

	RunTroikaTask(Guard, GCornerTaskGetPathToEnemyClosest);
	TestEqual(TEXT("0x102a48f2 the goal record carries flags 2"), Guard->StartTaskNav.LastGoal.GoalFlags, 2);
	TestTrue(TEXT("0x102ecf2e the node route is installed"), Guard->StartTaskNav.bLastSetGoalResult);
	TestEqual(TEXT("...through the install tail SetRandomGoal shares (0x1030b4d0)"), Guard->Navigator.PathNoGoalInstalls, 1);
	TestEqual(TEXT("0x1030ba50 path+0x5c := 4"), Guard->Navigator.GetGoalType(), 4);
	TestEqual(TEXT("path+0x60 is NOT written: its store follows the flag-2 return"), Guard->Navigator.GetGoalFlags(), 0);
	TestTrue(TEXT("the follower is asked for the route to the enemy"), Motor->RequestedFeet.Equals(Foe->Origin, 0.01));
	TestTrue(TEXT("a head stands"), Guard->Navigator.IsGoalActive());
	TestTrue(TEXT("0x102a4928 the task completes"), Guard->Schedule.TaskStatus == EElysiumTaskStatus::Complete
		&& !Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGoalFlag2FarGoalTest,
	"Elysium.Arm.Navigator.GoalFlag2.NoNodeNearGoalRefuses", GCornerChainTestFlags)
bool FElysiumGoalFlag2FarGoalTest::RunTest(const FString&)
{
	// The only node is at the guard; the foe stands far past the 2048-unit box `0x102f41b0` searches.
	FElysiumNpcWorldFixture F(GoalFlagWorld(TEXT("goal_flag2_far"), 18713, true, 20000.0),
		[](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumNpc* Foe = F.Npc(TEXT("foe"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("foe"), Foe)
		|| !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Foe });
	FElysiumNpcWorldFixture::PrepareForKernelDrive(Guard);
	Guard->BaseMemory.Enemy = Foe->Handle;

	RunTroikaTask(Guard, GCornerTaskGetPathToEnemyClosest);
	TestFalse(TEXT("0x102f41b0 -1: no node near the goal refuses"), Guard->StartTaskNav.bLastSetGoalResult);
	TestTrue(TEXT("0x102a4942 TaskFail(0x0c)"), CornerChainFailedWith(Guard, GCornerFailNoRoute));
	TestEqual(TEXT("0x102a4935 the DevWarning"), Guard->StartTaskNav.LastDevMessage,
		FString(TEXT("GetPathToEnemy failed!!\n")));
	TestEqual(TEXT("no route was installed"), Guard->Navigator.PathNoGoalInstalls, 0);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
