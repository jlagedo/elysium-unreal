#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapEntities.h"
#include "ElysiumMapPlaces.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumLocalIdSpace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumPlaceSet.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"

// 0018 story 4, commits 5 and 6: the kernel's node reads answered by the place set, and the capped
// point pick behind `TASK_GET_PATH_TO_RANDOM_NODE`.
//
//   Substrate.PlaceSeams.HintArms        `CAI_Hint::GetPosition 0x102d1180` / `0x102f46d0`, the hint
//                                        yaw `0x102d12e0` / `0x102f47b0`, the hint's type and origin,
//                                        the live hint list.
//   Substrate.PlaceSeams.Patrol          `0x102d2900`'s node id, `PatrolNodePosition` at `m_eHull`,
//                                        `0x102aa640` to the node, `PatrolNodeInterestRecord` the hint,
//                                        `0x1029f610` / `0x10307ac0` over the network.
//   Substrate.PlaceSeams.UnusableNode    `0x1027db30` -> slot 527 `0x10293e80`.
//   Substrate.PlaceSeams.ListNodesInBox  `0x102f32f0`'s selection, its eviction quirk and its order.
//   Substrate.PlaceSeams.NearestNode     `0x102f41b0`: the box, the order, the first clear line.
//   Substrate.PlaceSeams.Wander.*        the pick, clause by clause, as the task arm `0x10285d7f`
//                                        calls it.
//   Content.Places.WanderHub             the pick on `sm_hub_1`'s baked place set, from a
//                                        pedestrian's spawn.

static constexpr EAutomationTestFlags GElysiumPlaceSeamFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace ElysiumPlaceSeamTests
{
	constexpr double U = ElysiumMove::U;

	FVector Cm(double X, double Y, double Z)
	{
		return FVector(X, Y, Z) * U;
	}

	// Every hull's Z offset on a builder's place: `1 + hull` cm, so the hull a caller asks with shows.
	void LiftPlaces(FElysiumNpcWorldBuilder& Builder)
	{
		for (FElysiumPlaceRow& Row : Builder.Places)
		{
			for (int32 Hull = 0; Hull < ElysiumRetailHulls::Count; ++Hull)
			{
				Row.ZOffsetCm[Hull] = 1.0f + static_cast<float>(Hull);
			}
		}
	}

	FElysiumEntityDef& AddHintRow(FElysiumNpcWorldBuilder& Builder, const TCHAR* Classname, const TCHAR* Name,
		const FVector& OriginCm, const TCHAR* HintType, const TCHAR* Group = nullptr)
	{
		FElysiumEntityDef& Def = Builder.AddEntity(Classname, Name, OriginCm);
		Def.Keys.Add(TEXT("hinttype"), HintType);
		if (Group != nullptr)
		{
			Def.Keys.Add(TEXT("Group"), Group);
		}
		return Def;
	}

	// The class-LOCAL task as the GLOBAL id a schedule step carries.
	int32 GlobalTask(const FElysiumNpc& Npc, int32 LocalTask)
	{
		const FElysiumLocalIdSpace* Space = Npc.IdSpace(EElysiumIdCategory::Task);
		return Space != nullptr ? Space->LocalToGlobal(LocalTask) : LocalTask;
	}

	// One base `StartTask` call from a clean task status, as `FStartTask19BaseFixture::Run` makes it.
	void RunBaseTask(FElysiumNpc& Npc, int32 LocalTask, float Data)
	{
		Npc.Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
		Npc.Schedule.TaskStatus = EElysiumTaskStatus::New;
		Npc.BaseScheduleHost.FailureReason = 0;
		FElysiumScheduleStep Step;
		Step.TaskId = GlobalTask(Npc, LocalTask);
		Step.Data = Data;
		Npc.FElysiumNpcBase::StartTaskSlot442(&Step);
	}

	constexpr int32 TaskGetPathToRandomNode = 0x1f;   // TASK_GET_PATH_TO_RANDOM_NODE (0x10316ff0)
	constexpr int32 TaskWander = 0x76;                // TASK_WANDER
	constexpr int32 FailNoReachableNode = 0x18;       // FAIL_NO_REACHABLE_NODE
}

// -------------------------------------------------------------------------------------------------
// The hint's node arms.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSeamHintArmsTest,
	"Elysium.Substrate.PlaceSeams.HintArms", GElysiumPlaceSeamFlags)
bool FElysiumPlaceSeamHintArmsTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSeamTests;
	FElysiumNpcWorldBuilder Builder(TEXT("__place_seams_hints__"), 41);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddPlace(2, Cm(300.0, 20.0, 0.0), /*YawDeg (Unreal)=*/-30.0f);   // node 0
	Builder.AddPlace(2, Cm(600.0, 0.0, 0.0));                                // node 1
	LiftPlaces(Builder);
	AddHintRow(Builder, TEXT("info_node_patrol_point"), TEXT("pp"), Cm(301.0, 20.0, 0.0), TEXT("10000"), TEXT("A1"));
	FElysiumEntityDef& Loose = AddHintRow(Builder, TEXT("info_hint"), TEXT("loose"), Cm(50.0, 60.0, 5.0), TEXT("10100"));
	Loose.Keys.Add(TEXT("angles"), TEXT("0 45 0"));
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumHint* Bound = FElysiumHint::Cast(F.World.FindByName(TEXT("pp")));
	FElysiumHint* Unbound = FElysiumHint::Cast(F.World.FindByName(TEXT("loose")));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("the node-bound hint"), Bound)
		|| !TestNotNull(TEXT("the standalone hint"), Unbound))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	TestEqual(TEXT("the patrol point took node 0"), Bound->NodeId, 0);
	TestEqual(TEXT("the info_hint is standalone"), Unbound->NodeId, static_cast<int32>(INDEX_NONE));

	// `0x102d1180`: the bound hint answers its NODE at the NPC's pathing hull (`0x102f46d0`, `+0x156c`).
	FVector Pos = FVector(-1.0);
	TestTrue(TEXT("the bound hint has a position"), Guard->HintPositionCm(Bound->Handle.Index, Pos));
	const float PathLift = 1.0f + static_cast<float>(Guard->PathingHullKind);
	TestTrue(*FString::Printf(TEXT("...the node at the pathing hull, not the hint's origin (%s)"), *Pos.ToString()),
		Pos.Equals(Cm(300.0, 20.0, 0.0) + FVector(0.0, 0.0, PathLift), 1e-3));
	// A pathing hull that differs from the standing one is the one asked (the Sheriff's split).
	Guard->PathingHullKind = 5;
	Guard->HintPositionCm(Bound->Handle.Index, Pos);
	TestEqual(TEXT("...it follows +0x156c"), Pos.Z, 6.0);
	Guard->PathingHullKind = Guard->HullKind;
	TestTrue(TEXT("the standalone hint answers its own origin"), Guard->HintPositionCm(Unbound->Handle.Index, Pos)
		&& Pos.Equals(Unbound->Origin, 1e-3));
	FVector Units = FVector::ZeroVector;
	TestTrue(TEXT("HintStandPosition is the same body in cm / U"),
		Guard->HintStandPosition(Bound->Handle.Index, Units)
		&& Units.Equals((Cm(300.0, 20.0, 0.0) + FVector(0.0, 0.0, PathLift)) / U, 1e-3));
	// `0x102f46d0`'s out-of-range arms answer `vec3_origin`, and still answer.
	Bound->NodeId = 7;
	TestTrue(TEXT("an id past the network still answers"), Guard->HintPositionCm(Bound->Handle.Index, Pos));
	TestEqual(TEXT("...vec3_origin"), Pos, FVector::ZeroVector);
	Bound->NodeId = 0;
	TestFalse(TEXT("an index that names no hint answers false"), Guard->HintPositionCm(Guard->Handle.Index, Pos));

	// `0x102d12e0`: the node's `+0x6c` (Source yaw, the row's reflected back), or the hint's own angles.
	float Yaw = 0.0f;
	TestTrue(TEXT("the bound hint's yaw is its node's"), Guard->HintYaw(Bound->Handle.Index, Yaw));
	TestEqual(TEXT("...in the retail frame"), Yaw, 30.0f);
	TestTrue(TEXT("StartTaskHintYaw is the same body"), Guard->StartTaskHintYaw(Bound->Handle.Index, Yaw) && Yaw == 30.0f);
	Guard->HintYaw(Unbound->Handle.Index, Yaw);
	TestEqual(TEXT("the standalone hint's yaw is its own angles.y"), Yaw, 45.0f);
	Bound->NodeId = 7;
	Guard->HintYaw(Bound->Handle.Index, Yaw);
	TestEqual(TEXT("0x102f47b0 past the network answers 0.0"), Yaw, 0.0f);
	Bound->NodeId = 0;

	// The hint's own words: its type and its `GetAbsOrigin`, in the retail frame's units.
	int32 Type = 0;
	TestTrue(TEXT("NavHintNodeType reads m_nHintType"), Guard->NavHintNodeType(Unbound->Handle.Index, Type) && Type == 10100);
	FVector OriginUnits = FVector::ZeroVector;
	TestTrue(TEXT("NavHintNodeOrigin reads the hint's origin"), Guard->NavHintNodeOrigin(Bound->Handle.Index, OriginUnits));
	TestTrue(TEXT("...the HINT's, in Source units (Y reflected)"), OriginUnits.Equals(FVector(301.0, -20.0, 0.0), 1e-3));

	// The live list, head first, and the candidates gathered from it.
	TArray<int32> List;
	TestTrue(TEXT("NavAllHintNodes answers the world's list"), Guard->NavAllHintNodes(List));
	TestTrue(TEXT("...in its own order"), List == F.World.HintList());
	TArray<FElysiumNpcBase::FHintWords> Nodes;
	TArray<int32> NodeIds;
	Guard->GatherHintNodes(Nodes, NodeIds);
	TestEqual(TEXT("GatherHintNodes holds both hints"), Nodes.Num(), 2);
	TestTrue(TEXT("...in list order"), NodeIds == List);
	const int32 BoundAt = NodeIds.IndexOfByKey(Bound->Handle.Index);
	TestTrue(TEXT("...with the hint's own origin, not its node's"),
		Nodes.IsValidIndex(BoundAt) && Nodes[BoundAt].OriginCm.Equals(Bound->Origin, 1e-3));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Patrol.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSeamPatrolTest,
	"Elysium.Substrate.PlaceSeams.Patrol", GElysiumPlaceSeamFlags)
bool FElysiumPlaceSeamPatrolTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSeamTests;
	FElysiumNpcWorldBuilder Builder(TEXT("__place_seams_patrol__"), 43);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddPlace(2, Cm(100.0, 0.0, 0.0));   // node 0: a1
	Builder.AddPlace(2, Cm(200.0, 0.0, 0.0));   // node 1: a2
	Builder.AddPlace(2, Cm(300.0, 0.0, 0.0));   // node 2: no hint
	LiftPlaces(Builder);
	FElysiumEntityDef& A1 = AddHintRow(Builder, TEXT("info_node_patrol_point"), TEXT("a1"), Cm(101.0, 0.0, 0.0), TEXT("10000"), TEXT("A1"));
	A1.Keys.Add(TEXT("ip_percent"), TEXT("100"));
	A1.Keys.Add(TEXT("target_name"), TEXT("spot"));
	AddHintRow(Builder, TEXT("info_node_patrol_point"), TEXT("a2"), Cm(201.0, 0.0, 0.0), TEXT("10000"), TEXT("A2"));
	AddHintRow(Builder, TEXT("info_hint"), TEXT("loose"), Cm(0.0, 50.0, 0.0), TEXT("10000"), TEXT("S"));
	FElysiumEntityDef& Guard = Builder.AddNpc(TEXT("guard"));
	Guard.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder), [](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Npc = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Npc) || !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	FElysiumRecordingNpcMotor* Motor = F.Services.NpcMotors[0].Get();
	FElysiumNpc::ResetPatrolPathPool();
	using EBuild = FElysiumNpc::EPatrolPathBuild;

	// `0x102d2900`: the hint's `m_nNodeID`, not its entity.
	TestEqual(TEXT("A1 is node 0"), Npc->PatrolNodeIdFor(TEXT("A1")), 0);
	TestEqual(TEXT("A2 is node 1"), Npc->PatrolNodeIdFor(TEXT("A2")), 1);
	TestEqual(TEXT("a standalone patrol hint has no node: -1"), Npc->PatrolNodeIdFor(TEXT("S")), static_cast<int32>(INDEX_NONE));

	// The follow-path input builds a path of NETWORK indices.
	FElysiumInputArgs Args;
	Args.Param = FElysiumVariant::String(TEXT("A1 A2"));
	Npc->InputFollowPatrolPath(Args);
	if (!TestNotNull(TEXT("the path was built"), Npc->PatrolPathCell.Path))
	{
		return false;
	}
	TestTrue(TEXT("...of network indices"), Npc->PatrolPathCell.Path->Count == 2
		&& Npc->PatrolPathCell.Path->Nodes[0] == 0 && Npc->PatrolPathCell.Path->Nodes[1] == 1);

	// `PatrolNodePosition` at the hull the caller names; `0x102aa640` names `m_eHull`.
	FVector At = FVector::ZeroVector;
	TestTrue(TEXT("node 1 at hull 3"), Npc->PatrolNodePosition(1, 3, At) == FElysiumNpc::EPatrolNode::Found
		&& At.Equals(Cm(200.0, 0.0, 0.0) + FVector(0.0, 0.0, 4.0), 1e-3));
	TestTrue(TEXT("node 3 is out of range"), Npc->PatrolNodePosition(3, 0, At) == FElysiumNpc::EPatrolNode::OutOfRange);
	Npc->IssuePatrolMoveStart(&Npc->PatrolPathCell);
	const FVector Want = Cm(100.0, 0.0, 0.0) + FVector(0.0, 0.0, 1.0f + static_cast<float>(Npc->HullKind));
	TestTrue(*FString::Printf(TEXT("0x102aa640 routes to node 0 at m_eHull (%s)"), *Motor->RequestedFeet.ToString()),
		Motor->RequestedFeet.Equals(Want, 1e-3));

	// `PatrolNodeInterestRecord`: the node's hint, bounds-checked; its `ip_percent` and `target_name`.
	const FElysiumEntity* A1Hint = F.World.FindByName(TEXT("a1"));
	TestEqual(TEXT("node 0 holds a1"), Npc->PatrolNodeInterestRecord(0), A1Hint != nullptr ? A1Hint->Handle.Index : -2);
	TestEqual(TEXT("node 2 holds no hint"), Npc->PatrolNodeInterestRecord(2), static_cast<int32>(INDEX_NONE));
	const int32 Misses = FElysiumNpc::PatrolNodeMissCounter();
	TestEqual(TEXT("node 9 is past the network"), Npc->PatrolNodeInterestRecord(9), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("...counted"), FElysiumNpc::PatrolNodeMissCounter(), Misses + 1);
	TestEqual(TEXT("NavNodeWordAt is the same read, 0 for none"), Npc->NavNodeWordAt(2), 0);
	TestEqual(TEXT("a1's ip_percent"), Npc->PatrolNodeInterestPercent(Npc->PatrolNodeInterestRecord(0)), 100);
	TestTrue(TEXT("0x1029f650 draws against it and sets the flag"), Npc->FUN_1029f650(0));
	FString Name;
	TestTrue(TEXT("the record's +0x468"), Npc->PatrolNodeInterestRecordName(Npc->PatrolNodeInterestRecord(0), Name)
		&& Name == TEXT("spot"));

	// `0x1029f610` / `0x10307ac0` over the network.
	TestTrue(TEXT("a path of network nodes validates"), Npc->FUN_1029f610(&Npc->PatrolPathCell));
	const int32 Far[] = { 0, 3, -1 };
	Npc->BuildPatrolPath(&Npc->PatrolPathHuntCell, 0, 0, 0, Far, EBuild::Replace);
	const int32 MissesFar = FElysiumNpc::PatrolNodeMissCounter();
	TestFalse(TEXT("one id past the count fails it"), Npc->FUN_1029f610(&Npc->PatrolPathHuntCell));
	TestEqual(TEXT("...bumping DAT_106c994c"), FElysiumNpc::PatrolNodeMissCounter(), MissesFar + 1);
	Npc->OnRestore(true);
	TestNotNull(TEXT("the restore keeps the valid route"), Npc->PatrolPathCell.Path);
	TestNull(TEXT("...and releases the invalid one"), Npc->PatrolPathHuntCell.Path);
	FElysiumNpc::ResetPatrolPathPool();
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x1027db30` and slot 527.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSeamUnusableNodeTest,
	"Elysium.Substrate.PlaceSeams.UnusableNode", GElysiumPlaceSeamFlags)
bool FElysiumPlaceSeamUnusableNodeTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSeamTests;
	FElysiumNpcWorldBuilder Builder(TEXT("__place_seams_unusable__"), 47);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddPlace(2, Cm(100.0, 0.0, 0.0));
	Builder.AddPlace(2, Cm(200.0, 0.0, 0.0));
	AddHintRow(Builder, TEXT("info_node_patrol_point"), TEXT("a1"), Cm(100.0, 0.0, 0.0), TEXT("10000"), TEXT("A1"));
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Npc = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	// Node 0 holds a hint, and `IsHintAvailableToMe` (`0x102d1540`, story 8's seam) answers free.
	TestFalse(TEXT("a node whose hint is available is usable"), Npc->IsUnusableNodeIndex(0));
	TestFalse(TEXT("a node with no hint is usable"), Npc->IsUnusableNodeIndex(1));
	const int32 Misses = FElysiumNpc::PatrolNodeMissCounter();
	TestFalse(TEXT("an index past the network answers false"), Npc->IsUnusableNodeIndex(2));
	TestEqual(TEXT("...and bumps DAT_106c994c (0x1027db5f)"), FElysiumNpc::PatrolNodeMissCounter(), Misses + 1);
	TestFalse(TEXT("slot 527 on the row itself"),
		Npc->IsUnusableNode(const_cast<FElysiumPlaceRow*>(&F.World.Places().Row(0))));
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CAI_Network::ListNodesInBox` `0x102f32f0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSeamListNodesTest,
	"Elysium.Substrate.PlaceSeams.ListNodesInBox", GElysiumPlaceSeamFlags)
bool FElysiumPlaceSeamListNodesTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSeamTests;
	// Twelve nodes on +X at the listed distances (units), in index order: the first ten fill the
	// queue; node 10 (distance 1) is nearer than its head (5) and EVICTS it; node 11 (20) is not and
	// stays out. So the answer drops the node at 5 -- retail's `0x102f3770` on the result queue.
	const float Distances[] = { 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 1, 20 };
	TArray<FElysiumPlaceRow> Rows;
	for (const float D : Distances)
	{
		FElysiumPlaceRow Row;
		Row.Type = 2;
		Row.OriginCm = Cm(D, 0.0, 0.0);
		Rows.Add(Row);
	}
	FElysiumPlaceRow Outside;
	Outside.Type = 2;
	Outside.OriginCm = Cm(0.0, 0.0, 101.0);   // one unit outside the box
	Rows.Add(Outside);
	FElysiumPlaceSet Places;
	Places.AdoptRows(Rows);
	const TArray<int32> Order = Places.ListNodesInBox(10, FVector(-100.0), FVector(100.0),
		[](int32) { return true; },
		[&Places](int32 Node) { return static_cast<float>(Places.Row(Node).OriginCm.SizeSquared() / (U * U)); });
	const TArray<int32> Want = { 10, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	TestTrue(*FString::Printf(TEXT("nearest first, the node at 5 evicted, the far one kept out (%s)"),
		*FString::JoinBy(Order, TEXT(","), [](int32 N) { return FString::FromInt(N); })), Order == Want);
	const TArray<int32> Valid = Places.ListNodesInBox(10, FVector(-100.0), FVector(100.0),
		[](int32 Node) { return Node % 2 == 0; },
		[&Places](int32 Node) { return static_cast<float>(Places.Row(Node).OriginCm.SizeSquared() / (U * U)); });
	TestTrue(TEXT("the filter's slot 0 admits first"), Valid == TArray<int32>({ 10, 0, 2, 4, 6, 8 }));
	// The box edge taken off node 0's own origin, so "on the edge" is exact.
	const double Edge0 = Places.Row(0).OriginCm.X / U;
	const TArray<int32> Edge = Places.ListNodesInBox(10, FVector(-Edge0, -1.0, -1.0), FVector(Edge0, 1.0, 1.0),
		[](int32) { return true; }, [](int32) { return 0.0f; });
	TestTrue(TEXT("the box is inclusive at both ends"), Edge.Contains(0) && Edge.Contains(10) && !Edge.Contains(1));
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x102f41b0`, the network's nearest node to a point.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSeamNearestNodeTest,
	"Elysium.Substrate.PlaceSeams.NearestNode", GElysiumPlaceSeamFlags)
bool FElysiumPlaceSeamNearestNodeTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSeamTests;
	FElysiumNpcWorldBuilder Builder(TEXT("__place_seams_nearest__"), 53);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddPlace(2, Cm(3000.0, 0.0, 0.0));   // node 0: outside the 2048 box
	Builder.AddPlace(2, Cm(0.0, 300.0, 0.0));    // node 1
	Builder.AddPlace(2, Cm(100.0, 0.0, 0.0));    // node 2: the nearest
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Npc = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	TestEqual(TEXT("the nearest node with a clear line"), Npc->NavNearestNodeTo(FVector::ZeroVector), 2);
	// A wall on the line to node 2: the next nearest is taken.
	FElysiumRecordingServices::FCameraHullBlocker Wall;
	Wall.PointCm = Cm(50.0, 0.0, 0.0);
	Wall.RadiusCm = 10.0f;
	F.Services.CameraHullBlockers.Add(Wall);
	TestEqual(TEXT("a blocked line falls to the next nearest"), Npc->NavNearestNodeTo(FVector::ZeroVector), 1);
	Wall.PointCm = Cm(0.0, 150.0, 0.0);
	F.Services.CameraHullBlockers.Add(Wall);
	TestEqual(TEXT("every line blocked: -1"), Npc->NavNearestNodeTo(FVector::ZeroVector), -1);
	F.Services.CameraHullBlockers.Reset();
	TestEqual(TEXT("a point with no node inside ±2048: -1"), Npc->NavNearestNodeTo(Cm(-5000.0, 0.0, 0.0)), -1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The capped point pick, `TASK_GET_PATH_TO_RANDOM_NODE` (0018 story 4, "The pick, entire").
// -------------------------------------------------------------------------------------------------

namespace ElysiumPlaceSeamTests
{
	// A guard at the origin facing +X (Source yaw 0) with a recording motor whose routes are the
	// straight line, over the places a case lays.
	struct FWanderFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumRecordingNpcMotor* Motor = nullptr;

		explicit FWanderFixture(TFunctionRef<void(FElysiumNpcWorldBuilder&)> Lay)
			: World([&Lay]
			{
				FElysiumNpcWorldBuilder Builder(TEXT("__place_seams_wander__"), 59);
				Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
				FElysiumEntityDef& Def = Builder.AddNpcOfClass(TEXT("guard"), FVector::ZeroVector, TEXT("CNPC_VHumanCombatant"));
				Def.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
				Lay(Builder);
				return MoveTemp(Builder);
			}(), [](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; })
		{
			Guard = World.Npc(TEXT("guard"));
			Motor = World.Services.NpcMotors.Num() > 0 ? World.Services.NpcMotors[0].Get() : nullptr;
			FElysiumNpcWorldFixture::Quiet({ Guard });
			if (Motor != nullptr)
			{
				Motor->RouteQuery = [](const FVector& DestCm, float& OutLengthCm)
				{
					OutLengthCm = static_cast<float>(DestCm.Size());
					return true;
				};
			}
		}

		bool Ready(FAutomationTestBase& Test) const
		{
			return Test.TestNotNull(TEXT("guard"), Guard) && Test.TestNotNull(TEXT("a recording motor"), Motor);
		}

		// The task arm `0x10285d7f` with an order of `Units`.
		void Run(float Units)
		{
			RunBaseTask(*Guard, TaskGetPathToRandomNode, Units);
		}
		bool Completed() const { return Guard->Schedule.TaskStatus == EElysiumTaskStatus::Complete; }
		int32 Reason() const { return Guard->BaseScheduleHost.FailureReason; }
		int32 Picked() const { return Guard->StartTaskNav.LastRandomGoalNode; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSeamWanderOrderTest,
	"Elysium.Substrate.PlaceSeams.Wander.Order200", GElysiumPlaceSeamFlags)
bool FElysiumPlaceSeamWanderOrderTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSeamTests;
	// Ahead and in the outer half: 1, 2. Ahead but near: 3. Behind: 4. Beyond 200: 5. Ahead, in
	// range, a climb node: 6.
	FWanderFixture F([](FElysiumNpcWorldBuilder& B)
	{
		B.AddPlace(2, Cm(-500.0, 0.0, 0.0));   // 0: far behind
		B.AddPlace(2, Cm(150.0, 20.0, 0.0));   // 1
		B.AddPlace(2, Cm(120.0, -60.0, 0.0));  // 2
		B.AddPlace(2, Cm(40.0, 0.0, 0.0));     // 3
		B.AddPlace(2, Cm(-150.0, 0.0, 0.0));   // 4
		B.AddPlace(2, Cm(260.0, 0.0, 0.0));    // 5
		B.AddPlace(4, Cm(170.0, 0.0, 0.0));    // 6
	});
	if (!F.Ready(*this))
	{
		return false;
	}
	const int32 SetGoalsBefore = F.Guard->StartTaskNav.SetGoalCalls;
	const int32 GoalTypeBefore = F.Guard->StartTaskNav.LastGoal.Type;
	const float ToleranceBefore = F.Guard->Navigator.GoalToleranceCm;
	TSet<int32> Seen;
	for (int32 Round = 0; Round < 24; ++Round)
	{
		F.Run(200.0f);
		if (!TestTrue(TEXT("an order of 200 completes inside StartTask"), F.Completed()))
		{
			return false;
		}
		Seen.Add(F.Picked());
	}
	TestTrue(TEXT("every pick is a place within 200 and ahead, in the outer half"),
		Seen.Num() > 0 && Seen.Difference(TSet<int32>({ 1, 2 })).Num() == 0);
	TestEqual(TEXT("...and the draw reaches both"), Seen.Num(), 2);
	// The installed route: a PATH, no goal.
	TestEqual(TEXT("SetGoal is never called"), F.Guard->StartTaskNav.SetGoalCalls, SetGoalsBefore);
	TestEqual(TEXT("...so no goal type is recorded"), F.Guard->StartTaskNav.LastGoal.Type, GoalTypeBefore);
	TestEqual(TEXT("...and no tolerance is written"), F.Guard->Navigator.GoalToleranceCm, ToleranceBefore);
	TestEqual(TEXT("the path type word is 4 (0x1030ba50)"), F.Guard->Navigator.GoalType, 4);
	TestEqual(TEXT("each pick installed one path"), F.Guard->Navigator.PathNoGoalInstalls, 24);
	FVector Last = FVector::ZeroVector;
	F.World.World.Places().GetPositionCm(F.Picked(), F.Guard->PathingHullKind, Last);
	TestTrue(TEXT("the mover walks to the picked place"), F.Motor->RequestedFeet.Equals(Last, 1e-3));
	TestEqual(TEXT("...with retail's waypoint arrival radius, 0.0625 units (0x10451f78)"),
		F.Motor->LastMoveRequest.AcceptanceToleranceCm, static_cast<float>(0.0625 * U), 1e-4f);
	TestTrue(TEXT("...the goal-less install headed a path"), F.Guard->Navigator.IsGoalActive());
	TestEqual(TEXT("nav +0x14 is the endpoint distance squared"), F.Guard->Navigator.EndpointDistanceSqrUnits,
		static_cast<float>(FVector::DistSquared(F.Guard->Origin, Last) / (U * U)), 0.5f);

	// A zero direction skips the ahead test: with only the places behind in range, one is drawn.
	FWanderFixture Behind([](FElysiumNpcWorldBuilder& B)
	{
		B.AddPlace(2, Cm(-150.0, 0.0, 0.0));
		B.AddPlace(2, Cm(-120.0, 40.0, 0.0));
	});
	if (Behind.Ready(*this))
	{
		Behind.Run(200.0f);
		TestTrue(TEXT("the task arm's facing still finds a place behind when none is ahead"), Behind.Completed());
		TestTrue(TEXT("a zero direction draws from every candidate"),
			Behind.Guard->StartTaskSetRandomGoal(200.0f, FVector::ZeroVector));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSeamWanderCapTest,
	"Elysium.Substrate.PlaceSeams.Wander.Cap", GElysiumPlaceSeamFlags)
bool FElysiumPlaceSeamWanderCapTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSeamTests;
	FWanderFixture F([](FElysiumNpcWorldBuilder& B)
	{
		B.AddPlace(2, Cm(400.0, 0.0, 0.0));    // 0: inside the cap
		B.AddPlace(2, Cm(480.0, 50.0, 0.0));   // 1: inside
		B.AddPlace(2, Cm(900.0, 0.0, 0.0));    // 2: beyond the cap, inside the order
		B.AddPlace(2, Cm(3000.0, 0.0, 0.0));   // 3: beyond
		FElysiumPlaceWanderCap Human;
		Human.Hull = 0;
		Human.CapUnits = 500.0f;
		B.WanderCaps.Add(Human);
	});
	if (!F.Ready(*this))
	{
		return false;
	}
	for (int32 Round = 0; Round < 16; ++Round)
	{
		F.Run(4096.0f);
		TestTrue(TEXT("an order of 4096 completes"), F.Completed());
		TestTrue(*FString::Printf(TEXT("...never beyond the cap (picked %d)"), F.Picked()), F.Picked() == 0 || F.Picked() == 1);
	}
	TestEqual(TEXT("the searched distance is the cap"), F.Guard->StartTaskNav.LastRandomGoalDistanceUnits, 500.0f);
	// A hull the map declares no cap for takes the human's.
	F.Guard->PathingHullKind = 12;
	F.Run(4096.0f);
	TestEqual(TEXT("an undeclared hull takes the human cap"), F.Guard->StartTaskNav.LastRandomGoalDistanceUnits, 500.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSeamWanderTiersTest,
	"Elysium.Substrate.PlaceSeams.Wander.Tiers", GElysiumPlaceSeamFlags)
bool FElysiumPlaceSeamWanderTiersTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSeamTests;
	FWanderFixture F([](FElysiumNpcWorldBuilder& B)
	{
		B.AddPlace(2, Cm(150.0, 0.0, 0.0));    // 0: cooling
		B.AddPlace(2, Cm(-40.0, 0.0, 0.0));    // 1: expired, behind and near -- the worst band
		B.AddPlace(4, Cm(160.0, 0.0, 0.0));    // 2: a climb node, never
	});
	if (!F.Ready(*this))
	{
		return false;
	}
	FElysiumPlaceSet& Places = F.World.World.Places();
	const float Now = static_cast<float>(F.World.World.NowSeconds());
	Places.SetNodeCooldown(0, Now + 100.0f);
	for (int32 Round = 0; Round < 8; ++Round)
	{
		F.Run(200.0f);
		TestEqual(TEXT("an expired place, however poor its band, wins over a cooling one"), F.Picked(), 1);
	}
	Places.SetNodeCooldown(1, Now + 100.0f);
	F.Run(200.0f);
	TestEqual(TEXT("a cooling place is picked when no expired one stands"), F.Picked(), 0);
	Places.SetNodeCooldown(0, Now);
	Places.SetNodeCooldown(1, Now + 100.0f);
	F.Run(200.0f);
	TestEqual(TEXT("a cooldown AT curtime has expired (0x102ff3e0's `<=`)"), F.Picked(), 0);

	// Only the climb node in range: nothing to pick.
	FWanderFixture Climb([](FElysiumNpcWorldBuilder& B) { B.AddPlace(4, Cm(150.0, 0.0, 0.0)); });
	if (Climb.Ready(*this))
	{
		Climb.Run(200.0f);
		TestFalse(TEXT("a type-4 place is never picked"), Climb.Completed());
		TestEqual(TEXT("...TaskFail(0x18)"), Climb.Reason(), FailNoReachableNode);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSeamWanderFailTest,
	"Elysium.Substrate.PlaceSeams.Wander.Fail", GElysiumPlaceSeamFlags)
bool FElysiumPlaceSeamWanderFailTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSeamTests;
	FWanderFixture F([](FElysiumNpcWorldBuilder& B)
	{
		B.AddPlace(2, Cm(900.0, 0.0, 0.0));
	});
	if (!F.Ready(*this))
	{
		return false;
	}
	const int32 SetGoalsBefore = F.Guard->StartTaskNav.SetGoalCalls;
	F.Run(200.0f);
	TestFalse(TEXT("no place in range does not complete"), F.Completed());
	TestTrue(TEXT("...it fails inside StartTask"), F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	TestEqual(TEXT("...with 0x18"), F.Reason(), FailNoReachableNode);
	TestEqual(TEXT("...and no draw"), F.Guard->StartTaskNav.LastRandomGoalDraws, 0);
	TestEqual(TEXT("...and no SetGoal"), F.Guard->StartTaskNav.SetGoalCalls, SetGoalsBefore);

	// `TASK_WANDER 0x76`: five radial draws (the probe is story 5's seam), then `SetRandomGoal(1.0,
	// vec3_origin)` -- the same pick, whose one-unit order finds no place.
	const int32 PicksBefore = F.Guard->StartTaskNav.RandomGoalRequests;
	RunBaseTask(*F.Guard, TaskWander, 20050.0f);
	TestEqual(TEXT("TASK_WANDER falls back to the pick"), F.Guard->StartTaskNav.RandomGoalRequests, PicksBefore + 1);
	TestEqual(TEXT("...with an order of 1.0"), F.Guard->StartTaskNav.LastRandomGoalOrderUnits, 1.0f);
	TestEqual(TEXT("...after recording the wander's own max"), F.Guard->StartTaskNav.LastSearchMaxUnits, 50.0f);
	TestEqual(TEXT("...which finds nothing: 0x18"), F.Reason(), FailNoReachableNode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSeamWanderDetourTest,
	"Elysium.Substrate.PlaceSeams.Wander.Detour", GElysiumPlaceSeamFlags)
bool FElysiumPlaceSeamWanderDetourTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSeamTests;
	FWanderFixture F([](FElysiumNpcWorldBuilder& B)
	{
		B.AddPlace(2, Cm(150.0, 0.0, 0.0));    // 0: the far side of a wall
		B.AddPlace(2, Cm(160.0, 30.0, 0.0));   // 1: no route at all
		B.AddPlace(2, Cm(140.0, -30.0, 0.0));  // 2: a fair route
	});
	if (!F.Ready(*this))
	{
		return false;
	}
	const FVector Wall = Cm(150.0, 0.0, 0.0);
	const FVector Island = Cm(160.0, 30.0, 0.0);
	F.Motor->RouteQuery = [Wall, Island](const FVector& DestCm, float& OutLengthCm)
	{
		if (DestCm.Equals(Island, 1.0))
		{
			return false;
		}
		// Just past twice the order (2 x 200 units) for the wall, the straight line otherwise.
		OutLengthCm = DestCm.Equals(Wall, 1.0) ? static_cast<float>(401.0 * U) : static_cast<float>(DestCm.Size());
		return true;
	};
	for (int32 Round = 0; Round < 12; ++Round)
	{
		F.Run(200.0f);
		TestTrue(TEXT("the pick completes"), F.Completed());
		TestEqual(TEXT("...only on the place with a fair route"), F.Picked(), 2);
		TestTrue(TEXT("...within the draw budget"), F.Guard->StartTaskNav.LastRandomGoalDraws <= 3);
	}
	// Every route a detour: the band empties inside the budget and the task fails.
	F.Motor->RouteQuery = [](const FVector&, float& OutLengthCm) { OutLengthCm = 1.0e6f; return true; };
	F.Run(200.0f);
	TestEqual(TEXT("every route too long: 0x18"), F.Reason(), FailNoReachableNode);
	TestEqual(TEXT("...after one draw per place"), F.Guard->StartTaskNav.LastRandomGoalDraws, 3);
	// A band emptied by unroutable picks falls to the next one, as an empty band does.
	FWanderFixture Fall([](FElysiumNpcWorldBuilder& B)
	{
		B.AddPlace(2, Cm(150.0, 0.0, 0.0));    // 0: ahead, outer half -- unroutable
		B.AddPlace(2, Cm(-40.0, 0.0, 0.0));    // 1: behind and near -- the last band
	});
	if (Fall.Ready(*this))
	{
		const FVector Ahead = Cm(150.0, 0.0, 0.0);
		Fall.Motor->RouteQuery = [Ahead](const FVector& DestCm, float& OutLengthCm)
		{
			OutLengthCm = static_cast<float>(DestCm.Size());
			return !DestCm.Equals(Ahead, 1.0);
		};
		Fall.Run(200.0f);
		TestTrue(TEXT("the preferred band dropped: the pick still completes"), Fall.Completed());
		TestEqual(TEXT("...on the next band's place"), Fall.Picked(), 1);
		TestEqual(TEXT("...after two draws"), Fall.Guard->StartTaskNav.LastRandomGoalDraws, 2);
	}
	// The budget: five draws in all, however many places stand.
	FWanderFixture Many([](FElysiumNpcWorldBuilder& B)
	{
		for (int32 Index = 0; Index < 8; ++Index)
		{
			B.AddPlace(2, Cm(150.0, -40.0 + 10.0 * Index, 0.0));
		}
	});
	if (Many.Ready(*this))
	{
		Many.Motor->RouteQuery = [](const FVector&, float&) { return false; };
		Many.Run(200.0f);
		TestEqual(TEXT("no route anywhere: 0x18"), Many.Reason(), FailNoReachableNode);
		TestEqual(TEXT("...after kWanderMaxDraws (5) draws"), Many.Guard->StartTaskNav.LastRandomGoalDraws, 5);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// The pick on the baked hub.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlacesWanderHubTest, "Elysium.Content.Places.WanderHub",
	GElysiumPlaceSeamFlags)
bool FElysiumPlacesWanderHubTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSeamTests;
	const TCHAR* Map = TEXT("sm_hub_1");
	const UElysiumMapPlaces* Asset = LoadObject<UElysiumMapPlaces>(nullptr,
		*FElysiumContentPaths::BakedMapPlaces(Map), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!TestNotNull(TEXT("the hub's places asset"), Asset))
	{
		return false;
	}
	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("the hub's entity table loads"), ElysiumEntityDefSource::Load(Map, Defs) != EElysiumEntityDefSource::None))
	{
		return false;
	}
	const FElysiumEntityDef* Pedestrian = Defs.Defs.FindByPredicate(
		[](const FElysiumEntityDef& Def) { return Def.Classname == TEXT("npc_VPedestrian"); });
	if (!TestNotNull(TEXT("a pedestrian's spawn"), Pedestrian))
	{
		return false;
	}
	const FVector SpawnCm = Pedestrian->Origin;
	const FString* Angles = Pedestrian->Keys.Find(TEXT("angles"));
	const FString AnglesText = Angles != nullptr ? *Angles : FString(TEXT("0 0 0"));
	FWanderFixture F([Asset, SpawnCm, &AnglesText](FElysiumNpcWorldBuilder& B)
	{
		B.Places = Asset->Rows;
		B.WanderCaps = Asset->WanderCaps;
		FElysiumEntityDef* Guard = B.Find(TEXT("guard"));
		Guard->Origin = SpawnCm;
		Guard->Keys.Add(TEXT("angles"), AnglesText);
	});
	if (!F.Ready(*this))
	{
		return false;
	}
	F.Motor->RouteQuery = [SpawnCm](const FVector& DestCm, float& OutLengthCm)
	{
		OutLengthCm = static_cast<float>(FVector::Dist(SpawnCm, DestCm));
		return true;
	};
	F.Run(200.0f);
	TestTrue(*FString::Printf(TEXT("an order of 200 from the pedestrian at %s finds a place"), *SpawnCm.ToString()),
		F.Completed());
	FVector At = FVector::ZeroVector;
	if (F.World.World.Places().GetPositionCm(F.Picked(), F.Guard->PathingHullKind, At))
	{
		TestTrue(TEXT("...within 200 units"), FVector::Dist(At, F.Guard->Origin) / U <= 200.0);
	}
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
