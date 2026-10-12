// Substrate automation (selected Arm physics cases use prepared resident native content): animated-prop timing, clip resynchronization, placement, fallback, and relay lifetime.
#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/ElysiumPlayerWorldFixture.h"
#include "ElysiumModelCatalogues.h"
#include "ElysiumModelPhysicsInputs.h"
#include "ElysiumPhysicsPropComponent.h"
#include "ElysiumCollisionChannels.h"
#include "Substrate/ElysiumModelPhysics.h"
#include "Visual/ElysiumPreparedPropModels.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Physics/PhysicsInterfaceCore.h"
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
#include "ElysiumGaitSpeeds.h"               // the animation's per-direction speed
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumSessionSubsystem.h"
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
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// One context flag (runs anywhere) + the product filter (this project's own suite bucket).
// EAutomationTestFlags is a strong enum in 5.8, so the constant carries that type (ENUM_CLASS_FLAGS
// makes the `|` yield an EAutomationTestFlags), not int32.
namespace ElysiumPropTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;


// CDynamicProp's spawn/activate/think lifecycle (RE35). Four facts, in the order they happen:
//   * `CBaseProp::Spawn` (FUN_1018df70) stands the prop on a HELD pose — the rest sequence at
//     frame 0 with the play rate at zero — not on a playing clip. `demo_sequence` is not an
//     engine keyfield and contributes nothing; the pose comes from the activity/index rule.
//   * `CDynamicProp::Activate` (FUN_101906c0) resolves `LoopSequence` and arms the start behind
//     a RandomFloat(0.1, 0.99) stagger, so nothing starts on the map's first frozen-time tick.
//   * `SetAnimation` plays on the clip's own STUDIO_LOOPING bit, not a forced one shot.
//   * A finished one-shot HOLDS ITS FINAL FRAME. The think returns without rewriting
//     m_flNextThink once `RandomAnimation` is 0 — true on all 749 shipped entities — so it
//     disarms permanently and the revert-to-LoopSequence branch is unreachable in shipped data.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropAnimateThinkTest,
	"Elysium.Substrate.PropAnimateThink", GElysiumTestFlags)
bool FElysiumPropAnimateThinkTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.AnimatedPropModels.Add(TEXT("models/cinematic/cin_stake.mdl"), TEXT("cin_stake"));
	Services.AnimatedPropModels.Add(TEXT("models/cinematic/cin_cigar.mdl"), TEXT("cin_cigar"));
	// Rest clips that are NOT the authored keyvalues, so a pose sourced from `demo_sequence`
	// would be distinguishable from one sourced by the activity/index rule.
	Services.AnimatedPropRestClips.Add(TEXT("cin_stake"), TEXT("idle01"));
	Services.AnimatedPropRestClips.Add(TEXT("cin_cigar"), TEXT("rest_pose"));
	Services.AnimatedPropClipLoops.Add(TEXT("cin_stake|idle01"), true);
	Services.ClipSeconds = 4.0f;

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__prop_anim__");
	{
		FElysiumEntityDef Stake;
		Stake.Classname = TEXT("prop_dynamic");
		Stake.TargetName = TEXT("stake");
		Stake.ModelMesh = TEXT("cin_stake");
		Stake.Keys.Add(TEXT("model"), TEXT("models/cinematic/cin_stake.mdl"));
		Stake.Keys.Add(TEXT("LoopSequence"), TEXT("idle01"));
		Defs.Defs.Add(MoveTemp(Stake));

		FElysiumEntityDef Cigar;
		Cigar.Classname = TEXT("prop_dynamic");
		Cigar.TargetName = TEXT("cigar");
		Cigar.ModelMesh = TEXT("cin_cigar");
		Cigar.Keys.Add(TEXT("model"), TEXT("models/cinematic/cin_cigar.mdl"));
		Cigar.Keys.Add(TEXT("demo_sequence"), TEXT("idle"));
		Defs.Defs.Add(MoveTemp(Cigar));
	}

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* Stake = World.FindByName(TEXT("stake"));
	if (!TestNotNull(TEXT("stake resolved"), Stake))
	{
		return false;
	}

	// Spawn: a held rest pose on both props. `loop=0` is load-bearing — the anim proxy's Request
	// early-outs on a repeated looping clip without restoring the play rate, so a hold created as
	// a loop could never be released.
	TestEqual(TEXT("the prop stands its rest sequence, held"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_stake idle01 loop=0")), 1);
	TestTrue(TEXT("the rest pose is seeked to frame 0"),
		Services.Saw(TEXT("SeekCinematicClip 0.000")));
	TestEqual(TEXT("demo_sequence is not an engine keyfield; the rest pose comes from the model"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_cigar rest_pose loop=0")), 1);
	TestEqual(TEXT("nothing stands on the demo_sequence value"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_cigar idle")), 0);

	// Activate armed the loop start behind the stagger, so the frozen-time pass must not fire it.
	World.Tick(0.0);
	TestEqual(TEXT("the authored loop does not start on the map's first tick"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_stake idle01 loop=1")), 0);

	// Past the longest stagger (0.99) the loop starts, once, honouring its STUDIO_LOOPING bit.
	World.Tick(1.0);
	TestEqual(TEXT("the authored loop starts after the stagger"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_stake idle01 loop=1")), 1);

	// A scripted one-shot replaces the loop and re-arms the think.
	World.EnqueueInput(TEXT("!self"), FName(TEXT("SetAnimation")),
		FElysiumVariant::String(TEXT("scene")), /*Delay*/ 0.0,
		FElysiumEntityHandle::Invalid(), Stake->Handle);
	World.Tick(1.0);
	TestEqual(TEXT("SetAnimation plays the one-shot on the clip's own loop bit"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_stake scene loop=0")), 1);

	// Mid-clip nothing changes (ClipSeconds is 4).
	World.Tick(3.0);
	TestEqual(TEXT("the one-shot is left alone while it runs"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_stake scene loop=0")), 1);

	// Past the clip's end the prop holds its final frame: OnAnimationDone fires and the think
	// disarms. Retail does not return to `LoopSequence` and does not fall back to a rest pose.
	World.Tick(5.5);
	TestEqual(TEXT("a finished one-shot does not revert to LoopSequence"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_stake idle01 loop=1")), 1);
	TestEqual(TEXT("a finished one-shot does not restart itself"),
		Services.Count(TEXT("PlayAnimatedPropClip cin_stake scene loop=0")), 1);
	TestEqual(TEXT("the think disarms once the clip has finished"),
		Stake->NextThink, ELYSIUM_NEVER_THINK);
	return true;
}


// A prop's clip phase is a function of the substrate clock, not of accumulated animation delta.
//
// Retail gets this without trying: `CBaseAnimating::StudioFrameAdvance` (FUN_1008f120) recomputes
// its interval as `curtime - m_flAnimTime` and leaves the stamp at curtime, so the advance
// telescopes and the total is exactly elapsed game time however many calls there were. That is why
// `CDynamicPropAnimThink` can run at 10 Hz beside a choreo actor seeked every frame and neither
// drifts. Unreal's sequence player accumulates instead, so the 10 Hz think measures the clip
// against elapsed game time and corrects it when the two have parted company.
//
// The correction is deliberately NOT `SeekCinematicClip`: that pins the body at play rate 0 and
// collapses any crossfade, which is right for a scene driving every frame and wrong for a clip that
// must keep running smoothly between corrections.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropClipResyncTest,
	"Elysium.Substrate.PropClipResync", GElysiumTestFlags)
bool FElysiumPropClipResyncTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.AnimatedPropModels.Add(TEXT("models/cinematic/cin_stake.mdl"), TEXT("cin_stake"));
	Services.AnimatedPropModels.Add(TEXT("models/props/lamp.mdl"), TEXT("lamp"));
	Services.AnimatedPropRestClips.Add(TEXT("cin_stake"), TEXT("idle01"));
	Services.AnimatedPropRestClips.Add(TEXT("lamp"), TEXT("idle"));
	Services.AnimatedPropClipLoops.Add(TEXT("cin_stake|idle01"), true);
	Services.ClipSeconds = 4.0f;

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__prop_resync__");
	{
		// A looping prop, so the think stays armed for the whole test.
		FElysiumEntityDef Stake;
		Stake.Classname = TEXT("prop_dynamic");
		Stake.TargetName = TEXT("stake");
		Stake.ModelMesh = TEXT("cin_stake");
		Stake.Keys.Add(TEXT("model"), TEXT("models/cinematic/cin_stake.mdl"));
		Stake.Keys.Add(TEXT("LoopSequence"), TEXT("idle01"));
		Defs.Defs.Add(MoveTemp(Stake));

		// No LoopSequence: this one never leaves its held rest pose, and never arms a think.
		FElysiumEntityDef Lamp;
		Lamp.Classname = TEXT("prop_dynamic");
		Lamp.TargetName = TEXT("lamp");
		Lamp.ModelMesh = TEXT("lamp");
		Lamp.Keys.Add(TEXT("model"), TEXT("models/props/lamp.mdl"));
		Defs.Defs.Add(MoveTemp(Lamp));
	}

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	USkeletalMeshComponent* Body = Services.AnimatedPropBodies.FindRef(TEXT("cin_stake"));
	USkeletalMeshComponent* LampBody = Services.AnimatedPropBodies.FindRef(TEXT("lamp"));
	if (!TestNotNull(TEXT("the animated body was built"), Body))
	{
		return false;
	}

	// Past the longest stagger (0.99) the loop is running, and AnimStartTime is stamped at the
	// think that started it. From here on the expected phase is `Now - that stamp`.
	World.Tick(1.0);
	const int32 AfterStart = Services.Count(TEXT("ResyncCinematicClip"));

	// A body that reports no position at all — no anim host — is an ordinary no-op, not an error
	// and not a correction.
	World.Tick(1.2);
	TestEqual(TEXT("a body that cannot report its position is never resynced"),
		Services.Count(TEXT("ResyncCinematicClip")), AfterStart);

	// In phase: the clip sits exactly where elapsed game time says it should.
	Services.ClipPositions.Add(Body, 0.4f);
	World.Tick(1.4);
	TestEqual(TEXT("a clip in phase is left alone"),
		Services.Count(TEXT("ResyncCinematicClip")), AfterStart);

	// Inside the tolerance. This is the assertion that stops the tolerance being quietly tightened:
	// the position read here is up to one frame stale by construction, so correcting a sub-frame
	// difference would snap on every think and read as a 10 Hz stutter.
	Services.ClipPositions.Add(Body, 0.6f - 0.02f);
	World.Tick(1.6);
	TestEqual(TEXT("drift under the tolerance is not worth a correction"),
		Services.Count(TEXT("ResyncCinematicClip")), AfterStart);

	// Beyond it: exactly one correction, to elapsed game time — not to the position plus a delta.
	Services.ClipPositions.Add(Body, 0.3f);
	World.Tick(1.8);
	TestEqual(TEXT("real drift is corrected once"),
		Services.Count(TEXT("ResyncCinematicClip")), AfterStart + 1);
	TestTrue(TEXT("and corrected to elapsed game time"),
		Services.Saw(TEXT("ResyncCinematicClip 0.800")));
	TestTrue(TEXT("the body reads the corrected phase back"),
		FMath::IsNearlyEqual(Services.ClipPositions.FindRef(Body), 0.8f, 1e-3f));

	// A looping clip wraps at the source, in double: at 9.0 s of elapsed time on a 4 s clip the
	// phase is 1.0, never 9.0. Narrowing an unbounded elapsed time to float instead would quantise
	// visibly over a long session.
	Services.ClipPositions.Add(Body, 0.0f);
	World.Tick(10.0);
	TestTrue(TEXT("a looping phase wraps into the clip's own length"),
		Services.Saw(TEXT("ResyncCinematicClip 1.000")));

	// Across the loop seam the distance is circular. The clip started at 1.0, so a tick at 13.01
	// puts the phase at 12.01 mod 4 = 0.01 — which is 0.02 from a position of 3.99 the short way
	// round, and 3.98 the long way. A plain difference would correct here on every single think.
	const int32 BeforeSeam = Services.Count(TEXT("ResyncCinematicClip"));
	Services.ClipPositions.Add(Body, 3.99f);
	World.Tick(13.01);
	TestEqual(TEXT("drift across the loop seam is measured the short way round"),
		Services.Count(TEXT("ResyncCinematicClip")), BeforeSeam);

	// A prop with no LoopSequence and no random animator stands its held rest pose and arms no
	// think at all, so the pose it was spawned on is never touched again. That — not the
	// bRestPoseHeld guard inside the think — is what actually keeps a resting prop at rest; the
	// guard is the defence for a prop that IS thinking and has been put back on a held pose.
	FElysiumEntity* Lamp = World.FindByName(TEXT("lamp"));
	TestNotNull(TEXT("lamp resolved"), Lamp);
	if (LampBody)
	{
		Services.ClipPositions.Add(LampBody, 3.0f);
	}
	// Put the stake back in phase first (13.0 mod 4 = 1.0 at the tick below), so the only prop that
	// could produce a correction on this tick is the lamp.
	Services.ClipPositions.Add(Body, 1.0f);
	const int32 BeforeLamp = Services.Count(TEXT("ResyncCinematicClip"));
	World.Tick(14.0);
	TestEqual(TEXT("a prop standing at rest never armed a think"),
		Lamp->NextThink, ELYSIUM_NEVER_THINK);
	TestEqual(TEXT("so its held pose is never resynced"),
		Services.Count(TEXT("ResyncCinematicClip")), BeforeLamp);

	return true;
}


// A skeletal prop is placed exactly like its static mesh. Both are built from the same model in
// the same frame, so `model_quat` — the placement of the exporter's Unreal-native OBJ — is the
// whole answer for either, and the two representations of one prop must not disagree. Taking the
// full quaternion rather than rederiving a yaw is what keeps a placement's pitch and roll: 15 of
// the corpus's animated-prop placements are leaning palms.

#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimatedPropPlacementTest,
	"Elysium.Arm.AnimatedPropPlacement", GElysiumTestFlags)
bool FElysiumAnimatedPropPlacementTest::RunTest(const FString&)
{
	auto BuildOne = [](FElysiumRecordingServices& Services, const FVector& SourceAngles,
		const FQuat& ModelQuat, bool bAnimated)
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__prop_place__");
		FElysiumEntityDef Prop;
		Prop.Classname = TEXT("prop_dynamic");
		Prop.TargetName = TEXT("prop");
		Prop.ModelMesh = TEXT("prop_mesh");
		Prop.ModelQuat = ModelQuat;
		Prop.Keys.Add(TEXT("model"), TEXT("models/test/prop.mdl"));
		Prop.Keys.Add(TEXT("angles"), FString::Printf(TEXT("%f %f %f"),
			SourceAngles.X, SourceAngles.Y, SourceAngles.Z));
		Defs.Defs.Add(MoveTemp(Prop));
		if (bAnimated)
		{
			Services.AnimatedPropModels.Add(TEXT("models/test/prop.mdl"), TEXT("prop_anim"));
		}
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);
	};

	// `sp_theatre` ships eleven of its sixteen animated props at `angles "0 270 0"`, whose exported
	// model_quat is a +90 degree Unreal yaw. Both representations want that verbatim.
	const FQuat Yaw90(FRotator(0.f, 90.f, 0.f));
	{
		FElysiumRecordingServices Animated;
		BuildOne(Animated, FVector(0.f, 270.f, 0.f), Yaw90, /*bAnimated=*/true);
		TestTrue(TEXT("the skeletal body is built"),
			Animated.Saw(TEXT("BuildAnimatedPropVisual prop_anim")));
		const FRotator Got = Animated.LastAnimatedPropRotation.Rotator();
		TestTrue(FString::Printf(
			TEXT("a 270 degree Source yaw stands the skeletal body at model_quat (got %s)"),
			*Got.ToString()), Got.Equals(FRotator(0.f, 90.f, 0.f), 0.01f));
	}
	{
		FElysiumRecordingServices Static;
		BuildOne(Static, FVector(0.f, 270.f, 0.f), Yaw90, /*bAnimated=*/false);
		const FRotator Got = Static.LastPropRotation.Rotator();
		TestTrue(FString::Printf(TEXT("the static mesh keeps model_quat verbatim (got %s)"),
			*Got.ToString()), Got.Equals(FRotator(0.f, 90.f, 0.f), 0.01f));
	}

	// `sm_oceanhouse_1`'s leaning palms — 15 of the corpus's animated-prop placements carry pitch
	// or roll. A yaw-only rederivation would stand these bolt upright, which is why the placement
	// is taken as the authored quaternion rather than rebuilt from the `angles` yaw.
	{
		FElysiumRecordingServices Leaning;
		const FRotator Authored(24.0994f, 284.031f, -4.27304f);
		BuildOne(Leaning, FVector(24.0994f, 284.031f, -4.27304f), FQuat(Authored), /*bAnimated=*/true);
		TestTrue(TEXT("the leaning skeletal body is built"),
			Leaning.Saw(TEXT("BuildAnimatedPropVisual prop_anim")));
		const FRotator Got = Leaning.LastAnimatedPropRotation.Rotator();
		TestTrue(FString::Printf(TEXT("the authored lean survives (got %s)"),
			*Got.ToString()), FMath::Abs(Got.Pitch) > 1.f && FMath::Abs(Got.Roll) > 1.f);
		TestTrue(TEXT("the skeletal rotation is model_quat verbatim"),
			Leaning.LastAnimatedPropRotation.Equals(FQuat(Authored), 0.001f));

		// The two representations of one prop must not disagree: a divergence here is a prop that
		// turns as it starts animating.
		FElysiumRecordingServices LeaningStatic;
		BuildOne(LeaningStatic, FVector(24.0994f, 284.031f, -4.27304f), FQuat(Authored),
			/*bAnimated=*/false);
		TestTrue(TEXT("and the static mesh of the same placement takes the same rotation"),
			LeaningStatic.LastPropRotation.Equals(Leaning.LastAnimatedPropRotation, 0.001f));
	}
	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS


// An indexed model that bakes no playable clip is not an animated representation — it is a
// bind-pose skeleton standing where the baked static mesh should be. `lampfloor`, `glassa` and
// `junkyardcraneb` are the shipped cases; the exporter now keeps them out of the index, and this
// is the runtime's own guard for an index that still carries one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropZeroClipFallbackTest,
	"Elysium.Substrate.PropZeroClipFallback", GElysiumTestFlags)
bool FElysiumPropZeroClipFallbackTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.AnimatedPropModels.Add(TEXT("models/scenery/lampfloor.mdl"), TEXT("lampfloor"));
	Services.AnimatedPropRestClips.Add(TEXT("lampfloor"), FString());   // bakes no clip

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__prop_fallback__");
	FElysiumEntityDef Lamp;
	Lamp.Classname = TEXT("prop_dynamic");
	Lamp.TargetName = TEXT("plus_bag");
	Lamp.ModelMesh = TEXT("lampfloor_static");
	Lamp.Keys.Add(TEXT("model"), TEXT("models/scenery/lampfloor.mdl"));
	Lamp.Keys.Add(TEXT("LoopSequence"), TEXT("idle"));
	Defs.Defs.Add(MoveTemp(Lamp));

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);
	World.Tick(1.0);

	TestFalse(TEXT("a clipless model does not stand a skeletal body"),
		Services.Saw(TEXT("BuildAnimatedPropVisual lampfloor")));
	TestTrue(TEXT("it keeps its baked static mesh instead"),
		Services.Saw(TEXT("BuildPropVisual lampfloor")));
	TestEqual(TEXT("and plays nothing, authored LoopSequence notwithstanding"),
		Services.Count(TEXT("PlayAnimatedPropClip")), 0);
	return true;
}


// 101907e0 ->100a5bb0 ->1002e580 requires actual selected source .phy, never a generic
// collider on a recording component. Reuse the accepted prepared CopCar/native world producer.
#if ELYSIUM_WITH_ARM_TESTS
struct FPropSolidNativeFixture
{
	FPlayerWorldFixture Native;
	AElysiumMapActor* Map = nullptr;
	TSharedPtr<FElysiumPreparedPropModels> Prepared;
	TUniquePtr<FElysiumEntityWorld> Entities;
	const UElysiumModelPhysicsInputs* Inputs = nullptr;

	~FPropSolidNativeFixture()
	{
		Entities.Reset();
		if (Map) ElysiumPreparedProps::Release(Map);
	}

	bool Prepare(FAutomationTestBase& Test)
	{
		if (!Native.CreateWorld(Test)) return false;
		Map = Native.SpawnMapActorDeferred();
		auto* Placed = LoadObject<UElysiumPlacedModelCatalogue>(nullptr,
			TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels"));
		auto* Skins = LoadObject<UElysiumPropSkinCatalogue>(nullptr,
			TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins.DA_PropSkins"));
		const TArray<FString> Ids = { TEXT("vtmb:model:scenery/vehicles/omnishpere/copcar") };
		TSet<FSoftObjectPath> Paths; FString Error;
		if (!Test.TestTrue(TEXT("existing selected CopCar preparation paths"),
			FElysiumPreparedPropModels::GatherPaths(Placed, Skins, Ids, Paths, Error))) return false;
		TArray<UObject*> Assets;
		for (const auto& Path : Paths) Assets.Add(Path.TryLoad());
		Prepared = FElysiumPreparedPropModels::Create(Map, 1, Placed, Skins, Ids, Assets, Error);
		if (!Test.TestTrue(TEXT("actual resident CopCar preparation"), Prepared.IsValid())) return false;
		UStaticMesh* SourceMesh = Prepared->StaticMesh(Ids[0], Error);
		Inputs = UElysiumModelPhysicsInputs::Find(SourceMesh);
		if (!Test.TestTrue(TEXT("actual persisted source-body and selected collision cook"),
			Inputs && Inputs->bSourceBody && Inputs->SelectedBody
			&& Inputs->SelectedBody->AggGeom.GetElementCount() > 0)) return false;
		FElysiumWorldServices Services; Services.Embodiment = Map;
		Entities = MakeUnique<FElysiumEntityWorld>(Map, nullptr, Services);
		Entities->Load(FElysiumEntityDefs());
		return true;
	}

	FElysiumEntity* Spawn(const TCHAR* Name, const TCHAR* Solid, const FVector& At)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("prop_dynamic"); Def.TargetName = Name; Def.Origin = At;
		Def.Keys.Add(TEXT("model"), TEXT("models/scenery/vehicles/OmniShpere/CopCar.mdl"));
		Def.Keys.Add(TEXT("solid"), Solid); Def.Keys.Add(TEXT("angles"), TEXT("17 31 23"));
		return Entities->Resolve(Entities->SpawnRuntimeEntity(MoveTemp(Def)));
	}

	bool Publish(FAutomationTestBase& Test)
	{
		return Test.TestTrue(TEXT("actual native frame publishes selected shapes"), Native.TestWorld.TickTestWorld(1.f / 60.f))
			&& Test.TestTrue(TEXT("following native publication frame"), Native.TestWorld.TickTestWorld(1.f / 60.f));
	}

	bool Hits(UPrimitiveComponent* Expected, const FVector& Center) const
	{
		FHitResult Hit;
		Native.World->LineTraceSingleByChannel(Hit, Center - FVector(1000,0,0), Center + FVector(1000,0,0),
			ElysiumCollision::SightChannel, FCollisionQueryParams(FName(TEXT("PropSolidSelected")), false));
		return Hit.GetComponent() == Expected;
	}

	static int32 QueryShapes(UPrimitiveComponent* Component)
	{
		FBodyInstance* Body = ElysiumModelPhysics::Identity(Component);
		if (!Body) return 0;
		int32 Count = 0;
		FPhysicsCommand::ExecuteRead(Body->GetPhysicsActor(), [&](const FPhysicsActorHandle&)
		{
			TArray<FPhysicsShapeHandle> Shapes; Body->GetAllShapes_AssumesLocked(Shapes);
			for (const auto& Shape : Shapes) if (FPhysicsInterface::IsQueryShape(Shape)) ++Count;
		});
		return Count;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropSolidCollisionTest,
	"Elysium.Arm.PropSolidCollision", GElysiumTestFlags)
bool FElysiumPropSolidCollisionTest::RunTest(const FString&)
{
	FPropSolidNativeFixture Fixture;
	if (!Fixture.Prepare(*this)) return false;
	FElysiumEntity* Solid = Fixture.Spawn(TEXT("solid_copcar"), TEXT("6"), FVector(5000,4000,3000));
	FElysiumEntity* Unsolid = Fixture.Spawn(TEXT("plain_copcar"), TEXT("0"), FVector(8000,4000,3000));
	if (!TestNotNull(TEXT("solid prop resolved"), Solid) || !TestNotNull(TEXT("plain prop resolved"), Unsolid)
		|| !TestNotNull(TEXT("solid6 publishes actual selected native object"), Solid->VPhysicsGetObject())) return false;
	UPrimitiveComponent* SolidBody = ElysiumModelPhysics::ComponentFor(Solid->VPhysicsGetObject());
	if (!TestNotNull(TEXT("selected native component"), SolidBody) || !Fixture.Publish(*this)) return false;
	TestEqual(TEXT("solid6 admits one actual selected native query shape"), Fixture.QueryShapes(SolidBody), 1);
	const FVector Center = Fixture.Inputs->SelectedBody->AggGeom.CalcAABB(
		ElysiumModelPhysics::Identity(SolidBody)->GetUnrealWorldTransform()).GetCenter();
	TestTrue(TEXT("solid6 switches on actual selected .phy scene collision"), Fixture.Hits(SolidBody, Center));
	TestNull(TEXT("solid0 publishes no physics object"), Unsolid->VPhysicsGetObject());
	UPrimitiveComponent* UnsolidBody = Unsolid->GetAttachBody();
	if (!TestNotNull(TEXT("solid0 still has prepared art"), UnsolidBody)) return false;
	TestEqual(TEXT("solid0 art stays noncolliding"), UnsolidBody->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("solid0 art cannot substitute selected collision"), Fixture.Hits(UnsolidBody, Center + FVector(3000,0,0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropSolidCatalogueTest,
	"Elysium.Arm.PropSolidCatalogue", GElysiumTestFlags)
bool FElysiumPropSolidCatalogueTest::RunTest(const FString&)
{
	FPropSolidNativeFixture Fixture;
	if (!Fixture.Prepare(*this)) return false;
	FElysiumEntity* Solid = Fixture.Spawn(TEXT("solid_copcar"), TEXT("3"), FVector(5000,4000,3000));
	if (!TestNotNull(TEXT("catalogue solid3 resolved"), Solid)
		|| !TestNotNull(TEXT("catalogue solid3 publishes actual native object"), Solid->VPhysicsGetObject())) return false;
	const auto* Recipe = ElysiumModelPhysics::RecipeFor(Solid->VPhysicsGetObject());
	UPrimitiveComponent* Component = ElysiumModelPhysics::ComponentFor(Solid->VPhysicsGetObject());
	if (!TestNotNull(TEXT("solid3 request reached selected native recipe"), Recipe)
		|| !TestNotNull(TEXT("selected component"), Component)) return false;
	TestTrue(TEXT("solid3 selects static Model collision, as solid6 does"),
		Recipe->Kind == EElysiumPhysicsRecipeKind::Model && Recipe->bStatic);
	TestTrue(TEXT("catalogue static request uses source selected body, not generic render setup"),
		ElysiumModelPhysics::Identity(Component)->GetBodySetup() == Fixture.Inputs->SelectedBody.Get());
	if (!Fixture.Publish(*this)) return false;
	TestEqual(TEXT("catalogue static request admits one selected native query shape"), Fixture.QueryShapes(Component), 1);
	return true;
}


// 8.4a — `disableshadows`, a CBaseEntity keyfield `CBaseEntity::KeyValue` folds into `m_fEffects`
// (docs/vtmb/entity_visuals.md); here it is read once and applied to the cast-shadow flag.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropDisableShadowsTest,
	"Elysium.Arm.PropDisableShadows", GElysiumTestFlags)
bool FElysiumPropDisableShadowsTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__prop_shadows__");
	FElysiumEntityDef Dark;
	Dark.Classname = TEXT("prop_dynamic");
	Dark.TargetName = TEXT("dark_lamp");
	Dark.ModelMesh = TEXT("lamp_static");
	Dark.Keys.Add(TEXT("model"), TEXT("models/scenery/lamp.mdl"));
	Dark.Keys.Add(TEXT("disableshadows"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(Dark));
	FElysiumEntityDef Lit;
	Lit.Classname = TEXT("prop_dynamic");
	Lit.TargetName = TEXT("lit_lamp");
	Lit.ModelMesh = TEXT("lamp2_static");
	Lit.Keys.Add(TEXT("model"), TEXT("models/scenery/lamp2.mdl"));
	Defs.Defs.Add(MoveTemp(Lit));

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	UStaticMeshComponent* DarkBody = Services.PropBodies.FindRef(TEXT("lamp"));
	UStaticMeshComponent* LitBody = Services.PropBodies.FindRef(TEXT("lamp2"));
	TestNotNull(TEXT("the dark lamp stands a body"), DarkBody);
	TestNotNull(TEXT("the lit lamp stands a body"), LitBody);
	if (DarkBody)
	{
		TestFalse(TEXT("disableshadows 1 suppresses the cast"), DarkBody->CastShadow);
	}
	if (LitBody)
	{
		TestTrue(TEXT("an absent disableshadows key casts normally"), LitBody->CastShadow);
	}
	return true;
}


// Break gates existing selected native shapes and real prepared art together; no recipe fallback.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropSolidGatingTest,
	"Elysium.Arm.PropSolidGating", GElysiumTestFlags)
bool FElysiumPropSolidGatingTest::RunTest(const FString&)
{
	FPropSolidNativeFixture Fixture;
	if (!Fixture.Prepare(*this)) return false;
	FElysiumEntity* Solid = Fixture.Spawn(TEXT("solid_copcar"), TEXT("6"), FVector(5000,4000,3000));
	if (!TestNotNull(TEXT("solid prop resolved"), Solid)
		|| !TestNotNull(TEXT("solid before Break has actual native object"), Solid->VPhysicsGetObject())) return false;
	void* Actor = Solid->VPhysicsGetObject();
	UPrimitiveComponent* Body = ElysiumModelPhysics::ComponentFor(Actor);
	UPrimitiveComponent* Art = Solid->GetAttachBody();
	if (!TestNotNull(TEXT("actual native component"), Body) || !TestNotNull(TEXT("prepared prop art"), Art)
		|| !Fixture.Publish(*this)) return false;
	const FVector Center = Fixture.Inputs->SelectedBody->AggGeom.CalcAABB(
		ElysiumModelPhysics::Identity(Body)->GetUnrealWorldTransform()).GetCenter();
	TestEqual(TEXT("solid before Break has one native query shape"), Fixture.QueryShapes(Body), 1);
	TestTrue(TEXT("solid before Break actually blocks scene query"), Fixture.Hits(Body, Center));
	TestTrue(TEXT("prepared art visible before Break"), Art->IsVisible());
	Fixture.Entities->AcceptInput(TEXT("solid_copcar"), FName(TEXT("Break")), FElysiumVariant::Void(),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("Break gates actual native query shapes off"), Fixture.QueryShapes(Body), 0);
	TestFalse(TEXT("Break gates collision off in actual native scene"), Fixture.Hits(Body, Center));
	TestFalse(TEXT("Break gates prepared art visibility off"), Art->IsVisible());
	TestTrue(TEXT("Break gates the existing actor without replacement"), Solid->VPhysicsGetObject() == Actor);
	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS

} // namespace ElysiumPropTests

#endif // WITH_DEV_AUTOMATION_TESTS
