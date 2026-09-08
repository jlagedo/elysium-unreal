// Content-free Substrate automation: new-game entry, map activation, touch admission, frame order, registry, and base I/O chain.
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
#include "Substrate/ElysiumStealth.h"        // the player think's 0.1 s heartbeat
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Scripting/ElysiumPythonVM.h"
#include "ElysiumViewState.h"
#include "Visual/ElysiumRopes.h"
#include "Scripting/ElysiumScriptFS.h"
#include "ElysiumScriptHost.h"
#include "Scripting/ElysiumScriptNatives.h"
#include "Tests/ElysiumEntityDebugStateTestHelpers.h"
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
namespace ElysiumEntityLifecycleTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;


// The frame order, asserted at both levels it is declared
// at: the engine tick table (tick groups + the pause split, read off the class defaults —
// the late-bound prerequisites are wired at registration and belong to the Play tier), and
// the substrate's own two-pass drive inside one frame.
//
// The order is RETAIL's, and it is move-FIRST (RE21): the engine runs the whole
// ProcessUsercmds -> CPlayerMove::RunCommand chain while draining the client's `clc_move`
// message, strictly before SV_Frame calls GameFrame — so the pawn has already moved by the
// time the first think or queued event runs.


// The preset New Game entries. Held as data so registration and the requests they build are
// assertable without a game instance — every row must name a verb and an entry point, spell each
// verb once across the whole table, and survive the trip through MakeNewGameRequest intact.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNewGameEntriesTest,
	"Elysium.Substrate.NewGameEntries", GElysiumTestFlags)
bool FElysiumNewGameEntriesTest::RunTest(const FString&)
{
	const FElysiumNewGameRequest Mock =
		ElysiumStory::MakeMockCharacterRequest(TEXT("tutorial"));
	TestEqual(TEXT("the shared mock is Malkavian"), Mock.Clan,
		FElysiumSheet::ClanFromName(TEXT("Malkavian")));
	TestFalse(TEXT("the shared mock is female"), Mock.bMale);
	TestEqual(TEXT("the shared mock takes the retail Completely Batshit History row"),
		Mock.HistoryId, 63);
	TestEqual(TEXT("the shared mock enters the tutorial by request"),
		Mock.EntryPoint, FString(TEXT("tutorial")));
	TestTrue(TEXT("the shared mock keeps the authored baseline allocation"), Mock.Spends.IsEmpty());

	TSet<FString> Verbs;
	for (const ElysiumStory::FElysiumNewGameEntry& Entry : ElysiumStory::NewGameEntryTable())
	{
		TestNotNull(TEXT("every entry names a verb"), Entry.Verb);
		if (Entry.Verb == nullptr)
		{
			continue;
		}
		const FString Verb(Entry.Verb);
		TestFalse(*FString::Printf(TEXT("verb '%s' is registered once"), *Verb), Verbs.Contains(Verb));
		Verbs.Add(Verb);
		if (Entry.Alias != nullptr)
		{
			const FString Alias(Entry.Alias);
			TestFalse(*FString::Printf(TEXT("alias '%s' is registered once"), *Alias),
				Verbs.Contains(Alias));
			Verbs.Add(Alias);
		}
		TestNotNull(*FString::Printf(TEXT("'%s' names an entry point"), *Verb), Entry.EntryPoint);
		TestNotNull(*FString::Printf(TEXT("'%s' carries help"), *Verb), Entry.Help);

		const FElysiumNewGameRequest Request = ElysiumStory::MakeNewGameRequest(Entry);
		TestEqual(*FString::Printf(TEXT("'%s' carries its entry point"), *Verb),
			Request.EntryPoint, FString(Entry.EntryPoint ? Entry.EntryPoint : TEXT("")));
		TestEqual(*FString::Printf(TEXT("'%s' carries its replay intent"), *Verb),
			Request.bReplayEntryMap, Entry.bReplayEntryMap);
		TestEqual(*FString::Printf(TEXT("'%s' uses the shared mock clan"), *Verb),
			Request.Clan, Mock.Clan);
		TestEqual(*FString::Printf(TEXT("'%s' uses the shared mock sex"), *Verb),
			Request.bMale, Mock.bMale);
		TestEqual(*FString::Printf(TEXT("'%s' uses the shared mock history"), *Verb),
			Request.HistoryId, Mock.HistoryId);
		TestEqual(*FString::Printf(TEXT("'%s' uses the shared baseline allocation"), *Verb),
			Request.Spends.Num(), Mock.Spends.Num());
		for (const TPair<FName, int32>& Spend : Mock.Spends)
		{
			TestEqual(*FString::Printf(TEXT("'%s' shares the %s allocation"),
				*Verb, *Spend.Key.ToString()), Request.Spends.FindRef(Spend.Key), Spend.Value);
		}
	}

	// The theatre replay is the one preset the opening-scene QA loop drives; it must keep both
	// spellings and must ask for the map's state to be forgotten, or its trigger_once stays spent.
	const ElysiumStory::FElysiumNewGameEntry* Theatre =
		ElysiumStory::NewGameEntryTable().FindByPredicate(
			[](const ElysiumStory::FElysiumNewGameEntry& E)
			{ return E.Verb != nullptr && FString(E.Verb) == TEXT("elysium.newgame_ttd"); });
	if (TestNotNull(TEXT("the theatre replay entry exists"), Theatre))
	{
		TestEqual(TEXT("it keeps its compact alias"), FString(Theatre->Alias), FString(TEXT("newgame_ttd")));
		TestTrue(TEXT("it replays its entry map"), Theatre->bReplayEntryMap);
	}
	return true;
}

// The map-epoch boundary (S4). The mint and the stale-retire rule are a plain value precisely so
// this can assert them without a world, and the broadcast contract is asserted over a real delegate
// with counting subscribers: what an application-lifetime subsystem actually binds to.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapEpochTest,
	"Elysium.Substrate.MapEpoch", GElysiumTestFlags)
bool FElysiumMapEpochTest::RunTest(const FString&)
{
	FElysiumMapEpoch Epochs;
	TestEqual(TEXT("no epoch is live before the first map"), Epochs.Current(), uint64(0));
	TestFalse(TEXT("epoch 0 never retires"), Epochs.ShouldRetire(0));

	const uint64 First = Epochs.Begin();
	const uint64 Second = Epochs.Begin();
	TestTrue(TEXT("a minted epoch is never 0"), First != 0);
	TestTrue(TEXT("epochs are strictly increasing"), Second > First);
	TestEqual(TEXT("the newest mint is the live epoch"), Epochs.Current(), Second);

	// Hard travel overlaps two worlds: the outgoing actor's EndPlay can run after the incoming epoch
	// is minted, and that late retire must not free the new map's state.
	TestFalse(TEXT("a stale epoch does not retire"), Epochs.ShouldRetire(First));
	Epochs.Retire(First);
	TestEqual(TEXT("a stale retire leaves the live epoch alone"), Epochs.Current(), Second);

	TestTrue(TEXT("the live epoch retires"), Epochs.ShouldRetire(Second));
	Epochs.Retire(Second);
	TestEqual(TEXT("retiring clears the live epoch"), Epochs.Current(), uint64(0));
	TestFalse(TEXT("retiring twice is a no-op"), Epochs.ShouldRetire(Second));

	// Every subscriber sees exactly one begin and one retire per cycle, over as many cycles as a
	// session has travels — the property the leak this boundary exists to close depends on.
	FElysiumMapEpoch Cycles;
	FOnElysiumMapEpochBegin Began;
	FOnElysiumMapEpochRetired Retired;
	int32 BeginCounts[2] = { 0, 0 };
	int32 RetireCounts[2] = { 0, 0 };
	uint64 LastRetired = 0;
	for (int32 Subscriber = 0; Subscriber < 2; ++Subscriber)
	{
		Began.AddLambda([&BeginCounts, Subscriber](uint64) { ++BeginCounts[Subscriber]; });
		Retired.AddLambda([&RetireCounts, &LastRetired, Subscriber](uint64 Epoch)
		{
			++RetireCounts[Subscriber];
			LastRetired = Epoch;
		});
	}

	constexpr int32 CycleCount = 8;
	for (int32 Cycle = 0; Cycle < CycleCount; ++Cycle)
	{
		const uint64 Epoch = Cycles.Begin();
		Began.Broadcast(Epoch);
		// The subsystem broadcasts only a retire that actually happened, so a repeat is silent.
		for (int32 Attempt = 0; Attempt < 2; ++Attempt)
		{
			if (Cycles.ShouldRetire(Epoch))
			{
				Cycles.Retire(Epoch);
				Retired.Broadcast(Epoch);
			}
		}
		TestEqual(TEXT("the retire carries the epoch that closed"), LastRetired, Epoch);
	}
	for (int32 Subscriber = 0; Subscriber < 2; ++Subscriber)
	{
		TestEqual(TEXT("one begin per cycle per subscriber"), BeginCounts[Subscriber], CycleCount);
		TestEqual(TEXT("one retire per cycle per subscriber"), RetireCounts[Subscriber], CycleCount);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapReadinessTest,
	"Elysium.Substrate.MapReadiness", GElysiumTestFlags)
bool FElysiumMapReadinessTest::RunTest(const FString&)
{
	FString Failure;
	FElysiumMapRuntimePrerequisites Gameplay;

	// Prerequisites are independent observations, not an assumed tick order. Complete the
	// player-facing half first and prove the gate still waits for construction/collision.
	Gameplay.bPossessedPawnReady = true;
	Gameplay.bPlayerBodyReady = true;
	Gameplay.bFinalPlacementReady = true;
	Gameplay.bTickPrerequisitesReady = true;
	TestEqual(TEXT("out-of-order partial completion waits"),
		Gameplay.Evaluate(0.5, Failure), EElysiumMapReadinessResult::Waiting);

	Gameplay.bCollisionReady = true;
	Gameplay.bSpawnTransformReady = true;
	Gameplay.bPlayerEntityReady = true;
	Gameplay.bEntityWorldReady = true;
	Gameplay.bAnimationPreloadReady = true;
	TestEqual(TEXT("construction has not completed yet"),
		Gameplay.Evaluate(1.0, Failure), EElysiumMapReadinessResult::Waiting);
	Gameplay.bConstructionComplete = true;
	TestEqual(TEXT("all gameplay prerequisites open the gate in any completion order"),
		Gameplay.Evaluate(1.0, Failure), EElysiumMapReadinessResult::Ready);

	FElysiumMapRuntimePrerequisites Backdrop;
	Backdrop.bMenuBackdrop = true;
	Backdrop.bConstructionComplete = true;
	Backdrop.bEntityWorldReady = true;
	Backdrop.bAnimationPreloadReady = true;
	Backdrop.bCollisionReady = true;
	TestEqual(TEXT("a backdrop omits every pawn prerequisite"),
		Backdrop.Evaluate(0.0, Failure), EElysiumMapReadinessResult::Ready);

	FElysiumMapRuntimePrerequisites CollisionFailure = Gameplay;
	CollisionFailure.bCollisionReady = false;
	CollisionFailure.bCollisionFailed = true;
	TestEqual(TEXT("a collision failure never opens the gate"),
		CollisionFailure.Evaluate(0.0, Failure), EElysiumMapReadinessResult::Failed);
	TestTrue(TEXT("collision failure is structured"), Failure.Contains(TEXT("collision")));

	FElysiumMapRuntimePrerequisites Timeout = Gameplay;
	Timeout.bPossessedPawnReady = false;
	Timeout.bFinalPlacementReady = false;
	TestEqual(TEXT("an incomplete map waits before the watchdog"),
		Timeout.Evaluate(FElysiumMapRuntimePrerequisites::WatchdogSeconds - 0.01, Failure),
		EElysiumMapReadinessResult::Waiting);
	TestEqual(TEXT("the watchdog fails closed"),
		Timeout.Evaluate(FElysiumMapRuntimePrerequisites::WatchdogSeconds, Failure),
		EElysiumMapReadinessResult::Failed);
	TestTrue(TEXT("watchdog reason names missing prerequisites"),
		Failure.Contains(TEXT("possessed player pawn"))
		&& Failure.Contains(TEXT("final player placement")));

	FElysiumMapRuntimePrerequisites MissingAnimations = Gameplay;
	MissingAnimations.bAnimationPreloadReady = false;
	TestEqual(TEXT("a completed build cannot activate before its map animations are resident"),
		MissingAnimations.Evaluate(0.0, Failure), EElysiumMapReadinessResult::Failed);
	TestTrue(TEXT("animation residency failure is structured"),
		Failure.Contains(TEXT("animation residency")));
	MissingAnimations.bAnimationPreloadPending = true;
	TestEqual(TEXT("an active native request waits behind the activation barrier"),
		MissingAnimations.Evaluate(0.0, Failure), EElysiumMapReadinessResult::Waiting);
	TestEqual(TEXT("native asset loading can exceed the old short prerequisite window"),
		MissingAnimations.Evaluate(20.0, Failure), EElysiumMapReadinessResult::Waiting);
	TestEqual(TEXT("native loading still has a bounded failure deadline"),
		MissingAnimations.Evaluate(120.0, Failure), EElysiumMapReadinessResult::Failed);
	MissingAnimations.bAnimationPreloadPending = false;
	MissingAnimations.bAnimationPreloadReady = true;
	TestEqual(TEXT("completed native loading admits the same ready state"),
		MissingAnimations.Evaluate(20.0, Failure), EElysiumMapReadinessResult::Ready);

	FElysiumMapRuntimePrerequisites MissingSubstrate = Backdrop;
	MissingSubstrate.bEntityWorldReady = false;
	TestEqual(TEXT("a completed build with no substrate fails immediately"),
		MissingSubstrate.Evaluate(0.0, Failure), EElysiumMapReadinessResult::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumActivationLifecycleTest,
	"Elysium.Substrate.ActivationLifecycle", GElysiumTestFlags)
bool FElysiumActivationLifecycleTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__activation_lifecycle__");

	auto AddWire = [](FElysiumEntityDef& Source, const TCHAR* Output,
		const TCHAR* Target, const TCHAR* Input, const TCHAR* Param)
	{
		FElysiumOutputDef Wire;
		Wire.Name = Output;
		Wire.Target = Target;
		Wire.Input = Input;
		Wire.Param = Param;
		Wire.Times = -1;
		Source.Outputs.Add(MoveTemp(Wire));
	};

	FElysiumEntityDef Auto;
	Auto.Classname = TEXT("logic_auto");
	Auto.TargetName = TEXT("auto1");
	AddWire(Auto, TEXT("OnMapLoad"), TEXT("counter1"), TEXT("Add"), TEXT("3"));
	Defs.Defs.Add(MoveTemp(Auto));

	FElysiumEntityDef Timer;
	Timer.Classname = TEXT("logic_timer");
	Timer.TargetName = TEXT("timer1");
	Timer.Keys.Add(TEXT("RefireTime"), TEXT("1"));
	AddWire(Timer, TEXT("OnTimer"), TEXT("counter1"), TEXT("Add"), TEXT("11"));
	Defs.Defs.Add(MoveTemp(Timer));

	FElysiumEntityDef Trigger;
	Trigger.Classname = TEXT("trigger_multiple");
	Trigger.TargetName = TEXT("trigger1");
	Trigger.Keys.Add(TEXT("spawnflags"), TEXT("1")); // ALLOW_CLIENTS
	AddWire(Trigger, TEXT("OnStartTouch"), TEXT("counter1"), TEXT("Add"), TEXT("7"));
	Defs.Defs.Add(MoveTemp(Trigger));

	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Defs.Defs.Add(MoveTemp(Counter));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	FElysiumEntity* AutoEntity = World.FindByName(TEXT("auto1"));
	if (!TestNotNull(TEXT("logic_auto resolved"), AutoEntity))
	{
		return false;
	}
	TestTrue(TEXT("logic_auto used the registered leaf factory"),
		AutoEntity->Class && !AutoEntity->Class->bStub && !AutoEntity->IsRecordOnly());
	TestEqual(TEXT("logic_auto Spawn armed the first think"), AutoEntity->NextThink, 0.0f);
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	FElysiumEntity* TriggerEntity = World.FindByName(TEXT("trigger1"));
	FElysiumEntity* CounterEntity = World.FindByName(TEXT("counter1"));
	if (!TestNotNull(TEXT("trigger resolved"), TriggerEntity)
		|| !TestNotNull(TEXT("counter resolved"), CounterEntity))
	{
		return false;
	}

	auto CounterValue = [](const FElysiumEntity* Entity)
	{
		return ElysiumEntityDebugTest::CounterValue(Entity);
	};

	// Construction may queue map-entry work, but every gameplay drive and physical ingress is
	// inert until the map actor opens the gate.
	World.EnqueueInput(TEXT("counter1"), FName(TEXT("Add")), FElysiumVariant::Int(5), 0.0,
		Player, Player);
	for (int32 i = 0; i < 3; ++i)
	{
		World.RunPlayerThink(10.0 + i);
		World.Tick(10.0 + i);
		World.RouteBrushTouch(TriggerEntity->Handle, Player, true);
	}
	TestFalse(TEXT("Load leaves the world dormant"), World.IsActive());
	TestEqual(TEXT("dormant thinks, timers and events do not run"), CounterValue(CounterEntity), 0.0f);
	TestEqual(TEXT("dormant overlap callbacks are forgotten"), World.TouchBegins(), 0);
	TestEqual(TEXT("the queued input remains pending"), World.Queue().Num(), 1);

	World.Activate(0.0);
	World.Activate(99.0); // idempotent: must not move the frozen activation time or replay anything
	TestTrue(TEXT("Activate opens the gate"), World.IsActive());
	TestEqual(TEXT("a second Activate does not change now"), World.NowSeconds(), 0.0);

	// This mirrors the map actor's activation transaction: reconcile final containment, then run
	// player-think and the ordinary think-first/event-queue pass at the unchanged game time.
	World.RouteBrushTouch(TriggerEntity->Handle, Player, true);
	World.RouteBrushTouch(TriggerEntity->Handle, Player, true);
	World.RunPlayerThink(0.0);
	World.Tick(0.0);
	TestEqual(TEXT("initial auto, queued input and touch complete in the activation pass"),
		CounterValue(CounterEntity), 15.0f);
	TestEqual(TEXT("activation containment emits exactly one begin"), World.TouchBegins(), 1);

	World.Tick(1.0);
	TestEqual(TEXT("timers start only after activation"), CounterValue(CounterEntity), 26.0f);
	World.RouteBrushTouch(TriggerEntity->Handle, Player, false);
	World.RouteBrushTouch(TriggerEntity->Handle, Player, true);
	World.Tick(1.0);
	TestEqual(TEXT("a genuine exit and re-entry emits a new edge"), CounterValue(CounterEntity), 33.0f);
	TestEqual(TEXT("one exit was recorded"), World.TouchEnds(), 1);
	TestEqual(TEXT("the re-entry is the second begin"), World.TouchBegins(), 2);

	// `elysium.trigger off` is a global exploration gate, broader than ent_pause: overlap ingress,
	// entity automation and queued I/O all hold together. It is intentionally reversible, so map
	// load work queued while off starts only when the owner explicitly re-enables it.
	FElysiumEntityWorld::SetTriggerResolutionEnabled(false);
	World.EnqueueInput(TEXT("counter1"), FName(TEXT("Add")), FElysiumVariant::Int(5), 0.0,
		Player, Player);
	World.RouteBrushTouch(TriggerEntity->Handle, Player, false);
	World.RouteBrushTouch(TriggerEntity->Handle, Player, true);
	World.RunPlayerThink(10.0);
	World.Tick(10.0);
	TestEqual(TEXT("trigger-off holds entity automation and queued I/O"), CounterValue(CounterEntity), 33.0f);
	TestEqual(TEXT("trigger-off ignores proximity ingress"), World.TouchBegins(), 2);
	TestEqual(TEXT("trigger-off keeps queued work for an explicit resume"), World.Queue().Num(), 1);

	FElysiumEntityWorld::SetTriggerResolutionEnabled(true);
	World.Tick(10.0);
	TestEqual(TEXT("trigger-on resumes the held timer and queued I/O"), CounterValue(CounterEntity), 49.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTriggerPawnIdentityTest,
	"Elysium.Substrate.TriggerPawnIdentity", GElysiumTestFlags)
bool FElysiumTriggerPawnIdentityTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__trigger_pawn_identity__");
	FElysiumEntityDef TriggerDef;
	TriggerDef.Classname = TEXT("trigger_once");
	TriggerDef.TargetName = TEXT("trig_dialog_outside_chopshop");
	TriggerDef.Keys.Add(TEXT("spawnflags"), TEXT("1")); // ALLOW_CLIENTS only
	TriggerDef.Keys.Add(TEXT("StartDisabled"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(TriggerDef));
	FElysiumEntityDef JackDef;
	JackDef.Classname = TEXT("npc_VVampire");
	JackDef.TargetName = TEXT("Jack");
	Defs.Defs.Add(MoveTemp(JackDef));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumEntity* Trigger = World.FindByName(TEXT("trig_dialog_outside_chopshop"));
	FElysiumEntity* Jack = World.FindByName(TEXT("Jack"));
	if (!TestNotNull(TEXT("post-feed trigger resolved"), Trigger)
		|| !TestNotNull(TEXT("Jack resolved"), Jack))
	{
		return false;
	}

	World.EnqueueInput(TEXT("trig_dialog_outside_chopshop"), FName(TEXT("Enable")),
		FElysiumVariant::Void(), 0.0, Jack->Handle, Jack->Handle);
	World.Tick(0.0);
	World.RouteBrushTouch(Trigger->Handle, Jack->Handle, /*bBegin*/ true);
	TestEqual(TEXT("Jack's refreshed overlap fails the client-only gate"), World.TouchBegins(), 0);
	World.RouteBrushTouch(Trigger->Handle, Player, /*bBegin*/ true);
	TestEqual(TEXT("the real player's later approach supplies the first accepted touch"),
		World.TouchBegins(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisabledTouchAdmissionTest,
	"Elysium.Substrate.DisabledTouchAdmission", GElysiumTestFlags)
bool FElysiumDisabledTouchAdmissionTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__disabled_touch__");
	FElysiumEntityDef TriggerDef;
	TriggerDef.Classname = TEXT("trigger_multiple");
	TriggerDef.TargetName = TEXT("trigger1");
	TriggerDef.Keys.Add(TEXT("spawnflags"), TEXT("1"));
	TriggerDef.Keys.Add(TEXT("StartDisabled"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(TriggerDef));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumEntity* Trigger = World.FindByName(TEXT("trigger1"));
	if (!TestNotNull(TEXT("disabled trigger resolved"), Trigger))
	{
		return false;
	}

	// A rejected begin must not occupy the deduplication set. Once Enable restores collision, the
	// engine's refreshed overlap is admitted as a new edge.
	World.RouteBrushTouch(Trigger->Handle, Player, /*bBegin*/ true);
	TestEqual(TEXT("a disabled begin is rejected before deduplication"), World.TouchBegins(), 0);
	World.EnqueueInput(TEXT("trigger1"), FName(TEXT("Enable")), FElysiumVariant::Void(), 0.0,
		Player, Player);
	World.Tick(0.0);
	World.RouteBrushTouch(Trigger->Handle, Player, /*bBegin*/ true);
	TestEqual(TEXT("enable-time overlap refresh produces one begin"), World.TouchBegins(), 1);

	// Disabling clears the retained pair before physical collision is removed. A late raw end is a
	// no-op, and a later re-enable can therefore create another genuine begin.
	World.EnqueueInput(TEXT("trigger1"), FName(TEXT("Disable")), FElysiumVariant::Void(), 0.0,
		Player, Player);
	World.Tick(0.0);
	TestEqual(TEXT("disable deterministically clears the active pair"), World.TouchEnds(), 1);
	World.RouteBrushTouch(Trigger->Handle, Player, /*bBegin*/ false);
	TestEqual(TEXT("a late physical end is deduplicated"), World.TouchEnds(), 1);
	World.RouteBrushTouch(Trigger->Handle, Player, /*bBegin*/ true);
	TestEqual(TEXT("disabled containment remains inadmissible"), World.TouchBegins(), 1);

	World.EnqueueInput(TEXT("trigger1"), FName(TEXT("Enable")), FElysiumVariant::Void(), 0.0,
		Player, Player);
	World.Tick(0.0);
	World.RouteBrushTouch(Trigger->Handle, Player, /*bBegin*/ true);
	TestEqual(TEXT("re-enable produces a fresh begin edge"), World.TouchBegins(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumEnvironmentalAudioTouchAdmissionTest,
	"Elysium.Substrate.EnvironmentalAudioTouchAdmission", GElysiumTestFlags)
bool FElysiumEnvironmentalAudioTouchAdmissionTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__environmental_audio_touch__");
	FElysiumEntityDef TriggerDef;
	TriggerDef.Classname = TEXT("trigger_environmental_audio");
	TriggerDef.TargetName = TEXT("environmental1");
	TriggerDef.Keys.Add(TEXT("spawnflags"), TEXT("1"));
	TriggerDef.Keys.Add(TEXT("StartDisabled"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(TriggerDef));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumEntity* Trigger = World.FindByName(TEXT("environmental1"));
	if (!TestNotNull(TEXT("environmental-audio trigger resolves"), Trigger))
	{
		return false;
	}
	TestTrue(TEXT("environmental audio used the registered leaf factory"),
		Trigger->Class && !Trigger->Class->bStub && !Trigger->IsRecordOnly());
	TestFalse(TEXT("StartDisabled reaches the leaf physical gate"),
		Trigger->IsBrushBodyEnabled());

	World.RouteBrushTouch(Trigger->Handle, Player, /*bBegin*/ true);
	TestEqual(TEXT("StartDisabled environmental audio rejects containment"),
		World.TouchBegins(), 0);

	World.EnqueueInput(TEXT("environmental1"), FName(TEXT("Enable")),
		FElysiumVariant::Void(), 0.0, Player, Player);
	World.Tick(0.0);
	World.RouteBrushTouch(Trigger->Handle, Player, /*bBegin*/ true);
	TestEqual(TEXT("environmental audio inherits CBaseTrigger admission"),
		World.TouchBegins(), 1);
	TestEqual(TEXT("environmental audio Enable is a real inherited input"),
		World.UnknownInputs(), 0);

	World.EnqueueInput(TEXT("environmental1"), FName(TEXT("Disable")),
		FElysiumVariant::Void(), 0.0, Player, Player);
	World.Tick(0.0);
	TestEqual(TEXT("disabling environmental audio clears its retained pair"),
		World.TouchEnds(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFilteredTouchAdmissionTest,
	"Elysium.Substrate.FilteredTouchAdmission", GElysiumTestFlags)
bool FElysiumFilteredTouchAdmissionTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__filtered_touch__");

	FElysiumEntityDef Accept;
	Accept.Classname = TEXT("filter_activator_name");
	Accept.TargetName = TEXT("accept_player");
	Accept.Keys.Add(TEXT("filtername"), TEXT("!pla*"));
	Defs.Defs.Add(MoveTemp(Accept));

	FElysiumEntityDef Reject;
	Reject.Classname = TEXT("filter_activator_name");
	Reject.TargetName = TEXT("reject_player");
	Reject.Keys.Add(TEXT("filtername"), TEXT("somebody_else"));
	Defs.Defs.Add(MoveTemp(Reject));

	FElysiumEntityDef Multi;
	Multi.Classname = TEXT("filter_multi");
	Multi.TargetName = TEXT("player_filter");
	Multi.Keys.Add(TEXT("filtertype"), TEXT("0")); // AND
	Multi.Keys.Add(TEXT("Filter01"), TEXT("accept_player"));
	Multi.Keys.Add(TEXT("Filter02"), TEXT("reject_player"));
	Defs.Defs.Add(MoveTemp(Multi));

	FElysiumEntityDef TriggerDef;
	TriggerDef.Classname = TEXT("trigger_multiple");
	TriggerDef.TargetName = TEXT("trigger1");
	TriggerDef.Keys.Add(TEXT("spawnflags"), TEXT("1"));
	TriggerDef.Keys.Add(TEXT("filtername"), TEXT("player_filter"));
	Defs.Defs.Add(MoveTemp(TriggerDef));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumEntity* Trigger = World.FindByName(TEXT("trigger1"));
	if (!TestNotNull(TEXT("filtered trigger resolved"), Trigger))
	{
		return false;
	}

	World.RouteBrushTouch(Trigger->Handle, Player, /*bBegin*/ true);
	TestEqual(TEXT("a failed name/multi filter is rejected before touch deduplication"),
		World.TouchBegins(), 0);

	World.EnqueueInput(TEXT("reject_player"), FName(TEXT("Kill")), FElysiumVariant::Void(), 0.0,
		Player, Player);
	World.Tick(0.0);
	World.RouteBrushTouch(Trigger->Handle, Player, /*bBegin*/ true);
	TestEqual(TEXT("a stale child filter passes and the previously rejected pair can begin"),
		World.TouchBegins(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFrameOrderTest, "Elysium.Substrate.FrameOrder", GElysiumTestFlags)
bool FElysiumFrameOrderTest::RunTest(const FString&)
{
	// --- the tick table: declared with tick groups, never inferred from registration order ---
	const AElysiumMapActor* Map = GetDefault<AElysiumMapActor>();
	if (!TestNotNull(TEXT("map actor class defaults"), Map))
	{
		return false;
	}

	// Steps 2-3 run before physics AND before the move: the clock advances, a freshly seated pawn is
	// placed and frozen, and the player entity's own think runs — the RunCommand shell retail wraps
	// the move in.
	TestTrue(TEXT("the pre-move pass ticks"), Map->PreMoveTickFunction.bCanEverTick);
	TestTrue(TEXT("the pre-move pass starts enabled"), Map->PreMoveTickFunction.bStartWithTickEnabled);
	TestEqual(TEXT("the pre-move pass is TG_PrePhysics"),
		static_cast<int32>(Map->PreMoveTickFunction.TickGroup), static_cast<int32>(TG_PrePhysics));

	// The primary actor tick is an explicit barrier because Unreal makes an NPC movement component
	// depend on the actor owning its current floor — this map actor. It carries no GameFrame work.
	TestTrue(TEXT("the movement-base barrier ticks"), Map->PrimaryActorTick.bCanEverTick);
	TestEqual(TEXT("the movement-base barrier is TG_PrePhysics"),
		static_cast<int32>(Map->PrimaryActorTick.TickGroup), static_cast<int32>(TG_PrePhysics));

	// Steps 5-6 also run before physics, but AFTER both player and NPC movement. A separate tick is
	// what permits the forward movement->gameplay edge without closing the floor-owner cycle.
	TestTrue(TEXT("the gameplay pass ticks"), Map->GameplayTickFunction.bCanEverTick);
	TestTrue(TEXT("the gameplay pass starts enabled"), Map->GameplayTickFunction.bStartWithTickEnabled);
	TestEqual(TEXT("the gameplay pass is TG_PrePhysics"),
		static_cast<int32>(Map->GameplayTickFunction.TickGroup), static_cast<int32>(TG_PrePhysics));

	// Step 4 — the pawn's mover is in the same group as both, for the same reason: it is ordered by
	// prerequisite, between them.
	const UElysiumMovementComponent* Mover = GetDefault<UElysiumMovementComponent>();
	if (TestNotNull(TEXT("movement component class defaults"), Mover))
	{
		TestTrue(TEXT("the mover ticks"), Mover->PrimaryComponentTick.bCanEverTick);
		TestEqual(TEXT("the mover is TG_PrePhysics, between the two gameplay passes"),
			static_cast<int32>(Mover->PrimaryComponentTick.TickGroup), static_cast<int32>(TG_PrePhysics));
	}

	// Step 8 runs after physics, on the same actor: the +use cursor traces against the frame's
	// final positions. Four tick functions, not separate actors — one actor keeps the order declarable.
	TestTrue(TEXT("the post-move pass ticks"), Map->PostMoveTickFunction.bCanEverTick);
	TestTrue(TEXT("the post-move pass starts enabled"), Map->PostMoveTickFunction.bStartWithTickEnabled);
	TestEqual(TEXT("the post-move pass is TG_PostPhysics"),
		static_cast<int32>(Map->PostMoveTickFunction.TickGroup), static_cast<int32>(TG_PostPhysics));
	TestTrue(TEXT("the post-move pass is later than both pre-physics passes"),
		static_cast<int32>(Map->PostMoveTickFunction.TickGroup)
			> static_cast<int32>(Map->PreMoveTickFunction.TickGroup));

	// And it is later than the mover too, which is what makes the post-move pass a safe place
	// to read the published body sample: by the time it runs, the mover's tick tail has written it.
	//
	// What this does **not** cover is the player visual's own ordering. `AElysiumMapActor::
	// BuildPlayerVisual` adds the mesh -> mover tick prerequisite when it attaches the visual, and a
	// per-instance prerequisite created at runtime is not reachable from a class default. It is
	// asserted by the built world or not at all; asserting an engine default here instead would be
	// asserting somebody else's ordering and calling it ours.
	if (Mover)
	{
		TestTrue(TEXT("the post-move pass is later than the mover, so the body sample is settled"),
			static_cast<int32>(Map->PostMoveTickFunction.TickGroup)
				> static_cast<int32>(Mover->PrimaryComponentTick.TickGroup));
	}

	// The cast's half of the same rule. An NPC body's actor tick carries no ordering against
	// its own CharacterMovement (the engine wires no such prerequisite; `ACharacter` orders only the
	// mesh), so a selection read from `Tick` would be reading whichever of the two happened to
	// register first. The animation pass is therefore its own tick function, in the same group as the
	// player's — declared on the class, which is what makes it assertable here at all.
	const AElysiumNpcBody* Npc = GetDefault<AElysiumNpcBody>();
	if (TestNotNull(TEXT("NPC body class defaults"), Npc))
	{
		TestTrue(TEXT("the NPC animation pass ticks"), Npc->AnimTickFunction.bCanEverTick);
		TestTrue(TEXT("the NPC animation pass starts enabled"),
			Npc->AnimTickFunction.bStartWithTickEnabled);
		TestEqual(TEXT("the NPC animation pass is TG_PostPhysics"),
			static_cast<int32>(Npc->AnimTickFunction.TickGroup), static_cast<int32>(TG_PostPhysics));
		TestFalse(TEXT("and it stops while the world is held"),
			Npc->AnimTickFunction.bTickEvenWhenPaused);
		if (const UCharacterMovementComponent* NpcMove = Npc->GetCharacterMovement())
		{
			TestTrue(TEXT("the NPC animation pass is later than its own mover, so its sample is settled"),
				static_cast<int32>(Npc->AnimTickFunction.TickGroup)
					> static_cast<int32>(NpcMove->PrimaryComponentTick.TickGroup));
		}
		// The turn-in-place stays where it was: adding the selection did not move an existing behaviour
		// a frame later.
		TestEqual(TEXT("the NPC actor tick stays in pre-physics"),
			static_cast<int32>(Npc->PrimaryActorTick.TickGroup), static_cast<int32>(TG_PrePhysics));
	}

	// Step 10 rebuilds the view state after everything that could change it has run, so it is later
	// than all four map passes.
	const UElysiumPresentationSubsystem* Present = GetDefault<UElysiumPresentationSubsystem>();
	if (TestNotNull(TEXT("presentation subsystem class defaults"), Present))
	{
		TestTrue(TEXT("the publish pass ticks"), Present->PublishTickFunction.bCanEverTick);
		TestTrue(TEXT("the publish pass starts enabled"), Present->PublishTickFunction.bStartWithTickEnabled);
		TestEqual(TEXT("the publish pass is TG_PostUpdateWork"),
			static_cast<int32>(Present->PublishTickFunction.TickGroup), static_cast<int32>(TG_PostUpdateWork));
		TestTrue(TEXT("the publish pass is last of the five"),
			static_cast<int32>(Present->PublishTickFunction.TickGroup)
				> static_cast<int32>(Map->PostMoveTickFunction.TickGroup));
	}

	// Before activation, the lifecycle-bearing passes are allowed to poll while held, but
	// their gameplay branches are phase-gated. ActivateRuntime restores their flags to false; the
	// post-move gameplay pass is never needed for readiness. Presentation remains live throughout.
	TestTrue(TEXT("the pre-move pass can poll readiness while held"), Map->PreMoveTickFunction.bTickEvenWhenPaused);
	TestTrue(TEXT("the movement-base barrier can run while held"), Map->PrimaryActorTick.bTickEvenWhenPaused);
	TestTrue(TEXT("the activation pass can run while held"), Map->GameplayTickFunction.bTickEvenWhenPaused);
	TestFalse(TEXT("the post-move pass stops when held"), Map->PostMoveTickFunction.bTickEvenWhenPaused);
	if (Present)
	{
		TestTrue(TEXT("presentation keeps publishing when held"),
			Present->PublishTickFunction.bTickEvenWhenPaused);
	}

	// The HUD itself does not tick at all: it reconciles from the publisher's OnViewPublished, which
	// is a frame later if it comes off an actor tick in TG_PrePhysics.
	const AElysiumHUD* Hud = GetDefault<AElysiumHUD>();
	if (TestNotNull(TEXT("HUD class defaults"), Hud))
	{
		TestFalse(TEXT("the HUD does not tick"), Hud->PrimaryActorTick.bCanEverTick);
	}

	// --- steps 5-6: think first, then service the queue (RE2's retail order) ---------------
	// A logic_timer due this frame fires OnTimer from its Think; the wire it fires lands in the
	// queue at zero delay. Think-first means the same tick's queue pass delivers it, so the
	// counter has moved after ONE tick. Queue-first would leave it for the next frame.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test_frame__");

	FElysiumEntityDef Timer;
	Timer.Classname = TEXT("logic_timer");
	Timer.TargetName = TEXT("timer1");
	Timer.Keys.Add(TEXT("RefireTime"), TEXT("1"));
	{
		FElysiumOutputDef Wire;
		Wire.Name = TEXT("OnTimer");
		Wire.Target = TEXT("counter1");
		Wire.Input = TEXT("Add");
		Wire.Param = TEXT("5");
		Timer.Outputs.Add(Wire);
	}
	Defs.Defs.Add(MoveTemp(Timer));

	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Defs.Defs.Add(MoveTemp(Counter));

	// Null game state, so the world's own Now() reads 0 and the tick's `Now` is whatever the
	// caller passes — which is what lets the test place the think's due time by hand.
	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* CounterEntity = World.FindByName(TEXT("counter1"));
	if (!TestNotNull(TEXT("counter1 resolved"), CounterEntity))
	{
		return false;
	}
	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		return ElysiumEntityDebugTest::CounterValue(Entity);
	};

	// Before the timer is due nothing thinks and nothing is queued.
	World.Tick(0.5);
	TestEqual(TEXT("nothing before the think is due"), CounterValue(CounterEntity), 0.f);

	// The frame the think is due: its output is delivered in that same frame's queue pass.
	World.Tick(1.0);
	TestEqual(TEXT("the think's wire lands in the same frame's queue pass"),
		CounterValue(CounterEntity), 5.f);

	// --- steps 3 vs 5: the player thinks on the OTHER side of the move ----------------------
	// Retail runs the player's own think inside CPlayerMove::RunCommand, around the move, and not
	// in Physics_RunThinkFunctions. So the world is driven twice a frame and the player is the one
	// entity the general think pass must skip — otherwise it thinks twice, and the second one lands
	// after the move instead of before it.
	FElysiumEntityDefs PlayerDefs;
	PlayerDefs.MapName = TEXT("__test_player_think__");
	FElysiumEntityWorld PlayerWorld(/*Owner*/ nullptr, /*GameState*/ nullptr);
	PlayerWorld.Load(MoveTemp(PlayerDefs));
	PlayerWorld.SpawnPlayer();
	PlayerWorld.Activate(0.0);

	FElysiumPlayer* PlayerEnt = PlayerWorld.FindPlayer();
	if (TestNotNull(TEXT("the player entity spawned"), PlayerEnt))
	{
		// Arm it due, then run the pre-move pass. `RunPlayerThink` disarms it before entering the
		// think, and the think's own tail re-arms it on the stealth surface's 0.1 s heartbeat, so a
		// FIRED player think reads as that next deadline rather than as never. The observable is the
		// deadline moving off the armed time onto the heartbeat; the pass that does not fire is the
		// one below, which leaves 1.0 exactly where the test put it.
		PlayerEnt->NextThink = 1.0f;
		PlayerWorld.RunPlayerThink(2.0);
		TestEqual(TEXT("the pre-move pass runs the player's think"), PlayerEnt->NextThink,
			static_cast<float>(2.0 + ElysiumStealth::UpdateIntervalSeconds), 1.e-4f);

		// Arm it again and run the POST-move pass instead. The general think pass skips the player,
		// so it stays armed — the pre-move pass is the only thing that may fire it.
		PlayerEnt->NextThink = 1.0f;
		PlayerWorld.Tick(2.0);
		TestEqual(TEXT("the post-move think pass leaves the player alone"),
			PlayerEnt->NextThink, 1.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHullCollisionBoundsTest,
	"Elysium.Substrate.HullCollisionBounds", GElysiumTestFlags)
bool FElysiumHullCollisionBoundsTest::RunTest(const FString&)
{
	UElysiumHullCollisionComponent* Hull = NewObject<UElysiumHullCollisionComponent>();
	if (!TestNotNull(TEXT("collision-only hull component constructs"), Hull))
	{
		return false;
	}

	const FBox Local(FVector(-10.0, -20.0, -30.0), FVector(40.0, 50.0, 60.0));
	Hull->SetLocalCollisionBounds(Local);
	const FBox World = Hull->CalcBounds(FTransform(FVector(100.0, 200.0, 300.0))).GetBox();
	TestTrue(TEXT("collision-only hull bounds remain valid without a render section"), World.IsValid != 0);
	TestEqual(TEXT("collision-only hull minimum transforms into world space"),
		World.Min, FVector(90.0, 180.0, 270.0));
	TestEqual(TEXT("collision-only hull maximum transforms into world space"),
		World.Max, FVector(140.0, 250.0, 360.0));
	return true;
}


// FElysiumClassRegistry — the case-folded base-chain walk that drives all I/O dispatch.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRegistryTest, "Elysium.Substrate.Registry", GElysiumTestFlags)
bool FElysiumRegistryTest::RunTest(const FString&)
{
	const FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();

	// The base is always registered at module load.
	const FElysiumClassDesc* Base = Registry.BaseDesc();
	if (!TestNotNull(TEXT("CBaseEntity base registered"), Base))
	{
		return false;
	}

	// A known leaf class links up to the base.
	const FElysiumClassDesc* Relay = Registry.Find(FName(TEXT("logic_relay")));
	if (!TestNotNull(TEXT("logic_relay registered"), Relay))
	{
		return false;
	}

	// Its own input resolves.
	TestNotNull(TEXT("logic_relay.Trigger resolves"),
		reinterpret_cast<const void*>(Registry.FindInput(*Relay, FName(TEXT("Trigger")))));

	// A base input resolves through the chain from the leaf (case-folded).
	TestNotNull(TEXT("inherited Kill resolves"),
		reinterpret_cast<const void*>(Registry.FindInput(*Relay, FName(TEXT("Kill")))));
	TestNotNull(TEXT("Kill folds case"),
		reinterpret_cast<const void*>(Registry.FindInput(*Relay, FName(TEXT("kILL")))));

	// A nonsense input resolves to nothing.
	TestNull(TEXT("unknown input is null"),
		reinterpret_cast<const void*>(Registry.FindInput(*Relay, FName(TEXT("NoSuchInput")))));

	// An unregistered classname is not found (the world falls it back to an inert record).
	TestNull(TEXT("unknown class not found"), Registry.Find(FName(TEXT("not_a_real_class_xyz"))));

	return true;
}


// RE29 — CGlobalEntityList::FindEntityByName's exact matching rule. Only a final `*` is special;
// matching is case-insensitive, and a bare star selects every named entity.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumEntityNameMatchTest,
	"Elysium.Substrate.EntityNameMatch", GElysiumTestFlags)
bool FElysiumEntityNameMatchTest::RunTest(const FString&)
{
	TestTrue(TEXT("exact names fold case"),
		FElysiumEntityWorld::NameMatches(TEXT("Plus_Closet"), TEXT("plus_closet")));
	TestTrue(TEXT("a trailing star is a prefix match"),
		FElysiumEntityWorld::NameMatches(TEXT("plus_Closet"), TEXT("PLUS_*")));
	TestTrue(TEXT("a bare star matches every named entity"),
		FElysiumEntityWorld::NameMatches(TEXT("anything"), TEXT("*")));
	TestFalse(TEXT("a star away from the end is literal"),
		FElysiumEntityWorld::NameMatches(TEXT("guard_alpha"), TEXT("guard*_alpha")));
	TestFalse(TEXT("an empty pattern matches nothing"),
		FElysiumEntityWorld::NameMatches(TEXT("anything"), FString()));
	TestFalse(TEXT("a nameless entity never matches"),
		FElysiumEntityWorld::NameMatches(FString(), TEXT("*")));
	return true;
}


// End-to-end I/O through a bare world: logic_relay -> math_counter, no bodies, no PIE.
// Exercises both chokepoints (AcceptInput + the event queue), FireOutput, and the
// "falsy when dead" identity contract.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumIOChainTest, "Elysium.Substrate.IOChain", GElysiumTestFlags)
bool FElysiumIOChainTest::RunTest(const FString&)
{
	// Two point entities wired in code: firing the relay's Trigger re-fires OnTrigger, which the
	// def routes to the counter's Add(5). No brush entities, so BuildBrushBody never touches the
	// (null) owner actor; GameState null means the clock reads 0 and Python no-ops — neither is
	// needed to exercise the I/O bus.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");

	FElysiumEntityDef Relay;
	Relay.Classname = TEXT("logic_relay");
	Relay.TargetName = TEXT("relay1");
	{
		FElysiumOutputDef Wire;
		Wire.Name = TEXT("OnTrigger");
		Wire.Target = TEXT("counter1");
		Wire.Input = TEXT("Add");
		Wire.Param = TEXT("5");
		Relay.Outputs.Add(Wire);
	}
	Defs.Defs.Add(MoveTemp(Relay));

	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Defs.Defs.Add(MoveTemp(Counter));

	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	TestEqual(TEXT("two entities loaded"), World.NumEntities(), 2);

	FElysiumEntity* CounterEntity = World.FindByName(TEXT("counter1"));
	FElysiumEntity* RelayEntity = World.FindByName(TEXT("relay1"));
	if (!TestNotNull(TEXT("counter1 resolved"), CounterEntity) ||
		!TestNotNull(TEXT("relay1 resolved"), RelayEntity))
	{
		return false;
	}

	// Reads the counter's live Value out of its debug-state rows (the concrete class is file-local
	// to the .cpp, so this base virtual is the seam).
	auto CounterValue = [](const FElysiumEntity* Entity) -> FString
	{
		return ElysiumEntityDebugTest::Row(Entity, TEXT("Value"));
	};

	TestEqual(TEXT("counter starts at 0"), FCString::Atof(*CounterValue(CounterEntity)), 0.0f);

	// Fire through the real queue chokepoint, addressed at the relay as `!self`.
	World.EnqueueInput(TEXT("!self"), FName(TEXT("Trigger")), FElysiumVariant::Void(), /*Delay*/ 0.0,
		FElysiumEntityHandle::Invalid(), RelayEntity->Handle);

	// Drain: every event fires at t=0 (null clock), so a couple of ticks carries the whole chain.
	for (int32 i = 0; i < 4; ++i)
	{
		World.Tick(0.0);
	}

	TestEqual(TEXT("counter advanced to 5 via the I/O chain"),
		FCString::Atof(*CounterValue(CounterEntity)), 5.0f);
	TestTrue(TEXT("history recorded the delivery"), World.RingBuffer().Num() > 0);

	// Identity: a live handle resolves; after Kill it reads falsy (the contract scripts rely on).
	const FElysiumEntityHandle CounterHandle = CounterEntity->Handle;
	TestNotNull(TEXT("live handle resolves"), World.Resolve(CounterHandle));

	World.EnqueueInput(TEXT("!self"), FName(TEXT("Kill")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), CounterHandle);
	for (int32 i = 0; i < 4; ++i)
	{
		World.Tick(0.0);
	}
	TestNull(TEXT("killed handle resolves to null"), World.Resolve(CounterHandle));

	return true;
}

} // namespace ElysiumEntityLifecycleTests

#endif // WITH_DEV_AUTOMATION_TESTS
