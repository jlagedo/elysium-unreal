// Content-free Substrate automation: camera solve, reconstruction rig, authored tracks, shots, and published view state.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "ElysiumAppState.h"
#include "ElysiumAudioLatency.h"
#include "ElysiumBinds.h"
#include "ElysiumBrushComponent.h"
#include "Player/ElysiumCameraShots.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraRig.h"
#include "ElysiumCameraSolve.h"
#include "Substrate/ElysiumCameraTrack.h"
#include "ElysiumChoreoSettings.h"
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
#include "ElysiumGaitSpeeds.h"               // the animation's per-direction speed
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumGymSpec.h"
#include "Visual/ElysiumPoseDeviation.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumLineService.h"
#include "ElysiumLookCurve.h"                // the mouse path's pure rules
#include "Debug/ElysiumMoveCourses.h"        // the event-timed press's pure half
#include "ElysiumMapActor.h"
#include "ElysiumMapEpoch.h"
#include "Map/ElysiumFeedTargeting.h"
#include "Map/ElysiumMapCollision.h"
#include "ElysiumSoundCache.h"
#include "ElysiumMovementComponent.h"
#include "Visual/ElysiumObjModel.h"
#include "Visual/ElysiumNpcClips.h"
#include "ElysiumLocomotionSample.h"         // the body sample's pure rules
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
namespace ElysiumCameraTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The camera — the weight driver and the scripted-shot channel.
//
// VtMB ships one camera with a blend weight, not two cameras, and the whole first<->third
// transition is that weight ramping at a fixed rate with a priority order over three latches
// (`docs/vtmb/camera-view-modes.md` §3). That is a pure function of time and flags, so it is asserted here
// with no pawn, no world and no RHI — which is also what makes "0 -> 1 in 0.5 s" a number rather
// than a stopwatch.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraTest, "Elysium.Substrate.Camera", GElysiumTestFlags)
bool FElysiumCameraTest::RunTest(const FString&)
{
	// Advance a weight block in fixed steps, the way a frame would.
	auto Run = [](FElysiumCameraWeights& W, float Seconds, float Step, float TimeScale = 1.0f)
	{
		for (float T = 0.0f; T < Seconds - KINDA_SMALL_NUMBER; T += Step)
		{
			W.Advance(Step, TimeScale);
		}
	};

	// --- the transition is 0.5 s in both directions, at 2.0/s ---
	{
		FElysiumCameraWeights W;
		TestEqual(TEXT("a fresh camera is first person"), W.Third, 0.0f);
		TestFalse(TEXT("and reports first person"), W.IsThirdPerson());

		W.bUserThird = true;
		Run(W, 0.25f, 1.0f / 60.0f);
		TestTrue(TEXT("halfway through the blend the weight is mid-ramp"),
			W.Third > 0.4f && W.Third < 0.6f);
		TestTrue(TEXT("but it already reports third person -- the predicate is a disjunction"),
			W.IsThirdPerson());

		Run(W, 0.30f, 1.0f / 60.0f);
		TestEqual(TEXT("a full traversal takes 0.5 s and clamps at 1"), W.Third, 1.0f);

		W.bUserThird = false;
		Run(W, 0.50f, 1.0f / 60.0f);
		TestEqual(TEXT("and 0.5 s back down, clamped at 0"), W.Third, 0.0f);
		TestFalse(TEXT("only at exactly 0 does it stop being third person"), W.IsThirdPerson());
	}

	// --- the frame rate does not change the duration ---
	{
		FElysiumCameraWeights Fast, Slow;
		Fast.bUserThird = Slow.bUserThird = true;
		Run(Fast, 0.5f, 1.0f / 240.0f);
		Run(Slow, 0.5f, 1.0f / 30.0f);
		TestEqual(TEXT("240 Hz and 30 Hz reach the same place"), Fast.Third, Slow.Third);
	}

	// --- it is scaled by the player's time scale, not wall-clock ---
	{
		FElysiumCameraWeights W;
		W.bUserThird = true;
		Run(W, 0.5f, 1.0f / 60.0f, /*TimeScale*/ 0.5f);
		TestTrue(TEXT("at half time scale half a second gets halfway"),
			FMath::IsNearlyEqual(W.Third, 0.5f, 0.02f));
		Run(W, 0.5f, 1.0f / 60.0f, 0.5f);
		TestEqual(TEXT("and the full second finishes it"), W.Third, 1.0f);

		FElysiumCameraWeights Held;
		Held.bUserThird = true;
		Run(Held, 1.0f, 1.0f / 60.0f, /*TimeScale*/ 0.0f);
		TestEqual(TEXT("a held world does not blend at all"), Held.Third, 0.0f);
	}

	// --- symmetric and interruptible: reversing mid-blend resumes, it does not restart ---
	// This is the FIRST/THIRD weight's own symmetry — an integrator with no transition object, so
	// there is nothing to restart. It is NOT retail's camera-override reversal, which the plan and
	// `server_cine_camera.md` §7 both used to point here: that one is `FUN_1017d0b0`'s back-dated
	// mark, it lives on `FElysiumCameraOverrideChannel::Arm`, and it is asserted on the stored
	// `mark`/`duration` pair by `Elysium.Substrate.CameraOverride` (SC3).
	{
		FElysiumCameraWeights W;
		W.bUserThird = true;
		Run(W, 0.25f, 1.0f / 60.0f);
		const float Mid = W.Third;

		W.bUserThird = false;
		Run(W, 0.10f, 1.0f / 60.0f);
		TestTrue(TEXT("reversing continues from the current weight"), W.Third < Mid && W.Third > 0.0f);

		// Turning it straight back on and running the *remaining* time finishes the blend: there is no
		// transition object holding a start snapshot, so nothing restarts.
		W.bUserThird = true;
		Run(W, 0.5f, 1.0f / 60.0f);
		TestEqual(TEXT("and re-reversing finishes without restarting"), W.Third, 1.0f);
	}

	// --- the priority order: forced-third and feed win, then forced-first, then the user toggle ---
	{
		FElysiumCameraWeights W;
		W.bUserThird = false;
		W.bForcedThird = true;
		Run(W, 0.5f, 1.0f / 60.0f);
		TestEqual(TEXT("forced-third outranks the user's first-person toggle"), W.Third, 1.0f);
		TestEqual(TEXT("and says so"), FString(W.Driver()), FString(TEXT("forced-third")));

		W.bForcedThird = false;
		W.bForcedFirst = true;
		W.bUserThird = true;
		Run(W, 0.5f, 1.0f / 60.0f);
		TestEqual(TEXT("forced-first outranks the user's third-person toggle"), W.Third, 0.0f);

		// The feed camera raises its own weight, and that alone holds third person.
		W.bForcedFirst = false;
		W.bUserThird = false;
		W.bFeed = true;
		Run(W, 0.5f, 1.0f / 60.0f);
		TestEqual(TEXT("the feed camera drives the third weight up"), W.Third, 1.0f);
		TestEqual(TEXT("and is the deciding driver"), FString(W.Driver()), FString(TEXT("feed")));
		TestTrue(TEXT("the feed channel itself takes one second"),
			FMath::IsNearlyEqual(W.Feed, 0.5f, 0.02f));
		Run(W, 0.5f, 1.0f / 60.0f);
		TestEqual(TEXT("and clamps at full weight after that second"), W.Feed, 1.0f);
	}

	// --- the ordinary feed solver: entry yaw, exponential pitch and square-root dolly ---------
	{
		FElysiumCameraCvars Cvars;
		const FElysiumFeedCameraPose Start =
			ElysiumCam::SolveOrdinaryFeedCamera(0.0f, 15.0f, Cvars);
		TestTrue(TEXT("feed starts on the captured yaw"),
			FMath::IsNearlyEqual(Start.Rotation.Yaw, 15.0f));
		TestTrue(TEXT("feed starts at zero pitch and offset"),
			Start.Rotation.Pitch == 0.0f && Start.Offset.IsNearlyZero());

		const FElysiumFeedCameraPose One =
			ElysiumCam::SolveOrdinaryFeedCamera(1.0f, 15.0f, Cvars);
		TestTrue(TEXT("one second advances the automatic orbit by camfeed_yaw"),
			FMath::IsNearlyEqual(One.Rotation.Yaw, 65.0f, 0.001f));
		const float ExpectedPitch = -(80.0f - 80.0f * FMath::Pow(2.0f, -0.35f));
		TestTrue(TEXT("pitch follows the recovered exponential"),
			FMath::IsNearlyEqual(One.Rotation.Pitch, ExpectedPitch, 0.001f));
		TestTrue(TEXT("one second places the eye fifty Source units behind its solved forward"),
			FMath::IsNearlyEqual(One.Offset.Size(), 50.0f * ElysiumCam::U, 0.01f));

		const FElysiumFeedCameraPose Clamped =
			ElysiumCam::SolveOrdinaryFeedCamera(6.0f, 15.0f, Cvars);
		TestTrue(TEXT("the feed pitch reaches its sixty-degree clamp"),
			FMath::IsNearlyEqual(Clamped.Rotation.Pitch, -60.0f, 0.001f));
	}

	// --- a scripted camera counts as third person, which is what draws the player model under it ---
	{
		FElysiumCameraWeights W;
		W.Scripted = 0.3f;
		TestTrue(TEXT("a scripted shot reads as third person"), W.IsThirdPerson());
		W.Scripted = 0.0f;
		W.Secondary = 0.2f;
		TestTrue(TEXT("so does the secondary weight"), W.IsThirdPerson());
		Run(W, 0.5f, 1.0f / 60.0f);
		TestTrue(TEXT("which decays at 0.5/s"), FMath::IsNearlyEqual(W.Secondary, 0.2f - 0.25f * 1.0f, 0.02f)
			|| W.Secondary == 0.0f);
	}

	// --- first person draws no player body, and nothing composed over the view may reopen it ---
	{
		FElysiumCameraCvars Cvars;
		Cvars.IdealDist = 100.0f;
		Cvars.FadeStart = 80.0f;
		Cvars.FadeEnd = 20.0f;
		FElysiumCameraWeights W;
		TestEqual(TEXT("true first person hides the player body"),
			ElysiumCam::SolveModelAlpha(FVector(100.0f, 0.0f, 0.0f), W, Cvars), 0.0f);
		W.Scripted = 0.5f;
		TestEqual(TEXT("a scripted shot over a first-person view still draws no body"),
			ElysiumCam::SolveModelAlpha(FVector(100.0f, 0.0f, 0.0f), W, Cvars), 0.0f);
		W.Scripted = 1.0f;
		TestEqual(TEXT("not even at full shot weight — a visible cutscene player is the stand-in"),
			ElysiumCam::SolveModelAlpha(FVector(100.0f, 0.0f, 0.0f), W, Cvars), 0.0f);
		W.Scripted = 0.0f;
		W.Third = 1.0f;
		TestEqual(TEXT("ordinary third person still uses the recovered distance band"),
			ElysiumCam::SolveModelAlpha(FVector(80.0f, 0.0f, 0.0f), W, Cvars), 1.0f);
		TestEqual(TEXT("and still fades out as the boom closes on the eye"),
			ElysiumCam::SolveModelAlpha(FVector(20.0f, 0.0f, 0.0f), W, Cvars), 0.0f);
		W.Scripted = 1.0f;
		TestEqual(TEXT("a shot over a third-person view reads the boom, not the shot weight"),
			ElysiumCam::SolveModelAlpha(FVector(20.0f, 0.0f, 0.0f), W, Cvars), 0.0f);
	}

	// --- the fade band reads THIS frame's boom, not the one before it ---
	// The band is a function of how far the camera ended up from the eye, so it can only be solved
	// after the rig has swept and damped. Evaluating it in phase one made every alpha describe the
	// previous frame and made `Sample.ModelAlpha` disagree with `Sample.BoomLength` in one record.
	{
		UElysiumCameraComponent* Phased = NewObject<UElysiumCameraComponent>();
		TestNotNull(TEXT("the phase-order case has a camera component"), Phased);
		if (Phased)
		{
			Phased->SetThirdPerson(true);
			// 2.0/s for half a second is the full traversal, so the weight is at 1 and the band is live.
			Phased->AdvanceFrame(0.5f);
			TestEqual(TEXT("phase one advances the third-person weight"),
				Phased->ThirdPersonWeight(), 1.0f);
			TestEqual(TEXT("and leaves the band alone, because no rig has solved a boom yet"),
				Phased->ModelAlpha(), 0.0f);

			// The rig lands between the phases, well past `min(cam_idealdist, cam_fadestart)`.
			Phased->SetSolvedBoom(FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator, false);
			Phased->FinalizeFrame();
			TestEqual(TEXT("phase two reads the boom the rig solved in the same frame"),
				Phased->ModelAlpha(), 1.0f);

			// `ApplyCameraModifiers` runs once per view target, so a view-target blend reaches this
			// path twice at the same delta. The second pass must read the frame's answer.
			Phased->SetSolvedBoom(FVector::ZeroVector, FRotator::ZeroRotator, false);
			Phased->FinalizeFrame();
			TestEqual(TEXT("phase two is idempotent within a frame"),
				Phased->ModelAlpha(), 1.0f);

			// **The unguarded door does not eat the frame.** A scene capture or a spectator can reach
			// the fallback before the camera manager runs, and it must not leave the manager's own
			// post-rig call a no-op — that is what made the alpha describe the previous frame's boom.
			Phased->FinalizeFrameUnguarded();
			TestEqual(TEXT("the unguarded door re-solves from the boom in force"),
				Phased->ModelAlpha(), 0.0f);
			Phased->SetSolvedBoom(FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator, false);
			Phased->FinalizeFrameUnguarded();
			TestEqual(TEXT("and a later rig solve still reaches the band through it"),
				Phased->ModelAlpha(), 1.0f);
		}
	}

	// --- the easing is at the point of use, not in the ramp ---
	{
		TestEqual(TEXT("SimpleSpline(0)"), ElysiumCam::SimpleSpline(0.0f), 0.0f);
		TestEqual(TEXT("SimpleSpline(1)"), ElysiumCam::SimpleSpline(1.0f), 1.0f);
		TestEqual(TEXT("SimpleSpline(0.5) is its own midpoint"), ElysiumCam::SimpleSpline(0.5f), 0.5f);
		TestTrue(TEXT("and it eases in below the midpoint"), ElysiumCam::SimpleSpline(0.25f) < 0.25f);
		TestTrue(TEXT("and out above it"), ElysiumCam::SimpleSpline(0.75f) > 0.75f);
		TestEqual(TEXT("it clamps rather than extrapolating"), ElysiumCam::SimpleSpline(2.0f), 1.0f);
	}

	// --- the rate-limited approach: clamp the step, snap when inside it, wrap in angle space ---
	{
		TestEqual(TEXT("a step larger than the gap arrives"),
			ElysiumCam::Approach(0.0f, 10.0f, 1000.0f, 1.0f), 10.0f);
		TestEqual(TEXT("a step smaller than the gap is clamped"),
			ElysiumCam::Approach(0.0f, 10.0f, 4.0f, 1.0f), 4.0f);
		TestEqual(TEXT("and it works downward too"),
			ElysiumCam::Approach(10.0f, 0.0f, 4.0f, 1.0f), 6.0f);
		TestEqual(TEXT("a zero speed snaps"), ElysiumCam::Approach(0.0f, 10.0f, 0.0f, 1.0f), 10.0f);
		// 359 -> 1 is two degrees, not 358.
		TestTrue(TEXT("angle space takes the short way round"),
			FMath::IsNearlyEqual(ElysiumCam::ApproachAngle(179.0f, -179.0f, 1.0f, 1.0f), 180.0f, 0.01f));
	}

	// --- the cvar surface: defaults are VtMB's, a console value overrides, units convert once ---
	{
		TMap<FString, FString> Store;
		auto Lookup = [&Store](const TCHAR* Name) -> FString
		{
			const FString* V = Store.Find(FString(Name).ToLower());
			return V ? *V : FString();
		};

		FElysiumCameraCvars Cvars;
		Cvars.LoadFrom(Lookup);
		TestTrue(TEXT("cam_idealdist defaults to 85 Source units, held in cm"),
			FMath::IsNearlyEqual(Cvars.IdealDist, 85.0f * 2.54f, 0.01f));
		TestEqual(TEXT("cam_targetangle is degrees and needs no conversion"), Cvars.TargetAngle, 15.0f);
		TestTrue(TEXT("the damper is on with two constants"),
			Cvars.bDampOn && Cvars.HookesConstant == 4.0f && Cvars.HookesConstantWall == 15.0f);
		TestTrue(TEXT("the recovered feed cvars retain their Source defaults"),
			Cvars.FeedYaw == 50.0f && Cvars.FeedPitch == 80.0f
				&& FMath::IsNearlyEqual(Cvars.FeedForwardBase, -50.0f * ElysiumCam::U));

		Store.Add(TEXT("cam_idealdist"), TEXT("50"));
		Store.Add(TEXT("cdamp_on"), TEXT("0"));
		Cvars.LoadFrom(Lookup);
		TestTrue(TEXT("a console value wins over the default"),
			FMath::IsNearlyEqual(Cvars.IdealDist, 50.0f * 2.54f, 0.01f));
		TestFalse(TEXT("and cdamp_on 0 bypasses the damper"), Cvars.bDampOn);

		// Every name `docs/vtmb/camera-view-modes.md` §1 lists is declared, so none of them can fall through to
		// Python when a user's config.cfg or the patch's aliases write it.
		TestTrue(TEXT("the whole cvar surface is declared"), ElysiumCam::CvarDefs().Num() >= 24);
		bool bFoundPrefs = false;
		for (const ElysiumCam::FCvarDef& Def : ElysiumCam::CvarDefs())
		{
			bFoundPrefs |= FString(Def.Name) == TEXT("camera_prefs");
		}
		TestTrue(TEXT("including the archived weapon prefs, so a cfg round-trips"), bFoundPrefs);
	}

	// --- the scripted-shot channel: handle-based push/pop and a timed ramp ---
	{
		FElysiumCameraShotStack Stack;
		TestFalse(TEXT("an idle channel has no shot"), Stack.IsActive());
		TestEqual(TEXT("and no weight"), Stack.GetWeight(), 0.0f);

		FElysiumCameraShot Dialogue;
		Dialogue.DebugName = TEXT("DialogDefault");
		Dialogue.BlendSeconds = 0.5f;
		Dialogue.FieldOfView = 40.0f;
		const int32 DlgId = Stack.Push(Dialogue);
		TestTrue(TEXT("a push mints an id"), DlgId > 0);
		TestEqual(TEXT("and the shot decides"), Stack.Top()->DebugName, FString(TEXT("DialogDefault")));

		for (int32 i = 0; i < 30; ++i) { Stack.Advance(1.0f / 60.0f); }
		TestEqual(TEXT("the ramp takes the shot's own duration, not a fixed rate"), Stack.GetWeight(), 1.0f);

		// A cutscene opening over a conversation: the later push decides, and the ramp holds at 1.
		FElysiumCameraShot Cutscene;
		Cutscene.DebugName = TEXT("Theatre");
		const int32 CutId = Stack.Push(Cutscene);
		TestEqual(TEXT("the later push decides"), Stack.Top()->DebugName, FString(TEXT("Theatre")));
		Stack.Advance(1.0f / 60.0f);
		TestEqual(TEXT("and the channel stays at full weight across the swap"), Stack.GetWeight(), 1.0f);

		// Shots end out of order — a conversation closing behind a running cutscene.
		TestTrue(TEXT("a shot pops from wherever it sits"), Stack.Pop(DlgId));
		TestEqual(TEXT("the cutscene is untouched"), Stack.Top()->DebugName, FString(TEXT("Theatre")));
		TestFalse(TEXT("a doubled pop is a no-op"), Stack.Pop(DlgId));

		// Refreshing a live shot is what a `Follow` attach type is; it must not restart the ramp.
		FElysiumCameraShot Moved = Cutscene;
		Moved.Origin = FVector(100.0f, 0.0f, 0.0f);
		Moved.BlendSeconds = 99.0f;
		TestTrue(TEXT("a live shot refreshes"), Stack.Update(CutId, Moved));
		TestEqual(TEXT("with its new values"), (float)Stack.Top()->Origin.X, 100.0f);
		TestEqual(TEXT("but keeps the blend it was pushed with"), Stack.Top()->BlendSeconds, 0.5f);
		TestFalse(TEXT("and a stale id refreshes nothing"), Stack.Update(DlgId, Moved));

		TestTrue(TEXT("the last shot pops"), Stack.Pop(CutId));
		TestTrue(TEXT("leaving nothing on top"), Stack.Top() == nullptr);
		for (int32 i = 0; i < 30; ++i) { Stack.Advance(1.0f / 60.0f); }
		TestEqual(TEXT("and the weight ramps back out over the same duration"), Stack.GetWeight(), 0.0f);
		TestFalse(TEXT("the channel is idle again"), Stack.IsActive());

		// A shot with no blend cuts.
		FElysiumCameraShot Cut;
		Cut.BlendSeconds = 0.0f;
		Stack.Push(Cut);
		Stack.Advance(1.0f / 60.0f);
		TestEqual(TEXT("a zero-duration shot is a cut"), Stack.GetWeight(), 1.0f);
	}

	// --- the strafe bank (`V_CalcRoll`) ---
	{
		const FRotator Level = FRotator::ZeroRotator;   // right vector is +Y
		const float Angle = 2.0f;                       // cl_rollangle
		const float Speed = 200.0f;                     // cl_rollspeed, cm/s

		TestEqual(TEXT("a still body does not bank"),
			ElysiumCam::SolveViewRoll(FVector::ZeroVector, Level, Angle, Speed), 0.0f);

		// Below `cl_rollspeed` the bank is proportional; the sign follows which way the strafe goes.
		TestEqual(TEXT("half the roll speed banks half the angle"),
			ElysiumCam::SolveViewRoll(FVector(0.0f, 100.0f, 0.0f), Level, Angle, Speed), 1.0f);
		TestEqual(TEXT("and the other way banks the other way"),
			ElysiumCam::SolveViewRoll(FVector(0.0f, -100.0f, 0.0f), Level, Angle, Speed), -1.0f);

		// At or above it the bank saturates -- it never exceeds `cl_rollangle`.
		TestEqual(TEXT("past the roll speed the bank saturates"),
			ElysiumCam::SolveViewRoll(FVector(0.0f, 400.0f, 0.0f), Level, Angle, Speed), 2.0f);

		// Running straight ahead is orthogonal to the right vector, so it never banks.
		TestEqual(TEXT("forward motion does not bank"),
			ElysiumCam::SolveViewRoll(FVector(400.0f, 0.0f, 0.0f), Level, Angle, Speed), 0.0f);

		// Either cvar at zero disables it outright, which is what `cl_rollangle 0` is for.
		TestEqual(TEXT("a zero angle disables the bank"),
			ElysiumCam::SolveViewRoll(FVector(0.0f, 100.0f, 0.0f), Level, 0.0f, Speed), 0.0f);
		TestEqual(TEXT("and so does a zero speed"),
			ElysiumCam::SolveViewRoll(FVector(0.0f, 100.0f, 0.0f), Level, Angle, 0.0f), 0.0f);
	}

	// --- the water clearance (`GetWaterOffset`, `cl_waterdist`) ---
	{
		// Everything is measured from a surface at Z 0, in Source units, because the rule is
		// authored in them and `cl_waterdist` 4 is the only number in it.
		const float In = ElysiumCam::U;
		const float Dist = 4.0f * In;
		const float Surface = 0.0f;

		TestEqual(TEXT("cl_waterdist defaults to 4 Source units, held in cm"),
			FElysiumCameraCvars().WaterDist, 4.0f * ElysiumCam::U);

		// Treading (level 2): the view is RAISED until it clears the plane by cl_waterdist, so an
		// eye one inch above the surface rises the remaining three and the camera stays dry.
		TestEqual(TEXT("treading one inch above the plane lifts three"),
			ElysiumCam::SolveWaterOffset(2, Surface + In, Surface, Dist), 3.0f * In);
		// Already clear of the band: the original's step loop terminates before its first step.
		TestEqual(TEXT("treading five inches above the plane lifts nothing"),
			ElysiumCam::SolveWaterOffset(2, Surface + 5.0f * In, Surface, Dist), 0.0f);
		// The band is closed at cl_waterdist: exactly clear is clear.
		TestEqual(TEXT("and exactly four inches above is already the clearance"),
			ElysiumCam::SolveWaterOffset(2, Surface + Dist, Surface, Dist), 0.0f);

		// Submerged (level 3): the opposite direction — the view is LOWERED back under the plane,
		// so a swimming camera does not surface and flicker the underwater post-process.
		TestEqual(TEXT("submerged one inch below the plane drops three"),
			ElysiumCam::SolveWaterOffset(3, Surface - In, Surface, Dist), -3.0f * In);
		TestEqual(TEXT("submerged five inches below the plane drops nothing"),
			ElysiumCam::SolveWaterOffset(3, Surface - 5.0f * In, Surface, Dist), 0.0f);

		// Below the waist the rule does not run at all — VtMB gates the whole walk on level > 1.
		TestEqual(TEXT("a dry body is never offset"),
			ElysiumCam::SolveWaterOffset(0, Surface + In, Surface, Dist), 0.0f);
		TestEqual(TEXT("and neither is one in up to its feet"),
			ElysiumCam::SolveWaterOffset(1, Surface + In, Surface, Dist), 0.0f);
	}

	// --- the scripted composition: the last term applied, over whatever the base rig produced ---
	{
		const FVector Base(0.0f, 0.0f, 0.0f);
		const FVector Shot(100.0f, 0.0f, 0.0f);

		// Weight 0 is the identity. This is the property that lets the layer run unconditionally.
		{
			FVector L = Base; FRotator R = FRotator::ZeroRotator; float Fov = 90.0f;
			ElysiumCam::ComposeScriptedShot(L, R, Fov, Shot, FRotator(10.0f, 20.0f, 0.0f), 40.0f, 0.0f);
			TestEqual(TEXT("a weightless shot leaves the base view alone"), L, Base);
			TestEqual(TEXT("including its fov"), Fov, 90.0f);
		}

		// Weight 1 is the shot outright.
		{
			FVector L = Base; FRotator R = FRotator::ZeroRotator; float Fov = 90.0f;
			ElysiumCam::ComposeScriptedShot(L, R, Fov, Shot, FRotator(10.0f, 20.0f, 0.0f), 40.0f, 1.0f);
			TestEqual(TEXT("a full-weight shot is the view"), L, Shot);
			TestEqual(TEXT("with its own fov"), Fov, 40.0f);
		}

		// Mid-ramp is the interpolation the cutscene was authored around.
		{
			FVector L = Base; FRotator R = FRotator::ZeroRotator; float Fov = 90.0f;
			ElysiumCam::ComposeScriptedShot(L, R, Fov, Shot, FRotator::ZeroRotator, 40.0f, 0.5f);
			TestEqual(TEXT("half weight is the midpoint"), (float)L.X, 50.0f);
			TestEqual(TEXT("and the fov meets in the middle"), Fov, 65.0f);
		}

		// A shot file with no `FieldOfView` keeps the player's.
		{
			FVector L = Base; FRotator R = FRotator::ZeroRotator; float Fov = 90.0f;
			ElysiumCam::ComposeScriptedShot(L, R, Fov, Shot, FRotator::ZeroRotator, 0.0f, 1.0f);
			TestEqual(TEXT("a shot with no fov keeps the player's"), Fov, 90.0f);
			TestEqual(TEXT("while still moving the camera"), L, Shot);
		}

		// The rotator lerp takes the short way round, so an edit across +/-180 does not spin. Going
		// the long way would land on 0; the short way lands on +/-180, which is the same heading.
		{
			FVector L = Base; FRotator R(0.0f, 170.0f, 0.0f); float Fov = 90.0f;
			ElysiumCam::ComposeScriptedShot(L, R, Fov, Base, FRotator(0.0f, -170.0f, 0.0f), 0.0f, 0.5f);
			TestEqual(TEXT("a shot across the +/-180 boundary takes the short way"),
				(float)FMath::Abs(FRotator::NormalizeAxis(R.Yaw)), 180.0f);
		}
	}

	return true;
}

// The switch-frame table (`docs/vtmb/camera-view-modes.md` §5, the render hand-off).
//
// This is a direct transcription of the recovered table and the acceptance for the whole draw
// policy: every surface that switches, the exact frame it switches on in each direction, and which
// gates are boolean against the one that fades. It is a pure function of weights, boom and shot
// keys, so it asserts with no pawn, no world and no RHI.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraDrawTest, "Elysium.Substrate.CameraDraw",
	GElysiumTestFlags)
bool FElysiumCameraDrawTest::RunTest(const FString&)
{
	FElysiumCameraCvars Cvars;
	Cvars.IdealDist = 100.0f;
	Cvars.FadeStart = 80.0f;
	Cvars.FadeEnd = 20.0f;

	const FElysiumShotPresentation NoShot;
	const FVector NearEye(5.0f, 0.0f, 0.0f);
	const FVector FarBoom(200.0f, 0.0f, 0.0f);

	// --- first -> third: everything switches on the FIRST frame, the body starts transparent ---
	{
		FElysiumCameraWeights W;
		W.bUserThird = true;
		W.Third = 0.0001f;   // the first frame the ramp has left zero
		const FElysiumCameraDrawPolicy P = ElysiumCam::SolveDrawPolicy(W, NearEye, Cvars, NoShot);
		TestTrue(TEXT("the predicate is true on the first frame of first->third"), P.bThirdPerson);
		TestTrue(TEXT("the body is draw-eligible immediately"), P.bBodyEligible);
		TestEqual(TEXT("but starts at alpha 0, because the boom is still inside cam_fadeend"),
			P.BodyAlpha, 0.0f);
		TestTrue(TEXT("the world weapon becomes eligible immediately, with no fade"),
			P.bWorldWeaponEligible);
		TestFalse(TEXT("the first-person hands stop submitting immediately"), P.bViewmodelEligible);
		TestEqual(TEXT("and the crosshair is already the plain third-person reticle"),
			P.Reticle, EElysiumReticlePath::ThirdPerson);
		TestTrue(TEXT("an ordinary mode toggle never hides the HUD"), P.bShowHud);
	}

	// --- mid-blend, past the band's ceiling: the one soft hand-off has completed ---
	{
		FElysiumCameraWeights W;
		W.bUserThird = true;
		W.Third = 1.0f;
		const FElysiumCameraDrawPolicy P = ElysiumCam::SolveDrawPolicy(W, FarBoom, Cvars, NoShot);
		TestEqual(TEXT("the body is opaque past min(cam_idealdist, cam_fadestart)"), P.BodyAlpha, 1.0f);
	}

	// --- third -> first: the switch is the LAST frame, at exactly zero ---
	{
		FElysiumCameraWeights W;
		W.Third = 0.001f;
		const FElysiumCameraDrawPolicy Almost = ElysiumCam::SolveDrawPolicy(W, NearEye, Cvars, NoShot);
		TestTrue(TEXT("a hair above zero is still third person"), Almost.bThirdPerson);
		TestTrue(TEXT("the body is still eligible through the whole return"), Almost.bBodyEligible);
		TestTrue(TEXT("the world weapon stays eligible until the weight reaches zero"),
			Almost.bWorldWeaponEligible);
		TestFalse(TEXT("and the hands stay suppressed for the whole return"), Almost.bViewmodelEligible);

		W.Third = 0.0f;
		const FElysiumCameraDrawPolicy Zero = ElysiumCam::SolveDrawPolicy(W, NearEye, Cvars, NoShot);
		TestFalse(TEXT("exactly zero ends third person"), Zero.bThirdPerson);
		TestFalse(TEXT("the body stops submitting on that frame"), Zero.bBodyEligible);
		TestFalse(TEXT("so does the world weapon"), Zero.bWorldWeaponEligible);
		TestTrue(TEXT("and the hands resume on exactly that frame"), Zero.bViewmodelEligible);
		TestEqual(TEXT("the crosshair returns to the use-icon path"),
			Zero.Reticle, EElysiumReticlePath::FirstPerson);
	}

	// --- a scripted camera over a first-person run: eligible, and fully transparent ---
	// This is why retail needs no player-body suppression and why creating an `npc_VPlayerController`
	// double does not put two bodies on screen (`docs/vtmb/camera-view-modes.md` §6).
	{
		FElysiumCameraWeights W;
		W.Scripted = 1.0f;
		const FElysiumCameraDrawPolicy P = ElysiumCam::SolveDrawPolicy(W, FarBoom, Cvars, NoShot);
		TestTrue(TEXT("a scripted camera satisfies the predicate"), P.bThirdPerson);
		TestTrue(TEXT("so the body is draw-eligible under a cutscene"), P.bBodyEligible);
		TestEqual(TEXT("and invisible, because the third-person boom is still zero-weighted"),
			P.BodyAlpha, 0.0f);
		TestFalse(TEXT("the first-person hands are suppressed under any scripted camera"),
			P.bViewmodelEligible);
	}

	// --- the latches the predicate does and does not count ---
	{
		FElysiumCameraWeights W;
		W.bForcedFirst = true;
		TestFalse(TEXT("forced-first is not a term in the disjunction"),
			ElysiumCam::SolveDrawPolicy(W, NearEye, Cvars, NoShot).bThirdPerson);

		FElysiumCameraWeights Feed;
		Feed.Feed = 0.25f;
		TestTrue(TEXT("the feed weight is"),
			ElysiumCam::SolveDrawPolicy(Feed, NearEye, Cvars, NoShot).bThirdPerson);

		FElysiumCameraWeights Secondary;
		Secondary.Secondary = 0.25f;
		TestTrue(TEXT("and so is the secondary scripted weight"),
			ElysiumCam::SolveDrawPolicy(Secondary, NearEye, Cvars, NoShot).bThirdPerson);

		FElysiumCameraWeights Forced;
		Forced.bForcedThird = true;
		TestTrue(TEXT("forced-third is, with no weight at all"),
			ElysiumCam::SolveDrawPolicy(Forced, NearEye, Cvars, NoShot).bThirdPerson);
	}

	// --- HUD policy belongs to the NAMED shot channel and to nothing else ---
	{
		FElysiumCameraWeights W;
		W.Scripted = 1.0f;

		// A Worldcraft `camera_track` publishes a value shot. It carries no `ShowHud` key, so it
		// cannot take the HUD down no matter how long it owns the view.
		FElysiumShotPresentation Track;
		Track.bNamed = false;
		Track.bShowHud = false;
		TestTrue(TEXT("a camera_track cannot hide the HUD"),
			ElysiumCam::SolveDrawPolicy(W, FarBoom, Cvars, Track).bShowHud);

		// A named story shot authoring neither key hides both surfaces: both parse default 0.
		FElysiumShotPresentation Story;
		Story.bNamed = true;
		const FElysiumCameraDrawPolicy StoryPolicy =
			ElysiumCam::SolveDrawPolicy(W, FarBoom, Cvars, Story);
		TestFalse(TEXT("a named shot with no keys hides the HUD"), StoryPolicy.bShowHud);
		TestFalse(TEXT("and the viewmodel with it"), StoryPolicy.bViewmodelEligible);

		// An interaction shot opts back in — `special-case.txt`'s Hacking and Intrusion entries.
		FElysiumShotPresentation Interaction;
		Interaction.bNamed = true;
		Interaction.bShowHud = true;
		TestTrue(TEXT("an interaction shot opts the HUD back in"),
			ElysiumCam::SolveDrawPolicy(W, FarBoom, Cvars, Interaction).bShowHud);

		// `DrawViewmodel` can only ever suppress. With the predicate true it cannot force hands on.
		FElysiumShotPresentation Draws;
		Draws.bNamed = true;
		Draws.bDrawViewmodel = true;
		TestFalse(TEXT("DrawViewmodel never forces the hands on under a scripted camera"),
			ElysiumCam::SolveDrawPolicy(W, FarBoom, Cvars, Draws).bViewmodelEligible);
	}

	return true;
}

// The third-person rig.
//
// Nothing here reproduces a decompiled function; it is the project's own third-person rig, and the
// assertions are about the three properties that make it *different* from the recovered one: the
// damper is frame-rate independent, collision is asymmetric, and the body sits off-centre.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraRigTest, "Elysium.Substrate.CameraRig", GElysiumTestFlags)
bool FElysiumCameraRigTest::RunTest(const FString&)
{
	using namespace ElysiumRig;

	// --- the store owns every axis it names, and the conversion happens exactly once ---
	{
		FElysiumCameraRigTuning Project;
		FElysiumCameraCvars Cvars;   // constructed at retail's defaults, already in centimetres
		const FElysiumCameraRigTuning T = ResolveTuning(Project, Cvars);

		TestEqual(TEXT("cam_idealdist 85 owns the rest length"), T.BoomLength, 85.0f * ElysiumCam::U);
		TestEqual(TEXT("cam_trace_radius 9 owns the probe"), T.ProbeRadius, 9.0f * ElysiumCam::U);
		TestEqual(TEXT("cam_targetangle 15 owns the pitch offset"), T.PitchOffset, 15.0f);
		TestEqual(TEXT("c_mindistance owns the dolly floor"), T.DollyMin, 30.0f * ElysiumCam::U);
		TestEqual(TEXT("c_maxdistance owns the dolly ceiling"), T.DollyMax, 200.0f * ElysiumCam::U);
		TestEqual(TEXT("cdamp_springlength owns the dead band"), T.DamperDeadBand, 0.1f * ElysiumCam::U);
		TestEqual(TEXT("cdamp_maxdist owns the lag clamp"), T.DamperMaxLag, 50.0f * ElysiumCam::U);

		// The collision floor is a different quantity from the dolly floor and stays the project's.
		TestEqual(TEXT("the collision retract floor is not c_mindistance"),
			T.MinBoomLength, Project.MinBoomLength);
		TestEqual(TEXT("and neither is the recovery rate"), T.ReturnSpeed, Project.ReturnSpeed);
		TestEqual(TEXT("nor the shoulder offset"), T.ShoulderOffset, Project.ShoulderOffset);

		// A Hooke rate becomes the half-life that decays at the same speed, and the wall constant is
		// the stiffer of the two — retail snaps in and eases out.
		TestTrue(TEXT("cdamp_hookesconstant 4 becomes ln2/4"),
			FMath::IsNearlyEqual(T.PositionHalfLifeFree, UE_LN2 / 4.0f, 1e-4f));
		TestTrue(TEXT("cdamp_hookesconstantwall 15 becomes ln2/15"),
			FMath::IsNearlyEqual(T.PositionHalfLifeWall, UE_LN2 / 15.0f, 1e-4f));
		TestTrue(TEXT("the wall damper is the stiffer of the two"),
			T.PositionHalfLifeWall < T.PositionHalfLifeFree);

		// **One conversion, at LoadFrom.** A store value of 50 must reach the rig as 127 cm, not as
		// 50 and not as 322.
		Cvars.IdealDist = 50.0f * ElysiumCam::U;
		TestEqual(TEXT("a retuned cam_idealdist reaches the rig converted exactly once"),
			ResolveTuning(Project, Cvars).BoomLength, 127.0f);

		// Retail's own two bypasses survive as bypasses.
		Cvars.bCollide = false;
		Cvars.bDampOn = false;
		const FElysiumCameraRigTuning Off = ResolveTuning(Project, Cvars);
		TestFalse(TEXT("cam_collide 0 turns the sweep off"), Off.bCollide);
		TestFalse(TEXT("cdamp_on 0 turns the damper off"), Off.bDampOn);
	}

	// --- the hand-orbit: clamped, and an untouched axis follows its cvar ---
	{
		FElysiumCameraRigTuning T;
		T.OrbitSpeed = 90.0f;
		T.DollySpeed = 100.0f;
		T.OrbitYawMin = -135.0f;
		T.OrbitYawMax = 135.0f;
		T.OrbitPitchMin = 0.0f;
		T.OrbitPitchMax = 90.0f;
		T.BoomLength = 200.0f;
		T.DollyMin = 100.0f;
		T.DollyMax = 300.0f;

		FElysiumOrbitState O;
		TestFalse(TEXT("an untouched axis is not held, so it follows its cvar"), O.bYawHeld);

		// Two seconds of held left orbit is 180 degrees of step, clamped to the recovered limit.
		StepOrbit(O, static_cast<uint64>(EElysiumButton::CamYawLeft), FVector2D::ZeroVector, 2.0f, T);
		TestEqual(TEXT("c_minyaw clamps the orbit"), O.YawOffset, -135.0f);
		TestTrue(TEXT("and the axis is now held, so it stops following the cvar"), O.bYawHeld);

		StepOrbit(O, static_cast<uint64>(EElysiumButton::CamYawRight), FVector2D::ZeroVector, 10.0f, T);
		TestEqual(TEXT("c_maxyaw clamps the other way"), O.YawOffset, 135.0f);

		FElysiumOrbitState P;
		StepOrbit(P, static_cast<uint64>(EElysiumButton::CamPitchUp), FVector2D::ZeroVector, 5.0f, T);
		TestEqual(TEXT("c_minpitch clamps an upward orbit"), P.PitchOffset, 0.0f);
		StepOrbit(P, static_cast<uint64>(EElysiumButton::CamPitchDown), FVector2D::ZeroVector, 5.0f, T);
		TestEqual(TEXT("c_maxpitch clamps a downward one"), P.PitchOffset, 90.0f);

		// The dolly clamp bounds the composed distance, not the offset — `c_mindistance` is a
		// distance limit, so 200 + offset must stay inside [100, 300].
		FElysiumOrbitState D;
		StepOrbit(D, static_cast<uint64>(EElysiumButton::CamOut), FVector2D::ZeroVector, 10.0f, T);
		TestEqual(TEXT("c_maxdistance bounds the composed rest length"), D.DollyOffset, 100.0f);
		StepOrbit(D, static_cast<uint64>(EElysiumButton::CamIn), FVector2D::ZeroVector, 10.0f, T);
		TestEqual(TEXT("and c_mindistance bounds it the other way"), D.DollyOffset, -100.0f);

		// `snapto` / `cam_restore` release every hold, which is what hands the axes back to the cvars.
		RestoreOrbit(D);
		TestEqual(TEXT("cam_restore returns the dolly to its cvar"), D.DollyOffset, 0.0f);
		TestFalse(TEXT("and releases the hold"), D.bDollyHeld);

		// **The orbit is not the view.** Nothing here writes a control rotation; the camera swings
		// around the player and the player does not turn.
		FElysiumOrbitState M;
		StepOrbit(M, static_cast<uint64>(EElysiumButton::CamMouseMove), FVector2D(10.0f, 5.0f), 0.0f, T);
		TestEqual(TEXT("a mouse-driven orbit folds the look delta into yaw"), M.YawOffset, 10.0f);
		TestEqual(TEXT("and into pitch"), M.PitchOffset, 5.0f);

		// ...and it folds it there INSTEAD of into the view, so the same delta must not also reach the
		// control rotation. Both mouse-driven bits intercept, including the axis `+camdistance` does
		// not itself consume.
		TestFalse(TEXT("an idle frame leaves the mouse to the view"),
			OrbitInterceptsMouse(0));
		TestTrue(TEXT("+cammousemove takes the frame's mouse for the camera"),
			OrbitInterceptsMouse(static_cast<uint64>(EElysiumButton::CamMouseMove)));
		TestTrue(TEXT("and so does +camdistance"),
			OrbitInterceptsMouse(static_cast<uint64>(EElysiumButton::CamDistance)));
		TestFalse(TEXT("a held button-orbit is not a mouse interception"),
			OrbitInterceptsMouse(static_cast<uint64>(EElysiumButton::CamYawLeft)));
	}

	// --- the held flags are what an axis stops following its cvar WITH ---
	{
		FElysiumCameraRigTuning Base;
		Base.YawOffset = 10.0f;        // `cam_yaw`
		Base.PitchOffset = 15.0f;      // `cam_targetangle`
		Base.BoomLength = 200.0f;      // `cam_idealdist`
		Base.DollyMin = 50.0f;
		Base.DollyMax = 400.0f;
		Base.OrbitYawMin = -135.0f;
		Base.OrbitYawMax = 135.0f;
		Base.OrbitPitchMin = 0.0f;
		Base.OrbitPitchMax = 90.0f;
		Base.OrbitSpeed = 20.0f;
		Base.DollySpeed = 20.0f;

		// Nothing touched: every axis is the cvar's, verbatim.
		FElysiumOrbitState O;
		const FElysiumCameraRigTuning Untouched = ComposeOrbit(Base, O);
		TestEqual(TEXT("an untouched yaw is exactly cam_yaw"), Untouched.YawOffset, 10.0f);
		TestEqual(TEXT("an untouched pitch is exactly cam_targetangle"), Untouched.PitchOffset, 15.0f);
		TestEqual(TEXT("an untouched boom is exactly cam_idealdist"), Untouched.BoomLength, 200.0f);

		// A live retune moves an untouched axis at once — the property that makes `elysium.cmd
		// cam_idealdist 50` land on the next frame.
		FElysiumCameraRigTuning Retuned = Base;
		Retuned.YawOffset = 40.0f;
		Retuned.BoomLength = 300.0f;
		TestEqual(TEXT("and it follows a retune live"), ComposeOrbit(Retuned, O).YawOffset, 40.0f);
		TestEqual(TEXT("boom included"), ComposeOrbit(Retuned, O).BoomLength, 300.0f);

		// One second of held right orbit against the ORIGINAL tuning: +20 degrees, and the axis is now
		// the player's, frozen against `cam_yaw` = 10.
		StepOrbit(O, static_cast<uint64>(EElysiumButton::CamYawRight), FVector2D::ZeroVector, 1.0f, Base);
		TestTrue(TEXT("the orbited axis is held"), O.bYawHeld);
		TestEqual(TEXT("and composes onto the cvar it was following"),
			ComposeOrbit(Base, O).YawOffset, 30.0f);

		// **The point of the flag.** Retuning `cam_yaw` from 10 to 40 must not drag a boom the player
		// has already placed by hand; the axes they have not touched still follow.
		TestEqual(TEXT("a held yaw stops following cam_yaw"), ComposeOrbit(Retuned, O).YawOffset, 30.0f);
		TestEqual(TEXT("while an untouched boom on the same frame still follows cam_idealdist"),
			ComposeOrbit(Retuned, O).BoomLength, 300.0f);

		// `snapto` / `cam_restore` hands it back, which is the recovered way out.
		RestoreOrbit(O);
		TestFalse(TEXT("cam_restore releases the yaw hold"), O.bYawHeld);
		TestEqual(TEXT("and the axis follows its cvar again"),
			ComposeOrbit(Retuned, O).YawOffset, 40.0f);

		// The pitch axis composes the other way round — the orbit is Source's down-positive and
		// `PitchOffset` is degrees above the eye line.
		FElysiumOrbitState P;
		StepOrbit(P, static_cast<uint64>(EElysiumButton::CamPitchDown), FVector2D::ZeroVector, 1.0f, Base);
		TestEqual(TEXT("a downward orbit lowers the boom's lift"),
			ComposeOrbit(Base, P).PitchOffset, -5.0f);
		FElysiumCameraRigTuning PitchRetuned = Base;
		PitchRetuned.PitchOffset = 45.0f;
		TestEqual(TEXT("and a held pitch stops following cam_targetangle"),
			ComposeOrbit(PitchRetuned, P).PitchOffset, -5.0f);
	}

	// --- the pivot damper: two constants, a dead band and a lag clamp ---
	{
		FElysiumCameraRigTuning T;
		T.PositionHalfLifeFree = 0.2f;
		T.PositionHalfLifeWall = 0.05f;
		T.DamperDeadBand = 1.0f;
		T.DamperMaxLag = 500.0f;

		const FVector At(0.0f, 0.0f, 0.0f);
		const FVector Goal(100.0f, 0.0f, 0.0f);

		const FVector Free = DampPivot(At, Goal, /*bClipped=*/false, T, 0.2f);
		const FVector Wall = DampPivot(At, Goal, /*bClipped=*/true, T, 0.2f);
		TestTrue(TEXT("the free damper closes half the gap in its own half-life"),
			FMath::IsNearlyEqual(static_cast<float>(Free.X), 50.0f, 0.01f));
		TestTrue(TEXT("the wall damper is stiffer over the same step"), Wall.X > Free.X);

		TestEqual(TEXT("inside the dead band the pivot has arrived"),
			DampPivot(FVector(99.5f, 0.0f, 0.0f), Goal, false, T, 0.2f), Goal);

		// The clamp bounds the trail and then damps from the bound: 1100 behind is pulled onto the
		// 500 bound, and half of *that* gap closes over one half-life. Releasing the pivot onto the
		// body instead would answer `Goal`.
		TestTrue(TEXT("beyond the lag clamp the pivot eases in from the bound"),
			DampPivot(FVector(-1000.0f, 0.0f, 0.0f), Goal, false, T, 0.2f)
				.Equals(FVector(-150.0f, 0.0f, 0.0f), KINDA_SMALL_NUMBER));

		T.bDampOn = false;
		TestEqual(TEXT("cdamp_on 0 makes the pivot rigid"), DampPivot(At, Goal, false, T, 0.2f), Goal);
	}

	// --- a body faster than the spring rides the bound instead of sawtoothing ---
	{
		// The shipped free half-life settles at `Speed * PositionHalfLifeFree / ln 2`, which at run
		// speed is past `DamperMaxLag` by construction — so this is the ordinary case of running
		// forward, not an edge one. If the clamp released the pivot onto the body the lag would
		// reset to zero every time it crossed the bound, which is what reads as the camera
		// repeatedly catching up rather than trailing.
		const FElysiumCameraRigTuning Ship;
		const float Dt = 1.0f / 60.0f;
		const float Speed = 600.0f;   // cm/s, a little above `ElysiumMove::RunSpeed`

		FVector Body = FVector::ZeroVector;
		FVector Pivot = FVector::ZeroVector;
		float Lag = 0.0f;
		bool bReset = false;
		bool bOverran = false;
		for (int32 Step = 0; Step < 240; ++Step)
		{
			Body.X += Speed * Dt;
			Pivot = DampPivot(Pivot, Body, /*bClipped=*/false, Ship, Dt);

			const float Next = static_cast<float>(FVector::Dist(Pivot, Body));
			bReset |= Next < Lag - KINDA_SMALL_NUMBER;
			bOverran |= Next > Ship.DamperMaxLag + KINDA_SMALL_NUMBER;
			Lag = Next;
		}

		TestFalse(TEXT("the lag never falls back while the body holds its speed"), bReset);
		TestFalse(TEXT("and never trails further than cdamp_maxdist"), bOverran);
		TestTrue(TEXT("a body faster than the spring settles against the bound"),
			Lag > Ship.DamperMaxLag * 0.9f);
	}

	// --- the damper is exact at any step ---
	{
		// One step of 0.2 s must land exactly where two of 0.1 s do. This is the whole reason the
		// decay is a half-life rather than the recovered rig's `Clamp(K * Dt, 0, 1)` Euler step,
		// and it is what `uv run elysium debug move --hz` compares across rates.
		const FVector Start(0.0f, 0.0f, 0.0f);
		const FVector Target(100.0f, 0.0f, 0.0f);
		const float HalfLife = 0.1f;

		const FVector OneStep = DampToward(Start, Target, HalfLife, 0.2f);
		const FVector TwoSteps = DampToward(DampToward(Start, Target, HalfLife, 0.1f), Target, HalfLife, 0.1f);
		TestTrue(TEXT("the damper composes exactly across a subdivided step"),
			OneStep.Equals(TwoSteps, KINDA_SMALL_NUMBER));

		// And the half-life means what it says.
		TestEqual(TEXT("one half-life closes exactly half the gap"),
			(float)DampToward(Start, Target, HalfLife, HalfLife).X, 50.0f);

		// The degenerate ends.
		TestEqual(TEXT("a zero half-life snaps"),
			(float)DampToward(Start, Target, 0.0f, 1.0f / 60.0f).X, 100.0f);
		TestEqual(TEXT("and a zero step holds"),
			(float)DampToward(Start, Target, HalfLife, 0.0f).X, 0.0f);
	}

	// --- the boom's rotation ---
	{
		FElysiumCameraRigTuning Tuning;
		Tuning.PitchOffset = 10.0f;

		// The camera sits at `Pivot - Forward * Distance`, so lifting it above the eye line pitches
		// the boom DOWN. Getting this sign backwards puts the camera under the character, where it
		// drags through the floor -- which is what the clip channel showed when it was.
		const FRotator Level = BoomRotation(FRotator(0.0f, 90.0f, 0.0f), Tuning);
		TestEqual(TEXT("lifting the camera pitches the boom down"), (float)Level.Pitch, -10.0f);
		TestEqual(TEXT("and keeps the player's yaw"), (float)Level.Yaw, 90.0f);

		// And the resulting camera really is above the pivot, which is the property the sign is for.
		const FVector Above = BoomTarget(FVector::ZeroVector, Level, 200.0f, Tuning);
		TestTrue(TEXT("so the camera ends up above the pivot"), Above.Z > 0.0);

		// A banked view must not roll the arm, or the character swings across the frame.
		const FRotator Banked = BoomRotation(FRotator(0.0f, 0.0f, 30.0f), Tuning);
		TestEqual(TEXT("a banked view never rolls the boom"), (float)Banked.Roll, 0.0f);

		// The pitch clamp is the rig's, not the controller's.
		const FRotator Steep = BoomRotation(FRotator(-80.0f, 0.0f, 0.0f), Tuning);
		TestEqual(TEXT("the boom pitch clamps at the rig's own limit"), (float)Steep.Pitch, Tuning.PitchMin);

		// **A control rotation arrives in Unreal's canonical [0, 360), not signed.**
		// `APlayerCameraManager::LimitViewPitch` ends with `FRotator::ClampAxis`, so a view looking
		// down five degrees reaches here as 355 rather than -5. Clamping that without normalizing
		// pins the boom at `PitchMax` for every downward view and only releases it once the angle
		// wraps past 360 — which reads in game as the camera snapping to maximum-up the moment you
		// look down, then recentring if you keep going. Yaw was always normalized here; pitch has to
		// be too, and these two cases are the difference.
		const FRotator DownWrapped = BoomRotation(FRotator(355.0f, 0.0f, 0.0f), Tuning);
		TestEqual(TEXT("an unnormalized downward pitch is read as downward"),
			(float)DownWrapped.Pitch, -15.0f);
		const FRotator SteepWrapped = BoomRotation(FRotator(271.0f, 0.0f, 0.0f), Tuning);
		TestEqual(TEXT("and still clamps to the rig's lower limit, never the upper"),
			(float)SteepWrapped.Pitch, Tuning.PitchMin);
	}

	// --- the shoulder offset is in boom space ---
	{
		FElysiumCameraRigTuning Tuning;
		Tuning.ShoulderOffset = FVector(0.0f, 40.0f, 10.0f);

		// Looking down +X: the camera sits back along -X, right along +Y and up along +Z.
		const FVector Behind = BoomTarget(FVector::ZeroVector, FRotator::ZeroRotator, 200.0f, Tuning);
		TestTrue(TEXT("the camera sits behind the pivot, offset to the shoulder"),
			Behind.Equals(FVector(-200.0f, 40.0f, 10.0f), KINDA_SMALL_NUMBER));

		// Turned 90 degrees, the offset turns with it -- it stays on the same shoulder rather than
		// sliding across the frame.
		const FVector Turned = BoomTarget(FVector::ZeroVector, FRotator(0.0f, 90.0f, 0.0f), 200.0f, Tuning);
		TestTrue(TEXT("and it follows the boom rather than the world"),
			Turned.Equals(FVector(-40.0f, -200.0f, 10.0f), 0.01f));
	}

	// --- collision is asymmetric ---
	{
		FElysiumCameraRigTuning Tuning;
		Tuning.BoomLength = 220.0f;
		Tuning.MinBoomLength = 40.0f;
		Tuning.WallPullIn = 12.0f;
		Tuning.ReturnSpeed = 260.0f;

		// Contact retracts on the frame it happens. Easing here would leave the wall inside the
		// near plane for the duration of the ease.
		const float Hit = SolveBoomDistance(220.0f, 220.0f, /*bHit*/ true, 100.0f, Tuning, 1.0f / 60.0f);
		TestEqual(TEXT("contact retracts immediately, less the wall pull-in"), Hit, 88.0f);

		// A contact closer than the floor still respects it.
		const float Crushed = SolveBoomDistance(220.0f, 220.0f, true, 20.0f, Tuning, 1.0f / 60.0f);
		TestEqual(TEXT("but never past the minimum boom"), Crushed, 40.0f);

		// Clearance grows back rate-limited -- this is the half that is *not* symmetric, and it is
		// why the camera does not pop out of a doorway the first frame the sweep misses.
		const float Recovering = SolveBoomDistance(88.0f, 220.0f, false, 0.0f, Tuning, 0.1f);
		TestEqual(TEXT("clearance grows back at the return speed"), Recovering, 114.0f);
		TestTrue(TEXT("which is slower than the retract that caused it"), Recovering < 220.0f);

		// It never overshoots the rest length.
		const float Restored = SolveBoomDistance(219.0f, 220.0f, false, 0.0f, Tuning, 1.0f);
		TestEqual(TEXT("and stops at the rest length"), Restored, 220.0f);

		// A zero return speed restores instantly, the A/B against the rate limit.
		Tuning.ReturnSpeed = 0.0f;
		TestEqual(TEXT("a zero return speed restores at once"),
			SolveBoomDistance(88.0f, 220.0f, false, 0.0f, Tuning, 1.0f / 60.0f), 220.0f);
	}

	// The client shot tracker — `C_BaseCineCamera` (`FUN_10001fe0` / `FUN_10001d40` / `FUN_10001c80`).
	//
	// **This is the owner's "the dialogue camera wobbles while the NPC animates" defect.** The shot's
	// look-at is a head bone, re-resolved every frame (retail's camera think does the same), so the
	// goal angle jitters by a fraction of a degree all conversation long. Nothing in the anchor
	// grammar suppresses that; the shot's own 10-degree `AngularTolerance` deadband is what does.
	{
		// Jack.txt's constraints, verbatim: MoveSpeed 500, MoveAccel 250, TurnAccel 30,
		// MaxTurnRate [60,60,60], DistanceTolerance 5, AngularTolerance [10,10,10].
		FElysiumCameraShot Shot;
		Shot.bTracked = true;   // a file shot: `CamMode` 1
		Shot.Origin = FVector(0.0f, 0.0f, 165.1f);
		Shot.LookAt = FVector(300.0f, 0.0f, 165.1f);
		Shot.bUseLookAt = true;
		Shot.MoveSpeed = 500.0f * ElysiumCam::U;
		Shot.MoveAccel = 250.0f * ElysiumCam::U;
		Shot.TurnAccel = 30.0f;
		Shot.MaxTurnRate = FVector(60.0f, 60.0f, 60.0f);
		Shot.DistanceTolerance = 5.0f * ElysiumCam::U;
		Shot.AngularTolerance = FVector(10.0f, 10.0f, 10.0f);

		FElysiumScriptedShotTracker Tracker;
		Tracker.Start(Shot);
		TestTrue(TEXT("a shot starts framed on its goal, position settled"),
			Tracker.Location.Equals(Shot.Origin, 0.001f) && Tracker.bPositionSettled);
		TestEqual(TEXT("and with no carried speed"), Tracker.Speed, 0.0f);

		const float Dt = 1.0f / 60.0f;
		// One second of the idle's head motion: the look-at swings a few degrees either way, which is
		// what the animated `Bone: Bip01 Head` anchor does every frame of a conversation.
		const FRotator Framed = Tracker.Rotation;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			const float Jitter = 8.0f * FMath::Sin(Frame * 0.5f);   // +/- 8 degrees of yaw
			Shot.LookAt = Shot.Origin + FVector(300.0f, 0.0f, 0.0f).RotateAngleAxis(Jitter, FVector::ZAxisVector);
			Tracker.Advance(Shot, Dt);
		}
		TestTrue(TEXT("a look-at jittering inside AngularTolerance does not rotate the camera"),
			Tracker.Rotation.Equals(Framed, 0.001f));
		TestTrue(TEXT("and the yaw axis stays settled through it"), Tracker.bYawSettled);
		TestTrue(TEXT("a still subject never moves the camera either"),
			Tracker.Location.Equals(Shot.Origin, 0.001f));

		// A real turn — the subject walks off the mark — is past the band, so the camera pans, and it
		// pans until it is inside the **1-degree acquire band** (`FUN_10001d40`'s `_DAT_101e34ec`
		// unsettled tolerance), then parks. It does not land exactly on the target: retail's acquire
		// band is a whole degree wide, which is what the port's old 0.05 constant understated.
		Shot.LookAt = Shot.Origin + FVector(300.0f, 0.0f, 0.0f).RotateAngleAxis(45.0f, FVector::ZAxisVector);
		for (int32 Frame = 0; Frame < 300; ++Frame)
		{
			Tracker.Advance(Shot, Dt);
		}
		TestTrue(TEXT("a drift past the tolerance does pan the camera"),
			FMath::IsNearlyEqual(FRotator::NormalizeAxis(Tracker.Rotation.Yaw), 45.0f,
				FElysiumScriptedShotTracker::UnsettledAngleTolerance));
		TestTrue(TEXT("and parks inside retail's 1-degree acquire band"), Tracker.bYawSettled);

		// Position hysteresis, the same shape one axis down: parked until the goal drifts more than
		// DistanceTolerance (5 u = 12.7 cm), and once moving it closes to within 1 u.
		const FVector Parked = Tracker.Location;
		Shot.Origin = Parked + FVector(10.0f, 0.0f, 0.0f);   // under 12.7 cm
		Tracker.Advance(Shot, Dt);
		TestTrue(TEXT("a goal drift inside DistanceTolerance leaves the camera parked"),
			Tracker.Location.Equals(Parked, 0.001f) && Tracker.bPositionSettled);
		Shot.Origin = Parked + FVector(100.0f, 0.0f, 0.0f);
		Tracker.Advance(Shot, Dt);
		TestFalse(TEXT("a drift past it takes the camera out of the park"), Tracker.bPositionSettled);
		TestTrue(TEXT("and it accelerates rather than teleporting"),
			Tracker.Speed > 0.0f && Tracker.Speed <= Shot.MoveSpeed
				&& !Tracker.Location.Equals(Shot.Origin, 0.001f));
		for (int32 Frame = 0; Frame < 600; ++Frame)
		{
			Tracker.Advance(Shot, Dt);
		}
		TestTrue(TEXT("a moving camera closes to within the 1-unit settle distance, not the tolerance"),
			FVector::Distance(Tracker.Location, Shot.Origin) < FElysiumScriptedShotTracker::SettleDistance);

		// A tracked shot that authors no `TurnAccel` runs at its rate ceiling outright
		// (`FUN_10001c80` with `TurnAccel` 0). Its position sibling does **not**: `MoveAccel` 0 is
		// retail's crawl (M7), asserted in `Elysium.Substrate.CameraTracker`.
		FElysiumCameraShot Value;
		Value.bTracked = true;
		Value.Origin = FVector(1000.0f, 0.0f, 0.0f);
		Value.bUseLookAt = false;
		Value.MoveSpeed = 100.0f;
		FElysiumScriptedShotTracker ValueTracker;
		ValueTracker.Start(Value);
		Value.Origin = FVector(1100.0f, 0.0f, 0.0f);
		ValueTracker.Advance(Value, 0.5f);
		TestTrue(TEXT("with no MoveAccel the camera takes retail's decel arm and crawls at the floor"),
			FMath::IsNearlyEqual(ValueTracker.Location.X,
				1000.0f + FElysiumScriptedShotTracker::MinTrackSpeed * 0.5f, 0.01f));

		// **The sp_tutorial_1 scenematic.** A `camera_track` pair publishes a direct shot — retail's
		// `CInput` override (`FUN_100ffb90`), or any `CamMode != 1` in `FUN_10001a20` — whose origin
		// and look-at are re-derived every frame with no tracker between them. With the tracker
		// applied instead, the shot's zero rates froze the aim on the seed frame while the dolly
		// travelled: the camera flew the authored path pointing at nothing.
		FElysiumCameraShot Track;
		Track.Origin = FVector(0.0f, 0.0f, 100.0f);
		Track.LookAt = FVector(500.0f, 0.0f, 100.0f);
		Track.bUseLookAt = true;
		Track.MoveSpeed = 0.0f;
		Track.TurnAccel = 0.0f;
		Track.MaxTurnRate = FVector::ZeroVector;
		TestFalse(TEXT("a value shot is direct by default"), Track.bTracked);
		FElysiumScriptedShotTracker TrackTracker;
		TrackTracker.Start(Track);
		Track.Origin = FVector(0.0f, 300.0f, 100.0f);          // the dolly moved
		Track.LookAt = FVector(0.0f, 300.0f + 500.0f, 100.0f);  // and the focus cut 90 degrees
		TrackTracker.Advance(Track, Dt);
		TestTrue(TEXT("a direct shot's position is the authored sample"),
			TrackTracker.Location.Equals(Track.Origin, 0.001f));
		TestTrue(TEXT("and its aim is re-derived from the authored target every frame"),
			FMath::IsNearlyEqual(FRotator::NormalizeAxis(TrackTracker.Rotation.Yaw), 90.0f, 0.001f));
		Track.LookAt = FVector(-500.0f, 300.0f, 100.0f);        // a hard cut the other way
		TrackTracker.Advance(Track, Dt);
		TestTrue(TEXT("an authored cut stays a cut, not a pan"),
			FMath::IsNearlyEqual(FMath::Abs(FRotator::NormalizeAxis(TrackTracker.Rotation.Yaw)), 180.0f, 0.001f));

		// The same numbers under the tracker are the defect: the aim does not leave the seed.
		FElysiumCameraShot Frozen = Track;
		Frozen.bTracked = true;
		Frozen.LookAt = FVector(0.0f, 800.0f, 100.0f);
		FElysiumScriptedShotTracker FrozenTracker;
		FrozenTracker.Start(Frozen);
		Frozen.LookAt = FVector(-500.0f, 300.0f, 100.0f);
		FrozenTracker.Advance(Frozen, Dt);
		TestTrue(TEXT("a tracked shot with no turn rate cannot re-aim, which is why the track must be direct"),
			FMath::IsNearlyEqual(FRotator::NormalizeAxis(FrozenTracker.Rotation.Yaw), 90.0f, 0.001f));

		// `SnapOnShotChange` is the file's own "cut, do not chase" — and it is a **one-shot**
		// (`FUN_10002210`'s tail arms `m_bSnapPending`, `FUN_10001a20` consumes it on the next
		// rendered frame). The shot change arms it; the frame after that tracks like any other.
		FElysiumCameraShot Snap = Shot;
		Snap.bSnapOnShotChange = true;
		Snap.Origin = FVector(-500.0f, 250.0f, 100.0f);
		Tracker.Start(Snap);
		TestTrue(TEXT("a SnapOnShotChange shot change arms the one-shot"), Tracker.bSnapPending);
		Snap.Origin = FVector(-800.0f, 250.0f, 100.0f);
		Tracker.Advance(Snap, Dt);
		TestTrue(TEXT("SnapOnShotChange hard-copies the goal"),
			Tracker.Location.Equals(Snap.Origin, 0.001f));
		TestFalse(TEXT("and the pending flag is consumed"), Tracker.bSnapPending);
	}

	// A `vdata/camerashots/` `FieldOfView` is a 4:3-referenced Source angle, so the window's own
	// aspect widens it (Hor+) at apply time. Rendering the authored 40 at 16:9 is what read as "the
	// camera is closer than retail".
	{
		TestTrue(TEXT("a 4:3 window renders the authored angle unchanged"),
			FMath::IsNearlyEqual(ElysiumCam::WidenSourceFov(40.0f, 4.0f / 3.0f), 40.0f, 0.01f));
		const float Wide = ElysiumCam::WidenSourceFov(40.0f, 16.0f / 9.0f);
		TestTrue(TEXT("a 16:9 window widens a 40-degree shot to about 51.8 degrees"),
			FMath::IsNearlyEqual(Wide, 51.78f, 0.05f));
		TestTrue(TEXT("retail's own default_fov 75 lands near the recovered 91 degrees at 16:9"),
			FMath::IsNearlyEqual(ElysiumCam::WidenSourceFov(75.0f, 16.0f / 9.0f), 91.0f, 0.6f));
		TestEqual(TEXT("a shot with no FieldOfView keeps the player's"),
			ElysiumCam::WidenSourceFov(0.0f, 16.0f / 9.0f), 0.0f);
	}

	// The player lens is the same rule, off the same cvar surface: `default_fov` 75 renders ~91.3
	// degrees at 16:9 and exactly 75 at the 4:3 it is written against.
	{
		FElysiumCameraCvars Lens;
		TestTrue(TEXT("default_fov defaults to retail's 75"),
			FMath::IsNearlyEqual(Lens.DefaultFov, 75.0f, 0.01f));
		TestTrue(TEXT("viewmodel_fov defaults to retail's 54"),
			FMath::IsNearlyEqual(Lens.ViewmodelFov, 54.0f, 0.01f));

		// Both are declared into the VtMB console store, so a user's `config.cfg` governs them.
		bool bDefaultFovDeclared = false;
		bool bViewmodelFovDeclared = false;
		for (const ElysiumCam::FCvarDef& Def : ElysiumCam::CvarDefs())
		{
			bDefaultFovDeclared |= FString(Def.Name) == TEXT("default_fov") && FString(Def.Default) == TEXT("75");
			bViewmodelFovDeclared |= FString(Def.Name) == TEXT("viewmodel_fov") && FString(Def.Default) == TEXT("54");
		}
		TestTrue(TEXT("default_fov is declared at 75"), bDefaultFovDeclared);
		TestTrue(TEXT("viewmodel_fov is declared at 54"), bViewmodelFovDeclared);

		// A cfg write reaches the struct, still 4:3-referenced.
		TMap<FString, FString> Cfg;
		Cfg.Add(TEXT("default_fov"), TEXT("90"));
		Lens.LoadFrom([&Cfg](const TCHAR* Name) -> FString
		{
			const FString* Hit = Cfg.Find(Name);
			return Hit ? *Hit : FString();
		});
		TestTrue(TEXT("a config.cfg default_fov governs the player lens"),
			FMath::IsNearlyEqual(Lens.DefaultFov, 90.0f, 0.01f));

		TestTrue(TEXT("default_fov 75 renders about 91.3 degrees at 16:9"),
			FMath::IsNearlyEqual(ElysiumCam::WidenSourceFov(75.0f, 16.0f / 9.0f), 91.31f, 0.05f));
		TestTrue(TEXT("and exactly 75 at the 4:3 reference"),
			FMath::IsNearlyEqual(ElysiumCam::WidenSourceFov(75.0f, 4.0f / 3.0f), 75.0f, 0.01f));

		// The player view actually writes it. With no game viewport the render aspect falls back to
		// the view's own 4:3, so the headless assertion is the authored number exactly.
		UElysiumCameraComponent* LensCamera = NewObject<UElysiumCameraComponent>();
		if (TestNotNull(TEXT("the lens test has a camera component"), LensCamera))
		{
			FMinimalViewInfo LensView;
			LensView.FOV = 90.0f;
			LensView.AspectRatio = 4.0f / 3.0f;
			LensCamera->ApplyToView(LensView);
			TestTrue(TEXT("the player view renders default_fov, not the engine's 90"),
				FMath::IsNearlyEqual(static_cast<float>(LensView.FOV), 75.0f, 0.01f));
		}
	}

	return true;
}

// The client tracker's exact numerics and the frame contract — SC1.
//
// `C_BaseCineCamera` (`client.dll`): the frame latch and the two delta guards in `FUN_10001a20`, the
// 1-degree acquire band and the 1 u/s speed floor, the one-shot snap `FUN_10002390`, retail's
// `RemainingTime` `FUN_100010f0` with both of its defects, the `MoveAccel == 0` crawl, and the FOV
// copy `FUN_10001c20` with its dev-cvar freeze. Every constant here is retail's, cited at its
// declaration in `Public/ElysiumCameraSolve.h`.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraTrackerTest, "Elysium.Substrate.CameraTracker",
	GElysiumTestFlags)
bool FElysiumCameraTrackerTest::RunTest(const FString&)
{
	using FTracker = FElysiumScriptedShotTracker;

	// `jack.txt`'s constraints, verbatim — the game's most-seen tracked shot.
	auto MakeJackShot = []()
	{
		FElysiumCameraShot Shot;
		Shot.bTracked = true;                       // `CamMode` 1
		Shot.Origin = FVector(0.0f, 0.0f, 165.1f);
		Shot.LookAt = Shot.Origin + FVector(300.0f, 0.0f, 0.0f);
		Shot.bUseLookAt = true;
		Shot.MoveSpeed = 500.0f * ElysiumCam::U;
		Shot.MoveAccel = 250.0f * ElysiumCam::U;
		Shot.TurnAccel = 30.0f;
		Shot.MaxTurnRate = FVector(60.0f, 60.0f, 60.0f);
		Shot.DistanceTolerance = 5.0f * ElysiumCam::U;
		Shot.AngularTolerance = FVector(10.0f, 10.0f, 10.0f);
		return Shot;
	};

	// --- the 1-degree acquire band (`FUN_10001d40`, `tol = settled ? AngularTolerance[i] : 1.0f`) ---
	{
		TestEqual(TEXT("the unsettled angular tolerance is retail's 1.0 degree"),
			FTracker::UnsettledAngleTolerance, 1.0f);

		FElysiumCameraShot Shot = MakeJackShot();
		FTracker Tracker;
		Tracker.Start(Shot);
		const FRotator Framed = Tracker.Rotation;
		TestFalse(TEXT("shot start leaves the aim axes unsettled, so they acquire first"),
			Tracker.bYawSettled);

		// Half a degree of goal error: inside retail's acquire band, outside the port's old 0.05.
		Shot.LookAt = Shot.Origin
			+ FVector(300.0f, 0.0f, 0.0f).RotateAngleAxis(0.5f, FVector::ZAxisVector);
		TestTrue(TEXT("0.5 degrees is inside the 1-degree band and outside the old 0.05 one"),
			0.5f < FTracker::UnsettledAngleTolerance && 0.5f > 0.05f);
		Tracker.Advance(Shot, 1.0f / 60.0f);
		TestTrue(TEXT("an unsettled axis half a degree off its goal settles rather than turning"),
			Tracker.bYawSettled);
		TestTrue(TEXT("and the camera does not move at all"), Tracker.Rotation.Equals(Framed, 0.001f));
		TestTrue(TEXT("its turn rate is zeroed in the settled arm"),
			FMath::IsNearlyZero(Tracker.TurnRate.Y));
	}

	// --- the 1.0 u/s speed floor (`clamp(speed, 1.0f, MoveSpeed)`, 0x10002137-0x1000215a) ---
	{
		TestTrue(TEXT("the speed floor is 1.0 Source unit per second"),
			FMath::IsNearlyEqual(FTracker::MinTrackSpeed, 2.54f, 0.0001f));

		// An accelerating frame whose un-floored approach would be almost nothing.
		FElysiumCameraShot Crawl = MakeJackShot();
		Crawl.MoveSpeed = 500.0f * ElysiumCam::U;
		Crawl.MoveAccel = 0.001f;                 // cm/s^2 — a rounding error of an acceleration
		Crawl.DistanceTolerance = 0.0f;
		FTracker Slow;
		Slow.Start(Crawl);
		Crawl.Origin += FVector(100.0f, 0.0f, 0.0f);
		Slow.Advance(Crawl, 1.0f / 60.0f);
		TestEqual(TEXT("an unsettled camera never runs below the 1 u/s floor"),
			Slow.Speed, FTracker::MinTrackSpeed);
		TestTrue(TEXT("and it advances by exactly the floor's step"),
			FMath::IsNearlyEqual(static_cast<float>(Slow.Location.X),
				FTracker::MinTrackSpeed / 60.0f, 0.0001f));

		// A decelerating frame — retail's other arm — is floored by the same clamp.
		FElysiumCameraShot Braking = MakeJackShot();
		Braking.MoveSpeed = 500.0f * ElysiumCam::U;
		Braking.MoveAccel = 1.0f;                 // stopDist = v^2/2a >= dist, so control decelerates
		Braking.DistanceTolerance = 0.0f;
		FTracker Stopping;
		Stopping.Start(Braking);
		Stopping.bPositionSettled = false;
		Stopping.Speed = FTracker::MinTrackSpeed;
		Braking.Origin += FVector(3.0f, 0.0f, 0.0f);
		Stopping.Advance(Braking, 1.0f / 60.0f);
		TestEqual(TEXT("the decel arm is floored by the same clamp, not driven to zero"),
			Stopping.Speed, FTracker::MinTrackSpeed);

		// The floor is what makes `FUN_100019a0`'s `speed <= 1.0` a clean "is the camera dollying".
		TestFalse(TEXT("a camera exactly at the floor reads as not dollying"), Stopping.IsDollying());
		Stopping.Speed = FTracker::MinTrackSpeed * 10.0f;
		TestTrue(TEXT("a camera above the floor reads as dollying"), Stopping.IsDollying());
	}

	// --- the one-shot snap (`FUN_10002390`, armed at 0x100024c0 / the tail of FUN_10002210) ---
	{
		// `sp_tutorial_1`'s `LookAtTarget_Snap` shape: it cuts on the shot change and then **tracks**
		// its anchor. Re-applying the snap every frame is what stopped it tracking at all.
		FElysiumCameraShot Snap = MakeJackShot();
		Snap.bSnapOnShotChange = true;
		Snap.MoveSpeed = 150.0f * ElysiumCam::U;
		Snap.MoveAccel = 50.0f * ElysiumCam::U;
		Snap.DistanceTolerance = 1.0f * ElysiumCam::U;
		Snap.AngularTolerance = FVector(1.0f, 1.0f, 1.0f);

		FTracker Tracker;
		Tracker.Start(Snap);
		TestTrue(TEXT("the shot change arms the pending snap"), Tracker.bSnapPending);

		// The anchor is somewhere else entirely by the time the first frame runs: that frame cuts.
		Snap.Origin = FVector(-500.0f, 250.0f, 100.0f);
		Snap.LookAt = Snap.Origin + FVector(0.0f, 300.0f, 0.0f);
		Tracker.Advance(Snap, 1.0f / 60.0f);
		TestTrue(TEXT("the pending frame hard-copies the goal"),
			Tracker.Location.Equals(Snap.Origin, 0.001f));
		TestTrue(TEXT("and re-derives the aim from the look-at, because the shot is CamMode 1"),
			FMath::IsNearlyEqual(FRotator::NormalizeAxis(Tracker.Rotation.Yaw), 90.0f, 0.001f));
		TestFalse(TEXT("the one-shot is consumed"), Tracker.bSnapPending);

		// The hard copy itself leaves all three axes **unsettled** (retail clears `m_bAngleSettled`
		// rather than setting it), so the frame after a cut acquires at the tight band. Asserted on
		// the entry point directly, because the same frame's angle step re-settles a zero error.
		FTracker Direct;
		Direct.Start(Snap);
		Direct.bPitchSettled = Direct.bYawSettled = Direct.bRollSettled = true;
		Direct.Speed = 123.0f;
		Direct.Snap(Snap);
		TestFalse(TEXT("the snap clears the yaw settle flag"), Direct.bYawSettled);
		TestFalse(TEXT("and the pitch one"), Direct.bPitchSettled);
		TestFalse(TEXT("and the roll one"), Direct.bRollSettled);
		TestTrue(TEXT("it settles the position and zeroes the speed"),
			Direct.bPositionSettled && Direct.Speed == 0.0f);

		// Ten frames of the anchor walking away. A snapping shot that still snapped would sit on the
		// goal every frame; a tracking one closes on it at its own rate and never reaches it here.
		const FVector CutOrigin = Tracker.Location;
		const FVector Walked = CutOrigin + FVector(0.0f, 4000.0f, 0.0f);
		Snap.Origin = Walked;
		Snap.LookAt = Walked + FVector(0.0f, 300.0f, 0.0f);
		for (int32 Frame = 0; Frame < 10; ++Frame)
		{
			Tracker.Advance(Snap, 1.0f / 60.0f);
		}
		TestFalse(TEXT("the frames after the cut track rather than snap"),
			Tracker.Location.Equals(Walked, 0.001f));
		TestTrue(TEXT("and they do move toward the anchor"),
			Tracker.Location.Y > CutOrigin.Y + 0.1f);
		TestFalse(TEXT("the camera is dollying while it closes"), Tracker.bPositionSettled);
	}

	// --- the two frame-delta guards (`FUN_10001a20`) ---
	{
		// Measured through the `MoveAccel == 0` crawl, whose speed is pinned at the floor, so the
		// frame's advance is exactly `MinTrackSpeed * dt` and the effective delta is readable.
		auto AdvanceOneFrame = [](float DeltaSeconds)
		{
			FElysiumCameraShot Shot;
			Shot.bTracked = true;
			Shot.Origin = FVector::ZeroVector;
			Shot.bUseLookAt = false;
			Shot.MoveSpeed = 100.0f;
			Shot.MoveAccel = 0.0f;
			Shot.DistanceTolerance = 0.0f;
			FTracker Tracker;
			Tracker.Start(Shot);
			Shot.Origin = FVector(1000.0f, 0.0f, 0.0f);
			Tracker.Advance(Shot, DeltaSeconds);
			return static_cast<float>(Tracker.Location.X) / FTracker::MinTrackSpeed;
		};

		TestTrue(TEXT("a 5 second frame is clamped to the 1 second ceiling"),
			FMath::IsNearlyEqual(AdvanceOneFrame(5.0f), FTracker::FrameDeltaCeiling, 0.0001f));
		TestTrue(TEXT("a 0.1 ms frame is floored to 10 ms"),
			FMath::IsNearlyEqual(AdvanceOneFrame(0.0001f), FTracker::FrameDeltaFloor, 0.0001f));
		// The floor's compare constant and its stored literal are the **same** 0.01 (RC9 read
		// `_DAT_101e34e8` as `0a d7 23 3c`), so a 5 ms frame — well above the earlier 1/255 reading —
		// is floored too. That is a 200 Hz frame being advanced as if it were 100 Hz, and it is
		// retail's.
		TestTrue(TEXT("a 5 ms frame is floored to 10 ms too, because the threshold is 0.01"),
			FMath::IsNearlyEqual(AdvanceOneFrame(0.005f), FTracker::FrameDeltaFloor, 0.0001f));
		TestTrue(TEXT("a zero frame is floored to 10 ms, which is retail's zero handling"),
			FMath::IsNearlyEqual(AdvanceOneFrame(0.0f), FTracker::FrameDeltaFloor, 0.0001f));
		TestTrue(TEXT("and so is a negative one"),
			FMath::IsNearlyEqual(AdvanceOneFrame(-1.0f), FTracker::FrameDeltaFloor, 0.0001f));
		TestTrue(TEXT("an ordinary 60 Hz frame passes through untouched"),
			FMath::IsNearlyEqual(AdvanceOneFrame(1.0f / 60.0f), 1.0f / 60.0f, 0.0001f));
	}

	// --- `RemainingTime` verbatim, both defects (`FUN_100010f0`) — M6 ---
	{
		// `jack.txt` closing 100 u from rest. Units cancel, so this is the Source-unit call.
		const float Retail = ElysiumCam::RemainingTranslationSeconds(0.0f, 500.0f, 250.0f, 100.0f);
		TestTrue(TEXT("RemainingTime(0, 500, 250, 100) is retail's 0.17 s"),
			FMath::IsNearlyEqual(Retail, 0.1697f, 0.001f));

		// What a correct kinematic solve answers for the same move: accelerate to the midpoint at
		// 250 u/s^2 and brake. The port used to return this, so `jack.txt` panned ~7x slower.
		const float Correct = 2.0f * FMath::Sqrt(2.0f * 50.0f / 250.0f);
		TestTrue(TEXT("the correct solve is 1.27 s, which is the divergence M6 removes"),
			FMath::IsNearlyEqual(Correct, 1.2649f, 0.001f));
		TestTrue(TEXT("retail is about seven times faster than the truth"), Correct / Retail > 7.0f);

		// The NaN band: the parse defaults (MoveSpeed 150, MoveAccel 50) enter the triangle arm for
		// d <= 450 and drive the radicand negative past d - R2 > 4a = 200. Clamped, so finite.
		bool bAllFinite = true;
		for (float Distance = 200.0f; Distance <= 450.0f; Distance += 5.0f)
		{
			const float T = ElysiumCam::RemainingTranslationSeconds(0.0f, 150.0f, 50.0f, Distance);
			bAllFinite &= FMath::IsFinite(T) && !FMath::IsNaN(T) && T >= 0.0f;
		}
		TestTrue(TEXT("the clamped radicand is finite across the whole MoveSpeed > 2*MoveAccel band"),
			bAllFinite);
		TestEqual(TEXT("and the radicand's zero crossing is exactly d - R2 == 4a"),
			ElysiumCam::RemainingTranslationSeconds(0.0f, 150.0f, 50.0f, 200.0f), 0.0f);

		// The trapezoid arm is entered when 2*R1 < d, and it carries retail's `(vmax - v)^2/(2a)`.
		const float Trapezoid = ElysiumCam::RemainingTranslationSeconds(0.0f, 150.0f, 50.0f, 900.0f);
		TestTrue(TEXT("the trapezoid arm answers finitely too"),
			FMath::IsFinite(Trapezoid) && Trapezoid > 0.0f);

		// And the `SyncRotateOnMove` gate that divides by it never produces a NaN pose.
		FElysiumCameraShot Sync = MakeJackShot();
		Sync.MoveSpeed = 150.0f * ElysiumCam::U;
		Sync.MoveAccel = 50.0f * ElysiumCam::U;
		Sync.bSyncRotateOnMove = true;
		Sync.DistanceTolerance = 0.0f;
		FTracker SyncTracker;
		SyncTracker.Start(Sync);
		Sync.Origin += FVector(300.0f * ElysiumCam::U, 0.0f, 0.0f);
		Sync.LookAt = Sync.Origin + FVector(0.0f, 300.0f, 0.0f);
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			SyncTracker.Advance(Sync, 1.0f / 60.0f);
		}
		TestTrue(TEXT("a SyncRotateOnMove shot inside the NaN band keeps a finite pose"),
			SyncTracker.Location.ContainsNaN() == false && SyncTracker.Rotation.ContainsNaN() == false
				&& SyncTracker.TurnRate.ContainsNaN() == false);
	}

	// --- `MoveAccel == 0`: retail's crawl, through a branch (M7) ---
	{
		FElysiumCameraShot Crawl;
		Crawl.bTracked = true;
		Crawl.Origin = FVector::ZeroVector;
		Crawl.bUseLookAt = false;
		Crawl.MoveSpeed = 500.0f * ElysiumCam::U;
		Crawl.MoveAccel = 0.0f;                    // retail's `0/0` compare is false: the decel arm
		Crawl.DistanceTolerance = 0.0f;
		FTracker Tracker;
		Tracker.Start(Crawl);
		Crawl.Origin = FVector(1000.0f, 0.0f, 0.0f);

		Tracker.Advance(Crawl, 1.0f);
		TestEqual(TEXT("MoveAccel 0 pins the speed at the 1 u/s floor, exactly as retail"),
			Tracker.Speed, FTracker::MinTrackSpeed);
		TestTrue(TEXT("so the camera crawls 2.54 cm in a second and does not reach MoveSpeed"),
			FMath::IsNearlyEqual(static_cast<float>(Tracker.Location.X), 2.54f, 0.0001f));
		TestTrue(TEXT("and nothing on that path divides: no NaN, no infinity"),
			FMath::IsFinite(Tracker.Speed) && !Tracker.Location.ContainsNaN());

		// A hundred seconds of it is still a crawl — the arm never falls through to MoveSpeed.
		for (int32 Frame = 0; Frame < 100; ++Frame)
		{
			Tracker.Advance(Crawl, 1.0f);
		}
		TestTrue(TEXT("a hundred seconds of the crawl covers 2.54 m, not the shot's 1270 cm/s"),
			static_cast<float>(Tracker.Location.X) < 300.0f);
	}

	// --- FOV: a copy every frame, and the dev-cvar freeze (`FUN_10001c20`) — M12 ---
	{
		FElysiumCameraShot Shot = MakeJackShot();
		Shot.FieldOfView = 40.0f;
		FTracker Tracker;
		Tracker.Start(Shot);
		TestEqual(TEXT("shot start seeds the cached FOV"), Tracker.Fov, 40.0f);

		Shot.FieldOfView = 55.0f;
		Tracker.Advance(Shot, 1.0f / 60.0f);
		TestEqual(TEXT("the FOV is copied from the shot record every frame, never lerped"),
			Tracker.Fov, 55.0f);
		Shot.FieldOfView = 20.0f;
		Tracker.Advance(Shot, 1.0f / 60.0f);
		TestEqual(TEXT("a second frame copies the new value outright"), Tracker.Fov, 20.0f);

		// The guard is retail's own ConVar `camera_fov` (`client.dll` object `0x102de308`, name string
		// `0x10270c88`, default `"-1"`, flags 0) and its threshold is `_DAT_101e34f4` = **10.0**, both
		// read byte-exact by RC9. It therefore lives in the VtMB console store like every other
		// retail-named cvar, not as an `elysium.*` engine one — M12 closes.
		TestEqual(TEXT("the guard threshold is retail's 10.0"), FTracker::FovOverrideThreshold, 10.0f);
		TestNull(TEXT("and the elysium.* placeholder is gone"),
			IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.CameraShotFovOverride")));

		bool bCameraFovDeclared = false;
		for (const ElysiumCam::FCvarDef& Def : ElysiumCam::CvarDefs())
		{
			bCameraFovDeclared |= FString(Def.Name) == TEXT("camera_fov")
				&& FString(Def.Default) == TEXT("-1");
		}
		TestTrue(TEXT("camera_fov is declared in the VtMB store at retail's -1"), bCameraFovDeclared);

		// The shipped default never fires: -1 is not above 10.
		FElysiumCameraCvars Lens;
		TestTrue(TEXT("the default is below the threshold, so a stock run never freezes"),
			Lens.CameraFov <= FTracker::FovOverrideThreshold);

		// A console-store write reaches the loaded value, which is what the component hands the
		// tracker every frame.
		TMap<FString, FString> Cfg;
		Cfg.Add(TEXT("camera_fov"), TEXT("90"));
		Lens.LoadFrom([&Cfg](const TCHAR* Name) -> FString
		{
			const FString* Hit = Cfg.Find(Name);
			return Hit ? *Hit : FString();
		});
		TestEqual(TEXT("a camera_fov write reaches the cvar surface unclamped"), Lens.CameraFov, 90.0f);

		Shot.FieldOfView = 25.0f;
		TestEqual(TEXT("the guard returns the cvar"), Tracker.TrackFov(Shot, Lens.CameraFov), 90.0f);
		Tracker.Advance(Shot, 1.0f / 60.0f, Lens.CameraFov);
		TestEqual(TEXT("and freezes the cached FOV at its previous value, as retail does"),
			Tracker.Fov, 20.0f);

		// Exactly at the threshold the guard does NOT fire — the compare is strictly greater.
		Tracker.Advance(Shot, 1.0f / 60.0f, 10.0f);
		TestEqual(TEXT("camera_fov exactly at 10 is not above it, so the copy resumes"),
			Tracker.Fov, 25.0f);

		Shot.FieldOfView = 30.0f;
		Tracker.Advance(Shot, 1.0f / 60.0f, -1.0f);
		TestEqual(TEXT("and the shipped default copies the record outright"), Tracker.Fov, 30.0f);
	}

	// --- the frame latch: one Advance per rendered frame (`m_nFrameCache`, FUN_10001a20) ---
	{
		// The port already has the latch, by a different route: `UElysiumCameraComponent::AdvanceFrame`
		// and `UElysiumCameraService::Advance` both stamp `GFrameCounter` and return early on a repeat,
		// and `UElysiumCameraModifier::ModifyCamera` zeroes the delta on the second pass. This slice
		// asserts it rather than rebuilding it, because the tracker's whole state is per-frame.
		UElysiumCameraComponent* Latched = NewObject<UElysiumCameraComponent>();
		if (TestNotNull(TEXT("the frame-latch case has a camera component"), Latched))
		{
			FElysiumCameraShot Shot;
			Shot.BlendSeconds = 1.0f;
			Latched->PushShot(Shot);
			Latched->AdvanceFrame(0.5f);
			const float AfterOne = Latched->GetShots().GetWeight();
			TestTrue(TEXT("one advance takes half of a one-second ramp"),
				FMath::IsNearlyEqual(AfterOne, 0.5f, 0.001f));
			Latched->AdvanceFrame(0.5f);
			TestEqual(TEXT("a second pass in the same rendered frame advances nothing"),
				Latched->GetShots().GetWeight(), AfterOne);
		}
	}

	return true;
}

// camera_track — paired value streams and the recovered keyframe timing surface.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraTrackTest, "Elysium.Substrate.CameraTrack", GElysiumTestFlags)
bool FElysiumCameraTrackTest::RunTest(const FString&)
{
	using namespace ElysiumCameraTrack;

	FPoint A;
	A.Position = FVector::ZeroVector;
	A.bTimeControl = true;
	A.MoveTime = 2.0f;
	A.Pause = 1.0f;
	A.Roll = 170.0f;
	A.FocalLength = 50.0f;
	FPoint B = A;
	B.Position = FVector(100.0f, 0.0f, 0.0f);
	B.Pause = 0.5f;
	B.Roll = -170.0f;
	B.FocalLength = 25.0f;

	FPath Timed;
	Timed.Points = { A, B };
	Timed.RebuildTimes();
	TestEqual(TEXT("the root is reached immediately and leaves after its authored pause"),
		Timed.Departures[0], 1.0f);
	TestEqual(TEXT("TimeControl movement starts after the root dwell"), Timed.Arrivals[1], 3.0f);
	TestEqual(TEXT("the destination pause extends completion"), Timed.EndTime, 3.5f);

	FSample Sample;
	TestTrue(TEXT("the root sample is available during its dwell"), Timed.Sample(0.5f, Sample));
	TestTrue(TEXT("the root is held for its authored pause"), Sample.Position.Equals(A.Position, 0.01f));
	TestTrue(TEXT("the midpoint samples between endpoints after the dwell"), Timed.Sample(2.0f, Sample));
	TestTrue(TEXT("linear rate defaults put the two-point Catmull midpoint at 50"),
		FMath::IsNearlyEqual(Sample.Position.X, 50.0f, 0.01f));
	// Roll is NOT unwrapped onto the shortest path. Retail normalises each key's roll once at spawn
	// and then Catmulls the plain values, so 170 -> -170 sweeps the long way through zero rather than
	// the short 20 degrees across 180. Reproduced rather than corrected: the sweep is what the shot
	// was authored against.
	TestTrue(TEXT("roll interpolates plainly, so 170 to -170 passes through zero"),
		FMath::Abs(Sample.Roll) < 0.1f);
	TestTrue(TEXT("a positive lens becomes a horizontal FOV"), Sample.FieldOfView > 0.0f);
	// Converted before interpolating, not after: the midpoint of a 50mm and a 25mm key is the mean of
	// their two FOVs, not the FOV of the mean focal length (which would read ~52.4 degrees).
	TestTrue(TEXT("the FOV is interpolated, not the focal length"),
		FMath::IsNearlyEqual(Sample.FieldOfView,
			0.5f * (FocalLengthToHorizontalFov(50.0f) + FocalLengthToHorizontalFov(25.0f)), 0.05f));
	TestTrue(TEXT("the path is complete after the destination pause"), Timed.Sample(3.5f, Sample) && Sample.bFinished);

	FPoint SpeedA;
	SpeedA.Position = FVector::ZeroVector;
	SpeedA.MoveSpeed = 50.0f;
	FPoint SpeedB = SpeedA;
	SpeedB.Position = FVector(254.0f, 0.0f, 0.0f);
	TestTrue(TEXT("speed timing converts Source units per second exactly once"),
		FMath::IsNearlyEqual(SegmentSeconds(SpeedA, SpeedB), 2.0f, 0.001f));
	TestTrue(TEXT("RateOut/RateIn use the recovered endpoint-slope cubic"),
		FMath::IsNearlyEqual(EaseRate(0.25f, 2.0f, 1.0f), 0.390625f, KINDA_SMALL_NUMBER));
	SpeedB.MoveSpeed = 150.0f;
	TestTrue(TEXT("a smooth destination averages endpoint speeds"),
		FMath::IsNearlyEqual(SegmentSeconds(SpeedA, SpeedB), 1.0f, 0.001f));
	SpeedB.bCorner = true;
	TestTrue(TEXT("a corner destination uses the source speed"),
		FMath::IsNearlyEqual(SegmentSeconds(SpeedA, SpeedB), 2.0f, 0.001f));
	TestTrue(TEXT("50mm on a 36mm horizontal gate is about 39.6 degrees"),
		FMath::IsNearlyEqual(FocalLengthToHorizontalFov(50.0f), 39.5978f, 0.01f));

	FPath Cut;
	A.Pause = 0.0f;
	A.MoveTime = 0.0f;
	B.Pause = 0.0f;
	Cut.Points = { A, B };
	Cut.RebuildTimes();
	TestTrue(TEXT("a zero-duration chain reaches its final key immediately"),
		Cut.Sample(0.0f, Sample) && Sample.bFinished && Sample.Position.Equals(B.Position, 0.01f));

	// `CCameraKeyFrame::Activate`'s fold. sp_theatre's courtroom chain authors 87 of its edits as
	// `MoveTime 0.03` and sm_gallery_1 writes 7 as `0.01`; retail's threshold is 0.05, so all of them
	// are rewritten to true zero-time edits at spawn and none survives to be interpolated.
	TestTrue(TEXT("a 0.03 TimeControl segment folds to an edit"), ShouldFold(true, 0.03f));
	TestTrue(TEXT("a 0.01 TimeControl segment folds to an edit"), ShouldFold(true, 0.01f));
	TestTrue(TEXT("retail's threshold is inclusive at 0.05"), ShouldFold(true, 0.05f));
	TestFalse(TEXT("just above the threshold stays camera movement"), ShouldFold(true, 0.0501f));
	TestFalse(TEXT("the fold only applies to TimeControl segments"), ShouldFold(false, 0.03f));
	TestTrue(TEXT("0.1 — the shortest authored move in the corpus — is movement"),
		!ShouldFold(true, 0.1f));

	// What the sampler sees after the fold: MoveTime zeroed, so the segment consumes no time and the
	// destination becomes current immediately. The folded 0.03 s is re-attributed to a pause by the
	// entity, which is where the chain's clock keeps it.
	FPath Folded;
	A.MoveTime = 0.0f;
	A.Pause = 0.0f;
	B.Position = FVector(900.0f, -400.0f, 200.0f);
	B.Pause = 0.03f;   // the re-attributed edit time, now spent AFTER the cut rather than before
	Folded.Points = { A, B };
	Folded.RebuildTimes();
	TestTrue(TEXT("a folded edit is a hard cut at the sampler"), IsHardCut(A));
	TestTrue(TEXT("the folded edit switches to the destination immediately"),
		Folded.Sample(0.0f, Sample) && Sample.Position.Equals(B.Position, 0.01f));
	TestTrue(TEXT("the re-attributed time is held on the destination key"),
		FMath::IsNearlyEqual(Folded.EndTime, 0.03f, KINDA_SMALL_NUMBER));

	// Above the threshold the segment is a real move — the theatre's own dollies run 0.3 to 15.5.
	FPath ShortMove;
	A.MoveTime = 0.3f;
	B.Pause = 0.0f;
	ShortMove.Points = { A, B };
	ShortMove.RebuildTimes();
	TestFalse(TEXT("a MoveTime above the fold threshold remains camera movement"), IsHardCut(A));
	TestTrue(TEXT("the short move samples between the authored endpoints"),
		ShortMove.Sample(0.15f, Sample)
			&& !Sample.Position.Equals(A.Position, 0.01f)
			&& !Sample.Position.Equals(B.Position, 0.01f));
	TestFalse(TEXT("camera movement never requests a temporal camera cut"),
		CrossesHardCut(ShortMove, 0.0f, 0.3f));

	FPath ZeroCut;
	A.MoveTime = 0.0f;
	ZeroCut.Points = { A, B };
	ZeroCut.RebuildTimes();
	TestTrue(TEXT("an authored zero-time transition is classified as a hard cut"), IsHardCut(A));
	TestTrue(TEXT("a zero-time edit switches to the destination immediately"),
		ZeroCut.Sample(0.0f, Sample) && Sample.Position.Equals(B.Position, 0.01f));
	TestTrue(TEXT("forward playback reports the zero-time edit exactly once"),
		CrossesHardCut(ZeroCut, -KINDA_SMALL_NUMBER, 0.0f));
	TestFalse(TEXT("a sampled hard cut is not reported again on the next frame"),
		CrossesHardCut(ZeroCut, 0.0f, 0.1f));

	// The A/B: at a threshold of 0 nothing folds, which leaves the theatre's edits as 30-millisecond
	// slews. An exact authored zero is still a cut either way — that one never needed folding.
	// R4.5 moved the threshold off a cvar onto `UElysiumChoreoSettings::CameraCutSeconds`; the CDO
	// is mutated and restored directly.
	{
		UElysiumChoreoSettings* ChoreoSettings = GetMutableDefault<UElysiumChoreoSettings>();
		const float Restore = ChoreoSettings->CameraCutSeconds;
		ChoreoSettings->CameraCutSeconds = 0.0f;
		TestFalse(TEXT("CameraCutSeconds 0 disables the fold"), ShouldFold(true, 0.03f));
		A.MoveTime = 0.0f;
		TestTrue(TEXT("an exact zero is a cut with the fold disabled"), IsHardCut(A));
		ChoreoSettings->CameraCutSeconds = Restore;
	}

	UElysiumCameraComponent* TemporalCamera = NewObject<UElysiumCameraComponent>();
	TestNotNull(TEXT("the temporal-cut phase test has a camera component"), TemporalCamera);
	if (TemporalCamera)
	{
		FElysiumCameraShot TemporalShot;
		TemporalShot.BlendSeconds = 0.0f;
		const int32 TemporalShotId = TemporalCamera->PushShot(TemporalShot);
		TestTrue(TEXT("a zero-blend push latches a cut until the camera apply phase"),
			TemporalCamera->ConsumeTemporalCameraCutRequest());
		TestFalse(TEXT("the camera apply phase consumes the cut exactly once"),
			TemporalCamera->ConsumeTemporalCameraCutRequest());
		// camera_track publishes a new value every frame. Exercise an updated origin, look-at and lens
		// before the component solves so the rendered-view seam, not only the value stack, is covered.
		TemporalShot.Origin = FVector(120.0f, -30.0f, 45.0f);
		TemporalShot.LookAt = FVector(220.0f, -30.0f, 45.0f);
		TemporalShot.bUseLookAt = true;
		TemporalShot.FieldOfView = FocalLengthToHorizontalFov(35.0f);
		TestTrue(TEXT("a moving track value reaches the live camera shot"),
			TemporalCamera->UpdateShot(TemporalShotId, TemporalShot));
		// Phase one is all this case needs: the shot chase and the stack's weight. The fade band is
		// phase two's and reads a boom no rig has solved here.
		TemporalCamera->AdvanceFrame(1.0f / 60.0f);
		FMinimalViewInfo ScriptedView;
		ScriptedView.PostProcessSettings.MotionBlurAmount = 0.5f;
		TemporalCamera->ApplyToView(ScriptedView);
		TestTrue(TEXT("the refreshed track origin reaches the rendered view"),
			ScriptedView.Location.Equals(TemporalShot.Origin, 0.01f));
		TestTrue(TEXT("the refreshed track target reaches the rendered view"),
			ScriptedView.Rotation.Equals((TemporalShot.LookAt - TemporalShot.Origin).Rotation(), 0.01f));
		TestTrue(TEXT("the refreshed track lens reaches the rendered view"),
			FMath::IsNearlyEqual(ScriptedView.FOV, TemporalShot.FieldOfView, 0.001f));
		TestTrue(TEXT("a visible scripted shot overrides camera motion blur"),
			ScriptedView.PostProcessSettings.bOverride_MotionBlurAmount);
		TestEqual(TEXT("scripted camera edits and dollies render without radial smear"),
			ScriptedView.PostProcessSettings.MotionBlurAmount, 0.0f);

		TemporalShot.BlendSeconds = 1.0f;
		TemporalShot.bCameraCut = true;
		TestTrue(TEXT("an authored cut update reaches the pending-until-apply latch"),
			TemporalCamera->UpdateShot(TemporalShotId, TemporalShot));
		TestTrue(TEXT("the authored cut remains pending until camera application"),
			TemporalCamera->ConsumeTemporalCameraCutRequest());
		TemporalShot.bCameraCut = false;
		TemporalCamera->UpdateShot(TemporalShotId, TemporalShot);
		TestFalse(TEXT("an ordinary value update does not invent a temporal cut"),
			TemporalCamera->ConsumeTemporalCameraCutRequest());
		TestTrue(TEXT("the focused shot can be popped"),
			TemporalCamera->PopShot(TemporalShotId, 0.0f));
		TestTrue(TEXT("a zero-blend pop also latches a cut until camera application"),
			TemporalCamera->ConsumeTemporalCameraCutRequest());
	}

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__camera_track_test__");
	FElysiumEntityDef Pos;
	Pos.Classname = TEXT("camera_track");
	Pos.TargetName = TEXT("pos");
	Pos.Origin = FVector(10.0f, 20.0f, 30.0f);
	Pos.Keys.Add(TEXT("HoldAtEnd"), TEXT("1"));
	Pos.Keys.Add(TEXT("FocalLength"), TEXT("50"));
	Pos.Keys.Add(TEXT("FromPlayerTime"), TEXT("0.25"));
	Pos.Keys.Add(TEXT("ToPlayerTime"), TEXT("0.75"));
	FElysiumOutputDef Completed;
	Completed.Name = TEXT("OnAnimationCompleted");
	Completed.Target = TEXT("completed");
	Completed.Input = TEXT("Add");
	Completed.Param = TEXT("1");
	Pos.Outputs.Add(Completed);
	FElysiumOutputDef Reached = Completed;
	Reached.Name = TEXT("OnReachedKeyframe");
	Reached.Target = TEXT("reached");
	Pos.Outputs.Add(Reached);
	Defs.Defs.Add(Pos);
	FElysiumEntityDef Target = Pos;
	Target.TargetName = TEXT("target");
	Target.Origin = FVector(100.0f, 200.0f, 300.0f);
	Target.Outputs.Reset();
	Defs.Defs.Add(Target);
	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("completed");
	Defs.Defs.Add(Counter);
	Counter.TargetName = TEXT("reached");
	Defs.Defs.Add(Counter);

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);
	World.AcceptInput(TEXT("pos"), FName(TEXT("PlayAsCameraPosition")), FElysiumVariant::Void(),
		FElysiumEntityHandle(), FElysiumEntityHandle());
	TestEqual(TEXT("the first role pushes one value shot"), Services.Count(TEXT("PushCameraShotValue")), 1);
	World.Tick(0.0);
	auto CounterValue = [](FElysiumEntityWorld& CounterWorld, const TCHAR* Name)
	{
		TArray<TPair<FString, FString>> Rows;
		if (FElysiumEntity* CounterEnt = CounterWorld.FindByName(Name))
		{
			CounterEnt->GetDebugState(Rows);
		}
		for (const TPair<FString, FString>& Row : Rows)
		{
			if (Row.Key == TEXT("Value")) { return FCString::Atof(*Row.Value); }
		}
		return -1.0f;
	};
	TestEqual(TEXT("zero-duration completion fires once"), CounterValue(World, TEXT("completed")), 1.0f);
	TArray<TPair<FString, FString>> ReachedRows;
	World.FindByName(TEXT("reached"))->GetDebugState(ReachedRows);
	TestTrue(TEXT("arrival fires the authored OnReachedKeyframe output"),
		ReachedRows.ContainsByPredicate([](const TPair<FString, FString>& Row)
		{
			return Row.Key == TEXT("Value") && FMath::IsNearlyEqual(FCString::Atof(*Row.Value), 1.0f);
		}));
	World.Tick(1.0);
	TestEqual(TEXT("held completion does not fire again"), CounterValue(World, TEXT("completed")), 1.0f);
	World.AcceptInput(TEXT("target"), FName(TEXT("PlayAsCameraTarget")), FElysiumVariant::Void(),
		FElysiumEntityHandle(), FElysiumEntityHandle());
	TestEqual(TEXT("the paired role updates rather than pushes another shot"),
		Services.Count(TEXT("PushCameraShotValue")), 1);
	TestTrue(TEXT("the composed value uses the authored target"),
		Services.LastCameraShot.bUseLookAt && Services.LastCameraShot.LookAt.Equals(Target.Origin, 0.01f));
	TestFalse(TEXT("the composed track shot is direct — retail's CInput override, not the C_BaseCineCamera tracker"),
		Services.LastCameraShot.bTracked);
	World.AcceptInput(TEXT("pos"), FName(TEXT("RestoreCameraToPlayerControl")), FElysiumVariant::Float(1.25f),
		FElysiumEntityHandle(), FElysiumEntityHandle());
	TestFalse(TEXT("restoring the current position returns the complete camera pair to the player"),
		World.HasTrackCamera());
	World.AcceptInput(TEXT("target"), FName(TEXT("Restore")), FElysiumVariant::Float(1.25f),
		FElysiumEntityHandle(), FElysiumEntityHandle());
	TestEqual(TEXT("a stale target restore cannot pop the camera twice"),
		Services.Count(TEXT("PopCameraShot")), 1);
	TestTrue(TEXT("the explicit Restore parameter overrides ToPlayerTime"),
		Services.Saw(TEXT("PopCameraShot 1 blend=1.25")));

	const FElysiumEntity* PosEntity = World.FindByName(TEXT("pos"));
	TestNotNull(TEXT("the camera owner remains addressable"), PosEntity);
	if (PosEntity)
	{
		World.SelectTrackCameraRole(false, PosEntity->Handle);
		World.PublishTrackCamera(false, PosEntity->Handle, FVector(1.0f, 2.0f, 3.0f),
			FRotator::ZeroRotator, 0.0f, 60.0f, 0.0f, true);
		TestTrue(TEXT("the world carries a hard-cut instruction onto the value shot"),
			Services.LastCameraShot.bCameraCut);
		World.PublishTrackCamera(false, PosEntity->Handle, FVector(2.0f, 3.0f, 4.0f),
			FRotator::ZeroRotator, 0.0f, 60.0f, 0.0f);
		TestFalse(TEXT("the temporal cut instruction is one-shot"),
			Services.LastCameraShot.bCameraCut);
	}

	// sp_tutorial_1's lockpick focus uses two roots: trackc1 reaches its tail first and holds the
	// position, while focusc1 dwells for another second and then returns player control. The old
	// role-local completion left the held position lease alive forever.
	FElysiumEntityDefs LockpickDefs;
	LockpickDefs.MapName = TEXT("__sp_tutorial_1_lockpick_camera_test__");
	FElysiumEntityDef LockpickPosition;
	LockpickPosition.Classname = TEXT("camera_track");
	LockpickPosition.TargetName = TEXT("trackc1");
	LockpickPosition.Keys.Add(TEXT("TimeControl"), TEXT("1"));
	LockpickPosition.Keys.Add(TEXT("MoveTime"), TEXT("0.1"));
	LockpickPosition.Keys.Add(TEXT("NextKey"), TEXT("trackc2"));
	LockpickPosition.Keys.Add(TEXT("HoldAtEnd"), TEXT("1"));
	LockpickDefs.Defs.Add(MoveTemp(LockpickPosition));
	FElysiumEntityDef LockpickPositionEnd;
	LockpickPositionEnd.Classname = TEXT("camera_keyframe");
	LockpickPositionEnd.TargetName = TEXT("trackc2");
	LockpickPositionEnd.Origin = FVector(10.0f, 0.0f, 0.0f);
	LockpickDefs.Defs.Add(MoveTemp(LockpickPositionEnd));

	FElysiumEntityDef LockpickTarget;
	LockpickTarget.Classname = TEXT("camera_track");
	LockpickTarget.TargetName = TEXT("focusc1");
	LockpickTarget.Origin = FVector(0.0f, 100.0f, 0.0f);
	LockpickTarget.Keys.Add(TEXT("TimeControl"), TEXT("1"));
	LockpickTarget.Keys.Add(TEXT("MoveTime"), TEXT("0.1"));
	LockpickTarget.Keys.Add(TEXT("NextKey"), TEXT("focusc2"));
	LockpickTarget.Keys.Add(TEXT("HoldAtEnd"), TEXT("0"));
	FElysiumOutputDef LockpickRestore;
	LockpickRestore.Name = TEXT("OnAnimationCompleted");
	LockpickRestore.Target = TEXT("focusc1");
	LockpickRestore.Input = TEXT("RestoreCameraToPlayerControl");
	LockpickRestore.Param = TEXT("0.50");
	LockpickRestore.Delay = 1.0f;
	LockpickTarget.Outputs.Add(MoveTemp(LockpickRestore));
	LockpickDefs.Defs.Add(MoveTemp(LockpickTarget));
	FElysiumEntityDef LockpickTargetEnd;
	LockpickTargetEnd.Classname = TEXT("camera_keyframe");
	LockpickTargetEnd.TargetName = TEXT("focusc2");
	LockpickTargetEnd.Origin = FVector(0.0f, 110.0f, 0.0f);
	LockpickTargetEnd.Keys.Add(TEXT("Pause"), TEXT("1"));
	LockpickDefs.Defs.Add(MoveTemp(LockpickTargetEnd));

	FElysiumRecordingServices LockpickServices;
	LockpickServices.bHasPlayer = true;
	FElysiumEntityWorld LockpickWorld(nullptr, nullptr, LockpickServices.Bundle());
	LockpickWorld.Load(MoveTemp(LockpickDefs));
	LockpickWorld.Activate(0.0);
	LockpickWorld.AcceptInput(TEXT("focusc1"), FName(TEXT("PlayAsCameraTarget")),
		FElysiumVariant::Void(), FElysiumEntityHandle(), FElysiumEntityHandle());
	LockpickWorld.AcceptInput(TEXT("trackc1"), FName(TEXT("PlayAsCameraPosition")),
		FElysiumVariant::Void(), FElysiumEntityHandle(), FElysiumEntityHandle());
	LockpickWorld.Tick(0.2);
	TestTrue(TEXT("the held lockpick position remains while the focus endpoint dwells"),
		LockpickWorld.HasTrackCamera());
	LockpickWorld.Tick(1.2);
	TestFalse(TEXT("the non-held focus completion returns the lockpick camera to the player"),
		LockpickWorld.HasTrackCamera());
	TestEqual(TEXT("the lockpick completion pops the composed shot exactly once"),
		LockpickServices.Count(TEXT("PopCameraShot")), 1);
	LockpickWorld.Tick(2.3);
	TestEqual(TEXT("the authored delayed restore remains a harmless stale no-op"),
		LockpickServices.Count(TEXT("PopCameraShot")), 1);

	// Retail starts on the root, fires OnReached immediately, dwells for Pause, then leaves. Keep
	// that output ordering covered through the entity-world clock as well as through the pure path.
	FElysiumEntityDefs DwellDefs;
	DwellDefs.MapName = TEXT("__camera_track_root_dwell_test__");
	FElysiumEntityDef Dwell;
	Dwell.Classname = TEXT("camera_track");
	Dwell.TargetName = TEXT("dwell");
	Dwell.Keys.Add(TEXT("TimeControl"), TEXT("1"));
	Dwell.Keys.Add(TEXT("MoveTime"), TEXT("2"));
	Dwell.Keys.Add(TEXT("Pause"), TEXT("1"));
	Dwell.Keys.Add(TEXT("NextKey"), TEXT("dwell_end"));
	FElysiumOutputDef DwellReached = Reached;
	DwellReached.Target = TEXT("dwell_reached");
	Dwell.Outputs.Add(DwellReached);
	FElysiumOutputDef DwellLeaving = Reached;
	DwellLeaving.Name = TEXT("OnLeavingKeyframe");
	DwellLeaving.Target = TEXT("dwell_left");
	Dwell.Outputs.Add(DwellLeaving);
	FElysiumOutputDef DwellCompleted = Completed;
	DwellCompleted.Target = TEXT("dwell_completed");
	Dwell.Outputs.Add(DwellCompleted);
	DwellDefs.Defs.Add(Dwell);

	FElysiumEntityDef DwellEnd;
	DwellEnd.Classname = TEXT("camera_keyframe");
	DwellEnd.TargetName = TEXT("dwell_end");
	DwellEnd.Origin = FVector(100.0f, 0.0f, 0.0f);
	DwellEnd.Keys.Add(TEXT("Pause"), TEXT("0.5"));
	FElysiumOutputDef DwellEndReached = Reached;
	DwellEndReached.Target = TEXT("dwell_end_reached");
	DwellEnd.Outputs.Add(DwellEndReached);
	DwellDefs.Defs.Add(DwellEnd);

	for (const TCHAR* Name : { TEXT("dwell_reached"), TEXT("dwell_left"),
		TEXT("dwell_end_reached"), TEXT("dwell_completed") })
	{
		FElysiumEntityDef DwellCounter;
		DwellCounter.Classname = TEXT("math_counter");
		DwellCounter.TargetName = Name;
		DwellDefs.Defs.Add(MoveTemp(DwellCounter));
	}

	FElysiumRecordingServices DwellServices;
	DwellServices.bHasPlayer = true;
	FElysiumEntityWorld DwellWorld(nullptr, nullptr, DwellServices.Bundle());
	DwellWorld.Load(MoveTemp(DwellDefs));
	DwellWorld.Activate(0.0);
	DwellWorld.AcceptInput(TEXT("dwell"), FName(TEXT("PlayAsCameraPosition")), FElysiumVariant::Void(),
		FElysiumEntityHandle(), FElysiumEntityHandle());
	DwellWorld.Tick(0.0);
	TestEqual(TEXT("the root OnReached output fires at play time"),
		CounterValue(DwellWorld, TEXT("dwell_reached")), 1.0f);
	TestEqual(TEXT("the root does not leave at play time"),
		CounterValue(DwellWorld, TEXT("dwell_left")), 0.0f);
	DwellWorld.Tick(0.5);
	TestEqual(TEXT("the root remains held inside its pause"),
		CounterValue(DwellWorld, TEXT("dwell_left")), 0.0f);
	DwellWorld.Tick(1.0);
	TestEqual(TEXT("the root OnLeaving output fires when its pause expires"),
		CounterValue(DwellWorld, TEXT("dwell_left")), 1.0f);
	DwellWorld.Tick(3.0);
	TestEqual(TEXT("the destination is reached after root pause plus MoveTime"),
		CounterValue(DwellWorld, TEXT("dwell_end_reached")), 1.0f);
	TestEqual(TEXT("the destination dwell delays completion"),
		CounterValue(DwellWorld, TEXT("dwell_completed")), 0.0f);
	DwellWorld.Tick(3.5);
	TestEqual(TEXT("completion follows the destination dwell"),
		CounterValue(DwellWorld, TEXT("dwell_completed")), 1.0f);

	// Position and target are exclusive retail player slots, not a last-writer-wins contest between
	// every live track. Put the old pair after the new pair in entity order: without selection leases,
	// their later Think calls reclaim both roles and their completion tears down the newer shot.
	FElysiumEntityDefs ReplacementDefs;
	ReplacementDefs.MapName = TEXT("__camera_track_replacement_test__");
	auto ReplacementTrack = [](const TCHAR* Name, const FVector& At, float Pause)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("camera_track");
		Def.TargetName = Name;
		Def.Origin = At;
		Def.Keys.Add(TEXT("Pause"), *FString::SanitizeFloat(Pause));
		return Def;
	};
	ReplacementDefs.Defs.Add(ReplacementTrack(TEXT("new_position"), FVector(100.0f, 0.0f, 0.0f), 3.0f));
	ReplacementDefs.Defs.Add(ReplacementTrack(TEXT("new_target"), FVector(200.0f, 0.0f, 0.0f), 3.0f));
	FElysiumEntityDef OldPosition = ReplacementTrack(TEXT("old_position"), FVector(10.0f, 0.0f, 0.0f), 2.0f);
	FElysiumOutputDef OldCompleted = Completed;
	OldCompleted.Target = TEXT("old_completed");
	OldPosition.Outputs.Add(OldCompleted);
	ReplacementDefs.Defs.Add(MoveTemp(OldPosition));
	ReplacementDefs.Defs.Add(ReplacementTrack(TEXT("old_target"), FVector(20.0f, 0.0f, 0.0f), 2.0f));
	FElysiumEntityDef OldCounter;
	OldCounter.Classname = TEXT("math_counter");
	OldCounter.TargetName = TEXT("old_completed");
	ReplacementDefs.Defs.Add(MoveTemp(OldCounter));

	FElysiumRecordingServices ReplacementServices;
	ReplacementServices.bHasPlayer = true;
	FElysiumEntityWorld ReplacementWorld(nullptr, nullptr, ReplacementServices.Bundle());
	ReplacementWorld.Load(MoveTemp(ReplacementDefs));
	ReplacementWorld.Activate(0.0);
	auto PlayReplacementRole = [&ReplacementWorld](const TCHAR* Name, const TCHAR* Input)
	{
		ReplacementWorld.AcceptInput(Name, FName(Input), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());
	};
	PlayReplacementRole(TEXT("old_target"), TEXT("PlayAsCameraTarget"));
	PlayReplacementRole(TEXT("old_position"), TEXT("PlayAsCameraPosition"));
	PlayReplacementRole(TEXT("new_target"), TEXT("PlayAsCameraTarget"));
	PlayReplacementRole(TEXT("new_position"), TEXT("PlayAsCameraPosition"));
	TestTrue(TEXT("the newer pair owns the composed shot immediately"),
		ReplacementServices.LastCameraShot.Origin.Equals(FVector(100.0f, 0.0f, 0.0f), 0.01f)
			&& ReplacementServices.LastCameraShot.bUseLookAt
			&& ReplacementServices.LastCameraShot.LookAt.Equals(FVector(200.0f, 0.0f, 0.0f), 0.01f));
	ReplacementWorld.Tick(0.5);
	TestTrue(TEXT("later entity-order thinks from superseded tracks cannot reclaim either role"),
		ReplacementServices.LastCameraShot.Origin.Equals(FVector(100.0f, 0.0f, 0.0f), 0.01f)
			&& ReplacementServices.LastCameraShot.LookAt.Equals(FVector(200.0f, 0.0f, 0.0f), 0.01f));
	ReplacementWorld.Tick(2.0);
	TestEqual(TEXT("a superseded track still advances and fires authored completion"),
		CounterValue(ReplacementWorld, TEXT("old_completed")), 1.0f);
	TestTrue(TEXT("superseded completion leaves the newer camera pair live"),
		ReplacementWorld.HasTrackCamera());
	TestEqual(TEXT("superseded completion does not pop the shared shot"),
		ReplacementServices.Count(TEXT("PopCameraShot")), 0);
	ReplacementWorld.Tick(3.0);
	TestFalse(TEXT("the selected pair restores normally when its own clock completes"),
		ReplacementWorld.HasTrackCamera());

	return true;
}

// The two channels are mutually exclusive — SC2.
//
// `CBasePlayer::SetCameraViewEntity` (`vampire.dll` `FUN_1017d280`, the `camera_track` role setter)
// **opens with `SetCineCamera(NULL)`**, and nothing anywhere in the cine path touches `+0x19b8`; the
// map teardown `FUN_10071970` tears both channels down together. Retail therefore cannot reach a
// state where an adopted cine camera and a live track override both own the view — which is exactly
// why the release of either is a cut (M1) and not a blend: there is no "one fading out while the
// other ramps in" for a blend to arbitrate.
//
// A separate case rather than an edit to the temporal-cut cluster above, so the exclusion rule reads
// as its own claim and survives SC4 rebuilding the adoption slot underneath it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraChannelExclusionTest,
	"Elysium.Substrate.CameraChannelExclusion", GElysiumTestFlags)
bool FElysiumCameraChannelExclusionTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__camera_channel_exclusion_test__");
	FElysiumEntityDef Track;
	Track.Classname = TEXT("camera_track");
	Track.TargetName = TEXT("pos");
	Track.Origin = FVector(100.0f, 0.0f, 0.0f);
	Track.Keys.Add(TEXT("Pause"), TEXT("30"));      // long enough that nothing completes on its own
	Defs.Defs.Add(MoveTemp(Track));

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	const auto PlayTrack = [&World]()
	{
		World.AcceptInput(TEXT("pos"), FName(TEXT("PlayAsCameraPosition")), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());
	};

	// A track lease, alone.
	PlayTrack();
	TestTrue(TEXT("the track lease owns the view"), World.HasTrackCamera());
	TestFalse(TEXT("and no cine camera is adopted"), World.HasScriptedCamera());

	// Adopting a cine camera drops it — `SetCamera` is "*the* cinematic camera mode".
	World.SetScriptedCamera(TEXT("jack"), FElysiumEntityHandle());
	TestTrue(TEXT("the cine camera is adopted"), World.HasScriptedCamera());
	TestFalse(TEXT("and the adoption cleared the live track lease"), World.HasTrackCamera());
	TestFalse(TEXT("including the role itself, so a superseded track cannot reclaim it"),
		World.TrackCameraOwner(/*bTargetRole*/ false).IsSet());

	// The superseded track's own clock keeps running and still cannot take the view back.
	World.Tick(1.0);
	TestFalse(TEXT("a running track that lost its lease does not repossess the view"),
		World.HasTrackCamera());
	TestTrue(TEXT("and the cine camera is untouched by it"), World.HasScriptedCamera());

	// And the other direction: leasing a track role clears the adopted camera, retail's
	// `SetCineCamera(NULL)` at the top of `FUN_1017d280`.
	PlayTrack();
	TestTrue(TEXT("the track lease is back"), World.HasTrackCamera());
	TestFalse(TEXT("and leasing it dropped the adopted cine camera"), World.HasScriptedCamera());

	return true;
}

// vdata/camerashots — the shot files SetCamera names.
//
// The grammar is documented by Troika in the shipped `camera shots how-to.txt`, so this asserts the
// read against that document rather than against itself: which block wins, how the two target points
// combine, and that Source units become cm exactly once.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraShotsTest, "Elysium.Substrate.CameraShots", GElysiumTestFlags)
bool FElysiumCameraShotsTest::RunTest(const FString&)
{
	// `dialogdefault.txt`, verbatim in shape — the conversation camera every `.dlg` line starts from.
	const FString Text = TEXT(R"(
CameraShotTable
{
	// This is the default camera that is used when dialog begins.
	DialogDefault
	{
		End
		{
			"Position"		"DialogTarget"
			"AttachPos"		"Origin"
			"AttachType"		"Follow"
			"OffsetOrigin"		"[40, 0, 65]"
		}

		Target
		{
			Point1
			{
				"Position"	"DialogTarget"
				"AttachPos"	"Bone: Bip01 Head"
				"AttachType"	"None"
			}
			Point2
			{
				"Position"	"Player"
				"AttachPos"	"EyePosition"
				"AttachType"	"None"
			}
		}

		CameraConstraints
		{
			"MoveSpeed"		"500"
			"MoveAccel"		"250"
			"MaxTurnRate"		"[60, 60, 60]"
			"DistanceTolerance"	"5"
			"FieldOfView"		"40"
			"DialogPOV"		"1"
		}
	}
}
)");

	FElysiumCameraShotDef Def;
	TestTrue(TEXT("the shot file parses"), ElysiumCameraShots::ParseText(Text, Def));
	TestEqual(TEXT("one file, one shot, named after it"), Def.Name, FString(TEXT("dialogdefault")));

	TestTrue(TEXT("End is where the shot lives"), Def.End.bPresent);
	TestFalse(TEXT("this one authors no Start, so it enters from wherever the camera is"),
		Def.Start.bPresent);
	TestTrue(TEXT("it hangs off the conversation partner"),
		Def.End.Position == EElysiumShotPosition::DialogTarget);
	TestTrue(TEXT("and follows them"), Def.End.Attach == EElysiumShotAttach::Follow);
	TestTrue(TEXT("OffsetOrigin converts Source units to cm exactly once"),
		Def.End.OffsetOrigin.Equals(FVector(40.0f, 0.0f, 65.0f) * 2.54f, 0.01f));

	TestTrue(TEXT("the first target point is a bone on the partner"),
		Def.Target1.bPresent && Def.Target1.AttachPos == TEXT("Bone: Bip01 Head"));
	// `AttachType None` selects the frame the offset is added in (world axes), NOT a one-time sample:
	// retail's camera think re-resolves all four anchors every server tick whatever this says
	// (`vampire.dll` `FUN_1006e8e0`, loop `0x1006ea90`). The steadiness of a conversation camera is
	// the client tracker's deadbands, asserted in `Elysium.Substrate.Camera`.
	TestTrue(TEXT("and it takes its offset in world axes -- AttachType None"),
		Def.Target1.Attach == EElysiumShotAttach::None);
	TestTrue(TEXT("the second point is the player's eye"),
		Def.Target2.bPresent && Def.Target2.Position == EElysiumShotPosition::Player);

	TestEqual(TEXT("FieldOfView is degrees, unconverted"), Def.Constraints.FieldOfView, 40.0f);
	TestTrue(TEXT("MoveSpeed is inches/sec, held in cm/s"),
		FMath::IsNearlyEqual(Def.Constraints.MoveSpeed, 500.0f * 2.54f, 0.01f));
	TestTrue(TEXT("MaxTurnRate parses out of its bracket form"),
		Def.Constraints.MaxTurnRate.Equals(FVector(60.0f, 60.0f, 60.0f), 0.01f));
	TestTrue(TEXT("DialogPOV rides through"), Def.Constraints.bDialogPOV);
	TestTrue(TEXT("DistanceTolerance converts too"),
		FMath::IsNearlyEqual(Def.Constraints.DistanceTolerance, 5.0f * 2.54f, 0.01f));
	// Nothing in the file says otherwise, so the defaults the how-to implies hold.
	TestFalse(TEXT("AutoPositionFromTarget defaults off"), Def.Constraints.bAutoPositionFromTarget);
	// Both presentation keys parse with a default of **0** — an absent field asks the shot to hide
	// that surface, and the corpus opts back in explicitly for interaction shots only.
	TestFalse(TEXT("ShowHud defaults off"), Def.Constraints.bShowHud);
	TestFalse(TEXT("DrawViewmodel defaults off"), Def.Constraints.bDrawViewmodel);

	// The keys DialogDefault does not write take retail's parse defaults, not zero: `FUN_100721e0`
	// seeds the whole record before it reads the block.
	TestTrue(TEXT("an unwritten TurnAccel is retail's 30 deg/s^2"),
		FMath::IsNearlyEqual(Def.Constraints.TurnAccel, 30.0f, 0.01f));
	TestTrue(TEXT("an unwritten AngularTolerance is retail's [1,1,1] degrees"),
		Def.Constraints.AngularTolerance.Equals(FVector(1.0f, 1.0f, 1.0f), 0.01f));

	// A shot with no `CameraConstraints` block at all takes the identical set — retail's
	// whole-block-absent path (`0x10072300`) seeds the same values the per-key path does.
	FElysiumCameraShotDef Bare;
	TestTrue(TEXT("a shot with no constraints block parses"), ElysiumCameraShots::ParseText(TEXT(R"(
CameraShotTable { Bare { End { "Position" "DialogTarget" "AttachPos" "Origin" } } }
)"), Bare));
	TestTrue(TEXT("MoveSpeed defaults to retail's 150 u/s"),
		FMath::IsNearlyEqual(Bare.Constraints.MoveSpeed, 150.0f * ElysiumCam::U, 0.01f));
	TestTrue(TEXT("MoveAccel defaults to retail's 50 u/s^2"),
		FMath::IsNearlyEqual(Bare.Constraints.MoveAccel, 50.0f * ElysiumCam::U, 0.01f));
	TestTrue(TEXT("TurnAccel defaults to retail's 30 deg/s^2"),
		FMath::IsNearlyEqual(Bare.Constraints.TurnAccel, 30.0f, 0.01f));
	TestTrue(TEXT("MaxTurnRate defaults to retail's [90,90,90] deg/s"),
		Bare.Constraints.MaxTurnRate.Equals(FVector(90.0f, 90.0f, 90.0f), 0.01f));
	TestTrue(TEXT("DistanceTolerance defaults to retail's 10 u"),
		FMath::IsNearlyEqual(Bare.Constraints.DistanceTolerance, 10.0f * ElysiumCam::U, 0.01f));
	TestTrue(TEXT("AngularTolerance defaults to retail's [1,1,1] degrees"),
		Bare.Constraints.AngularTolerance.Equals(FVector(1.0f, 1.0f, 1.0f), 0.01f));
	TestTrue(TEXT("FieldOfView defaults to retail's 75"),
		FMath::IsNearlyEqual(Bare.Constraints.FieldOfView, 75.0f, 0.01f));
	TestFalse(TEXT("and every flag parses clear"),
		Bare.Constraints.bDialogPOV || Bare.Constraints.bSyncRotateOnMove
			|| Bare.Constraints.bSnapOnShotChange || Bare.Constraints.bShowHud
			|| Bare.Constraints.bDrawViewmodel || Bare.Constraints.bAutoPositionFromTarget);

	// The clamp is retail's, on the parse and not on the use.
	FElysiumCameraShotDef Clamped;
	ElysiumCameraShots::ParseText(TEXT(R"(
CameraShotTable { Wide { End { "Position" "Player" } CameraConstraints { "FieldOfView" "300" } } }
)"), Clamped);
	TestTrue(TEXT("FieldOfView clamps to [20,120]"),
		FMath::IsNearlyEqual(Clamped.Constraints.FieldOfView, 120.0f, 0.01f));

	// A file with no shot block is a miss, not a half-built shot.
	FElysiumCameraShotDef Empty;
	TestFalse(TEXT("an empty table parses to nothing"),
		ElysiumCameraShots::ParseText(TEXT("CameraShotTable\n{\n}\n"), Empty));
	TestFalse(TEXT("and so does empty text"), ElysiumCameraShots::ParseText(FString(), Empty));

	// Retail's `Position` parser (`FUN_10071e00`) is an `_strstr` chain — `Player`, `DialogTarget`,
	// `GrappleVictim`, `GrappleAttacker`, `Named` — and anything else falls through to `World`
	// (`0x4`). A bare entity name is *not* read off the record: `Named` resolves to nothing until a
	// caller supplies the entity through `SetShotAnchorEntity` (`FUN_1006ef50`). The how-to's
	// "the name of an entity in the map" wording describes the director keyvalue, not the shot file.
	FElysiumCameraShotDef Named;
	AddExpectedError(TEXT("is not a keyword"), EAutomationExpectedErrorFlags::Contains, 0);
	TestTrue(TEXT("a shot with an unrecognised Position still parses"), ElysiumCameraShots::ParseText(TEXT(R"(
CameraShotTable { Vantage { End { "Position" "cam_marker_1" "AttachPos" "Origin" "AttachType" "None" } } }
)"), Named));
	TestTrue(TEXT("an unrecognised Position falls through to World, as retail's _strstr chain does"),
		Named.End.Position == EElysiumShotPosition::World && Named.End.NamedEntity.IsEmpty());

	// --- multi-shot files: `special-case.txt` and the `Hacking` block -------------------------
	// Most of `vdata/camerashots/` is one shot per file, but `special-case.txt` carries five
	// siblings and retail addresses them by name (`FUN_10070470("Hacking", ...)`,
	// `docs/vtmb/computer-terminals.md` §7.3 step 6). `ParseText` keeps answering block 0 so no
	// existing `SetCamera` caller changes.
	{
		const FString MultiText = TEXT(R"(
CameraShotTable
{
	DeathCam { End { "Position" "Player" "OffsetOrigin" "[0, 0, 100]" } }
	Hacking
	{
		End    { "Position" "Named"  "AttachPos" "Attachment: screen_axis"  "AttachType" "Follow" }
		Target { Point1 { "Position" "Named"  "AttachPos" "Attachment: screen"  "AttachType" "Follow" } }
		CameraConstraints
		{
			"MoveAccel" "250.0"  "TurnAccel" "180"
			"MoveSpeed" "300"    "MaxTurnRate" "[200, 200, 200]"
			"DistanceTolerance" "1"  "AngularTolerance" "[1,1,1]"
			"FieldOfView" "75"   "DialogPOV" "0"  "DrawViewmodel" "0"
			"SyncRotateOnMove" "1"   "ShowHud" "1"
		}
	}
}
)");
		TArray<FElysiumCameraShotDef> All;
		TestTrue(TEXT("a multi-shot file parses whole"),
			ElysiumCameraShots::ParseAllText(MultiText, All));
		TestEqual(TEXT("in authored order"), All.Num(), 2);

		FElysiumCameraShotDef First;
		TestTrue(TEXT("ParseText still answers block 0"),
			ElysiumCameraShots::ParseText(MultiText, First));
		TestEqual(TEXT("which is the file's first shot"), First.Name, FString(TEXT("DeathCam")));

		ElysiumCameraShots::FlushCache();
		ON_SCOPE_EXIT { ElysiumCameraShots::FlushCache(); };
		ElysiumCameraShots::InstallNamed(TEXT("special-case"), All);

		const FElysiumCameraShotDef* Hacking =
			ElysiumCameraShots::LoadNamed(TEXT("special-case"), TEXT("Hacking"));
		if (TestNotNull(TEXT("LoadNamed finds the Hacking block"), Hacking))
		{
			// The retail terminal shot: on the `screen_axis` attachment, looking at `screen`.
			TestEqual(TEXT("the camera sits on screen_axis"), Hacking->End.AttachPos,
				FString(TEXT("Attachment: screen_axis")));
			TestEqual(TEXT("and looks at screen"), Hacking->Target1.AttachPos,
				FString(TEXT("Attachment: screen")));
			TestEqual(TEXT("FOV 75"), Hacking->Constraints.FieldOfView, 75.0f);
			TestTrue(TEXT("ShowHud 1"), Hacking->Constraints.bShowHud);
			TestFalse(TEXT("DrawViewmodel 0"), Hacking->Constraints.bDrawViewmodel);
			// It is not a snap: the shot eases in on its own constraints (C19).
			TestTrue(TEXT("MoveSpeed 300 u/s"),
				FMath::IsNearlyEqual(Hacking->Constraints.MoveSpeed, 300.0f * ElysiumCam::U, 0.01f));
			TestTrue(TEXT("MoveAccel 250 u/s^2"),
				FMath::IsNearlyEqual(Hacking->Constraints.MoveAccel, 250.0f * ElysiumCam::U, 0.01f));
			TestTrue(TEXT("TurnAccel 180 deg/s^2"),
				FMath::IsNearlyEqual(Hacking->Constraints.TurnAccel, 180.0f, 0.01f));
			TestTrue(TEXT("MaxTurnRate [200,200,200]"),
				Hacking->Constraints.MaxTurnRate.Equals(FVector(200.0f, 200.0f, 200.0f), 0.01f));
			TestTrue(TEXT("DistanceTolerance 1 u"),
				FMath::IsNearlyEqual(Hacking->Constraints.DistanceTolerance, 1.0f * ElysiumCam::U,
					0.01f));
			// A bare `Position: Named` with no entity name is the shot asking for its subject.
			TestTrue(TEXT("its anchors are bare Named"),
				Hacking->End.Position == EElysiumShotPosition::Named
					&& Hacking->End.NamedEntity.IsEmpty());
		}
		TestEqual(TEXT("Load still answers block 0 of the same file"),
			ElysiumCameraShots::Load(TEXT("special-case"))->Name, FString(TEXT("DeathCam")));
		TestNull(TEXT("a missing block is a miss, not block 0"),
			ElysiumCameraShots::LoadNamed(TEXT("special-case"), TEXT("NoSuchShot")));
		TestNull(TEXT("and so is a missing file"),
			ElysiumCameraShots::LoadNamed(TEXT("no-such-file"), TEXT("Hacking")));
	}

	// --- the exposure clamp, the one named Presentation modernization ------------------------
	// Retail has no exposure state at all (`slice-bc-decompiles.md` §7, C20), so nothing parses
	// these: they are the pusher's ask, carried on the shot record.
	{
		FElysiumShotPresentation Presentation;
		float Min = -1.0f;
		float Max = -1.0f;
		TestFalse(TEXT("an ordinary shot leaves the scene's own exposure alone"),
			ElysiumCam::SolveExposureClamp(Presentation, Min, Max));
		TestTrue(TEXT("and writes neither end"), Min == -1.0f && Max == -1.0f);

		Presentation.bClampExposure = true;
		Presentation.ExposureBrightness = 0.75f;
		TestTrue(TEXT("a clamped shot answers"),
			ElysiumCam::SolveExposureClamp(Presentation, Min, Max));
		TestEqual(TEXT("with the same brightness at both ends"), Min, 0.75f);
		TestEqual(TEXT("min and max together"), Max, 0.75f);
	}

	return true;
}

// The presentation seam (`docs/architecture/runtime-architecture.md` §11). FElysiumViewState is a value and its
// rules are total functions over it, so the whole set is asserted with no world, no HUD and no
// viewport — the hand-built state a widget renders from is exactly what is built here.
//
// The load-bearing case is the one the polled HUD got wrong: a conversation already on screen when
// the pause menu opens is republished as *closed*, so the box reconciles to Teardown instead of
// drawing through the menu.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumViewStateTest, "Elysium.Substrate.ViewState", GElysiumTestFlags)
bool FElysiumViewStateTest::RunTest(const FString&)
{
	using EState = EElysiumAppState;
	static const EState All[] = { EState::Boot, EState::FrontEnd, EState::Loading,
		EState::Playing, EState::Paused, EState::GameOver };

	// --- the one gating rule ---------------------------------------------------------------
	// Playing with no screen up is the only combination that shows a player-facing surface. Both
	// writers are asked, because `elysium.menu` raises a screen without moving the app state and
	// TriggerGameOver moves the state without the menu having opened yet.
	for (const EState S : All)
	{
		const bool bExpected = (S == EState::Playing);
		TestEqual(*FString::Printf(TEXT("%s with no menu"), ElysiumAppState::Name(S)),
			ElysiumView::ShowsPlayerSurface(S, /*bMenuOpen*/ false), bExpected);
		TestFalse(*FString::Printf(TEXT("%s with a menu up"), ElysiumAppState::Name(S)),
			ElysiumView::ShowsPlayerSurface(S, /*bMenuOpen*/ true));
	}

	// --- the reticle ------------------------------------------------------------------------
	FElysiumViewState V;
	TestEqual(TEXT("no surface, no reticle"), ElysiumView::ResolveReticle(V), ElysiumView::EReticle::None);

	V.bPlayerSurface = true;
	TestEqual(TEXT("the plain aim cross by default"),
		ElysiumView::ResolveReticle(V), ElysiumView::EReticle::Cross);

	V.Interaction.bVisible = true;
	V.Interaction.Icon = 7;
	TestEqual(TEXT("a usable under the cursor swaps in the context icon"),
		ElysiumView::ResolveReticle(V), ElysiumView::EReticle::UseIcon);

	V.Feed.bPaired = true;
	TestEqual(TEXT("a paired feed suppresses the reticle but not the player surface"),
		ElysiumView::ResolveReticle(V), ElysiumView::EReticle::None);
	V.Feed.bPaired = false;

	// A panel with HideHUD owns the screen (P4.10) — no crosshair under it, icon or not.
	V.bSignHidesHUD = true;
	TestEqual(TEXT("a HideHUD panel takes the reticle with it"),
		ElysiumView::ResolveReticle(V), ElysiumView::EReticle::None);
	V.bSignHidesHUD = false;

	// The rule is total: a suppressed surface reports nothing even with an icon left in the field.
	V.bPlayerSurface = false;
	TestEqual(TEXT("suppression outranks a stale icon"),
		ElysiumView::ResolveReticle(V), ElysiumView::EReticle::None);

	// --- the dialogue reconcile ---------------------------------------------------------------
	// The pointers are identity only and never dereferenced, so two distinct addresses stand in for
	// two conversations.
	// The reconcile identity is the world's open-dialog serial, not the conversation address: the
	// pointer only has to be non-null for `IsOpen()`, and both conversations here deliberately share
	// one, because that is exactly what a same-frame close-and-open can produce.
	const FElysiumDlgConversation* const ConvAddr = reinterpret_cast<const FElysiumDlgConversation*>(0x1);

	FElysiumDialogueView Closed;
	FElysiumDialogueView TurnOne;
	TurnOne.Conversation = ConvAddr;
	TurnOne.DialogSerial = 1;
	TurnOne.Revision = 3;
	FElysiumDialogueView TurnTwo = TurnOne;
	TurnTwo.Revision = 4;
	FElysiumDialogueView Other;
	Other.Conversation = ConvAddr;
	Other.DialogSerial = 2;
	Other.Revision = 3;

	using EAction = ElysiumView::EDialogueAction;
	TestEqual(TEXT("nothing open, nothing up"),
		ElysiumView::ReconcileDialogue(0, 0, Closed), EAction::None);
	TestEqual(TEXT("a conversation opens"),
		ElysiumView::ReconcileDialogue(0, 0, TurnOne), EAction::Rebuild);
	TestEqual(TEXT("the same turn again leaves the retained box alone"),
		ElysiumView::ReconcileDialogue(1, 3, TurnOne), EAction::None);
	TestEqual(TEXT("the turn advances"),
		ElysiumView::ReconcileDialogue(1, 3, TurnTwo), EAction::Rebuild);
	TestEqual(TEXT("a different conversation at the same address and revision still rebuilds"),
		ElysiumView::ReconcileDialogue(1, 3, Other), EAction::Rebuild);
	TestEqual(TEXT("the conversation ends"),
		ElysiumView::ReconcileDialogue(1, 3, Closed), EAction::Teardown);

	// The bug the seam closes: the publisher withholds the whole player-facing surface while a
	// screen is up, so a box that is on screen when the pause menu opens is told to come down.
	FElysiumViewState Paused;
	Paused.App = EState::Paused;
	Paused.bPlayerSurface = ElysiumView::ShowsPlayerSurface(Paused.App, /*bMenuOpen*/ true);
	TestFalse(TEXT("a paused frame publishes no surface"), Paused.bPlayerSurface);
	TestFalse(TEXT("and therefore no conversation"), Paused.Dialogue.IsOpen());
	TestEqual(TEXT("so an open box comes down instead of drawing through the menu"),
		ElysiumView::ReconcileDialogue(1, 3, Paused.Dialogue), EAction::Teardown);

	// --- the meters -------------------------------------------------------------------------
	// Compared by value, because the change delegate fires on a difference and nothing else.
	FElysiumVitals Vit;
	TestFalse(TEXT("no player entity means no meters"), Vit.bValid);
	FElysiumVitals Same = Vit;
	TestTrue(TEXT("an unchanged sheet compares equal"), Same == Vit);
	Same.bValid = true;
	Same.Health = 80;
	Same.MaxHealth = 100;
	TestTrue(TEXT("a seeded sheet does not"), Same != Vit);
	FElysiumVitals Bled = Same;
	Bled.BloodPool = Same.BloodPool - 1;
	TestTrue(TEXT("and neither does one point of blood"), Bled != Same);

	return true;
}

} // namespace ElysiumCameraTests

#endif // WITH_DEV_AUTOMATION_TESTS
