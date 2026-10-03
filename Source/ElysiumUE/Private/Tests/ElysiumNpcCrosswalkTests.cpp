#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapPlaces.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcFlags.h"
#include "ElysiumVariant.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcNavigator.h"
#include "Substrate/ElysiumPlaceSet.h"
#include "Substrate/ElysiumScriptedCharacter.h"
#include "Tests/ElysiumNpcTestFixture.h"

// 0018 story 7, the crosswalk lane: the pair state and the hint's signal write (`0x102f97c0`), the
// pedestrian splice (decision 2), the arrival wait (`0x102f0400` -> `0x102a0bc0` -> `0x102a0b90`),
// the per-think rule's two arms (`0x102a0d20`) and the selector's `0x102`, the fresh route on green,
// the queue arm (`0x10298340`), and the link's save words (Troika Save / Restore tails /
// `0x102998c0`). Every case stands a three-node network: curbs A (node 0) and B (node 1) a walkable
// pair across the road, C (node 2) far off, joined to A by a jump-only pair and to B by a walkable
// one, each node bound to an `info_node_crosswalk` hint.

static constexpr EAutomationTestFlags GXwTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	constexpr float GXwU = ElysiumMove::U;
	const FVector GXwCurbA(3000.0, 0.0, 0.0);
	const FVector GXwCurbB(3000.0, 600.0, 0.0);
	const FVector GXwCurbC(6000.0, 600.0, 0.0);
	// The interesting place across the road, beyond curb B.
	const FVector GXwGoal(3000.0, 1500.0, 0.0);
	constexpr int32 GXwPairAB = 0;
	constexpr int32 GXwPairAC = 1;   // jump-only
	constexpr int32 GXwPairBC = 2;
	constexpr int32 GXwSchedWalkToPlace = 0x100;
	constexpr int32 GXwSchedWaitAtCrosswalk = 0x102;

	// A body that reports only what a case scripts: the move facts, where it stands, and the route
	// `QueryRoute` answers (the corner points the splice reads). A new request is alive until the case
	// ends it.
	struct FXwMotor final : IElysiumNpcMotor
	{
		FElysiumNpcMoveFacts Facts;
		EElysiumNpcMoveStatus Status = EElysiumNpcMoveStatus::Idle;
		FVector Feet = FVector::ZeroVector;
		int32 StopCalls = 0;
		// Whether a travel request is live (MoveTo until Stop): the body translates only then.
		bool bRequestLive = false;
		// Every turn asked of the body (`Face`), the facing half `MotorUpdateYaw` keeps driving.
		int32 FaceCalls = 0;
		TArray<FElysiumNpcMoveRequest> MoveRequests;
		TArray<FVector> RoutePoints;
		mutable int32 RouteQueries = 0;
		mutable int32 LastRouteMultiplier = -1;

		virtual bool MoveTo(const FElysiumNpcMoveRequest& Request) override
		{
			MoveRequests.Add(Request);
			bRequestLive = true;
			Facts = FElysiumNpcMoveFacts();
			Facts.bRequestAlive = true;
			Facts.RemainingDistance2DCm = static_cast<float>(FVector::Dist2D(Feet, Request.DestinationCm));
			Status = EElysiumNpcMoveStatus::Moving;
			return true;
		}
		virtual bool QueryRoute(const FElysiumNpcRouteQuery& Query, FElysiumNpcRouteAnswer& Out) const override
		{
			++RouteQueries;
			LastRouteMultiplier = Query.PedestrianCostMultiplier;
			Out.bReachable = RoutePoints.Num() > 0;
			Out.PointsCm = RoutePoints;
			Out.LengthCm = 0.f;
			for (int32 Index = 0; Index + 1 < RoutePoints.Num(); ++Index)
			{
				Out.LengthCm += static_cast<float>(FVector::Dist(RoutePoints[Index], RoutePoints[Index + 1]));
			}
			return true;
		}
		virtual void Face(float, float) override { ++FaceCalls; }
		virtual void Stop() override
		{
			++StopCalls;
			bRequestLive = false;
		}
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
			Out = Facts;
			return true;
		}

		// The follower ended the request with `Code`, naming `Blocker`, `RemainingUnits` short.
		void Ended(EElysiumNpcMoveResultCode Code, const FElysiumEntityHandle& Blocker, float RemainingUnits)
		{
			Facts = FElysiumNpcMoveFacts();
			Facts.bRequestEnded = true;
			Facts.ResultCode = Code;
			Facts.BlockingEntity = Blocker;
			Facts.RemainingDistance2DCm = RemainingUnits * GXwU;
			Status = Code == EElysiumNpcMoveResultCode::Success
				? EElysiumNpcMoveStatus::Reached : EElysiumNpcMoveStatus::Failed;
		}
		FVector LastDest() const
		{
			return MoveRequests.Num() > 0 ? MoveRequests.Last().DestinationCm : FVector(-1.0);
		}
	};

	// `Motor` is `FElysiumScriptedCharacter`'s protected word; a case hands its NPC the scripted body
	// through a pointer to that member.
	struct FXwMotorSlot : FElysiumScriptedCharacter
	{
		static IElysiumNpcMotor* FElysiumScriptedCharacter::* Member() { return &FXwMotorSlot::Motor; }
	};

	// `CurrentSpotIndex` (`+0x62ec`, the claimed interesting place) is `FElysiumNpc`'s protected word.
	struct FXwSpotSlot : FElysiumNpc
	{
		static int32 FElysiumNpc::* Member() { return &FXwSpotSlot::CurrentSpotIndex; }
	};

	FElysiumNpcWorldBuilder XwWorld(const TCHAR* Map, uint32 Seed)
	{
		FElysiumNpcWorldBuilder Builder(Map, Seed);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		Builder.AddPlace(2, GXwCurbA);
		Builder.AddPlace(2, GXwCurbB);
		Builder.AddPlace(2, GXwCurbC);
		Builder.AddCrosswalkPair(0, 1, 1);
		Builder.AddCrosswalkPair(0, 2, 2);
		Builder.AddCrosswalkPair(1, 2, 1);
		// The node rows, in node order: `CNodeEnt::Spawn`'s counter binds them to nodes 0, 1, 2.
		Builder.AddEntity(TEXT("info_node_crosswalk"), TEXT("cw_a"), GXwCurbA);
		Builder.AddEntity(TEXT("info_node_crosswalk"), TEXT("cw_b"), GXwCurbB);
		Builder.AddEntity(TEXT("info_node_crosswalk"), TEXT("cw_c"), GXwCurbC);
		// The interesting place across the road (`intersting_place`, retail's own spelling).
		Builder.AddEntity(TEXT("intersting_place"), TEXT("place"), GXwGoal);
		Builder.AddNpc(TEXT("walker"), FVector::ZeroVector);
		Builder.AddNpc(TEXT("queue"), FVector(-500.0, 0.0, 0.0));
		return Builder;
	}

	// Two NPCs on scripted bodies (declared before the world, so they outlive it).
	struct FXwRig
	{
		FXwMotor WalkerMotor;
		FXwMotor QueueMotor;
		FElysiumNpcWorldFixture F;
		FElysiumNpc* Walker = nullptr;
		FElysiumNpc* Queue = nullptr;

		FXwRig(const TCHAR* Map, uint32 Seed)
			: F(XwWorld(Map, Seed))
		{
			Walker = F.Npc(TEXT("walker"));
			Queue = F.Npc(TEXT("queue"));
			FElysiumNpcWorldFixture::Quiet({ Walker, Queue });
			FElysiumNpcWorldFixture::PrepareForKernelDrive(Walker);
			FElysiumNpcWorldFixture::PrepareForKernelDrive(Queue);
			if (Walker != nullptr)
			{
				Walker->*FXwMotorSlot::Member() = &WalkerMotor;
			}
			if (Queue != nullptr)
			{
				Queue->*FXwMotorSlot::Member() = &QueueMotor;
			}
		}
		~FXwRig()
		{
			if (Walker != nullptr)
			{
				Walker->*FXwMotorSlot::Member() = nullptr;
			}
			if (Queue != nullptr)
			{
				Queue->*FXwMotorSlot::Member() = nullptr;
			}
		}
		bool Ready() const { return Walker != nullptr && Queue != nullptr; }
		FElysiumPlaceSet& Places() { return F.World.Places(); }
		int32 PlaceIndex()
		{
			const FElysiumEntity* Place = F.World.FindByName(TEXT("place"));
			return Place != nullptr ? Place->Handle.Index : INDEX_NONE;
		}

		void Fire(const TCHAR* Hint, const TCHAR* Input)
		{
			F.World.AcceptInput(FString(Hint), FName(Input), FElysiumVariant(), FElysiumEntityHandle::Invalid(),
				FElysiumEntityHandle::Invalid());
		}

		// A body standing at `At` (the entity record and the body agree).
		static void Stand(FElysiumNpc& Npc, FXwMotor& Motor, const FVector& At)
		{
			Motor.Feet = At;
			Npc.Origin = At;
		}

		// `SetGoal` with a location goal of `Type` at `GoalCm`, the route the body will answer given.
		static bool Goal(FElysiumNpc& Npc, FXwMotor& Motor, int32 Type, const FVector& GoalCm,
			TArray<FVector> Route)
		{
			Motor.RoutePoints = MoveTemp(Route);
			FElysiumNpcBase::FStartTaskNavGoal NavGoal;
			NavGoal.Type = Type;
			NavGoal.DestCm = GoalCm;
			NavGoal.bDestSet = true;
			return Npc.StartTaskSetGoal(NavGoal, 1);
		}

		// One `Move` on the facts the body now reports.
		static void Step(FElysiumNpc& Npc)
		{
			Npc.BaseScheduleHost.bShouldMove = true;
			Npc.BaseScheduleHost.MoveWaitFinished = 0.0;
			Npc.NavigatorMoveStep();
		}
	};
}

// --- The signal: `Walk` / `DontWalk` -> `0x102f97c0` ----------------------------------------------

#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCrosswalkSignalTest,
	"Elysium.Arm.NpcCrosswalk.Signal", GXwTestFlags)
bool FElysiumNpcCrosswalkSignalTest::RunTest(const FString&)
{
	FXwRig R(TEXT("xw_signal"), 0x7c0a1);
	if (!TestTrue(TEXT("rig"), R.Ready()))
	{
		return false;
	}
	FElysiumPlaceSet& Places = R.Places();
	TestEqual(TEXT("three pairs"), Places.CrosswalkPairs().Num(), 3);
	TestEqual(TEXT("two a walking hull can take"), Places.NumWalkableCrosswalkPairs(), 2);
	TestFalse(TEXT("the A-C pair is jump-only (word 2)"), Places.IsCrosswalkPairWalkable(GXwPairAC));
	TestFalse(TEXT("every pair starts green"), Places.IsCrosswalkRed(GXwPairAB)
		|| Places.IsCrosswalkRed(GXwPairAC) || Places.IsCrosswalkRed(GXwPairBC));
	TestEqual(TEXT("0x102f96e0 finds a pair from either end"), Places.FindCrosswalkPair(1, 0), GXwPairAB);

	R.Fire(TEXT("cw_a"), TEXT("DontWalk"));
	TestTrue(TEXT("DontWalk at A's hint turns A-B red (| 0xf0)"), Places.IsCrosswalkRed(GXwPairAB));
	TestTrue(TEXT("...and A-C, jump-only or not: the write tests no motion word"),
		Places.IsCrosswalkRed(GXwPairAC));
	TestFalse(TEXT("...and leaves B-C, which does not hold A"), Places.IsCrosswalkRed(GXwPairBC));
	R.Fire(TEXT("cw_b"), TEXT("DontWalk"));
	TestTrue(TEXT("DontWalk at B's hint turns B-C red"), Places.IsCrosswalkRed(GXwPairBC));
	R.Fire(TEXT("cw_a"), TEXT("Walk"));
	TestFalse(TEXT("Walk at A's hint turns A-B green (& 0xffffff0f)"), Places.IsCrosswalkRed(GXwPairAB));
	TestFalse(TEXT("...and A-C"), Places.IsCrosswalkRed(GXwPairAC));
	TestTrue(TEXT("...and leaves B-C red"), Places.IsCrosswalkRed(GXwPairBC));
	Places.BeginMapSpawn();
	TestFalse(TEXT("a fresh map load (0x102f6690) starts every pair green"), Places.IsCrosswalkRed(GXwPairBC));
	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS

// --- Arrival on red, through the real think: `0x100` -> `0x102` -> `0x100` -------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCrosswalkArrivalTest,
	"Elysium.Substrate.NpcCrosswalk.ThinkWaitsAtRed", GXwTestFlags)
bool FElysiumNpcCrosswalkArrivalTest::RunTest(const FString&)
{
	FXwRig R(TEXT("xw_arrival"), 0x7c0a2);
	if (!TestTrue(TEXT("rig"), R.Ready()))
	{
		return false;
	}
	FElysiumNpc& W = *R.Walker;
	FXwMotor& M = R.WalkerMotor;
	const FVector Start(-2000.0, 0.0, 0.0);
	FXwRig::Stand(W, M, Start);
	R.Fire(TEXT("cw_a"), TEXT("DontWalk"));
	const int32 PlaceIndex = R.PlaceIndex();
	if (!TestTrue(TEXT("the interesting place across the road stands"), PlaceIndex != INDEX_NONE))
	{
		return false;
	}

	// The map has been up past the NPC's first second: `NPCInitThink` has run `StartNPC` and the
	// first ordinary thinks have run (`TroikaNPCInit` releases any interesting place, `0x102b53d0`),
	// so what the case states below is not undone by the NPC's own start-up.
	FElysiumNpcWorldFixture::Wake({ &W }, R.F.World.NowSeconds());
	R.F.Advance(R.F.World.NowSeconds() + 1.5);
	FXwRig::Stand(W, M, Start);
	M.RoutePoints = { Start, GXwCurbA, GXwCurbB, GXwGoal };

	// A `use_interesting` pedestrian at the tail of `0xff SCHED_TROIKA_WALK_TO_INTERESTING_PLACE_SETUP`
	// (`SET_FAIL_SCHEDULE; FIND_INTERESTING_PLACE; SET_PRESERVE_PATH 1; SET_SCHEDULE 0x100`), stated
	// word for word: `FIND_INTERESTING_PLACE` (`0x102a1f23`) claimed the place and wrote `+0x62ec` and
	// `m_vecInterestingPlace`, `SET_PRESERVE_PATH 1` set the flag, and `SET_SCHEDULE` (`0x10282e27`)
	// installs `0x100` through `CAI_BaseNPC::SetSchedule(int)` (`0x10280de0` -> `0x10280e50`), whose
	// slot-435 `0x102a0940` keeps the claim under `PRESERVE_PATH`. NOT the Troika forced
	// `SetSchedule` (`0x102ae780`): its `ForceScheduleChange` (`0x102ae490`) clears `PRESERVE_PATH` at
	// `0x102ae69b` first, so `0x102a0940` releases the place -- retail's own write, on a path `0xff`
	// never takes. From here every step is the kernel's own think.
	W.bUseInteresting = true;
	// `PickSpotFor(place, this, &m_vecInterestingPlace, 1)` (`0x102da0d0`): the claim the place holds.
	TestTrue(TEXT("the place takes the claim"),
		static_cast<FElysiumInterestingPlace*>(R.F.World.FindByName(TEXT("place")))->Claim(W.Handle));
	W.*FXwSpotSlot::Member() = PlaceIndex;
	W.InterestingPlacePosition = GXwGoal / GXwU;
	W.NpcFlags.Set(EElysiumNpcFlag::PRESERVE_PATH);
	W.ChangeSchedule(GXwSchedWalkToPlace);
	const int32 WalkToPlace = W.ResolveScheduleId(GXwSchedWalkToPlace);
	const int32 WaitAtCrosswalk = W.ResolveScheduleId(GXwSchedWaitAtCrosswalk);
	if (!TestEqual(TEXT("0x100 installed"), W.Schedule.Current, WalkToPlace)
		|| !TestEqual(TEXT("...with the place still claimed (+0x62ec)"), W.*FXwSpotSlot::Member(), PlaceIndex))
	{
		return false;
	}
	double Now = R.F.World.NowSeconds();

	// Think: `GET_PATH_TO_INTERESTING_PLACE` sets the type-8 goal; the splice lays [A, B, goal].
	R.F.Advance(Now + 0.3);
	TestEqual(TEXT("the claim held through the think (+0x62ec, what GET_PATH_TO_INTERESTING_PLACE reads)"),
		W.*FXwSpotSlot::Member(), PlaceIndex);
	if (!TestEqual(TEXT("GET_PATH_TO_INTERESTING_PLACE laid legs [A, B, goal]"), W.PedestrianLegs.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("leg 0 is curb A's node, flagged 4 | 0x20"), W.PedestrianLegs[0].NodeId * 0x100 + W.PedestrianLegs[0].Flags, 0x24);
	TestEqual(TEXT("leg 1 is curb B's node, flagged 4 | 0x20"), W.PedestrianLegs[1].NodeId * 0x100 + W.PedestrianLegs[1].Flags, 0x124);
	TestEqual(TEXT("the goal leg is no node, flagged 8"), W.PedestrianLegs[2].Flags, 0x8);
	TestEqual(TEXT("the body walks to curb A"), M.LastDest(), GXwCurbA);
	TestEqual(TEXT("the route was priced with the leg's own multiplier"), M.LastRouteMultiplier,
		M.MoveRequests.Last().PedestrianCostMultiplier);

	// The body reaches curb A: `0x102f0400` runs the wait test (red), then pops.
	const int32 StopsBeforeArrival = M.StopCalls;
	const int32 FacesBeforeArrival = M.FaceCalls;
	FXwRig::Stand(W, M, GXwCurbA);
	M.Ended(EElysiumNpcMoveResultCode::Success, FElysiumEntityHandle::Invalid(), 0.f);
	R.F.Advance(R.F.World.NowSeconds() + 0.15);
	TestTrue(TEXT("0x102a0b90: AT_CROSSWALK"), W.NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));
	TestEqual(TEXT("...and +0x630c the A-B link"), W.PedestrianPair, GXwPairAB);
	TestEqual(TEXT("the pop stored curb A's node at path+0x44"), W.Navigator.LastNodePassed, 0);

	// The next thinks: `0x102a0d20` raises DONTWALK, `0x100`'s interrupt reselects, the selector
	// answers `0x102`: PAUSE_MOVING pauses the path (the body parks), FACE_NEXT_NODE faces the leg
	// after the far curb, WAIT_INDEFINITE holds.
	// (The pause may land on the think right after the arrival, inside the step above: the move step
	// reads `path+0x10` on every call, so the park is asserted against the counts from before the
	// arrival, not from here.)
	R.F.Advance(R.F.World.NowSeconds() + 0.5);
	TestEqual(TEXT("schedule 0x102 SCHED_TROIKA_WAIT_AT_CROSSWALK runs"), W.Schedule.Current, WaitAtCrosswalk);
	TestTrue(TEXT("PAUSE_MOVING paused the path (0x102ee2a0)"), W.NavigatorIsPaused());
	TestTrue(TEXT("...no translation: the body's request was stopped (the park at 0x102effab)"),
		W.bNavBodyParked && !M.bRequestLive && M.StopCalls > StopsBeforeArrival);
	TestTrue(TEXT("...facing still runs: a turn was asked of the body after the arrival"),
		M.FaceCalls > FacesBeforeArrival);
	TestTrue(TEXT("the pedestrian still waits"), W.NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));
	const double Stamp = W.NextCrosswalkUpdateTime;
	TestTrue(TEXT("the 1 s re-check is armed"), Stamp > 0.0);
	const int32 RequestsWhilePaused = M.MoveRequests.Num();
	R.F.Advance(R.F.World.NowSeconds() + 2.0);
	TestEqual(TEXT("still red two seconds on: still 0x102 (FACE_NEXT_NODE did not fail it)"), W.Schedule.Current,
		WaitAtCrosswalk);
	TestTrue(TEXT("...still waiting"), W.NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));
	TestTrue(TEXT("...and still not translating: no request issued, none live, while the path is paused"),
		M.MoveRequests.Num() == RequestsWhilePaused && !M.bRequestLive);

	// Green: the next re-check raises WALK and drops AT_CROSSWALK; `0x102`'s interrupt reselects
	// `0x100`, whose `SetGoal` builds a FRESH route from the curb -- spliced again.
	const int32 RequestsBefore = M.MoveRequests.Num();
	M.RoutePoints = { GXwCurbA, GXwCurbB, GXwGoal };
	R.Fire(TEXT("cw_a"), TEXT("Walk"));
	R.F.Advance(R.F.World.NowSeconds() + 1.5);
	TestFalse(TEXT("green: AT_CROSSWALK dropped"), W.NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));
	TestEqual(TEXT("...and 0x100 runs again"), W.Schedule.Current, WalkToPlace);
	TestFalse(TEXT("...its WAIT_FOR_MOVEMENT lifted the pause"), W.NavigatorIsPaused());
	bool bFreshLegToA = false;
	for (int32 Index = RequestsBefore; Index < M.MoveRequests.Num(); ++Index)
	{
		bFreshLegToA |= M.MoveRequests[Index].DestinationCm.Equals(GXwCurbA);
	}
	TestTrue(TEXT("the fresh route's first leg is curb A, where it stands"), bFreshLegToA);
	TestEqual(TEXT("...and the pedestrian walks on to curb B without a wait"), M.LastDest(), GXwCurbB);
	return true;
}

// --- Routes that lay one leg -----------------------------------------------------------------------

#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCrosswalkOneLegTest,
	"Elysium.Arm.NpcCrosswalk.OneLeg", GXwTestFlags)
bool FElysiumNpcCrosswalkOneLegTest::RunTest(const FString&)
{
	FXwRig R(TEXT("xw_one_leg"), 0x7c0a3);
	if (!TestTrue(TEXT("rig"), R.Ready()))
	{
		return false;
	}
	FElysiumNpc& W = *R.Walker;
	FXwMotor& M = R.WalkerMotor;
	R.Fire(TEXT("cw_a"), TEXT("DontWalk"));
	FXwRig::Stand(W, M, FVector::ZeroVector);

	// A type-4 goal is no pedestrian's: one leg, no route asked, never a wait.
	TestTrue(TEXT("a type-4 goal routes"), FXwRig::Goal(W, M, 4, GXwGoal, { FVector::ZeroVector, GXwCurbA, GXwCurbB, GXwGoal }));
	TestEqual(TEXT("no curb legs"), W.PedestrianLegs.Num(), 0);
	TestEqual(TEXT("no route asked"), M.RouteQueries, 0);
	TestEqual(TEXT("the one request is the goal"), M.LastDest(), GXwGoal);
	TestTrue(TEXT("...and the head is the goal"), W.Navigator.bHeadIsGoal);
	FXwRig::Stand(W, M, GXwGoal);
	M.Ended(EElysiumNpcMoveResultCode::Success, FElysiumEntityHandle::Invalid(), 0.f);
	FXwRig::Step(W);
	TestTrue(TEXT("it arrives"), W.Navigator.LastOutcome.Kind == EElysiumNpcNavOutcomeKind::Arrived);
	TestFalse(TEXT("...with no crosswalk flag"), W.NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));

	// A type-8 route that passes no curb: the goal alone (the named modernization: crossing away from
	// a crosswalk is priced, never waited at).
	FXwRig::Stand(W, M, FVector::ZeroVector);
	TestTrue(TEXT("a type-8 goal routes"), FXwRig::Goal(W, M, 8, FVector(0.0, 1500.0, 0.0),
		{ FVector::ZeroVector, FVector(0.0, 1500.0, 0.0) }));
	TestEqual(TEXT("the goal leg alone"), W.PedestrianLegs.Num(), 1);
	TestEqual(TEXT("...issued"), M.LastDest(), FVector(0.0, 1500.0, 0.0));
	TestTrue(TEXT("...and it is the goal"), W.Navigator.bHeadIsGoal);

	// A route passing curb A alone (B out of reach of it) has no pair to wait at.
	TestTrue(TEXT("a type-8 goal past one curb routes"), FXwRig::Goal(W, M, 8, FVector(3000.0, -1500.0, 0.0),
		{ FVector::ZeroVector, GXwCurbA, FVector(3000.0, -1500.0, 0.0) }));
	TestEqual(TEXT("one curb is no pair: the goal leg alone"), W.PedestrianLegs.Num(), 1);
	return true;
}

// --- `0x10298340`: the queue behind a waiter -------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCrosswalkQueueTest,
	"Elysium.Arm.NpcCrosswalk.QueueArm", GXwTestFlags)
bool FElysiumNpcCrosswalkQueueTest::RunTest(const FString&)
{
	FXwRig R(TEXT("xw_queue"), 0x7c0a4);
	if (!TestTrue(TEXT("rig"), R.Ready()))
	{
		return false;
	}
	FElysiumNpc& W = *R.Walker;
	FElysiumNpc& Q = *R.Queue;
	FXwMotor& QM = R.QueueMotor;
	R.Fire(TEXT("cw_a"), TEXT("DontWalk"));
	// The waiter at curb A.
	FXwRig::Stand(W, R.WalkerMotor, GXwCurbA);
	W.SetAtCrosswalkLink(GXwPairAB);

	// A second pedestrian, 20 units short of curb A, blocked by the waiter.
	const FVector Near = GXwCurbA - FVector(20.0 * GXwU, 0.0, 0.0);
	FXwRig::Stand(Q, QM, Near);
	if (!TestTrue(TEXT("the queued walker's route builds"), FXwRig::Goal(Q, QM, 8, GXwGoal, { Near, GXwCurbA, GXwCurbB, GXwGoal }))
		|| !TestEqual(TEXT("...with legs [A, B, goal]"), Q.PedestrianLegs.Num(), 3))
	{
		return false;
	}
	const int32 Requests = QM.MoveRequests.Num();
	const int32 Stops = QM.StopCalls;
	QM.Ended(EElysiumNpcMoveResultCode::Blocked, W.Handle, 20.f);
	FXwRig::Step(Q);
	// Red: arm (ii) swallows the step (`*result = 0`); the port parks the body for it.
	TestFalse(TEXT("red: nothing failed"), Q.Navigator.bNavFailed);
	TestTrue(TEXT("...no NPC-blocker outcome"), Q.Navigator.LastOutcome.Kind != EElysiumNpcNavOutcomeKind::NpcBlocked
		&& Q.Navigator.LastOutcome.Kind != EElysiumNpcNavOutcomeKind::Failed);
	TestFalse(TEXT("...no 0.25 s hold"), Q.Navigator.bBlockerHoldStanding);
	TestEqual(TEXT("...no new request"), QM.MoveRequests.Num(), Requests);
	TestTrue(TEXT("...the body parked for the swallowed step"), Q.bNavBodyParked && QM.StopCalls == Stops + 1);
	TestEqual(TEXT("...the head is still curb A"), Q.PedestrianLegs[0].NodeId, 0);
	TestTrue(TEXT("0x102a0bc0's side effect: the queued walker is AT_CROSSWALK"),
		Q.NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));
	TestEqual(TEXT("...on the same link"), Q.PedestrianPair, GXwPairAB);

	// The next pass (no pause landed): the parked leg is issued again, and the body walks into the
	// waiter to be re-probed.
	FXwRig::Step(Q);
	TestFalse(TEXT("the next pass unparks"), Q.bNavBodyParked);
	TestEqual(TEXT("...re-issuing the head leg to curb A"), QM.LastDest(), GXwCurbA);

	// Green while the waiter still stands (its own re-check pending): arm (iii) within 32 units.
	R.Fire(TEXT("cw_a"), TEXT("Walk"));
	Q.NpcFlags.Clear(EElysiumNpcFlag::AT_CROSSWALK);
	Q.PedestrianPair = INDEX_NONE;
	QM.Ended(EElysiumNpcMoveResultCode::Blocked, W.Handle, 20.f);
	FXwRig::Step(Q);
	TestEqual(TEXT("green within 32 units: AdvancePath, the next request is curb B"), QM.LastDest(), GXwCurbB);
	TestEqual(TEXT("...curb A was popped"), Q.Navigator.LastNodePassed, 0);
	TestFalse(TEXT("...and nothing latched"), Q.NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));

	// 40 units short: past the 1024.0 square, the base arm (the door step) has it.
	const FVector Far = GXwCurbA - FVector(40.0 * GXwU, 0.0, 0.0);
	FXwRig::Stand(Q, QM, Far);
	FXwRig::Goal(Q, QM, 8, GXwGoal, { Far, GXwCurbA, GXwCurbB, GXwGoal });
	QM.Ended(EElysiumNpcMoveResultCode::Blocked, W.Handle, 40.f);
	FXwRig::Step(Q);
	TestEqual(TEXT("40 units: no advance, the head is still curb A"), Q.PedestrianLegs.Num() > 0
		? Q.PedestrianLegs[0].NodeId : -2, 0);
	TestTrue(TEXT("...and no request to curb B"), QM.LastDest() != GXwCurbB);

	// A blocker that is not waiting: arm (i) refuses whatever the light says.
	R.Fire(TEXT("cw_a"), TEXT("DontWalk"));
	W.NpcFlags.Clear(EElysiumNpcFlag::AT_CROSSWALK);
	FXwRig::Stand(Q, QM, Near);
	FXwRig::Goal(Q, QM, 8, GXwGoal, { Near, GXwCurbA, GXwCurbB, GXwGoal });
	QM.Ended(EElysiumNpcMoveResultCode::Blocked, W.Handle, 20.f);
	FXwRig::Step(Q);
	TestFalse(TEXT("a blocker not AT_CROSSWALK: no queue, no latch"), Q.NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));
	return true;
}

// --- The link's save words, through the real persistence path -----------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCrosswalkSaveTest,
	"Elysium.Arm.NpcCrosswalk.SaveRestore", GXwTestFlags)
bool FElysiumNpcCrosswalkSaveTest::RunTest(const FString&)
{
	FXwRig From(TEXT("xw_save"), 0x7c0a5);
	FXwRig To(TEXT("xw_save"), 0x7c0a5);
	if (!TestTrue(TEXT("rigs"), From.Ready() && To.Ready()))
	{
		return false;
	}
	From.Walker->SetAtCrosswalkLink(GXwPairAB);
	From.Places().SetCrosswalkWalk(0, false);
	TestEqual(TEXT("the destination walker starts unlinked"), To.Walker->PedestrianPair, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("...with the ctor's -1 restore ids (0x1028d230)"), To.Walker->RestorePedLinkNode, static_cast<int32>(INDEX_NONE));

	// `Freeze` -> `ApplySnapshot`: `FElysiumNpc::Serialize` writes Troika Save's tail, the load reads
	// Troika Restore's pair, and `OnPostRestore` -> slot 130 `0x102998c0` re-finds the link.
	ElysiumRoundTripSnapshot(From.F.World, To.F.World);
	TestEqual(TEXT("+0x6310 read link+4, the pair's first node"), To.Walker->RestorePedLinkNode, 0);
	TestEqual(TEXT("+0x6314 read link+8, the pair's second node"), To.Walker->RestorePedLinkDestNode, 1);
	TestEqual(TEXT("0x102998c0 re-found the link through 0x102f96e0"), To.Walker->PedestrianPair, GXwPairAB);
	TestFalse(TEXT("the signal is not saved: the restored link reads green, so the waiter is released"),
		To.Places().IsCrosswalkRed(GXwPairAB));
	TestEqual(TEXT("an unlinked NPC wrote the byte alone: its ids stay -1"), To.Queue->RestorePedLinkNode,
		static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("...and it re-finds nothing"), To.Queue->PedestrianPair, static_cast<int32>(INDEX_NONE));

	// A first id outside the network counts `DAT_106c994c` and leaves +0x630c alone.
	FElysiumNpc& Q = *To.Queue;
	Q.RestorePedLinkNode = 99;
	Q.RestorePedLinkDestNode = 1;
	const int32 Misses = FElysiumNpc::NodeIndexErrorCount();
	Q.RestorePedestrianLink();
	TestEqual(TEXT("an id past the network is one miss"), FElysiumNpc::NodeIndexErrorCount(), Misses + 1);
	TestEqual(TEXT("...and re-finds nothing"), Q.PedestrianPair, static_cast<int32>(INDEX_NONE));
	Q.RestorePedLinkNode = -1;
	Q.RestorePedestrianLink();
	TestEqual(TEXT("an unset id skips the re-find and the count"), FElysiumNpc::NodeIndexErrorCount(), Misses + 1);
	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS

// --- The hub's pairs, as baked ---------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlacesHubCrosswalksTest, "Elysium.Content.Places.HubCrosswalks",
	GXwTestFlags)
bool FElysiumPlacesHubCrosswalksTest::RunTest(const FString&)
{
	const UElysiumMapPlaces* Asset = LoadObject<UElysiumMapPlaces>(nullptr,
		*FElysiumContentPaths::BakedMapPlaces(TEXT("sm_hub_1")), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!TestNotNull(TEXT("sm_hub_1's place set is baked"), Asset))
	{
		return false;
	}
	FElysiumPlaceSet Places;
	Places.Adopt(*Asset);
	// Nodes 258-263 joined by 8 pairs; 260-263 (NW corner) and 259-261 (south curb) carry hull-0
	// word 2, jump-only (findings R-C), so a pedestrian walks six.
	TestEqual(TEXT("8 crosswalk pairs"), Places.CrosswalkPairs().Num(), 8);
	TestEqual(TEXT("each with its hull-0 word"), Asset->CrosswalkPairMotions.Num(), 8);
	TestEqual(TEXT("6 walkable"), Places.NumWalkableCrosswalkPairs(), 6);
	TestFalse(TEXT("260-263 is jump-only"), Places.IsCrosswalkPairWalkable(Places.FindCrosswalkPair(260, 263)));
	TestFalse(TEXT("259-261 is jump-only"), Places.IsCrosswalkPairWalkable(Places.FindCrosswalkPair(259, 261)));
	TestTrue(TEXT("258-259 across the road is walkable"), Places.IsCrosswalkPairWalkable(Places.FindCrosswalkPair(258, 259)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
