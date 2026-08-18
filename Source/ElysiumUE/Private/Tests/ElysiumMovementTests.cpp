// Content-free Substrate automation: movement math, gym specifications, placement space, courses, gait speeds, and locomotion samples.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "ElysiumAppState.h"
#include "ElysiumAudioLatency.h"
#include "ElysiumBinds.h"
#include "ElysiumBrushComponent.h"
#include "Player/ElysiumCameraShots.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraRig.h"
#include "ElysiumCameraSolve.h"
#include "Substrate/ElysiumCameraTrack.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumCommands.h"
#include "ElysiumContentPaths.h"
#include "Debug/ElysiumChannelRecorder.h"
#include "Debug/ElysiumConsole.h"
#include "Debug/ElysiumLogTap.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumDecals.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumNpcBody.h"
#include "Visual/ElysiumLightRig.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEnvironment.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumStub.h"
#include "ElysiumWeatherState.h"
#include "ElysiumFog.h"
#include "ElysiumEventQueue.h"
#include "ElysiumWireReport.h"
#include "ElysiumExpr.h"
#include "ElysiumGaitSpeeds.h"               // the animation's per-direction speed (CCC7)
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "Debug/ElysiumArenaSpec.h"
#include "ElysiumGymSpec.h"
#include "Visual/ElysiumPoseDeviation.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumLineService.h"
#include "ElysiumLookCurve.h"                // the mouse path's pure rules (CCC3)
#include "Debug/ElysiumCastRun.h"             // the body trace's second producer
#include "Debug/ElysiumLocomotionTrace.h"     // the recorded body trace, both producers
#include "Debug/ElysiumMoveCourses.h"        // the event-timed press's pure half (CCC3)
#include "Debug/ElysiumMoveRun.h"             // the body trace's first producer
#include "ElysiumMapActor.h"
#include "ElysiumMapEpoch.h"
#include "Map/ElysiumFeedTargeting.h"
#include "Map/ElysiumMapCollision.h"
#include "ElysiumSoundCache.h"
#include "ElysiumMovementComponent.h"
#include "Visual/ElysiumObjModel.h"
#include "Visual/ElysiumNpcClips.h"
#include "ElysiumLocomotionSample.h"         // the body sample's pure rules (CCC1)
#include "ElysiumMoveSolve.h"                // ElysiumMove::StandViewZ / U — the gaze test's units
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumDisposition.h"    // FElysiumEyeTargetTuning
#include "ElysiumPawn.h"
#include "ElysiumPresentationSubsystem.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumInterestingPlaces.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumQuestLog.h"
#include "Substrate/ElysiumQuestView.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSkillClasses.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumScenePlayer.h"
#include "Substrate/ElysiumSheetMath.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Scripting/ElysiumPythonVM.h"
#include "ElysiumViewState.h"
#include "Visual/ElysiumRopes.h"
#include "Scripting/ElysiumScriptFS.h"
#include "ElysiumScriptHost.h"
#include "Scripting/ElysiumScriptNatives.h"
#include "Tests/ElysiumOverlapTestProbe.h"
#include "Tests/ElysiumTestServices.h"
#include "ElysiumTimeControl.h"
#include "ElysiumUseIcons.h"
#include "ElysiumUserCmd.h"
#include "ElysiumVariant.h"

#include "Math/RotationMatrix.h"
#include "Animation/AnimSequence.h"
#include "Serialization/MemoryWriter.h"
#include "Tests/AutomationCommon.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sound/SoundGenerator.h"
#include "Sound/SoundWaveProcedural.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// One context flag (runs anywhere) + the product filter (this project's own suite bucket).
// EAutomationTestFlags is a strong enum in 5.8, so the constant carries that type (ENUM_CLASS_FLAGS
// makes the `|` yield an EAutomationTestFlags), not int32.
namespace ElysiumMovementTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// =====================================================================================
// The mover's arithmetic (4.7) — every number here is one `docs/vtmb/source_movement.md` records
// off the decompile, asserted without a pawn, a world or an RHI.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMovementTest, "Elysium.Substrate.Movement", GElysiumTestFlags)
bool FElysiumMovementTest::RunTest(const FString&)
{
	using namespace ElysiumMove;
	const FElysiumMoveTuning T;

	// FVector is double-precision; the tuning and the recorded constants are float. Narrow at the
	// comparison rather than widening every constant, so the numbers below read as the doc writes
	// them.
	auto F = [](double D) { return static_cast<float>(D); };

	// --- Friction: 3D speed, a stopspeed floor, and all three components scaled ---------------
	{
		// Below the 0.1 cut-off nothing happens at all.
		FVector V(0.05f, 0.0f, 0.0f);
		ApplyFriction(V, T.Friction, T.StopSpeed, 1.0f, 1.0f / 60.0f);
		TestEqual(TEXT("friction ignores a near-stopped body"), F(V.X), 0.05f);

		// Above stopspeed the drop is proportional to the speed itself.
		V = FVector(500.0f, 0.0f, 0.0f);
		const float Dt = 1.0f / 60.0f;
		ApplyFriction(V, T.Friction, T.StopSpeed, 1.0f, Dt);
		const float Expected = 500.0f - 500.0f * T.Friction * Dt;
		TestTrue(TEXT("friction drops control * friction * dt"), FMath::IsNearlyEqual(F(V.X), Expected, 0.01f));

		// Under stopspeed the *control* speed floors at sv_stopspeed, so a slow body loses a
		// constant amount rather than a proportional one — that is what makes the stop crisp.
		V = FVector(10.0f, 0.0f, 0.0f);
		ApplyFriction(V, T.Friction, T.StopSpeed, 1.0f, Dt);
		const float FlooredDrop = T.StopSpeed * T.Friction * Dt;
		TestTrue(TEXT("below stopspeed the control speed floors"),
			FMath::IsNearlyEqual(F(V.X), 10.0f - FlooredDrop, 0.01f));

		// It scales the vertical component too — the decompile does not special-case Z.
		V = FVector(300.0f, 0.0f, 300.0f);
		ApplyFriction(V, T.Friction, T.StopSpeed, 1.0f, Dt);
		TestTrue(TEXT("friction scales Z as well as XY"), F(V.Z) < 300.0f);
	}

	// --- Accelerate, and the air-accel asymmetry ----------------------------------------------
	{
		const float Dt = 1.0f / 60.0f;
		const FVector Dir(1.0f, 0.0f, 0.0f);

		FVector V = FVector::ZeroVector;
		ApplyAccelerate(V, Dir, RunSpeed, T.Accelerate, 1.0f, Dt);
		TestTrue(TEXT("ground accel adds accel * dt * wishspeed"),
			FMath::IsNearlyEqual(F(V.X), T.Accelerate * Dt * RunSpeed, 0.01f));

		// Already at the target: nothing is added.
		V = FVector(RunSpeed, 0.0f, 0.0f);
		ApplyAccelerate(V, Dir, RunSpeed, T.Accelerate, 1.0f, Dt);
		TestTrue(TEXT("ground accel adds nothing at the target"),
			FMath::IsNearlyEqual(F(V.X), RunSpeed, 0.01f));

		// The whole of air-strafing: the CAP binds the target, but the UNCAPPED wishspeed drives
		// accelspeed. Past the 30 u/s cap the air gives no more forward speed...
		V = FVector(AirSpeedCap + 10.0f, 0.0f, 0.0f);
		FVector Before = V;
		ApplyAirAccelerate(V, Dir, RunSpeed, T.AirAccel, T.AirSpeedCap, 1.0f, Dt);
		TestTrue(TEXT("air accel is capped along the wish direction"),
			FMath::IsNearlyEqual(F(V.X), F(Before.X), 0.01f));

		// ...but sideways, where the projected speed is still 0, it gives a full uncapped kick.
		// That asymmetry is why a Source player can gain speed by strafing in the air.
		V = FVector(1000.0f, 0.0f, 0.0f);
		const FVector Side(0.0f, 1.0f, 0.0f);
		ApplyAirAccelerate(V, Side, RunSpeed, T.AirAccel, T.AirSpeedCap, 1.0f, Dt);
		TestTrue(TEXT("but sideways it accelerates on the UNCAPPED wishspeed"),
			FMath::IsNearlyEqual(F(V.Y), FMath::Min(T.AirAccel * RunSpeed * Dt, AirSpeedCap), 0.01f));
		TestTrue(TEXT("and the sideways kick beats the capped target"), F(V.Y) > 0.0f);
	}

	// --- CheckVelocity clamps per COMPONENT, not per magnitude --------------------------------
	{
		FVector V(T.MaxVelocity * 2.0f, T.MaxVelocity * 2.0f, 0.0f);
		CheckVelocity(V, T.MaxVelocity);
		TestTrue(TEXT("each component clamps to sv_maxvelocity"),
			FMath::IsNearlyEqual(F(V.X), T.MaxVelocity, 0.01f) &&
			FMath::IsNearlyEqual(F(V.Y), T.MaxVelocity, 0.01f));
		// The distinction from GetClampedToMaxSize: the magnitude is allowed past the limit.
		TestTrue(TEXT("so the magnitude may exceed it — this is not a size clamp"),
			F(V.Size()) > T.MaxVelocity);

		V = FVector(FMath::Sqrt(-1.0f), 0.0f, 0.0f);
		CheckVelocity(V, T.MaxVelocity);
		TestTrue(TEXT("and a non-finite component is scrubbed"), F(V.X) == 0.0f);
	}

	// --- ClipVelocity's blocked bits ----------------------------------------------------------
	{
		FVector Out;
		const int32 Floor = ClipVelocity(FVector(100.0f, 0.0f, -100.0f), FVector::UpVector, Out);
		TestEqual(TEXT("a floor plane reports bit 1"), Floor, 1);
		TestTrue(TEXT("and the downward component is removed"), FMath::IsNearlyEqual(F(Out.Z), 0.0f, 0.01f));

		const int32 Wall = ClipVelocity(FVector(100.0f, 0.0f, 0.0f), FVector(-1.0f, 0.0f, 0.0f), Out);
		TestEqual(TEXT("a vertical wall reports bit 2"), Wall, 2);
		TestTrue(TEXT("and the into-wall component is removed"), FMath::IsNearlyEqual(F(Out.X), 0.0f, 0.01f));
	}

	// --- The jump: a held push under reduced gravity, not a single impulse --------------------
	{
		// The four numbers are `rules.txt`'s, not `CGameMovement` constants. sv_jump_boost is an
		// origin pop measured in inches, NOT an apex height — asserting that here is what stops
		// the old `sqrt(2 * boost * g)` reading coming back.
		TestTrue(TEXT("BaseJumpVelocity is 185 Source units/s"),
			FMath::IsNearlyEqual(T.BaseJumpVelocity, 185.0f * U, 0.5f));
		TestTrue(TEXT("gravity runs at 0.75 during a jump"),
			FMath::IsNearlyEqual(T.JumpGravityMultiplier, 0.75f, 1e-4f));
		TestTrue(TEXT("and the push window is JumpHoldTime"),
			FMath::IsNearlyEqual(T.JumpHoldSeconds, 0.2f, 1e-4f));
		TestTrue(TEXT("sv_jump_boost is an origin pop in inches, not an apex"),
			FMath::IsNearlyEqual(T.JumpBoost, 25.0f, 1e-4f));

		// The ballistic tail, once the push window has closed. The half-step split is exact for
		// constant acceleration, so this piece is dt-invariant even though the height it reaches
		// is no longer `sv_jump_boost`.
		// ApexHeight already returns Source units.
		const float TailApex = ApexHeight(T.BaseJumpVelocity,
			T.Gravity * T.JumpGravityMultiplier);
		TestTrue(TEXT("the ballistic tail alone clears 28 units"), TailApex > 28.0f);

		// Integrate the whole thing the way FullWalkMove does, holding the button for the window.
		auto SimulateApex = [&T](float Dt)
		{
			const float G = T.Gravity * T.JumpGravityMultiplier;
			FVector V(0.0f, 0.0f, T.BaseJumpVelocity);
			float Z = T.JumpBoost * U * ElysiumMove::JumpBoostScale;   // the press-frame pop
			float Peak = Z;
			float Hold = T.JumpHoldSeconds;
			for (int32 i = 0; i < 4096 && (V.Z > 0.0f || Z > 0.0f); ++i)
			{
				StartGravity(V, G, Dt);
				// The held push re-asserts the launch speed while the window is open.
				if (Hold > 0.0f) { V.Z = FMath::Max(V.Z, T.BaseJumpVelocity); Hold -= Dt; }
				Z += V.Z * Dt;
				FinishGravity(V, G, Dt);
				Peak = FMath::Max(Peak, Z);
			}
			return Peak / U;
		};

		const float Apex60 = SimulateApex(1.0f / 60.0f);
		const float Apex120 = SimulateApex(1.0f / 120.0f);
		const float Apex240 = SimulateApex(1.0f / 240.0f);

		// The headline: the reachable height is far above the 25 units the old model produced,
		// which is what made the tutorial's crate stack unclimbable.
		TestTrue(TEXT("a held jump clears well past 25 units"), Apex60 > 60.0f);
		// The push window is wall-clock, so the three rates agree to within a frame's worth of it.
		TestTrue(TEXT("and 120 fps agrees with 60"), FMath::Abs(Apex120 - Apex60) < 4.0f);
		TestTrue(TEXT("and 240 fps agrees with 60"), FMath::Abs(Apex240 - Apex60) < 4.0f);

		// A tap is shorter than a hold — the mousewheel-vs-spacebar difference players report.
		TestTrue(TEXT("the ballistic tail alone is shorter than a full held jump"),
			TailApex < Apex60);
	}

	// --- The hulls (RE22) and the frame bound --------------------------------------------------
	{
		// Read off the CGameMovement constructor: the ducked hull keeps the standing footprint and
		// halves the height, and its eye is at 30 — not stock Source's VEC_DUCK_VIEW of 28.
		TestTrue(TEXT("the standing hull is 72u tall"), FMath::IsNearlyEqual(StandHeight / U, 72.0f, 0.01f));
		TestTrue(TEXT("the ducked hull is 36u"), FMath::IsNearlyEqual(DuckHeight / U, 36.0f, 0.01f));
		TestTrue(TEXT("the standing eye is at 64u"), FMath::IsNearlyEqual(StandViewZ / U, 64.0f, 0.01f));
		TestTrue(TEXT("and the ducked eye at 30u, not Source's 28"),
			FMath::IsNearlyEqual(DuckViewZ / U, 30.0f, 0.01f));

		// The frame bound is ONE number, and the mover and the clock must clamp with it together.
		TestTrue(TEXT("a hitch bounds to 0.1 s"),
			FMath::IsNearlyEqual(ElysiumFrame::ClampFrameDelta(5.0), 0.1, 1e-9));
		TestTrue(TEXT("a sub-millisecond frame floors at 0.001 s"),
			FMath::IsNearlyEqual(ElysiumFrame::ClampFrameDelta(0.00001), 0.001, 1e-9));
		TestTrue(TEXT("and zero passes through rather than fabricating time"),
			ElysiumFrame::ClampFrameDelta(0.0) == 0.0);

		// These two constants are also what FElysiumTimeControl::ApplyToWorld writes into
		// AWorldSettings::Min/MaxUndilatedFrameTime, and what Config/DefaultGame.ini declares.
		// If they drift, the engine's filter and this one stop being the same filter and
		// engine-tick consumers advance further on a long frame than the substrate does.
		TestEqual(TEXT("the floor is Host_FilterTime's"), ElysiumFrame::MinFrameSeconds, 0.001);
		TestEqual(TEXT("and the ceiling is Host_FilterTime's"), ElysiumFrame::MaxFrameSeconds, 0.1);

		// The bound scales with time dilation, as retail's (`timescale * [0.001, 0.1]`) and
		// Unreal's (`Min/Max * Dilation`) both do. The delta handed in is already dilated, so a
		// flat bound would disagree with the engine in both directions.
		TestTrue(TEXT("a hitch at 0.25x bounds to 0.025 s"),
			FMath::IsNearlyEqual(ElysiumFrame::ClampFrameDelta(5.0, 0.25), 0.025, 1e-9));
		TestTrue(TEXT("and the floor scales down with it rather than fabricating motion"),
			FMath::IsNearlyEqual(ElysiumFrame::ClampFrameDelta(0.00001, 0.25), 0.00025, 1e-9));
		TestTrue(TEXT("a hitch at 2x bounds to 0.2 s"),
			FMath::IsNearlyEqual(ElysiumFrame::ClampFrameDelta(5.0, 2.0), 0.2, 1e-9));
		// A held world advances nothing, and a negative scale must not invert the bound.
		TestTrue(TEXT("scale 0 collapses the bound to zero"),
			ElysiumFrame::ClampFrameDelta(0.05, 0.0) == 0.0);
		TestTrue(TEXT("and a negative scale is treated as held, not reversed"),
			ElysiumFrame::ClampFrameDelta(0.05, -1.0) == 0.0);
	}

	// --- The cvar surface is declared, so a config.cfg governs and nothing falls to Python -----
	{
		TestTrue(TEXT("the sv_* movement surface is declared"), CvarDefs().Num() >= 9);

		TMap<FString, FString> Store;
		auto Lookup = [&Store](const TCHAR* Name)
		{
			const FString* Found = Store.Find(Name);
			return Found ? *Found : FString();
		};

		FElysiumMoveTuning Tuned;
		Tuned.LoadFrom(Lookup);
		TestTrue(TEXT("an empty store keeps VtMB's own defaults"),
			FMath::IsNearlyEqual(Tuned.Gravity, Gravity, 0.01f));

		Store.Add(TEXT("sv_gravity"), TEXT("400"));
		Store.Add(TEXT("sv_jump_boost"), TEXT("100"));
		Tuned.LoadFrom(Lookup);
		TestTrue(TEXT("a console value wins, converted from Source units once"),
			FMath::IsNearlyEqual(Tuned.Gravity, 400.0f * U, 0.01f));
		// The pop stays in Source units because it *is* a distance in inches — read back literally.
		TestTrue(TEXT("and sv_jump_boost reads back in Source units, unconverted"),
			FMath::IsNearlyEqual(Tuned.JumpBoost, 100.0f, 0.01f));
	}

	return true;
}

// =====================================================================================
// The gym's specification (CCC0). The layout is derived from `ElysiumMove`'s constants, so what
// is asserted here is the **derivation** — that each lane straddles the value it names — and never
// what a body will do on it. An expectation recomputed from the same constants as the geometry
// moves with the geometry and could never fail; the behaviour is the headless run's to record.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGymSpecTest, "Elysium.Substrate.GymSpec", GElysiumTestFlags)
bool FElysiumGymSpecTest::RunTest(const FString&)
{
	using namespace ElysiumMove;
	const FElysiumMoveTuning T;
	const ElysiumGym::FSpec Spec = ElysiumGym::Build(T);

	TestTrue(TEXT("the gym has lanes"), Spec.Lanes.Num() > 0);
	TestTrue(TEXT("the gym has solids"), Spec.Placements.Num() > 0);

	// --- Structural invariants the spawner depends on -----------------------------------------
	{
		TSet<FName> LaneNames;
		for (const ElysiumGym::FLane& L : Spec.Lanes)
		{
			TestFalse(TEXT("a lane is named"), L.Name.IsNone());
			TestFalse(FString::Printf(TEXT("lane '%s' is declared once"), *L.Name.ToString()),
				LaneNames.Contains(L.Name));
			LaneNames.Add(L.Name);
		}

		// (Lane, Tag) becomes a spawned component's name, so a duplicate is a silently dropped solid.
		TSet<FString> Keys;
		for (const ElysiumGym::FPlacement& P : Spec.Placements)
		{
			const FString Key = P.Lane.ToString() + TEXT("_") + P.Tag.ToString();
			TestFalse(FString::Printf(TEXT("solid '%s' is placed once"), *Key), Keys.Contains(Key));
			Keys.Add(Key);

			TestTrue(FString::Printf(TEXT("solid '%s' has a real extent"), *Key),
				P.Extent.X > 0.0 && P.Extent.Y > 0.0 && P.Extent.Z > 0.0);
			TestTrue(FString::Printf(TEXT("solid '%s' is finite"), *Key),
				P.Center.ContainsNaN() == false && P.Rot.ContainsNaN() == false);
			TestTrue(FString::Printf(TEXT("solid '%s' belongs to a declared lane"), *Key),
				LaneNames.Contains(P.Lane));
		}
	}

	// --- Lanes do not overlap, so one body's course cannot touch another's geometry -----------
	{
		for (const ElysiumGym::FLane& L : Spec.Lanes)
		{
			const double LaneY = L.FeetOrigin.Y;
			for (const ElysiumGym::FPlacement& P : Spec.Placements)
			{
				if (P.Lane != L.Name)
				{
					continue;
				}
				const double Reach = FMath::Abs(P.Center.Y - LaneY) + P.Extent.Y;
				// A doorway jamb reaches exactly to the band edge, and the spec derives its centre
				// and half-extent in float — so the slack here is float rounding at cm scale, not a
				// margin the layout is allowed to spend.
				TestTrue(FString::Printf(TEXT("'%s/%s' stays inside its lane's Y band"),
					*P.Lane.ToString(), *P.Tag.ToString()),
					Reach <= ElysiumGym::LaneHalfWidth * U + 0.01);
			}
		}
		TestTrue(TEXT("the lane pitch clears the lane width"),
			ElysiumGym::LanePitch > 2.0f * ElysiumGym::LaneHalfWidth);
	}

	// --- Each bracket straddles the constant it names -----------------------------------------
	auto Bracket = [&Spec, this](const TCHAR* Below, const TCHAR* At, const TCHAR* Above,
		float Threshold, const TCHAR* What)
	{
		const ElysiumGym::FLane* B = Spec.FindLane(FName(Below));
		const ElysiumGym::FLane* M = Spec.FindLane(FName(At));
		const ElysiumGym::FLane* A = Spec.FindLane(FName(Above));
		if (!B || !M || !A)
		{
			AddError(FString::Printf(TEXT("%s is missing a bracket lane"), What));
			return;
		}
		TestEqual(FString::Printf(TEXT("%s: the middle rung IS the constant"), What),
			M->BracketUnits, Threshold);
		TestTrue(FString::Printf(TEXT("%s: one rung below, one above"), What),
			B->BracketUnits < Threshold && A->BracketUnits > Threshold);
	};

	Bracket(TEXT("riser_m1"), TEXT("riser_0"), TEXT("riser_p1"), T.StepSize / U, TEXT("StepSize"));
	Bracket(TEXT("pop_m1"), TEXT("pop_0"), TEXT("pop_p1"), T.JumpBoost, TEXT("JumpBoost"));
	Bracket(TEXT("stand_m1"), TEXT("stand_0"), TEXT("stand_p1"), StandHeight / U, TEXT("StandHeight"));
	Bracket(TEXT("duck_m1"), TEXT("duck_0"), TEXT("duck_p1"), DuckHeight / U, TEXT("DuckHeight"));
	Bracket(TEXT("door_m1"), TEXT("door_0"), TEXT("door_p1"), 2.0f * HullHalfWidth / U,
		TEXT("the hull width"));

	// The riser bracket also has to reach past the cliff on both sides, which is what tells 19 from
	// 20 and 24 — a two-sided bracket alone would only ever prove the first refusal.
	{
		const ElysiumGym::FLane* Low = Spec.FindLane(FName(TEXT("riser_m2")));
		const ElysiumGym::FLane* High = Spec.FindLane(FName(TEXT("riser_p6")));
		TestTrue(TEXT("the riser bracket reaches two below the step"),
			Low && Low->BracketUnits < T.StepSize / U - 1.0f);
		TestTrue(TEXT("and well above it"),
			High && High->BracketUnits > T.StepSize / U + 1.0f);
	}

	// --- The slope bracket is a statement about a normal, not about a pitch --------------------
	{
		const float LimitDeg = FMath::RadiansToDegrees(FMath::Acos(StandableZ));
		int32 Standable = 0;
		int32 Steep = 0;
		for (const ElysiumGym::FLane& L : Spec.Lanes)
		{
			if (L.Family != ElysiumGym::EFamily::Slope)
			{
				continue;
			}
			(L.BracketUnits < LimitDeg ? Standable : Steep)++;

			// A ramp pitched at P has a top-face normal whose Z is cos(P). That identity is the
			// whole reason a pitch brackets `StandableZ` at all, so assert it on the emitted solid
			// rather than trusting the constructor.
			const ElysiumGym::FPlacement* Ramp = Spec.Placements.FindByPredicate(
				[&L](const ElysiumGym::FPlacement& P)
				{ return P.Lane == L.Name && P.Tag == FName(TEXT("ramp")); });
			if (!Ramp)
			{
				AddError(FString::Printf(TEXT("slope lane '%s' has no ramp"), *L.Name.ToString()));
				continue;
			}
			const double NormalZ = Ramp->Rot.RotateVector(FVector::UpVector).Z;
			TestTrue(FString::Printf(TEXT("'%s' top-face normal Z is cos(pitch)"), *L.Name.ToString()),
				FMath::IsNearlyEqual(NormalZ,
					FMath::Cos(FMath::DegreesToRadians(L.BracketUnits)), 1e-4));
		}
		TestTrue(TEXT("slopes straddle the standable limit on both sides"),
			Standable >= 2 && Steep >= 2);
	}

	// --- The unduck chamber admits a ducked hull and refuses a standing one --------------------
	{
		const ElysiumGym::FLane* Chamber = Spec.FindLane(FName(TEXT("unduck_ground")));
		TestNotNull(TEXT("the grounded unduck chamber exists"), Chamber);
		TestNotNull(TEXT("the airborne unduck chamber exists"),
			Spec.FindLane(FName(TEXT("unduck_air"))));
		if (Chamber)
		{
			TestTrue(TEXT("the chamber is taller than the ducked hull"),
				Chamber->BracketUnits > DuckHeight / U);
			TestTrue(TEXT("and shorter than the standing one"),
				Chamber->BracketUnits < StandHeight / U);
		}
	}

	// --- The crouch-jump lane names the lift, which is half the hull difference ----------------
	{
		const ElysiumGym::FLane* DuckPop = Spec.FindLane(FName(TEXT("duckpop")));
		TestNotNull(TEXT("the crouch-jump lane exists"), DuckPop);
		if (DuckPop)
		{
			TestTrue(TEXT("its bracket is the airborne duck's own lift"),
				FMath::IsNearlyEqual(DuckPop->BracketUnits,
					(StandHeight - DuckHeight) * 0.5f / U, 1e-3f));
		}
	}

	// --- The leniency lanes: one lip, one drop, brackets in frames (CCC3) ---------------------
	{
		static const TCHAR* const LedgeNames[] =
			{ TEXT("ledge_m1"), TEXT("ledge_0"), TEXT("ledge_p1"),
			  TEXT("ledge_p2"), TEXT("ledge_p4") };
		static const float LedgeOffsets[] = { -1.0f, 0.0f, 1.0f, 2.0f, 4.0f };
		static const TCHAR* const LandNames[] =
			{ TEXT("land_p1"), TEXT("land_0"), TEXT("land_m1"),
			  TEXT("land_m2"), TEXT("land_m4") };
		static const float LandOffsets[] = { 1.0f, 0.0f, -1.0f, -2.0f, -4.0f };

		auto CheckLeniencyLane = [&Spec, this](const TCHAR* Name, float Offset,
			ElysiumGym::EFamily Family)
		{
			const ElysiumGym::FLane* L = Spec.FindLane(FName(Name));
			if (!TestNotNull(FString::Printf(TEXT("leniency lane '%s' exists"), Name), L))
			{
				return;
			}
			TestEqual(FString::Printf(TEXT("'%s' brackets its own frame offset"), Name),
				L->BracketUnits, Offset);
			TestTrue(FString::Printf(TEXT("'%s' is the family it is named for"), Name),
				L->Family == Family);
			// It starts where every other lane starts, so the seat rule has no special case.
			TestTrue(FString::Printf(TEXT("'%s' seats at the standard inset"), Name),
				FMath::IsNearlyEqual(L->FeetOrigin.X,
					(-ElysiumGym::RunUp + ElysiumGym::StartInset) * U, 1e-3));

			// The lip is the upper slab's far face, at X = 0 — the feature-face convention every
			// family uses — and the drop below it is `PitDepth`, deep enough that the fall spans far
			// more frames than the widest bracket asks for.
			const ElysiumGym::FPlacement* Upper = Spec.Placements.FindByPredicate(
				[L](const ElysiumGym::FPlacement& P)
				{ return P.Lane == L->Name && P.Tag == FName(TEXT("upper")); });
			const ElysiumGym::FPlacement* Lower = Spec.Placements.FindByPredicate(
				[L](const ElysiumGym::FPlacement& P)
				{ return P.Lane == L->Name && P.Tag == FName(TEXT("lower")); });
			if (!TestNotNull(FString::Printf(TEXT("'%s' has a run-up"), Name), Upper)
				|| !TestNotNull(FString::Printf(TEXT("'%s' has a floor to land on"), Name), Lower))
			{
				return;
			}
			TestTrue(FString::Printf(TEXT("'%s' puts the lip at the feature face"), Name),
				FMath::IsNearlyEqual(Upper->Center.X + Upper->Extent.X, 0.0, 1e-3));
			TestTrue(FString::Printf(TEXT("'%s' walks off the top of the run-up"), Name),
				FMath::IsNearlyEqual(Upper->Center.Z + Upper->Extent.Z, 0.0, 1e-3));
			TestTrue(FString::Printf(TEXT("'%s' drops a full PitDepth"), Name),
				FMath::IsNearlyEqual(Lower->Center.Z + Lower->Extent.Z,
					-ElysiumGym::PitDepth * U, 1e-3));
			// The lower floor starts under the lip and runs to the wall, so the body lands on it and
			// then keeps going until it is stopped — which is what makes the reach saturate.
			TestTrue(FString::Printf(TEXT("'%s' catches the body from the lip onward"), Name),
				FMath::IsNearlyEqual(Lower->Center.X - Lower->Extent.X, 0.0, 1e-3)
				&& FMath::IsNearlyEqual(Lower->Center.X + Lower->Extent.X,
					ElysiumGym::LaneLength * U, 1e-3));
		};

		for (int32 i = 0; i < UE_ARRAY_COUNT(LedgeNames); ++i)
		{
			CheckLeniencyLane(LedgeNames[i], LedgeOffsets[i], ElysiumGym::EFamily::Ledge);
			CheckLeniencyLane(LandNames[i], LandOffsets[i], ElysiumGym::EFamily::Landing);
		}

		// The brackets straddle the decision on both sides, which is what makes a refusal a cliff
		// rather than an absence: `ledge_m1` must jump and `ledge_p1` must not, and if leniency is
		// ever added it is `ledge_p1`/`land_m1` that move.
		const ElysiumGym::FLane* LedgeBefore = Spec.FindLane(FName(TEXT("ledge_m1")));
		const ElysiumGym::FLane* LedgeAfter = Spec.FindLane(FName(TEXT("ledge_p1")));
		TestTrue(TEXT("the coyote bracket straddles the ground-loss frame"),
			LedgeBefore && LedgeAfter
			&& LedgeBefore->BracketUnits < 0.0f && LedgeAfter->BracketUnits > 0.0f);
		const ElysiumGym::FLane* LandAfter = Spec.FindLane(FName(TEXT("land_p1")));
		const ElysiumGym::FLane* LandBefore = Spec.FindLane(FName(TEXT("land_m1")));
		TestTrue(TEXT("the buffer bracket straddles the landing frame"),
			LandAfter && LandBefore
			&& LandAfter->BracketUnits > 0.0f && LandBefore->BracketUnits < 0.0f);
	}

	// --- What `CCC7` may move is flagged, and what it may not is not --------------------------
	{
		for (const ElysiumGym::FLane& L : Spec.Lanes)
		{
			// Only the two families whose answer is a horizontal *distance* move with the gait. The
			// leniency lanes are deliberately not among them: their answer is whether one press
			// became a jump, and a press placed against a body event produces the same 0 or 1
			// however fast the body reached the lip. This row is what enforces that claim.
			const bool bHorizontalReach =
				L.Family == ElysiumGym::EFamily::Gap || L.Family == ElysiumGym::EFamily::Flat;
			TestEqual(FString::Printf(TEXT("'%s' declares the right speed class"), *L.Name.ToString()),
				L.bSpeedDependent, bHorizontalReach);
		}
	}

	return true;
}

// A constant owns exactly one bracket. This is the acceptance criterion "moving a constant in
// `ElysiumMove` turns exactly the bracket that constant owns red", asserted on the geometry with
// no world — the headless run then confirms it on behaviour.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGymSpecOwnershipTest,
	"Elysium.Substrate.GymSpecOwnership", GElysiumTestFlags)
bool FElysiumGymSpecOwnershipTest::RunTest(const FString&)
{
	const FElysiumMoveTuning Base;
	const ElysiumGym::FSpec Before = ElysiumGym::Build(Base);

	FElysiumMoveTuning Moved = Base;
	Moved.StepSize += ElysiumMove::U;                 // one Source unit taller
	const ElysiumGym::FSpec After = ElysiumGym::Build(Moved);

	TestEqual(TEXT("moving a constant does not add or drop a solid"),
		After.Placements.Num(), Before.Placements.Num());

	int32 Moved3D = 0;
	for (int32 i = 0; i < Before.Placements.Num() && i < After.Placements.Num(); ++i)
	{
		const ElysiumGym::FPlacement& A = Before.Placements[i];
		const ElysiumGym::FPlacement& B = After.Placements[i];
		TestEqual(TEXT("the lane order is stable"), B.Lane, A.Lane);
		TestEqual(TEXT("the tag order is stable"), B.Tag, A.Tag);

		const bool bSame = A.Center.Equals(B.Center, 1e-3) && A.Extent.Equals(B.Extent, 1e-3);
		if (!bSame)
		{
			++Moved3D;
			// Only the lanes `StepSize` owns may move, and every one of them must.
			TestTrue(FString::Printf(TEXT("'%s/%s' moved, and it is a riser"),
				*A.Lane.ToString(), *A.Tag.ToString()),
				A.Lane.ToString().StartsWith(TEXT("riser_")));
		}
	}
	TestTrue(TEXT("the risers did move"), Moved3D > 0);

	// And the lane the constant *is* moves by exactly what the constant moved by.
	{
		const ElysiumGym::FLane* A = Before.FindLane(FName(TEXT("riser_0")));
		const ElysiumGym::FLane* B = After.FindLane(FName(TEXT("riser_0")));
		TestTrue(TEXT("the middle riser tracks the step exactly"),
			A && B && FMath::IsNearlyEqual(B->BracketUnits - A->BracketUnits, 1.0f, 1e-3f));
	}
	return true;
}

// The one place the spec's feet-anchored convention meets the pawn's centre-anchored box. This is
// the "stands **on** the gym rather than above it" acceptance in the form that can be asserted:
// the hull's own underside, not its origin, is what has to land on the floor.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGymSeatTest, "Elysium.Substrate.GymSeat", GElysiumTestFlags)
bool FElysiumGymSeatTest::RunTest(const FString&)
{
	// The pawn's constructed hull, read off the CDO rather than restated — a half-height typed
	// twice is a half-height that can disagree with itself.
	const AElysiumPawn* Pawn = GetDefault<AElysiumPawn>();
	TestNotNull(TEXT("the player pawn has a CDO"), Pawn);
	if (!Pawn)
	{
		return false;
	}
	const float HalfHeight = Pawn->GetBodyHalfHeight();
	TestTrue(TEXT("the hull half-height is the standing hull's"),
		FMath::IsNearlyEqual(HalfHeight, ElysiumMove::StandHeight * 0.5f, 0.01f));

	const FVector Feet(1234.0, -567.0, 89.0);
	const FVector Origin = ElysiumGym::SeatOrigin(Feet, HalfHeight);

	TestTrue(TEXT("seating moves nothing horizontally"),
		FMath::IsNearlyEqual(Origin.X, Feet.X, 1e-4) && FMath::IsNearlyEqual(Origin.Y, Feet.Y, 1e-4));

	const double HullMinZ = Origin.Z - HalfHeight;
	TestTrue(TEXT("the hull's underside clears the floor by exactly DistEpsilon"),
		FMath::IsNearlyEqual(HullMinZ - Feet.Z, ElysiumMove::DistEpsilon, 1e-4));
	TestTrue(TEXT("so the body is above the surface, never inside it"), HullMinZ > Feet.Z);

	// And a gym lane's own start seats the same way — the lanes are authored at floor level, so a
	// start that needed a per-lane fudge would mean the convention had leaked.
	{
		const FElysiumMoveTuning T;
		const ElysiumGym::FSpec Spec = ElysiumGym::Build(T);
		for (const ElysiumGym::FLane& L : Spec.Lanes)
		{
			TestTrue(FString::Printf(TEXT("lane '%s' starts at floor level"), *L.Name.ToString()),
				FMath::IsNearlyEqual(L.FeetOrigin.Z, 0.0, 1e-4));
		}

		// The lane the green room's drive mode seats a body on by default (CCC6). It is named here
		// because the harness names it: a lane that is renamed or dropped would otherwise turn a
		// hand-driven session into a body standing in the void, with nothing red to say so.
		const ElysiumGym::FLane* Drive = Spec.FindLane(TEXT("flat"));
		TestNotNull(TEXT("the gym carries the 'flat' lane drive mode seats on"), Drive);
		if (Drive != nullptr)
		{
			// Long enough to reach a run and stop again — that is what the flat lane is for, and it is
			// why drive mode starts there rather than on a bracket rung.
			TestTrue(TEXT("and it is the long clear run rather than a bracket rung"),
				Drive->Family == ElysiumGym::EFamily::Flat);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerViewTransformTest,
	"Elysium.Substrate.PlayerViewTransform", GElysiumTestFlags)
bool FElysiumPlayerViewTransformTest::RunTest(const FString&)
{
	for (const FVector& SourceView : {
		FVector::ZeroVector,
		FVector(23.5f, -147.25f, 9.75f),
		FVector(-89.f, 179.f, -31.f) })
	{
		const FRotator UnrealView = ElysiumPlayerView::ToUnreal(SourceView);
		TestTrue(FString::Printf(TEXT("Source view %s round-trips"), *SourceView.ToString()),
			ElysiumPlayerView::ToSource(UnrealView).Equals(SourceView));
		TestTrue(TEXT("pitch follows the Source-to-Unreal handedness conversion"),
			FMath::IsNearlyEqual(UnrealView.Pitch, -SourceView.X));
		TestTrue(TEXT("yaw follows the Source-to-Unreal handedness conversion"),
			FMath::IsNearlyEqual(UnrealView.Yaw, -SourceView.Y));
		TestTrue(TEXT("roll retains its signed axis"),
			FMath::IsNearlyEqual(UnrealView.Roll, SourceView.Z));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerPlacementSpaceTest,
	"Elysium.Substrate.PlayerPlacementSpace", GElysiumTestFlags)
bool FElysiumPlayerPlacementSpaceTest::RunTest(const FString&)
{
	constexpr float HalfHeight = 96.f;
	const FVector AuthoredFeet(120.f, -340.f, 15.f);
	const FVector ExpectedCenter = AuthoredFeet + FVector(0.f, 0.f, HalfHeight);

	TestTrue(TEXT("authored map spawns convert feet to the capsule centre exactly once"),
		ElysiumPlayerPlacement::ToCapsuleCenter(AuthoredFeet,
			EElysiumPlayerPlacementSpace::Feet, HalfHeight).Equals(ExpectedCenter));

	const FVector LandmarkOrigin(1000.f, 2000.f, -300.f);
	const FVector LandmarkOffset(25.f, -50.f, 10.f);
	TestTrue(TEXT("landmark-relative entry keeps the authored feet offset"),
		ElysiumPlayerPlacement::ToCapsuleCenter(LandmarkOrigin + LandmarkOffset,
			EElysiumPlayerPlacementSpace::Feet, HalfHeight).Equals(
				LandmarkOrigin + LandmarkOffset + FVector(0.f, 0.f, HalfHeight)));
	TestTrue(TEXT("direct landmark entry uses the landmark as feet origin"),
		ElysiumPlayerPlacement::ToCapsuleCenter(LandmarkOrigin,
			EElysiumPlayerPlacementSpace::Feet, HalfHeight).Equals(
				LandmarkOrigin + FVector(0.f, 0.f, HalfHeight)));

	const FVector LegacySaveCenter(-400.f, 20.f, 777.f);
	TestTrue(TEXT("legacy save restoration preserves its capsule-centre payload"),
		ElysiumPlayerPlacement::ToCapsuleCenter(LegacySaveCenter,
			EElysiumPlayerPlacementSpace::CapsuleCenter, HalfHeight).Equals(LegacySaveCenter));
	return true;
}

// =====================================================================================
// The pose deviation measure (CCC5/CCC6). Two callers share it: the Content tier asserts a real
// baked body left its bind pose, and the green room's drive panel reports the same number live. It
// is the only observable the T-pose failure has, so the arithmetic is worth pinning on its own.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPoseDeviationTest,
	"Elysium.Substrate.PoseDeviation", GElysiumTestFlags)
bool FElysiumPoseDeviationTest::RunTest(const FString&)
{
	TArray<FTransform> A;
	A.SetNum(4);
	TArray<FTransform> B = A;

	{
		const ElysiumPose::FDeviation Same = ElysiumPose::Measure(A, B);
		TestEqual(TEXT("identical poses move no bone"), Same.MovedBones, 0);
		TestTrue(TEXT("and deviate by nothing"), FMath::IsNearlyEqual(Same.MaxDegrees, 0.f, 1e-4f));
	}

	// One bone, one known angle.
	B[2].SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(30.0)));
	{
		const ElysiumPose::FDeviation One = ElysiumPose::Measure(A, B);
		TestEqual(TEXT("one rotated bone reads as one moved bone"), One.MovedBones, 1);
		TestTrue(TEXT("at the angle it was rotated by"),
			FMath::IsNearlyEqual(One.MaxDegrees, 30.0f, 0.01f));
	}

	// **Bone 0 is not a pose.** In component space the root carries the actor transform, so a body
	// that merely walked across the gym would otherwise read as a changed pose — which is exactly the
	// signal drive mode watches for a T-pose.
	B = A;
	B[0].SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(90.0)));
	{
		const ElysiumPose::FDeviation Root = ElysiumPose::Measure(A, B);
		TestEqual(TEXT("the root is skipped"), Root.MovedBones, 0);
		TestTrue(TEXT("and contributes no angle"), FMath::IsNearlyEqual(Root.MaxDegrees, 0.f, 1e-4f));
	}

	// The threshold separates a posed bone from arithmetic noise, and it is exclusive.
	B = A;
	B[1].SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(0.4)));
	B[3].SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(0.6)));
	{
		const ElysiumPose::FDeviation Noise = ElysiumPose::Measure(A, B);
		TestEqual(TEXT("only the bone past the threshold counts"), Noise.MovedBones, 1);
	}

	// Mismatched lengths compare what both carry rather than reading off the end: a component's
	// transform array and a reference pose can legitimately disagree while an LOD is settling.
	{
		TArray<FTransform> Short;
		Short.SetNum(2);
		const ElysiumPose::FDeviation Ragged = ElysiumPose::Measure(A, Short);
		TestEqual(TEXT("a shorter pose is compared as far as it goes"), Ragged.MovedBones, 0);
	}
	return true;
}

#if !UE_BUILD_SHIPPING
// =====================================================================================
// The event-timed press (CCC3). A leniency course cannot say "jump at 2.4 seconds": the time it
// takes to reach a lip moves with the gait, and `CCC7` may halve it. It says "jump K frames after
// the ground is lost" instead, and the harness measures the event in a probe pass. What is pure —
// and therefore asserted here — is the placement: given a resolved frame, exactly one command in
// the stream carries the press, and nothing else about the stream moves.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMoveCoursesTest,
	"Elysium.Substrate.MoveCourses", GElysiumTestFlags)
bool FElysiumMoveCoursesTest::RunTest(const FString&)
{
	using namespace ElysiumMoveCourses;
	const float Step = 1.0f / 60.0f;
	const uint64 JumpBit = static_cast<uint64>(EElysiumButton::Jump);

	FCourse C;
	C.Name = FName(TEXT("test_ledge"));
	C.Host = EHost::GymStage;
	C.Segments.Add({ 2.0f, FVector2D(1.0, 0.0), 0, 0.0f });
	C.EventJump = FEventJump{ EBodyEvent::GroundLost, 2 };

	// The probe pass: the same course, no press. This is what makes the two passes comparable —
	// the stream is the same length and carries the same intent, minus the one frame under test.
	const FElysiumUserCmdStream Probe = Expand(C, Step, INDEX_NONE);
	TestEqual(TEXT("the probe stream is the whole course"), Probe.Num(), 120);
	int32 ProbePresses = 0;
	for (const FElysiumUserCmd& Cmd : Probe.Cmds)
	{
		ProbePresses += Cmd.IsDown(EElysiumButton::Jump) ? 1 : 0;
	}
	TestEqual(TEXT("the probe pass presses nothing"), ProbePresses, 0);

	// The record pass: the press lands at event + offset, and on exactly one frame.
	const FElysiumUserCmdStream Record = Expand(C, Step, 40);
	TestEqual(TEXT("both passes are the same length"), Record.Num(), Probe.Num());
	int32 Presses = 0;
	int32 PressAt = INDEX_NONE;
	for (int32 i = 0; i < Record.Num(); ++i)
	{
		if (Record.Cmds[i].IsDown(EElysiumButton::Jump))
		{
			++Presses;
			PressAt = i;
		}
	}
	TestEqual(TEXT("exactly one frame presses jump"), Presses, 1);
	TestEqual(TEXT("and it is the resolved frame plus the offset"), PressAt, 42);

	// Nothing else moved: the press is OR-ed on, so the body keeps walking through it.
	for (int32 i = 0; i < Record.Num(); ++i)
	{
		TestEqual(TEXT("the move intent is untouched by the press"),
			Record.Cmds[i].Move, Probe.Cmds[i].Move);
		TestEqual(TEXT("no other button is touched"),
			Record.Cmds[i].Buttons & ~JumpBit, Probe.Cmds[i].Buttons & ~JumpBit);
	}

	// A negative offset is the buffer bracket, and it reaches backwards from the event.
	C.EventJump = FEventJump{ EBodyEvent::GroundGained, -4 };
	const FElysiumUserCmdStream Before = Expand(C, Step, 40);
	TestTrue(TEXT("a negative offset presses before the event"),
		Before.Cmds.IsValidIndex(36) && Before.Cmds[36].IsDown(EElysiumButton::Jump));

	// An offset that would fall off either end places nothing rather than clamping — a press
	// silently moved to frame 0 would be a course quietly measuring something else.
	C.EventJump = FEventJump{ EBodyEvent::GroundLost, 500 };
	const FElysiumUserCmdStream Past = Expand(C, Step, 40);
	C.EventJump = FEventJump{ EBodyEvent::GroundLost, -500 };
	const FElysiumUserCmdStream Under = Expand(C, Step, 40);
	int32 OutOfRangePresses = 0;
	for (int32 i = 0; i < Past.Num(); ++i)
	{
		OutOfRangePresses += Past.Cmds[i].IsDown(EElysiumButton::Jump) ? 1 : 0;
		OutOfRangePresses += Under.Cmds[i].IsDown(EElysiumButton::Jump) ? 1 : 0;
	}
	TestEqual(TEXT("an out-of-range offset places no press at all"), OutOfRangePresses, 0);

	// A course with no event jump ignores a resolved frame entirely, which is what lets the harness
	// run every existing course through the same call unchanged.
	FCourse Plain;
	Plain.Segments.Add({ 1.0f, FVector2D(1.0, 0.0), 0, 0.0f });
	const FElysiumUserCmdStream PlainStream = Expand(Plain, Step, 10);
	int32 PlainPresses = 0;
	for (const FElysiumUserCmd& Cmd : PlainStream.Cmds)
	{
		PlainPresses += Cmd.IsDown(EElysiumButton::Jump) ? 1 : 0;
	}
	TestEqual(TEXT("a course with no event jump is unaffected"), PlainPresses, 0);

	// --- Every leniency lane gets a recipe, and it is a single press against the right edge -----
	{
		const FElysiumMoveTuning T;
		const ElysiumGym::FSpec Spec = ElysiumGym::Build(T);
		const TArray<FCourse> Courses = Gym(Spec);
		int32 Leniency = 0;
		for (const FCourse& Course : Courses)
		{
			const ElysiumGym::FLane* Lane = Spec.FindLane(Course.GymLane);
			if (!Lane || (Lane->Family != ElysiumGym::EFamily::Ledge
				&& Lane->Family != ElysiumGym::EFamily::Landing))
			{
				TestFalse(FString::Printf(TEXT("'%s' does not time a press against an event"),
					*Course.Name.ToString()), Course.EventJump.IsSet());
				continue;
			}
			++Leniency;
			if (!TestTrue(FString::Printf(TEXT("'%s' times its press against an event"),
				*Course.Name.ToString()), Course.EventJump.IsSet()))
			{
				continue;
			}
			const EBodyEvent Expected = Lane->Family == ElysiumGym::EFamily::Ledge
				? EBodyEvent::GroundLost : EBodyEvent::GroundGained;
			TestTrue(FString::Printf(TEXT("'%s' watches the edge its family is about"),
				*Course.Name.ToString()), Course.EventJump->Event == Expected);
			TestEqual(FString::Printf(TEXT("'%s' carries its lane's frame offset"),
				*Course.Name.ToString()),
				Course.EventJump->FrameOffset, FMath::RoundToInt32(Lane->BracketUnits));
			// A committed bracket: nothing here may be deferred, or the measurement never lands.
			TestFalse(FString::Printf(TEXT("'%s' is committed, not deferred"),
				*Course.Name.ToString()), Course.bDeferBaseline);
		}
		TestEqual(TEXT("both leniency brackets are five rungs"), Leniency, 10);
	}

	return true;
}
#endif // !UE_BUILD_SHIPPING

// =====================================================================================
// The animation's per-direction speed (CCC7). The fan below is the male body's authored `walk`
// grid, in cm/s, read out of `docs/vtmb/animation_and_movers.md` — so what is asserted is the
// table's arithmetic against numbers the export produces, not the export itself, which is
// `Elysium.Content.GaitSpeeds`.
// =====================================================================================

namespace
{
	// `move_and_ranged`'s `walk`, cells 0..8 at move_yaw -180..+180 in 45-degree steps. Cells 0 and 8
	// are the same clip across the wrap seam.
	FElysiumGaitSpeedTable MaleWalkFan()
	{
		FElysiumGaitSpeedTable Fan;
		Fan.Count = 9;
		Fan.AxisMin = -180.0f;
		Fan.AxisMax = 180.0f;
		const float Authored[9] = { 88.6f, 113.9f, 97.1f, 88.1f, 136.7f, 88.1f, 60.7f, 113.9f, 88.6f };
		FMemory::Memcpy(Fan.Cells, Authored, sizeof(Authored));
		return Fan;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGaitSpeedsTest,
	"Elysium.Substrate.GaitSpeeds", GElysiumTestFlags)
bool FElysiumGaitSpeedsTest::RunTest(const FString&)
{
	// --- A table with nothing in it answers nothing, rather than answering zero convincingly -----
	const FElysiumGaitSpeedTable Empty;
	TestFalse(TEXT("a default table is not valid"), Empty.IsValid());
	TestEqual(TEXT("and it commands no speed"), Empty.SpeedAt(0.0f), 0.0f);
	TestEqual(TEXT("and it has no ceiling"), Empty.Peak(), 0.0f);

	FElysiumGaitSpeedTable Walk = MaleWalkFan();
	TestTrue(TEXT("the authored walk fan is valid"), Walk.IsValid());

	// --- The cells sit on the angles, and 0 lands exactly on cell 4 -----------------------------
	// This is the whole reason the axis is divided by `Count - 1` rather than by `Count`: the cells
	// are the range's endpoints, not its buckets.
	TestEqual(TEXT("forward is the 0-degree cell"), Walk.Forward(), 136.7f, 0.01f);
	TestEqual(TEXT("the backpedal is the wrap seam"), Walk.SpeedAt(-180.0f), 88.6f, 0.01f);
	TestEqual(TEXT("+180 is the same direction as -180"), Walk.SpeedAt(180.0f), 88.6f, 0.01f);
	TestEqual(TEXT("strafing right is the +90 cell"), Walk.SpeedAt(90.0f), 60.7f, 0.01f);
	TestEqual(TEXT("strafing left is the -90 cell"), Walk.SpeedAt(-90.0f), 97.1f, 0.01f);

	// --- Between two cells ----------------------------------------------------------------------
	// 20 degrees is 4/9ths of the way from cell 4 to cell 5. Retail would snap to cell 4 here — its
	// speed comes from digital keys and can only land on a cell — and a stick lands between two, so
	// the blend is the recorded divergence.
	const float Blended = FMath::Lerp(136.7f, 88.1f, 20.0f / 45.0f);
	TestEqual(TEXT("an angle between two cells blends them"), Walk.SpeedAt(20.0f), Blended, 0.01f);

	// --- The wrap, which is where an unwrapped index walks off the front ------------------------
	// A body 190 degrees off its facing is 170 degrees off it the other way. Written without the
	// double `Fmod` this indexes negatively and reads the wrong cell or crashes.
	TestEqual(TEXT("past the seam wraps rather than clamping"),
		Walk.SpeedAt(190.0f), Walk.SpeedAt(-170.0f), 0.01f);
	TestEqual(TEXT("and so does a full turn"), Walk.SpeedAt(360.0f), Walk.Forward(), 0.01f);
	TestEqual(TEXT("a turn and a bit is the bit"), Walk.SpeedAt(380.0f),
		Walk.SpeedAt(20.0f), 0.01f);

	// --- The ceiling ----------------------------------------------------------------------------
	TestEqual(TEXT("the peak is the largest cell"), Walk.Peak(), 136.7f, 0.01f);
	TestTrue(TEXT("and no direction can command more than it"),
		Walk.SpeedAt(-133.0f) <= Walk.Peak() && Walk.SpeedAt(47.0f) <= Walk.Peak());

	// --- The scale is a field, not a pre-multiply ------------------------------------------------
	// `sv_sneakscale` is 2.3, which is what makes retail's crouch faster than its walk.
	FElysiumGaitSpeedTable Sneak;
	Sneak.Count = 9;
	Sneak.AxisMin = -180.0f;
	Sneak.AxisMax = 180.0f;
	for (int32 Index = 0; Index < 9; ++Index)
	{
		Sneak.Cells[Index] = 70.0f;
	}
	Sneak.Cells[2] = 79.3f;
	Sneak.Scale = 2.3f;
	TestEqual(TEXT("the scale multiplies the peak"), Sneak.Peak(), 79.3f * 2.3f, 0.01f);
	TestEqual(TEXT("and every reading"), Sneak.Forward(), 70.0f * 2.3f, 0.01f);
	TestTrue(TEXT("so a scaled sneak outruns the authored walk"), Sneak.Forward() > Walk.Forward());

	// --- Symmetrization, which is a divergence and therefore has to be visible -------------------
	// The authored fan walks left half again as fast as it walks right. Averaging the mirrored pairs
	// removes that; the forward cell is unpaired and cannot move, and cells 0 and 8 are one clip so
	// averaging them is a no-op.
	const float MirroredMean = 0.5f * (97.1f + 60.7f);
	Walk.Symmetrize();
	TestEqual(TEXT("strafing right takes the mirrored mean"), Walk.SpeedAt(90.0f),
		MirroredMean, 0.01f);
	TestEqual(TEXT("and so does strafing left"), Walk.SpeedAt(-90.0f), MirroredMean, 0.01f);
	TestEqual(TEXT("forward is unpaired and does not move"), Walk.Forward(), 136.7f, 0.01f);
	TestEqual(TEXT("the seam cell is its own mirror"), Walk.SpeedAt(180.0f), 88.6f, 0.01f);

	// --- The set answers for the body as a whole -------------------------------------------------
	FElysiumGaitSpeeds Set;
	TestFalse(TEXT("an unresolved set is not valid"), Set.IsValid());
	Set.Walk = Walk;
	TestTrue(TEXT("one resolved gait is enough to steer by"), Set.IsValid());
	Set.Sneak = Sneak;
	TestEqual(TEXT("the set's ceiling spans every gait"), Set.Peak(),
		FMath::Max(Walk.Peak(), Sneak.Peak()), 0.01f);

	// --- The seam's decision table ---------------------------------------------------------------
	// The whole of what the mover asks. Asserted here rather than on the component because none of
	// it needs a pawn: it is a decision over body state, two cvars and a table.
	FElysiumGaitSpeeds Body;
	Body.Walk = MaleWalkFan();
	Body.Run = MaleWalkFan();
	for (int32 Index = 0; Index < 9; ++Index)
	{
		Body.Run.Cells[Index] *= 3.5f;      // a run fan, roughly the shipped ratio
	}
	Body.Sneak = MaleWalkFan();
	Body.Sneak.Scale = 2.3f;

	FElysiumWishSpeedInput In;
	In.JumpMaxSpeed = ElysiumMove::JumpMaxSpeed;
	In.NoclipSpeed = ElysiumMove::NoclipSpeed;

	// A body with no fan at all falls back to the shipped constants.
	const FElysiumGaitSpeeds NoFan;
	TestEqual(TEXT("a body with no fan runs at speed_runbase"),
		ElysiumGait::WishSpeedFrom(In, NoFan), ElysiumMove::RunSpeed, 0.01f);
	In.bWalkKey = true;
	TestEqual(TEXT("...and +speed selects the slow gait"),
		ElysiumGait::WishSpeedFrom(In, NoFan), ElysiumMove::WalkSpeed, 0.01f);
	In.bDucked = true;
	TestEqual(TEXT("...and a ducked body takes Source's third"),
		ElysiumGait::WishSpeedFrom(In, NoFan), ElysiumMove::WalkSpeed / 3.0f, 0.01f);
	In.bDucked = false;
	In.bWalkKey = false;

	// With a fan: the gait's own cells, at the commanded direction.
	TestEqual(TEXT("a body with a fan runs at the run fan's forward cell"),
		ElysiumGait::WishSpeedFrom(In, Body), Body.Run.Forward(), 0.01f);
	In.bWalkKey = true;
	TestEqual(TEXT("+speed reads the walk fan"),
		ElysiumGait::WishSpeedFrom(In, Body), Body.Walk.Forward(), 0.01f);
	In.bDucked = true;
	TestEqual(TEXT("a ducked body reads the sneak fan, with no third applied"),
		ElysiumGait::WishSpeedFrom(In, Body), Body.Sneak.Forward(), 0.01f);
	TestTrue(TEXT("...so the authored crouch outruns the authored walk, as retail's does"),
		Body.Sneak.Forward() > Body.Walk.Forward());
	In.bDucked = false;

	// The direction is the point: a strafe commands a different speed from a walk forward.
	In.WishYawDegrees = 90.0f;
	TestEqual(TEXT("a strafe commands its own cell"),
		ElysiumGait::WishSpeedFrom(In, Body), Body.Walk.SpeedAt(90.0f), 0.01f);
	TestTrue(TEXT("...which is slower than forward on this fan"),
		ElysiumGait::WishSpeedFrom(In, Body) < Body.Walk.Forward());
	In.WishYawDegrees = 0.0f;

	// The deflection scales it, so a half-pushed stick commands half the gait.
	In.Scale = 0.5f;
	TestEqual(TEXT("the command's deflection scales the answer"),
		ElysiumGait::WishSpeedFrom(In, Body), Body.Walk.Forward() * 0.5f, 0.01f);
	In.Scale = 1.0f;
	In.bWalkKey = false;

	// A gait that resolved no fan falls back on its own, not wholesale: this body walks and runs at
	// its authored speed and sneaks at the constant.
	FElysiumGaitSpeeds Partial = Body;
	Partial.Sneak = FElysiumGaitSpeedTable();
	In.bDucked = true;
	TestEqual(TEXT("a missing sneak fan falls back to the constant"),
		ElysiumGait::WishSpeedFrom(In, Partial), ElysiumMove::RunSpeed / 3.0f, 0.01f);
	In.bDucked = false;
	TestEqual(TEXT("...while the gaits that did resolve are unaffected"),
		ElysiumGait::WishSpeedFrom(In, Partial), Body.Run.Forward(), 0.01f);

	// Airborne: the held grounded speed, because retail's tables stop refreshing for the jump.
	// `sv_jump_maxspeed` is the ceiling and never fires — the run peak is below it.
	In.bOnGround = false;
	In.LastGroundedWishSpeed = 400.0f;
	TestEqual(TEXT("an airborne body keeps commanding what it left the ground with"),
		ElysiumGait::WishSpeedFrom(In, Body), 400.0f, 0.01f);
	In.LastGroundedWishSpeed = 0.0f;
	TestEqual(TEXT("...and falls back to sv_jump_maxspeed with nothing held"),
		ElysiumGait::WishSpeedFrom(In, Body), ElysiumMove::JumpMaxSpeed, 0.01f);
	In.bOnGround = true;

	// Noclip short-circuits the ladder: a crouched fly is not a sneak.
	In.bNoclip = true;
	In.bDucked = true;
	TestEqual(TEXT("noclip ignores the gait tables"),
		ElysiumGait::WishSpeedFrom(In, Body), ElysiumMove::NoclipSpeed, 0.01f);

	// Between two cells the answer is the blend, not the cell retail would have snapped to — the
	// recorded divergence, asserted so it cannot quietly revert.
	FElysiumWishSpeedInput Between;
	Between.WishYawDegrees = 20.0f;
	Between.bWalkKey = true;
	const float Blended20 = ElysiumGait::WishSpeedFrom(Between, Body);
	TestEqual(TEXT("an angle between two cells commands the blend"),
		Blended20, Body.Walk.SpeedAt(20.0f), 0.01f);
	TestTrue(TEXT("...which is not the cell retail would have snapped to"),
		!FMath::IsNearlyEqual(Blended20, Body.Walk.Forward(), 0.01f));

	return true;
}

// =====================================================================================
// The body sample (CCC1). One struct, two producers — so what is asserted here is the part of it
// that is a *rule* rather than a reading: how a world yaw becomes a facing-relative one, how
// Source's two duck flags become one stance, and how the jump phase falls out of the hold window
// and the vertical sign. A reading needs a body; a rule does not, and a rule the two producers
// disagreed about would be the contract failing quietly.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLocomotionSampleTest,
	"Elysium.Substrate.Locomotion", GElysiumTestFlags)
bool FElysiumLocomotionSampleTest::RunTest(const FString&)
{
	using namespace ElysiumLocomotion;

	// --- The relative yaw, which has to wrap ---------------------------------------------------
	// Every one of these is a body that is walking 20 degrees off its facing, or straight backwards.
	// Written without the wrap they come out as 340, -340 and -360.
	TestEqual(TEXT("a yaw right of facing is positive"), RelativeYaw(20.0f, 0.0f), 20.0f);
	TestEqual(TEXT("a yaw left of facing is negative"), RelativeYaw(-20.0f, 0.0f), -20.0f);
	TestEqual(TEXT("crossing north from the left"), RelativeYaw(10.0f, 350.0f), 20.0f);
	TestEqual(TEXT("crossing north from the right"), RelativeYaw(350.0f, 10.0f), -20.0f);
	TestEqual(TEXT("a full turn is no turn"), RelativeYaw(360.0f, 0.0f), 0.0f);
	// The backpedal, which is the boundary itself: it must land on one side and stay there.
	TestEqual(TEXT("straight backwards is the boundary"), FMath::Abs(RelativeYaw(180.0f, 0.0f)),
		180.0f);
	TestEqual(TEXT("and the boundary is reached the same way from either side"),
		FMath::Abs(RelativeYaw(0.0f, 180.0f)), 180.0f);

	// --- The pose parameter: recovered sign, plus a slew and a hold that are behaviour ------------
	// The three sign cases first. `RelativeYaw` is already right-positive with zero forward, which is
	// what the retail selector's reversed subtraction produces — so a strafe right is +90 and the
	// value maps onto `CalculateDirection` with no negation.
	{
		FElysiumLocomotionSample Body;
		Body.FacingYaw = 0.0f;
		Body.MoveYawPose = RelativeYaw(0.0f, Body.FacingYaw);
		TestEqual(TEXT("running forward is zero"), Body.MoveYaw(), 0.0f);
		Body.MoveYawPose = RelativeYaw(90.0f, Body.FacingYaw);
		TestEqual(TEXT("strafing right is +90"), Body.MoveYaw(), 90.0f);
		Body.MoveYawPose = RelativeYaw(-90.0f, Body.FacingYaw);
		TestEqual(TEXT("strafing left is -90"), Body.MoveYaw(), -90.0f);
		Body.MoveYawPose = RelativeYaw(180.0f, Body.FacingYaw);
		TestEqual(TEXT("backpedalling is the seam"), FMath::Abs(Body.MoveYaw()), 180.0f);
	}

	// The slew. 720 deg/s, so a sixteenth of a second covers 45 degrees and no more.
	{
		FElysiumMoveYawFilter Filter;
		// A fresh filter has never written, so its first moving frame snaps — that is what arms the
		// slew for the frames after it.
		TestEqual(TEXT("the first moving frame is where the body is going"),
			AdvanceMoveYaw(Filter, 30.0f, 100.0f, 1.0f / 60.0f), 30.0f, 0.01f);
		AdvanceMoveYaw(Filter, 0.0f, 100.0f, 1.0f / 16.0f);
		TestEqual(TEXT("...and the frame after it slews"), Filter.Value, 0.0f, 0.01f);

		const float Stepped = AdvanceMoveYaw(Filter, 90.0f, 100.0f, 1.0f / 16.0f);
		TestEqual(TEXT("a 90-degree turn is rationed to 45 in a sixteenth of a second"), Stepped,
			45.0f, 0.01f);
		TestTrue(TEXT("...so it has not arrived yet"), Stepped < 90.0f);
		// And it does arrive, rather than easing forever.
		for (int32 Frame = 0; Frame < 8; ++Frame)
		{
			AdvanceMoveYaw(Filter, 90.0f, 100.0f, 1.0f / 16.0f);
		}
		TestEqual(TEXT("...and it lands exactly on the target"), Filter.Value, 90.0f, 0.01f);
	}

	// The wrap: a turn across the seam takes the short way round, which is the whole reason this is
	// a fixed turn and not a lerp. Written as a lerp, -170 to +170 sweeps 340 degrees the wrong way.
	{
		FElysiumMoveYawFilter Filter;
		AdvanceMoveYaw(Filter, -170.0f, 100.0f, 1.0f / 60.0f);
		const float Crossed = AdvanceMoveYaw(Filter, 170.0f, 100.0f, 1.0f / 120.0f);
		TestTrue(TEXT("a turn across the seam goes the short way"), Crossed < -170.0f);
		TestTrue(TEXT("...staying on the near side of the boundary"), Crossed >= -180.0f);
	}

	// The hold. A body that stops keeps the direction it was going — retail's write is gated on
	// movement, so nothing is written and the parameter does not fall back to forward.
	{
		FElysiumMoveYawFilter Filter;
		AdvanceMoveYaw(Filter, 90.0f, 100.0f, 1.0f / 60.0f);
		TestEqual(TEXT("a strafing body's stride is sideways"), Filter.Value, 90.0f, 0.01f);
		const float Stopped = AdvanceMoveYaw(Filter, 0.0f, 0.0f, 1.0f / 60.0f);
		TestEqual(TEXT("and stopping holds it rather than snapping forward"), Stopped, 90.0f, 0.01f);
	}

	// The re-arm. Because the write is gated on movement, 0.3 s without one means the body has been
	// standing still — so a standing start snaps to the new direction instead of slewing into it,
	// which is what stops an eighth of a second of walking forward out of every sidestep.
	{
		FElysiumMoveYawFilter Filter;
		AdvanceMoveYaw(Filter, 0.0f, 100.0f, 1.0f / 60.0f);
		for (int32 Frame = 0; Frame < 30; ++Frame)      // half a second stationary
		{
			AdvanceMoveYaw(Filter, 0.0f, 0.0f, 1.0f / 60.0f);
		}
		TestEqual(TEXT("a standing start snaps to the direction it leaves in"),
			AdvanceMoveYaw(Filter, 90.0f, 100.0f, 1.0f / 60.0f), 90.0f, 0.01f);

		// A brief stumble is not a standing start, and slews.
		FElysiumMoveYawFilter Stumble;
		AdvanceMoveYaw(Stumble, 0.0f, 100.0f, 1.0f / 60.0f);
		AdvanceMoveYaw(Stumble, 0.0f, 0.0f, 0.1f);
		const float Slewed = AdvanceMoveYaw(Stumble, 90.0f, 100.0f, 1.0f / 60.0f);
		TestTrue(TEXT("a momentary stop still slews"), Slewed < 90.0f);
	}

	// --- The stance, which is four values because the flags are two -----------------------------
	// The pair that matters is the last one: the release edge sets `bDucking` while `bDucked` is
	// still true, and under a low ceiling the body stays there rather than passing through it. A
	// three-value stance reports that as an ordinary crouch and loses the stand-up entirely.
	TestEqual(TEXT("neither flag is standing"),
		static_cast<int32>(StanceFrom(false, false)),
		static_cast<int32>(EElysiumStance::Standing));
	TestEqual(TEXT("ducking alone is the duck ramp"),
		static_cast<int32>(StanceFrom(false, true)),
		static_cast<int32>(EElysiumStance::Lowering));
	TestEqual(TEXT("ducked alone is the settled crouch"),
		static_cast<int32>(StanceFrom(true, false)),
		static_cast<int32>(EElysiumStance::Ducked));
	TestEqual(TEXT("both is the unduck ramp, not a deeper crouch"),
		static_cast<int32>(StanceFrom(true, true)),
		static_cast<int32>(EElysiumStance::Rising));

	// --- The jump phase, derived rather than stored ----------------------------------------------
	FElysiumLocomotionSample S;
	S.bOnGround = true;
	S.LocalVelocity.Z = 400.0f;
	S.JumpHoldRemaining = 0.1f;
	TestEqual(TEXT("a grounded body is grounded whatever it carries"),
		static_cast<int32>(S.JumpPhase()), static_cast<int32>(EElysiumJumpPhase::Grounded));

	S.bOnGround = false;
	TestEqual(TEXT("rising is ascending"),
		static_cast<int32>(S.JumpPhase()), static_cast<int32>(EElysiumJumpPhase::Ascend));

	// The held push is the other half: VtMB's jump keeps pushing while the button is down, so the
	// window being open means ascending even on the frame the vertical sign has already turned.
	S.LocalVelocity.Z = -1.0f;
	TestEqual(TEXT("an open hold window is still ascending"),
		static_cast<int32>(S.JumpPhase()), static_cast<int32>(EElysiumJumpPhase::Ascend));

	S.JumpHoldRemaining = 0.0f;
	TestEqual(TEXT("falling with the window closed is descending"),
		static_cast<int32>(S.JumpPhase()), static_cast<int32>(EElysiumJumpPhase::Descend));

	// --- The speed is a derivation, so it cannot disagree with the velocity ----------------------
	S.LocalVelocity = FVector(30.0f, 40.0f, -900.0f);
	TestEqual(TEXT("speed is the planar magnitude and ignores the fall"), S.Speed2D(), 50.0f);

	// A default sample is a body standing still that is asking for nothing, and the zero wish scale
	// is what distinguishes that from a body asking to walk straight ahead.
	const FElysiumLocomotionSample Fresh;
	TestEqual(TEXT("a fresh sample is at rest"), Fresh.Speed2D(), 0.0f);
	TestEqual(TEXT("a fresh sample commands nothing"), Fresh.WishScale, 0.0f);
	TestEqual(TEXT("a fresh sample is standing"),
		static_cast<int32>(Fresh.Stance), static_cast<int32>(EElysiumStance::Standing));
	return true;
}

// =====================================================================================
// The recorded body trace (`Debug/ElysiumLocomotionTrace.h`). The file-on-disk half of the same
// contract the sample above states: two producers, one schema, one writer.
//
// The claim this holds is the one neither harness can hold by itself: that the cast's columns ARE
// the player's columns, that the writer fills every one of them, and that each lands the value its
// own record carries rather than a plausible neighbour. No world is needed for any of it — the
// writer takes the two published records and nothing else, which is the property being asserted.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLocomotionTraceTest,
	"Elysium.Substrate.LocomotionTrace", GElysiumTestFlags)
bool FElysiumLocomotionTraceTest::RunTest(const FString&)
{
	const TArrayView<const TCHAR* const> Shared = ElysiumLocomotionTrace::Channels();
	const TArray<const TCHAR*> CastColumns = FElysiumCastRun::DeclaredChannels();
	const TArray<const TCHAR*> PlayerColumns = FElysiumMoveRun::DeclaredChannels();

	// --- One schema, two producers -------------------------------------------------------------
	// Both producers declare the shared trace first, in the same order, and then whatever only they
	// can measure — the player its replayed command stream, the two mover answers and the camera
	// solve; the cast the speed its motor was commanding while the cell played. A SHARED column in
	// one and not the other would be the contract splitting, and this is where that shows.
	for (int32 Index = 0; Index < Shared.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("the cast carries shared column %d in the same place"), Index),
			CastColumns.IsValidIndex(Index) && FCString::Strcmp(CastColumns[Index], Shared[Index]) == 0);
		TestTrue(FString::Printf(TEXT("the player carries shared column %d in the same place"), Index),
			PlayerColumns.IsValidIndex(Index) && FCString::Strcmp(PlayerColumns[Index], Shared[Index]) == 0);
	}
	TestTrue(TEXT("...and each producer adds columns of its own"),
		CastColumns.Num() > Shared.Num() && PlayerColumns.Num() > Shared.Num());
	// The cast's own column is the commanded speed, named here so adding a second one is a
	// deliberate edit rather than a silent divergence from the shared schema.
	TestEqual(TEXT("the cast adds exactly one column"), CastColumns.Num(), Shared.Num() + 1);
	TestEqual(TEXT("...and it is the commanded speed"),
		FString(CastColumns.Last()), FString(TEXT("act_cmd")));

	// Both open, which is the registry's own gate: a name with no comparison rule cannot be a column.
	{
		FElysiumChannelRecorder Opened;
		FString OpenError;
		TestTrue(TEXT("the cast's columns are all registered"), Opened.Open(CastColumns, OpenError));
		TestTrue(TEXT("the player's columns are all registered"), Opened.Open(PlayerColumns, OpenError));
	}

	// --- The writer fills the whole schema -----------------------------------------------------
	// `EndFrame` refuses a frame with an unwritten column, so a clean frame IS the assertion that
	// the writer covers every channel it declares — the day a column is added and not written, this
	// fails rather than a hole appearing in a CSV somewhere.
	FElysiumChannelRecorder Recorder;
	FString Error;
	if (!TestTrue(TEXT("the shared trace opens"), Recorder.Open(Shared, Error)))
	{
		return false;
	}

	FElysiumLocomotionSample Sample;
	// 2.54 cm to the unit, so 254 cm/s is a round 100 u/s — the columns are emitted in Source units
	// and these numbers are what prove the conversion rather than restating it.
	Sample.FacingYaw = 90.0f;
	Sample.LocalVelocity = FVector(254.0f, 0.0f, 0.0f);
	Sample.MoveYawWish = 30.0f;
	Sample.MoveYawVelocity = 20.0f;
	Sample.MoveYawPose = 20.0f;      // the sample's unfiltered value; the record's is what is written
	Sample.bOnGround = true;
	Sample.Water = EElysiumWaterLevel::Waist;
	// The unduck ramp: the one stance that is BOTH ducked and ducking, which is what proves the two
	// columns are Source's own pair rather than a three-value collapse.
	Sample.Stance = EElysiumStance::Rising;

	FElysiumAnimationSelection Selection;
	Selection.RequestedActivity = TEXT("ACT_WALK");
	Selection.ResolvedActivity = TEXT("ACT_WALK_PISTOL");
	Selection.SequenceLabel = TEXT("pistol_walk");
	Selection.AnimationName = TEXT("pistol_walk_0");
	Selection.OwnerStem = TEXT("male_shared");
	Selection.Stem = TEXT("some_body");
	Selection.Route = EElysiumAnimRoute::Activity;
	Selection.Outcome = EElysiumAnimOutcome::Resolved;
	Selection.AssetKind = EElysiumAnimAssetKind::Sequence;
	Selection.GraphState = EElysiumGraphState::Walk;
	Selection.AirPhase = EElysiumAirPhase::Grounded;
	Selection.Generation = 7;
	Selection.MoveYaw = 45.0f;       // the driver's filtered answer, which is what the trace records
	Selection.GroundSpeedCmPerSecond = 254.0f;
	Selection.FadeSeconds = 0.25f;

	Recorder.BeginFrame();
	ElysiumLocomotionTrace::Frame(Recorder, 1.0f / 60.0f, FVector(254.0f, 508.0f, 0.0f),
		FVector(0.0f, 254.0f, 0.0f), Sample, Selection);
	TestTrue(TEXT("the writer leaves no declared column unwritten"), Recorder.EndFrame(Error));

	ElysiumLocomotionTrace::FTotals Totals;
	Totals.Observe(Sample, Selection);

	// A second frame, carrying the one value that is not a reading: a miss counts as a fallback.
	FElysiumAnimationSelection Missed = Selection;
	Missed.Outcome = EElysiumAnimOutcome::MissingSequence;
	Missed.RequestedActivity = TEXT("ACT_SNEAK");
	Recorder.BeginFrame();
	ElysiumLocomotionTrace::Frame(Recorder, 1.0f / 60.0f, FVector::ZeroVector, FVector::ZeroVector,
		Sample, Missed);
	TestTrue(TEXT("a second frame is accepted"), Recorder.EndFrame(Error));
	Totals.Observe(Sample, Missed);
	Totals.Write(Recorder);

	// --- What landed where ----------------------------------------------------------------------
	FString Csv;
	FString Manifest;
	Recorder.Serialize(Csv, Manifest);

	TArray<FString> Lines;
	Csv.ParseIntoArrayLines(Lines);
	if (!TestEqual(TEXT("a header and one line per frame"), Lines.Num(), 3))
	{
		return false;
	}
	TArray<FString> Header;
	Lines[0].ParseIntoArray(Header, TEXT(","));
	TArray<FString> Row;
	Lines[1].ParseIntoArray(Row, TEXT(","));

	auto Column = [&Header, &Row](const TCHAR* Name) -> FString
	{
		const int32 Index = Header.IndexOfByKey(FString(Name));
		return Row.IsValidIndex(Index) ? Row[Index] : FString();
	};

	TestEqual(TEXT("the origin is emitted in Source units"), Column(TEXT("px")),
		FString(TEXT("100.0000")));
	TestEqual(TEXT("...and so is the velocity"), Column(TEXT("vy")), FString(TEXT("100.0000")));
	TestEqual(TEXT("the speed comes off the sample, not off the velocity argument"),
		Column(TEXT("speed2d")), FString(TEXT("100.0000")));
	TestEqual(TEXT("the unduck ramp reports ducked"), Column(TEXT("ducked")), FString(TEXT("1")));
	TestEqual(TEXT("...and ducking, both at once, which is the whole point of the pair"),
		Column(TEXT("ducking")), FString(TEXT("1")));
	TestEqual(TEXT("the water level rides as its ordinal"), Column(TEXT("water")),
		FString::FromInt(static_cast<int32>(EElysiumWaterLevel::Waist)));
	TestEqual(TEXT("the pose parameter is the record's, not the sample's"),
		Column(TEXT("move_yaw")), FString(TEXT("45.000")));
	TestEqual(TEXT("...with the sample's own two yaws beside it"),
		Column(TEXT("move_yaw_vel")), FString(TEXT("20.000")));
	TestEqual(TEXT("the activity code is the LOGICAL request's"), Column(TEXT("act_code")),
		FString::FromInt(static_cast<int32>(EElysiumAnimActivityCode::Walk)));
	TestEqual(TEXT("the graph state is the one the record named"), Column(TEXT("act_state")),
		FString::FromInt(static_cast<int32>(EElysiumGraphState::Walk)));
	TestEqual(TEXT("the stride is the selected cell's own speed, in Source units"),
		Column(TEXT("act_stride")), FString(TEXT("100.0000")));
	TestEqual(TEXT("the generation rides, so a reader can tell a re-resolve from a hold"),
		Column(TEXT("act_gen")), FString(TEXT("7")));

	// --- The totals, which are what a baseline actually compares --------------------------------
	TestEqual(TEXT("a clean resolve counts as resolved"), Totals.ResolvedFrames, 1);
	TestEqual(TEXT("and a miss counts as a fallback"), Totals.FallbackFrames, 1);
	TestTrue(TEXT("both activities are in the reached mask"),
		(Totals.CodesSeen & (1u << static_cast<uint32>(EElysiumAnimActivityCode::Walk))) != 0
		&& (Totals.CodesSeen & (1u << static_cast<uint32>(EElysiumAnimActivityCode::Sneak))) != 0);
	TestEqual(TEXT("the peak speed is in Source units"), Totals.PeakSpeed2D, 100.0, 0.001);
	TestTrue(TEXT("the run channels reach the manifest"),
		Manifest.Contains(TEXT("\"name\": \"act_resolved\"")));
	TestTrue(TEXT("the bank the selection resolved through rides as metadata"),
		Manifest.Contains(TEXT("\"anim_banks\": \"male_shared\"")));
	TestTrue(TEXT("...and so does the identity it resolved to"),
		Manifest.Contains(TEXT("ACT_WALK_PISTOL=pistol_walk@male_shared:pistol_walk_0")));
	// There is no string channel by design, so an identity must never have become a column.
	TestEqual(TEXT("no identity leaked into the CSV"),
		Header.IndexOfByKey(FString(TEXT("anim_banks"))), INDEX_NONE);
	return true;
}

// ================================================================================================
// The combat arena's spec (`Debug/ElysiumArenaSpec.h`)
// ================================================================================================
//
// **Nothing below restates a dimension.** An expectation recomputed from the same constants as the
// geometry moves with the geometry and can never turn red — the rule the gym spec states and the
// reason its own test asserts structure rather than numbers. What is asserted here is the set of
// geometric CLAIMS the room makes that a changed dimension could quietly falsify: that every place
// a body is put is inside the walls and outside the solids, and that an anchor advertised as cover
// is genuinely against the block.
//
// The last one is the load-bearing claim. `FAnchor::bAgainstCover` is what the panel colours green
// and what makes an anchor mean "hidden" rather than "somewhere to stand". Note what it is NOT
// tested as: "the block lies between this anchor and the room's middle" is true of every point in
// the room, because the block IS the middle — a probe that reads as meaningful and measures
// nothing. Adjacency is the property that actually produces cover, and it is what breaks if the
// setback, the block or the room's size moves.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumArenaSpecTest, "Elysium.Substrate.ArenaSpec",
	GElysiumTestFlags)
bool FElysiumArenaSpecTest::RunTest(const FString&)
{
	const ElysiumArena::FSpec Spec = ElysiumArena::Build();

	TestTrue(TEXT("the arena has solids"), Spec.Solids.Num() > 0);
	TestTrue(TEXT("the arena has spawn pads"), Spec.Pads.Num() > 0);
	TestTrue(TEXT("the arena has anchors"), Spec.Anchors.Num() > 0);

	// --- Structural invariants the builder depends on ---------------------------------------------
	{
		TSet<FName> SolidTags;
		for (const ElysiumArena::FSolid& Solid : Spec.Solids)
		{
			// The tag becomes the spawned component's name, so a duplicate is a component the engine
			// silently renames and a trace hit that reads back as the wrong part of the room.
			TestFalse(FString::Printf(TEXT("solid tag %s is unique"), *Solid.Tag.ToString()),
				SolidTags.Contains(Solid.Tag));
			SolidTags.Add(Solid.Tag);
			TestTrue(FString::Printf(TEXT("solid %s has a positive extent"), *Solid.Tag.ToString()),
				Solid.Extent.GetMin() > 0.0f);
		}
		TSet<FName> PadNames;
		for (const ElysiumArena::FPad& Pad : Spec.Pads)
		{
			TestFalse(FString::Printf(TEXT("pad %s is unique"), *Pad.Name.ToString()),
				PadNames.Contains(Pad.Name));
			PadNames.Add(Pad.Name);
			TestNotNull(FString::Printf(TEXT("pad %s is findable"), *Pad.Name.ToString()),
				Spec.FindPad(Pad.Name));
		}
		TSet<FName> AnchorNames;
		for (const ElysiumArena::FAnchor& Anchor : Spec.Anchors)
		{
			TestFalse(FString::Printf(TEXT("anchor %s is unique"), *Anchor.Name.ToString()),
				AnchorNames.Contains(Anchor.Name));
			AnchorNames.Add(Anchor.Name);
			TestNotNull(FString::Printf(TEXT("anchor %s is findable"), *Anchor.Name.ToString()),
				Spec.FindAnchor(Anchor.Name));
		}
	}

	// The two solids every placement test below is stated against, found by the tags the builder
	// spawns them under rather than rebuilt from the constants.
	const ElysiumArena::FSolid* Cover = Spec.Solids.FindByPredicate(
		[](const ElysiumArena::FSolid& S) { return S.Tag == FName(TEXT("cover")); });
	const ElysiumArena::FSolid* Floor = Spec.Solids.FindByPredicate(
		[](const ElysiumArena::FSolid& S) { return S.Tag == FName(TEXT("floor")); });
	if (!TestNotNull(TEXT("the arena has a cover solid"), Cover)
		|| !TestNotNull(TEXT("the arena has a floor plate"), Floor))
	{
		return false;
	}

	// The plate's TOP is the arena's Z origin, which is what makes every feet-anchored point in the
	// room sit at zero. A plate that drifted off it would put every pad and anchor underground.
	TestEqual(TEXT("the floor's top surface is the arena's Z origin"),
		static_cast<float>(Floor->Center.Z + Floor->Extent.Z), 0.0f);

	// --- Every placed point is on the floor, inside the walls, and outside the solids -------------
	// Walls are excluded from the containment test and used as its bound instead: the interior is
	// whatever the four walls leave, so asking whether a point is inside a wall and asking whether
	// it is inside the room are the same question asked twice.
	auto InsideXY = [](const ElysiumArena::FSolid& Solid, const FVector& Point, float Margin)
	{
		return FMath::Abs(Point.X - Solid.Center.X) < Solid.Extent.X + Margin
			&& FMath::Abs(Point.Y - Solid.Center.Y) < Solid.Extent.Y + Margin;
	};
	// A capsule's own radius, near enough: a point that clears the block by less than this is a body
	// spawned interpenetrating it.
	constexpr float BodyRadiusCm = 40.0f;

	TArray<TPair<FString, FVector>> Placed;
	for (const ElysiumArena::FPad& Pad : Spec.Pads)
	{
		Placed.Emplace(FString::Printf(TEXT("pad %s"), *Pad.Name.ToString()), Pad.FeetOrigin);
	}
	for (const ElysiumArena::FAnchor& Anchor : Spec.Anchors)
	{
		Placed.Emplace(FString::Printf(TEXT("anchor %s"), *Anchor.Name.ToString()),
			Anchor.FeetOrigin);
	}
	Placed.Emplace(TEXT("the player's start"), Spec.PlayerFeet);

	for (const TPair<FString, FVector>& Entry : Placed)
	{
		TestEqual(FString::Printf(TEXT("%s stands on the floor"), *Entry.Key),
			static_cast<float>(Entry.Value.Z), Spec.FloorZ());
		TestTrue(FString::Printf(TEXT("%s is over the floor plate"), *Entry.Key),
			InsideXY(*Floor, Entry.Value, -BodyRadiusCm));
		TestFalse(FString::Printf(TEXT("%s is not inside the cover block"), *Entry.Key),
			InsideXY(*Cover, Entry.Value, BodyRadiusCm));
		for (const ElysiumArena::FSolid& Solid : Spec.Solids)
		{
			if (Solid.Tag == FName(TEXT("floor")) || Solid.Tag == FName(TEXT("cover")))
			{
				continue;
			}
			TestFalse(FString::Printf(TEXT("%s is not inside %s"), *Entry.Key, *Solid.Tag.ToString()),
				InsideXY(Solid, Entry.Value, BodyRadiusCm));
		}
	}

	// --- The cover claim --------------------------------------------------------------------------
	// A shadow anchor's segment to the room's middle must pass through the block; a non-shadow
	// anchor's must not. "Against" is the gap between the anchor and the nearest face of the block:
	// a body's width or so is cover, and anything a stride away is not.
	auto GapToCover = [&Cover](const FVector& Point)
	{
		// The Chebyshev-style gap to an axis-aligned box in the horizontal plane. Negative inside.
		return FMath::Max(
			FMath::Abs(Point.X - Cover->Center.X) - Cover->Extent.X,
			FMath::Abs(Point.Y - Cover->Center.Y) - Cover->Extent.Y);
	};
	// One body's width off the face is against it; the corner set has to be well clear, so the two
	// bands are separated rather than sharing one threshold that a nudge could cross.
	constexpr double AgainstCm = 2.0 * BodyRadiusCm;
	constexpr double ClearCm = 6.0 * BodyRadiusCm;

	int32 CoverAnchors = 0;
	for (const ElysiumArena::FAnchor& Anchor : Spec.Anchors)
	{
		const double Gap = GapToCover(Anchor.FeetOrigin);
		if (Anchor.bAgainstCover)
		{
			++CoverAnchors;
			TestTrue(FString::Printf(
				TEXT("anchor %s claims cover and stands against the block (gap %.0f cm)"),
				*Anchor.Name.ToString(), Gap), Gap > 0.0 && Gap < AgainstCm);
			// A cover anchor also has to outrank a bare one, because that ordering is the whole of
			// how `PickHighestRatedCandidate` is steered toward cover — the selector reads the
			// rating and knows nothing about the block.
			for (const ElysiumArena::FAnchor& Other : Spec.Anchors)
			{
				if (!Other.bAgainstCover)
				{
					TestTrue(TEXT("a cover anchor outranks a bare one"), Anchor.Rating > Other.Rating);
				}
			}
		}
		else
		{
			TestTrue(FString::Printf(
				TEXT("anchor %s claims no cover and is clear of the block (gap %.0f cm)"),
				*Anchor.Name.ToString(), Gap), Gap > ClearCm);
		}
	}
	TestTrue(TEXT("the room offers at least one cover anchor"), CoverAnchors > 0);

	// The player starts in the open. A fight that begins with the player already behind the only
	// solid in the room is a fight whose acquisition never happens.
	TestTrue(TEXT("the player starts clear of the block"), GapToCover(Spec.PlayerFeet) > ClearCm);

	// --- One pad that is hidden from the player's start --------------------------------------------
	// Named `behind_cover` and required to earn it. Unlike the adjacency band above this IS a sight
	// line, and it is a meaningful one: the player's start is off to one side rather than at the
	// block, so the segment between the two either passes through the solid or it does not.
	// Spawning a character the player cannot yet see is the only way to watch an acquisition happen
	// rather than start already acquired.
	if (const ElysiumArena::FPad* Behind = Spec.FindPad(FName(TEXT("behind_cover"))))
	{
		constexpr int32 Samples = 128;
		bool bBlocked = false;
		for (int32 Index = 1; Index < Samples && !bBlocked; ++Index)
		{
			const FVector Point = FMath::Lerp(Spec.PlayerFeet, Behind->FeetOrigin,
				static_cast<float>(Index) / Samples);
			bBlocked = InsideXY(*Cover, Point, 0.0f);
		}
		TestTrue(TEXT("behind_cover is out of sight from the player's start"), bBlocked);
	}
	else
	{
		AddError(TEXT("the arena has no behind_cover pad"));
	}

	return true;
}

} // namespace ElysiumMovementTests

#endif // WITH_DEV_AUTOMATION_TESTS
