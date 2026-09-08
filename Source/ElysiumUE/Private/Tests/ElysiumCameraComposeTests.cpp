// Content-free Substrate automation: retail's TWO scripted channels and the composition shape.
//
// SC2 of `docs/project/camera_scripted.md`. VtMB runs a cine camera that **hard-writes** the base
// pose with no weight at all (`C_BaseCineCamera::CalcView` `client.dll` `FUN_10001b50`) and a
// `camera_track` override that is the only blended one, composed on top of whichever base won
// (`CInput::OverrideView` `FUN_100ffb90`). They are one camera in series, never two viewpoints, and
// they are mutually exclusive at the source (`vampire.dll` `FUN_1017d280` opens with
// `SetCineCamera(NULL)`).
//
// Everything here is a pure function of values, a bare `UElysiumCameraComponent`, or both — no pawn,
// no world, no RHI.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumCameraComponent.h"
#include "ElysiumCameraSolve.h"

#include "Camera/CameraTypes.h"
#include "HAL/IConsoleManager.h"

namespace ElysiumCameraComposeTests
{
// One context flag (runs anywhere) + the product filter, the same pair every camera suite declares.
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// A cine shot: what `FElysiumCameraDirector::Resolve` stamps for a `vdata/camerashots/` file.
static FElysiumCameraShot MakeCineShot(const FVector& Origin, const FVector& LookAt)
{
	FElysiumCameraShot Shot;
	Shot.bCine = true;
	Shot.bTracked = true;          // `CamMode` 1
	Shot.BlendSeconds = 0.0f;      // a cine shot has no ramp at all
	Shot.Origin = Origin;
	Shot.LookAt = LookAt;
	Shot.bUseLookAt = true;
	Shot.DebugName = TEXT("cine");
	return Shot;
}

// A track/value shot: what `PublishTrackCamera` and every other value producer push.
static FElysiumCameraShot MakeTrackShot(const FVector& Origin, const FVector& LookAt, float Blend)
{
	FElysiumCameraShot Shot;
	Shot.bCine = false;
	Shot.bTracked = false;         // the `CInput` override re-derives the aim every frame
	Shot.BlendSeconds = Blend;
	Shot.Origin = Origin;
	Shot.LookAt = LookAt;
	Shot.bUseLookAt = true;
	Shot.DebugName = TEXT("camera_track");
	return Shot;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraComposeTest, "Elysium.Substrate.CameraCompose",
	GElysiumTestFlags)
bool FElysiumCameraComposeTest::RunTest(const FString&)
{
	// --- the stand-in aim point is 240 Source units, not 100 (RC9, `_DAT_1022b298`) ---
	{
		TestEqual(TEXT("the composition's forward stand-in is 240 Source units"),
			ElysiumCam::ViewForwardPointUnits, 240.0f);
		TestTrue(TEXT("which is 609.6 cm"),
			FMath::IsNearlyEqual(ElysiumCam::ViewForwardPointCm, 609.6f, 0.001f));

		// A rotation-carrying shot's stand-in target is exactly its own aim at full weight, so the
		// two authoring forms land on the same pose.
		const FVector Origin(10.0f, -20.0f, 30.0f);
		const FRotator Aim(12.0f, 34.0f, 0.0f);
		const FVector Target = ElysiumCam::ScriptedShotTargetPoint(Origin, Aim);
		TestTrue(TEXT("the stand-in target sits 240 u along the shot's own forward"),
			FMath::IsNearlyEqual(static_cast<float>(FVector::Distance(Origin, Target)),
				ElysiumCam::ViewForwardPointCm, 0.01f));
		TestTrue(TEXT("and re-deriving it answers the shot's own rotation"),
			(Target - Origin).Rotation().Equals(Aim, 0.01f));
	}

	// --- the point lerp is not the rotator lerp, and it is exactly retail's expression ---
	{
		// A 90-degree delta: the base looks down +X, the shot aims at +Y from the same origin.
		const FVector Origin = FVector::ZeroVector;
		const FRotator BaseRot = FRotator::ZeroRotator;
		const FVector ShotOrigin = FVector::ZeroVector;
		const FVector ShotTarget(0.0f, 500.0f, 0.0f);

		for (const float Weight : { 0.25f, 0.5f, 0.75f })
		{
			const float E = ElysiumCam::SimpleSpline(Weight);

			FVector L = Origin;
			FRotator R = BaseRot;
			float Fov = 90.0f;
			ElysiumCam::ComposeScriptedShot(L, R, Fov, ShotOrigin, ShotTarget, 0.0f, 0.0f, Weight);

			// `VectorAngles(lerp(viewFwdPoint, target, e) - lerp(origin, shotOrigin, e))`, written out.
			const FVector FwdPoint = Origin + BaseRot.Vector() * ElysiumCam::ViewForwardPointCm;
			const FVector Expected = (FMath::Lerp(FwdPoint, ShotTarget, E)
				- FMath::Lerp(Origin, ShotOrigin, E)).Rotation().Vector();
			TestTrue(FString::Printf(
				TEXT("the composed aim at weight %.2f is VectorAngles of the lerped points"), Weight),
				R.Vector().Equals(Expected, 0.0005f));
		}

		// And it traces a measurably different arc from an angle lerp: at the midpoint the point lerp
		// is still ~39 degrees round while the rotator lerp is at 45.
		FVector L = Origin;
		FRotator R = BaseRot;
		float Fov = 90.0f;
		ElysiumCam::ComposeScriptedShot(L, R, Fov, ShotOrigin, ShotTarget, 0.0f, 0.0f, 0.5f);
		const FRotator RotatorLerp = FMath::Lerp(BaseRot, FRotator(0.0f, 90.0f, 0.0f), 0.5f);
		TestTrue(TEXT("a point lerp and a rotator lerp trace measurably different arcs"),
			FMath::Abs(FRotator::NormalizeAxis(R.Yaw - RotatorLerp.Yaw)) > 1.0f);
		TestTrue(TEXT("the point lerp lags the uniform sweep at the midpoint"),
			FRotator::NormalizeAxis(R.Yaw) < 45.0f);

		// Both agree at the ends, which is what makes the shape a *feel* change and not a pose change.
		FVector Full = Origin;
		FRotator FullRot = BaseRot;
		float FullFov = 90.0f;
		ElysiumCam::ComposeScriptedShot(Full, FullRot, FullFov, ShotOrigin, ShotTarget, 0.0f, 0.0f, 1.0f);
		TestTrue(TEXT("at full weight the composed aim is the shot's own"),
			FMath::IsNearlyEqual(static_cast<float>(FRotator::NormalizeAxis(FullRot.Yaw)), 90.0f, 0.01f));
	}

	// --- the ease is at the compose site and the ramp is linear ---
	{
		TestEqual(TEXT("SimpleSpline is its own midpoint, so weight 0.5 composes at e = 0.5"),
			ElysiumCam::SimpleSpline(0.5f), 0.5f);

		FVector L = FVector::ZeroVector;
		FRotator R = FRotator::ZeroRotator;
		float Fov = 90.0f;
		ElysiumCam::ComposeScriptedShot(L, R, Fov, FVector(100.0f, 0.0f, 0.0f),
			FVector(700.0f, 0.0f, 0.0f), 0.0f, 40.0f, 0.5f);
		TestTrue(TEXT("half weight puts the origin at the midpoint"),
			FMath::IsNearlyEqual(static_cast<float>(L.X), 50.0f, 0.001f));
		TestTrue(TEXT("and the fov halfway too"), FMath::IsNearlyEqual(Fov, 65.0f, 0.001f));

		// **The FOV lerp is unconditional** (`0x100ffcef`: `FLD ; FSUB ; FMUL e ; FADD ; FSTP`, no
		// test). Retail cannot publish a zero there — `m_flCameraFOVOverride` is `CBaseEntity`'s
		// slot `0xC4`, default 75 — so a 0 handed to the compose lerps the view toward 0 rather
		// than being ignored, and it is the **producer's** job to seed 75 where a value shot
		// authors no FOV.
		FVector ZeroL = FVector::ZeroVector;
		FRotator ZeroR = FRotator::ZeroRotator;
		float ZeroFov = 90.0f;
		ElysiumCam::ComposeScriptedShot(ZeroL, ZeroR, ZeroFov, FVector(100.0f, 0.0f, 0.0f),
			FVector(700.0f, 0.0f, 0.0f), 0.0f, /*ShotFov*/ 0.0f, 0.5f);
		TestTrue(TEXT("a zero shot FOV composes through rather than being skipped"),
			FMath::IsNearlyEqual(ZeroFov, 45.0f, 0.001f));
		TestEqual(TEXT("and CBaseEntity's slot-0xC4 default, the value retail publishes, is 75"),
			ElysiumCam::CameraFovOverrideDefault, 75.0f);

		// The ramp itself is linear: half the authored duration is half the weight, un-eased.
		FElysiumCameraShotStack Stack;
		Stack.Push(MakeTrackShot(FVector::ZeroVector, FVector(100.0f, 0.0f, 0.0f), 2.0f));
		Stack.Advance(1.0f);
		TestTrue(TEXT("the ramp reaches 0.5 at exactly half the authored duration"),
			FMath::IsNearlyEqual(Stack.GetTrackWeight(), 0.5f, 0.0001f));
		Stack.Advance(0.5f);
		TestTrue(TEXT("and 0.75 at three quarters — no ease anywhere in the ramp"),
			FMath::IsNearlyEqual(Stack.GetTrackWeight(), 0.75f, 0.0001f));
	}

	// --- roll is assigned, not lerped: the base view's bank is discarded outright ---
	{
		FVector L = FVector::ZeroVector;
		FRotator R(0.0f, 0.0f, 20.0f);          // a banked base view
		float Fov = 90.0f;
		ElysiumCam::ComposeScriptedShot(L, R, Fov, FVector(100.0f, 0.0f, 0.0f),
			FVector(700.0f, 0.0f, 0.0f), /*ShotRoll*/ 0.0f, 0.0f, 1.0f);
		TestTrue(TEXT("a 20-degree base roll is discarded by a shot that authors none"),
			FMath::IsNearlyZero(static_cast<float>(R.Roll), 0.001f));

		// Mid-ramp it is `e * shotRoll`, not `lerp(baseRoll, shotRoll, e)` — which would read 15 here.
		FRotator Half(0.0f, 0.0f, 20.0f);
		FVector HalfL = FVector::ZeroVector;
		float HalfFov = 90.0f;
		ElysiumCam::ComposeScriptedShot(HalfL, Half, HalfFov, FVector(100.0f, 0.0f, 0.0f),
			FVector(700.0f, 0.0f, 0.0f), /*ShotRoll*/ 10.0f, 0.0f, 0.5f);
		TestTrue(TEXT("mid-ramp roll is e x shotRoll, with no term from the base"),
			FMath::IsNearlyEqual(static_cast<float>(Half.Roll), 5.0f, 0.001f));
	}

	// --- the ramp's stored state is retail's signed duration (M5, RC9's +/-0.01 dead band) ---
	{
		TestTrue(TEXT("the ramp dead band is retail's symmetric 10 ms"),
			FMath::IsNearlyEqual(FElysiumCameraShotStack::RampDeadBandSeconds, 0.01f, 0.0001f));

		// `startTime <= 0` — the override is off.
		FElysiumCameraShotStack Idle;
		TestEqual(TEXT("an unarmed channel has no weight"), Idle.GetTrackWeight(), 0.0f);
		Idle.Advance(10.0f);
		TestEqual(TEXT("and time alone does not arm it"), Idle.GetTrackWeight(), 0.0f);

		// `|duration| <= 0.01` — weight 1 immediately, and it stays.
		FElysiumCameraShotStack Cut;
		const int32 CutId = Cut.Push(MakeTrackShot(FVector::ZeroVector, FVector::ForwardVector, 0.0f));
		TestEqual(TEXT("a zero-duration push is live at full weight on the same frame"),
			Cut.GetTrackWeight(), 1.0f);
		Cut.Advance(5.0f);
		TestEqual(TEXT("and it stays there — the dead band is a cut in, not a fast fade"),
			Cut.GetTrackWeight(), 1.0f);
		FElysiumCameraShotStack Band;
		Band.Push(MakeTrackShot(FVector::ZeroVector, FVector::ForwardVector, 0.005f));
		TestEqual(TEXT("5 ms is inside the dead band, so it is a cut too"), Band.GetTrackWeight(), 1.0f);

		// `duration < -0.01` — the blend out, from wherever the weight is.
		Cut.Pop(CutId, 0.5f);
		TestEqual(TEXT("the blend out starts from the weight in force"), Cut.GetTrackWeight(), 1.0f);
		Cut.Advance(0.25f);
		TestTrue(TEXT("and decays linearly over its duration"),
			FMath::IsNearlyEqual(Cut.GetTrackWeight(), 0.5f, 0.0001f));
		Cut.Advance(0.25f);
		TestEqual(TEXT("reaching zero exactly at the end"), Cut.GetTrackWeight(), 0.0f);

		// A push mid-ramp is re-timed by back-dating the start (`FUN_1017d0b0`), never by writing the
		// weight: reversing resumes from where it is instead of restarting.
		FElysiumCameraShotStack Reverse;
		const int32 FirstId = Reverse.Push(
			MakeTrackShot(FVector::ZeroVector, FVector::ForwardVector, 1.0f));
		Reverse.Advance(0.5f);
		TestTrue(TEXT("half a one-second ramp is half the weight"),
			FMath::IsNearlyEqual(Reverse.GetTrackWeight(), 0.5f, 0.0001f));
		Reverse.Push(MakeTrackShot(FVector(10.0f, 0.0f, 0.0f), FVector::ForwardVector, 1.0f));
		TestTrue(TEXT("a second push does not restart the ramp"),
			FMath::IsNearlyEqual(Reverse.GetTrackWeight(), 0.5f, 0.0001f));
		Reverse.Advance(0.5f);
		TestEqual(TEXT("it finishes the remaining half"), Reverse.GetTrackWeight(), 1.0f);
		TestTrue(TEXT("popping one of two track shots leaves the channel owned"),
			Reverse.Pop(FirstId, 0.5f) && Reverse.GetTrackWeight() == 1.0f);

		// **Only `dur <= 0` turns the override off.** `FUN_1017d6d0` hard-clears the mark on the
		// non-positive arm alone (`0x1017d802` -> `0x1017d87e`); a positive duration inside the dead
		// band is stored as `-dur` with a live mark, and `FUN_100fc900`'s tail reads `|dur| <= 0.01`
		// as weight 1 **that stays** — so a 5 ms "blend out" is a cut IN, not a fast fade out.
		FElysiumCameraShotStack Brief;
		const int32 BriefId = Brief.Push(
			MakeTrackShot(FVector::ZeroVector, FVector::ForwardVector, 1.0f));
		Brief.Advance(1.0f);
		TestEqual(TEXT("the arrival completes"), Brief.GetTrackWeight(), 1.0f);
		Brief.Pop(BriefId, 0.005f);
		TestEqual(TEXT("a 5 ms release leaves the override hard on, as retail does"),
			Brief.GetTrackWeight(), 1.0f);
		Brief.Advance(5.0f);
		TestEqual(TEXT("and it stays on: the dead band is not a fast fade in either sign"),
			Brief.GetTrackWeight(), 1.0f);

		// The shipped shape — every one of the 168 `camera_track` chains returns with
		// `ToPlayerTime 0` — is the arm both readings agree on: the mark is cleared and the
		// override is off on the same frame.
		FElysiumCameraShotStack Cut0;
		const int32 Cut0Id = Cut0.Push(
			MakeTrackShot(FVector::ZeroVector, FVector::ForwardVector, 0.0f));
		Cut0.Pop(Cut0Id, 0.0f);
		TestEqual(TEXT("a zero release clears the mark outright"), Cut0.GetTrackWeight(), 0.0f);
	}

	// --- the two channels answer separately, and a cine shot has no ramp ---
	{
		FElysiumCameraShotStack Stack;
		const int32 CineId = Stack.Push(MakeCineShot(FVector(500.0f, 0.0f, 100.0f),
			FVector(500.0f, 300.0f, 100.0f)));
		TestNotNull(TEXT("the cine channel carries the adopted shot"), Stack.TopCine());
		TestNull(TEXT("and the track channel is empty"), Stack.TopTrack());
		TestEqual(TEXT("a cine shot is full scripted weight the instant it is pushed"),
			Stack.GetWeight(), 1.0f);
		TestEqual(TEXT("with no track ramp armed at all"), Stack.GetTrackWeight(), 0.0f);

		const int32 TrackId = Stack.Push(MakeTrackShot(FVector(0.0f, 0.0f, 0.0f),
			FVector(100.0f, 0.0f, 0.0f), 1.0f));
		TestNotNull(TEXT("the track channel takes the value shot"), Stack.TopTrack());
		TestNotNull(TEXT("without displacing the cine one"), Stack.TopCine());
		TestEqual(TEXT("the two ids are distinct channels, not a stack order"),
			Stack.TopCineId(), CineId);
		TestEqual(TEXT("each answering its own top"), Stack.TopTrackId(), TrackId);

		// **M1: the release of a cine shot is a cut.** Not a fast fade — instantaneous, and its
		// `BlendOutSeconds` argument is not consulted at all.
		Stack.Pop(CineId, 5.0f);
		TestNull(TEXT("the cine channel is empty the frame the pop fires"), Stack.TopCine());
		TestEqual(TEXT("and the argument that would have softened it is ignored"),
			Stack.GetWeight(), Stack.GetTrackWeight());
	}

	// --- the composition through the component: the boom, the hard write and the override ---
	{
		const FVector Eye(0.0f, 0.0f, 160.0f);
		const FVector Boom(-250.0f, 0.0f, 0.0f);          // a 250 cm third-person boom
		const FVector CineOrigin(1000.0f, 0.0f, 200.0f);
		const FVector CineLookAt(1000.0f, 500.0f, 200.0f);

		// A live cine shot suppresses the boom entirely and hard-writes the pose.
		UElysiumCameraComponent* Cine = NewObject<UElysiumCameraComponent>();
		if (TestNotNull(TEXT("the cine case has a camera component"), Cine))
		{
			Cine->SetThirdPerson(true);
			const int32 CineId = Cine->PushShot(MakeCineShot(CineOrigin, CineLookAt));
			Cine->AdvanceFrame(0.5f);                     // the third weight reaches 1 in 0.5 s
			Cine->SetSolvedBoom(Boom, FRotator::ZeroRotator, false);
			TestEqual(TEXT("the rig is fully third person"), Cine->ThirdPersonWeight(), 1.0f);

			FMinimalViewInfo View;
			View.Location = Eye;
			View.Rotation = FRotator::ZeroRotator;
			View.AspectRatio = 4.0f / 3.0f;
			Cine->ApplyToView(View);
			TestTrue(TEXT("a live cine shot composes at the shot origin, boom and all"),
				View.Location.Equals(CineOrigin, 0.01f));
			TestTrue(TEXT("and aims where the shot looks"),
				FMath::IsNearlyEqual(static_cast<float>(FRotator::NormalizeAxis(View.Rotation.Yaw)),
					90.0f, 0.01f));

			// The release is a cut: no residual weight on the frame it fires...
			TestTrue(TEXT("the cine shot pops"), Cine->PopShot(CineId));
			TestEqual(TEXT("leaving zero scripted weight on the same frame"),
				Cine->GetShots().GetWeight(), 0.0f);

			// ...and the very next frame is the rig's own view, boom restored.
			++GFrameCounter;   // one rendered frame, which is what the component's guard counts
			Cine->AdvanceFrame(1.0f / 60.0f);
			FMinimalViewInfo After;
			After.Location = Eye;
			After.Rotation = FRotator::ZeroRotator;
			After.AspectRatio = 4.0f / 3.0f;
			Cine->ApplyToView(After);
			TestTrue(TEXT("the frame after the release is the player view, boom and all"),
				After.Location.Equals(Eye + Boom, 0.01f));
		}

		// A track override at half weight over a live cine base lerps FROM the cine pose.
		UElysiumCameraComponent* Both = NewObject<UElysiumCameraComponent>();
		if (TestNotNull(TEXT("the two-channel case has a camera component"), Both))
		{
			const FVector TrackOrigin(1000.0f, 1000.0f, 200.0f);
			Both->SetThirdPerson(true);
			Both->PushShot(MakeCineShot(CineOrigin, CineLookAt));
			Both->PushShot(MakeTrackShot(TrackOrigin, TrackOrigin + FVector(0.0f, 500.0f, 0.0f), 1.0f));
			++GFrameCounter;
			Both->AdvanceFrame(0.5f);                     // the track ramp reaches 0.5
			Both->SetSolvedBoom(Boom, FRotator::ZeroRotator, false);
			TestTrue(TEXT("the track ramp is at half weight"),
				FMath::IsNearlyEqual(Both->GetShots().GetTrackWeight(), 0.5f, 0.0001f));

			FMinimalViewInfo View;
			View.Location = Eye;
			View.Rotation = FRotator::ZeroRotator;
			View.AspectRatio = 4.0f / 3.0f;
			Both->ApplyToView(View);

			const FVector FromCine = FMath::Lerp(CineOrigin, TrackOrigin,
				ElysiumCam::SimpleSpline(0.5f));
			TestTrue(TEXT("the override lerps from the CINE pose, not from the rig's"),
				View.Location.Equals(FromCine, 0.01f));
			TestFalse(TEXT("which is nowhere near the boomed player view"),
				View.Location.Equals(FMath::Lerp(Eye + Boom, TrackOrigin, 0.5f), 1.0f));
		}

		// A value shot that authors no FOV: the publish seeds `CBaseEntity`'s 75 in its place, so the
		// unconditional lerp lands on the same lens the player view already carries instead of
		// dragging it to zero. This is the half of the FOV rule the producers own.
		UElysiumCameraComponent* NoFov = NewObject<UElysiumCameraComponent>();
		if (TestNotNull(TEXT("the no-FOV case has a camera component"), NoFov))
		{
			FElysiumCameraShot Shot = MakeTrackShot(FVector(200.0f, 0.0f, 160.0f),
				FVector(800.0f, 0.0f, 160.0f), 0.0f);
			TestEqual(TEXT("the fixture authors no FOV at all"), Shot.FieldOfView, 0.0f);
			NoFov->PushShot(Shot);
			++GFrameCounter;
			NoFov->AdvanceFrame(1.0f / 60.0f);
			TestEqual(TEXT("a zero-blend track shot is at full weight"),
				NoFov->GetShots().GetTrackWeight(), 1.0f);

			FMinimalViewInfo View;
			View.Location = Eye;
			View.Rotation = FRotator::ZeroRotator;
			View.AspectRatio = 4.0f / 3.0f;          // the 4:3 reference: no widening
			NoFov->ApplyToView(View);
			TestTrue(TEXT("the composed view keeps a 75-degree lens rather than collapsing to zero"),
				FMath::IsNearlyEqual(static_cast<float>(View.FOV),
					ElysiumCam::CameraFovOverrideDefault, 0.01f));
		}
	}

	// --- no cvar exists that could soften the release (M1, ruled: the cvar is NOT created) ---
	{
		TestNull(TEXT("elysium.CameraShotReleaseSeconds is not registered, and must never be"),
			IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.CameraShotReleaseSeconds")));
		// M12's placeholder retired with RC9: the guard is retail's `camera_fov` in the VtMB console
		// store, so the `elysium.*` stand-in is gone rather than shadowing it.
		TestNull(TEXT("the elysium.* FOV-guard placeholder is gone, replaced by camera_fov"),
			IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.CameraShotFovOverride")));
	}

	// --- `camortho`: Source's orthographic debug view (M11, re-scoped by RC9) ---
	{
		bool bWidthDeclared = false;
		bool bHeightDeclared = false;
		bool bCameraFovDeclared = false;
		for (const ElysiumCam::FCvarDef& Def : ElysiumCam::CvarDefs())
		{
			bWidthDeclared |= FString(Def.Name) == TEXT("c_orthowidth")
				&& FString(Def.Default) == TEXT("100");
			bHeightDeclared |= FString(Def.Name) == TEXT("c_orthoheight")
				&& FString(Def.Default) == TEXT("100");
			bCameraFovDeclared |= FString(Def.Name) == TEXT("camera_fov")
				&& FString(Def.Default) == TEXT("-1");
		}
		TestTrue(TEXT("c_orthowidth is declared at retail's 100"), bWidthDeclared);
		TestTrue(TEXT("c_orthoheight is declared at retail's 100"), bHeightDeclared);
		TestTrue(TEXT("and camera_fov at retail's -1, in the VtMB store rather than as elysium.*"),
			bCameraFovDeclared);

		// Source units in, cm out — the conversion happens once, at the read.
		FElysiumCameraCvars Cvars;
		TMap<FString, FString> Cfg;
		Cfg.Add(TEXT("c_orthowidth"), TEXT("200"));
		Cvars.LoadFrom([&Cfg](const TCHAR* Name) -> FString
		{
			const FString* Hit = Cfg.Find(Name);
			return Hit ? *Hit : FString();
		});
		TestTrue(TEXT("c_orthowidth 200 reads as 508 cm"),
			FMath::IsNearlyEqual(Cvars.OrthoWidth, 200.0f * ElysiumCam::U, 0.001f));
		TestTrue(TEXT("and the unset height keeps retail's 100 u"),
			FMath::IsNearlyEqual(Cvars.OrthoHeight, 100.0f * ElysiumCam::U, 0.001f));

		UElysiumCameraComponent* Ortho = NewObject<UElysiumCameraComponent>();
		if (TestNotNull(TEXT("the camortho case has a camera component"), Ortho))
		{
			++GFrameCounter;
			Ortho->AdvanceFrame(1.0f / 60.0f);            // loads the cvar surface

			FMinimalViewInfo Off;
			Off.Location = FVector::ZeroVector;
			Off.AspectRatio = 4.0f / 3.0f;
			Off.ProjectionMode = ECameraProjectionMode::Perspective;
			Ortho->ApplyBaseToView(Off);
			TestFalse(TEXT("camortho off leaves the view perspective"),
				Off.ProjectionMode == ECameraProjectionMode::Orthographic);

			Ortho->SetOrthographic(true);
			FMinimalViewInfo On;
			On.Location = FVector::ZeroVector;
			On.AspectRatio = 4.0f / 3.0f;
			On.ProjectionMode = ECameraProjectionMode::Perspective;
			Ortho->ApplyBaseToView(On);
			TestTrue(TEXT("camortho on renders orthographically"),
				On.ProjectionMode == ECameraProjectionMode::Orthographic);
			TestTrue(TEXT("at the width c_orthowidth asks for, in cm"),
				FMath::IsNearlyEqual(static_cast<float>(On.OrthoWidth),
					Ortho->GetCvars().OrthoWidth, 0.01f));
			// **`c_orthoheight` is applied too.** Retail writes the whole rect
			// `(-w*0.5, -h*0.5, w*0.5, h*0.5)`; Unreal carries one ortho extent plus an aspect, so
			// the pair lands as the aspect it implies, constrained — the same `w x h` of world is
			// framed. Retail stretches that rect over the window and Unreal letterboxes it instead,
			// which is the named pixel-only divergence.
			TestTrue(TEXT("the rect's second extent lands as a constrained aspect ratio"),
				On.bConstrainAspectRatio);
			TestTrue(TEXT("which is exactly c_orthowidth / c_orthoheight"),
				FMath::IsNearlyEqual(static_cast<float>(On.AspectRatio),
					Ortho->GetCvars().OrthoWidth / Ortho->GetCvars().OrthoHeight, 0.001f));
			TestFalse(TEXT("and the perspective view is left with the window's own aspect"),
				Off.bConstrainAspectRatio);
		}
	}

	// --- `m_iFOV`, the player's own override, in the place the spectator replace used to hold -----
	//
	// **The `cl_view_entity` arm is gone.** RC10 read `CalcView`'s tail (`0x1019158e`) as retail's
	// death/observer view and this suite asserted the compose branch for it; RC14 closed the writer
	// search — `engine.dll DAT_20315be0` has exactly one writer in the image and it is the
	// `cl_view_entity` console command (`0x200276d0`), so no shipped run ever takes the arm. Under
	// the owner's ruling on dev-only console toggles the branch, its struct and its setter were
	// deleted; what is asserted instead is that the *observable* surface no longer offers the
	// replace, and that the one FOV rule the death path does need is in its place.
	{
		UElysiumCameraComponent* Fov = NewObject<UElysiumCameraComponent>();
		if (TestNotNull(TEXT("the FOV case has a camera component"), Fov))
		{
			++GFrameCounter;
			Fov->AdvanceFrame(1.0f / 60.0f);
			Fov->SetSolvedBoom(FVector(-250.0f, 0.0f, 0.0f), FRotator::ZeroRotator, false);

			// The pure rule, with no camera at all: zero latches to 60 (`FUN_100f28d0`), and any
			// other override is the angle itself.
			TestTrue(TEXT("a zero m_iFOV latches to 60"),
				FMath::IsNearlyEqual(ElysiumCameraView::LatchPlayerFov(0), 60.0f, 0.001f));
			TestTrue(TEXT("and a live zoom override is its own angle"),
				FMath::IsNearlyEqual(ElysiumCameraView::LatchPlayerFov(20), 20.0f, 0.001f));

			FMinimalViewInfo Ordinary;
			Ordinary.Location = FVector(0.0f, 0.0f, 160.0f);
			Ordinary.Rotation = FRotator::ZeroRotator;
			Ordinary.AspectRatio = 4.0f / 3.0f;
			Fov->ApplyBaseToView(Ordinary);

			TestFalse(TEXT("no producer has written m_iFOV on a fresh camera"),
				Fov->GetPlayerFovOverride().IsSet());
			// A 4:3 view, so `WidenSourceFov` passes the authored angle through untouched and the
			// composed number is the record's own.
			// ...and retail has no "unset" state: a fresh player carries `m_iFOV == 0` from `Spawn`,
			// which the HUD think latches to 60 before the engine sees it. `default_fov` is the
			// no-local-player branch only (`FUN_100f12b0` `0x100f12dc`), so it never renders in play.
			TestTrue(TEXT("so the living lens is the 60-degree latch, not default_fov"),
				FMath::IsNearlyEqual(static_cast<float>(Ordinary.FOV),
					ElysiumCameraView::ZeroFovLatchDegrees, 0.01f));
			TestFalse(TEXT("and default_fov (75) is not what a living player renders"),
				FMath::IsNearlyEqual(static_cast<float>(Ordinary.FOV), Fov->GetCvars().DefaultFov, 0.01f));

			// What `Event_Killed` writes.
			Fov->SetPlayerFovOverride(0);
			FMinimalViewInfo Dead;
			Dead.Location = FVector(0.0f, 0.0f, 160.0f);
			Dead.Rotation = FRotator::ZeroRotator;
			Dead.AspectRatio = 4.0f / 3.0f;
			Fov->ApplyBaseToView(Dead);
			TestTrue(TEXT("a zero override composes 60, not the record's 75"),
				FMath::IsNearlyEqual(static_cast<float>(Dead.FOV), 60.0f, 0.01f));
			TestTrue(TEXT("and the eye is not moved by the FOV write"),
				Dead.Location.Equals(Ordinary.Location, 0.01f));

			Fov->ClearPlayerFovOverride();
			TestFalse(TEXT("clearing it hands the lens back to default_fov"),
				Fov->GetPlayerFovOverride().IsSet());
		}
	}

	return true;
}

} // namespace ElysiumCameraComposeTests

#endif // WITH_DEV_AUTOMATION_TESTS
