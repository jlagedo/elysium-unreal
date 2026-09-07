// Shot start and the two `OnDataChanged` signals — `C_BaseCineCamera::StartShot` `FUN_10002210` and
// `C_BaseCineCamera::OnDataChanged` `0x100024c0` (SC5).
//
// Retail seeds a starting shot from one of two places, and which one is decided by the shot record's
// own presence flags: `(flags & 2) == 0 || (flags & 1) != 0` — "no `End`, **or** has `Start`" — takes
// the replicated goal, and the `else` takes `CViewRender::GetViewSetup()`, so an `End`-without-`Start`
// shot dollies in from wherever the player is looking. That is the shipped `jack.txt` /
// `dialogdefault.txt` / `centerfullview.txt` shape, which is most of the conversation camera.
//
// The re-seed itself is two signals that retail keeps apart and the port used to conflate into one
// "the top shot id changed": a new `m_nClientResetFrame` says *a shot started* and clears no settle
// flag, while a new `m_ShotIndex` says *a different record is live* and arms the one-shot snap **or**
// clears the three angle-settled flags — never both, and never the pose.
//
// Values only: no pawn, no world, no RHI.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumCameraSolve.h"

namespace ElysiumCameraShotStartTests
{
static constexpr EAutomationTestFlags GElysiumShotStartFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// A `centerfullview`-shaped tracked shot: an `End` anchor, a `Target` block, and the file's own
// constraints so the dolly is rate-limited rather than instant.
FElysiumCameraShot MakeTrackedShot()
{
	FElysiumCameraShot Shot;
	Shot.DebugName = TEXT("CenterFullView");
	Shot.bTracked = true;
	Shot.bCine = true;
	Shot.ShotIndex = 1;
	Shot.Origin = FVector(1000.0f, 0.0f, 0.0f);
	Shot.LookAt = FVector(1000.0f, 400.0f, 0.0f);
	Shot.bUseLookAt = true;
	Shot.TargetPointCount = 2;
	Shot.bTargetPoint1Flagged = true;
	Shot.bTargetPoint2Flagged = true;
	Shot.FieldOfView = 75.0f;
	Shot.MoveSpeed = 250.0f * ElysiumCam::U;
	Shot.MoveAccel = 100.0f * ElysiumCam::U;
	Shot.MaxTurnRate = FVector(90.0f, 90.0f, 90.0f);
	Shot.TurnAccel = 60.0f;
	Shot.DistanceTolerance = 5.0f * ElysiumCam::U;
	Shot.AngularTolerance = FVector(5.0f, 5.0f, 5.0f);
	// `End` and no `Start` — the live-view arm.
	Shot.bHasEndAnchor = true;
	Shot.bHasStartAnchor = false;
	return Shot;
}

FElysiumViewSetup MakeLiveView()
{
	FElysiumViewSetup View;
	View.Location = FVector(0.0f, 0.0f, 180.0f);
	View.Rotation = FRotator(-10.0f, 45.0f, 0.0f);
	return View;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraShotStartTest, "Elysium.Substrate.CameraShotStart",
	ElysiumCameraShotStartTests::GElysiumShotStartFlags)

bool FElysiumCameraShotStartTest::RunTest(const FString&)
{
	using namespace ElysiumCameraShotStartTests;

	const FElysiumViewSetup LiveView = MakeLiveView();

	// --- the arm test itself, both halves of `!(End) || Start` -----------------------------------
	{
		FElysiumCameraShot Shot = MakeTrackedShot();
		TestFalse(TEXT("End without Start takes the live-view arm"), Shot.StartsOnGoal());

		Shot.bHasStartAnchor = true;
		TestTrue(TEXT("a Start anchor puts it back on the goal arm"), Shot.StartsOnGoal());

		Shot.bHasStartAnchor = false;
		Shot.bHasEndAnchor = false;
		TestTrue(TEXT("and a shot with neither anchor takes the goal arm too"), Shot.StartsOnGoal());
	}

	// --- `End` without `Start`: seed from the supplied view, then dolly to the goal ---------------
	{
		const FElysiumCameraShot Shot = MakeTrackedShot();
		FElysiumScriptedShotTracker Tracker;
		Tracker.Start(Shot, LiveView);

		TestTrue(TEXT("the pose is the live view, not the shot's framing"),
			Tracker.Location.Equals(LiveView.Location, 0.01f));
		TestTrue(TEXT("and so are the angles"),
			Tracker.Rotation.Equals(LiveView.Rotation, 0.01f));
		TestTrue(TEXT("which is nowhere near the goal"),
			!Tracker.Location.Equals(Shot.Origin, 1.0f));

		// It closes under the shot file's own rate rather than cutting.
		const double Opening = (Shot.Origin - Tracker.Location).Size();
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			Tracker.Advance(Shot, 1.0f / 60.0f);
		}
		const double Closing = (Shot.Origin - Tracker.Location).Size();
		TestTrue(TEXT("the camera dollies toward the goal"), Closing < Opening);
		TestTrue(TEXT("and has not arrived in three frames at 250 u/s"), Closing > 1.0);
		TestTrue(TEXT("it is translating, so the viewmodel gate is closed"), Tracker.IsDollying());

		// Given long enough it lands on the shot's framing.
		for (int32 Frame = 0; Frame < 600; ++Frame)
		{
			Tracker.Advance(Shot, 1.0f / 60.0f);
		}
		TestTrue(TEXT("the dolly ends on the authored origin"),
			Tracker.Location.Equals(Shot.Origin, Shot.DistanceTolerance + 1.0f));
	}

	// --- a `Start`-bearing shot seeds ON the goal -------------------------------------------------
	{
		FElysiumCameraShot Shot = MakeTrackedShot();
		Shot.bHasStartAnchor = true;
		FElysiumScriptedShotTracker Tracker;
		Tracker.Start(Shot, LiveView);

		TestTrue(TEXT("the pose is the shot's own origin"),
			Tracker.Location.Equals(Shot.Origin, 0.01f));
		TestTrue(TEXT("and the angles are derived from its look-at"),
			Tracker.Rotation.Equals((Shot.LookAt - Shot.Origin).Rotation(), 0.01f));
		TestFalse(TEXT("a shot that opens on its goal is not dollying"), Tracker.IsDollying());
	}

	// --- neither anchor: the goal arm too --------------------------------------------------------
	{
		FElysiumCameraShot Shot = MakeTrackedShot();
		Shot.bHasEndAnchor = false;
		FElysiumScriptedShotTracker Tracker;
		Tracker.Start(Shot, LiveView);
		TestTrue(TEXT("no End and no Start still seeds on the goal"),
			Tracker.Location.Equals(Shot.Origin, 0.01f));
	}

	// --- the settle flags are asymmetric at start -------------------------------------------------
	{
		FElysiumCameraShot Shot = MakeTrackedShot();
		FElysiumScriptedShotTracker Tracker;
		// Dirty every field shot start is supposed to write, so a missing write shows up.
		Tracker.Speed = 999.0f;
		Tracker.TurnRate = FVector(11.0f, 22.0f, 33.0f);
		Tracker.bPositionSettled = false;
		Tracker.bPitchSettled = true;
		Tracker.bYawSettled = true;
		Tracker.bRollSettled = true;

		Tracker.Start(Shot, LiveView);
		TestTrue(TEXT("position is settled from frame one, on the wide DistanceTolerance band"),
			Tracker.bPositionSettled);
		TestFalse(TEXT("pitch is unsettled, so it acquires at the tight 1 degree band"),
			Tracker.bPitchSettled);
		TestFalse(TEXT("yaw likewise"), Tracker.bYawSettled);
		TestFalse(TEXT("roll likewise"), Tracker.bRollSettled);
		TestEqual(TEXT("the speed is zeroed"), Tracker.Speed, 0.0f);
		TestTrue(TEXT("and the three turn rates with it"),
			Tracker.TurnRate.IsNearlyZero());
		TestFalse(TEXT("a shot without SnapOnShotChange arms no snap"), Tracker.bSnapPending);

		// `if (flags & 0x80) m_bSnapPending = 1;`, the tail of shot start.
		Shot.bSnapOnShotChange = true;
		Tracker.Start(Shot, LiveView);
		TestTrue(TEXT("SnapOnShotChange arms the one-shot at the tail of shot start"),
			Tracker.bSnapPending);
	}

	// --- no `Target` block: the authored angles stand ---------------------------------------------
	{
		FElysiumCameraShot Shot = MakeTrackedShot();
		Shot.bHasStartAnchor = true;          // the goal arm, so the angles come from the shot
		Shot.TargetPointCount = 0;            // `rec->+0xd4 == 0`
		Shot.bTargetPoint1Flagged = false;
		Shot.bTargetPoint2Flagged = false;
		Shot.bUseLookAt = false;
		Shot.Rotation = FRotator(-25.0f, 120.0f, 0.0f);

		FElysiumScriptedShotTracker Tracker;
		Tracker.Start(Shot, LiveView);
		TestTrue(TEXT("with no Target block the shot keeps its authored angles"),
			Tracker.Rotation.Equals(Shot.Rotation, 0.01f));

		// And it keeps them across a frame: the `+0xd4` gate is not a shot-start-only decision.
		Tracker.Advance(Shot, 1.0f / 60.0f);
		TestTrue(TEXT("and holds them while the shot runs"),
			Tracker.Rotation.Equals(Shot.Rotation, 0.01f));
	}

	// --- the reset-frame signal: arms shot start, clears no settle flag ---------------------------
	{
		FElysiumCameraShot Shot = MakeTrackedShot();
		Shot.ResetFrame = 4;
		FElysiumScriptedShotTracker Tracker;
		FElysiumShotStartEdges Edges;

		Edges.OnDataChanged(Shot, Tracker);
		TestTrue(TEXT("the first sight of a shot is a reset-frame change"),
			Edges.bShotStartPending);
		Edges.bShotStartPending = false;

		// Park the aim, then re-shoot the SAME record: `SetShot` stamps the reset frame again.
		Tracker.bPitchSettled = true;
		Tracker.bYawSettled = true;
		Tracker.bRollSettled = true;
		Tracker.bSnapPending = false;

		Shot.ResetFrame = 5;
		const bool bIndexChanged = Edges.OnDataChanged(Shot, Tracker);
		TestTrue(TEXT("a new reset frame arms shot start"), Edges.bShotStartPending);
		TestFalse(TEXT("and is not a shot-index change"), bIndexChanged);
		TestTrue(TEXT("the settled aim survives a re-shot: pitch"), Tracker.bPitchSettled);
		TestTrue(TEXT("yaw"), Tracker.bYawSettled);
		TestTrue(TEXT("roll"), Tracker.bRollSettled);
		TestFalse(TEXT("and no snap is armed"), Tracker.bSnapPending);

		// A frame with nothing new arms nothing.
		Edges.bShotStartPending = false;
		Edges.OnDataChanged(Shot, Tracker);
		TestFalse(TEXT("an unchanged shot arms nothing"), Edges.bShotStartPending);
	}

	// --- the shot-index signal: snap OR re-acquire, never both, never the pose --------------------
	{
		// (a) the record carries `SnapOnShotChange`.
		{
			FElysiumCameraShot Shot = MakeTrackedShot();
			Shot.bSnapOnShotChange = true;
			Shot.ResetFrame = 9;
			FElysiumScriptedShotTracker Tracker;
			FElysiumShotStartEdges Edges;
			Edges.OnDataChanged(Shot, Tracker);
			Edges.bShotStartPending = false;
			Tracker.bSnapPending = false;
			Tracker.bPitchSettled = true;
			Tracker.bYawSettled = true;
			Tracker.bRollSettled = true;

			// A different record swapped under the same camera: the index changes, the reset frame
			// does not.
			Shot.ShotIndex = 2;
			TestTrue(TEXT("a new shot index is reported as one"), Edges.OnDataChanged(Shot, Tracker));
			TestTrue(TEXT("SnapOnShotChange arms the one-shot"), Tracker.bSnapPending);
			TestTrue(TEXT("and the angle flags are left alone: pitch"), Tracker.bPitchSettled);
			TestTrue(TEXT("yaw"), Tracker.bYawSettled);
			TestTrue(TEXT("roll"), Tracker.bRollSettled);
			TestFalse(TEXT("a shot-index change never arms shot start"), Edges.bShotStartPending);
		}

		// (b) the record does not.
		{
			FElysiumCameraShot Shot = MakeTrackedShot();
			Shot.ResetFrame = 9;
			FElysiumScriptedShotTracker Tracker;
			FElysiumShotStartEdges Edges;
			Edges.OnDataChanged(Shot, Tracker);
			Edges.bShotStartPending = false;
			Tracker.bSnapPending = false;
			Tracker.bPitchSettled = true;
			Tracker.bYawSettled = true;
			Tracker.bRollSettled = true;

			Shot.ShotIndex = 3;
			Edges.OnDataChanged(Shot, Tracker);
			TestFalse(TEXT("without SnapOnShotChange no snap is armed"), Tracker.bSnapPending);
			TestFalse(TEXT("the aim re-acquires instead: pitch"), Tracker.bPitchSettled);
			TestFalse(TEXT("yaw"), Tracker.bYawSettled);
			TestFalse(TEXT("roll"), Tracker.bRollSettled);
			TestFalse(TEXT("and shot start is still not armed"), Edges.bShotStartPending);
		}
	}

	// --- the channel stamps the reset frame, and the think does not ------------------------------
	{
		FElysiumCameraShotStack Stack;
		FElysiumCameraShot Shot = MakeTrackedShot();
		const int32 Id = Stack.Push(Shot);
		const FElysiumCameraShot* Live = Stack.Find(Id);
		if (TestNotNull(TEXT("the pushed shot"), Live))
		{
			const int32 Stamped = Live->ResetFrame;
			TestTrue(TEXT("a push stamps a reset frame"), Stamped != 0);

			// `Update` is the think: it re-publishes the goal and must not re-stamp, or nothing would
			// ever dolly.
			FElysiumCameraShot Moved = Shot;
			Moved.Origin += FVector(10.0f, 0.0f, 0.0f);
			Stack.Update(Id, Moved);
			Live = Stack.Find(Id);
			TestEqual(TEXT("a Follow re-resolve preserves it"), Live->ResetFrame, Stamped);
			TestTrue(TEXT("while the goal itself moved"),
				Live->Origin.Equals(Moved.Origin, 0.01f));

			// `Restart` is `SetShot` on the camera already up.
			Stack.Restart(Id);
			Live = Stack.Find(Id);
			TestTrue(TEXT("a re-shot of the same id stamps a new one"), Live->ResetFrame != Stamped);
			TestEqual(TEXT("and does not change the shot index"), Live->ShotIndex, Shot.ShotIndex);
		}

		// Two shots pushed in a row never share a stamp, so revealing the one underneath is a shot
		// start for it.
		const int32 Second = Stack.Push(MakeTrackedShot());
		TestTrue(TEXT("a second push gets its own stamp"),
			Stack.Find(Second)->ResetFrame != Stack.Find(Id)->ResetFrame);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
