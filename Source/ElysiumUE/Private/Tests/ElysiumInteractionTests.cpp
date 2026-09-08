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
#include "Substrate/ElysiumCameraCinematic.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumInterestingPlaces.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumNpc.h"
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
#include "Substrate/ElysiumSignData.h"
#include "Tests/ElysiumEntityDebugStateTestHelpers.h"
#include "Tests/ElysiumOverlapTestProbe.h"
#include "Tests/ElysiumScratchContentRoot.h"
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
#include "Engine/GameInstance.h"
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
	// The world's default is retail's slot-44 `return 1` — a second `+use` press releases. This leaf
	// exists to drive the OTHER arm: the exclusivity rules a held session imposes on a press aimed
	// somewhere else. `CGameSign::vfunc44` `0x10212600` is retail's one entity override and answers
	// exactly this, so a fixture that says false is not inventing a behaviour.
	virtual bool ReleasesOnSecondUse() const override { return false; }
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

// FElysiumWorldServices — the substrate's outbound seam. Runs the shape of the
// tutorial's own logic_auto chain end to end against the recording stub: no RHI, no actors,
// no `$ELYSIUM_EXPORT_ROOT`. sp_tutorial_1's five logic_autos fire OnMapLoad at an NPC (WillTalk), a
// door (Lock), a math_counter and a delayed wire; this reproduces that shape and adds one
// entity per entity-addressed service; all five seams are exercised by the same world activation.
//
// The second half is the contract that makes the first half meaningful: the SAME defs on a
// world with NO services must reach the SAME logical state. Embodiment, audio, travel,
// presentation, and weather are outputs of the logic, never inputs to it.


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
		return ElysiumEntityDebugTest::CounterValue(Entity);
	};

	// --- With services: the chain reaches all five seams ---------------------------------
	FElysiumRecordingServices Rec;
	Rec.bHasPlayer = true;
	Rec.PlayerLocation = FVector(1000.f, 0.f, 0.f);
	Rec.PlayerRotation = FRotator(0.f, 90.f, 0.f);

	{
		FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr, Rec.Bundle());
		World.Load(BuildDefs());
		// A played map has a player entity — the map actor creates one after Load, and the
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
		// player entity — sampled off the body at the top of the frame — and asks the travel
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


// 11.15 — the two perception queries on the same seam. Nothing in the substrate consumes
// them yet (light sampling and senses are their callers), so what is under
// test here is the CONTRACT every one of those callers will be written against: the
// headless answers, and that a scripted answer actually comes back through the seam.
//
// Both defaults are load-bearing rather than placeholders. A `-nullrhi` Substrate run has
// no collision world and no light rig; an implementation that answered "blocked" and
// "dark" there would blind every NPC and hand the stealth surface a free pass in exactly
// the runs meant to prove neither happens.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPerceptionQueryTest,
	"Elysium.Substrate.PerceptionQueries", GElysiumTestFlags)
bool FElysiumPerceptionQueryTest::RunTest(const FString&)
{
	const FVector Eye(0.0, 0.0, 160.0);
	const FVector Target(500.0, 0.0, 160.0);

	// --- A fully headless world has no seam to ask at all, which every caller must survive -----
	{
		const FElysiumWorldServices None;
		TestNull(TEXT("a default-constructed bundle carries no embodiment"), None.Embodiment);
	}

	// --- The recording stub answers the headless values until a test says otherwise ------------
	{
		FElysiumRecordingServices Services;
		IElysiumEmbodiment& Seam = Services;

		TestTrue(TEXT("an unscripted line of sight is clear"), Seam.QueryLineOfSight(Eye, Target));
		TestEqual(TEXT("an unscripted point is fully lit"), Seam.QueryLightAtPoint(Target), 1.0f);
		TestTrue(TEXT("both crossings are recorded, so a caller's cadence is assertable"),
			Services.Saw(TEXT("QueryLineOfSight")) && Services.Saw(TEXT("QueryLightAtPoint")));
	}

	// --- ...and the scripted values come back through the seam ---------------------------------
	{
		FElysiumRecordingServices Services;
		Services.bLineOfSightClear = false;
		Services.LightAtPoint = 0.25f;
		IElysiumEmbodiment& Seam = Services;

		TestFalse(TEXT("a blocked segment reports blocked"), Seam.QueryLineOfSight(Eye, Target));
		TestEqual(TEXT("a dim point reports its level"), Seam.QueryLightAtPoint(Target), 0.25f);
		TestTrue(TEXT("the recorded line carries the verdict, not just the call"),
			Services.Saw(TEXT("QueryLineOfSight")) && Services.Log().Contains(TEXT("blocked")));
		TestTrue(TEXT("...and the light level it answered"),
			Services.Log().Contains(TEXT("= 0.25")));

		// The queries are geometry, never a verdict (K13): asking twice with the same arguments
		// answers the same thing, because nothing about cadence, debounce or grace lives here.
		TestEqual(TEXT("the query holds no state of its own"),
			Seam.QueryLightAtPoint(Target), Seam.QueryLightAtPoint(Target));
	}
	return true;
}


// Modern +use — deterministic selection order and the world-owned focus/session lifecycle.


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
		return ElysiumEntityDebugTest::CounterValue(Entity);
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


// Brush movers — visible body attachment, PASSABLE doors, use_override, prop_button,
// and the recovered func_elevator state machine exercised as one authored-style chain.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorElevatorTest,
	"Elysium.Substrate.DoorElevator", GElysiumTestFlags)
bool FElysiumDoorElevatorTest::RunTest(const FString&)
{
	// The `use_override` that names nothing is asserted below by what it does — the door stays shut
	// and `invalid_open` never counts. Its `"does_not_exist".Use [no target]` row is I/O accounting
	// emitted at Display, not a warning, so there is deliberately no expected error to declare here.
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
		return ElysiumEntityDebugTest::CounterValue(Entity);
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
		TestTrue(TEXT("restored elevator is resting at floor two"),
			ElysiumEntityDebugTest::Row(RestoredElevator, TEXT("Current floor")) == TEXT("2")
			&& ElysiumEntityDebugTest::Row(RestoredElevator, TEXT("Target floor")) == TEXT("(none)"));
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


// func_rotating + the character attach point.
//
// Two things a `parentname` needs that the substrate did not have: a character that can BE a
// parent (an ornament worn on an NPC, which survives the model swap a level script does), and the
// continuous spinner its own children ride. The rate is the assertion that matters — VtMB authors
// clock hands as `maxspeed` in degrees/second, so a second hand is 6 and a minute hand is 0.1.


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
	auto AngleOf = [](const FElysiumEntity* Entity)
	{
		return ElysiumEntityDebugTest::RowAsFloat(Entity, TEXT("Angle"));
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


// Computer-terminal first slice: ordered KeyValues, exclusive serial-checked authority,
// password and deterministic skill paths, and Function output delivery on the one queue.
// The fixture is project-authored and content-independent; no retail strings are embedded.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalDefinitionTest,
	"Elysium.Substrate.TerminalDefinition", GElysiumTestFlags)
bool FElysiumTerminalDefinitionTest::RunTest(const FString&)
{
	const FString Text = TEXT(R"KV(
TerminalDefinition
{
	"screen saver" "Synthetic monitor"
	"brackets" "[]"
	LogonScreen { "line0" "Test console" "line1" "Authorized users only" }
	SubDir
	{
		"name" "Vault"
		"password" "needle"
		"description" "Door controls"
		"difficulty" "0"
		Function { "name" "Open" "description" "Open door" "runtext" "Opened." "trigger" "0" }
		Function { "name" "Close" "description" "Close door" "dependency" "G.AllowClose" }
	}
	SubDir { "name" "Logs" "description" "Audit log" }
	Email { "subject" "Status" "sender" "ops" "body" "Nominal" "autodelete" "1" }
}
)KV");

	FElysiumTerminalDefinition Definition;
	FString Error;
	if (!TestTrue(TEXT("synthetic TerminalDefinition parses"),
		FElysiumTerminalDefinition::ParseText(Text, Definition, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("authored logon line order is preserved"),
		FString::Join(Definition.LogonLines, TEXT("|")), TEXT("Test console|Authorized users only"));
	TestEqual(TEXT("authored screensaver label is preserved"), Definition.ScreenSaver,
		TEXT("Synthetic monitor"));
	TestEqual(TEXT("authored directory order is preserved"), Definition.Directories.Num(), 2);
	TestEqual(TEXT("first directory stays first"), Definition.Directories[0].Name, TEXT("Vault"));
	TestEqual(TEXT("repeated Function blocks preserve order"),
		Definition.Directories[0].Functions.Num(), 2);
	TestEqual(TEXT("the numbered output is typed"),
		Definition.Directories[0].Functions[0].Trigger, 0);
	TestEqual(TEXT("email fields are typed without activating TERM7 behavior"),
		Definition.Emails.Num(), 1);
	TestTrue(TEXT("email autodelete is retained"), Definition.Emails[0].bAutoDelete);

	// `CPropHacking+0x904` is a 64-byte field filled with `Q_strncpy`, so a longer authored label is
	// truncated at load and the screensaver's column placement measures the truncated string
	// (`docs/vtmb/computer-terminals.md` §4.4).
	{
		const FString Long = FString::ChrN(80, TEXT('Z'));
		FElysiumTerminalDefinition Capped;
		FString CapError;
		TestTrue(TEXT("an over-long screensaver label still parses"),
			FElysiumTerminalDefinition::ParseText(
				FString::Printf(TEXT("TerminalDefinition { \"screen saver\" \"%s\" }"), *Long),
				Capped, CapError));
		TestEqual(TEXT("and is truncated to the retail field width"), Capped.ScreenSaver.Len(),
			ElysiumTerminalScreenSaverMax);
	}

	// `CPropHacking::LoadFromFile` `0x1021cba0` reads every `Email` field with `Q_strncpy` into a
	// fixed record slot, and hands three of them a compiled-in DEFAULT rather than the empty
	// string (`0x105b078c`, `0x105b0760`, `0x105b073c`). Both halves are content-visible: an
	// `Email` block with no `subject` draws retail's stand-in on the glass, and an over-long body
	// is truncated at load, not on the way to the screen.
	{
		const FString LongSubject = FString::ChrN(60, TEXT('S'));
		const FString LongBody = FString::ChrN(700, TEXT('B'));
		const FString LongScript = FString::ChrN(90, TEXT('R'));
		FElysiumTerminalDefinition Capped;
		FString CapError;
		if (TestTrue(TEXT("an Email block with over-long fields still parses"),
			FElysiumTerminalDefinition::ParseText(FString::Printf(TEXT(
				"TerminalDefinition { Email { \"subject\" \"%s\" \"body\" \"%s\" "
				"\"runscript\" \"%s\" \"dependency\" \"%s\" } Email { } }"),
				*LongSubject, *LongBody, *LongScript, *LongScript), Capped, CapError))
			&& TestEqual(TEXT("both Email blocks are records"), Capped.Emails.Num(), 2))
		{
			TestEqual(TEXT("subject is capped at Q_strncpy(..., 0x20) minus the terminator"),
				Capped.Emails[0].Subject.Len(), ElysiumTerminalEmailCaps::Subject);
			TestEqual(TEXT("body at 0x200 minus the terminator"), Capped.Emails[0].Body.Len(),
				ElysiumTerminalEmailCaps::Body);
			TestEqual(TEXT("runscript at 0x40 minus the terminator"),
				Capped.Emails[0].RunScript.Len(), ElysiumTerminalEmailCaps::RunScript);
			TestEqual(TEXT("and dependency shares that 64-byte slot"),
				Capped.Emails[0].Dependency.Len(), ElysiumTerminalEmailCaps::Dependency);
			// The authored keys that WERE present take no default...
			TestEqual(TEXT("an authored sender is absent, so the default stands"),
				Capped.Emails[0].Sender, FString(ElysiumTerminalEmailCaps::DefaultSender));
			// ...and a block that authors nothing takes all three.
			TestEqual(TEXT("an empty Email block takes the retail subject default"),
				Capped.Emails[1].Subject, FString(TEXT("this email has no subject")));
			TestEqual(TEXT("the retail sender default"), Capped.Emails[1].Sender,
				FString(TEXT("this email has no sender")));
			TestEqual(TEXT("and the retail body default"), Capped.Emails[1].Body,
				FString(TEXT("this email has no body")));
			// `dependency` and `runscript` have NO default — retail passes the empty string — so an
			// unauthored one must stay empty or every mail would run a script named "".
			TestTrue(TEXT("dependency has no default"), Capped.Emails[1].Dependency.IsEmpty());
			TestTrue(TEXT("nor runscript"), Capped.Emails[1].RunScript.IsEmpty());
		}
		// An authored-but-EMPTY value is not an absent key: `KeyValues` returns the empty string and
		// the default never applies, which is the same distinction the sign's `"XPos" ""` turns on.
		FElysiumTerminalDefinition Blank;
		if (TestTrue(TEXT("an Email block with an explicitly empty subject parses"),
			FElysiumTerminalDefinition::ParseText(
				TEXT("TerminalDefinition { Email { \"subject\" \"\" } }"), Blank, CapError))
			&& TestEqual(TEXT("as one record"), Blank.Emails.Num(), 1))
		{
			TestTrue(TEXT("an authored empty subject stays empty; the default is for an ABSENT key"),
				Blank.Emails[0].Subject.IsEmpty());
		}
	}

	// The other three halves of the same loader (`0x1021cba0`, §12): the top-level keys, the
	// `SubDir` record and the `Function` record. One file that overruns every one of those caps,
	// and two records that author nothing at all — `name` (`1021ceaf` / `1021cf9d`), `description`
	// (`1021cefc` / `1021cfe2`) and `runtext` (`1021d003`) are each read with their own KEY LITERAL
	// as the default, because retail passes the same `.rdata` pointer as key and as fallback
	// (`0x1053fd80`, `0x105b07d8`, `0x105b07c0`). An empty block is therefore not an empty screen.
	{
		const FString Long = FString::ChrN(700, TEXT('L'));
		FElysiumTerminalDefinition Capped;
		FString CapError;
		const FString CapText = FString::Printf(TEXT(
			"TerminalDefinition {"
			"  \"screen saver\" \"%s\" \"brackets\" \"<<<>>>\""
			"  \"email_password\" \"%s\" \"email_username\" \"%s\""
			"  SubDir {"
			"    \"name\" \"%s\" \"password\" \"%s\" \"description\" \"%s\" \"dependency\" \"%s\""
			"    Function {"
			"      \"name\" \"%s\" \"description\" \"%s\" \"runtext\" \"%s\""
			"      \"dependency\" \"%s\" \"runscript\" \"%s\""
			"    }"
			"    Function { }"
			"  }"
			"  SubDir { }"
			"}"),
			*Long, *Long, *Long, *Long, *Long, *Long, *Long, *Long, *Long, *Long, *Long, *Long);
		if (TestTrue(TEXT("a record that overruns every loader cap still parses"),
			FElysiumTerminalDefinition::ParseText(CapText, Capped, CapError))
			&& TestEqual(TEXT("both SubDir blocks are records"), Capped.Directories.Num(), 2)
			&& TestEqual(TEXT("both Function blocks are records"),
				Capped.Directories[0].Functions.Num(), 2))
		{
			TestEqual(TEXT("screen saver is capped at Q_strncpy(..., 0x40) minus the terminator"),
				Capped.ScreenSaver.Len(), ElysiumTerminalCaps::ScreenSaver);
			TestEqual(TEXT("brackets keep two characters plus the NUL of the 3-byte field"),
				Capped.Brackets, FString(TEXT("<<")));
			TestEqual(TEXT("email_password at 0x20 minus the terminator"),
				Capped.EmailPassword.Len(), ElysiumTerminalCaps::EmailPassword);
			TestEqual(TEXT("email_username shares that width"), Capped.EmailUsername.Len(),
				ElysiumTerminalCaps::EmailUsername);

			const FElysiumTerminalDirectory& Directory = Capped.Directories[0];
			TestEqual(TEXT("a SubDir name is capped at 0x10 minus the terminator"),
				Directory.Name.Len(), ElysiumTerminalCaps::Name);
			TestEqual(TEXT("its password shares that 16-byte slot"), Directory.Password.Len(),
				ElysiumTerminalCaps::Password);
			TestEqual(TEXT("its description at 0x20 minus the terminator"),
				Directory.Description.Len(), ElysiumTerminalCaps::Description);
			TestEqual(TEXT("its dependency at 0x40 minus the terminator"),
				Directory.Dependency.Len(), ElysiumTerminalCaps::Dependency);

			const FElysiumTerminalFunction& Function = Directory.Functions[0];
			TestEqual(TEXT("a Function name takes the same 16-byte slot as a SubDir name"),
				Function.Name.Len(), ElysiumTerminalCaps::Name);
			TestEqual(TEXT("and the same 32-byte description"), Function.Description.Len(),
				ElysiumTerminalCaps::Description);
			TestEqual(TEXT("its runtext at 0x200 minus the terminator, like an email body"),
				Function.RunText.Len(), ElysiumTerminalCaps::RunText);
			TestEqual(TEXT("its dependency at 0x40 minus the terminator"), Function.Dependency.Len(),
				ElysiumTerminalCaps::Dependency);
			TestEqual(TEXT("and its runscript shares that 64-byte slot"), Function.RunScript.Len(),
				ElysiumTerminalCaps::RunScript);

			// The three literal defaults, on the two blocks that author nothing.
			const FElysiumTerminalFunction& Bare = Directory.Functions[1];
			TestEqual(TEXT("a Function with no name answers to the literal 'name'"), Bare.Name,
				FString(TEXT("name")));
			TestEqual(TEXT("with no description it draws the literal 'description'"),
				Bare.Description, FString(TEXT("description")));
			TestEqual(TEXT("and with no runtext it prints the literal 'runtext'"), Bare.RunText,
				FString(TEXT("runtext")));
			TestTrue(TEXT("a Function dependency still has no default"), Bare.Dependency.IsEmpty());
			TestTrue(TEXT("nor its runscript"), Bare.RunScript.IsEmpty());

			const FElysiumTerminalDirectory& BareDir = Capped.Directories[1];
			TestEqual(TEXT("a SubDir with no name answers to the literal 'name'"), BareDir.Name,
				FString(TEXT("name")));
			TestEqual(TEXT("and with no description its title box reads 'description'"),
				BareDir.Description, FString(TEXT("description")));
			TestTrue(TEXT("a SubDir password has no default"), BareDir.Password.IsEmpty());
			TestTrue(TEXT("nor its dependency"), BareDir.Dependency.IsEmpty());
		}
	}

	// `brackets` is the one top-level key with an `.rdata` default (`0x105b083c` = "[]",
	// `0x1021cc8a`). Every shipped `hackterminals` file but one authors the line, and the tutorial
	// authors it EMPTY — which is not the same thing as omitting it, because `KeyValues` answers an
	// authored empty string and the default never applies.
	{
		FElysiumTerminalDefinition Bare;
		FElysiumTerminalDefinition Blank;
		FString BracketError;
		if (TestTrue(TEXT("a file with no brackets line parses"),
			FElysiumTerminalDefinition::ParseText(TEXT("TerminalDefinition { }"), Bare, BracketError)))
		{
			TestEqual(TEXT("an absent brackets key takes the compiled-in \"[]\""), Bare.Brackets,
				FString(TEXT("[]")));
		}
		if (TestTrue(TEXT("an explicitly empty brackets line parses"),
			FElysiumTerminalDefinition::ParseText(TEXT("TerminalDefinition { \"brackets\" \"\" }"),
				Blank, BracketError)))
		{
			TestTrue(TEXT("an authored empty brackets stays empty, as the tutorial authors it"),
				Blank.Brackets.IsEmpty());
		}
	}

	// `1021cf84`: the Function loop is `while (block != NULL && count < 0x14)`, so a twenty-first
	// block is never read into the record vector and is content nobody can reach.
	{
		FString ManyText = TEXT("TerminalDefinition { SubDir { \"name\" \"deck\"");
		for (int32 Index = 0; Index < 25; ++Index)
		{
			ManyText += FString::Printf(TEXT(" Function { \"name\" \"f%d\" }"), Index);
		}
		ManyText += TEXT(" } }");
		FElysiumTerminalDefinition Many;
		FString ManyError;
		if (TestTrue(TEXT("a SubDir with twenty-five Function blocks parses"),
			FElysiumTerminalDefinition::ParseText(ManyText, Many, ManyError))
			&& TestEqual(TEXT("as one directory"), Many.Directories.Num(), 1))
		{
			TestEqual(TEXT("the loader keeps twenty functions per SubDir"),
				Many.Directories[0].Functions.Num(),
				ElysiumTerminalCaps::FunctionsPerDirectory);
			TestEqual(TEXT("and they are the first twenty, in authored order"),
				Many.Directories[0].Functions[19].Name, FString(TEXT("f19")));
		}
	}

	FElysiumTerminalDefinition Invalid;
	TestFalse(TEXT("an out-of-range trigger fails closed"),
		FElysiumTerminalDefinition::ParseText(
			TEXT("TerminalDefinition { SubDir { Function { name bad trigger 8 } } }"),
			Invalid, Error));
	TestTrue(TEXT("the parse failure identifies the trigger"), Error.Contains(TEXT("trigger 8")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalSessionTest,
	"Elysium.Substrate.TerminalSession", GElysiumTestFlags)
bool FElysiumTerminalSessionTest::RunTest(const FString&)
{
	const FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();
	const FElysiumClassDesc* HackingClass = Registry.Find(FName(TEXT("prop_hacking")));
	if (!TestNotNull(TEXT("prop_hacking is registered"), HackingClass))
	{
		return false;
	}
	TestEqual(TEXT("prop_hacking leaves the model-only prop chain"), HackingClass->BaseName,
		ElysiumTerminalClassName());
	for (const TCHAR* Field : { TEXT("start_enabled"), TEXT("textcolumns"), TEXT("hack_file"),
		TEXT("difficulty"), TEXT("skilltype") })
	{
		TestNotNull(*FString::Printf(TEXT("terminal field %s resolves through the chain"), Field),
			reinterpret_cast<const void*>(Registry.FindField(*HackingClass, FName(Field))));
	}

	FElysiumTerminalDefinition Definition;
	FString ParseError;
	if (!FElysiumTerminalDefinition::ParseText(TEXT(R"KV(
TerminalDefinition
{
	"screen saver" "Synthetic session"
	LogonScreen { "line0" "Test console" }
	SubDir
	{
		"name" "Vault"
		"password" "needle"
		"description" "Door controls"
		"difficulty" "0"
		Function { "name" "Open" "runtext" "Opened." "trigger" "0" }
	}
	SubDir
	{
		"name" "Plain"
		Function { "name" "Ping" }
	}
}
)KV"), Definition, ParseError))
	{
		AddError(ParseError);
		return false;
	}

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__terminal_session__");
	FElysiumEntityDef TerminalDef;
	TerminalDef.Classname = TEXT("prop_hacking");
	TerminalDef.TargetName = TEXT("terminal");
	TerminalDef.Keys.Add(TEXT("start_enabled"), TEXT("1"));
	TerminalDef.Keys.Add(TEXT("difficulty"), TEXT("1"));
	TerminalDef.Keys.Add(TEXT("skilltype"), TEXT("2"));
	FElysiumOutputDef Trigger;
	Trigger.Name = TEXT("OnTrigger0");
	Trigger.Target = TEXT("triggered");
	Trigger.Input = TEXT("Add");
	Trigger.Param = TEXT("1");
	TerminalDef.Outputs.Add(MoveTemp(Trigger));
	// `CBaseVampireSkillEntity::vfunc39` / `vfunc42` (`0x102181a9` / `0x1021827e`) are the first and
	// last things a terminal's entry and exit do. `tuthack` authors neither row, so the fixture
	// wires them by hand — the outputs exist on `CBaseEntity`'s own map `0x10552e18`.
	FElysiumOutputDef UseBegin;
	UseBegin.Name = TEXT("OnUseBegin");
	UseBegin.Target = TEXT("use_began");
	UseBegin.Input = TEXT("Add");
	UseBegin.Param = TEXT("1");
	TerminalDef.Outputs.Add(MoveTemp(UseBegin));
	FElysiumOutputDef UseEnd;
	UseEnd.Name = TEXT("OnUseEnd");
	UseEnd.Target = TEXT("use_ended");
	UseEnd.Input = TEXT("Add");
	UseEnd.Param = TEXT("1");
	TerminalDef.Outputs.Add(MoveTemp(UseEnd));
	Defs.Defs.Add(MoveTemp(TerminalDef));
	for (const TCHAR* CounterName : { TEXT("triggered"), TEXT("use_began"), TEXT("use_ended") })
	{
		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = CounterName;
		Counter.Keys.Add(TEXT("min"), TEXT("0"));
		Counter.Keys.Add(TEXT("max"), TEXT("10"));
		Defs.Defs.Add(MoveTemp(Counter));
	}

	FElysiumRecordingServices Services;
	// The screen cone gates every terminal session; stand the glass in front of the eye.
	Services.StandTerminalScreen();
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	AddExpectedError(TEXT("terminal content failed: hack_file is empty"),
		EAutomationExpectedErrorFlags::Contains, 1);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumTerminal* BaseTerminal = World.FindByName(TEXT("terminal"))
		? World.FindByName(TEXT("terminal"))->AsTerminal() : nullptr;
	if (!TestNotNull(TEXT("terminal entity resolves"), BaseTerminal))
	{
		return false;
	}
	FElysiumPropHacking* Terminal = static_cast<FElysiumPropHacking*>(BaseTerminal);
	Terminal->InstallDefinition(MoveTemp(Definition));
	Terminal->InputEnable();

	const FElysiumUseBeginResult Opened = World.BeginPlayerUseSession(Terminal->Handle, Player);
	TestEqual(TEXT("+use starts one explicit terminal session"), Opened.Outcome,
		EElysiumUseOutcome::SessionStarted);
	const uint32 FirstSerial = Terminal->SessionSerial;
	World.Tick(0.0);
	TestTrue(TEXT("entry fires the skill base's OnUseBegin once"),
		FMath::IsNearlyEqual(
			ElysiumEntityDebugTest::CounterValue(World.FindByName(TEXT("use_began"))), 1.0f));
	TestTrue(TEXT("and OnUseEnd has not fired yet"),
		FMath::IsNearlyEqual(
			ElysiumEntityDebugTest::CounterValue(World.FindByName(TEXT("use_ended"))), 0.0f));
	FElysiumTerminalView View;
	TestTrue(TEXT("the active terminal publishes a view"), World.BuildTerminalView(View));
	TestEqual(TEXT("the view carries the captured serial"), View.SessionSerial, FirstSerial);
	TestEqual(TEXT("the view carries the authored screensaver label"), View.ScreenSaverLabel,
		TEXT("Synthetic session"));
	TestTrue(TEXT("an active terminal blocks saving"),
		World.ScriptedSessionSaveBlockReason().Contains(TEXT("terminal")));
	TestEqual(TEXT("a second use is exclusive"),
		World.BeginPlayerUseSession(Terminal->Handle, Player).Outcome, EElysiumUseOutcome::Busy);
	TestFalse(TEXT("a stale serial cannot submit"),
		World.SubmitTerminalCommand(Terminal->Handle, FirstSerial - 1, TEXT("Vault")));

	TestTrue(TEXT("directory command reaches the authority"),
		World.SubmitTerminalCommand(Terminal->Handle, FirstSerial, TEXT("Vault")));
	TestEqual(TEXT("a locked directory enters password mode"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Password);
	TestTrue(TEXT("a wrong password is handled"),
		World.SubmitTerminalCommand(Terminal->Handle, FirstSerial, TEXT("wrong")));
	TestEqual(TEXT("wrong password increments that directory only"),
		Terminal->DirectoryAttempts[0], 1);
	TestTrue(TEXT("password comparison is case-insensitive"),
		World.SubmitTerminalCommand(Terminal->Handle, FirstSerial, TEXT("NEEDLE")));
	TestEqual(TEXT("accepted password enters the directory"), Terminal->CurrentDirectory, 0);
	TestTrue(TEXT("the authored Function executes"),
		World.SubmitTerminalCommand(Terminal->Handle, FirstSerial, TEXT("Open")));
	World.Tick(0.0);
	TestTrue(TEXT("OnTrigger0 delivers through the ordinary queue"),
		FMath::IsNearlyEqual(
			ElysiumEntityDebugTest::CounterValue(World.FindByName(TEXT("triggered"))), 1.0f));

	// The loader's two literal defaults, on the glass. `CPropHacking::LoadFromFile` hands
	// `description` (`0x1021cefc`) and `runtext` (`0x1021d003`) their own key string as the
	// fallback, and both are DRAWN: the directory draw uses the description as its title box
	// (`FUN_1021aca0`) and `ExecuteFunction` prints the runtext as the printer's format string
	// (`FUN_1021c6d0`). The second `SubDir` authors neither line.
	auto ScreenShows = [Terminal](const TCHAR* Needle)
	{
		for (int32 Row = 0; Row < Terminal->Screen.Rows(); ++Row)
		{
			if (Terminal->Screen.RowTextTrimmed(Row).Contains(Needle))
			{
				return true;
			}
		}
		return false;
	};
	TestTrue(TEXT("a passwordless directory opens on its name alone"),
		World.SubmitTerminalCommand(Terminal->Handle, FirstSerial, TEXT("Plain")));
	TestEqual(TEXT("and that is the directory the session is in"), Terminal->CurrentDirectory, 1);
	TestTrue(TEXT("a directory with no description draws the literal 'description' in its title box"),
		ScreenShows(TEXT("description")));
	TestTrue(TEXT("its Function runs"),
		World.SubmitTerminalCommand(Terminal->Handle, FirstSerial, TEXT("Ping")));
	TestTrue(TEXT("a Function with no runtext prints the literal 'runtext'"),
		ScreenShows(TEXT("runtext")));

	TestTrue(TEXT("quit closes through the captured session"),
		World.SubmitTerminalCommand(Terminal->Handle, FirstSerial, TEXT("quit")));
	World.Tick(0.0);
	TestTrue(TEXT("the exit fires the skill base's OnUseEnd once"),
		FMath::IsNearlyEqual(
			ElysiumEntityDebugTest::CounterValue(World.FindByName(TEXT("use_ended"))), 1.0f));
	TestFalse(TEXT("the closed terminal no longer publishes"), World.BuildTerminalView(View));
	TestTrue(TEXT("closing clears the save block"), World.ScriptedSessionSaveBlockReason().IsEmpty());
	TestFalse(TEXT("a closed session rejects its old serial"),
		World.SubmitTerminalCommand(Terminal->Handle, FirstSerial, TEXT("list")));

	TestEqual(TEXT("the terminal can be opened again"),
		World.BeginPlayerUseSession(Terminal->Handle, Player).Outcome,
		EElysiumUseOutcome::SessionStarted);
	const uint32 SecondSerial = Terminal->SessionSerial;
	TestTrue(TEXT("a reopened terminal has a new serial"), SecondSerial != FirstSerial);
	// The preceding password success intentionally persists. Relock only this synthetic fixture so
	// the same terminal can exercise the independent skill-bypass route in the reopened session.
	Terminal->DirectoryUnlocked[0] = 0;
	World.SubmitTerminalCommand(Terminal->Handle, SecondSerial, TEXT("Vault"));
	TestTrue(TEXT("break in password mode starts the recovered skill bypass"),
		World.SubmitTerminalCommand(Terminal->Handle, SecondSerial, TEXT("break")));
	TestTrue(TEXT("the timed bypass adds its own save block"), Terminal->AttemptUser.IsSet());
	World.Tick(5.0);
	TestEqual(TEXT("rating zero fails entity difficulty one deterministically"),
		Terminal->LastRoll, 1);
	TestEqual(TEXT("failed bypass returns to root without closing the terminal"),
		Terminal->CurrentDirectory, INDEX_NONE);
	TestEqual(TEXT("failed bypass increments the directory attempt counter"),
		Terminal->DirectoryAttempts[0], 2);
	TestTrue(TEXT("the terminal session remains active after bypass failure"),
		World.BuildTerminalView(View));

	Terminal->Difficulty = 0;
	World.SubmitTerminalCommand(Terminal->Handle, SecondSerial, TEXT("Vault"));
	World.SubmitTerminalCommand(Terminal->Handle, SecondSerial, TEXT("break"));
	World.Tick(10.0);
	TestEqual(TEXT("directory difficulty zero falls back to entity difficulty zero"),
		Terminal->LastRoll, 3);
	TestEqual(TEXT("successful bypass enters the same directory transition as a password"),
		Terminal->CurrentDirectory, 0);
	World.SubmitTerminalCommand(Terminal->Handle, SecondSerial, TEXT("quit"));

	// --- slice B: the gate's own arms -------------------------------------------------------
	// Same-player re-entry is allowed (`0x102180c0`: "no current user, **or** that user IS the
	// requester"); a second activator is not. This is what makes the per-tick gate re-check pass
	// while the session is live.
	FElysiumUseContext Gate;
	Gate.Owner = Terminal->Handle;
	Gate.Activator = Player;
	Gate.EyeOrigin = Services.PlayerLocation;
	Gate.bHasEyeOrigin = true;
	TestEqual(TEXT("the terminal reopens once more"),
		World.BeginPlayerUseSession(Terminal->Handle, Player).Outcome,
		EElysiumUseOutcome::SessionStarted);
	TestTrue(TEXT("the same player passes the gate while in session"),
		Terminal->CanPlayerFocus(Gate));
	FElysiumUseContext Other = Gate;
	Other.Activator = FElysiumEntityHandle(4242, 1);
	TestFalse(TEXT("a second activator is refused"), Terminal->CanPlayerFocus(Other));
	World.SubmitTerminalCommand(Terminal->Handle, Terminal->SessionSerial, TEXT("quit"));

	// No eye at all fails closed.
	FElysiumUseContext Blind = Gate;
	Blind.bHasEyeOrigin = false;
	TestFalse(TEXT("no eye fails the gate closed"), Terminal->CanPlayerFocus(Blind));

	// --- a model with no attachments refuses, and pushes no camera --------------------------
	{
		FElysiumRecordingServices Blank;
		Blank.bHasPlayer = true;
		Blank.PlayerLocation = FVector(-300.0f, 0.0f, 0.0f);
		FElysiumEntityDefs BareDefs;
		BareDefs.MapName = TEXT("__terminal_no_screen__");
		FElysiumEntityDef BareTerminal;
		BareTerminal.Classname = TEXT("prop_hacking");
		BareTerminal.TargetName = TEXT("terminal");
		BareTerminal.Keys.Add(TEXT("start_enabled"), TEXT("1"));
		// An authored model that carries neither attachment: the defect the named error exists for.
		BareTerminal.Keys.Add(TEXT("model"), TEXT("models/synthetic/no_screen.mdl"));
		BareDefs.Defs.Add(MoveTemp(BareTerminal));

		FElysiumEntityWorld Blind2(nullptr, nullptr, Blank.Bundle());
		AddExpectedError(TEXT("terminal content failed: hack_file is empty"),
			EAutomationExpectedErrorFlags::Contains, 1);
		AddExpectedError(TEXT("resolves no 'screen' attachment"),
			EAutomationExpectedErrorFlags::Contains, 1);
		// `BuildPropVisual` on the double answers a component for any stem, so the model is
		// "present" and the missing pair is reported once, at spawn.
		Blind2.Load(MoveTemp(BareDefs));
		const FElysiumEntityHandle BlindPlayer = Blind2.SpawnPlayer();
		Blind2.Activate(0.0);
		FElysiumEntity* BareEntity = Blind2.FindByName(TEXT("terminal"));
		FElysiumTerminal* BareBase = BareEntity ? BareEntity->AsTerminal() : nullptr;
		if (TestNotNull(TEXT("the attachment-less terminal resolves"), BareBase))
		{
			BareBase->InputEnable();
			TestFalse(TEXT("it resolved no attachments"), BareBase->bScreenAttachmentsResolved);
			TestEqual(TEXT("and names the missing part"), FString(BareBase->AttachmentError),
				FString(TEXT("screen")));
			TestEqual(TEXT("+use is refused"),
				Blind2.BeginPlayerUseSession(BareBase->Handle, BlindPlayer).Outcome,
				EElysiumUseOutcome::Unavailable);
			TestEqual(TEXT("and no camera shot was pushed"),
				Blank.Count(TEXT("PushCameraShotNamed")), 0);
		}
	}

	// --- an UNRESOLVABLE shot does not refuse: retail runs the session cameraless -------------
	// `FUN_10070470` returns NULL when `FUN_1006e130` cannot load the named block, and
	// `CPropHacking::vfunc39` step 8 then calls `FUN_1017cef0(player, NULL)`, which clears the
	// camera fields and leaves the client on the player's own eye (`slice-bc-decompiles.md`
	// §3.2/§3.4). Everything before it — the hold, the draw, the prompt — has already happened.
	{
		FElysiumRecordingServices NoShot;
		NoShot.StandTerminalScreen();
		NoShot.bNamedCameraShotResolves = false;
		FElysiumEntityDefs ShotDefs;
		ShotDefs.MapName = TEXT("__terminal_no_shot__");
		FElysiumEntityDef ShotTerminal;
		ShotTerminal.Classname = TEXT("prop_hacking");
		ShotTerminal.TargetName = TEXT("terminal");
		ShotTerminal.Keys.Add(TEXT("start_enabled"), TEXT("1"));
		ShotDefs.Defs.Add(MoveTemp(ShotTerminal));

		FElysiumEntityWorld ShotWorld(nullptr, nullptr, NoShot.Bundle());
		AddExpectedError(TEXT("terminal content failed: hack_file is empty"),
			EAutomationExpectedErrorFlags::Contains, 1);
		AddExpectedError(TEXT("did not resolve; the session runs cameraless"),
			EAutomationExpectedErrorFlags::Contains, 1);
		ShotWorld.Load(MoveTemp(ShotDefs));
		const FElysiumEntityHandle ShotPlayer = ShotWorld.SpawnPlayer();
		ShotWorld.Activate(0.0);
		FElysiumEntity* ShotEntity = ShotWorld.FindByName(TEXT("terminal"));
		FElysiumTerminal* ShotBase = ShotEntity ? ShotEntity->AsTerminal() : nullptr;
		if (TestNotNull(TEXT("the cameraless terminal resolves"), ShotBase))
		{
			ShotBase->InputEnable();
			FElysiumPlayer* ShotPlayerEntity = ShotWorld.FindPlayer();
			TestEqual(TEXT("an unresolvable shot still opens the session"),
				ShotWorld.BeginPlayerUseSession(ShotBase->Handle, ShotPlayer).Outcome,
				EElysiumUseOutcome::SessionStarted);
			// M8: the terminal keeps no handle of its own — the adoption slot is the only place a
			// live cine shot exists. A shot that did not load reaches `FUN_1017cef0(player, NULL)`
			// and the slot ends up empty, which is what "cameraless" means.
			TestEqual(TEXT("nothing was adopted into the cine slot"),
				ShotWorld.CineCameraShotId(), 0);
			TestFalse(TEXT("... and the slot is empty"), ShotWorld.HasScriptedCamera());
			TestEqual(TEXT("the push was attempted exactly once"),
				NoShot.Count(TEXT("PushCameraShotNamed special-case:Hacking exposure=clamped")), 1);
			FElysiumTerminalView CamlessView;
			TestTrue(TEXT("and the screen is published as usual"),
				ShotWorld.BuildTerminalView(CamlessView));
			if (TestNotNull(TEXT("the cameraless player resolves"), ShotPlayerEntity))
			{
				TestFalse(TEXT("the player is still held in place"), ShotPlayerEntity->IsMobile());
			}
			ShotWorld.EndPlayerUseSession(ShotBase->Handle, EElysiumUseEndReason::Completed);
			TestEqual(TEXT("and the exit pops nothing it never pushed"),
				NoShot.Count(TEXT("PopCameraShot")), 0);
		}
	}
	return true;
}


// The retail integer-only Python truth gate at the terminal and sign dependency surfaces.
//
// Retail evaluates a computer-terminal dependency (CPropHacking::TestDependency -> the shared
// CDialogDependency::CallPyDialogFunction helper) and a sign wrapper dependency (CGameSign::
// LoadSignData -> FUN_101d2850) with the EXACT logic_pythoncheck rule: the Py_eval_input result
// is TRUE only when it is a non-zero Python integer; a non-integer/null/error is FALSE (RE:
// $ELYSIUM_WORK_ROOT/research/event-surface/terminal-sign-truth-findings.md; claims C073/C079).
// That is FElysiumVariant::IsPythonCheckTrue, not the generic ToBool — so a truthy non-integer (a
// non-empty string, the float 1.0), which ToBool calls TRUE, must read FALSE at both surfaces.
// These two tests drive EvalCondition onto each variant category through a sentinel host and
// assert the observable outcome (a rendered directory, a selected wrapper block).


// A script host whose result is fully determined by the source string, so the dependency gates
// can be driven onto any FElysiumVariant category without a Python VM or the export corpus.
class FElysiumSentinelScriptHost final : public IElysiumScriptHost
{
public:
	virtual FElysiumVariant Eval(const FString& Source, const FElysiumScriptContext&,
		FString* OutError = nullptr) override
	{
		if (OutError)
		{
			OutError->Reset();
		}
		if (Source == TEXT("INT_NONZERO"))   { return FElysiumVariant::Int(7); }
		if (Source == TEXT("INT_ZERO"))      { return FElysiumVariant::Int(0); }
		if (Source == TEXT("STRING_TRUTHY")) { return FElysiumVariant::String(TEXT("open")); }
		if (Source == TEXT("FLOAT_TRUTHY"))  { return FElysiumVariant::Float(1.0f); }
		return FElysiumVariant::Void();
	}
	virtual const TCHAR* Name() const override { return TEXT("sentinel"); }
};

// UElysiumGameStateSubsystem is a UGameInstanceSubsystem (ClassWithin GameInstance), so a
// NewObject with a package/transient outer ensures ("created in invalid Outer Package"). Outer it to
// a throwaway UGameInstance instead to get a valid, un-Initialized game state whose SetScriptHost is
// all these dependency-gate tests need — no running game, no subsystem collection. The instance is
// kept alive by the returned subsystem's outer chain for the synchronous test scope.
static UElysiumGameStateSubsystem* MakeHeadlessGameState()
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	return NewObject<UElysiumGameStateSubsystem>(GameInstance);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalDependencyTruthinessTest,
	"Elysium.Substrate.TerminalDependencyTruthiness", GElysiumTestFlags)
bool FElysiumTerminalDependencyTruthinessTest::RunTest(const FString&)
{
	// A prop_hacking directory is visible iff its dependency passes the gate; an empty dependency
	// short-circuits to visible without evaluating (preserved from retail).
	UElysiumGameStateSubsystem* State = MakeHeadlessGameState();
	State->SetScriptHost(MakeUnique<FElysiumSentinelScriptHost>());

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__terminal_dependency_truth__");
	FElysiumEntityDef TerminalDef;
	TerminalDef.Classname = TEXT("prop_hacking");
	TerminalDef.TargetName = TEXT("terminal");
	TerminalDef.Keys.Add(TEXT("start_enabled"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(TerminalDef));

	FElysiumRecordingServices Services;
	// The screen cone gates every terminal session; stand the glass in front of the eye.
	Services.StandTerminalScreen();
	FElysiumEntityWorld World(nullptr, State, Services.Bundle());
	AddExpectedError(TEXT("terminal content failed: hack_file is empty"),
		EAutomationExpectedErrorFlags::Contains, 1);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);

	FElysiumTerminal* Base = World.FindByName(TEXT("terminal"))
		? World.FindByName(TEXT("terminal"))->AsTerminal() : nullptr;
	if (!TestNotNull(TEXT("terminal entity resolves"), Base))
	{
		return false;
	}
	FElysiumPropHacking* Terminal = static_cast<FElysiumPropHacking*>(Base);

	FElysiumTerminalDefinition Def;
	Def.ScreenSaver = TEXT("Truth console");
	auto AddDir = [&Def](const TCHAR* Name, const TCHAR* Dependency)
	{
		FElysiumTerminalDirectory Dir;
		Dir.Name = Name;
		Dir.Dependency = Dependency;
		Def.Directories.Add(MoveTemp(Dir));
	};
	AddDir(TEXT("OpenDir"),    TEXT(""));               // empty -> short-circuit pass
	AddDir(TEXT("IntYesDir"),  TEXT("INT_NONZERO"));    // non-zero int -> pass
	AddDir(TEXT("IntZeroDir"), TEXT("INT_ZERO"));       // zero int -> fail
	AddDir(TEXT("StrDir"),     TEXT("STRING_TRUTHY"));  // truthy string -> fail (ToBool would pass)
	AddDir(TEXT("FloatDir"),   TEXT("FLOAT_TRUTHY"));   // float 1.0 -> fail (ToBool would pass)
	Terminal->InstallDefinition(MoveTemp(Def));
	Terminal->InputEnable();

	const FElysiumUseBeginResult Opened = World.BeginPlayerUseSession(Terminal->Handle, Player);
	TestEqual(TEXT("the terminal opens a session"), Opened.Outcome,
		EElysiumUseOutcome::SessionStarted);

	FElysiumTerminalView View;
	if (!TestTrue(TEXT("the active terminal publishes a view"), World.BuildTerminalView(View)))
	{
		return false;
	}

	auto DirectoryVisible = [&View](const TCHAR* Name) -> bool
	{
		for (const FElysiumTerminalActionView& Action : View.Actions)
		{
			if (Action.Id.StartsWith(TEXT("dir:")) && Action.Label == Name)
			{
				return true;
			}
		}
		return false;
	};

	TestTrue(TEXT("empty dependency short-circuits to visible"), DirectoryVisible(TEXT("OpenDir")));
	TestTrue(TEXT("non-zero integer dependency is visible"), DirectoryVisible(TEXT("IntYesDir")));
	TestFalse(TEXT("zero integer dependency is hidden"), DirectoryVisible(TEXT("IntZeroDir")));
	// The retail divergence from generic truthiness: a truthy non-integer reads FALSE.
	TestFalse(TEXT("truthy string dependency is hidden (integer-only rule)"),
		DirectoryVisible(TEXT("StrDir")));
	TestFalse(TEXT("float 1.0 dependency is hidden (integer-only rule)"),
		DirectoryVisible(TEXT("FloatDir")));

	World.EndPlayerUseSession(Terminal->Handle, EElysiumUseEndReason::Completed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSignDependencyTruthinessTest,
	"Elysium.Substrate.SignDependencyTruthiness", GElysiumTestFlags)
bool FElysiumSignDependencyTruthinessTest::RunTest(const FString&)
{
	// A `Sign { dependency; filename }` wrapper selects the first block whose dependency passes the
	// gate. The truthy-*string* block is authored FIRST and the non-zero-*integer* block second, so
	// the integer-only rule must skip the string and select the integer, where the old ToBool()
	// would have taken the string block first. FElysiumSignData::Load reads through
	// FElysiumContentPaths::SignFile, so the fixture lives under a scratch content root installed
	// for the test.
	const FElysiumScratchContentRoot Scratch(TEXT("SignTruth"));
	const FString ScratchSigns = Scratch.Directory(TEXT("signs"));

	if (!TestTrue(TEXT("the scratch content root is installed"),
		FPaths::IsSamePath(FElysiumContentPaths::SignsDir(), ScratchSigns)))
	{
		return false;
	}

	const FString WrapperLeaf = TEXT("elysium_test_sign_wrapper.txt");
	const FString StringTargetLeaf = TEXT("elysium_test_sign_string_target.txt");
	const FString IntTargetLeaf = TEXT("elysium_test_sign_int_target.txt");

	auto Write = [&ScratchSigns](const FString& Leaf, const FString& Body) -> bool
	{
		return FFileHelper::SaveStringToFile(Body, *(ScratchSigns / Leaf));
	};

	const bool bWrote =
		Write(WrapperLeaf, FString::Printf(TEXT(
			"Sign\n{\n\t\"dependency\" \"STRING_TRUTHY\"\n\t\"filename\" \"%s\"\n}\n"
			"Sign\n{\n\t\"dependency\" \"INT_NONZERO\"\n\t\"filename\" \"%s\"\n}\n"),
			*StringTargetLeaf, *IntTargetLeaf))
		&& Write(StringTargetLeaf, TEXT("SignData\n{\n\t\"HideHUD\" \"0\"\n}\n"))
		&& Write(IntTargetLeaf, TEXT("SignData\n{\n\t\"HideHUD\" \"0\"\n}\n"));
	if (!TestTrue(TEXT("sign fixture files written"), bWrote))
	{
		return false;
	}

	UElysiumGameStateSubsystem* State = MakeHeadlessGameState();
	State->SetScriptHost(MakeUnique<FElysiumSentinelScriptHost>());
	FElysiumEntityWorld World(nullptr, State);
	World.Activate(0.0);

	FElysiumSignData Data;
	const bool bLoaded = FElysiumSignData::Load(WrapperLeaf, Data, &World);
	if (!TestTrue(TEXT("the wrapper resolves to a target"), bLoaded))
	{
		return false;
	}
	// Retail selects the integer block; ToBool() would have taken the truthy-string block first.
	TestEqual(TEXT("integer-only rule selects the non-zero-integer block"),
		Data.SourceFile, IntTargetLeaf);
	TestNotEqual(TEXT("...not the truthy-string block"), Data.SourceFile, StringTargetLeaf);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueConditionTruthinessTest,
	"Elysium.Substrate.DialogueConditionTruthiness", GElysiumTestFlags)
bool FElysiumDialogueConditionTruthinessTest::RunTest(const FString&)
{
	// A `.dlg` col-4 PC-choice condition is offered iff it passes retail's CDialogDependency::Test ->
	// CallPyDialogFunction gate: Py_eval_input, then TRUE only for a non-zero Python integer. That is
	// FElysiumVariant::IsPythonCheckTrue (the shared logic_pythoncheck/terminal/sign rule), not the
	// generic ToBool — so a truthy non-integer (a non-empty string, the float 1.0) which ToBool calls
	// TRUE must GATE OUT the choice. This drives the real FElysiumNpc::OpenConversation lambda, whose
	// EvalCondition lands on each variant category through the sentinel host, and reads back the
	// world's live conversation to observe which choices survive the gate.
	//
	// OpenConversation loads the `dialogname` `.dlg` off disk (FElysiumContentPaths::DlgFromDialogname),
	// so the synthetic fixture lives under a scratch content root installed for the test.
	const FElysiumScratchContentRoot Scratch(TEXT("DlgCondTruth"));
	const FString ScratchRoot = Scratch.Root;
	const FString DialogName = TEXT("dlg/test/cond_truth.dlg");
	const FString DlgPath = ScratchRoot / DialogName;

	if (!TestTrue(TEXT("the scratch content root is installed"),
		FPaths::IsSamePath(FElysiumContentPaths::DlgFromDialogname(DialogName), DlgPath)))
	{
		return false;
	}

	// One 13-field `.dlg` row in the on-disk `{ TAB content TAB }` shape (cols 6-11 empty, col-12
	// Malkavian empty). Local to this file rather than reaching for the dialogue-suite helper.
	auto Row = [](int32 Id, const TCHAR* Text, const TCHAR* Link, const TCHAR* Cond) -> FString
	{
		auto F = [](const FString& S) { return FString::Printf(TEXT("{\t%s\t}"), *S); };
		FString R;
		R += F(FString::FromInt(Id)); // 0 id
		R += F(Text);                 // 1 male text
		R += F(Text);                 // 2 female text
		R += F(Link);                 // 3 link
		R += F(Cond);                 // 4 condition (PC gate)
		R += F(FString());            // 5 action
		for (int32 i = 6; i <= 11; ++i) { R += F(FString()); }
		R += F(FString());            // 12 Malkavian
		return R;
	};

	// Entry NPC line 1 (line-1 fallback with no sentinel / usescript), then five PC choices whose
	// col-4 evaluates to each variant category, then the target NPC line 20 that closes the response
	// band. The sentinel host maps the (identity-normalized) condition text to a variant; an empty
	// col-4 is an open gate that never reaches the host.
	const TArray<FString> Rows = {
		Row(1,  TEXT("Entry."),           TEXT("#"),  TEXT("")),
		Row(10, TEXT("Open gate."),       TEXT("20"), TEXT("")),               // empty -> available
		Row(11, TEXT("Non-zero int."),    TEXT("20"), TEXT("INT_NONZERO")),    // Int(7) -> available
		Row(12, TEXT("Zero int."),        TEXT("20"), TEXT("INT_ZERO")),       // Int(0) -> gated out
		Row(13, TEXT("Truthy string."),   TEXT("20"), TEXT("STRING_TRUTHY")),  // String -> gated out (ToBool: available)
		Row(14, TEXT("Float one."),       TEXT("20"), TEXT("FLOAT_TRUTHY")),   // Float(1.0) -> gated out (ToBool: available)
		Row(20, TEXT("Follow-up."),       TEXT("#"),  TEXT("")),
	};
	const FString Joined = FString::Join(Rows, TEXT("\r\n")) + TEXT("\r\n");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(DlgPath), /*Tree*/ true);
	if (!TestTrue(TEXT("dialogue fixture written"), FFileHelper::SaveStringToFile(Joined, *DlgPath)))
	{
		return false;
	}

	UElysiumGameStateSubsystem* State = MakeHeadlessGameState();
	State->SetScriptHost(MakeUnique<FElysiumSentinelScriptHost>());

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, State, Services.Bundle());

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__dlg_condition_truth__");
	FElysiumEntityDef NpcDef;
	NpcDef.Classname = TEXT("npc_VVampire");
	NpcDef.TargetName = TEXT("Truthy");
	NpcDef.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
	NpcDef.Keys.Add(TEXT("dialogname"), DialogName);
	Defs.Defs.Add(MoveTemp(NpcDef));
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0); // admit the NPC mind/body before dialogue acquires it

	FElysiumEntity* NpcEntity = World.FindByName(TEXT("Truthy"));
	FElysiumNpc* Npc = NpcEntity ? NpcEntity->AsNpc() : nullptr;
	if (!TestNotNull(TEXT("the NPC entity resolves as an NPC"), Npc))
	{
		return false;
	}

	if (!TestTrue(TEXT("OpenConversation opens the dialogue"),
		Npc->OpenConversation(Player, EElysiumDialogOpenerKind::Forced)))
	{
		return false;
	}

	FElysiumDlgConversation* Conv = World.GetOpenDialog();
	if (!TestNotNull(TEXT("the world exposes the open conversation"), Conv))
	{
		return false;
	}
	if (TestNotNull(TEXT("the conversation opens on an NPC line"), Conv->CurrentNpcLine()))
	{
		TestEqual(TEXT("the line-1 fallback selects the entry line"), Conv->CurrentNpcLine()->Id, 1);
	}

	auto ChoiceVisible = [Conv](int32 LineId) -> bool
	{
		// The band now carries a gate result per row (M-DISABLED); these fixtures are all Python
		// gates, so every surviving row is enabled.
		const TArray<FElysiumDlgVisibleChoice>& Visible = Conv->VisibleChoices();
		for (int32 i = 0; i < Visible.Num(); ++i)
		{
			const FElysiumDlgLine* Line = Conv->VisibleChoice(i);
			if (Line && Line->Id == LineId)
			{
				return true;
			}
		}
		return false;
	};

	TestTrue(TEXT("an empty condition is offered"), ChoiceVisible(10));
	TestTrue(TEXT("a non-zero integer condition is offered"), ChoiceVisible(11));
	TestFalse(TEXT("a zero integer condition is gated out"), ChoiceVisible(12));
	// The retail divergence from generic truthiness: a truthy non-integer reads FALSE, so ToBool()
	// (which offers both) fails these two assertions until the gate uses IsPythonCheckTrue().
	TestFalse(TEXT("a truthy string condition is gated out (integer-only rule)"), ChoiceVisible(13));
	TestFalse(TEXT("a float 1.0 condition is gated out (integer-only rule)"), ChoiceVisible(14));
	TestEqual(TEXT("only the open and non-zero-integer choices survive the gate"),
		Conv->VisibleChoices().Num(), 2);

	World.CloseDialog(/*bSilent=*/true);
	return true;
}


// ------------------------------------------------------------------------------------------------
// The lock family's `Intrusion` camera and placement — `CBaseLockableEnt` slots 32/36/37/39-43.
//
// Recovered in `$ELYSIUM_WORK_ROOT/_camera_recovery/rc_group_f.md` §RC15.1. `CPropDoorknob`,
// `CPropPadlock`, `CPropDoorknobElectronic` and `CItemContainerLock` share ONE body per slot:
//
//   32 `FUN_10224ae0`  the `+USE` gate — and there is no distance arm anywhere in it;
//   36 `FUN_10224ca0`  `"item_g_lockpick"`;
//   37 `FUN_10224eb0`  `_DAT_104454c8` = 80.0 units;
//   39 `FUN_10225070`  `FUN_10070470("Intrusion", NULL, this, this, NULL)`,
//                      `cam->m_bForcePlayerLook = 0`, `SetCineCamera`, `SetImmobilized(true)`,
//                      then `OnSkillAttemptBegin`;
//   40 / 43 `FUN_10224440`  place the player at the lock, through the model's `camera_position` /
//                      `camera_target` attachments;
//   41 `FUN_102252f0`  the per-tick view snap plus the timed roll;
//   42 `FUN_10225140`  `OnUseEnd`, `SetCineCamera(NULL)` (a CUT), `SetImmobilized(false)`, usermsg 6.
//
// **Straying past 80 units is a REPOSITION, not a break-off**: `FUN_10167e00`'s far arm dispatches
// slot 43 and its near arm slot 41, and only slot 32 failing — or the player letting go — ends a
// session. Case 4 asserts exactly that, because the brief this slice was cut from said "break-off".
//
// Everything runs on a bare `FElysiumEntityWorld` + `FElysiumRecordingServices`, with the shipped
// `Intrusion` block hand-built and seeded through `ElysiumCameraShots::InstallNamed` so the real
// factory (`FElysiumCameraCinematic::CreateRuntimeCamera`) runs with no export mounted.
namespace ElysiumLockableCameraTests
{
// `vdata/camerashots/special-case.txt`'s `Intrusion` block, key for key. Two `Named` anchors on the
// lock model's own attachments, both `AttachType Follow`, **no `Start`** (so the shot eases in from
// the player's own eye), and constraints faster than `Hacking`'s in every axis.
static FElysiumCameraShotDef IntrusionShot()
{
	FElysiumCameraShotDef Def;
	Def.Name = TEXT("Intrusion");
	Def.End.bPresent = true;
	Def.End.Position = EElysiumShotPosition::Named;
	Def.End.AttachPos = TEXT("Attachment: camera_position");
	Def.End.AttachPoint = EElysiumShotAttachPos::Attachment;
	Def.End.AttachPointName = TEXT("camera_position");
	Def.End.Attach = EElysiumShotAttach::Follow;
	Def.Target1.bPresent = true;
	Def.Target1.Position = EElysiumShotPosition::Named;
	Def.Target1.AttachPos = TEXT("Attachment: camera_target");
	Def.Target1.AttachPoint = EElysiumShotAttachPos::Attachment;
	Def.Target1.AttachPointName = TEXT("camera_target");
	Def.Target1.Attach = EElysiumShotAttach::Follow;
	Def.TargetPointCount = 1;
	Def.bTargetPoint1Flagged = true;
	Def.Constraints.MoveAccel = 450.0f * ElysiumCam::U;
	Def.Constraints.TurnAccel = 250.0f;
	Def.Constraints.MoveSpeed = 300.0f * ElysiumCam::U;
	Def.Constraints.MaxTurnRate = FVector(320.0f, 320.0f, 320.0f);
	Def.Constraints.DistanceTolerance = 5.0f * ElysiumCam::U;
	Def.Constraints.AngularTolerance = FVector(3.0f, 3.0f, 3.0f);
	Def.Constraints.FieldOfView = 75.0f;
	Def.Constraints.bDialogPOV = false;
	Def.Constraints.bSyncRotateOnMove = true;
	// The pair that separates this block from `Hacking`, which authors `DrawViewmodel 0`: the
	// lockpick is EQUIPPED for the shot, never holstered.
	Def.Constraints.bDrawViewmodel = true;
	Def.Constraints.bShowHud = true;
	return Def;
}

static void InstallIntrusion()
{
	TArray<FElysiumCameraShotDef> Blocks;
	Blocks.Add(IntrusionShot());
	ElysiumCameraShots::InstallNamed(TEXT("special-case"), Blocks);
}

// Slot 36's one classname, as a minimal catalogue: the entity-class registration is what makes a
// loose `item_g_lockpick` an `FElysiumItem` the inventory can see. `is_wieldable 0` is the shipped
// record's own value and it is load-bearing here — it is why acquisition does NOT make the pick
// active, which is exactly the state that sends `FUN_10167e00` down its slot-40 arm.
static FElysiumItemTable LockpickTable()
{
	FElysiumItemTable Table;
	FElysiumItemDef Pick;
	Pick.Classname = TEXT("item_g_lockpick");
	Pick.PrintName = TEXT("Lockpicks");
	Pick.Type = EElysiumItemType::Generic;
	Pick.PlayerModel = TEXT("models/items/test/lockpick.mdl");
	Table.Items.Add(MoveTemp(Pick));
	Table.Reindex();
	return Table;
}

// One lock leaf on its parent, deliberately BODILESS: the lock's geometry reaches this port through
// `GetUseBodyWorldBounds` and `GetBodyAttachment`, both of which the double answers directly, and a
// bodiless child raises no physical-attachment warning.
struct FLockWorld
{
	FElysiumRecordingServices Services;
	TUniquePtr<FElysiumEntityWorld> World;
	FElysiumEntityHandle Player;
	FElysiumLockableEntity* Lock = nullptr;
	FElysiumPlayer* PlayerEntity = nullptr;
};

static TUniquePtr<FLockWorld> StandLock(const TCHAR* LockClass, const TCHAR* ParentClass,
	const FVector& LockOrigin)
{
	TUniquePtr<FLockWorld> Fixture = MakeUnique<FLockWorld>();

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__lockable_camera__");

	FElysiumEntityDef Parent;
	Parent.Classname = ParentClass;
	Parent.TargetName = TEXT("holder");
	if (FCString::Strifind(ParentClass, TEXT("item_container")) != nullptr)
	{
		Parent.ModelMesh = TEXT("test_crate");
		Parent.Keys.Add(TEXT("model"), TEXT("models/test/crate.mdl"));
		Parent.Keys.Add(TEXT("use_icon"), TEXT("5"));
	}
	else
	{
		Parent.Keys.Add(TEXT("wait"), TEXT("-1"));
	}
	Defs.Defs.Add(MoveTemp(Parent));

	FElysiumEntityDef LockDef;
	LockDef.Classname = LockClass;
	LockDef.TargetName = TEXT("lock");
	LockDef.Origin = LockOrigin;
	LockDef.Keys.Add(TEXT("parentname"), TEXT("holder"));
	// A non-zero `difficulty` seeds the lock LOCKED in Spawn, and 10 against a headless rating of 0
	// keeps the timed roll a failure — so a session opened here stays open until the case ends it.
	LockDef.Keys.Add(TEXT("difficulty"), TEXT("10"));
	LockDef.Keys.Add(TEXT("skilltype"), TEXT("1"));   // the Intrusion feat, as all 800 shipped do
	Defs.Defs.Add(MoveTemp(LockDef));

	FElysiumEntityDef Loose;
	Loose.Classname = TEXT("item_g_lockpick");
	Loose.TargetName = TEXT("picks");
	Defs.Defs.Add(MoveTemp(Loose));

	Fixture->Services.bHasPlayer = true;
	Fixture->Services.PlayerLocation = LockOrigin + FVector(60.0, 0.0, 0.0);
	Fixture->World = MakeUnique<FElysiumEntityWorld>(nullptr, nullptr, Fixture->Services.Bundle());
	Fixture->World->Load(MoveTemp(Defs));
	Fixture->Player = Fixture->World->SpawnPlayer();
	Fixture->World->Activate(0.0);
	Fixture->PlayerEntity = Fixture->World->FindPlayer();
	FElysiumEntity* LockEntity = Fixture->World->FindByName(TEXT("lock"));
	Fixture->Lock = LockEntity ? LockEntity->AsLockableEntity() : nullptr;
	// `CBaseCombatWeaponDefaultTouch` — the acquisition route the tutorial's own lockpicks take.
	if (FElysiumEntity* Picks = Fixture->World->FindByName(TEXT("picks")))
	{
		Fixture->World->RouteEntityTouch(Picks->Handle, Fixture->Player, /*bBegin*/ true);
	}
	return Fixture;
}

static FElysiumCameraCinematic* AdoptedCine(FElysiumEntityWorld& World)
{
	FElysiumEntity* Entity = World.Resolve(World.CineCameraEntity());
	return Entity ? Entity->AsCameraCinematic() : nullptr;
}

static FElysiumUseContext HeldContext(const FLockWorld& Fixture, const FVector& EyeCm)
{
	FElysiumUseContext Context;
	Context.Activator = Fixture.Player;
	Context.Owner = Fixture.Lock->Handle;
	Context.EyeOrigin = EyeCm;
	Context.bHasEyeOrigin = true;
	return Context;
}
}

using namespace ElysiumLockableCameraTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLockableCameraTest,
	"Elysium.Substrate.LockableCamera", GElysiumTestFlags)
bool FElysiumLockableCameraTest::RunTest(const FString&)
{
	const FElysiumItemTable Items = LockpickTable();
	ElysiumItems::Install(Items);
	ElysiumCameraShots::FlushCache();
	ON_SCOPE_EXIT
	{
		ElysiumCameraShots::FlushCache();
		ElysiumItems::Uninstall(Items);
	};

	const FVector LockOrigin(0.0, 0.0, 100.0);
	// The two `$attachment`s the `Intrusion` block anchors on and `FUN_10224440` measures with:
	// `camera_target` on the lock face, `camera_position` one metre out along -X, which makes the
	// flattened standing axis exactly (-1, 0, 0).
	const FVector CameraPositionCm(-100.0, 0.0, 100.0);
	const FVector CameraTargetCm = LockOrigin;
	// `_DAT_1048c35c` = 31.0 units out from the lock's OWN abs origin along that axis. Spelled with
	// float literals because the runtime's constant is a float and `ToString` prints three decimals.
	const FVector StandCm = LockOrigin + FVector(-31.0f * 2.54f, 0.0, 0.0);
	const FVector ProbeEndCm = StandCm - FVector(0.0, 0.0, 1024.0f * 2.54f);

	// --- 1. A doorknob adopts the `Intrusion` shot, bound to itself on anchors 1 and 2 ------------
	{
		ElysiumCameraShots::FlushCache();
		InstallIntrusion();
		TUniquePtr<FLockWorld> Fixture = StandLock(TEXT("prop_doorknob"),
			TEXT("func_door_rotating"), LockOrigin);
		if (!TestNotNull(TEXT("the doorknob resolves"), Fixture->Lock)
			|| !TestNotNull(TEXT("the player entity resolves"), Fixture->PlayerEntity))
		{
			return false;
		}
		Fixture->Services.BodyAttachments.Add(FName(TEXT("camera_position")),
			FTransform(CameraPositionCm));
		Fixture->Services.BodyAttachments.Add(FName(TEXT("camera_target")),
			FTransform(CameraTargetCm));
		TestTrue(TEXT("the knob starts locked from its own difficulty"),
			Fixture->Lock->IsUseLocked());
		TestTrue(TEXT("and the player carries slot 36's item_g_lockpick"),
			Fixture->PlayerEntity->Inventory.Has(*Fixture->PlayerEntity, TEXT("item_g_lockpick")));
		TestTrue(TEXT("the player starts mobile"), Fixture->PlayerEntity->IsMobile());

		const FElysiumUseBeginResult Opened =
			Fixture->World->BeginPlayerUseSession(Fixture->Lock->Handle, Fixture->Player);
		TestEqual(TEXT("+use on a pickable lock opens an explicit session"), Opened.Outcome,
			EElysiumUseOutcome::SessionStarted);

		FElysiumCameraCinematic* Cine = AdoptedCine(*Fixture->World);
		if (!TestNotNull(TEXT("slot 39 adopted a runtime camera_cinematic"), Cine))
		{
			return false;
		}
		TestEqual(TEXT("carrying the Intrusion shot"), Cine->ShotDef.Name,
			FString(TEXT("Intrusion")));
		TestEqual(TEXT("on CamMode 1, the mode FUN_10070470 passes"), Cine->CamMode,
			static_cast<int32>(EElysiumCineCamMode::NamedShot));
		// `FUN_10070470(name, Start, End, Point1, Point2)` called with `(NULL, this, this, NULL)`.
		TestFalse(TEXT("anchor 0 (Start) is unbound — the shot authors no Start block"),
			Cine->Bindings.Anchors[0].Entity.IsSet());
		TestTrue(TEXT("anchor 1 (End) is the lock itself"),
			Cine->Bindings.Anchors[1].Entity == Fixture->Lock->Handle);
		TestTrue(TEXT("anchor 2 (Target Point1) is the lock itself"),
			Cine->Bindings.Anchors[2].Entity == Fixture->Lock->Handle);
		TestFalse(TEXT("anchor 3 (Point2) is unbound"), Cine->Bindings.Anchors[3].Entity.IsSet());
		TestTrue(TEXT("both anchors resolved their Attachment: name on the lock's model"),
			Cine->Bindings.Anchors[1].IsPointIndexResolved()
				&& Cine->Bindings.Anchors[2].IsPointIndexResolved());
		// The load-bearing line: the constructor seeds 1 and the opener writes 0, so this is the one
		// interaction shot besides `FuncMonitor` that does NOT turn the player's eye every tick.
		TestFalse(TEXT("m_bForcePlayerLook was cleared on the camera the opener created"),
			Cine->bForcePlayerLook);
		TestTrue(TEXT("the runtime camera carries the disposable bit"), Cine->bDisposable);
		TestTrue(TEXT("the shared cine slot holds its published shot"),
			Fixture->World->CineCameraShotId() != 0
				&& Fixture->World->CineCameraShotId() == Cine->PublishedShotId);
		// Order — `0x1022509b` `FUN_1017cef0`, then `0x102250a1` `FUN_1015ef40`. Nothing headless
		// observes the interleave of two calls inside one body, so what is asserted is the pair of
		// post-conditions plus (case 3) that the hold is NOT gated on the adoption succeeding.
		TestFalse(TEXT("and the player is immobilized once the opener has adopted"),
			Fixture->PlayerEntity->IsMobile());

		// --- slot 42, the close: a same-tick cut that destroys the disposable camera -------------
		const FElysiumEntityHandle CameraHandle = Cine->Handle;
		const int32 AdoptedShot = Fixture->World->CineCameraShotId();
		const int32 PopsBefore = Fixture->Services.Count(TEXT("PopCameraShot"));
		Fixture->World->EndPlayerUseSession(Fixture->Lock->Handle, EElysiumUseEndReason::Released);
		TestFalse(TEXT("the closer drops the cine slot"), Fixture->World->HasScriptedCamera());
		TestEqual(TEXT("... to nothing, never to a previously stacked shot"),
			Fixture->World->CineCameraShotId(), 0);
		FElysiumEntity* Dead = Fixture->World->Resolve(CameraHandle);
		TestTrue(TEXT("and destroys the disposable camera with it"),
			Dead == nullptr || Dead->IsDead());
		TestEqual(TEXT("the release is a CUT — a zero-second pop, no ramp"),
			Fixture->Services.Count(FString::Printf(TEXT("PopCameraShot %d blend=0.00"),
				AdoptedShot)), 1);
		TestEqual(TEXT("exactly one pop"), Fixture->Services.Count(TEXT("PopCameraShot")),
			PopsBefore + 1);
		TestTrue(TEXT("and the player is mobile again"), Fixture->PlayerEntity->IsMobile());
	}

	// --- 2. A padlock and a container lock run the very same slot-39 body ------------------------
	// `CPropDoorknobElectronic` is the fourth leaf and shares it too, but its Spawn forces
	// `requires_key`, so slot 32 refuses the pick path before the camera is ever reached.
	{
		struct FLeaf { const TCHAR* Lock; const TCHAR* Parent; };
		const FLeaf Leaves[] =
		{
			{ TEXT("prop_padlock"), TEXT("func_door_rotating") },
			{ TEXT("item_container_lock"), TEXT("item_container") },
		};
		for (const FLeaf& Leaf : Leaves)
		{
			ElysiumCameraShots::FlushCache();
			InstallIntrusion();
			TUniquePtr<FLockWorld> Fixture = StandLock(Leaf.Lock, Leaf.Parent, LockOrigin);
			if (!TestNotNull(*FString::Printf(TEXT("%s resolves"), Leaf.Lock), Fixture->Lock))
			{
				continue;
			}
			Fixture->Services.BodyAttachments.Add(FName(TEXT("camera_position")),
				FTransform(CameraPositionCm));
			Fixture->Services.BodyAttachments.Add(FName(TEXT("camera_target")),
				FTransform(CameraTargetCm));
			TestEqual(*FString::Printf(TEXT("%s opens a session"), Leaf.Lock),
				Fixture->World->BeginPlayerUseSession(Fixture->Lock->Handle,
					Fixture->Player).Outcome, EElysiumUseOutcome::SessionStarted);
			FElysiumCameraCinematic* Cine = AdoptedCine(*Fixture->World);
			if (!TestNotNull(*FString::Printf(TEXT("%s adopts a camera"), Leaf.Lock), Cine))
			{
				continue;
			}
			TestEqual(*FString::Printf(TEXT("%s adopts the Intrusion shot"), Leaf.Lock),
				Cine->ShotDef.Name, FString(TEXT("Intrusion")));
			TestTrue(*FString::Printf(TEXT("%s binds anchor 1 to itself"), Leaf.Lock),
				Cine->Bindings.Anchors[1].Entity == Fixture->Lock->Handle);
			TestTrue(*FString::Printf(TEXT("%s binds anchor 2 to itself"), Leaf.Lock),
				Cine->Bindings.Anchors[2].Entity == Fixture->Lock->Handle);
			TestFalse(*FString::Printf(TEXT("%s clears m_bForcePlayerLook"), Leaf.Lock),
				Cine->bForcePlayerLook);
			TestFalse(*FString::Printf(TEXT("%s immobilizes the player"), Leaf.Lock),
				Fixture->PlayerEntity->IsMobile());
			Fixture->World->EndPlayerUseSession(Fixture->Lock->Handle,
				EElysiumUseEndReason::Released);
			TestFalse(*FString::Printf(TEXT("%s cuts the slot on the way out"), Leaf.Lock),
				Fixture->World->HasScriptedCamera());
			TestTrue(*FString::Printf(TEXT("%s mobilizes the player"), Leaf.Lock),
				Fixture->PlayerEntity->IsMobile());
		}
	}

	// --- 3. A shot that will not load does NOT refuse the session ---------------------------------
	// `FUN_10070470` returns NULL, and slot 39 still reaches `FUN_1017cef0(player, NULL)` and
	// `FUN_1015ef40(player)`. The attempt runs cameraless and the player is still held.
	{
		// `FlushCache` alone would not make it miss: `vdata/camerashots/special-case.txt` is shipped
		// corpus, so on a machine with the export mounted the flushed cache re-reads the real file
		// and `Intrusion` resolves. `InstallMiss` seeds the remembered-miss entry `LoadFile` writes
		// for an absent file, which is the production refusal path.
		ElysiumCameraShots::FlushCache();
		ElysiumCameraShots::InstallMiss(TEXT("special-case"));
		TUniquePtr<FLockWorld> Fixture = StandLock(TEXT("prop_doorknob"),
			TEXT("func_door_rotating"), LockOrigin);
		if (!TestNotNull(TEXT("the cameraless doorknob resolves"), Fixture->Lock))
		{
			return false;
		}
		TestEqual(TEXT("an unresolvable Intrusion shot still opens the session"),
			Fixture->World->BeginPlayerUseSession(Fixture->Lock->Handle, Fixture->Player).Outcome,
			EElysiumUseOutcome::SessionStarted);
		TestEqual(TEXT("nothing was adopted into the cine slot"),
			Fixture->World->CineCameraShotId(), 0);
		TestFalse(TEXT("... and the slot is empty"), Fixture->World->HasScriptedCamera());
		TestNull(TEXT("no camera entity survived the failed SetShot"),
			AdoptedCine(*Fixture->World));
		TestFalse(TEXT("the hold is unconditional — it is not gated on the adoption"),
			Fixture->PlayerEntity->IsMobile());
		TestTrue(TEXT("and the timed attempt is running"), Fixture->Lock->AttemptUser.IsSet());
		Fixture->World->EndPlayerUseSession(Fixture->Lock->Handle, EElysiumUseEndReason::Released);
		TestTrue(TEXT("the closer still mobilizes"), Fixture->PlayerEntity->IsMobile());
	}

	// --- 4. Slot 37's 80 units picks an ARM of the maintenance body, and ends nothing --------------
	{
		ElysiumCameraShots::FlushCache();
		InstallIntrusion();
		TUniquePtr<FLockWorld> Fixture = StandLock(TEXT("prop_doorknob"),
			TEXT("func_door_rotating"), LockOrigin);
		if (!TestNotNull(TEXT("the reach doorknob resolves"), Fixture->Lock))
		{
			return false;
		}
		FElysiumRecordingServices& Services = Fixture->Services;
		Services.BodyAttachments.Add(FName(TEXT("camera_position")), FTransform(CameraPositionCm));
		Services.BodyAttachments.Add(FName(TEXT("camera_target")), FTransform(CameraTargetCm));
		// Retail measures against the held entity's own collision box, so give it one: a 20 cm cube
		// on the lock's origin. The reach itself is `80.0 * 2.54 = 203.2 cm`.
		Services.bHasUseBodyBounds = true;
		Services.UseBodyBounds = FBox(LockOrigin - FVector(10.0), LockOrigin + FVector(10.0));
		// Floor under the standing spot, so the 1024-unit probe finds ground.
		FElysiumRecordingServices::FCameraHullBlocker Floor;
		Floor.PointCm = FVector(StandCm.X, StandCm.Y, 40.0);
		Floor.RadiusCm = 32.0f;
		Services.CameraHullBlockers.Add(Floor);
		Services.bPlayerSweepMoves = true;
		Services.PlayerSweepContact = FVector(StandCm.X, StandCm.Y, 60.0);

		TestEqual(TEXT("the reach session opens"),
			Fixture->World->BeginPlayerUseSession(Fixture->Lock->Handle, Fixture->Player).Outcome,
			EElysiumUseOutcome::SessionStarted);

		// (a) FAR — the eye is 400 cm out, the nearest point on the box is at x = 10, so the
		//     manhattan XY distance is 390 cm, past the 203.2 cm reach.
		const int32 SweepsBefore = Services.Count(TEXT("SweepPlayerHullToward"));
		Fixture->Lock->TickPlayerUse(HeldContext(*Fixture, LockOrigin + FVector(400.0, 0.0, 0.0)));
		TestEqual(TEXT("beyond the reach, slot 43 sweeps the player in"),
			Services.Count(TEXT("SweepPlayerHullToward")), SweepsBefore + 1);
		// The whole point of the case: nothing here ends anything.
		TestTrue(TEXT("straying past 80 units does NOT end the session — the attempt survives"),
			Fixture->Lock->AttemptUser.IsSet());
		TestTrue(TEXT("... and neither does the camera"), Fixture->World->HasScriptedCamera());
		TestFalse(TEXT("... nor the hold"), Fixture->PlayerEntity->IsMobile());

		// (b) NEAR with the pick carried but NOT active — 20 cm of manhattan distance. Retail's
		//     middle arm switches to it (`player+0x724`) and then runs slot 40, the same placement.
		TestNull(TEXT("acquisition alone does not make a non-wieldable pick active"),
			Fixture->PlayerEntity->Inventory.Active(*Fixture->PlayerEntity));
		const int32 SweepsBeforeEquip = Services.Count(TEXT("SweepPlayerHullToward"));
		Fixture->Lock->TickPlayerUse(HeldContext(*Fixture, LockOrigin + FVector(30.0, 0.0, 0.0)));
		FElysiumItem* NowActive = Fixture->PlayerEntity->Inventory.Active(*Fixture->PlayerEntity);
		if (TestNotNull(TEXT("inside the reach the maintenance arm equips the lockpick"), NowActive))
		{
			TestEqual(TEXT("... and it is slot 36's classname"), NowActive->ClassName(),
				FString(TEXT("item_g_lockpick")));
		}
		TestEqual(TEXT("the equip arm then runs slot 40, the same placement body"),
			Services.Count(TEXT("SweepPlayerHullToward")), SweepsBeforeEquip + 1);

		// (c) NEAR with the pick now active — slot 41's per-tick half is the view snap at the lock's
		//     `WorldSpaceCenter()` (vfunc 0x300), the collision box's centre and not an attachment,
		//     and it moves nobody.
		const int32 SweepsBeforeRoll = Services.Count(TEXT("SweepPlayerHullToward"));
		const int32 SnapsBeforeRoll = Services.Count(TEXT("SnapPlayerViewTo"));
		Fixture->Lock->TickPlayerUse(HeldContext(*Fixture, LockOrigin + FVector(30.0, 0.0, 0.0)));
		TestEqual(TEXT("slot 41 snaps the view at the box centre"),
			Services.Count(FString::Printf(TEXT("SnapPlayerViewTo %s"),
				*Services.UseBodyBounds.GetCenter().ToCompactString())), 1);
		TestEqual(TEXT("... exactly once more"), Services.Count(TEXT("SnapPlayerViewTo")),
			SnapsBeforeRoll + 1);
		TestEqual(TEXT("and slot 41 never moves the player"),
			Services.Count(TEXT("SweepPlayerHullToward")), SweepsBeforeRoll);

		Fixture->World->EndPlayerUseSession(Fixture->Lock->Handle, EElysiumUseEndReason::Released);
	}

	// --- 5. `FUN_10224440`: the standing axis, the ground probe and the facing ---------------------
	{
		ElysiumCameraShots::FlushCache();
		InstallIntrusion();
		TUniquePtr<FLockWorld> Fixture = StandLock(TEXT("prop_doorknob"),
			TEXT("func_door_rotating"), LockOrigin);
		if (!TestNotNull(TEXT("the placement doorknob resolves"), Fixture->Lock))
		{
			return false;
		}
		FElysiumRecordingServices& Services = Fixture->Services;
		Services.BodyAttachments.Add(FName(TEXT("camera_position")), FTransform(CameraPositionCm));
		Services.BodyAttachments.Add(FName(TEXT("camera_target")), FTransform(CameraTargetCm));
		Services.bHasUseBodyBounds = false;   // no box: the reach test degenerates to the far arm

		// (a) No floor under the standing spot -> `fraction == 1` -> retail refuses to move at all.
		const FVector StartLocation = Services.PlayerLocation;
		Fixture->Lock->TickPlayerUse(HeldContext(*Fixture, LockOrigin + FVector(400.0, 0.0, 0.0)));
		TestEqual(TEXT("no solid ground over the lockpick position: nothing is swept"),
			Services.Count(TEXT("SweepPlayerHullToward")), 0);
		TestEqual(TEXT("... and nothing is turned"), Services.Count(TEXT("SnapPlayerViewTo")), 0);
		TestEqual(TEXT("... so the player has not moved"), Services.PlayerLocation, StartLocation);
		TestEqual(TEXT("the probe ran from the 31-unit standing spot, straight down 1024 units"),
			Services.Count(FString::Printf(TEXT("TraceCameraHull %s -> %s"), *StandCm.ToString(),
				*ProbeEndCm.ToString())), 1);

		// (b) The hull already inside geometry -> `startsolid` -> the other refusal.
		FElysiumRecordingServices::FCameraHullBlocker Wall;
		Wall.PointCm = StandCm;
		Wall.RadiusCm = 32.0f;
		Services.CameraHullBlockers.Add(Wall);
		Fixture->Lock->TickPlayerUse(HeldContext(*Fixture, LockOrigin + FVector(400.0, 0.0, 0.0)));
		TestEqual(TEXT("too close to solid geometry to pick: still nothing is swept"),
			Services.Count(TEXT("SweepPlayerHullToward")), 0);
		Services.CameraHullBlockers.Reset();

		// (c) Floor found -> sweep the player's hull to it, then face the `camera_position`
		//     attachment. That last step is `LookAtEntity(this, false)`, whose target for a lock is
		//     slot 193 `CPropDoorknob::vfunc193` `FUN_102243c0` = the `camera_position` attachment.
		FElysiumRecordingServices::FCameraHullBlocker Floor;
		Floor.PointCm = FVector(StandCm.X, StandCm.Y, 40.0);
		Floor.RadiusCm = 32.0f;
		Services.CameraHullBlockers.Add(Floor);
		Services.bPlayerSweepMoves = true;
		Services.PlayerSweepContact = FVector(StandCm.X, StandCm.Y, 60.0);
		Fixture->Lock->TickPlayerUse(HeldContext(*Fixture, LockOrigin + FVector(400.0, 0.0, 0.0)));
		TestEqual(TEXT("the placement sweeps the player's hull once"),
			Services.Count(TEXT("SweepPlayerHullToward")), 1);
		TestEqual(TEXT("the pawn lands on the sweep's contact"), Services.PlayerLocation,
			Services.PlayerSweepContact);
		TestEqual(TEXT("and the view is snapped onto the camera_position attachment"),
			Services.Count(FString::Printf(TEXT("SnapPlayerViewTo %s"),
				*CameraPositionCm.ToCompactString())), 1);
		TestEqual(TEXT("... which is where the player is now facing"), Services.PlayerRotation,
			(CameraPositionCm - Services.PlayerSweepContact).Rotation());

		// (d) A model with neither attachment: retail degrades to centre-to-centre behind its DevMsg
		//     and still places, rather than refusing. Both centres are `WorldSpaceCenter()`, which
		//     for a bodiless entity is VtMB's standing hull on its origin — the same fallback the
		//     `Center` anchor takes — and the facing follows `camera_position`'s own fallback to it.
		Services.BodyAttachments.Remove(FName(TEXT("camera_position")));
		Services.BodyAttachments.Remove(FName(TEXT("camera_target")));
		const FVector LockCentre =
			ElysiumCameraShots::SurroundingBounds(*Fixture->Lock).GetCenter();
		const FVector UserCentre =
			ElysiumCameraShots::SurroundingBounds(*Fixture->PlayerEntity).GetCenter();
		FVector FallbackAxis = LockCentre - UserCentre;
		FallbackAxis.Z = 0.0;
		const FVector FallbackStand =
			Fixture->Lock->Origin + FallbackAxis.GetSafeNormal() * (31.0f * 2.54f);
		Services.CameraHullBlockers.Reset();
		FElysiumRecordingServices::FCameraHullBlocker FallbackFloor;
		FallbackFloor.PointCm = FVector(FallbackStand.X, FallbackStand.Y, 40.0);
		FallbackFloor.RadiusCm = 32.0f;
		Services.CameraHullBlockers.Add(FallbackFloor);
		const int32 SnapsBefore = Services.Count(TEXT("SnapPlayerViewTo"));
		Fixture->Lock->TickPlayerUse(HeldContext(*Fixture, LockOrigin + FVector(400.0, 0.0, 0.0)));
		TestEqual(TEXT("a lock with no attachments still turns the player, at its own centre"),
			Services.Count(FString::Printf(TEXT("SnapPlayerViewTo %s"),
				*LockCentre.ToCompactString())), 1);
		TestEqual(TEXT("... exactly once more"), Services.Count(TEXT("SnapPlayerViewTo")),
			SnapsBefore + 1);
	}

	return true;
}

} // namespace ElysiumInteractionTests

#endif // WITH_DEV_AUTOMATION_TESTS
