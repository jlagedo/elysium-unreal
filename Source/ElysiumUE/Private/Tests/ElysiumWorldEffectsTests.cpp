// Content-free Substrate automation: animation binding identity, audio contracts, light rig, weather, stubs, gaze, and blend grids.
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
#include "ElysiumGymSpec.h"
#include "Visual/ElysiumPoseDeviation.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumLineService.h"
#include "ElysiumLookCurve.h"                // the mouse path's pure rules (CCC3)
#include "Debug/ElysiumMoveCourses.h"        // the event-timed press's pure half (CCC3)
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
#include "Tests/ElysiumDialogueTestHelpers.h"
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
namespace ElysiumWorldEffectsTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using ElysiumDialogueTestHelpers::ElysiumDlgBytes;
using ElysiumDialogueTestHelpers::ElysiumDlgRow;

// =====================================================================================
// Skeleton-bound animation resolution — cache identity and cinematic root selection.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimationBindingIdentityTest,
	"Elysium.Substrate.AnimationBindingIdentity", GElysiumTestFlags)
bool FElysiumAnimationBindingIdentityTest::RunTest(const FString&)
{
	const FString Clip(TEXT("entire_scene"));
	const FString Bank(TEXT("cinematic_santa_monica_courtroom_courtroom_bip1__bip02"));
	const FString Sheriff = ElysiumEntityAnimation::CinematicClipCacheKey(
		TEXT("sheriff"), Bank, Clip);
	const FString Sire = ElysiumEntityAnimation::CinematicClipCacheKey(
		TEXT("doppleganger_male"), Bank, Clip);
	TestFalse(TEXT("one cinematic bank retargeted to two models has two cache identities"),
		Sheriff == Sire);
	TestFalse(TEXT("ordinary and cinematic cache namespaces cannot alias"),
		Sheriff == ElysiumEntityAnimation::NpcClipCacheKey(TEXT("sheriff"), Clip));
	const FString NormalVisual = ElysiumEntityAnimation::NpcVisualCacheKey(
		TEXT("malkavian_male_armor_2"), false);
	const FString PlayerVisual = ElysiumEntityAnimation::NpcVisualCacheKey(
		TEXT("malkavian_male_armor_2"), true);
	TestFalse(TEXT("normal NPC and masked player meshes have distinct cache identities"),
		NormalVisual == PlayerVisual);
	TestFalse(TEXT("their skeleton-bound clips cannot alias either"),
		ElysiumEntityAnimation::NpcClipCacheKey(NormalVisual, Clip)
			== ElysiumEntityAnimation::NpcClipCacheKey(PlayerVisual, Clip));

	FElysiumBipedAnimProxy Proxy;
	UAnimSequence* Sequence = NewObject<UAnimSequence>();
	Proxy.PlayDirect(Sequence, /*bLoop=*/true);
	TestTrue(TEXT("the initial stance loops"), Proxy.IsPlayingLoop());
	Proxy.PlayDirect(Sequence, /*bLoop=*/false);
	TestFalse(TEXT("the same clip can change from a loop to a one-shot"), Proxy.IsPlayingLoop());
	Proxy.PlayDirect(Sequence, /*bLoop=*/true);
	TestTrue(TEXT("reset-to-idle restores looping on the same clip"), Proxy.IsPlayingLoop());
	TestEqual(TEXT("the clip player names what it is standing"), Proxy.GetPlaying(), Sequence);
	Proxy.StopDirect();
	TestTrue(TEXT("a stopped clip player says it cannot answer a position"),
		Proxy.GetClipPosition() < 0.f);

	FElysiumCinematicSet Multi;
	Multi.Roots.Add(TEXT("bip01"), TEXT("bank_one"));
	Multi.Roots.Add(TEXT("bip02"), TEXT("bank_two"));
	TestEqual(TEXT("cinematic roots match case-insensitively"),
		Multi.BankForRoot(TEXT("Bip02")), FString(TEXT("bank_two")));
	TestTrue(TEXT("an unknown root does not animate an arbitrary actor"),
		Multi.BankForRoot(TEXT("Bip99")).IsEmpty());
	TestTrue(TEXT("an empty root is ambiguous on a multi-actor set"),
		Multi.BankForRoot(FString()).IsEmpty());

	FElysiumCinematicSet Single;
	Single.Roots.Add(TEXT("bip01"), TEXT("solo_bank"));
	TestEqual(TEXT("a single-actor set may omit bonerename"),
		Single.BankForRoot(FString()), FString(TEXT("solo_bank")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAudioContractsTest,
	"Elysium.Substrate.AudioContracts", GElysiumTestFlags)

bool FElysiumAudioContractsTest::RunTest(const FString&)
{
	TestEqual(TEXT("leading sound and slash/case normalize once"),
		UElysiumAudioSubsystem::NormalizeSourcePath(
			TEXT(" /Sound\\Character/Dlg/Jack/LINE1_COL_E.MP3 ")),
		FString(TEXT("character/dlg/jack/line1_col_e.mp3")));
	TestEqual(TEXT("dialogue source derives its NPC line stem"),
		FElysiumLineService::DialogueLineSource(
			TEXT("E:/game/dlg/Main Characters/jack_tutorial.dlg"), 42),
		FString(TEXT("character/dlg/Main Characters/jack_tutorial/line42_col_e")));

	const FElysiumVoiceHandle First{ 7, 2 };
	const FElysiumVoiceHandle Reused{ 7, 3 };
	TestTrue(TEXT("generation distinguishes a reused slot"), First != Reused);
	TSet<FElysiumVoiceHandle> Handles;
	Handles.Add(First);
	TestFalse(TEXT("stale generation cannot find a reused voice"), Handles.Contains(Reused));

	TSharedPtr<FElysiumSoundCache::FDecoded, ESPMode::ThreadSafe> Pcm =
		MakeShared<FElysiumSoundCache::FDecoded, ESPMode::ThreadSafe>();
	Pcm->Info.Channels = 1;
	Pcm->Info.SampleRate = 4;
	Pcm->Info.FrameCount = 4;
	Pcm->Info.DurationSeconds = 1.f;
	const int16 Samples[] = { MIN_int16, -1, 0, MAX_int16 };
	Pcm->Pcm16.Append(reinterpret_cast<const uint8*>(Samples), sizeof(Samples));

	FSoundGeneratorInitParams GeneratorParams;
	GeneratorParams.NumChannels = 1;
	GeneratorParams.NumFramesPerCallback = 8;
	GeneratorParams.StartTime = 0.f;

	USoundWaveProcedural* OneShot = FElysiumSoundCache::MakeWave(Pcm, false);
	TestNotNull(TEXT("one-shot wave is created"), OneShot);
	if (OneShot)
	{
		TestEqual(TEXT("one-shot advances to EOF while inaudible"),
			OneShot->VirtualizationMode, EVirtualizationMode::PlayWhenSilent);
		ISoundGeneratorPtr Generator = OneShot->CreateSoundGenerator(GeneratorParams);
		float Out[8] = {};
		TestEqual(TEXT("one-shot generator stops at decoded EOF"),
			Generator->GetNextBuffer(Out, UE_ARRAY_COUNT(Out)), 4);
		TestTrue(TEXT("one-shot generator reports completion"), Generator->IsFinished());
	}

	USoundWaveProcedural* Loop = FElysiumSoundCache::MakeWave(Pcm, true);
	TestNotNull(TEXT("looping wave is created"), Loop);
	if (Loop)
	{
		TestEqual(TEXT("loop retains phase while inaudible"),
			Loop->VirtualizationMode, EVirtualizationMode::PlayWhenSilent);
		ISoundGeneratorPtr Generator = Loop->CreateSoundGenerator(GeneratorParams);
		float Out[10] = {};
		TestEqual(TEXT("looping generator fills across sample wrap"),
			Generator->GetNextBuffer(Out, UE_ARRAY_COUNT(Out)), 8);
		TestFalse(TEXT("looping generator does not report completion"), Generator->IsFinished());
		TestEqual(TEXT("loop wrap restarts at the first sample"), Out[4], Out[0]);
	}

	const FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();
	const FElysiumClassDesc* Base = Registry.BaseDesc();
	TestNotNull(TEXT("base entity descriptor exists"), Base);
	if (Base)
	{
		TestTrue(TEXT("PlayDialogFile is a real base input"),
			Registry.FindInput(*Base, TEXT("PlayDialogFile")) != nullptr);
		TestTrue(TEXT("SetSoundOverrideEnt is a real base input"),
			Registry.FindInput(*Base, TEXT("SetSoundOverrideEnt")) != nullptr);
		TestTrue(TEXT("SetFakeSilence is a real base input"),
			Registry.FindInput(*Base, TEXT("SetFakeSilence")) != nullptr);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLightRigTest,
	"Elysium.Substrate.LightRig", GElysiumTestFlags)

bool FElysiumLightRigTest::RunTest(const FString&)
{
	IFileManager::Get().MakeDirectory(*FPaths::AutomationTransientDir(), /*Tree*/ true);
	const FString LightsPath = FPaths::CreateTempFilename(
		*FPaths::AutomationTransientDir(), TEXT("ElysiumLightRig_"), TEXT(".lights"));
	const FString Sidecar =
		TEXT("1 0 0 0 0 0 0 100 50 25 1000 0 0 1 0 0\n")
		TEXT("2 0 0 0 1 0 0 50 40 30 2000 0.9396926 0.7660444 1 0 0\n")
		TEXT("3 0 0 0 0 0 -1 1 1 1 0 0 0 1 0 0\n");
	TestTrue(TEXT("synthetic light sidecar writes"), FFileHelper::SaveStringToFile(Sidecar, *LightsPath));

	UElysiumLightRig* Rig = NewObject<UElysiumLightRig>();
	UPointLightComponent* Point = NewObject<UPointLightComponent>();
	USpotLightComponent* Spot = NewObject<USpotLightComponent>();
	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>();
	Point->SetWorldLocation(FVector(100.f, 200.f, 300.f));

	TArray<UElysiumLightRig::FAdoptedLight> Adopted;
	Adopted.Add({Point, 0});
	Adopted.Add({Spot, 1});
	Adopted.Add({Sun, 2});
	TestEqual(TEXT("all synthetic sources adopt"), Rig->Adopt(Adopted, LightsPath), 3);

	TestFalse(TEXT("point baseline is non-inverse-square"), Point->bUseInverseSquaredFalloff != 0);
	TestTrue(TEXT("local source explicitly allows MegaLights"), Point->bAllowMegaLights != 0);
	TestEqual(TEXT("local source pins RT MegaLights shadows"),
		Point->MegaLightsShadowMethod.GetValue(), EMegaLightsShadowMethod::RayTracing);
	TestTrue(TEXT("point shadows are wired from rig calibration"), Point->CastShadows != 0);
	TestTrue(TEXT("spot inner cone comes from stopdot"), FMath::IsNearlyEqual(Spot->InnerConeAngle, 20.f, 0.05f));
	TestTrue(TEXT("spot outer cone comes from stopdot2"), FMath::IsNearlyEqual(Spot->OuterConeAngle, 40.f, 0.05f));

	Rig->SetNonSpotSourcesDisabled(true);
	TestTrue(TEXT("volumetric batch disables point"), Rig->IsSourceDisabled(0));
	TestFalse(TEXT("volumetric batch leaves spot alone"), Rig->IsSourceDisabled(1));
	TestTrue(TEXT("volumetric batch disables sun"), Rig->IsSourceDisabled(2));
	Rig->SetNonSpotSourcesDisabled(false);

	Point->SetSourceRadius(125.f);
	Point->SetIndirectLightingIntensity(3.f);
	Point->SetWorldLocation(FVector(999.f));
	Rig->SetSourceOverridden(0, true);
	Rig->RevertSource(0);
	TestFalse(TEXT("revert returns source to calibration"), Rig->IsSourceOverridden(0));
	TestTrue(TEXT("revert restores source shape"), FMath::IsNearlyZero(Point->SourceRadius));
	TestTrue(TEXT("revert restores Lumen contribution"),
		FMath::IsNearlyEqual(Point->IndirectLightingIntensity, 1.f));
	TestTrue(TEXT("revert restores authored transform"),
		Point->GetComponentLocation().Equals(FVector(100.f, 200.f, 300.f)));

	// The map-load contract restores global calibration first, then the complete override by the
	// stable sidecar index. Use the test's unique sidecar stem so no real survey can collide.
	const FString EditPath = FElysiumContentPaths::LightEdits(FPaths::GetBaseFilename(LightsPath));
	IFileManager::Get().MakeDirectory(*FElysiumContentPaths::LightEditsDir(), /*Tree*/ true);
	const FString SavedEdit = TEXT(R"JSON({
		"calibration": {
			"point_spot_scale": 0.004,
			"max_brightness": 12.0,
			"falloff_exponent": 1.5,
			"radius_scale": 1.25,
			"indirect_lighting_scale": 1.5,
			"volumetric_scattering_scale": 0.75,
			"point_shadows": false
		},
		"edits": [{
			"index": 0,
			"disabled": true,
			"overridden": true,
			"intensity": 2.5,
			"pos_cm": [10.0, 20.0, 30.0],
			"rot_deg": [0.0, 45.0, 0.0],
			"color": [0.2, 0.4, 0.8],
			"reach_cm": 3456.0,
			"falloff_exponent": 2.25,
			"source_radius_cm": 75.0,
			"soft_source_radius_cm": 50.0,
			"source_length_cm": 120.0,
			"indirect_lighting_scale": 2.0,
			"volumetric_scatter": 0.5,
			"specular_scale": 0.25,
			"cast_shadows": true,
			"cast_volumetric_shadow": false
		}]
	})JSON");
	TestTrue(TEXT("synthetic light edit writes"), FFileHelper::SaveStringToFile(SavedEdit, *EditPath));
	FString LoadMessage;
	TestTrue(TEXT("saved light edit loads"), Rig->LoadSurvey(LoadMessage));
	TestTrue(TEXT("saved calibration restores"), FMath::IsNearlyEqual(Rig->PointSpotScale, 0.004f));
	TestTrue(TEXT("saved override restores"), Rig->IsSourceOverridden(0));
	TestTrue(TEXT("saved disabled state restores"), Rig->IsSourceDisabled(0));
	TestTrue(TEXT("saved intensity restores"), FMath::IsNearlyEqual(Point->Intensity, 2.5f));
	TestTrue(TEXT("saved transform restores"), Point->GetComponentLocation().Equals(FVector(10.f, 20.f, 30.f)));
	TestTrue(TEXT("saved reach restores"), FMath::IsNearlyEqual(Point->AttenuationRadius, 3456.f));
	TestTrue(TEXT("saved source shape restores"),
		FMath::IsNearlyEqual(Point->SourceRadius, 75.f)
		&& FMath::IsNearlyEqual(Point->SoftSourceRadius, 50.f)
		&& FMath::IsNearlyEqual(Point->SourceLength, 120.f));
	TestTrue(TEXT("saved light transport restores"),
		FMath::IsNearlyEqual(Point->IndirectLightingIntensity, 2.f)
		&& FMath::IsNearlyEqual(Point->VolumetricScatteringIntensity, 0.5f)
		&& FMath::IsNearlyEqual(Point->SpecularScale, 0.25f));
	TestTrue(TEXT("saved shadow overrides restore"), Point->CastShadows != 0
		&& Point->bCastVolumetricShadow == 0);

	IFileManager::Get().Delete(*EditPath, /*RequireExists*/ false, /*EvenReadOnly*/ true);
	IFileManager::Get().Delete(*LightsPath, /*RequireExists*/ false, /*EvenReadOnly*/ true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeatherStateTest,
	"Elysium.Substrate.Weather.State", GElysiumTestFlags)
bool FElysiumWeatherStateTest::RunTest(const FString&)
{
	FElysiumWeatherState State;
	State.Configure(/*fade in*/ 10.0f, /*fade out*/ 20.0f, /*initial*/ 0.0f, 0.0);
	State.Retarget(1.0f, 0.0);
	State.Tick(5.0);
	TestTrue(TEXT("ten-second wet fade is halfway at five seconds"),
		FMath::IsNearlyEqual(State.CurrentWetness, 0.5f));

	State.Retarget(0.0f, 5.0);
	State.Tick(15.0);
	TestTrue(TEXT("mid-fade retarget is continuous and uses the twenty-second dry duration"),
		FMath::IsNearlyEqual(State.CurrentWetness, 0.25f));
	State.Tick(15.0);
	TestTrue(TEXT("a paused game clock does not advance wetness"),
		FMath::IsNearlyEqual(State.CurrentWetness, 0.25f));
	State.Retarget(2.0f, 15.0);
	TestTrue(TEXT("wetness targets clamp to one"), FMath::IsNearlyEqual(State.TargetWetness, 1.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeatherTimerSequenceTest,
	"Elysium.Substrate.Weather.TimerSequence", GElysiumTestFlags)
bool FElysiumWeatherTimerSequenceTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("sm_hub_1");
	auto AddWire = [](FElysiumEntityDef& Source, const TCHAR* Target,
		const TCHAR* Input, const TCHAR* Param, float Delay)
	{
		FElysiumOutputDef Wire;
		Wire.Name = TEXT("OnTimer");
		Wire.Target = Target;
		Wire.Input = Input;
		Wire.Param = Param;
		Wire.Delay = Delay;
		Wire.Times = -1;
		Source.Outputs.Add(MoveTemp(Wire));
	};

	FElysiumEntityDef WorldSpawn;
	WorldSpawn.Classname = TEXT("worldspawn");
	WorldSpawn.Keys.Add(TEXT("wetness_fadein"), TEXT("10"));
	WorldSpawn.Keys.Add(TEXT("wetness_fadeout"), TEXT("20"));
	WorldSpawn.Keys.Add(TEXT("wetness_fadetarget"), TEXT("0"));
	Defs.Defs.Add(MoveTemp(WorldSpawn));
	FElysiumEntityDef EventsWorld;
	EventsWorld.Classname = TEXT("events_world");
	EventsWorld.TargetName = TEXT("world");
	Defs.Defs.Add(MoveTemp(EventsWorld));
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FElysiumEntityDef Emitter;
		Emitter.Classname = TEXT("env_particle");
		Emitter.TargetName = TEXT("rain_emitter");
		Emitter.Keys.Add(TEXT("active"), TEXT("1"));
		Emitter.Keys.Add(TEXT("particle_definition"), TEXT("rain_follow_emitter"));
		Emitter.Keys.Add(TEXT("attach_type"), TEXT("11"));
		Emitter.Keys.Add(TEXT("bounds"), Index == 0 ? TEXT("512") : TEXT("256"));
		Emitter.Keys.Add(TEXT("ramp_scale"), TEXT("0"));
		Emitter.Keys.Add(TEXT("ramp_time"), TEXT("10"));
		Defs.Defs.Add(MoveTemp(Emitter));
	}
	FElysiumEntityDef Sound;
	Sound.Classname = TEXT("ambient_generic");
	Sound.TargetName = TEXT("rain_sounds");
	Sound.Keys.Add(TEXT("spawnflags"), TEXT("17"));
	Sound.Keys.Add(TEXT("message"), TEXT("area/Santa_Monica/rain_light_loop.wav"));
	Sound.Keys.Add(TEXT("health"), TEXT("4"));
	Sound.Keys.Add(TEXT("fadein"), TEXT("10"));
	Sound.Keys.Add(TEXT("fadeout"), TEXT("10"));
	Defs.Defs.Add(MoveTemp(Sound));

	FElysiumEntityDef On;
	On.Classname = TEXT("logic_timer");
	On.TargetName = TEXT("rain_on_timer");
	On.Keys.Add(TEXT("StartDisabled"), TEXT("0"));
	On.Keys.Add(TEXT("UseRandomTime"), TEXT("1"));
	On.Keys.Add(TEXT("LowerRandomBound"), TEXT("180"));
	On.Keys.Add(TEXT("UpperRandomBound"), TEXT("300"));
	AddWire(On, TEXT("rain_sounds"), TEXT("PlaySound"), TEXT(""), 0.0f);
	AddWire(On, TEXT("rain_emitter"), TEXT("SetRateScale"), TEXT("1"), 0.0f);
	AddWire(On, TEXT("world"), TEXT("FadeGlobalWetness"), TEXT("1"), 10.0f);
	AddWire(On, TEXT("rain_on_timer"), TEXT("Disable"), TEXT(""), 1.0f);
	AddWire(On, TEXT("rain_off_timer"), TEXT("Enable"), TEXT(""), 1.0f);
	Defs.Defs.Add(MoveTemp(On));

	FElysiumEntityDef Off;
	Off.Classname = TEXT("logic_timer");
	Off.TargetName = TEXT("rain_off_timer");
	Off.Keys.Add(TEXT("StartDisabled"), TEXT("1"));
	Off.Keys.Add(TEXT("UseRandomTime"), TEXT("1"));
	Off.Keys.Add(TEXT("LowerRandomBound"), TEXT("180"));
	Off.Keys.Add(TEXT("UpperRandomBound"), TEXT("500"));
	AddWire(Off, TEXT("rain_sounds"), TEXT("StopSound"), TEXT(""), 0.0f);
	AddWire(Off, TEXT("rain_emitter"), TEXT("SetRateScale"), TEXT("0"), 0.0f);
	AddWire(Off, TEXT("world"), TEXT("FadeGlobalWetness"), TEXT("0"), 10.0f);
	AddWire(Off, TEXT("rain_off_timer"), TEXT("Disable"), TEXT(""), 1.0f);
	AddWire(Off, TEXT("rain_on_timer"), TEXT("Enable"), TEXT(""), 1.0f);
	Defs.Defs.Add(MoveTemp(Off));

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);
	FElysiumEntity* OnTimer = World.FindByName(TEXT("rain_on_timer"));
	FElysiumEntity* OffTimer = World.FindByName(TEXT("rain_off_timer"));
	if (!TestNotNull(TEXT("rain_on_timer exists"), OnTimer)
		|| !TestNotNull(TEXT("rain_off_timer exists"), OffTimer))
	{
		return false;
	}
	TestTrue(TEXT("dry interval is authored random 180-300 seconds"),
		OnTimer->NextThink >= 180.0f && OnTimer->NextThink <= 300.0f);
	TestTrue(TEXT("rain-off timer starts disabled"), OffTimer->NextThink >= ELYSIUM_NEVER_THINK);
	Services.Calls.Reset();

	World.AcceptInput(TEXT("rain_on_timer"), FName(TEXT("FireTimer")),
		FElysiumVariant::Void(), FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(0.0);
	TestEqual(TEXT("rain-on emits one audio start"), Services.Count(TEXT("Submit ")), 1);
	TestTrue(TEXT("authored audio fade-in is applied"), Services.Saw(TEXT("Submit area/Santa_Monica/rain_light_loop.wav"))
		&& Services.Calls.ContainsByPredicate([](const FString& Call)
		{
			return Call.StartsWith(TEXT("Submit ")) && Call.Contains(TEXT("fade=10.00"));
		}));
	for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : Services.Emitters)
	{
		TestTrue(TEXT("rain-on fans out once to each emitter"),
			FMath::IsNearlyEqual(Pair.Value.RampTargetScale, 1.0f));
	}
	World.Tick(1.0);
	TestTrue(TEXT("rain-on disables after one second"), OnTimer->NextThink >= ELYSIUM_NEVER_THINK);
	TestTrue(TEXT("rain duration is authored random 180-500 seconds"),
		OffTimer->NextThink >= 181.0f && OffTimer->NextThink <= 501.0f);
	World.Tick(10.0);
	TestTrue(TEXT("delayed wetness-on reaches target one"),
		FMath::IsNearlyEqual(Services.LastWetness.TargetWetness, 1.0f));

	World.AcceptInput(TEXT("rain_off_timer"), FName(TEXT("FireTimer")),
		FElysiumVariant::Void(), FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(10.0);
	TestTrue(TEXT("authored audio fade-out is applied"),
		Services.Calls.ContainsByPredicate([](const FString& Call)
		{
			return Call.StartsWith(TEXT("StopVoice ")) && Call.Contains(TEXT("fade=10.00"));
		}));
	for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : Services.Emitters)
	{
		TestTrue(TEXT("rain-off fans out once to each emitter"),
			FMath::IsNearlyEqual(Pair.Value.RampTargetScale, 0.0f));
	}
	World.Tick(11.0);
	TestTrue(TEXT("rain-off disables after one second"), OffTimer->NextThink >= ELYSIUM_NEVER_THINK);
	TestTrue(TEXT("dry timer re-arms without duplicate output"),
		OnTimer->NextThink >= 191.0f && OnTimer->NextThink <= 311.0f);
	World.Tick(20.0);
	TestTrue(TEXT("delayed wetness-off reaches target zero"),
		FMath::IsNearlyZero(Services.LastWetness.TargetWetness));

	World.AcceptInput(TEXT("rain_on_timer"), FName(TEXT("FireTimer")),
		FElysiumVariant::Void(), FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(20.0);
	TestEqual(TEXT("the repeating cycle produces one audio start per rain-on"),
		Services.Count(TEXT("Submit ")), 2);
	return true;
}

// =====================================================================================
// The stub report — that an unimplemented surface says so, and that an implemented one
// stays quiet. The second half is the one worth guarding: a stub class names the inputs
// the shipped maps fire at a classname with no leaf, and if such a row ever shadowed a
// base input (Kill/ScriptHide/ScriptUnhide) it would turn a working input into a warning
// that does nothing. The tally is the observable; the warning text is cosmetic, so the
// volume is turned down for the duration rather than expected line by line.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStubReportTest, "Elysium.Substrate.Stubs", GElysiumTestFlags)
bool FElysiumStubReportTest::RunTest(const FString&)
{
	IConsoleVariable* Warn = IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.StubWarn"));
	const int32 PrevWarn = Warn ? Warn->GetInt() : 2;
	if (Warn) { Warn->Set(0); }
	ElysiumStub::ClearTally();
	ON_SCOPE_EXIT
	{
		if (Warn) { Warn->Set(PrevWarn); }
		ElysiumStub::ClearTally();
	};

	// One stub class (env_sprite — 208 wires across the shipped maps, no leaf), one classname with
	// no registration at all, and one real class to prove the quiet path.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");
	for (const TCHAR* Pair : { TEXT("env_sprite|sprite1"), TEXT("func_lod|lod1"), TEXT("math_counter|counter1") })
	{
		FString Class, Name;
		FString(Pair).Split(TEXT("|"), &Class, &Name);
		FElysiumEntityDef Def;
		Def.Classname = Class;
		Def.TargetName = Name;
		Defs.Defs.Add(MoveTemp(Def));
	}

	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* Sprite = World.FindByName(TEXT("sprite1"));
	FElysiumEntity* Lod = World.FindByName(TEXT("lod1"));
	FElysiumEntity* Counter = World.FindByName(TEXT("counter1"));
	if (!TestNotNull(TEXT("sprite1 resolved"), Sprite)
		|| !TestNotNull(TEXT("lod1 resolved"), Lod)
		|| !TestNotNull(TEXT("counter1 resolved"), Counter))
	{
		return false;
	}

	// A stub class is still an inert record: it names inputs, it does not implement any.
	TestTrue(TEXT("a stub class spawns record-only"), Sprite->IsRecordOnly());
	TestTrue(TEXT("an unregistered classname spawns record-only"), Lod->IsRecordOnly());
	TestFalse(TEXT("a real class does not"), Counter->IsRecordOnly());

	auto FireAt = [&World](const TCHAR* Target, const TCHAR* Input)
	{
		World.AcceptInput(Target, FName(Input), FElysiumVariant::Void(),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	};
	auto CountFor = [](const TCHAR* Kind, const TCHAR* Surface) -> int32
	{
		TArray<ElysiumStub::FTally> Rows;
		ElysiumStub::CollectTally(Rows);
		for (const ElysiumStub::FTally& R : Rows)
		{
			if (R.Kind == Kind && R.Surface == Surface) { return R.Count; }
		}
		return 0;
	};

	// 1. A stub class's named input resolves to the shared thunk and reports under its own name.
	FireAt(TEXT("sprite1"), TEXT("HideSprite"));
	FireAt(TEXT("sprite1"), TEXT("HideSprite"));
	TestEqual(TEXT("a stub input reports once per fire"),
		CountFor(TEXT("input"), TEXT("env_sprite.HideSprite")), 2);

	// 2. An input no class on the chain owns reports too — this is what covers the classnames the
	//    stub table does not enumerate.
	FireAt(TEXT("lod1"), TEXT("Frobnicate"));
	TestEqual(TEXT("an unresolvable input reports"),
		CountFor(TEXT("input"), TEXT("func_lod.Frobnicate")), 1);

	// 3. A wire naming an entity the map does not contain is its own kind, not an input gap.
	FireAt(TEXT("no_such_entity"), TEXT("Trigger"));
	TestEqual(TEXT("an unknown target reports as a target"),
		CountFor(TEXT("target"), TEXT("no_such_entity.Trigger")), 1);

	// 4. The shadowing guard: base inputs still work on a stub class and report nothing.
	FireAt(TEXT("sprite1"), TEXT("ScriptHide"));
	TestTrue(TEXT("a base input still reaches a stub class"), Sprite->IsHidden());
	TestEqual(TEXT("a base input on a stub class reports nothing"),
		CountFor(TEXT("input"), TEXT("env_sprite.ScriptHide")), 0);

	// 5. And an implemented input on a real class stays quiet.
	FireAt(TEXT("counter1"), TEXT("Add"));
	TestEqual(TEXT("an implemented input reports nothing"),
		CountFor(TEXT("input"), TEXT("math_counter.Add")), 0);

	return true;
}

// =====================================================================================
// Gaze — the selection cascade, the cone gate, the scripted inputs and the integrator (12.4)
// =====================================================================================
//
// Every arm of this is a plain function of positions and time, which is the point: the half that
// decides where a character looks holds no engine state, so the whole cascade is assertable here
// rather than only in a running world. The one thing this cannot check is that the answer reaches
// a material — that is the content tier's FacialMorphTargets and the live run's job.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGazeTest, "Elysium.Substrate.Gaze", GElysiumTestFlags)
bool FElysiumGazeTest::RunTest(const FString&)
{
	auto MakeWorld = [](FElysiumEntityWorld& World)
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__gaze_test__");
		FElysiumEntityDef Watcher;
		Watcher.Classname = TEXT("npc_VVampire");
		Watcher.TargetName = TEXT("watcher");
		Watcher.Origin = FVector::ZeroVector;
		Watcher.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
		Defs.Defs.Add(MoveTemp(Watcher));
		FElysiumEntityDef Prop;
		Prop.Classname = TEXT("prop_static");
		Prop.TargetName = TEXT("statue");
		Prop.Origin = FVector(300.f, 0.f, 0.f);
		Defs.Defs.Add(MoveTemp(Prop));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);
	};

	// The head frame every assertion measures in: eye height, facing +X.
	const FVector Head(0.f, 0.f, ElysiumMove::StandViewZ);
	const FVector Forward(1.f, 0.f, 0.f);
	FElysiumEyeTargetTuning Tuning;
	Tuning.TurnRate = 0.5f;
	// Keep the saccade out of the way of the selection assertions — a fidget would move the
	// commanded point off the subject as soon as the eyes converged on it.
	Tuning.MinInterval = 1000.f;
	Tuning.MaxInterval = 1000.f;

	// --- EyePosition is the view offset, not a bounds fraction -------------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		MakeWorld(World);
		FElysiumPlayer* Player = World.FindPlayer();
		if (!TestNotNull(TEXT("the world has a player"), Player))
		{
			return false;
		}
		Player->Origin = FVector(10.f, 20.f, 30.f);
		TestTrue(TEXT("EyePosition is origin + the standing view offset"),
			Player->EyePosition().Equals(FVector(10.f, 20.f, 30.f + ElysiumMove::StandViewZ)));
	}

	// --- The autonomous scan, and the cone that gates it --------------------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		MakeWorld(World);
		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumEntity* Raw = World.FindByName(TEXT("watcher"));
		FElysiumCombatCharacter* Watcher = Raw ? Raw->AsCombatCharacter() : nullptr;
		if (!TestNotNull(TEXT("the watcher is a combat character"), Watcher) || Player == nullptr)
		{
			return false;
		}

		// Straight ahead and well inside the 300-unit scan sphere: the player is picked.
		Player->Origin = FVector(500.f, 0.f, 0.f);
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		TestTrue(TEXT("a candidate inside the cone is chosen, at its EyePosition"),
			Watcher->EyeLookTarget.Equals(Player->EyePosition(), 0.1f));

		// The same candidate off to the side is outside the ±30° cone, so the character looks
		// straight ahead instead. dot((1,0,0), normalize(100,500,0)) = 0.196, well under 0.866.
		Watcher->NextEyeLookTime = 0.f;   // let the scan re-pick
		Player->Origin = FVector(100.f, 500.f, 0.f);
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		const FVector Ahead = Head + Forward * (500.f * ElysiumMove::U);
		TestTrue(TEXT("a candidate outside the cone is rejected for straight ahead"),
			Watcher->EyeLookTarget.Equals(Ahead, 0.1f));

		// A prop is not a candidate however well placed — retail's filter admits the player and
		// characters, and `statue` sits dead ahead at 300 units.
		Watcher->NextEyeLookTime = 0.f;
		Player->Origin = FVector(-500.f, 0.f, 0.f);   // behind, so only the prop is in the cone
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		TestTrue(TEXT("a non-character in the cone is not a gaze candidate"),
			Watcher->EyeLookTarget.Equals(Ahead, 0.1f));
	}

	// --- The dialogue arm, and the DialogPOV redirect -----------------------------------------
	// The camera-shot table's how-to defines `DialogPOV "1"` as "NPCs will look at the camera during
	// dialog, rather than the player's eye position", and 51 of the 66 shipped shot files set it —
	// so this, not the 40 authored `LookAtEntity*` wires, is where most of VtMB's look-at happens.
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		MakeWorld(World);
		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumEntity* Raw = World.FindByName(TEXT("watcher"));
		FElysiumCombatCharacter* Watcher = Raw ? Raw->AsCombatCharacter() : nullptr;
		if (Watcher == nullptr || Player == nullptr)
		{
			return false;
		}
		Player->Origin = FVector(500.f, 0.f, 0.f);
		Watcher->NextFidgetTime = TNumericLimits<float>::Max();

		// One think before the conversation opens. The mind admits an NPC on its first frozen-time
		// think and refuses every body acquisition until then, so a dialogue opened against a
		// never-ticked NPC is refused — a state the real path cannot reach, since an NPC has always
		// thought at least once before the player can talk to it.
		World.Tick(0.0);

		// The smallest conversation that opens: one spoken NPC line.
		TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
		if (!TestTrue(TEXT("the gaze fixture conversation parses"),
			FElysiumDlgFile::ParseBytes(
				ElysiumDlgBytes({ ElysiumDlgRow(11, TEXT("Hello."), TEXT("#"), TEXT(""), TEXT("")) }),
				File.Get())))
		{
			return false;
		}
		TSharedRef<FElysiumDlgConversation> Conv = MakeShared<FElysiumDlgConversation>(
			File, /*bMale*/ true, /*bMalk*/ false,
			[](const FString&) { return true; }, [](const FString&) {});
		Conv->Start();
		World.OpenDialog(Watcher->Handle, Conv);

		// With no shot asking for it, the NPC aims at the player's eye.
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		TestTrue(TEXT("the dialogue arm aims at the partner's EyePosition"),
			Watcher->EyeLookTarget.Equals(Player->EyePosition(), 0.1f));

		// With one, it aims at the camera instead. Placed inside the cone so nothing else can be
		// what moved it, and away from the player so the two answers cannot be confused.
		const FVector CameraPoint(420.f, 60.f, ElysiumMove::StandViewZ + 40.f);
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning, &CameraPoint);
		TestTrue(TEXT("DialogPOV redirects the dialogue arm to the camera"),
			Watcher->EyeLookTarget.Equals(CameraPoint, 0.1f));

		// It replaces the *player* as the subject and nothing else: the player looking back at the
		// NPC still resolves the NPC, with the same point supplied.
		Player->NextFidgetTime = TNumericLimits<float>::Max();
		Player->TickGaze(0.f, 0.f, Head, Forward, Tuning, &CameraPoint);
		TestTrue(TEXT("DialogPOV does not redirect the player's own aim"),
			Player->EyeLookTarget.Equals(Watcher->EyePosition(), 0.1f));
	}

	// --- The four scripted inputs -------------------------------------------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		MakeWorld(World);
		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumEntity* Raw = World.FindByName(TEXT("watcher"));
		FElysiumCombatCharacter* Watcher = Raw ? Raw->AsCombatCharacter() : nullptr;
		if (Watcher == nullptr || Player == nullptr)
		{
			return false;
		}
		Player->Origin = FVector(500.f, 0.f, 0.f);
		// Hold the saccade off. It engages the moment the eyes converge, and every assertion below
		// is about *what was selected*, which a fidget would immediately move off.
		Watcher->NextFidgetTime = TNumericLimits<float>::Max();

		FElysiumInputArgs Args;
		Args.Param = FElysiumVariant::String(ElysiumPlayerTargetName());

		Watcher->InputLookAtEntityEye(Args);
		TestEqual(TEXT("LookAtEntityEye pushes mode 1"), Watcher->EyeLookMode, 1);
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		TestTrue(TEXT("...and aims at the target's EyePosition"),
			Watcher->EyeLookTarget.Equals(Player->EyePosition(), 0.1f));

		// The shipped defect: Center pushes the Eye constant, so it lands on EyePosition too. If
		// this ever starts resolving WorldSpaceCenter, we have diverged from retail.
		Watcher->InputLookAtEntityDefault(Args);
		Watcher->InputLookAtEntityCenter(Args);
		TestEqual(TEXT("LookAtEntityCenter pushes mode 1, reproducing the shipped defect"),
			Watcher->EyeLookMode, 1);
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		TestTrue(TEXT("...so Center aims exactly where Eye does"),
			Watcher->EyeLookTarget.Equals(Player->EyePosition(), 0.1f));

		Watcher->InputLookAtEntityOrigin(Args);
		TestEqual(TEXT("LookAtEntityOrigin pushes mode 3"), Watcher->EyeLookMode, 3);
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		TestTrue(TEXT("...and aims at the target's origin, not its eyes"),
			Watcher->EyeLookTarget.Equals(Player->Origin, 0.1f));

		Watcher->InputLookAtEntityDefault(Args);
		TestEqual(TEXT("LookAtEntityDefault clears the mode"), Watcher->EyeLookMode, 0);
		TestTrue(TEXT("...and the target name with it"), Watcher->EyeLookTargetName.IsEmpty());

		// A scripted target naming an entity that is not there yields back to autonomous rather
		// than holding a dead aim.
		FElysiumInputArgs Gone;
		Gone.Param = FElysiumVariant::String(TEXT("no_such_entity"));
		Watcher->InputLookAtEntityEye(Gone);
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		TestEqual(TEXT("a scripted target that is gone releases to autonomous"),
			Watcher->EyeLookMode, 0);
	}

	// --- The integrator is a fixed 0.1 s step, not a per-frame lerp ---------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		MakeWorld(World);
		FElysiumEntity* Raw = World.FindByName(TEXT("watcher"));
		FElysiumCombatCharacter* Watcher = Raw ? Raw->AsCombatCharacter() : nullptr;
		FElysiumPlayer* Player = World.FindPlayer();
		if (Watcher == nullptr || Player == nullptr)
		{
			return false;
		}
		Player->Origin = FVector(500.f, 0.f, 0.f);
		Watcher->NextFidgetTime = TNumericLimits<float>::Max();
		FElysiumInputArgs Args;
		Args.Param = FElysiumVariant::String(ElysiumPlayerTargetName());
		Watcher->InputLookAtEntityEye(Args);

		// Seed the smoothed point somewhere definite, then step exactly one interval at rate 0.5:
		// the result must be the midpoint. A frame-rate-dependent lerp would land elsewhere.
		Watcher->TickGaze(0.f, 0.f, Head, Forward, Tuning);
		const FVector Commanded = Watcher->EyeLookTarget;
		const FVector Seed(0.f, 0.f, 100.f);
		Watcher->CurEyeTarget = Seed;
		Watcher->EyeIntegAccumulator = 0.f;
		Watcher->TickGaze(0.f, 0.1f, Head, Forward, Tuning);
		TestTrue(TEXT("one 0.1 s step at rate 0.5 moves the smoothed point halfway"),
			Watcher->CurEyeTarget.Equals(Seed + (Commanded - Seed) * 0.5f, 0.5f));

		// Two half-steps make one whole one — the accumulator carries the remainder rather than
		// discarding it, which is the whole reason the step is fixed.
		Watcher->CurEyeTarget = Seed;
		Watcher->EyeIntegAccumulator = 0.f;
		Watcher->TickGaze(0.f, 0.05f, Head, Forward, Tuning);
		TestTrue(TEXT("half an interval alone moves nothing"),
			Watcher->CurEyeTarget.Equals(Seed, 0.01f));
		Watcher->TickGaze(0.f, 0.05f, Head, Forward, Tuning);
		TestTrue(TEXT("...and the second half completes the same single step"),
			Watcher->CurEyeTarget.Equals(Seed + (Commanded - Seed) * 0.5f, 0.5f));

		TestEqual(TEXT("the integration rate is published from the disposition"),
			Watcher->EyeIntegRate, 0.5f);
	}

	// --- The fidget walks the authored keypad cells in order ----------------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		MakeWorld(World);
		FElysiumEntity* Raw = World.FindByName(TEXT("watcher"));
		FElysiumCombatCharacter* Watcher = Raw ? Raw->AsCombatCharacter() : nullptr;
		FElysiumPlayer* Player = World.FindPlayer();
		if (Watcher == nullptr || Player == nullptr)
		{
			return false;
		}
		Player->Origin = FVector(500.f, 0.f, 0.f);
		FElysiumInputArgs Args;
		Args.Param = FElysiumVariant::String(ElysiumPlayerTargetName());
		Watcher->InputLookAtEntityEye(Args);

		// Anger's authored triple, and a zero hold so each call advances exactly one step.
		FElysiumEyeTargetTuning Anger;
		Anger.FidgetPoints[0] = 0;
		Anger.FidgetPoints[1] = 2;
		Anger.FidgetPoints[2] = 0;
		Anger.HoldMin = 0.f;
		Anger.HoldMax = 0.f;
		Anger.TurnRate = 1.f;   // converge immediately so the saccade can engage

		Watcher->TickGaze(0.f, 1.f, Head, Forward, Anger);   // converge on the target
		Watcher->TickGaze(1.f, 0.1f, Head, Forward, Anger);  // step 0 -> cell 0
		TestEqual(TEXT("the fidget starts at the first authored cell"), Watcher->FidgetCell, 0);
		TestEqual(TEXT("...as step 0"), Watcher->FidgetStep, 0);

		Watcher->TickGaze(2.f, 0.1f, Head, Forward, Anger);
		TestEqual(TEXT("the second step takes the second authored cell"), Watcher->FidgetCell, 2);
		// Cell 2 is bottom-centre: 20 degrees below the head's forward, no yaw. That has to move the
		// commanded point DOWN and leave it dead ahead in plan, or the keypad is transposed.
		TestTrue(TEXT("cell 2 aims below the head"), Watcher->EyeLookTarget.Z < Head.Z);
		TestTrue(TEXT("...and does not yaw off centre"),
			FMath::IsNearlyZero(Watcher->EyeLookTarget.Y, 0.5f));

		Watcher->TickGaze(3.f, 0.1f, Head, Forward, Anger);
		TestEqual(TEXT("the third step takes the third authored cell"), Watcher->FidgetCell, 0);
		Watcher->TickGaze(4.f, 0.1f, Head, Forward, Anger);
		TestEqual(TEXT("exhausting the sequence ends the fidget"), Watcher->FidgetStep, -1);

		// Cell 8 is top-centre, the mirror of cell 2 — the row arithmetic has to be symmetric. One
		// call, not two: with a zero hold every call advances a step, so a second would already be
		// on the next cell.
		FElysiumEyeTargetTuning Up = Anger;
		Up.FidgetPoints[0] = 8;
		Watcher->FidgetStep = -1;
		Watcher->NextFidgetTime = 0.f;
		Watcher->TickGaze(5.f, 0.1f, Head, Forward, Up);
		TestEqual(TEXT("cell 8 is the top-centre cell"), Watcher->FidgetCell, 8);
		TestTrue(TEXT("...and aims above the head"), Watcher->EyeLookTarget.Z > Head.Z);
	}

	return true;
}

// =====================================================================================
// Blend grids (CAP7.3) — the axis arithmetic, content-free.
//
// The bug this guards is not subtle once stated: a VtMB locomotion sequence is a 9-cell fan over
// `move_yaw` running -180..180, and the exporter bakes the grid's base cell under the sequence's
// label. Cell 0 is the -180 cell, so the clip named `walk` is the BACKWARD walk. The whole point of
// resolving the grid is that 0 degrees lands on cell 4 and the character walks forward.
// =====================================================================================

namespace
{
	FElysiumBlendTable MakeYawTable()
	{
		FElysiumBlendTable Table;
		FElysiumPoseParamDesc MoveYaw;
		MoveYaw.Name = TEXT("move_yaw");
		MoveYaw.Flags = 1;
		MoveYaw.Start = -180.f;
		MoveYaw.End = 180.f;
		MoveYaw.Loop = 360.f;      // wraps
		Table.PoseParams.Add(MoveYaw);

		FElysiumPoseParamDesc AimYaw;
		AimYaw.Name = TEXT("aim_yaw");
		AimYaw.Start = -45.f;
		AimYaw.End = 45.f;
		AimYaw.Loop = 0.f;         // does NOT wrap
		Table.PoseParams.Add(AimYaw);

		// The shipped shape: 9 cells on axis 0, no axis 1.
		FElysiumBlendGrid Walk;
		Walk.Label = TEXT("walk");
		Walk.GroupSize[0] = 9;
		Walk.GroupSize[1] = 1;
		Walk.ParamIndex[0] = 0;
		Walk.ParamIndex[1] = INDEX_NONE;
		Walk.ParamStart[0] = -180.f;
		Walk.ParamEnd[0] = 180.f;
		static const TCHAR* Names[9] = { TEXT("walk_180"), TEXT("walk_225"), TEXT("walk_270"),
			TEXT("walk_315"), TEXT("walk_0"), TEXT("walk_45"), TEXT("walk_90"), TEXT("walk_135"),
			TEXT("walk_180") };
		for (int32 i = 0; i < 9; ++i)
		{
			FElysiumBlendCell Cell;
			Cell.Axis[0] = i;
			Cell.Axis[1] = 0;
			Cell.Clip = Names[i];
			if (i == 4)
			{
				Cell.Motion.CycleSeconds = 1.2f;
				Cell.Motion.GroundDistanceCm = 164.0f;
				Cell.Motion.GroundSpeedCmPerSecond = 136.7f;
			}
			Walk.Cells.Add(Cell);
		}
		Table.Grids.Add(Walk.Label, Walk);
		return Table;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBlendGridAxisTest,
	"Elysium.Substrate.BlendGrids", GElysiumTestFlags)
bool FElysiumBlendGridAxisTest::RunTest(const FString&)
{
	const FElysiumBlendTable Table = MakeYawTable();
	const FElysiumBlendGrid& Walk = *Table.Find(TEXT("walk"));
	const FElysiumPoseParamDesc* MoveYaw = Table.Param(0);

	int32 Cell = -1;
	float Fraction = -1.f;

	// THE assertion. A body nothing has steered sits at move_yaw 0, which is straight ahead, and a
	// -180..180 fan of nine puts that exactly on cell 4. Anything else and `walk` plays sideways or
	// backwards.
	ElysiumBlendGrids::ResolveAxis(Walk, 0, MoveYaw, 0.f, Cell, Fraction);
	TestEqual(TEXT("move_yaw 0 selects the middle cell"), Cell, 4);
	TestTrue(TEXT("...exactly, with no fraction"), FMath::IsNearlyZero(Fraction));

	// The endpoints. Under a 360 wrap +180 IS -180 — the same heading named twice — so it resolves
	// to cell 0, and the fan's last cell holds the same clip precisely because of that seam. Cell 8
	// is therefore unreachable by any wrapped value, which costs nothing: it is a duplicate.
	ElysiumBlendGrids::ResolveAxis(Walk, 0, MoveYaw, -180.f, Cell, Fraction);
	TestEqual(TEXT("-180 is cell 0"), Cell, 0);
	ElysiumBlendGrids::ResolveAxis(Walk, 0, MoveYaw, 180.f, Cell, Fraction);
	TestEqual(TEXT("+180 wraps onto the same cell as -180"), Cell, 0);
	TestEqual(TEXT("...and the two ends of the fan are the same animation"),
		Walk.CellAt(0, 0)->Clip, Walk.CellAt(8, 0)->Clip);

	// Just short of the seam is the far end of the fan.
	ElysiumBlendGrids::ResolveAxis(Walk, 0, MoveYaw, 179.f, Cell, Fraction);
	TestEqual(TEXT("179 degrees is the last distinct cell"), Cell, 7);
	TestTrue(TEXT("...nearly all the way to the seam"), Fraction > 0.9f);

	// A value past the end wraps through the loop modulus rather than clamping: 190 is -170.
	ElysiumBlendGrids::ResolveAxis(Walk, 0, MoveYaw, 190.f, Cell, Fraction);
	TestEqual(TEXT("190 wraps to -170, just past cell 0"), Cell, 0);
	TestTrue(TEXT("...carrying a fraction toward cell 1"), Fraction > 0.f);

	// Halfway between two cells reports the fraction the two-cell blend will weigh on.
	ElysiumBlendGrids::ResolveAxis(Walk, 0, MoveYaw, 22.5f, Cell, Fraction);
	TestEqual(TEXT("22.5 degrees sits on cell 4"), Cell, 4);
	TestTrue(TEXT("...half a cell toward cell 5"), FMath::IsNearlyEqual(Fraction, 0.5f, 0.001f));

	// An unused axis: documented as cell 0, weight 0. Its range is a degenerate 0/0, so this also
	// proves nothing divided by it.
	ElysiumBlendGrids::ResolveAxis(Walk, 1, nullptr, 0.f, Cell, Fraction);
	TestEqual(TEXT("an axis with no parameter is cell 0"), Cell, 0);
	TestTrue(TEXT("...with no weight"), FMath::IsNearlyZero(Fraction));

	// A non-wrapping parameter must CLAMP, not wrap. The aim parameters declare loop 0, and wrapping
	// one would swing a gun to the opposite extreme at the edge of its range.
	FElysiumBlendGrid Aim;
	Aim.Label = TEXT("aim");
	Aim.GroupSize[0] = 3;
	Aim.GroupSize[1] = 1;
	Aim.ParamIndex[0] = 1;
	Aim.ParamIndex[1] = INDEX_NONE;
	Aim.ParamStart[0] = -45.f;
	Aim.ParamEnd[0] = 45.f;
	for (int32 i = 0; i < 3; ++i)
	{
		FElysiumBlendCell C;
		C.Axis[0] = i;
		Aim.Cells.Add(C);
	}
	const FElysiumPoseParamDesc* AimYaw = Table.Param(1);
	ElysiumBlendGrids::ResolveAxis(Aim, 0, AimYaw, 0.f, Cell, Fraction);
	TestEqual(TEXT("a level aim is the centre cell"), Cell, 1);
	ElysiumBlendGrids::ResolveAxis(Aim, 0, AimYaw, 400.f, Cell, Fraction);
	TestEqual(TEXT("a non-looping parameter clamps rather than wrapping"), Cell, 2);

	// Selection: the neutral pose picks the forward walk out of the fan.
	const FElysiumBlendPick Pick = ElysiumBlendGrids::SelectCell(Walk, Table,
		FElysiumPoseParams::Neutral());
	if (TestNotNull(TEXT("the neutral pose selects a cell"), Pick.Cell))
	{
		TestEqual(TEXT("...and it is the forward walk, not the base cell"), Pick.Cell->Clip,
			FString(TEXT("walk_0")));
		TestTrue(TEXT("...and carries that cell's authored route speed"),
			FMath::IsNearlyEqual(Pick.Cell->Motion.GroundSpeedCmPerSecond, 136.7f, 0.01f));
	}

	// A steered pose selects a different cell — the property the resolved-name cache key exists for.
	FElysiumPoseParams Strafing;
	Strafing.Set(TEXT("move_yaw"), 90.f);
	const FElysiumBlendPick Sideways = ElysiumBlendGrids::SelectCell(Walk, Table, Strafing);
	if (TestNotNull(TEXT("a steered pose selects a cell"), Sideways.Cell))
	{
		TestEqual(TEXT("...the 90 degree one"), Sideways.Cell->Clip, FString(TEXT("walk_90")));
	}

	// A cell whose animation never baked is skipped rather than played as nothing.
	FElysiumBlendGrid Holed = Walk;
	Holed.Cells[4].Clip.Reset();
	const FElysiumBlendPick Repaired = ElysiumBlendGrids::SelectCell(Holed, Table,
		FElysiumPoseParams::Neutral());
	if (TestNotNull(TEXT("a hole in the fan still resolves"), Repaired.Cell))
	{
		TestTrue(TEXT("...to a neighbour that actually baked"), !Repaired.Cell->Clip.IsEmpty());
	}

	// The sidecar parse, including the null cell the schema permits and the single-cell grid the
	// exporter never writes.
	const FString Json = TEXT(R"({"stem":"t","model":"m",)")
		TEXT(R"("pose_parameters":[{"index":0,"name":"move_yaw","flags":1,)")
		TEXT(R"("start":-180.0,"end":180.0,"loop":360.0}],)")
		TEXT(R"("grids":{"walk":{"numblends":3,"groupsize":[3,1],"paramindex":[0,-1],)")
		TEXT(R"("paramstart":[-180.0,0.0],"paramend":[180.0,0.0],"cells":[)")
		TEXT(R"({"axis":[0,0],"anim":20,"clip":"walk_180","motion":)")
		TEXT(R"({"cycle_seconds":1.2,"ground_distance_cm":164.0,)")
		TEXT(R"("ground_speed_cm_s":136.7}},)")
		TEXT(R"({"axis":[1,0],"anim":16,"clip":null},)")
		TEXT(R"({"axis":[2,0],"anim":17,"clip":"walk_45"}]},)")
		TEXT(R"("lonely":{"numblends":1,"groupsize":[1,1],"paramindex":[-1,-1],)")
		TEXT(R"("paramstart":[0.0,0.0],"paramend":[0.0,0.0],)")
		TEXT(R"("cells":[{"axis":[0,0],"anim":1,"clip":"x"}]}}})");
	FElysiumBlendTable Parsed;
	FString Error;
	TestTrue(TEXT("the sidecar parses"), Parsed.LoadJsonText(Json, Error));
	TestEqual(TEXT("the pose parameter comes through"), Parsed.PoseParams.Num(), 1);
	TestNotNull(TEXT("the multi-cell grid is kept"), Parsed.Find(TEXT("walk")));
	TestNull(TEXT("a single-cell grid is not a blend space"), Parsed.Find(TEXT("lonely")));
	if (const FElysiumBlendGrid* Grid = Parsed.Find(TEXT("walk")))
	{
		TestEqual(TEXT("every cell is read, null included"), Grid->Cells.Num(), 3);
		const FElysiumBlendCell* Authored = Grid->CellAt(0, 0);
		if (TestNotNull(TEXT("the authored-motion cell exists"), Authored))
		{
			TestTrue(TEXT("...and its optional ground speed is read"),
				FMath::IsNearlyEqual(Authored->Motion.GroundSpeedCmPerSecond, 136.7f, 0.01f));
		}
		const FElysiumBlendCell* Null = Grid->CellAt(1, 0);
		if (TestNotNull(TEXT("the null cell exists"), Null))
		{
			TestTrue(TEXT("...and addresses no animation"), Null->Clip.IsEmpty());
			TestFalse(TEXT("...and an old/motionless cell invents no route speed"),
				Null->Motion.IsUsable());
		}
		// Case-insensitive, like every other label lookup in the module.
		TestNotNull(TEXT("labels resolve case-insensitively"), Parsed.Find(TEXT("WALK")));
	}

	// --- The speed fan (CCC7): the same grid read as per-direction speed rather than as clips ------
	// `MakeYawTable`'s fan authors motion on cell 4 alone, which is the hole case by construction: a
	// body that walked at 136.7 cm/s forward and at nothing in every other direction would stand
	// still the moment it strafed.
	FElysiumGaitSpeedTable Sparse;
	if (TestTrue(TEXT("a fan with one authored cell still yields a table"),
			ElysiumBlendGrids::SpeedFan(Walk, Table, 1.0f, Sparse)))
	{
		TestEqual(TEXT("...answering that cell where it was authored"), Sparse.Forward(), 136.7f, 0.01f);
		TestEqual(TEXT("...and filling every hole from it rather than with zero"),
			Sparse.SpeedAt(90.0f), 136.7f, 0.01f);
		TestEqual(TEXT("...including across the wrap seam"), Sparse.SpeedAt(180.0f), 136.7f, 0.01f);
	}

	// A fully authored fan, which is what a real sidecar carries. The hole fill must not touch it.
	FElysiumBlendGrid Full = Walk;
	const float Authored[9] = { 88.6f, 113.9f, 97.1f, 88.1f, 136.7f, 88.1f, 60.7f, 113.9f, 88.6f };
	for (int32 i = 0; i < 9; ++i)
	{
		Full.Cells[i].Motion.CycleSeconds = 1.0f;
		Full.Cells[i].Motion.GroundDistanceCm = Authored[i];
		Full.Cells[i].Motion.GroundSpeedCmPerSecond = Authored[i];
	}
	FElysiumGaitSpeedTable Fan;
	if (TestTrue(TEXT("a fully authored fan yields a table"),
			ElysiumBlendGrids::SpeedFan(Full, Table, 2.3f, Fan)))
	{
		TestEqual(TEXT("...cell by cell"), Fan.SpeedAt(-90.0f), 97.1f * 2.3f, 0.01f);
		TestEqual(TEXT("...with the gait's own scale applied"), Fan.Forward(), 136.7f * 2.3f, 0.01f);
		TestEqual(TEXT("...and the peak is the largest cell scaled"), Fan.Peak(), 136.7f * 2.3f, 0.01f);
	}

	// One hole in an otherwise authored fan interpolates from its two neighbours, not from the whole.
	FElysiumBlendGrid OneHole = Full;
	OneHole.Cells[5].Motion = FElysiumClipMotion();
	FElysiumGaitSpeedTable Patched;
	if (TestTrue(TEXT("one hole does not refuse the fan"),
			ElysiumBlendGrids::SpeedFan(OneHole, Table, 1.0f, Patched)))
	{
		TestEqual(TEXT("...it is filled from the cells either side"), Patched.SpeedAt(45.0f),
			0.5f * (136.7f + 60.7f), 0.01f);
		TestEqual(TEXT("...and its neighbours are untouched"), Patched.Forward(), 136.7f, 0.01f);
	}

	// The refusals. Each of these would otherwise produce a speed that is wrong rather than absent.
	FElysiumGaitSpeedTable Refused;
	FElysiumBlendGrid Motionless = Walk;
	for (FElysiumBlendCell& Blank : Motionless.Cells)
	{
		Blank.Motion = FElysiumClipMotion();
	}
	TestFalse(TEXT("a fan with no authored motion is refused"),
		ElysiumBlendGrids::SpeedFan(Motionless, Table, 1.0f, Refused));
	TestFalse(TEXT("a non-wrapping axis is not a gait fan"),
		ElysiumBlendGrids::SpeedFan(Aim, Table, 1.0f, Refused));
	FElysiumBlendGrid Sliced = Full;
	Sliced.ParamEnd[0] = 90.f;
	TestFalse(TEXT("a fan spanning part of its parameter is refused"),
		ElysiumBlendGrids::SpeedFan(Sliced, Table, 1.0f, Refused));

	return true;
}


} // namespace ElysiumWorldEffectsTests

#endif // WITH_DEV_AUTOMATION_TESTS
