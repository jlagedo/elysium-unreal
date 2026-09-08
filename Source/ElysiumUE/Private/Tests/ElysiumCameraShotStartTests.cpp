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
// The two `m_angCamAngles` cases below drive the real producer (`FElysiumCameraDirector::Resolve`)
// rather than hand-setting the field the tracker then reads back.
#include "Player/ElysiumCameraShots.h"

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

	// --- no `Target` block: the entity's abs angles are what gets published -----------------------
	//
	// **Driven from the producer**, not from a hand-set field. The two producers of a named shot's
	// goal are `FElysiumCameraDirector::Resolve` and `FElysiumCameraCinematic::ThinkNamedShot`, and
	// the rule under test is theirs: `0x1006f8f0` seeds `fStack_24..1c` from `GetAbsAngles()`
	// (vfunc `0x36c`) **unconditionally** and publishes them at `param_1[0x181..0x183]`; the `+0xd4`
	// gate only decides whether they are *replaced* by `VectorAngles(lookAt - GetOrigin())`. A shot
	// with no `Target` block therefore publishes the entity's own angles at every 24 Hz tick.
	// Asserting a rotation the case itself wrote would be true by construction and green while the
	// producer published a zero.
	//
	// Values only: no world, so every anchor answers `vec3_origin` exactly as a dead handle does
	// (`0x1006f09b`), which is all this needs — the question is the ANGLE, and its two sources are
	// the entity pose the caller hands in.
	{
		FElysiumCameraShotDef Def;
		Def.Name = TEXT("NoTargetBlock");
		Def.Start.bPresent = true;                 // the goal arm, `(flags & 1) != 0`
		Def.TargetPointCount = 0;                  // `rec->+0xd4 == 0`
		Def.Constraints.MoveSpeed = 250.0f * ElysiumCam::U;
		Def.Constraints.MoveAccel = 100.0f * ElysiumCam::U;

		// `GetOrigin()` (vfunc `0x370`) and `GetAbsAngles()` (vfunc `0x36c`) — the placement
		// `FUN_1006e8e0` wrote, which is what the entity answers for the rest of the shot.
		const FElysiumShotEntityPose Pose{ FVector(700.0f, -200.0f, 150.0f),
			FRotator(-25.0f, 120.0f, 0.0f) };

		FElysiumShotBindings Bindings;
		FElysiumCameraShot Shot;
		FElysiumCameraDirector::Resolve(nullptr, Def, FElysiumEntityHandle::Invalid(), Shot,
			&Bindings, EElysiumShotResolvePass::ShotStart,
			EElysiumShotOriginSelector::StartAnchor, &Pose);

		TestFalse(TEXT("a Target-less shot publishes no look-at"), Shot.bUseLookAt);
		TestTrue(TEXT("the PUBLISHED goal carries the entity's abs angles, not a zero rotation"),
			Shot.Rotation.Equals(Pose.Angles, 0.01f));
		TestTrue(TEXT("and says so, so the client copies them through"), Shot.bAnglesPublished);

		FElysiumScriptedShotTracker Tracker;
		Tracker.Start(Shot, LiveView);
		TestTrue(TEXT("shot start seeds on the published angles"),
			Tracker.Rotation.Equals(Pose.Angles, 0.01f));

		// And they hold across a frame: the `+0xd4` gate is not a shot-start-only decision, and the
		// think republishes the same triple every tick.
		Tracker.Advance(Shot, 1.0f / 60.0f);
		TestTrue(TEXT("and holds them while the shot runs"),
			Tracker.Rotation.Equals(Pose.Angles, 0.01f));
	}

	// --- with a `Target` block: the angle is measured from the entity's origin ---------------------
	//
	// `if (0 < rec->+0xd4) { pfVar5 = GetOrigin(); VectorAngles(lookAt - *pfVar5, ...); }` — the
	// LOCAL transform, while `param_1[0x17b..0x17d]` publishes the origin the `+0x594` selector
	// chose. Here the two are deliberately different points, and the published angle is the one
	// measured from the entity.
	{
		FElysiumCameraShotDef Def;
		Def.Name = TEXT("TargetBlock");
		Def.Start.bPresent = true;
		Def.Target1.bPresent = true;
		Def.TargetPointCount = 1;
		Def.bTargetPoint1Flagged = true;

		const FElysiumShotEntityPose Pose{ FVector(0.0f, 900.0f, 0.0f),
			FRotator(-25.0f, 120.0f, 0.0f) };

		FElysiumShotBindings Bindings;
		FElysiumCameraShot Shot;
		FElysiumCameraDirector::Resolve(nullptr, Def, FElysiumEntityHandle::Invalid(), Shot,
			&Bindings, EElysiumShotResolvePass::ShotStart,
			EElysiumShotOriginSelector::StartAnchor, &Pose);

		// Unbound anchors answer the world origin, so the look-at and the published origin are both
		// `(0,0,0)` and the entity is 9 m away in +Y.
		TestTrue(TEXT("the published origin is the selected anchor's"), Shot.Origin.IsNearlyZero());
		TestTrue(TEXT("and the look-at is the world origin"),
			Shot.bUseLookAt && Shot.LookAt.IsNearlyZero());
		TestTrue(TEXT("the published angle is measured from the ENTITY, not from that origin"),
			Shot.Rotation.Equals((Shot.LookAt - Pose.Origin).Rotation(), 0.01f));
		TestFalse(TEXT("which is not what re-deriving from the published origin would give"),
			Shot.Rotation.Equals((Shot.LookAt - Shot.Origin).Rotation(), 1.0f));
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
