#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcNavigator.h"
#include "Substrate/ElysiumScriptedCharacter.h"
#include "Tests/ElysiumNpcTestFixture.h"

// 0018/7 -- the door transaction end to end on the substrate: the door's NPC-failure words
// (`+0x640` / `+0x644`), `GetNPCOpenData`, the move-step sink `0x1027dc10` -> slot 531, the
// look-ahead `0x102f06e0`, `OnDoorBlocked 0x1027de00`'s 5 / 20 s and its link mark, the link
// predicate `0x102fce80` / `0x102ff960`, `DoorHitTop 0x100f0860` -> `OnDoorFullyOpen 0x1027dd10`,
// the lock test's arm 1 and `IsCloseBlocked 0x100f0c00`.
//
// The door is a SYNTHETIC `func_door_rotating`: `frontgate`, the tutorial's gate, is never opened by
// an NPC in retail (findings § 2 -- a relay opens it and the sheriff walks through), so the
// open-and-pass case stands its own door. The refusal the live check watches is the hub's
// `basic_smoke_door` (LOCKED, link 958); here it is the same flag on the synthetic door.

static constexpr EAutomationTestFlags GNpcDoorLinkTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The friend `FElysiumDoorBase` names for this suite: a Substrate-tier door has no brush body, so
// its move never reaches `MoveDone`; the suite runs the arrival itself.
struct FElysiumNpcDoorLinkTestAccess
{
	static void Arrive(FElysiumDoorBase& Door) { Door.MoveDone(); }
};

namespace ElysiumNpcDoorLinkTestsDetail
{
	constexpr float U = ElysiumMove::U;
	const FVector GuardFeetCm(50.0, -200.0, 0.0);

	// A leaf from the hinge along local +X, 100 x 10 x 200 cm: its far edge (100 cm / 39.37 units,
	// against a 1.97-unit half-thickness inside `0x100f3ea0`'s 4.0 tie band) puts
	// `m_fDoorAngleAtRest` at 0, so the two open directions are Source +Y and -Y.
	FElysiumConvexHull LeafHull()
	{
		FElysiumConvexHull Hull;
		for (float X : { 0.0f, 100.0f })
		{
			for (float Y : { -5.0f, 5.0f })
			{
				for (float Z : { -100.0f, 100.0f })
				{
					Hull.Vertices.Emplace(X, Y, Z);
				}
			}
		}
		return Hull;
	}

	// A motor that reports what a case scripts: the move facts, the legacy status and the feet.
	struct FDoorLinkMotor final : IElysiumNpcMotor
	{
		FElysiumNpcMoveFacts Facts;
		EElysiumNpcMoveStatus Status = EElysiumNpcMoveStatus::Moving;
		FVector Feet = FVector::ZeroVector;
		// Whether the stand point is reachable (`QueryRoute`, slot 531 arm 7's local route).
		bool bRoutes = false;
		FElysiumNpcMoveRequest LastRequest;
		// A new request is a new leg in flight from where the body stands.
		virtual bool MoveTo(const FElysiumNpcMoveRequest& Request) override
		{
			LastRequest = Request;
			Facts = FElysiumNpcMoveFacts();
			Facts.bRequestAlive = true;
			Facts.RemainingDistance2DCm = static_cast<float>(FVector::Dist2D(Feet, Request.DestinationCm));
			Status = EElysiumNpcMoveStatus::Moving;
			return true;
		}
		virtual bool QueryRoute(const FElysiumNpcRouteQuery& Query, FElysiumNpcRouteAnswer& Out) const override
		{
			Out.bReachable = bRoutes;
			Out.LengthCm = bRoutes ? static_cast<float>(FVector::Dist(Feet, Query.DestCm)) : 0.f;
			return true;
		}
		virtual void Face(float, float) override {}
		virtual void Stop() override {}
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

		// A request in flight, held at the door link of `Door` (unset: walking free).
		void HeldAt(const FElysiumEntityHandle& Door, float RemainingUnits)
		{
			Facts = FElysiumNpcMoveFacts();
			Facts.bRequestAlive = true;
			Facts.RemainingDistance2DCm = RemainingUnits * U;
			Facts.DoorLinkEntity = Door;
			Facts.DoorLinkPointCm = Feet;
			Status = EElysiumNpcMoveStatus::Moving;
		}
		// The leg's end reached (the waypoint arrival test), nothing in the way.
		void Arrived()
		{
			Facts = FElysiumNpcMoveFacts();
			Facts.bRequestAlive = true;
			Status = EElysiumNpcMoveStatus::Moving;
		}
		// Walking free with `Door`'s link ahead, its far end `DistanceCm` away.
		void Approaching(const FElysiumEntityHandle& Door, const FVector& FarEndCm, float RemainingUnits)
		{
			Facts = FElysiumNpcMoveFacts();
			Facts.bRequestAlive = true;
			Facts.RemainingDistance2DCm = RemainingUnits * U;
			Facts.UpcomingDoorLinkEntity = Door;
			Facts.UpcomingDoorLinkEndCm = FarEndCm;
			Facts.UpcomingDoorLinkDistanceCm = static_cast<float>(FVector::Dist(Feet, FarEndCm));
			Status = EElysiumNpcMoveStatus::Moving;
		}
	};

	// Two leg shapes for a route whose head is not the goal (`0x102f13d0` and S1 read the head).
	FElysiumNpc::FPedestrianLeg MidLeg(const FVector& DestCm)
	{
		FElysiumNpc::FPedestrianLeg Leg;
		Leg.DestCm = DestCm;
		return Leg;
	}
	FElysiumNpc::FPedestrianLeg GoalLeg(const FVector& DestCm)
	{
		FElysiumNpc::FPedestrianLeg Leg;
		Leg.DestCm = DestCm;
		Leg.Flags = 0x8;
		return Leg;
	}

	// `Motor` is `FElysiumScriptedCharacter`'s protected word, reached through a member pointer.
	struct FDoorLinkMotorSlot : FElysiumScriptedCharacter
	{
		static IElysiumNpcMotor* FElysiumScriptedCharacter::* Member() { return &FDoorLinkMotorSlot::Motor; }
	};

	FElysiumNpcWorldBuilder DoorWorld(const TCHAR* Map, uint32 Seed, int32 DoorSpawnFlags, const TCHAR* Wait)
	{
		FElysiumNpcWorldBuilder Builder(Map, Seed);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		Builder.AddNpc(TEXT("guard"), GuardFeetCm);
		FElysiumEntityDef& Door = Builder.AddEntity(TEXT("func_door_rotating"), TEXT("door"));
		Door.Hulls.Add(LeafHull());
		Door.Keys.Add(TEXT("distance"), TEXT("90"));
		Door.Keys.Add(TEXT("speed"), TEXT("90"));
		Door.Keys.Add(TEXT("wait"), Wait);
		Door.Keys.Add(TEXT("spawnflags"), FString::FromInt(DoorSpawnFlags));
		return Builder;
	}

	// A guard walking a type-4 goal 300 units off, the scripted motor in its body, and the door.
	struct FDoorLinkRig
	{
		FDoorLinkMotor Scripted;
		FElysiumNpcWorldFixture F;
		FElysiumNpc* Guard = nullptr;
		FElysiumDoorBase* Door = nullptr;

		FDoorLinkRig(const TCHAR* Map, uint32 Seed, int32 DoorSpawnFlags = 0, const TCHAR* Wait = TEXT("-1"))
			: F(DoorWorld(Map, Seed, DoorSpawnFlags, Wait))
		{
			Guard = F.Npc(TEXT("guard"));
			FElysiumEntity* DoorEntity = F.World.FindByName(TEXT("door"));
			Door = DoorEntity != nullptr ? DoorEntity->AsDoorBase() : nullptr;
			FElysiumNpcWorldFixture::Quiet({ Guard });
			FElysiumNpcWorldFixture::PrepareForKernelDrive(Guard);
			if (Guard == nullptr)
			{
				return;
			}
			Guard->*FDoorLinkMotorSlot::Member() = &Scripted;
			Guard->CapabilityWord |= 0xd00;   // `bits_CAP_*_OPEN_DOORS`, the mask slot 531 reads
			Guard->BaseScheduleHost.bShouldMove = true;
			Guard->BaseScheduleHost.MoveWaitFinished = 0.0;
			FElysiumNpcNavigator& Nav = Guard->Navigator;
			Nav.GoalType = 4;
			Nav.bHasHeadWaypoint = true;
			Nav.bHeadIsGoal = true;
			Nav.GoalPosCm = FVector(300.0 * U, 0.0, 0.0);
			Nav.GoalToleranceCm = 10.f * U;
			Nav.NavType = 0;
			Nav.bPaused = false;
			Scripted.Feet = GuardFeetCm;
			Guard->Origin = GuardFeetCm;
			FElysiumNpcMoveRequest Leg;
			Leg.DestinationCm = Nav.GoalPosCm;
			Leg.AcceptanceToleranceCm = 0.0625f * U;
			Leg.SpeedCmPerSecond = 150.f;
			Guard->NavIssueLeg(Leg);
		}
		~FDoorLinkRig()
		{
			if (Guard != nullptr)
			{
				Guard->*FDoorLinkMotorSlot::Member() = nullptr;
			}
		}
		bool Ready() const { return Guard != nullptr && Door != nullptr; }
		double Now() const { return F.World.NowSeconds(); }
	};
}

// --- GetNPCOpenData, the whole struct ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcDoorOpenDataTest,
	"Elysium.Substrate.NpcDoorLink.OpenData", GNpcDoorLinkTestFlags)
bool FElysiumNpcDoorOpenDataTest::RunTest(const FString&)
{
	using namespace ElysiumNpcDoorLinkTestsDetail;
	FDoorLinkRig R(TEXT("doorlink_opendata"), 70701);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;

	// The guard stands on Source +Y of the hinge (port -Y): `fwd·d >= 0`, so the open side is +Y.
	const FElysiumDoorNpcOpenData Closed = R.Door->GetNPCOpenData(R.Guard, /*bOpening*/ false);
	TestEqual(TEXT("0x100f2bd0 closed: ACT 0xc84"), Closed.Activity, 0xc84);
	// Stand = hinge + rest*24 + open*50, z - 54, Source units -> port (Y mirrored).
	TestTrue(TEXT("...stands 24 along the leaf and 50 out on the guard's side, 54 down"),
		Closed.StandPosCm.Equals(FVector(24.0 * U, -50.0 * U, -54.0 * U), 0.01));
	TestTrue(TEXT("...faces -open (Source -Y, port +Y)"), Closed.FaceDir.Equals(FVector(0.0, 1.0, 0.0), 1e-4));

	const FElysiumDoorNpcOpenData Opening = R.Door->GetNPCOpenData(R.Guard, /*bOpening*/ true);
	TestEqual(TEXT("opening: ACT_IDLE (1)"), Opening.Activity, 1);
	TestTrue(TEXT("...stands 100 out"), Opening.StandPosCm.Equals(FVector(24.0 * U, -100.0 * U, -54.0 * U), 0.01));

	// A null NPC answers -1; so does the base (sliding) body.
	TestEqual(TEXT("a null NPC answers -1"), R.Door->GetNPCOpenData(nullptr, false).Activity, INDEX_NONE);
	FElysiumNpcWorldBuilder Builder(TEXT("doorlink_opendata_slide"), 70702);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("guard"), GuardFeetCm);
	Builder.AddEntity(TEXT("func_door"), TEXT("slider")).Hulls.Add(LeafHull());
	FElysiumNpcWorldFixture Slide(MoveTemp(Builder));
	FElysiumEntity* Slider = Slide.World.FindByName(TEXT("slider"));
	FElysiumDoorBase* SliderDoor = Slider != nullptr ? Slider->AsDoorBase() : nullptr;
	if (TestNotNull(TEXT("the sliding door"), SliderDoor))
	{
		TestEqual(TEXT("0x100f0ef0 a sliding door answers -1"),
			SliderDoor->GetNPCOpenData(Slide.Npc(TEXT("guard")), false).Activity, INDEX_NONE);
	}
	return true;
}

// --- The move step: held at a closed door's link, slot 531 opens it, HitTop resumes -----------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcDoorOpenAndPassTest,
	"Elysium.Substrate.NpcDoorLink.OpenAndPass", GNpcDoorLinkTestFlags)
bool FElysiumNpcDoorOpenAndPassTest::RunTest(const FString&)
{
	using namespace ElysiumNpcDoorLinkTestsDetail;
	FDoorLinkRig R(TEXT("doorlink_open"), 70703);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	TestEqual(TEXT("the door stands closed"), static_cast<int32>(R.Door->State()),
		static_cast<int32>(FElysiumDoorBase::EToggleState::AtBottom));

	const FElysiumDoorNpcOpenData Open = R.Door->GetNPCOpenData(R.Guard, false);

	// 1. Held at the doorway: the step's blocker is the door (`goal+0x60 -> +0xa4`), S1 hands it to
	//    the sink, slot 531's arm 7 FINDS a route to `StandPos` (`1029864c`) and splices the door
	//    waypoint ahead of the head (`102986da`): `+0x5d24` kept, answers 0. Nothing opens yet.
	R.Scripted.bRoutes = true;
	R.Scripted.HeldAt(R.Door->Handle, 300.f);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("the door is still closed"), static_cast<int32>(R.Door->State()),
		static_cast<int32>(FElysiumDoorBase::EToggleState::AtBottom));
	TestTrue(TEXT("m_hOpeningDoor (+0x5d24) holds the door"), R.Guard->OpeningDoor == R.Door->Handle);
	TestFalse(TEXT("m_bOpeningDoorWait (+0x5d30): the door was closed"), R.Guard->bOpeningDoorWait);
	TestEqual(TEXT("no failure"), R.Guard->BaseScheduleHost.FailureReason, 0);
	if (TestEqual(TEXT("the splice: [stand point, goal]"), R.Guard->PedestrianLegs.Num(), 2))
	{
		TestEqual(TEXT("...the stand point is WP_TO_DOOR | DONT_SIMPLIFY"), R.Guard->PedestrianLegs[0].Flags, 0x30);
		TestTrue(TEXT("...carrying the door (wp+0x24)"), R.Guard->PedestrianLegs[0].Door == R.Door->Handle);
		TestTrue(TEXT("...at GetNPCOpenData's StandPos"), R.Guard->PedestrianLegs[0].DestCm.Equals(Open.StandPosCm, 0.01));
		TestEqual(TEXT("...then the goal"), R.Guard->PedestrianLegs[1].Flags, 0x8);
	}
	TestFalse(TEXT("the head is no longer the goal"), R.Guard->Navigator.CurWaypointIsGoal());
	TestTrue(TEXT("the body walks to the stand point"),
		R.Scripted.LastRequest.DestinationCm.Equals(Open.StandPosCm, 0.01));

	// 2. The stand point reached: `AdvancePath 0x102f0400`'s door arm -- `0x1027f550` passes and the
	//    door is closed (state 1) -> `0x10298800`: `m_bShouldMove = 0`, the path PAUSED, mode 1. The
	//    door waypoint pops and the leg to the goal is laid.
	R.Scripted.Arrived();
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("0x10298800: m_eAlternateAI = 1"), R.Guard->AlternateAi, 1);
	TestTrue(TEXT("...the path is paused"), R.Guard->Navigator.IsPaused());
	TestFalse(TEXT("...m_bShouldMove = 0"), R.Guard->BaseScheduleHost.bShouldMove);
	TestEqual(TEXT("...the door waypoint popped"), R.Guard->PedestrianLegs.Num(), 1);

	// 3. Mode 1 (`0x10290040`): face `FaceDir`; once facing, `0x10298840` -- activity, lock test,
	//    `Open` -- and mode 2 with its 5 s expiry.
	(void)R.Guard->RunAlternateAI(false);
	R.Guard->Angles.Y = R.Guard->MotorIdealYaw;                              // the turn completes
	const double Now = R.Now();
	(void)R.Guard->RunAlternateAI(false);
	TestEqual(TEXT("the transaction advanced: mode 2"), R.Guard->AlternateAi, 2);
	TestEqual(TEXT("...expiring curtime + 5.0"), R.Guard->AlternateAiExpireTime, Now + 5.0, 1e-6);
	TestEqual(TEXT("0x10298840 opened the door: GOING_UP"), static_cast<int32>(R.Door->State()),
		static_cast<int32>(FElysiumDoorBase::EToggleState::GoingUp));
	TestTrue(TEXT("...the guard is the activator"), R.Door->LastActivator == R.Guard->Handle);
	TestEqual(TEXT("...ACT 0xc84 restarted (GetNPCOpenData's Activity)"), R.Guard->IdealActivityNumber, 0xc84);

	// 4. HitTop: `0x100f0860` -> the activator's `OnDoorFullyOpen 0x1027dd10` (slot 532(1) for the
	//    door it holds, the un-pause, `m_bShouldMove = 1`, slot 528, mode 2 -> 0), then `+0x640 = 0`.
	R.Door->NpcFailedTimer = R.Now() + 99.0;
	FElysiumNpcDoorLinkTestAccess::Arrive(*R.Door);
	TestEqual(TEXT("AT_TOP"), static_cast<int32>(R.Door->State()),
		static_cast<int32>(FElysiumDoorBase::EToggleState::AtTop));
	TestEqual(TEXT("DoorHitTop zeroes +0x640"), R.Door->NpcFailedTimer, 0.0);
	TestFalse(TEXT("0x1027dd10 un-pauses the navigator"), R.Guard->Navigator.IsPaused());
	TestTrue(TEXT("...m_bShouldMove = 1"), R.Guard->BaseScheduleHost.bShouldMove);
	TestEqual(TEXT("...and mode 2 ends"), R.Guard->AlternateAi, 0);
	return true;
}

// --- S1's own arms: an obstruction inside the waypoint tolerance advances the path, no sink ---------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcDoorPreSinkTest,
	"Elysium.Substrate.NpcDoorLink.ObstructionPreSink", GNpcDoorLinkTestFlags)
bool FElysiumNpcDoorPreSinkTest::RunTest(const FString&)
{
	using namespace ElysiumNpcDoorLinkTestsDetail;
	FDoorLinkRig R(TEXT("doorlink_presink"), 70710);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	// A two-waypoint route (a mid point, then the goal), the waypoint tolerance 20 units.
	R.Guard->PedestrianLegs = { MidLeg(GuardFeetCm + FVector(0.0, 20.0, 0.0)), GoalLeg(R.Guard->Navigator.GoalPosCm) };
	R.Guard->Navigator.bHeadIsGoal = false;
	R.Guard->Navigator.WaypointToleranceCm = 20.f * U;
	// The door named 5 units short of the waypoint: `maxDist (5) < tolerance (20)`, not the goal ->
	// `maxDist = distClear`, `*result = 0`, `AdvancePath` -- the sink (slot 531) is never asked.
	R.Scripted.HeldAt(R.Door->Handle, 5.f);
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("0x102eefb0 advanced the path"), R.Guard->PedestrianLegs.Num(), 1);
	TestFalse(TEXT("...without slot 531 (no door taken)"), R.Guard->OpeningDoor.IsSet());
	TestEqual(TEXT("...and nothing failed"), R.Guard->BaseScheduleHost.FailureReason, 0);
	return true;
}

// --- A locked door on the move step: OnDoorBlocked, -2, the 4.0 s tail mark, 0x0c -----------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcDoorLockedMoveStepTest,
	"Elysium.Substrate.NpcDoorLink.LockedMoveStep", GNpcDoorLinkTestFlags)
bool FElysiumNpcDoorLockedMoveStepTest::RunTest(const FString&)
{
	using namespace ElysiumNpcDoorLinkTestsDetail;
	FDoorLinkRig R(TEXT("doorlink_locked"), 70704, /*LOCKED*/ 0x800);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	// No route to the stand point: slot 531 arm 7's NOT-FOUND arm, which on a closed door runs the
	// open (`0x10298840`) on the spot. (With a route the same refusal lands at the stand point.)
	R.Scripted.bRoutes = false;
	R.Scripted.HeldAt(R.Door->Handle, 300.f);
	const double Now = R.Now();
	R.Guard->NavigatorMoveStep();
	TestEqual(TEXT("the locked door stays shut"), static_cast<int32>(R.Door->State()),
		static_cast<int32>(FElysiumDoorBase::EToggleState::AtBottom));
	// `0x10298840` refused -> `0x1027de00`: no flag 0x10 or 0x40 yet, so 20 s on the door.
	TestEqual(TEXT("OnDoorBlocked stamps +0x640 = curtime + 20"), R.Door->NpcFailedTimer, Now + 20.0, 1e-6);
	TestTrue(TEXT("...m_hBlockedDoor"), R.Guard->BlockedDoor == R.Door->Handle);
	// Slot 531 then ORs 0x40 and answers -2; the step's -2 reaches `Move`'s tail.
	TestTrue(TEXT("slot 531 ORs 0x40 (no route)"), R.Door->HasNpcFailedFlags(0x40));
	TestEqual(TEXT("the -2 step fails 0x0c"), R.Guard->BaseScheduleHost.FailureReason, 0x0c);
	// The link the guard was held at: 20 s with the door from `0x1027de00`, then the tail's
	// `0x102f1fa0(nav, 4.0, NULL)` re-marks the same link 4 s with no door.
	TestTrue(TEXT("the door link is stale"), R.Door->LinkWords.bStale);
	TestEqual(TEXT("...until curtime + 4 (the tail's mark last)"), R.Door->LinkWords.StaleUntil, Now + 4.0, 1e-6);
	TestFalse(TEXT("...with no blocker"), R.Door->LinkWords.StaleDoor.IsSet());
	return true;
}

// --- OnDoorBlocked's two windows and its link mark ----------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcDoorBlockedWindowsTest,
	"Elysium.Substrate.NpcDoorLink.DoorBlockedWindows", GNpcDoorLinkTestFlags)
bool FElysiumNpcDoorBlockedWindowsTest::RunTest(const FString&)
{
	using namespace ElysiumNpcDoorLinkTestsDetail;
	FDoorLinkRig R(TEXT("doorlink_windows"), 70705);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	// Held at the door's link, so `0x102f1fa0` has a link to mark.
	R.Scripted.HeldAt(R.Door->Handle, 300.f);
	R.Guard->NavLastFacts = R.Scripted.Facts;
	R.Guard->bNavLastFactsValid = true;
	const double Now = R.Now();

	R.Guard->OnDoorBlocked(*R.Door);
	TestEqual(TEXT("a plain door: 20 s"), R.Door->NpcFailedTimer, Now + 20.0, 1e-6);
	TestEqual(TEXT("...and the link 20 s"), R.Door->LinkWords.StaleUntil, Now + 20.0, 1e-6);
	TestTrue(TEXT("...naming the door"), R.Door->LinkWords.StaleDoor == R.Door->Handle);

	// `& 0x40` -> 5 s for both; the door stamp is a MAX write, so it keeps the 20.
	R.Door->LinkWords = FElysiumDoorLinkWords();
	R.Door->AddNpcFailedFlags(0x40);
	R.Guard->OnDoorBlocked(*R.Door);
	TestEqual(TEXT("0x40: the link 5 s"), R.Door->LinkWords.StaleUntil, Now + 5.0, 1e-6);
	TestEqual(TEXT("...the door stamp is MAX-written (0x100f0e30)"), R.Door->NpcFailedTimer, Now + 20.0, 1e-6);

	// `& 0x10` skips the whole retry block.
	R.Door->LinkWords = FElysiumDoorLinkWords();
	R.Door->ClearNpcFailedFlags();
	R.Door->NpcFailedTimer = 0.0;
	R.Door->AddNpcFailedFlags(0x10);
	R.Guard->OnDoorBlocked(*R.Door);
	TestFalse(TEXT("0x10: no link mark"), R.Door->LinkWords.bStale);
	TestEqual(TEXT("...and no stamp"), R.Door->NpcFailedTimer, 0.0);

	// Alternate-AI 1 or 2 becomes 3 with a 1.0 s expiry.
	R.Guard->AlternateAi = 1;
	R.Guard->OnDoorBlocked(*R.Door);
	TestEqual(TEXT("mode 1 -> 3"), R.Guard->AlternateAi, 3);
	TestEqual(TEXT("...expiring in 1.0 s"), R.Guard->AlternateAiExpireTime, Now + 1.0, 1e-6);
	return true;
}

// --- The link predicate: stale, re-probe once per curtime, expiry --------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcDoorLinkPredicateTest,
	"Elysium.Substrate.NpcDoorLink.LinkPredicate", GNpcDoorLinkTestFlags)
bool FElysiumNpcDoorLinkPredicateTest::RunTest(const FString&)
{
	using namespace ElysiumNpcDoorLinkTestsDetail;
	FDoorLinkRig R(TEXT("doorlink_predicate"), 70706);
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;
	const FVector LinkStart(0.0, -40.0, 0.0);
	const FVector LinkEnd(0.0, 40.0, 0.0);
	const double Now = R.Now();

	TestTrue(TEXT("0x102fce80 (1): a link with no mark is usable"),
		R.Guard->DoorLinkPathfindingAllowed(*R.Door, LinkStart, LinkEnd));

	// Stale, not expired, the re-probe blocked by the closed leaf: refused, and the link's door gets
	// the door-blocked notice (`0x102ff960`'s tail).
	int32 Probes = 0;
	R.F.Services.TraceRetailQuery = [&Probes, &R](const FElysiumRetailTrace&, FElysiumRetailTraceResult& Out)
	{
		++Probes;
		Out.Fraction = 0.5f;
		Out.HitEntity = R.Door->Handle;
		return true;
	};
	R.Door->MarkLinkStale(Now, 10.0, R.Door->Handle);
	R.Guard->BlockedDoor = FElysiumEntityHandle();
	TestFalse(TEXT("(3) a blocked re-probe keeps the mark"),
		R.Guard->DoorLinkPathfindingAllowed(*R.Door, LinkStart, LinkEnd));
	TestEqual(TEXT("...one probe"), Probes, 1);
	TestTrue(TEXT("...and the door is re-sent to OnDoorBlocked"), R.Guard->BlockedDoor == R.Door->Handle);
	TestFalse(TEXT("(4) a second ask this curtime is stale without probing"),
		R.Guard->DoorLinkPathfindingAllowed(*R.Door, LinkStart, LinkEnd));
	TestEqual(TEXT("...still one probe"), Probes, 1);

	// A clear re-probe on a new curtime clears the mark.
	R.Guard->PathfinderLinkProbeTime = Now - 1.0;
	R.F.Services.TraceRetailQuery = [](const FElysiumRetailTrace&, FElysiumRetailTraceResult& Out)
	{
		Out.Fraction = 1.0f;
		return true;
	};
	TestTrue(TEXT("(3) a clear re-probe makes the link usable"),
		R.Guard->DoorLinkPathfindingAllowed(*R.Door, LinkStart, LinkEnd));
	TestFalse(TEXT("...and clears the bit"), R.Door->LinkWords.bStale);

	// (2) Expiry is STRICT: `curtime == +0x68` is still stale; past it, the bit clears unprobed.
	R.F.Services.TraceRetailQuery = [&Probes, &R](const FElysiumRetailTrace&, FElysiumRetailTraceResult& Out)
	{
		++Probes;
		Out.Fraction = 0.5f;
		Out.HitEntity = R.Door->Handle;
		return true;
	};
	R.Door->MarkLinkStale(Now, 0.0, FElysiumEntityHandle());
	R.Guard->PathfinderLinkProbeTime = Now - 1.0;
	TestFalse(TEXT("curtime == StaleUntil is not expired"),
		R.Guard->DoorLinkPathfindingAllowed(*R.Door, LinkStart, LinkEnd));
	R.Door->MarkLinkStale(Now - 10.0, 5.0, FElysiumEntityHandle());
	const int32 ProbesBefore = Probes;
	TestTrue(TEXT("curtime > StaleUntil re-enables the link"),
		R.Guard->DoorLinkPathfindingAllowed(*R.Door, LinkStart, LinkEnd));
	TestFalse(TEXT("...clearing the bit"), R.Door->LinkWords.bStale);
	TestEqual(TEXT("...without a probe"), Probes, ProbesBefore);
	R.F.Services.TraceRetailQuery = nullptr;
	return true;
}

// --- The look-ahead `0x102f06e0`: a pending door refuses (0x0e), a locked one is dropped silently -----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcDoorLookAheadTest,
	"Elysium.Substrate.NpcDoorLink.LookAhead", GNpcDoorLinkTestFlags)
bool FElysiumNpcDoorLookAheadTest::RunTest(const FString&)
{
	using namespace ElysiumNpcDoorLinkTestsDetail;
	{
		FDoorLinkRig R(TEXT("doorlink_lookahead"), 70707);
		if (!TestTrue(TEXT("rig"), R.Ready())) return false;
		R.F.Services.TraceRetailQuery = [&R](const FElysiumRetailTrace& Trace, FElysiumRetailTraceResult& Out)
		{
			// The ray meets the closed leaf halfway.
			Out.Fraction = 0.5f;
			Out.EndPosCm = (Trace.StartCm + Trace.EndCm) * 0.5;
			Out.HitEntity = R.Door->Handle;
			return true;
		};
		// Another NPC was refused here a moment ago: `+0x640` in the future.
		R.Door->NpcFailedTimer = R.Now() + 3.0;
		// `0x102f13d0` simplifies only a head that is NOT the goal: a mid waypoint, then the goal.
		R.Guard->PedestrianLegs = { MidLeg(GuardFeetCm + FVector(0.0, 300.0, 0.0)), GoalLeg(R.Guard->Navigator.GoalPosCm) };
		R.Guard->Navigator.bHeadIsGoal = false;
		// The far end of the door's link 100 cm ahead: inside the quick pass's 143.9 units.
		R.Scripted.Approaching(R.Door->Handle, GuardFeetCm + FVector(0.0, 100.0, 0.0), 300.f);
		R.Guard->NavigatorMoveStep();
		// slot 531 arm 4: `0x1027f550` refuses (0x10), the arm ORs 4 and answers -2 -> `OnNavFailed(0x0e)`
		// then `OnDoorBlocked`, whose retry block the 0x10 skips.
		TestEqual(TEXT("0x102f06e0 raises OnNavFailed(0x0e)"), R.Guard->BaseScheduleHost.FailureReason, 0x0e);
		TestTrue(TEXT("...slot 531 wrote 0x10 and 0x4"), R.Door->HasNpcFailedFlags(0x14));
		TestTrue(TEXT("...OnDoorBlocked named the door"), R.Guard->BlockedDoor == R.Door->Handle);
		TestEqual(TEXT("...and left its stamp alone (0x10)"), R.Door->NpcFailedTimer, R.Now() + 3.0, 1e-6);
		R.F.Services.TraceRetailQuery = nullptr;
	}
	{
		FDoorLinkRig R(TEXT("doorlink_lookahead_locked"), 70708, /*LOCKED*/ 0x800);
		if (!TestTrue(TEXT("rig"), R.Ready())) return false;
		R.F.Services.TraceRetailQuery = [&R](const FElysiumRetailTrace& Trace, FElysiumRetailTraceResult& Out)
		{
			Out.Fraction = 0.5f;
			Out.EndPosCm = (Trace.StartCm + Trace.EndCm) * 0.5;
			Out.HitEntity = R.Door->Handle;
			return true;
		};
		R.Guard->PedestrianLegs = { MidLeg(GuardFeetCm + FVector(0.0, 300.0, 0.0)), GoalLeg(R.Guard->Navigator.GoalPosCm) };
		R.Guard->Navigator.bHeadIsGoal = false;
		R.Scripted.Approaching(R.Door->Handle, GuardFeetCm + FVector(0.0, 100.0, 0.0), 300.f);
		R.Guard->NavigatorMoveStep();
		TestEqual(TEXT("a locked door's lock test drops the shortcut: no failure"),
			R.Guard->BaseScheduleHost.FailureReason, 0);
		TestEqual(TEXT("...and nothing is written on the door"), static_cast<int32>(R.Door->NpcFailedFlags), 0);
		TestFalse(TEXT("...nor on the guard"), R.Guard->BlockedDoor.IsSet());
		R.F.Services.TraceRetailQuery = nullptr;
	}
	return true;
}

// --- The lock test's arm 1, and the auto-close scan -----------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcDoorLockTestAndCloseTest,
	"Elysium.Substrate.NpcDoorLink.LockTestAndCloseBlocked", GNpcDoorLinkTestFlags)
bool FElysiumNpcDoorLockTestAndCloseTest::RunTest(const FString&)
{
	using namespace ElysiumNpcDoorLinkTestsDetail;
	FDoorLinkRig R(TEXT("doorlink_close"), 70709, 0, TEXT("4"));
	if (!TestTrue(TEXT("rig"), R.Ready())) return false;

	// `0x100eef10`: `noopenwanted` refuses an NPC too, but only while the player is hunted.
	R.Door->bNoOpenWanted = true;
	FElysiumPlayer* Player = R.F.World.FindPlayer();
	if (TestNotNull(TEXT("the player"), Player))
	{
		Player->Police.CopsInPursuit = 0;
		TestFalse(TEXT("noopenwanted with no pursuit admits the NPC"), R.Door->IsUseRefused(R.Guard->Handle));
		Player->Police.CopsInPursuit = 1;
		TestTrue(TEXT("...and refuses it while the police hunt the player"), R.Door->IsUseRefused(R.Guard->Handle));
		Player->Police.CopsInPursuit = 0;
	}
	R.Door->bNoOpenWanted = false;

	// Open, then stand the guard in the leaf's closing volume (the player well clear of it: the
	// scan keeps `FL_CLIENT` too, and the first body met decides).
	if (Player != nullptr)
	{
		Player->Origin = FVector(-5000.0, -5000.0, 0.0);
	}
	R.Door->InputOpen(FElysiumEntityHandle());
	FElysiumNpcDoorLinkTestAccess::Arrive(*R.Door);
	R.Guard->Origin = FVector(50.0, 0.0, -100.0);
	const double Now = R.Now();
	TestTrue(TEXT("0x100f0c00: a live NPC in the closing volume blocks the close"), R.Door->IsCloseBlocked(true));
	TestTrue(TEXT("...+0x644 |= 0x80"), R.Door->HasNpcFailedFlags(0x80));
	TestTrue(TEXT("0x1027dfb0: HIT_BY_DOOR"), R.Guard->Cognition.Conditions.Has(EElysiumNpcCond::HitByDoor));
	TestTrue(TEXT("...m_hCondHitByDoor"), R.Guard->CondHitByDoor == R.Door->Handle);
	// `debug_hit_by_mode` ships "1": the alternate arm, slot 532(4) -- no door stamp and no
	// `m_hBlockedDoor` (those belong to the arm at 0).
	TestEqual(TEXT("...the shipped debug_hit_by_mode 1 stamps no +0x640"), R.Door->NpcFailedTimer, 0.0);
	TestFalse(TEXT("...and names no m_hBlockedDoor"), R.Guard->BlockedDoor.IsSet());
	(void)Now;

	R.Guard->Origin = FVector(1000.0, 1000.0, 0.0);
	TestFalse(TEXT("clear of the volume, nothing blocks"), R.Door->IsCloseBlocked(true));
	return true;
}

#endif
