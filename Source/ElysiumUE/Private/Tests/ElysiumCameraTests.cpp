// Content-free Substrate automation: camera solve, reconstruction rig, authored tracks, shots, and published view state.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

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
	TestTrue(TEXT("the composed track shot bypasses the generic target-chase turn limiter"),
		Services.LastCameraShot.MaxTurnRate.IsZero());
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
	TestTrue(TEXT("and it does not follow -- AttachType None is set-and-stay"),
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

	// A file with no shot block is a miss, not a half-built shot.
	FElysiumCameraShotDef Empty;
	TestFalse(TEXT("an empty table parses to nothing"),
		ElysiumCameraShots::ParseText(TEXT("CameraShotTable\n{\n}\n"), Empty));
	TestFalse(TEXT("and so does empty text"), ElysiumCameraShots::ParseText(FString(), Empty));

	// A `Named` anchor carries the entity name, and a bare name is taken as one (the how-to writes
	// `Named` both as the keyword and as "the name of an entity in the map").
	FElysiumCameraShotDef Named;
	TestTrue(TEXT("a named-entity shot parses"), ElysiumCameraShots::ParseText(TEXT(R"(
CameraShotTable { Vantage { End { "Position" "cam_marker_1" "AttachPos" "Origin" "AttachType" "None" } } }
)"), Named));
	TestTrue(TEXT("an unrecognised Position is the entity's own name"),
		Named.End.Position == EElysiumShotPosition::Named && Named.End.NamedEntity == TEXT("cam_marker_1"));

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
	const FElysiumDlgConversation* const ConvA = reinterpret_cast<const FElysiumDlgConversation*>(0x1);
	const FElysiumDlgConversation* const ConvB = reinterpret_cast<const FElysiumDlgConversation*>(0x2);

	FElysiumDialogueView Closed;
	FElysiumDialogueView TurnOne;
	TurnOne.Conversation = ConvA;
	TurnOne.Revision = 3;
	FElysiumDialogueView TurnTwo = TurnOne;
	TurnTwo.Revision = 4;
	FElysiumDialogueView Other;
	Other.Conversation = ConvB;
	Other.Revision = 3;

	using EAction = ElysiumView::EDialogueAction;
	TestEqual(TEXT("nothing open, nothing up"),
		ElysiumView::ReconcileDialogue(nullptr, 0, Closed), EAction::None);
	TestEqual(TEXT("a conversation opens"),
		ElysiumView::ReconcileDialogue(nullptr, 0, TurnOne), EAction::Rebuild);
	TestEqual(TEXT("the same turn again leaves the retained box alone"),
		ElysiumView::ReconcileDialogue(ConvA, 3, TurnOne), EAction::None);
	TestEqual(TEXT("the turn advances"),
		ElysiumView::ReconcileDialogue(ConvA, 3, TurnTwo), EAction::Rebuild);
	TestEqual(TEXT("a different conversation at the same revision still rebuilds"),
		ElysiumView::ReconcileDialogue(ConvA, 3, Other), EAction::Rebuild);
	TestEqual(TEXT("the conversation ends"),
		ElysiumView::ReconcileDialogue(ConvA, 3, Closed), EAction::Teardown);

	// The bug the seam closes: the publisher withholds the whole player-facing surface while a
	// screen is up, so a box that is on screen when the pause menu opens is told to come down.
	FElysiumViewState Paused;
	Paused.App = EState::Paused;
	Paused.bPlayerSurface = ElysiumView::ShowsPlayerSurface(Paused.App, /*bMenuOpen*/ true);
	TestFalse(TEXT("a paused frame publishes no surface"), Paused.bPlayerSurface);
	TestFalse(TEXT("and therefore no conversation"), Paused.Dialogue.IsOpen());
	TestEqual(TEXT("so an open box comes down instead of drawing through the menu"),
		ElysiumView::ReconcileDialogue(ConvA, 3, Paused.Dialogue), EAction::Teardown);

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
