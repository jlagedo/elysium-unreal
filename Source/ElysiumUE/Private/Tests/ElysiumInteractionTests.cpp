// Content-free Substrate automation: world services, focused use sessions, filters, switches, doors, elevators, and attachment.
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
namespace ElysiumInteractionTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// A test-only leaf for the interaction world's captured-session rules. Production classes opt
// into these same virtuals when terminals/containers land; keeping this leaf here proves the
// foundation without prematurely making one of those content classes actionable.
class FElysiumTestUseSessionEntity final : public FElysiumEntity
{
public:
	int32 BeginCount = 0;
	int32 EndCount = 0;
	int32 EnterCount = 0;
	int32 LeaveCount = 0;
	EElysiumUseEndReason LastEndReason = EElysiumUseEndReason::Cancelled;

	static int32 TeardownEndCount;

	virtual bool IsUsable() const override { return true; }
	virtual FElysiumUseBeginResult BeginPlayerUse(const FElysiumUseContext&) override
	{
		++BeginCount;
		return FElysiumUseBeginResult::Started(
			TargetName == TEXT("explicit")
				? EElysiumUseSessionKind::Explicit : EElysiumUseSessionKind::WhileHeld);
	}
	virtual void EndPlayerUse(const FElysiumUseContext&, EElysiumUseEndReason Reason) override
	{
		++EndCount;
		LastEndReason = Reason;
		if (Reason == EElysiumUseEndReason::WorldTeardown)
		{
			++TeardownEndCount;
		}
	}
	virtual void OnUseCursorEnter() override { ++EnterCount; }
	virtual void OnUseCursorLeave() override { ++LeaveCount; }
};

int32 FElysiumTestUseSessionEntity::TeardownEndCount = 0;

static TUniquePtr<FElysiumEntity> MakeTestUseSessionEntity()
{
	return MakeUnique<FElysiumTestUseSessionEntity>();
}

static FElysiumClassRegistrar GTestUseSessionRegistrar(
	TEXT("test_use_session"), ElysiumBaseClassName(), &MakeTestUseSessionEntity,
	[](FElysiumClassDesc&) {});
// =====================================================================================
// FElysiumWorldServices (11.2) — the substrate's outbound seam. Runs the shape of the
// tutorial's own logic_auto chain end to end against the recording stub: no RHI, no actors,
// no `$ELYSIUM_EXPORT_ROOT`. sp_tutorial_1's five logic_autos fire OnMapLoad at an NPC (WillTalk), a
// door (Lock), a math_counter and a delayed wire; this reproduces that shape and adds one
// entity per entity-addressed service; all five seams are exercised by the same world activation.
//
// The second half is the contract that makes the first half meaningful: the SAME defs on a
// world with NO services must reach the SAME logical state. Embodiment, audio, travel,
// presentation, and weather are outputs of the logic, never inputs to it.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWorldServicesTest, "Elysium.Substrate.WorldServices", GElysiumTestFlags)
bool FElysiumWorldServicesTest::RunTest(const FString&)
{
	// One map's worth of defs, built twice (Load consumes them).
	auto BuildDefs = []()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__test__");

		auto Wire = [](FElysiumEntityDef& On, const TCHAR* Output, const TCHAR* Target,
			const TCHAR* Input, const TCHAR* Param, float Delay)
		{
			FElysiumOutputDef W;
			W.Name = Output;
			W.Target = Target;
			W.Input = Input;
			W.Param = Param;
			W.Delay = Delay;
			W.Times = -1;
			On.Outputs.Add(W);
		};

		// The ignition, shaped like the tutorial's own: an NPC latch, an NPC animation, a door
		// lock, an ambient sound, a screen fade, and one delayed counter wire.
		FElysiumEntityDef Auto;
		Auto.Classname = TEXT("logic_auto");
		Wire(Auto, TEXT("OnMapLoad"), TEXT("Jack"),      TEXT("WillTalk"),     TEXT("0"),          0.0f);
		Wire(Auto, TEXT("OnMapLoad"), TEXT("Jack"),      TEXT("SetAnimation"), TEXT("cower_idle"), 0.0f);
		Wire(Auto, TEXT("OnMapLoad"), TEXT("frontdoor"), TEXT("Lock"),         TEXT(""),           0.0f);
		Wire(Auto, TEXT("OnMapLoad"), TEXT("amb1"),      TEXT("PlaySound"),    TEXT(""),           0.0f);
		Wire(Auto, TEXT("OnMapLoad"), TEXT("fade1"),     TEXT("Fade"),         TEXT(""),           0.0f);
		Wire(Auto, TEXT("OnMapLoad"), TEXT("counter1"),  TEXT("Add"),          TEXT("5"),          0.1f);
		Defs.Defs.Add(MoveTemp(Auto));

		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("Jack");
		Npc.Origin = FVector(100.f, 200.f, 300.f);
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
		Npc.Keys.Add(TEXT("default_disposition"), TEXT("Neutral"));
		Defs.Defs.Add(MoveTemp(Npc));

		// A door with no hulls: the class runs, no brush body is built (there is no owner actor
		// to build one on either), and `Lock` still lands.
		FElysiumEntityDef Door;
		Door.Classname = TEXT("func_door");
		Door.TargetName = TEXT("frontdoor");
		Defs.Defs.Add(MoveTemp(Door));

		FElysiumEntityDef Amb;
		Amb.Classname = TEXT("ambient_generic");
		Amb.TargetName = TEXT("amb1");
		Amb.Keys.Add(TEXT("message"), TEXT("ambient\\tutorial\\hum.wav"));
		Amb.Keys.Add(TEXT("health"), TEXT("8"));          // VtMB VOLUME 0-10 -> 0.8 linear
		Defs.Defs.Add(MoveTemp(Amb));

		// start_enabled: the scheme fades in from the entity's own Spawn(), which is why the
		// audio service has to be live before the spawn pass rather than after it.
		FElysiumEntityDef Scheme;
		Scheme.Classname = TEXT("ambient_soundscheme");
		Scheme.TargetName = TEXT("scheme1");
		Scheme.Keys.Add(TEXT("scheme_file"), TEXT("sound/Schemes/Tutorial.txt"));
		Scheme.Keys.Add(TEXT("start_enabled"), TEXT("1"));
		Defs.Defs.Add(MoveTemp(Scheme));

		// SF_FADE_IN (1): the colour sits flat at MaxAlpha instead of ramping, so the fade is
		// already visible at its own start time — which is what makes it assertable against a
		// clock that never advances (a bare world has no game state, so `now` is always 0).
		FElysiumEntityDef Fade;
		Fade.Classname = TEXT("env_fade");
		Fade.TargetName = TEXT("fade1");
		Fade.Keys.Add(TEXT("duration"), TEXT("2.5"));
		Fade.Keys.Add(TEXT("holdtime"), TEXT("1.5"));
		Fade.Keys.Add(TEXT("spawnflags"), TEXT("1"));
		Wire(Fade, TEXT("OnBeginFade"), TEXT("counter1"), TEXT("Add"), TEXT("3"), 0.0f);
		Defs.Defs.Add(MoveTemp(Fade));

		FElysiumEntityDef Change;
		Change.Classname = TEXT("trigger_changelevel");
		Change.TargetName = TEXT("toalley");
		Change.Keys.Add(TEXT("map"), TEXT("la_hub_1"));
		Change.Keys.Add(TEXT("landmark"), TEXT("lm_alley"));
		Defs.Defs.Add(MoveTemp(Change));

		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Defs.Defs.Add(MoveTemp(Counter));

		return Defs;
	};

	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		for (const TPair<FString, FString>& Row : State)
		{
			if (Row.Key == TEXT("Value"))
			{
				return FCString::Atof(*Row.Value);
			}
		}
		return -1.f;
	};

	// --- With services: the chain reaches all five seams ---------------------------------
	FElysiumRecordingServices Rec;
	Rec.bHasPlayer = true;
	Rec.PlayerLocation = FVector(1000.f, 0.f, 0.f);
	Rec.PlayerRotation = FRotator(0.f, 90.f, 0.f);

	{
		FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr, Rec.Bundle());
		World.Load(BuildDefs());
		// A played map has a player entity (11.4) — the map actor creates one after Load, and the
		// travel seam below reads its placement, so the test builds the same shape.
		World.SpawnPlayer();
		World.Activate(0.0);

		// Spawn-time reaches: the NPC stood a body, the start_enabled scheme faded in.
		TestTrue(TEXT("the NPC stood a body through the embodiment"),
			Rec.Saw(TEXT("BuildNpcVisual jack")));
		TestTrue(TEXT("start_enabled ambient_soundscheme faded in at spawn"),
			Rec.Saw(TEXT("FadeInScheme sound/Schemes/Tutorial.txt")));
		TestEqual(TEXT("the ambient_soundscheme reports itself active"),
			Rec.ActiveSchemeRel(), FString(TEXT("sound/Schemes/Tutorial.txt")));

		// logic_auto ignites on the first tick; the 0.1 s wire lands on a later one.
		World.Tick(0.0);
		for (int32 i = 0; i < 4; ++i)
		{
			World.Tick(0.2);
		}

		FElysiumEntity* Counter = World.FindByName(TEXT("counter1"));
		if (!TestNotNull(TEXT("counter1 resolved"), Counter))
		{
			return false;
		}
		// 5 from the delayed OnMapLoad wire + 3 from env_fade's own OnBeginFade.
		TestEqual(TEXT("the delayed OnMapLoad wire and the fade's own output both drained"),
			CounterValue(Counter), 8.f);

		TestTrue(TEXT("SetAnimation reached the embodiment"),
			Rec.Saw(TEXT("PlayNpcClip jack cower_idle")));
		TestTrue(TEXT("ambient_generic played a voice through the audio service"),
			Rec.Saw(TEXT("Submit ambient/tutorial/hum.wav owner=entity:")));
		if (FElysiumEntity* Jack = World.FindByName(TEXT("jack")))
		{
			World.EnqueueInput(TEXT("!self"), FName(TEXT("PlayDialogFile")),
				FElysiumVariant::String(TEXT("sound/character/dlg/jack/direct_line")),
				0.0, FElysiumEntityHandle::Invalid(), Jack->Handle);
			World.Tick(0.3);
			TestTrue(TEXT("PlayDialogFile enters the owned line-service request path"),
				Rec.Saw(TEXT("Submit character/dlg/jack/direct_line.mp3 owner=direct:")));
		}
		World.EnqueueInput(TEXT("!self"), FName(TEXT("Whisper")),
			FElysiumVariant::String(TEXT("Crying")), 0.0,
			FElysiumEntityHandle::Invalid(), World.PlayerHandle());
		World.Tick(0.4);
		TestTrue(TEXT("player Whisper enters the typed request path"),
			Rec.Saw(TEXT("Submit whispers/crying")));
		TestTrue(TEXT("env_fade announced the fade to the presenter"),
			Rec.Saw(TEXT("StartFade dur=2.50 hold=1.50")));

		// The travel seam: a forced ChangeLevel captures the placement of the
		// player entity — sampled off the body at the top of the frame (11.4) — and asks the travel
		// service for the transition. This map has no source landmark, so the offset stays zero —
		// the warning path — and the yaw is the one the body reported.
		FElysiumEntity* Change = World.FindByName(TEXT("toalley"));
		if (TestNotNull(TEXT("toalley resolved"), Change))
		{
			AddExpectedError(TEXT("source landmark"), EAutomationExpectedErrorFlags::Contains, 0);
			World.EnqueueInput(TEXT("!self"), FName(TEXT("ChangeLevel")), FElysiumVariant::Void(), 0.0,
				FElysiumEntityHandle::Invalid(), Change->Handle);
			World.Tick(1.0);
			TestTrue(TEXT("trigger_changelevel asked the travel service for the transition"),
				Rec.Saw(TEXT("RequestLandmarkTravel la_hub_1@lm_alley")));
			TestTrue(TEXT("the transition carried the player's yaw"), Rec.Log().Contains(TEXT("yaw=90.0")));
		}
	}

	// Every service call the run made, so a failure message is worth reading.
	AddInfo(FString::Printf(TEXT("service calls: %s"), *Rec.Log()));

	// --- Without services: the same logic, the same state --------------------------------
	// A default bundle is four null pointers. Nothing may crash, and the logic layer must land
	// exactly where it did above — that is the whole claim of the seam, and it is also the
	// `elysium.NpcBodies 0` / `elysium.BrushBodies 0` A/B path the game already ships.
	{
		FElysiumEntityWorld Bare(/*Owner*/ nullptr, /*GameState*/ nullptr);
		Bare.Load(BuildDefs());
		Bare.Activate(0.0);

		TestNull(TEXT("a bare world has no embodiment"), Bare.Embodiment());
		TestNull(TEXT("a bare world has no audio"), Bare.Audio());
		TestNull(TEXT("a bare world has no travel"), Bare.Travel());
		TestNull(TEXT("a bare world has no presenter"), Bare.Presenter());

		Bare.Tick(0.0);
		for (int32 i = 0; i < 4; ++i)
		{
			Bare.Tick(0.2);
		}

		FElysiumEntity* Counter = Bare.FindByName(TEXT("counter1"));
		if (TestNotNull(TEXT("counter1 resolved without services"), Counter))
		{
			TestEqual(TEXT("the chain reaches the same state with no services at all"),
				CounterValue(Counter), 8.f);
		}
		// The fade is still world state, held for AElysiumHUD to poll, whether or not a presenter
		// heard about it — 11.8 is what moves that state onto the published view state.
		FLinearColor Faded;
		TestTrue(TEXT("the screen fade is still world state, not presenter state"),
			Bare.GetScreenFade(Faded));

		// Player interaction with no embodiment: no query result means no focus, and a queued press
		// is a safe no-op rather than a null deref.
		Bare.UpdatePlayerInteraction();
		TestFalse(TEXT("no embodiment means no use cursor"), Bare.GetAimedUsable().IsSet());
		Bare.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
		Bare.UpdatePlayerInteraction();
	}

	return true;
}

// =====================================================================================
// Modern +use — deterministic selection order and the world-owned focus/session lifecycle.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInteractionLifecycleTest,
	"Elysium.Substrate.InteractionLifecycle", GElysiumTestFlags)
bool FElysiumInteractionLifecycleTest::RunTest(const FString&)
{
	// Candidate scoring is total and stable. Exact beats assistance; hysteresis affects cone
	// admission only, and assisted candidates still resolve by aim/depth/handle.
	TArray<FElysiumUseCandidate> Scored;
	auto Candidate = [](int32 Index, EElysiumUseSelection Selection, float Aim, float Depth,
		bool bHysteresis = false)
	{
		FElysiumUseCandidate C;
		C.Owner = FElysiumEntityHandle(Index, 7);
		C.Selection = Selection;
		C.AimError = Aim;
		C.CameraDepth = Depth;
		C.bHysteresis = bHysteresis;
		return C;
	};
	Scored.Add(Candidate(3, EElysiumUseSelection::Assisted, 0.01f, 10.0f, true));
	Scored.Add(Candidate(2, EElysiumUseSelection::Exact, 1.0f, 100.0f));
	Scored.Add(Candidate(1, EElysiumUseSelection::Assisted, 0.0f, 1.0f));
	ElysiumInteraction::SortCandidates(Scored);
	TestEqual(TEXT("an exact hit always wins"), Scored[0].Owner.Index, 2);
	TestEqual(TEXT("assisted angular error wins after cone admission"), Scored[1].Owner.Index, 1);
	Scored.Reset();
	Scored.Add(Candidate(9, EElysiumUseSelection::Assisted, 0.2f, 30.0f));
	Scored.Add(Candidate(8, EElysiumUseSelection::Assisted, 0.1f, 40.0f));
	Scored.Add(Candidate(7, EElysiumUseSelection::Assisted, 0.1f, 20.0f));
	Scored.Add(Candidate(6, EElysiumUseSelection::Assisted, 0.1f, 20.0f));
	ElysiumInteraction::SortCandidates(Scored);
	TestEqual(TEXT("ties finish on the stable entity handle"), Scored[0].Owner.Index, 6);

	auto SessionDefs = []()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__interaction__");
		for (const TCHAR* Name : { TEXT("hold"), TEXT("explicit") })
		{
			FElysiumEntityDef Def;
			Def.Classname = TEXT("test_use_session");
			Def.TargetName = Name;
			Def.Keys.Add(TEXT("use_icon"), Name == FString(TEXT("hold")) ? TEXT("5") : TEXT("9"));
			Defs.Defs.Add(MoveTemp(Def));
		}
		return Defs;
	};

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(SessionDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	auto* Hold = static_cast<FElysiumTestUseSessionEntity*>(World.FindByName(TEXT("hold")));
	auto* Explicit = static_cast<FElysiumTestUseSessionEntity*>(World.FindByName(TEXT("explicit")));
	if (!TestNotNull(TEXT("while-held test entity"), Hold)
		|| !TestNotNull(TEXT("explicit test entity"), Explicit))
	{
		return false;
	}

	FElysiumUseCandidate HoldHit = Candidate(
		Hold->Handle.Index, EElysiumUseSelection::Exact, 0.0f, 50.0f);
	HoldHit.Owner = Hold->Handle;
	HoldHit.AnchorPoint = FVector(50, 0, 0);
	Services.UseQuery.Candidates = { HoldHit };
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("focus enters once"), Hold->EnterCount, 1);
	TestEqual(TEXT("the exact entity becomes focus"), World.GetFocusedUsable(), Hold->Handle);
	TestFalse(TEXT("the prompt starts its fade at zero"), World.GetInteractionView().bVisible);
	World.Tick(0.05);
	const FElysiumInteractionView HalfFade = World.GetInteractionView();
	TestTrue(TEXT("the prompt is visible during fade-in"), HalfFade.bVisible);
	TestTrue(TEXT("the 0.10 second fade is halfway at 0.05"),
		FMath::IsNearlyEqual(HalfFade.PromptAlpha, 0.5f, 0.02f));
	TestTrue(TEXT("focused prompt is actionable before capture"), HalfFade.bActionable);

	World.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("one press begins one interaction"), Hold->BeginCount, 1);
	TestEqual(TEXT("captured while-held session reports started"),
		World.GetLastUseOutcome(), EElysiumUseOutcome::SessionStarted);
	TestFalse(TEXT("a captured session is not actionable a second time"),
		World.GetInteractionView().bActionable);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("held frames never repeat Begin"), Hold->BeginCount, 1);

	FElysiumUseCandidate ExplicitHit = Candidate(
		Explicit->Handle.Index, EElysiumUseSelection::Exact, 0.0f, 45.0f);
	ExplicitHit.Owner = Explicit->Handle;
	Services.UseQuery.Candidates = { ExplicitHit };
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("looking away fires leave"), Hold->LeaveCount, 1);
	TestEqual(TEXT("new target receives focus while old session remains captured"),
		Explicit->EnterCount, 1);
	World.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("another press while captured is busy"),
		World.GetLastUseOutcome(), EElysiumUseOutcome::Busy);
	TestEqual(TEXT("busy press never begins the new target"), Explicit->BeginCount, 0);

	World.QueuePlayerUseEdge(EElysiumUseEdge::Released);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("release routes to the captured while-held owner"), Hold->EndCount, 1);
	TestEqual(TEXT("release carries its reason"), Hold->LastEndReason, EElysiumUseEndReason::Released);
	World.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("explicit session begins after capture clears"), Explicit->BeginCount, 1);
	TestFalse(TEXT("an explicit modal session suppresses the world prompt"),
		World.GetInteractionView().bVisible);

	Services.UseQuery = FElysiumUseQueryResult();
	World.UpdatePlayerInteraction();
	TestFalse(TEXT("looking away clears focus"), World.GetFocusedUsable().IsSet());
	TestEqual(TEXT("looking away does not end an explicit session"), Explicit->EndCount, 0);
	World.QueuePlayerUseEdge(EElysiumUseEdge::Released);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("release does not end an explicit session"), Explicit->EndCount, 0);
	TestTrue(TEXT("the explicit leaf/UI can complete its captured session"),
		World.EndPlayerUseSession(Explicit->Handle, EElysiumUseEndReason::Completed));
	TestEqual(TEXT("explicit completion reaches the captured owner"), Explicit->EndCount, 1);
	TestEqual(TEXT("explicit completion carries its reason"),
		Explicit->LastEndReason, EElysiumUseEndReason::Completed);
	TestEqual(TEXT("explicit completion reports completed"),
		World.GetLastUseOutcome(), EElysiumUseOutcome::Completed);

	Services.UseQuery.Candidates = { ExplicitHit };
	World.UpdatePlayerInteraction();
	World.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("the explicit owner can start another session"), Explicit->BeginCount, 2);
	World.AcceptInput(TEXT("explicit"), FName(TEXT("ScriptHide")), FElysiumVariant::Void(),
		Explicit->Handle, Explicit->Handle);
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("an inert captured owner is cancelled"), Explicit->EndCount, 2);
	TestEqual(TEXT("target invalidation carries its reason"),
		Explicit->LastEndReason, EElysiumUseEndReason::TargetInvalid);
	World.Tick(0.125);
	TestTrue(TEXT("the 0.15 second fade-out is halfway after 0.075 seconds"),
		FMath::IsNearlyEqual(World.GetInteractionView().PromptAlpha, 0.25f, 0.02f));
	World.Tick(0.20);
	TestFalse(TEXT("the retained prompt is gone after its fade-out"),
		World.GetInteractionView().bVisible);

	const FElysiumEntityHandle Stale = Hold->Handle;
	World.AcceptInput(TEXT("hold"), FName(TEXT("Kill")), FElysiumVariant::Void(),
		Hold->Handle, Hold->Handle);
	FElysiumUseCandidate StaleHit = Candidate(
		Stale.Index, EElysiumUseSelection::Exact, 0.0f, 20.0f);
	StaleHit.Owner = Stale;
	Services.UseQuery.Candidates = { StaleHit };
	World.UpdatePlayerInteraction();
	TestFalse(TEXT("a stale handle cannot become focus"), World.GetFocusedUsable().IsSet());

	const int32 TeardownsBefore = FElysiumTestUseSessionEntity::TeardownEndCount;
	{
		FElysiumRecordingServices TeardownServices;
		FElysiumEntityWorld TeardownWorld(nullptr, nullptr, TeardownServices.Bundle());
		TeardownWorld.Load(SessionDefs());
		TeardownWorld.SpawnPlayer();
		TeardownWorld.Activate(0.0);
		FElysiumEntity* TeardownHold = TeardownWorld.FindByName(TEXT("hold"));
		FElysiumUseCandidate Hit = Candidate(
			TeardownHold->Handle.Index, EElysiumUseSelection::Exact, 0.0f, 10.0f);
		Hit.Owner = TeardownHold->Handle;
		TeardownServices.UseQuery.Candidates = { Hit };
		TeardownWorld.UpdatePlayerInteraction();
		TeardownWorld.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
		TeardownWorld.UpdatePlayerInteraction();
	}
	TestEqual(TEXT("map teardown cancels a captured session"),
		FElysiumTestUseSessionEntity::TeardownEndCount, TeardownsBefore + 1);

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUseFilterTest,
	"Elysium.Substrate.UseFilter", GElysiumTestFlags)
bool FElysiumUseFilterTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__use_filter__");
	auto AddFilter = [&Defs](const TCHAR* Name, const TCHAR* Pattern)
	{
		FElysiumEntityDef Filter;
		Filter.Classname = TEXT("filter_activator_name");
		Filter.TargetName = Name;
		Filter.Keys.Add(TEXT("filtername"), Pattern);
		Defs.Defs.Add(MoveTemp(Filter));
	};
	AddFilter(TEXT("accept_player"), TEXT("!pla*"));
	AddFilter(TEXT("reject_player"), TEXT("somebody_else"));

	auto AddUsable = [&Defs](const TCHAR* Name, const TCHAR* Filter)
	{
		FElysiumEntityDef Use;
		Use.Classname = TEXT("test_use_session");
		Use.TargetName = Name;
		Use.Keys.Add(TEXT("use_filter_name"), Filter);
		Defs.Defs.Add(MoveTemp(Use));
	};
	AddUsable(TEXT("allowed"), TEXT("accept_player"));
	AddUsable(TEXT("rejected"), TEXT("reject_player"));
	AddUsable(TEXT("missing"), TEXT("filter_that_does_not_exist"));

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	World.Activate(0.0);

	auto Press = [&World, &Services](FElysiumEntity& Entity)
	{
		FElysiumUseCandidate Hit;
		Hit.Owner = Entity.Handle;
		Hit.Selection = EElysiumUseSelection::Exact;
		Services.UseQuery.Candidates = { Hit };
		World.UpdatePlayerInteraction();
		World.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
		World.UpdatePlayerInteraction();
	};

	auto* Allowed = static_cast<FElysiumTestUseSessionEntity*>(World.FindByName(TEXT("allowed")));
	auto* Rejected = static_cast<FElysiumTestUseSessionEntity*>(World.FindByName(TEXT("rejected")));
	auto* Missing = static_cast<FElysiumTestUseSessionEntity*>(World.FindByName(TEXT("missing")));
	if (!TestNotNull(TEXT("allowed use target"), Allowed)
		|| !TestNotNull(TEXT("rejected use target"), Rejected)
		|| !TestNotNull(TEXT("missing-filter use target"), Missing))
	{
		return false;
	}

	Press(*Allowed);
	TestEqual(TEXT("a passing use filter begins the target"), Allowed->BeginCount, 1);
	World.QueuePlayerUseEdge(EElysiumUseEdge::Released);
	World.UpdatePlayerInteraction();

	Press(*Rejected);
	TestEqual(TEXT("a failed use filter blocks before BeginPlayerUse"), Rejected->BeginCount, 0);
	TestEqual(TEXT("a failed use filter reports unavailable"), World.GetLastUseOutcome(),
		EElysiumUseOutcome::Unavailable);

	AddExpectedError(TEXT("use_filter_name 'filter_that_does_not_exist' did not resolve; allowing use"),
		EAutomationExpectedErrorFlags::Contains, 1);
	Press(*Missing);
	TestEqual(TEXT("an unresolved use filter remains non-blocking"), Missing->BeginCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropSwitchUseTest,
	"Elysium.Substrate.PropSwitchUse", GElysiumTestFlags)
bool FElysiumPropSwitchUseTest::RunTest(const FString&)
{
	auto Counter = [](const TCHAR* Name)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("math_counter");
		Def.TargetName = Name;
		return Def;
	};
	auto Wire = [](FElysiumEntityDef& Def, const TCHAR* Output, const TCHAR* Target)
	{
		FElysiumOutputDef Row;
		Row.Name = Output;
		Row.Target = Target;
		Row.Input = TEXT("Add");
		Row.Param = TEXT("1");
		Def.Outputs.Add(MoveTemp(Row));
	};
	auto ReadCounter = [](const FElysiumEntity* Entity)
	{
		TArray<TPair<FString, FString>> State;
		if (Entity) { Entity->GetDebugState(State); }
		const TPair<FString, FString>* Value = State.FindByPredicate(
			[](const TPair<FString, FString>& Row) { return Row.Key == TEXT("Value"); });
		return Value ? FCString::Atof(*Value->Value) : -1.0f;
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__prop_switch_use__");
	FElysiumEntityDef Switch;
	Switch.Classname = TEXT("prop_switch");
	Switch.TargetName = TEXT("switch");
	Switch.ModelMesh = TEXT("switch_static");
	Switch.Keys.Add(TEXT("model"), TEXT("models/test/switch.mdl"));
	Switch.Keys.Add(TEXT("spawnflags"), TEXT("8192")); // starts activated
	Switch.Keys.Add(TEXT("use_icon"), TEXT("7"));
	Switch.Keys.Add(TEXT("locked_icon"), TEXT("8"));
	Wire(Switch, TEXT("OnUse"), TEXT("used"));
	Wire(Switch, TEXT("OnLockedUse"), TEXT("locked"));
	Wire(Switch, TEXT("OnActivate"), TEXT("activated"));
	Wire(Switch, TEXT("OnDeactivate"), TEXT("deactivated"));
	Defs.Defs.Add(MoveTemp(Switch));
	for (const TCHAR* Name : { TEXT("used"), TEXT("locked"), TEXT("activated"), TEXT("deactivated") })
	{
		Defs.Defs.Add(Counter(Name));
	}

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.ClipSeconds = 1.0f;
	Services.AnimatedPropModels.Add(TEXT("models/test/switch.mdl"), TEXT("switch_anim"));
	Services.AnimatedPropRestClips.Add(TEXT("switch_anim"), TEXT("idle_off"));
	Services.AnimatedPropClipLoops.Add(TEXT("switch_anim|idle_on"), true);
	Services.AnimatedPropClipLoops.Add(TEXT("switch_anim|idle_off"), true);
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumEntity* Live = World.FindByName(TEXT("switch"));
	if (!TestNotNull(TEXT("prop_switch resolves"), Live))
	{
		return false;
	}
	TestEqual(TEXT("unlocked switch publishes its use icon"), Live->GetUseIcon(), 7);

	Live->Use(Player);
	World.Tick(0.0);
	TestEqual(TEXT("unlocked use fires OnUse immediately"),
		ReadCounter(World.FindByName(TEXT("used"))), 1.0f);
	TestEqual(TEXT("deactivation output waits for its clip"),
		ReadCounter(World.FindByName(TEXT("deactivated"))), 0.0f);
	World.Tick(0.99);
	TestEqual(TEXT("transition remains pending before authored duration"),
		ReadCounter(World.FindByName(TEXT("deactivated"))), 0.0f);
	World.Tick(1.0);
	TestEqual(TEXT("deactivation output fires when the clip finishes"),
		ReadCounter(World.FindByName(TEXT("deactivated"))), 1.0f);

	World.AcceptInput(TEXT("switch"), FName(TEXT("Lock")), FElysiumVariant::Void(), Player, Player);
	TestTrue(TEXT("Lock changes switch interaction state"), Live->IsUseLocked());
	TestEqual(TEXT("locked switch publishes its locked icon"), Live->GetUseIcon(), 8);
	Live->Use(Player);
	World.Tick(1.0);
	TestEqual(TEXT("locked use fires only OnLockedUse"),
		ReadCounter(World.FindByName(TEXT("locked"))), 1.0f);
	TestEqual(TEXT("locked use does not refire OnUse"),
		ReadCounter(World.FindByName(TEXT("used"))), 1.0f);

	World.AcceptInput(TEXT("switch"), FName(TEXT("Unlock")), FElysiumVariant::Void(), Player, Player);
	World.AcceptInput(TEXT("switch"), FName(TEXT("Activate")), FElysiumVariant::Void(), Player, Player);
	World.Tick(2.0);
	TestEqual(TEXT("Activate input shares the transition/output path"),
		ReadCounter(World.FindByName(TEXT("activated"))), 1.0f);
	return true;
}

// =====================================================================================
// Brush movers — visible body attachment, PASSABLE doors, use_override, prop_button,
// and the recovered func_elevator state machine exercised as one authored-style chain.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorElevatorTest,
	"Elysium.Substrate.DoorElevator", GElysiumTestFlags)
bool FElysiumDoorElevatorTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("does_not_exist.Use"), EAutomationExpectedErrorFlags::Contains, 1);
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* EngineWorld = TestWorld.GetTestWorld();
	AActor* Owner = EngineWorld ? EngineWorld->SpawnActor<AActor>() : nullptr;
	if (!TestNotNull(TEXT("mover owner spawned"), Owner))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("MoverRoot"));
	Owner->SetRootComponent(Root);
	Root->RegisterComponent();
	Owner->AddInstanceComponent(Root);

	auto BoxHull = []()
	{
		FElysiumConvexHull Hull;
		for (float X : { -20.f, 20.f })
		{
			for (float Y : { -20.f, 20.f })
			{
				for (float Z : { -20.f, 20.f })
				{
					Hull.Vertices.Emplace(X, Y, Z);
				}
			}
		}
		return Hull;
	};
	auto Wire = [](FElysiumEntityDef& From, const TCHAR* Output, const TCHAR* Target,
		const TCHAR* Input, const TCHAR* Param = TEXT(""))
	{
		FElysiumOutputDef W;
		W.Name = Output;
		W.Target = Target;
		W.Input = Input;
		W.Param = Param;
		W.Times = -1;
		From.Outputs.Add(MoveTemp(W));
	};
	auto CounterValue = [](const FElysiumEntity* Entity)
	{
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		for (const TPair<FString, FString>& Row : State)
		{
			if (Row.Key == TEXT("Value"))
			{
				return FCString::Atof(*Row.Value);
			}
		}
		return -1.f;
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__door_elevator__");
	auto AddCounter = [&Defs](const TCHAR* Name)
	{
		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = Name;
		Defs.Defs.Add(MoveTemp(Counter));
	};
	for (const TCHAR* Name : { TEXT("pressed"), TEXT("locked_press"), TEXT("state2"),
		TEXT("move_start"), TEXT("reach_any"), TEXT("reach1"), TEXT("reach2"),
		TEXT("invalid_open"), TEXT("case_default") })
	{
		AddCounter(Name);
	}

	// The tutorial resets this counter to zero on every elevator arrival and forwards OutValue to
	// logic_case_toggle.InValue. Retail InValue is value matching (not the added delta input), so
	// zero misses Case01/02 and terminates at OnDefault instead of recursively requesting a floor.
	FElysiumEntityDef TutorialCounter;
	TutorialCounter.Classname = TEXT("math_counter");
	TutorialCounter.TargetName = TEXT("counter_elev");
	Wire(TutorialCounter, TEXT("OutValue"), TEXT("case_elev"), TEXT("InValue"));
	Defs.Defs.Add(MoveTemp(TutorialCounter));

	FElysiumEntityDef TutorialCase;
	TutorialCase.Classname = TEXT("logic_case_toggle");
	TutorialCase.TargetName = TEXT("case_elev");
	TutorialCase.Keys.Add(TEXT("InitialCase"), TEXT("1"));
	TutorialCase.Keys.Add(TEXT("Case01"), TEXT("1"));
	TutorialCase.Keys.Add(TEXT("Case02"), TEXT("2"));
	Wire(TutorialCase, TEXT("OnCase01"), TEXT("lift"), TEXT("GotoFloor"), TEXT("1"));
	Wire(TutorialCase, TEXT("OnCase02"), TEXT("lift"), TEXT("GotoFloor"), TEXT("2"));
	Wire(TutorialCase, TEXT("OnDefault"), TEXT("case_default"), TEXT("Add"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(TutorialCase));

	FElysiumEntityDef Button;
	Button.Classname = TEXT("prop_button");
	Button.TargetName = TEXT("elev_button");
	Button.ModelMesh = TEXT("elevator_button");
	Button.Keys.Add(TEXT("model"), TEXT("models/elevator_button.mdl"));
	Button.Keys.Add(TEXT("parentname"), TEXT("lift"));
	Button.Keys.Add(TEXT("max_states"), TEXT("2"));
	Button.Keys.Add(TEXT("current_state"), TEXT("0"));
	Button.Keys.Add(TEXT("use_icon"), TEXT("7"));
	Button.Keys.Add(TEXT("locked_icon"), TEXT("8"));
	Wire(Button, TEXT("OnPressed"), TEXT("pressed"), TEXT("Add"), TEXT("1"));
	Wire(Button, TEXT("OnPressedLocked"), TEXT("locked_press"), TEXT("Add"), TEXT("1"));
	Wire(Button, TEXT("OnSetState2"), TEXT("state2"), TEXT("Add"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(Button));

	FElysiumEntityDef Elevator;
	Elevator.Classname = TEXT("func_elevator");
	Elevator.TargetName = TEXT("lift");
	Elevator.Model = 1;
	Elevator.Hulls.Add(BoxHull());
	Elevator.BrushMesh = TEXT("brush_1");
	Elevator.ElevatorFloors = { 0.f, 254.f };
	Elevator.Keys.Add(TEXT("model"), TEXT("*1"));
	Elevator.Keys.Add(TEXT("speed"), TEXT("100"));       // one second per 100 Source inches
	Elevator.Keys.Add(TEXT("numfloors"), TEXT("2"));
	Wire(Elevator, TEXT("OnMoveStart"), TEXT("move_start"), TEXT("Add"), TEXT("1"));
	Wire(Elevator, TEXT("OnReachFloorAny"), TEXT("reach_any"), TEXT("Add"), TEXT("1"));
	Wire(Elevator, TEXT("OnReachFloorAny"), TEXT("counter_elev"), TEXT("SetValue"), TEXT("0"));
	Wire(Elevator, TEXT("OnReachFloor1"), TEXT("reach1"), TEXT("Add"), TEXT("1"));
	Wire(Elevator, TEXT("OnReachFloor2"), TEXT("reach2"), TEXT("Add"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(Elevator));

	FElysiumEntityDef Door;
	Door.Classname = TEXT("func_door");
	Door.TargetName = TEXT("lift_door");
	Door.Origin = FVector(50.f, 0.f, 0.f);
	Door.Model = 2;
	Door.Hulls.Add(BoxHull());
	Door.BrushMesh = TEXT("brush_2");
	Door.Keys.Add(TEXT("model"), TEXT("*2"));
	Door.Keys.Add(TEXT("parentname"), TEXT("lift"));
	Door.Keys.Add(TEXT("spawnflags"), TEXT("8"));        // PASSABLE
	Door.Keys.Add(TEXT("use_override"), TEXT("elev_button"));
	Defs.Defs.Add(MoveTemp(Door));

	FElysiumEntityDef InvalidDoor;
	InvalidDoor.Classname = TEXT("func_door");
	InvalidDoor.TargetName = TEXT("invalid_override");
	InvalidDoor.Origin = FVector(100.f, 0.f, 0.f);
	InvalidDoor.Model = 3;
	InvalidDoor.Hulls.Add(BoxHull());
	InvalidDoor.BrushMesh = TEXT("brush_3");
	InvalidDoor.Keys.Add(TEXT("model"), TEXT("*3"));
	InvalidDoor.Keys.Add(TEXT("use_override"), TEXT("does_not_exist"));
	Wire(InvalidDoor, TEXT("OnOpen"), TEXT("invalid_open"), TEXT("Add"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(InvalidDoor));

	FElysiumEntityDefs RestoreDefs = Defs;
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* LiveButton = World.FindByName(TEXT("elev_button"));
	FElysiumEntity* LiveElevator = World.FindByName(TEXT("lift"));
	FElysiumEntity* LiveDoor = World.FindByName(TEXT("lift_door"));
	FElysiumEntity* LiveInvalid = World.FindByName(TEXT("invalid_override"));
	if (!TestNotNull(TEXT("prop_button resolved"), LiveButton)
		|| !TestNotNull(TEXT("elevator resolved"), LiveElevator)
		|| !TestNotNull(TEXT("door resolved"), LiveDoor)
		|| !TestNotNull(TEXT("invalid override door resolved"), LiveInvalid))
	{
		return false;
	}

	TestEqual(TEXT("every annotated brush requested one visual"),
		Services.Count(TEXT("BuildBrushVisual")), 3);
	TestTrue(TEXT("door visual is attached at identity to collision"),
		LiveDoor->Body && LiveDoor->Body->GetVisual()
		&& LiveDoor->Body->GetVisual()->GetAttachParent() == LiveDoor->Body
		&& LiveDoor->Body->GetVisual()->GetRelativeTransform().Equals(FTransform::Identity));
	TestEqual(TEXT("PASSABLE door keeps its dedicated traceable profile"),
		LiveDoor->Body->GetCollisionProfileName(), FName(TEXT("ElysiumBrushPassable")));
	TestTrue(TEXT("parentname attaches the door to the elevator body"),
		LiveDoor->Body->GetAttachParent() == LiveElevator->Body);
	TestTrue(TEXT("parentname attaches the cabin button visual to the elevator body"),
		LiveButton->GetAttachBody()
		&& LiveButton->GetAttachBody()->GetAttachParent() == LiveElevator->Body);
	TestTrue(TEXT("model-backed prop_button registers an enabled use anchor"),
		Services.UseAnchorEnabled.FindRef(LiveButton->Handle));
	TestTrue(TEXT("attachment preserves the exported world pose"),
		LiveDoor->Body->GetComponentLocation().Equals(FVector(50.f, 0.f, 0.f), 0.1f));

	World.AcceptInput(TEXT("lift_door"), FName(TEXT("Use")), FElysiumVariant::Void(),
		LiveDoor->Handle, LiveDoor->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("use_override delegates one normal Use"), CounterValue(World.FindByName(TEXT("pressed"))), 1.f);
	TestEqual(TEXT("button cycles skin/state after OnPressed"), CounterValue(World.FindByName(TEXT("state2"))), 1.f);
	World.AcceptInput(TEXT("elev_button"), FName(TEXT("Lock")), FElysiumVariant::Void(),
		LiveButton->Handle, LiveButton->Handle);
	World.AcceptInput(TEXT("lift_door"), FName(TEXT("Use")), FElysiumVariant::Void(),
		LiveDoor->Handle, LiveDoor->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("locked override emits only OnPressedLocked"),
		CounterValue(World.FindByName(TEXT("locked_press"))), 1.f);
	TestEqual(TEXT("locked use does not emit OnPressed again"),
		CounterValue(World.FindByName(TEXT("pressed"))), 1.f);
	TestEqual(TEXT("locked button exposes locked icon"), LiveButton->GetUseIcon(), 8);
	FElysiumUseCandidate LockedHit;
	LockedHit.Owner = LiveButton->Handle;
	LockedHit.Selection = EElysiumUseSelection::Exact;
	LockedHit.CameraDepth = 40.0f;
	Services.UseQuery.Candidates = { LockedHit };
	World.UpdatePlayerInteraction();
	const FElysiumInteractionView LockedView = World.GetInteractionView();
	TestEqual(TEXT("locked focus publishes its locked icon"), LockedView.Icon, 8);
	TestTrue(TEXT("locked focus publishes locked state"), LockedView.bLocked);
	TestTrue(TEXT("a locked control remains actionable so it can emit its denial"),
		LockedView.bActionable);

	const FVector InvalidStart = LiveInvalid->Body->GetRelativeLocation();
	World.AcceptInput(TEXT("invalid_override"), FName(TEXT("Use")), FElysiumVariant::Void(),
		LiveInvalid->Handle, LiveInvalid->Handle);
	World.Tick(0.0);
	TestTrue(TEXT("missing use_override target fails closed"),
		LiveInvalid->Body->GetRelativeLocation().Equals(InvalidStart));
	TestEqual(TEXT("missing use_override never opens the door"),
		CounterValue(World.FindByName(TEXT("invalid_open"))), 0.f);

	// Retail completes an already-current GotoFloor synchronously: Any first, then the floor row.
	World.AcceptInput(TEXT("lift"), FName(TEXT("GotoFloor")), FElysiumVariant::Int(1),
		LiveButton->Handle, LiveButton->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("current-floor request emits the generic arrival immediately"),
		CounterValue(World.FindByName(TEXT("reach_any"))), 1.f);
	TestEqual(TEXT("current-floor request emits floor-one arrival immediately"),
		CounterValue(World.FindByName(TEXT("reach1"))), 1.f);
	TestEqual(TEXT("current-floor request does not start movement"),
		CounterValue(World.FindByName(TEXT("move_start"))), 0.f);
	TestEqual(TEXT("tutorial arrival reset misses configured cases and terminates at OnDefault"),
		CounterValue(World.FindByName(TEXT("case_default"))), 1.f);

	AddExpectedError(TEXT("invalid floor"), EAutomationExpectedErrorFlags::Contains, 1);
	World.AcceptInput(TEXT("lift"), FName(TEXT("GotoFloor")), FElysiumVariant::Int(9),
		LiveButton->Handle, LiveButton->Handle);
	TestEqual(TEXT("invalid floor does not start movement"),
		CounterValue(World.FindByName(TEXT("move_start"))), 0.f);

	World.AcceptInput(TEXT("counter_elev"), FName(TEXT("SetValue")), FElysiumVariant::Int(2),
		LiveButton->Handle, LiveButton->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("tutorial SetValue(2) matches Case02 and starts the requested floor"),
		CounterValue(World.FindByName(TEXT("move_start"))), 1.f);
	World.Tick(0.5);
	TestTrue(TEXT("elevator advances at constant speed"),
		FMath::IsNearlyEqual(LiveElevator->Body->GetRelativeLocation().Z, 127.f, 1.f));
	TestTrue(TEXT("parented door stays aligned while elevator moves"),
		FMath::IsNearlyEqual(LiveDoor->Body->GetComponentLocation().Z, 127.f, 1.f));
	TestTrue(TEXT("parented cabin button stays aligned while elevator moves"),
		LiveButton->GetAttachBody()
		&& FMath::IsNearlyEqual(LiveButton->GetAttachBody()->GetComponentLocation().Z, 127.f, 1.f));
	FElysiumMapSnapshot MovingSnapshot;
	World.Freeze(MovingSnapshot);
	World.AcceptInput(TEXT("lift"), FName(TEXT("GotoFloor")), FElysiumVariant::Int(1),
		LiveButton->Handle, LiveButton->Handle); // ignored while moving
	World.Tick(1.1);
	TestTrue(TEXT("elevator reaches the requested absolute floor"),
		FMath::IsNearlyEqual(LiveElevator->Body->GetRelativeLocation().Z, 254.f, 0.1f));
	TestEqual(TEXT("arrival fires generic output once for the completed move"),
		CounterValue(World.FindByName(TEXT("reach_any"))), 2.f);
	TestEqual(TEXT("arrival fires floor-two output"),
		CounterValue(World.FindByName(TEXT("reach2"))), 1.f);
	TestEqual(TEXT("floor-two reset returns through the non-recursive default path"),
		CounterValue(World.FindByName(TEXT("case_default"))), 2.f);
	TestEqual(TEXT("mid-move retarget was ignored"),
		CounterValue(World.FindByName(TEXT("move_start"))), 1.f);

	// The mover save policy resolves an in-flight request at its destination, resting.
	AActor* RestoreOwner = EngineWorld->SpawnActor<AActor>();
	USceneComponent* RestoreRoot = NewObject<USceneComponent>(RestoreOwner, TEXT("RestoreRoot"));
	RestoreOwner->SetRootComponent(RestoreRoot);
	RestoreRoot->RegisterComponent();
	RestoreOwner->AddInstanceComponent(RestoreRoot);
	FElysiumRecordingServices RestoreServices;
	FElysiumEntityWorld Restored(RestoreOwner, nullptr, RestoreServices.Bundle());
	Restored.Load(MoveTemp(RestoreDefs));
	Restored.ApplySnapshot(MovingSnapshot);
	Restored.Activate(0.5);
	Restored.Tick(0.5);
	FElysiumEntity* RestoredElevator = Restored.FindByName(TEXT("lift"));
	TestTrue(TEXT("restored in-flight elevator seats at its requested destination"),
		RestoredElevator && RestoredElevator->Body
		&& FMath::IsNearlyEqual(RestoredElevator->Body->GetRelativeLocation().Z, 254.f, 0.1f));
	if (RestoredElevator)
	{
		TArray<TPair<FString, FString>> State;
		RestoredElevator->GetDebugState(State);
		const TPair<FString, FString>* Current = State.FindByPredicate(
			[](const TPair<FString, FString>& Row) { return Row.Key == TEXT("Current floor"); });
		const TPair<FString, FString>* Target = State.FindByPredicate(
			[](const TPair<FString, FString>& Row) { return Row.Key == TEXT("Target floor"); });
		TestTrue(TEXT("restored elevator is resting at floor two"),
			Current && Current->Value == TEXT("2")
			&& Target && Target->Value == TEXT("(none)"));
	}

	World.AcceptInput(TEXT("lift_door"), FName(TEXT("ScriptHide")), FElysiumVariant::Void(),
		LiveDoor->Handle, LiveDoor->Handle);
	TestEqual(TEXT("hidden mover drops collision"), LiveDoor->Body->GetCollisionEnabled(),
		ECollisionEnabled::NoCollision);
	TestFalse(TEXT("hidden mover hides its attached visual"), LiveDoor->Body->GetVisual()->IsVisible());
	World.AcceptInput(TEXT("lift_door"), FName(TEXT("ScriptUnhide")), FElysiumVariant::Void(),
		LiveDoor->Handle, LiveDoor->Handle);
	TestEqual(TEXT("unhidden PASSABLE mover restores its profile"),
		LiveDoor->Body->GetCollisionProfileName(), FName(TEXT("ElysiumBrushPassable")));
	TestTrue(TEXT("unhidden mover restores its attached visual"), LiveDoor->Body->GetVisual()->IsVisible());

	World.AcceptInput(TEXT("elev_button"), FName(TEXT("ScriptHide")), FElysiumVariant::Void(),
		LiveButton->Handle, LiveButton->Handle);
	TestFalse(TEXT("hidden prop_button removes its use anchor"),
		Services.UseAnchorEnabled.FindRef(LiveButton->Handle));
	World.UpdatePlayerInteraction();
	TestFalse(TEXT("a stale exact query cannot focus a hidden button"),
		World.GetFocusedUsable().IsSet());
	TestFalse(TEXT("hidden button has no actionable prompt"),
		World.GetInteractionView().bActionable);
	World.AcceptInput(TEXT("elev_button"), FName(TEXT("ScriptUnhide")), FElysiumVariant::Void(),
		LiveButton->Handle, LiveButton->Handle);
	TestTrue(TEXT("ScriptUnhide restores the prop_button use anchor"),
		Services.UseAnchorEnabled.FindRef(LiveButton->Handle));
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("ScriptUnhide makes the button focusable again"),
		World.GetFocusedUsable(), LiveButton->Handle);

	return true;
}

// =====================================================================================
// func_rotating + the character attach point.
//
// Two things a `parentname` needs that the substrate did not have: a character that can BE a
// parent (an ornament worn on an NPC, which survives the model swap a level script does), and the
// continuous spinner its own children ride. The rate is the assertion that matters — VtMB authors
// clock hands as `maxspeed` in degrees/second, so a second hand is 6 and a minute hand is 0.1.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRotatingAttachTest,
	"Elysium.Substrate.RotatingAttach", GElysiumTestFlags)
bool FElysiumRotatingAttachTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("resolved parent 'no_body', but its attachment body is unavailable"),
		EAutomationExpectedErrorFlags::Contains, 2);
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* EngineWorld = TestWorld.GetTestWorld();
	AActor* Owner = EngineWorld ? EngineWorld->SpawnActor<AActor>() : nullptr;
	if (!TestNotNull(TEXT("rotating owner spawned"), Owner))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("RotatingRoot"));
	Owner->SetRootComponent(Root);
	Root->RegisterComponent();
	Owner->AddInstanceComponent(Root);

	auto BoxHull = []()
	{
		FElysiumConvexHull Hull;
		for (float X : { -20.f, 20.f })
		{
			for (float Y : { -20.f, 20.f })
			{
				for (float Z : { -20.f, 20.f })
				{
					Hull.Vertices.Emplace(X, Y, Z);
				}
			}
		}
		return Hull;
	};
	auto DebugRow = [](const FElysiumEntity* Entity, const TCHAR* Key)
	{
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		const TPair<FString, FString>* Row = State.FindByPredicate(
			[Key](const TPair<FString, FString>& R) { return R.Key == Key; });
		return Row ? Row->Value : FString();
	};
	auto AngleOf = [&DebugRow](const FElysiumEntity* Entity)
	{
		return FCString::Atof(*DebugRow(Entity, TEXT("Angle")));
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__rotating_attach__");

	// The character an ornament hangs off — the sp_theatre shape (a level script SetModels these).
	FElysiumEntityDef Understudy;
	Understudy.Classname = TEXT("npc_VPedestrian");
	Understudy.TargetName = TEXT("understudy");
	Understudy.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl"));
	Defs.Defs.Add(MoveTemp(Understudy));

	FElysiumEntityDef Ornament;
	Ornament.Classname = TEXT("prop_dynamic_ornament");
	Ornament.TargetName = TEXT("worn_sign");
	Ornament.ModelMesh = TEXT("prophet_sign");
	Ornament.Keys.Add(TEXT("model"), TEXT("models/props/prophet_sign.mdl"));
	Ornament.Keys.Add(TEXT("parentname"), TEXT("understudy"));
	Defs.Defs.Add(MoveTemp(Ornament));

	// A bodiless character cannot be a parent — the honest "nothing to attach to" path.
	FElysiumEntityDef Bodiless;
	Bodiless.Classname = TEXT("npc_VPedestrian");
	Bodiless.TargetName = TEXT("no_body");
	Defs.Defs.Add(MoveTemp(Bodiless));

	FElysiumEntityDef Orphan;
	Orphan.Classname = TEXT("prop_dynamic_ornament");
	Orphan.TargetName = TEXT("orphan_sign");
	Orphan.ModelMesh = TEXT("prophet_sign");
	Orphan.Keys.Add(TEXT("model"), TEXT("models/props/prophet_sign.mdl"));
	Orphan.Keys.Add(TEXT("parentname"), TEXT("no_body"));
	Defs.Defs.Add(MoveTemp(Orphan));

	// sm_bailbonds_1's second hand: maxspeed 6 deg/s = one revolution per minute.
	// spawnflags 69 = START_ON | Z_AXIS | NOT_SOLID, exactly as exported.
	FElysiumEntityDef SecondHand;
	SecondHand.Classname = TEXT("func_rotating");
	SecondHand.TargetName = TEXT("secondhand");
	SecondHand.Model = 1;
	SecondHand.Hulls.Add(BoxHull());
	SecondHand.BrushMesh = TEXT("brush_1");
	SecondHand.Keys.Add(TEXT("model"), TEXT("*1"));
	SecondHand.Keys.Add(TEXT("maxspeed"), TEXT("6"));
	SecondHand.Keys.Add(TEXT("spawnflags"), TEXT("69"));
	Defs.Defs.Add(MoveTemp(SecondHand));

	FElysiumEntityDef Hand;
	Hand.Classname = TEXT("prop_dynamic");
	Hand.TargetName = TEXT("second");
	Hand.ModelMesh = TEXT("clock_hand");
	Hand.Keys.Add(TEXT("model"), TEXT("models/props/clock_hand.mdl"));
	Hand.Keys.Add(TEXT("parentname"), TEXT("secondhand"));
	Defs.Defs.Add(MoveTemp(Hand));

	// A rotator that is not START_ON, to prove Start/Stop/Reverse/SetSpeed drive it.
	FElysiumEntityDef Idle;
	Idle.Classname = TEXT("func_rotating");
	Idle.TargetName = TEXT("idlerotator");
	Idle.Origin = FVector(200.f, 0.f, 0.f);
	Idle.Model = 2;
	Idle.Hulls.Add(BoxHull());
	Idle.BrushMesh = TEXT("brush_2");
	Idle.Keys.Add(TEXT("model"), TEXT("*2"));
	Idle.Keys.Add(TEXT("maxspeed"), TEXT("10"));
	Idle.Keys.Add(TEXT("spawnflags"), TEXT("4"));    // Z_AXIS, no START_ON
	Defs.Defs.Add(MoveTemp(Idle));

	// sp_tutorial_1's ceiling fan: spawnflags 513 = START_ON | SND_LARGE, so neither axis bit is
	// set and it falls to the default. Every fan and sky rotator in the corpus is authored this
	// way, and all of them must turn about Z — the case the Z_AXIS clock hand above cannot cover.
	FElysiumEntityDef Fan;
	Fan.Classname = TEXT("func_rotating");
	Fan.TargetName = TEXT("fanrot1");
	Fan.Origin = FVector(400.f, 0.f, 0.f);
	Fan.Model = 3;
	Fan.Hulls.Add(BoxHull());
	Fan.BrushMesh = TEXT("brush_3");
	Fan.Keys.Add(TEXT("model"), TEXT("*3"));
	Fan.Keys.Add(TEXT("maxspeed"), TEXT("360"));
	Fan.Keys.Add(TEXT("spawnflags"), TEXT("513"));
	Defs.Defs.Add(MoveTemp(Fan));

	FElysiumEntityDefs RestoreDefs = Defs;
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);
	World.Tick(0.0);   // the activation frame: a START_ON rotator starts its clock on its first think

	FElysiumEntity* LiveUnderstudy = World.FindByName(TEXT("understudy"));
	FElysiumEntity* LiveOrnament = World.FindByName(TEXT("worn_sign"));
	FElysiumEntity* LiveOrphan = World.FindByName(TEXT("orphan_sign"));
	FElysiumEntity* LiveSecond = World.FindByName(TEXT("secondhand"));
	FElysiumEntity* LiveHand = World.FindByName(TEXT("second"));
	FElysiumEntity* LiveIdle = World.FindByName(TEXT("idlerotator"));
	FElysiumEntity* LiveFan = World.FindByName(TEXT("fanrot1"));
	if (!TestNotNull(TEXT("ceiling fan resolved"), LiveFan)
		|| !TestNotNull(TEXT("character resolved"), LiveUnderstudy)
		|| !TestNotNull(TEXT("ornament resolved"), LiveOrnament)
		|| !TestNotNull(TEXT("orphan ornament resolved"), LiveOrphan)
		|| !TestNotNull(TEXT("func_rotating resolved"), LiveSecond)
		|| !TestNotNull(TEXT("rotator child resolved"), LiveHand)
		|| !TestNotNull(TEXT("idle func_rotating resolved"), LiveIdle))
	{
		return false;
	}

	// --- A character is an attach parent ---------------------------------------------------
	TestTrue(TEXT("a character's attach body is its skeletal body"),
		LiveUnderstudy->GetAttachBody() != nullptr
		&& LiveUnderstudy->GetAttachBody() == LiveUnderstudy->GetSkeletalBody());
	TestTrue(TEXT("parentname attaches an ornament to the character body"),
		LiveOrnament->GetAttachBody()
		&& LiveOrnament->GetAttachBody()->GetAttachParent() == LiveUnderstudy->GetAttachBody());
	TestNull(TEXT("a bodiless character has no attach body"),
		World.FindByName(TEXT("no_body"))->GetAttachBody());
	TestTrue(TEXT("a child of a bodiless character stays unattached"),
		LiveOrphan->GetAttachBody() && LiveOrphan->GetAttachBody()->GetAttachParent() == nullptr);

	// --- A model swap carries the children onto the new body --------------------------------
	const FTransform WornOffset(FRotator(0.f, 30.f, 0.f), FVector(0.f, 0.f, 90.f));
	LiveOrnament->GetAttachBody()->SetRelativeTransform(WornOffset);
	USkeletalMeshComponent* BeforeSwap = LiveUnderstudy->GetSkeletalBody();
	LiveUnderstudy->SetRuntimeModel(TEXT("models/character/npc/common/blueblood/female/Blueblood_Female.mdl"));
	TestTrue(TEXT("SetModel rebuilds the character body"),
		LiveUnderstudy->GetSkeletalBody() && LiveUnderstudy->GetSkeletalBody() != BeforeSwap);
	TestTrue(TEXT("SetModel re-parents the ornament onto the new body"),
		LiveOrnament->GetAttachBody()
		&& LiveOrnament->GetAttachBody()->GetAttachParent() == LiveUnderstudy->GetSkeletalBody());
	TestTrue(TEXT("SetModel preserves the ornament's worn offset"),
		LiveOrnament->GetAttachBody()->GetRelativeTransform().Equals(WornOffset, 0.01f));

	// --- func_rotating turns at maxspeed degrees per second ----------------------------------
	TestTrue(TEXT("parentname attaches the clock hand to the rotator body"),
		LiveHand->GetAttachBody()
		&& LiveHand->GetAttachBody()->GetAttachParent() == LiveSecond->Body);
	TestEqual(TEXT("NOT_SOLID rotator takes the traceable passable profile"),
		LiveSecond->Body->GetCollisionProfileName(), FName(TEXT("ElysiumBrushPassable")));

	World.Tick(15.0);
	TestTrue(TEXT("a 6 deg/s rotator has turned 90 degrees at fifteen seconds"),
		FMath::IsNearlyEqual(AngleOf(LiveSecond), 90.f, 0.01f));
	// The Z_AXIS flag selects the roll component of Spawn's m_vecMoveAng, so a clock hand sweeps
	// about X — the wall normal — not about Z. X survives the Y-negating reflection unchanged, and
	// the reflection reverses the turn, so a quarter turn reads as roll -90.
	// The reflected turn is the same one the fan below makes, but it reads back as roll +90 rather
	// than -90: FRotator's Roll and Pitch run opposite to a right-handed turn about X and Y, while
	// Yaw runs with it. The quaternion is FQuat(+X, -90 deg) in both readings.
	TestTrue(TEXT("a Z_AXIS rotator turns about Unreal X with the reflected sign"),
		FMath::IsNearlyEqual(LiveSecond->Body->GetRelativeRotation().Roll, 90.f, 0.1f)
		&& FMath::IsNearlyZero(LiveSecond->Body->GetRelativeRotation().Pitch, 0.1f)
		&& FMath::IsNearlyZero(LiveSecond->Body->GetRelativeRotation().Yaw, 0.1f));
	TestTrue(TEXT("the parented hand rides the rotation"),
		FMath::IsNearlyEqual(LiveHand->GetAttachBody()->GetComponentRotation().Roll, 90.f, 0.1f));

	// The unflagged default is yaw. 360 deg/s for 15.25 s is a quarter turn past the wrap, which
	// reads as yaw -90 with the reflected sign — a ceiling fan sweeping the ceiling, not the wall.
	World.Tick(15.25);
	TestTrue(TEXT("an unflagged rotator turns about Unreal Z with the reflected sign"),
		FMath::IsNearlyEqual(LiveFan->Body->GetRelativeRotation().Yaw, -90.f, 0.1f)
		&& FMath::IsNearlyZero(LiveFan->Body->GetRelativeRotation().Pitch, 0.1f)
		&& FMath::IsNearlyZero(LiveFan->Body->GetRelativeRotation().Roll, 0.1f));

	World.Tick(60.0);
	TestTrue(TEXT("one full revolution takes a minute and wraps"),
		FMath::IsNearlyEqual(AngleOf(LiveSecond), 0.f, 0.01f));

	// --- Stop / Start / Reverse / SetSpeed ---------------------------------------------------
	FElysiumEntityHandle Self = LiveIdle->Handle;
	World.Tick(70.0);
	TestTrue(TEXT("a rotator without START_ON does not turn"), FMath::IsNearlyZero(AngleOf(LiveIdle)));
	// A queued input runs at the start of the tick that drains it, so each one is dispatched at the
	// time it should take effect and the interval is measured from there.
	World.AcceptInput(TEXT("idlerotator"), FName(TEXT("Start")), FElysiumVariant::Void(), Self, Self);
	World.Tick(70.0);
	World.Tick(73.0);
	TestTrue(TEXT("Start turns it at maxspeed"), FMath::IsNearlyEqual(AngleOf(LiveIdle), 30.f, 0.01f));
	World.AcceptInput(TEXT("idlerotator"), FName(TEXT("Stop")), FElysiumVariant::Void(), Self, Self);
	World.Tick(73.0);
	World.Tick(80.0);
	TestTrue(TEXT("Stop freezes the angle where it was"),
		FMath::IsNearlyEqual(AngleOf(LiveIdle), 30.f, 0.01f));
	World.AcceptInput(TEXT("idlerotator"), FName(TEXT("Reverse")), FElysiumVariant::Void(), Self, Self);
	World.AcceptInput(TEXT("idlerotator"), FName(TEXT("Start")), FElysiumVariant::Void(), Self, Self);
	World.Tick(80.0);
	World.Tick(82.0);
	TestTrue(TEXT("Reverse turns the other way from where it stopped"),
		FMath::IsNearlyEqual(AngleOf(LiveIdle), 10.f, 0.01f));
	World.AcceptInput(TEXT("idlerotator"), FName(TEXT("SetSpeed")), FElysiumVariant::Float(1.0f), Self, Self);
	World.Tick(82.0);
	World.Tick(92.0);
	TestTrue(TEXT("SetSpeed keeps turning the same way from the angle it had"),
		FMath::IsNearlyEqual(AngleOf(LiveIdle), 0.f, 0.01f));

	// --- A save resumes mid-spin -------------------------------------------------------------
	FElysiumMapSnapshot Snapshot;
	World.Freeze(Snapshot);

	AActor* RestoreOwner = EngineWorld->SpawnActor<AActor>();
	USceneComponent* RestoreRoot = NewObject<USceneComponent>(RestoreOwner, TEXT("RotatingRestoreRoot"));
	RestoreOwner->SetRootComponent(RestoreRoot);
	RestoreRoot->RegisterComponent();
	RestoreOwner->AddInstanceComponent(RestoreRoot);
	FElysiumRecordingServices RestoreServices;
	FElysiumEntityWorld Restored(RestoreOwner, nullptr, RestoreServices.Bundle());
	Restored.Load(MoveTemp(RestoreDefs));
	Restored.ApplySnapshot(Snapshot);
	Restored.Activate(92.0);
	Restored.Tick(92.0);
	FElysiumEntity* RestoredSecond = Restored.FindByName(TEXT("secondhand"));
	if (TestNotNull(TEXT("restored rotator resolved"), RestoredSecond))
	{
		// 6 deg/s for 92 s is 552 degrees — one revolution and 192 more.
		TestTrue(TEXT("a restored rotator resumes at the saved angle"),
			FMath::IsNearlyEqual(AngleOf(RestoredSecond), 192.f, 0.01f)
			&& FMath::IsNearlyEqual(AngleOf(RestoredSecond), AngleOf(LiveSecond), 0.01f));
		Restored.Tick(107.0);
		TestTrue(TEXT("and keeps turning at its speed from there"),
			FMath::IsNearlyEqual(AngleOf(RestoredSecond), 282.f, 0.01f));
	}

	return true;
}

} // namespace ElysiumInteractionTests

#endif // WITH_DEV_AUTOMATION_TESTS
