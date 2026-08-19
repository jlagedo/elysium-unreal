// Content-free Substrate automation: event transport ordering, late binding, trigger lifetime, wire accounting, and logical save state.
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
namespace ElysiumEntityIOTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// =====================================================================================
// The event-resolution contract — `docs/architecture/gameplay-systems-architecture.md` §2.5.1,
// over the retail facts in `docs/vtmb/entity_io.md` → "Output-list and queue order" and the two
// worked orderings in `docs/vtmb/sp_tutorial_1-event-surface.md` §5.2 / §11.5.
//
// The five rules, each with a test below:
//   1. Firing enumerates an output's repeated rows in REVERSE parsed order.       OutputRowOrder
//   2. The queue is a deadline sort with equal-time FIFO, and zero-delay work a
//      receiver produces drains breadth-first, behind the pending cohort.         QueueDrainOrder
//   3. Servicing ONE record delivers to every name match in stable entity order,
//      then runs its field-6 Python.                                              RecordServiceOrder
//   4. `times` counts down only when positive; authored 0 and -1 both mean
//      unlimited.                                                                 OutputTimes
//   5. Binding is late (resolved at service, never prebound), and missing-at-
//      service is a counted non-fatal drop with no retry.                         LateBindingAndDrops
//
// These are statements about ORDER, so they are asserted on one ordered stream: the shared
// FElysiumOrderedIOSink (Private/Tests/ElysiumTestServices.h) records every chokepoint tap into a
// single array. Everything is driven through the real chokepoints — EnqueueInput and Tick — on a
// bare world of generic logic classes.
// =====================================================================================

namespace ElysiumEventOrderTests
{
	// Producer relays carry ALLOW_FAST_RETRIGGER (spawnflag 0x2) throughout. An ordinary relay
	// queues itself an `EnableRefire` at its longest delay + 0.001 after every fire, and that
	// self-event would land in the middle of the very sequences these tests assert. The lockout is
	// its own contract with its own coverage; here it is noise.
	FElysiumEntityDef Relay(const TCHAR* Name)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("logic_relay");
		Def.TargetName = Name;
		Def.Keys.Add(TEXT("spawnflags"), TEXT("2"));
		return Def;
	}

	FElysiumEntityDef Counter(const TCHAR* Name)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("math_counter");
		Def.TargetName = Name;
		return Def;
	}

	// One authored output row, in the seven-field shape the `.ents` exporter writes.
	void Wire(FElysiumEntityDef& Source, const TCHAR* Output, const TCHAR* Target,
		const TCHAR* Input, const TCHAR* Param = TEXT(""), float Delay = 0.f,
		int32 Times = -1, const TCHAR* Python = nullptr)
	{
		FElysiumOutputDef Row;
		Row.Name = Output;
		Row.Target = Target;
		Row.Input = Input;
		Row.Param = Param;
		Row.Delay = Delay;
		Row.Times = Times;
		Row.Python = Python != nullptr ? Python : TEXT("");
		Source.Outputs.Add(MoveTemp(Row));
	}

	// The sink is owned by the world; the raw pointer stays valid for the world's lifetime.
	FElysiumOrderedIOSink* Record(FElysiumEntityWorld& World)
	{
		TUniquePtr<FElysiumOrderedIOSink> Owned = MakeUnique<FElysiumOrderedIOSink>();
		FElysiumOrderedIOSink* Raw = Owned.Get();
		World.AddSink(MoveTemp(Owned));
		return Raw;
	}

	// A math_counter's live value, off the debug-state rows (the leaf class is file-local to
	// ElysiumLogicClasses.cpp, so this base virtual is the seam). -1 means "no such counter" — a
	// value no Add(1) chain here can produce.
	float CounterValueOf(const FElysiumEntity* Entity)
	{
		if (Entity == nullptr)
		{
			return -1.f;
		}
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
	}

	// The first live match's value — the ordinary case, where the name is unique.
	float CounterValue(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		return CounterValueOf(World.FindByName(Name));
	}

	// Fire a producer's Trigger through the queue, addressed at itself — the same shape a map's
	// own wire has, so nothing bypasses chokepoint 2.
	void Trigger(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		if (FElysiumEntity* Source = World.FindByName(Name))
		{
			World.EnqueueInput(TEXT("!self"), FName(TEXT("Trigger")), FElysiumVariant::Void(),
				/*Delay*/ 0.0, FElysiumEntityHandle::Invalid(), Source->Handle);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOutputRowOrderTest,
	"Elysium.Substrate.OutputRowOrder", GElysiumTestFlags)
bool FElysiumOutputRowOrderTest::RunTest(const FString&)
{
	using namespace ElysiumEventOrderTests;

	// --- (a) repeated rows on one output resolve in REVERSE parsed order -------------------
	// `FUN_100cd6d0` PREPENDS each parsed action to the output object's list and `FireOutput`
	// walks that list head to tail, so the last authored row is the first to reach the queue
	// (entity_io.md). Three rows at equal zero delay isolate the enumeration from the sort.
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__row_order__");
		FElysiumEntityDef Source = Relay(TEXT("fanout"));
		Wire(Source, TEXT("OnTrigger"), TEXT("row_a"), TEXT("Add"), TEXT("1"));
		Wire(Source, TEXT("OnTrigger"), TEXT("row_b"), TEXT("Add"), TEXT("1"));
		Wire(Source, TEXT("OnTrigger"), TEXT("row_c"), TEXT("Add"), TEXT("1"));
		Defs.Defs.Add(MoveTemp(Source));
		Defs.Defs.Add(Counter(TEXT("row_a")));
		Defs.Defs.Add(Counter(TEXT("row_b")));
		Defs.Defs.Add(Counter(TEXT("row_c")));

		FElysiumEntityWorld World(nullptr, nullptr);
		World.Load(MoveTemp(Defs));
		FElysiumOrderedIOSink* Sink = Record(World);
		World.Activate(0.0);

		Trigger(World, TEXT("fanout"));
		World.Tick(0.0);

		const bool bQueued = Sink->AppearsInOrder(TEXT("queue"),
			{ TEXT("row_c.Add"), TEXT("row_b.Add"), TEXT("row_a.Add") });
		const bool bDelivered = Sink->AppearsInOrder(TEXT("deliver"),
			{ TEXT("row_c.Add"), TEXT("row_b.Add"), TEXT("row_a.Add") });
		TestTrue(TEXT("repeated rows enqueue in reverse parsed order"), bQueued);
		TestTrue(TEXT("...and therefore deliver last-authored first"), bDelivered);
		if (!bQueued || !bDelivered)
		{
			AddInfo(FString::Printf(TEXT("row order stream: %s"), *Sink->Log()));
		}

		TestEqual(TEXT("every row fired exactly once (row_a)"), CounterValue(World, TEXT("row_a")), 1.f);
		TestEqual(TEXT("every row fired exactly once (row_b)"), CounterValue(World, TEXT("row_b")), 1.f);
		TestEqual(TEXT("every row fired exactly once (row_c)"), CounterValue(World, TEXT("row_c")), 1.f);
		TestEqual(TEXT("the pass drained the queue"), World.Queue().Num(), 0);
	}

	// --- (b) the office_knob worked example (sp_tutorial brief §5.2) -----------------------
	// Authored: [gate.Enable @0] then [noblood.Add(0) @+0.2]. Reverse enumeration meets the
	// delayed row FIRST, so it is the first into the queue — and the deadline sort then makes the
	// VISIBLE order "Enable now, Add after 0.2 game seconds". The two orders disagree on purpose:
	// that is exactly what distinguishes an enumeration bug from a sort bug.
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__knob_order__");
		FElysiumEntityDef Knob = Relay(TEXT("knob"));
		Wire(Knob, TEXT("OnTrigger"), TEXT("gate"), TEXT("Enable"));
		Wire(Knob, TEXT("OnTrigger"), TEXT("noblood"), TEXT("Add"), TEXT("0"), /*Delay*/ 0.2f);
		Defs.Defs.Add(MoveTemp(Knob));
		// The receiver starts disabled, so `Enable` is observable as state rather than only as a
		// delivery line.
		FElysiumEntityDef Gate = Relay(TEXT("gate"));
		Gate.Keys.Add(TEXT("StartDisabled"), TEXT("1"));
		Defs.Defs.Add(MoveTemp(Gate));
		Defs.Defs.Add(Counter(TEXT("noblood")));

		FElysiumEntityWorld World(nullptr, nullptr);
		World.Load(MoveTemp(Defs));
		FElysiumOrderedIOSink* Sink = Record(World);
		World.Activate(0.0);

		Trigger(World, TEXT("knob"));
		World.Tick(0.0);

		const bool bQueued = Sink->AppearsInOrder(TEXT("queue"),
			{ TEXT("noblood.Add"), TEXT("gate.Enable") });
		TestTrue(TEXT("reverse enumeration puts the delayed row into the queue first"), bQueued);
		if (!bQueued)
		{
			AddInfo(FString::Printf(TEXT("knob queue stream: %s"), *Sink->Log()));
		}
		TestTrue(TEXT("the zero-delay row is delivered in this pass"),
			Sink->Saw(TEXT("deliver"), TEXT("gate.Enable")));
		TestFalse(TEXT("the +0.2 row is not"),
			Sink->Saw(TEXT("deliver"), TEXT("noblood.Add")));
		TestEqual(TEXT("it is still pending"), World.Queue().Num(), 1);

		FElysiumEntity* GateEntity = World.FindByName(TEXT("gate"));
		if (TestNotNull(TEXT("gate resolved"), GateEntity))
		{
			TArray<TPair<FString, FString>> State;
			GateEntity->GetDebugState(State);
			const TPair<FString, FString>* Disabled = State.FindByPredicate(
				[](const TPair<FString, FString>& Row) { return Row.Key == TEXT("Disabled"); });
			TestTrue(TEXT("Enable reached the receiver"),
				Disabled != nullptr && Disabled->Value == TEXT("no"));
		}

		// Serviced PAST the deadline rather than exactly on it. `Delay` is a float (the `.ents`
		// field's own width) and the deadline is `double Now + (double)0.2f`, which is
		// 0.20000000298… — strictly greater than the double 0.2. Ticking at exactly 0.2 leaves the
		// row correctly not-yet-due and would assert a precision artifact rather than the sort. A
		// real frame lands wherever it lands, so the tick does too.
		World.Tick(0.3);
		TestTrue(TEXT("the deadline sort delivers Enable at t and Add at t+0.2"),
			Sink->AppearsInOrder(TEXT("deliver"), { TEXT("gate.Enable"), TEXT("noblood.Add") }));
		TestEqual(TEXT("both rows are spent"), World.Queue().Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumQueueDrainOrderTest,
	"Elysium.Substrate.QueueDrainOrder", GElysiumTestFlags)
bool FElysiumQueueDrainOrderTest::RunTest(const FString&)
{
	using namespace ElysiumEventOrderTests;

	// --- (a) breadth-first, never depth-first ----------------------------------------------
	// `CEventQueue::AddEvent` advances while `fireTime <= newFireTime`, so a zero-delay event a
	// receiver produces lands BEHIND the equal-time cohort already pending: with A and B due, if A
	// produces C the order is A, B, C — never A, C, B (entity_io.md).
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__drain_order__");
		FElysiumEntityDef RelayA = Relay(TEXT("relay_a"));
		Wire(RelayA, TEXT("OnTrigger"), TEXT("out_a"), TEXT("Add"), TEXT("1"));
		Defs.Defs.Add(MoveTemp(RelayA));
		FElysiumEntityDef RelayB = Relay(TEXT("relay_b"));
		Wire(RelayB, TEXT("OnTrigger"), TEXT("out_b"), TEXT("Add"), TEXT("1"));
		Defs.Defs.Add(MoveTemp(RelayB));
		Defs.Defs.Add(Counter(TEXT("out_a")));
		Defs.Defs.Add(Counter(TEXT("out_b")));

		FElysiumEntityWorld World(nullptr, nullptr);
		World.Load(MoveTemp(Defs));
		FElysiumOrderedIOSink* Sink = Record(World);
		World.Activate(0.0);

		Trigger(World, TEXT("relay_a"));
		Trigger(World, TEXT("relay_b"));
		World.Tick(0.0);

		const bool bBreadthFirst = Sink->AppearsInOrder(TEXT("deliver"),
			{ TEXT("relay_a.Trigger"), TEXT("relay_b.Trigger"), TEXT("out_a.Add"), TEXT("out_b.Add") });
		TestTrue(TEXT("a receiver's zero-delay work drains behind the pending equal-time cohort"),
			bBreadthFirst);
		if (!bBreadthFirst)
		{
			AddInfo(FString::Printf(TEXT("drain stream: %s"), *Sink->Log()));
		}
		TestEqual(TEXT("the whole chain drained in one pass"), World.Queue().Num(), 0);
	}

	// --- (b) the porch shape (sp_tutorial brief §11.5) -------------------------------------
	// `trig_off_porch.OnEndTouch` resolves six equal-time actions in one visible order and then a
	// delayed `Kill` at +0.5. The six are authored here in REVERSE of the expected delivery order,
	// because reverse enumeration is what makes the authored tail come out first; the delayed row
	// is authored LAST, so it is the FIRST thing enqueued and still the LAST thing delivered.
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__porch_order__");
		FElysiumEntityDef Porch = Relay(TEXT("porch"));
		Wire(Porch, TEXT("OnTrigger"), TEXT("act6"), TEXT("Add"), TEXT("1"));
		Wire(Porch, TEXT("OnTrigger"), TEXT("act5"), TEXT("Add"), TEXT("1"));
		Wire(Porch, TEXT("OnTrigger"), TEXT("act4"), TEXT("Add"), TEXT("1"));
		Wire(Porch, TEXT("OnTrigger"), TEXT("act3"), TEXT("Add"), TEXT("1"));
		Wire(Porch, TEXT("OnTrigger"), TEXT("act2"), TEXT("Add"), TEXT("1"));
		Wire(Porch, TEXT("OnTrigger"), TEXT("act1"), TEXT("Add"), TEXT("1"));
		Wire(Porch, TEXT("OnTrigger"), TEXT("act_late"), TEXT("Add"), TEXT("1"), /*Delay*/ 0.5f);
		Defs.Defs.Add(MoveTemp(Porch));
		Defs.Defs.Add(Counter(TEXT("act1")));
		Defs.Defs.Add(Counter(TEXT("act2")));
		Defs.Defs.Add(Counter(TEXT("act3")));
		Defs.Defs.Add(Counter(TEXT("act4")));
		Defs.Defs.Add(Counter(TEXT("act5")));
		Defs.Defs.Add(Counter(TEXT("act6")));
		Defs.Defs.Add(Counter(TEXT("act_late")));

		FElysiumEntityWorld World(nullptr, nullptr);
		World.Load(MoveTemp(Defs));
		FElysiumOrderedIOSink* Sink = Record(World);
		World.Activate(0.0);

		Trigger(World, TEXT("porch"));
		World.Tick(0.0);
		World.Tick(0.5);

		const bool bQueued = Sink->AppearsInOrder(TEXT("queue"),
			{ TEXT("act_late.Add"), TEXT("act1.Add"), TEXT("act2.Add"), TEXT("act3.Add"),
			  TEXT("act4.Add"), TEXT("act5.Add"), TEXT("act6.Add") });
		TestTrue(TEXT("the last authored row is the first enqueued, the delayed row included"),
			bQueued);
		const bool bDelivered = Sink->AppearsInOrder(TEXT("deliver"),
			{ TEXT("act1.Add"), TEXT("act2.Add"), TEXT("act3.Add"), TEXT("act4.Add"),
			  TEXT("act5.Add"), TEXT("act6.Add"), TEXT("act_late.Add") });
		TestTrue(TEXT("six equal-time actions deliver in reverse-row order, the +0.5 row last"),
			bDelivered);
		if (!bQueued || !bDelivered)
		{
			AddInfo(FString::Printf(TEXT("porch stream: %s"), *Sink->Log()));
		}
		TestEqual(TEXT("the whole transaction is spent"), World.Queue().Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRecordServiceOrderTest,
	"Elysium.Substrate.RecordServiceOrder", GElysiumTestFlags)
bool FElysiumRecordServiceOrderTest::RunTest(const FString&)
{
	using namespace ElysiumEventOrderTests;

	// Service of ONE record: deliver the input to every name match in stable entity order, then
	// execute its field-6 Python (entity_io.md; §2.5.1). Targetnames are non-unique, so three defs
	// share one name and the handle index — which IS the def-array index (R3) — is the order.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__record_service__");
	Defs.Defs.Add(Counter(TEXT("trio")));   // #0
	Defs.Defs.Add(Counter(TEXT("trio")));   // #1
	Defs.Defs.Add(Counter(TEXT("trio")));   // #2
	FElysiumEntityDef Fanout = Relay(TEXT("fanout"));   // #3
	Wire(Fanout, TEXT("OnTrigger"), TEXT("trio"), TEXT("Add"), TEXT("1"), /*Delay*/ 0.f,
		/*Times*/ -1, TEXT("G.Fanout = 1"));
	Defs.Defs.Add(MoveTemp(Fanout));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	FElysiumOrderedIOSink* Sink = Record(World);
	World.Activate(0.0);

	Trigger(World, TEXT("fanout"));
	World.Tick(0.0);

	TestEqual(TEXT("one row produces exactly one queued record"),
		Sink->CountOf(TEXT("queue"), TEXT("trio.Add")), 1);
	TestTrue(TEXT("...carrying both the name target and the field-6 payload"),
		Sink->Saw(TEXT("queue"), TEXT("+py")));

	const bool bStableOrder = Sink->AppearsInOrder(TEXT("deliver"),
		{ TEXT("#0 trio.Add"), TEXT("#1 trio.Add"), TEXT("#2 trio.Add") });
	TestTrue(TEXT("one record fans out to every name match in ascending entity order"),
		bStableOrder);
	if (!bStableOrder)
	{
		AddInfo(FString::Printf(TEXT("fan-out stream: %s"), *Sink->Log()));
	}
	TestEqual(TEXT("one record produced three deliveries"),
		Sink->CountOf(TEXT("deliver"), TEXT("trio.Add")), 3);
	TestEqual(TEXT("each match received it once"), CounterValue(World, TEXT("trio")), 1.f);

	// The Python step's POSITION (after the name deliveries, before the direct handle) is not
	// observable at this tier: `DeliverEvent` hands field-6 to `GameState->ScriptHost()`, and a
	// headless world has no game state, so the payload is a silent no-op and `OnPython` never
	// fires. What is assertable here is that the payload rides the same single record — which is
	// the part the ordering rule is about. Ordering the step itself needs a script host and
	// belongs to the Content tier.
	TestEqual(TEXT("a headless world runs no field-6 Python"),
		Sink->CountOf(TEXT("python"), TEXT("G.Fanout")), 0);

	// A runtime-spawned entity joins the fan-out at its own index — at the END, because its index
	// is minted past the map's def array. It must never jump the map-authored matches.
	const FElysiumEntityHandle Late = World.SpawnRuntimeEntity(Counter(TEXT("trio")));
	TestTrue(TEXT("the runtime match takes the next index"), Late.IsSet() && Late.Index == 4);
	Sink->Reset();

	Trigger(World, TEXT("fanout"));
	World.Tick(0.0);

	const bool bLateLast = Sink->AppearsInOrder(TEXT("deliver"),
		{ TEXT("#0 trio.Add"), TEXT("#1 trio.Add"), TEXT("#2 trio.Add"), TEXT("#4 trio.Add") });
	TestTrue(TEXT("a runtime-spawned match does not jump ahead of the map-authored ones"),
		bLateLast);
	TestEqual(TEXT("the second record fanned out to four matches"),
		Sink->CountOf(TEXT("deliver"), TEXT("trio.Add")), 4);
	if (!bLateLast)
	{
		AddInfo(FString::Printf(TEXT("late fan-out stream: %s"), *Sink->Log()));
	}
	TestEqual(TEXT("a map-authored match received both fires"),
		CounterValue(World, TEXT("trio")), 2.f);
	TestEqual(TEXT("the runtime match received only the second"),
		CounterValueOf(World.Resolve(Late)), 1.f);
	TestEqual(TEXT("no wire named nothing"), World.UnknownTargets(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOutputTimesTest,
	"Elysium.Substrate.OutputTimes", GElysiumTestFlags)
bool FElysiumOutputTimesTest::RunTest(const FString&)
{
	using namespace ElysiumEventOrderTests;

	// `FireOutput` decrements only a POSITIVE `times`; a row reaching zero is removed without
	// disturbing the remaining rows, and an authored 0 is unlimited exactly like -1 (§2.5.1). The
	// word in the contract is *authored*, so this one map is built the authored way — written as
	// `.ents` and parsed — rather than by hand, which would settle by construction the question of
	// where an authored 0 becomes unlimited.
	IFileManager::Get().MakeDirectory(*FPaths::AutomationTransientDir(), /*Tree*/ true);
	const FString EntsPath = FPaths::CreateTempFilename(
		*FPaths::AutomationTransientDir(), TEXT("ElysiumOutputTimes_"), TEXT(".ents"));

	auto RelayJson = [](const TCHAR* Name, const TCHAR* Target, int32 Times)
	{
		return FString::Printf(
			TEXT("{\"classname\":\"logic_relay\",\"targetname\":\"%s\",")
			TEXT("\"keys\":{\"spawnflags\":\"2\"},\"outputs\":[")
			TEXT("{\"name\":\"OnTrigger\",\"target\":\"%s\",\"input\":\"Add\",")
			TEXT("\"param\":\"1\",\"delay\":0,\"times\":%d}]}"),
			Name, Target, Times);
	};
	auto CounterJson = [](const TCHAR* Name)
	{
		return FString::Printf(
			TEXT("{\"classname\":\"math_counter\",\"targetname\":\"%s\"}"), Name);
	};
	const TArray<FString> Entities = {
		RelayJson(TEXT("relay_twice"), TEXT("hit_twice"), 2),
		RelayJson(TEXT("relay_minus_one"), TEXT("hit_minus_one"), -1),
		RelayJson(TEXT("relay_zero"), TEXT("hit_zero"), 0),
		CounterJson(TEXT("hit_twice")),
		CounterJson(TEXT("hit_minus_one")),
		CounterJson(TEXT("hit_zero")),
	};
	const FString Json = FString::Printf(TEXT("{\"map\":\"__output_times__\",\"entities\":[%s]}"),
		*FString::Join(Entities, TEXT(",")));
	if (!TestTrue(TEXT("synthetic .ents writes"), FFileHelper::SaveStringToFile(Json, *EntsPath)))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*EntsPath, /*RequireExists*/ false, /*EvenReadOnly*/ true);
	};

	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("synthetic .ents parses"), FElysiumEntityDefs::Parse(EntsPath, Defs)))
	{
		return false;
	}

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	FElysiumOrderedIOSink* Sink = Record(World);
	World.Activate(0.0);

	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		Trigger(World, TEXT("relay_twice"));
		Trigger(World, TEXT("relay_minus_one"));
		Trigger(World, TEXT("relay_zero"));
		World.Tick(0.0);
	}

	TestEqual(TEXT("times=2 fires exactly twice"),
		Sink->CountOf(TEXT("deliver"), TEXT("hit_twice.Add")), 2);
	TestEqual(TEXT("...and the receiver counted two"), CounterValue(World, TEXT("hit_twice")), 2.f);
	TestEqual(TEXT("times=-1 is unlimited"),
		Sink->CountOf(TEXT("deliver"), TEXT("hit_minus_one.Add")), 3);
	TestEqual(TEXT("...and the receiver counted three"),
		CounterValue(World, TEXT("hit_minus_one")), 3.f);
	// The other half of the same rule: retail's parser seeds `times` to -1 and rewrites an
	// authored 0 back to -1, so both spellings mean unlimited and only a positive value is a
	// countdown.
	TestEqual(TEXT("an authored times=0 is unlimited too"),
		Sink->CountOf(TEXT("deliver"), TEXT("hit_zero.Add")), 3);
	TestEqual(TEXT("...and the receiver counted three"),
		CounterValue(World, TEXT("hit_zero")), 3.f);

	// A spent row is a SILENT skip: nothing is queued, so nothing can be diagnosed. The third
	// attempt on the times=2 row must not look like a dead wire or a missing input.
	TestEqual(TEXT("a spent row queues nothing"),
		Sink->CountOf(TEXT("queue"), TEXT("hit_twice.Add")), 2);
	TestEqual(TEXT("a spent row is not a dead wire"), World.UnknownTargets(), 0);
	TestEqual(TEXT("a spent row is not a missing input"), World.UnknownInputs(), 0);
	TestEqual(TEXT("every pass drained"), World.Queue().Num(), 0);
	if (Sink->CountOf(TEXT("deliver"), TEXT("hit_zero.Add")) != 3)
	{
		AddInfo(FString::Printf(TEXT("times stream: %s"), *Sink->Log()));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLateBindingAndDropsTest,
	"Elysium.Substrate.LateBindingAndDrops", GElysiumTestFlags)
bool FElysiumLateBindingAndDropsTest::RunTest(const FString&)
{
	using namespace ElysiumEventOrderTests;

	// Binding is late: a target name resolves at SERVICE time against the live entity set, never
	// at parse — which is what makes maker children and script-spawned entities valid targets for
	// statically absent names. The mirror of the same rule is that missing-at-service is a counted
	// non-fatal drop, never a retry (§2.5.1).
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__late_binding__");
	FElysiumEntityDef LateRelay = Relay(TEXT("late_relay"));
	Wire(LateRelay, TEXT("OnTrigger"), TEXT("late_guy"), TEXT("Add"), TEXT("1"), /*Delay*/ 0.5f);
	Defs.Defs.Add(MoveTemp(LateRelay));
	FElysiumEntityDef DoomedRelay = Relay(TEXT("doomed_relay"));
	Wire(DoomedRelay, TEXT("OnTrigger"), TEXT("doomed"), TEXT("Add"), TEXT("1"), /*Delay*/ 0.5f);
	Defs.Defs.Add(MoveTemp(DoomedRelay));
	Defs.Defs.Add(Counter(TEXT("doomed")));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	FElysiumOrderedIOSink* Sink = Record(World);
	World.Activate(0.0);

	// --- (a) the target does not exist when the record is queued ---------------------------
	Trigger(World, TEXT("late_relay"));
	World.Tick(0.0);
	TestEqual(TEXT("the wire queues against a name no entity holds"), World.Queue().Num(), 1);
	TestTrue(TEXT("...without resolving anything at enqueue"),
		Sink->Saw(TEXT("queue"), TEXT("late_guy.Add")));
	TestEqual(TEXT("...and without counting a dead wire yet"), World.UnknownTargets(), 0);

	World.SpawnRuntimeEntity(Counter(TEXT("late_guy")));
	World.Tick(0.5);

	TestTrue(TEXT("the record binds to the entity that appeared after it was queued"),
		Sink->Saw(TEXT("deliver"), TEXT("late_guy.Add")));
	TestEqual(TEXT("...delivering once"), CounterValue(World, TEXT("late_guy")), 1.f);
	TestEqual(TEXT("late binding is not a dead wire"), World.UnknownTargets(), 0);
	TestEqual(TEXT("the queue is drained"), World.Queue().Num(), 0);

	// --- (b) the mirror: the target existed at enqueue and is gone at service ---------------
	World.Tick(1.0);
	Trigger(World, TEXT("doomed_relay"));
	World.Tick(1.0);
	TestEqual(TEXT("the delayed record is pending"), World.Queue().Num(), 1);

	FElysiumEntity* Doomed = World.FindByName(TEXT("doomed"));
	if (!TestNotNull(TEXT("doomed resolved"), Doomed))
	{
		return false;
	}
	Doomed->Kill();

	const int32 UnknownBefore = World.UnknownTargets();
	World.Tick(1.5);

	TestEqual(TEXT("a target that died before service counts exactly one dead wire"),
		World.UnknownTargets(), UnknownBefore + 1);
	TestTrue(TEXT("...and is reported as a missing target, not a missing input"),
		Sink->Saw(TEXT("no-target"), TEXT("doomed.Add")));
	TestEqual(TEXT("...with no input reported missing"), World.UnknownInputs(), 0);
	TestFalse(TEXT("...and nothing delivered"),
		Sink->Saw(TEXT("deliver"), TEXT("doomed.Add")));
	TestEqual(TEXT("the drop consumes the record"), World.Queue().Num(), 0);

	World.Tick(1.6);
	TestEqual(TEXT("a drop is never retried"), World.Queue().Num(), 0);
	TestEqual(TEXT("...so it is counted once and only once"),
		World.UnknownTargets(), UnknownBefore + 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMissingTargetClassificationTest,
	"Elysium.Substrate.MissingTargetClassification", GElysiumTestFlags)
bool FElysiumMissingTargetClassificationTest::RunTest(const FString&)
{
	using namespace ElysiumEventOrderTests;

	ElysiumStub::ClearTally();
	ON_SCOPE_EXIT { ElysiumStub::ClearTally(); };

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__missing_target_classification__");
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FElysiumEntityDef Target;
		Target.Classname = TEXT("point_target");
		Target.TargetName = TEXT("point_door");
		Defs.Defs.Add(MoveTemp(Target));
	}
	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	FElysiumOrderedIOSink* Sink = Record(World);
	World.Activate(0.0);

	// The first unlimited-output delivery fans out and kills every same-name marker.
	World.AcceptInput(TEXT("point_door"), FName(TEXT("Kill")), FElysiumVariant::Void(),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("the live fan-out is not a missing target"), World.UnknownTargets(), 0);
	TestEqual(TEXT("Kill reaches every same-name marker"),
		Sink->CountOf(TEXT("deliver"), TEXT("point_door.Kill")), 2);

	// A later refire is an expected post-cleanup miss, still visible in the I/O diagnostics.
	World.AcceptInput(TEXT("point_door"), FName(TEXT("Kill")), FElysiumVariant::Void(),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("the post-cleanup refire remains a counted I/O drop"), World.UnknownTargets(), 1);
	TestTrue(TEXT("the I/O sink retains the missing-target diagnostic"),
		Sink->Saw(TEXT("no-target"), TEXT("point_door.Kill")));

	TArray<ElysiumStub::FTally> Rows;
	ElysiumStub::CollectTally(Rows);
	TestFalse(TEXT("a missing receiver is not an implementation stub"),
		Rows.ContainsByPredicate([](const ElysiumStub::FTally& Row)
		{
			return Row.Kind == TEXT("target") || Row.Surface.Contains(TEXT("point_door.Kill"));
		}));

	// Named misses are log-once diagnostics, but every delivery attempt remains accounted.
	World.AcceptInput(TEXT("point_door"), FName(TEXT("Kill")), FElysiumVariant::Void(),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("a repeat is still counted"), World.UnknownTargets(), 2);
	TestEqual(TEXT("the repeated named miss is reported once"),
		Sink->CountOf(TEXT("no-target"), TEXT("point_door.Kill")), 1);

	return true;
}

// =====================================================================================
// The other half of the same contract: which TRANSPORT a caller is entitled to, and what a
// trigger's own latched state is worth across a save.
//
// The five rules above say what the queue does with a record. These say who may bypass it:
//
//   6. A script calling a reflected entity input is SYNCHRONOUS — the receiver mutates
//      before the evaluation returns, with null activator and null caller, while the
//      outputs that receiver fires still enter the ordinary queue.        SyncScriptInput
//      (`docs/vtmb/python_bridge.md` → "Synchronous calls versus queued Python")
//   7. A door's linked partner is driven through chokepoint 1 rather than by a direct C++
//      call, so the second leaf's swing is visible with its real provenance — and, because
//      the partner receives `Toggle` and never `Use`, it cannot mirror back.
//                                                                     DoorPartnerChokepoint
//   8. The by-handle chokepoint accounts a dead receiver exactly as the by-name one does:
//      a counted target miss on the sinks, outside the stub work list.    StaleDirectHandle
//   9. A trigger's latched gate state — the `wait` window, a dwell in progress, a pending
//      self-removal — survives freeze/restore, because none of it is derivable from the
//      def.                                                                TriggerStateSave
//  10. `wait == -1` is the whole of trigger_once: one activation, the touch handler nulled,
//      removal at +0.1, and the delayed rows already queued still delivering after the
//      entity is gone (`docs/vtmb/entity_io.md`).                              WaitMinusOne
//
// Rules 6-8 are transport claims, so they are asserted the same way the order rules are —
// on the FElysiumOrderedIOSink stream, which now closes each event line with its `act=`/`cal=`
// provenance. Rules 9-10 are state claims, asserted through the house freeze/rebuild/apply
// round trip.
// =====================================================================================

namespace ElysiumEventTransportTests
{
	using namespace ElysiumEventOrderTests;

	// A `wait`-carrying touch trigger. `spawnflags 1` is ALLOW_CLIENTS, without which
	// PassesTriggerFilters rejects the only toucher there is (entity_io.md).
	FElysiumEntityDef TriggerDef(const TCHAR* Classname, const TCHAR* Name, const TCHAR* Wait = nullptr)
	{
		FElysiumEntityDef Def;
		Def.Classname = Classname;
		Def.TargetName = Name;
		Def.Keys.Add(TEXT("spawnflags"), TEXT("1"));
		if (Wait != nullptr)
		{
			Def.Keys.Add(TEXT("wait"), Wait);
		}
		return Def;
	}

	// The player walking in and back out. Begin/end are edges (the world retains the pair), so a
	// re-entry has to be spelled as both.
	void Enter(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		if (const FElysiumEntity* T = World.FindByName(Name))
		{
			World.RouteBrushTouch(T->Handle, World.PlayerHandle(), /*bBegin*/ true);
		}
	}
	void Leave(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		if (const FElysiumEntity* T = World.FindByName(Name))
		{
			World.RouteBrushTouch(T->Handle, World.PlayerHandle(), /*bBegin*/ false);
		}
	}

	// `act=#<idx>` / `cal=#<idx>` as the sink spells them, so a provenance assertion names the
	// entity it means rather than a literal index.
	FString ActIs(const FElysiumEntityHandle& H) { return FString::Printf(TEXT("act=%s"), *H.ToString()); }
	FString CalIs(const FElysiumEntityHandle& H) { return FString::Printf(TEXT("cal=%s"), *H.ToString()); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSyncScriptInputTest,
	"Elysium.Substrate.SyncScriptInput", GElysiumTestFlags)
bool FElysiumSyncScriptInputTest::RunTest(const FString&)
{
	using namespace ElysiumEventTransportTests;

	// The seam IS reachable headless, through the expression evaluator rather than through a script
	// host: `ElysiumExpr::Exec` takes an FEnv whose `Ctx.World` is the entity world, and both
	// installed hosts (FElysiumExprScriptHost, and the CPython host's own reflected-input thunk)
	// bottom out in the same `AcceptInput(handle, ...)` call this drives. What a host adds on top is
	// only the `G` store and the level-script namespace, neither of which a reflected input call
	// touches — so evaluating the call string directly exercises the whole of the seam. The one
	// thing that is NOT reachable here is the CPython half of it: `Entity_fire_input` needs the
	// embedded VM, which is a build option, and the two paths are held together by review rather
	// than by this test.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__sync_script_input__");
	FElysiumEntityDef Producer = Relay(TEXT("sync_relay"));
	Wire(Producer, TEXT("OnTrigger"), TEXT("out_counter"), TEXT("Add"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(Producer));
	Defs.Defs.Add(Counter(TEXT("sync_counter")));
	Defs.Defs.Add(Counter(TEXT("out_counter")));
	Defs.Defs.Add(Counter(TEXT("cohort_one")));
	Defs.Defs.Add(Counter(TEXT("cohort_two")));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	FElysiumOrderedIOSink* Sink = Record(World);
	World.Activate(0.0);

	ElysiumExpr::FEnv Env;
	Env.Ctx.World = &World;

	// --- (a) the receiver has already mutated when the evaluation returns -------------------
	// No tick between the call and the read: a queued delivery would still be pending here, and the
	// counter would read 0.
	const FElysiumVariant Result = ElysiumExpr::Exec(TEXT("sync_counter.Add(3)"), Env);
	TestFalse(TEXT("the reflected call evaluates without error"), Env.bError);
	TestFalse(TEXT("an input call has no value"), Result.ToBool());
	TestEqual(TEXT("the receiver mutated before the evaluation returned"),
		CounterValue(World, TEXT("sync_counter")), 3.f);
	TestEqual(TEXT("the input itself was never queued"),
		Sink->CountOf(TEXT("queue"), TEXT("sync_counter.Add")), 0);
	TestTrue(TEXT("...it was delivered through chokepoint 1"),
		Sink->Saw(TEXT("deliver"), TEXT("sync_counter.Add")));
	TestEqual(TEXT("and nothing is pending"), World.Queue().Num(), 0);

	// --- (c) provenance: retail passes null activator AND null caller -----------------------
	const FString SyncLine = Sink->FirstLine(TEXT("deliver"), TEXT("sync_counter.Add"));
	TestTrue(TEXT("the synchronous delivery carries a null activator"),
		SyncLine.Contains(ActIs(FElysiumEntityHandle::Invalid())));
	TestTrue(TEXT("...and a null caller"),
		SyncLine.Contains(CalIs(FElysiumEntityHandle::Invalid())));

	// --- (b) what the receiver FIRES is still ordinary queued work --------------------------
	// Two records are made due first, so "behind the pending equal-time cohort" is a statement with
	// something to be behind. The call runs between the enqueues and the tick.
	Sink->Reset();
	World.EnqueueInput(TEXT("cohort_one"), FName(TEXT("Add")), FElysiumVariant::Int(1), /*Delay*/ 0.0,
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.EnqueueInput(TEXT("cohort_two"), FName(TEXT("Add")), FElysiumVariant::Int(1), /*Delay*/ 0.0,
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());

	ElysiumExpr::Exec(TEXT("sync_relay.Trigger()"), Env);
	TestFalse(TEXT("the reflected Trigger evaluates without error"), Env.bError);

	TestTrue(TEXT("the relay's Trigger ran synchronously, ahead of the pending cohort"),
		Sink->Saw(TEXT("deliver"), TEXT("sync_relay.Trigger")));
	TestEqual(TEXT("...without a queue record of its own"),
		Sink->CountOf(TEXT("queue"), TEXT("sync_relay.Trigger")), 0);
	TestTrue(TEXT("the output the body fired IS queued"),
		Sink->Saw(TEXT("queue"), TEXT("out_counter.Add")));
	TestFalse(TEXT("...and therefore has not been delivered yet"),
		Sink->Saw(TEXT("deliver"), TEXT("out_counter.Add")));
	const bool bBehindCohort = Sink->AppearsInOrder(TEXT("queue"),
		{ TEXT("cohort_one.Add"), TEXT("cohort_two.Add"), TEXT("out_counter.Add") });
	TestTrue(TEXT("it lands behind the equal-time cohort already pending"), bBehindCohort);

	// The fired output's own provenance: FireOutput names the relay as caller and propagates the
	// activator the input arrived with — which the synchronous call left null (python_bridge.md).
	const FElysiumEntity* Relay = World.FindByName(TEXT("sync_relay"));
	if (TestNotNull(TEXT("sync_relay resolved"), Relay))
	{
		const FString QueuedLine = Sink->FirstLine(TEXT("queue"), TEXT("out_counter.Add"));
		TestTrue(TEXT("the queued output carries the null activator the call passed"),
			QueuedLine.Contains(ActIs(FElysiumEntityHandle::Invalid())));
		TestTrue(TEXT("...and names the firing entity as caller"),
			QueuedLine.Contains(CalIs(Relay->Handle)));
	}

	World.Tick(0.0);
	const bool bDrained = Sink->AppearsInOrder(TEXT("deliver"),
		{ TEXT("sync_relay.Trigger"), TEXT("cohort_one.Add"), TEXT("cohort_two.Add"),
		  TEXT("out_counter.Add") });
	TestTrue(TEXT("the synchronous call is delivered before the cohort, its output after"), bDrained);
	if (!bBehindCohort || !bDrained)
	{
		AddInfo(FString::Printf(TEXT("sync-call stream: %s"), *Sink->Log()));
	}
	TestEqual(TEXT("the output arrived exactly once"), CounterValue(World, TEXT("out_counter")), 1.f);
	TestEqual(TEXT("the pass drained"), World.Queue().Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorPartnerChokepointTest,
	"Elysium.Substrate.DoorPartnerChokepoint", GElysiumTestFlags)
bool FElysiumDoorPartnerChokepointTest::RunTest(const FString&)
{
	using namespace ElysiumEventTransportTests;

	// CBaseDoor::DoorknobUse toggles this leaf and then its `linked_door` partner. The partner's
	// half is delivered through chokepoint 1 so the sinks, the I/O ring and the queue debugger see
	// the second leaf swing — synchronously, because DoorUse is already running inside an executing
	// Use handler (gameplay-systems-architecture.md §2.5.1's sanctioned seam).
	//
	// Both leaves are bodiless: a mover with no brush body cannot travel, but it still runs the
	// whole decision — locked check, toggle-state flip, OnOpen — which is the half this asserts.
	// The doors are linked BOTH ways on purpose: that is how a double door is authored, and it is
	// what makes "the partner receives Toggle, never Use" load-bearing rather than incidental.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__door_partner__");
	FElysiumEntityDef LeftLeaf;
	LeftLeaf.Classname = TEXT("func_door");
	LeftLeaf.TargetName = TEXT("door_a");
	LeftLeaf.Keys.Add(TEXT("linked_door"), TEXT("door_b"));
	Wire(LeftLeaf, TEXT("OnOpen"), TEXT("a_open"), TEXT("Add"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(LeftLeaf));
	FElysiumEntityDef RightLeaf;
	RightLeaf.Classname = TEXT("func_door");
	RightLeaf.TargetName = TEXT("door_b");
	RightLeaf.Keys.Add(TEXT("linked_door"), TEXT("door_a"));
	Wire(RightLeaf, TEXT("OnOpen"), TEXT("b_open"), TEXT("Add"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(RightLeaf));
	Defs.Defs.Add(Counter(TEXT("a_open")));
	Defs.Defs.Add(Counter(TEXT("b_open")));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	FElysiumOrderedIOSink* Sink = Record(World);
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);

	const FElysiumEntity* DoorA = World.FindByName(TEXT("door_a"));
	const FElysiumEntity* DoorB = World.FindByName(TEXT("door_b"));
	if (!TestNotNull(TEXT("door_a resolved"), DoorA) || !TestNotNull(TEXT("door_b resolved"), DoorB))
	{
		return false;
	}

	// The knob press, spelled as the ordinary Use input the +use path and `ent_fire` both reach.
	World.AcceptInput(TEXT("door_a"), FName(TEXT("Use")), FElysiumVariant::Void(), Player, Player);

	TestTrue(TEXT("the used leaf received Use"), Sink->Saw(TEXT("deliver"), TEXT("door_a.Use")));
	TestEqual(TEXT("the partner's half is visible at the chokepoint exactly once"),
		Sink->CountOf(TEXT("deliver"), TEXT("door_b.Toggle")), 1);
	TestEqual(TEXT("...and did not go through the queue"),
		Sink->CountOf(TEXT("queue"), TEXT("door_b.Toggle")), 0);
	TestEqual(TEXT("the queue is untouched by the swing itself"),
		Sink->CountOf(TEXT("queue"), TEXT("Toggle")), 0);

	const FString PartnerLine = Sink->FirstLine(TEXT("deliver"), TEXT("door_b.Toggle"));
	TestTrue(TEXT("the partner's delivery preserves the original activator"),
		PartnerLine.Contains(ActIs(Player)));
	TestTrue(TEXT("...and names the used leaf as caller"), PartnerLine.Contains(CalIs(DoorA->Handle)));

	// No mirror-back: the partner is handed `Toggle`, which is not the input DoorUse hangs off, so
	// the recursion is structurally impossible rather than guarded against.
	TestEqual(TEXT("the partner never receives Use"),
		Sink->CountOf(TEXT("deliver"), TEXT("door_b.Use")), 0);
	TestEqual(TEXT("and the used leaf is never toggled back by its partner"),
		Sink->CountOf(TEXT("deliver"), TEXT("door_a.Toggle")), 0);

	// Both leaves actually moved — the assertion that the visibility change did not cost the swing.
	World.Tick(0.0);
	TestEqual(TEXT("the used leaf opened"), CounterValue(World, TEXT("a_open")), 1.f);
	TestEqual(TEXT("and so did the partner"), CounterValue(World, TEXT("b_open")), 1.f);
	TestEqual(TEXT("no wire named nothing"), World.UnknownTargets(), 0);
	TestEqual(TEXT("no input was missing"), World.UnknownInputs(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStaleDirectHandleTest,
	"Elysium.Substrate.StaleDirectHandle", GElysiumTestFlags)
bool FElysiumStaleDirectHandleTest::RunTest(const FString&)
{
	using namespace ElysiumEventTransportTests;

	// The by-handle overload of chokepoint 1 has the same K3 obligation as the by-name one: a
	// receiver this map cannot deliver to is a counted target miss on the sinks, but it is not an
	// implementation stub. Retail data can legitimately kill a target before a later delivery.
	ElysiumStub::ClearTally();
	ON_SCOPE_EXIT
	{
		ElysiumStub::ClearTally();
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__stale_handle__");
	Defs.Defs.Add(Counter(TEXT("doomed")));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	FElysiumOrderedIOSink* Sink = Record(World);
	World.Activate(0.0);

	FElysiumEntity* Doomed = World.FindByName(TEXT("doomed"));
	if (!TestNotNull(TEXT("doomed resolved"), Doomed))
	{
		return false;
	}
	const FElysiumEntityHandle Handle = Doomed->Handle;

	// The live case first, so the difference is the entity's death and nothing else.
	World.AcceptInput(Handle, FName(TEXT("Add")), FElysiumVariant::Int(1),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("a live handle delivers"), CounterValue(World, TEXT("doomed")), 1.f);
	TestEqual(TEXT("and is not a dead wire"), World.UnknownTargets(), 0);

	Doomed->Kill();
	const int32 UnknownBefore = World.UnknownTargets();
	Sink->Reset();

	World.AcceptInput(Handle, FName(TEXT("Add")), FElysiumVariant::Int(1),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());

	TestEqual(TEXT("a handle with no live entity behind it counts one dead wire"),
		World.UnknownTargets(), UnknownBefore + 1);
	TestEqual(TEXT("...and is not reported as a missing input"), World.UnknownInputs(), 0);
	TestTrue(TEXT("...the sinks see it as an unknown target"),
		Sink->Saw(TEXT("no-target"), TEXT("<stale>.Add")));
	TestEqual(TEXT("...and nothing was delivered"),
		Sink->CountOf(TEXT("deliver"), TEXT("doomed.Add")), 0);

	// A stale receiver remains outside the implementation work list: no handler or runtime class is
	// missing merely because the intended entity has already died.
	auto TargetRowsMatching = [](const TCHAR* Fragment)
	{
		int32 N = 0;
		TArray<ElysiumStub::FTally> Rows;
		ElysiumStub::CollectTally(Rows);
		for (const ElysiumStub::FTally& R : Rows)
		{
			if (R.Kind == TEXT("target") && R.Surface.Contains(Fragment))
			{
				N += R.Count;
			}
		}
		return N;
	};
	TestEqual(TEXT("the stub work list does not gain a stale-handle row"),
		TargetRowsMatching(TEXT("<stale>.Add")), 0);

	// The by-handle path does not log-once the way the by-name path does, so a repeat is counted
	// and reported again rather than silently swallowed.
	World.AcceptInput(Handle, FName(TEXT("Add")), FElysiumVariant::Int(1),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("a second attempt counts again"), World.UnknownTargets(), UnknownBefore + 2);
	TestEqual(TEXT("and still creates no implementation stub"),
		TargetRowsMatching(TEXT("<stale>.Add")), 0);
	TestEqual(TEXT("both attempts reached the sinks"),
		Sink->CountOf(TEXT("no-target"), TEXT("<stale>.Add")), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTriggerStateSaveTest,
	"Elysium.Substrate.TriggerStateSave", GElysiumTestFlags)
bool FElysiumTriggerStateSaveTest::RunTest(const FString&)
{
	using namespace ElysiumEventTransportTests;

	// A trigger's gate state is not derivable from its def: the `wait` window is measured from the
	// last fire, a dwell is a partial accumulation, and a pending self-removal is a scheduled think
	// with a reason. All of it rides the leaf blob (save-architecture.md §4), so all of it is
	// asserted the same way — freeze one world, rebuild a second from the same defs, apply, and
	// drive the second forward from where the first stopped.

	// --- (a) trigger_multiple: the `wait` re-arm window survives ---------------------------
	{
		auto MakeDefs = []()
		{
			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__trigger_wait_save__");
			FElysiumEntityDef Gate = TriggerDef(TEXT("trigger_multiple"), TEXT("gate"), TEXT("1.0"));
			Wire(Gate, TEXT("OnTrigger"), TEXT("hits"), TEXT("Add"), TEXT("1"));
			Defs.Defs.Add(MoveTemp(Gate));
			Defs.Defs.Add(Counter(TEXT("hits")));
			return Defs;
		};

		FElysiumEntityWorld Before(nullptr, nullptr);
		Before.Load(MakeDefs());
		Before.SpawnPlayer();
		Before.Activate(0.0);

		Enter(Before, TEXT("gate"));
		Before.Tick(0.0);
		TestEqual(TEXT("the first entry fires"), CounterValue(Before, TEXT("hits")), 1.f);
		Leave(Before, TEXT("gate"));
		Before.Tick(0.4);

		FElysiumMapSnapshot MidWindow;
		Before.Freeze(MidWindow);
		TestTrue(TEXT("a fired trigger differs from its fresh build"), MidWindow.Entities.Num() > 0);

		FElysiumEntityWorld After(nullptr, nullptr);
		After.Load(MakeDefs());
		After.SpawnPlayer();
		After.ApplySnapshot(MidWindow);
		After.Activate(0.4);

		TestEqual(TEXT("the counter came back with the fire already on it"),
			CounterValue(After, TEXT("hits")), 1.f);

		// 0.4 into a 1.0 second window: swallowed, and NOT retried when the window re-arms.
		Enter(After, TEXT("gate"));
		After.Tick(0.4);
		TestEqual(TEXT("a touch inside the restored window is swallowed"),
			CounterValue(After, TEXT("hits")), 1.f);
		Leave(After, TEXT("gate"));

		// Past it: the same trigger fires again, which is what proves the window was a window and
		// not a permanent lockout.
		After.Tick(1.1);
		Enter(After, TEXT("gate"));
		After.Tick(1.1);
		TestEqual(TEXT("a touch after it fires"), CounterValue(After, TEXT("hits")), 2.f);
	}

	// --- (b) trigger_once: fired, suppressed, and its removal still pending ------------------
	{
		auto MakeDefs = []()
		{
			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__trigger_once_save__");
			FElysiumEntityDef Once = TriggerDef(TEXT("trigger_once"), TEXT("oneshot"));
			Wire(Once, TEXT("OnTrigger"), TEXT("hits"), TEXT("Add"), TEXT("1"));
			Defs.Defs.Add(MoveTemp(Once));
			Defs.Defs.Add(Counter(TEXT("hits")));
			return Defs;
		};

		FElysiumEntityWorld Before(nullptr, nullptr);
		Before.Load(MakeDefs());
		Before.SpawnPlayer();
		Before.Activate(0.0);

		Enter(Before, TEXT("oneshot"));
		Before.Tick(0.0);
		TestEqual(TEXT("the one-shot fired"), CounterValue(Before, TEXT("hits")), 1.f);
		// CTriggerOnce::Spawn forces `wait -1`, which schedules SUB_Remove at +0.1 — so at +0.05 the
		// entity is still alive with its removal pending. That is the state being saved.
		Leave(Before, TEXT("oneshot"));
		Before.Tick(0.05);
		TestNotNull(TEXT("removal is scheduled, not yet done"), Before.FindByName(TEXT("oneshot")));

		FElysiumMapSnapshot Pending;
		Before.Freeze(Pending);

		FElysiumEntityWorld After(nullptr, nullptr);
		After.Load(MakeDefs());
		After.SpawnPlayer();
		After.ApplySnapshot(Pending);
		After.Activate(0.05);

		FElysiumEntity* Restored = After.FindByName(TEXT("oneshot"));
		if (!TestNotNull(TEXT("the still-pending trigger restored alive"), Restored))
		{
			return false;
		}
		TestEqual(TEXT("the counter came back fired"), CounterValue(After, TEXT("hits")), 1.f);

		// Touch-suppressed: the restored trigger admits the contact and does nothing with it.
		Enter(After, TEXT("oneshot"));
		After.Tick(0.05);
		TestEqual(TEXT("a restored one-shot cannot fire again"),
			CounterValue(After, TEXT("hits")), 1.f);
		TestNotNull(TEXT("...and is still alive before its removal is due"),
			After.FindByName(TEXT("oneshot")));

		// The pending think crosses the restore: +0.1 from the original fire, on the restored world.
		// Serviced PAST the deadline rather than exactly on it, for the same reason the OutputTimes
		// case ticks at 0.3 for a +0.2 row: NextThink is a float, so `0.0 + 0.1` stores as
		// 0.100000001490…, which is strictly greater than the double 0.1. A real frame lands wherever
		// it lands, so the tick does too.
		After.Tick(0.15);
		TestNull(TEXT("the scheduled self-removal runs across the save"),
			After.FindByName(TEXT("oneshot")));
	}

	// --- (c) trigger_look: a partial dwell is not an instant fire ---------------------------
	{
		auto MakeDefs = []()
		{
			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__trigger_look_save__");
			FElysiumEntityDef Look = TriggerDef(TEXT("trigger_look"), TEXT("watcher"));
			Look.Keys.Add(TEXT("target"), TEXT("look_at"));
			Look.Keys.Add(TEXT("LookTime"), TEXT("1.0"));
			Look.Keys.Add(TEXT("FieldOfView"), TEXT("0.9"));
			Wire(Look, TEXT("OnTrigger"), TEXT("look_hits"), TEXT("Add"), TEXT("1"));
			Defs.Defs.Add(MoveTemp(Look));
			FElysiumEntityDef Subject;
			Subject.Classname = TEXT("info_landmark");
			Subject.TargetName = TEXT("look_at");
			Subject.Origin = FVector(500.f, 0.f, 0.f);
			Defs.Defs.Add(MoveTemp(Subject));
			Defs.Defs.Add(Counter(TEXT("look_hits")));
			return Defs;
		};
		// The player stands at the origin looking down +X, which is where the subject is: the dot
		// test passes for the whole run, so elapsed time is the only variable.
		auto StandAndStare = [](FElysiumRecordingServices& S)
		{
			S.bHasPlayer = true;
			S.PlayerLocation = FVector::ZeroVector;
			S.PlayerRotation = FRotator::ZeroRotator;
		};

		FElysiumRecordingServices BeforeServices;
		StandAndStare(BeforeServices);
		FElysiumEntityWorld Before(nullptr, nullptr, BeforeServices.Bundle());
		Before.Load(MakeDefs());
		const FElysiumEntityHandle Player = Before.SpawnPlayer();
		Before.Activate(0.0);

		Enter(Before, TEXT("watcher"));
		Before.Tick(0.4);   // 0.4 s of a 1.0 s dwell
		TestEqual(TEXT("a partial dwell has not fired"), CounterValue(Before, TEXT("look_hits")), 0.f);

		FElysiumMapSnapshot MidDwell;
		Before.Freeze(MidDwell);

		FElysiumRecordingServices AfterServices;
		StandAndStare(AfterServices);
		FElysiumEntityWorld After(nullptr, nullptr, AfterServices.Bundle());
		After.Load(MakeDefs());
		FElysiumOrderedIOSink* Sink = Record(After);
		// Index only: the epoch is the world's teardown generation, so two loads never agree on it —
		// which is exactly why the applier re-stamps every restored handle (save-architecture.md §6).
		TestEqual(TEXT("the restored player takes the same entity index"),
			After.SpawnPlayer().Index, Player.Index);
		After.ApplySnapshot(MidDwell);
		After.Activate(0.4);

		// The first post-restore think samples Dt against the saved LastThinkTime, so it advances the
		// dwell by zero rather than by the whole clock — the spurious instant fire this guards.
		After.Tick(0.4);
		TestEqual(TEXT("the first post-restore think does not fire"),
			CounterValue(After, TEXT("look_hits")), 0.f);
		After.Tick(0.9);
		TestEqual(TEXT("nor does one still short of the remaining dwell"),
			CounterValue(After, TEXT("look_hits")), 0.f);

		After.Tick(1.1);
		TestEqual(TEXT("the dwell completes only after the remaining time"),
			CounterValue(After, TEXT("look_hits")), 1.f);
		// The restored activator: without it the completed dwell would report an Invalid one.
		TestTrue(TEXT("the completed dwell names the player who stood there"),
			Sink->FirstLine(TEXT("queue"), TEXT("look_hits.Add")).Contains(ActIs(Player)));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWaitMinusOneTest,
	"Elysium.Substrate.WaitMinusOne", GElysiumTestFlags)
bool FElysiumWaitMinusOneTest::RunTest(const FString&)
{
	using namespace ElysiumEventTransportTests;

	// `wait == -1` is a `trigger_multiple` value like any other — trigger_once is only the classname
	// whose Spawn forces it (entity_io.md: "trigger_once's self-removal is not a spawnflag"). So the
	// route is asserted on an AUTHORED -1, where nothing but the keyvalue can be producing it.
	//
	// `ActivateMultiTrigger` takes it into `SetTouch(NULL); SetThink(SUB_Remove); nextthink =
	// curtime + 0.1`: one activation, no further touches, removal a tenth of a second later — and
	// the delayed rows the one activation already queued are ordinary queue entries that outlive the
	// entity that fired them.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__wait_minus_one__");
	FElysiumEntityDef Once = TriggerDef(TEXT("trigger_multiple"), TEXT("selfremove"), TEXT("-1"));
	Wire(Once, TEXT("OnStartTouch"), TEXT("touches"), TEXT("Add"), TEXT("1"));
	Wire(Once, TEXT("OnTrigger"), TEXT("now"), TEXT("Add"), TEXT("1"));
	Wire(Once, TEXT("OnTrigger"), TEXT("later"), TEXT("Add"), TEXT("1"), /*Delay*/ 0.5f);
	Defs.Defs.Add(MoveTemp(Once));
	Defs.Defs.Add(Counter(TEXT("touches")));
	Defs.Defs.Add(Counter(TEXT("now")));
	Defs.Defs.Add(Counter(TEXT("later")));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	FElysiumOrderedIOSink* Sink = Record(World);
	World.SpawnPlayer();
	World.Activate(0.0);

	Enter(World, TEXT("selfremove"));
	World.Tick(0.0);
	TestEqual(TEXT("the one accepted activation fired"), CounterValue(World, TEXT("now")), 1.f);
	TestEqual(TEXT("...and produced its edge output once"), CounterValue(World, TEXT("touches")), 1.f);
	TestTrue(TEXT("its delayed row is queued and intact"), Sink->Saw(TEXT("queue"), TEXT("later.Add")));
	TestEqual(TEXT("the delayed row is the only thing still pending"), World.Queue().Num(), 1);

	// The nulled touch handler is an OnTrigger gate, not a touch gate (entity_io.md, "OnStartTouch
	// still fires inside the wait == -1 removal window"): for the ~0.1 s before SUB_Remove runs the
	// trigger is still a solid FSOLID_TRIGGER volume with a clean deletion flag, so a genuine
	// re-entry still allocates a link and still reaches CBaseTrigger::StartTouch — which carries no
	// wait, removal or handler test of its own. What `SetTouch(NULL)` removes is the second call of
	// the pair, so MultiTouch never runs and no activation is produced.
	Leave(World, TEXT("selfremove"));
	Enter(World, TEXT("selfremove"));
	World.Tick(0.05);
	TestEqual(TEXT("no further activation is admitted"), CounterValue(World, TEXT("now")), 1.f);
	TestEqual(TEXT("but the edge output fires again inside the removal window"),
		CounterValue(World, TEXT("touches")), 2.f);
	TestNotNull(TEXT("the entity is alive until its removal is due"),
		World.FindByName(TEXT("selfremove")));

	// SUB_Remove at +0.1, through the ordinary substrate think — not a synchronous kill inside the
	// touch. The observable difference is exactly this tenth of a second of alive-but-suppressed.
	// Ticked past the deadline rather than exactly on it: NextThink is a float, so the stored
	// `0.0 + 0.1` is 0.100000001490…, strictly greater than the double 0.1.
	World.Tick(0.15);
	TestNull(TEXT("the trigger removes itself at +0.1"), World.FindByName(TEXT("selfremove")));

	// Kill() never touches the event queue, so the row a dead entity queued still binds to its
	// target by name at service time and delivers.
	World.Tick(0.6);
	TestEqual(TEXT("the delayed row delivers after the entity that queued it is gone"),
		CounterValue(World, TEXT("later")), 1.f);
	TestEqual(TEXT("the queue drained"), World.Queue().Num(), 0);
	TestEqual(TEXT("a dead caller is not a dead wire"), World.UnknownTargets(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDyingTriggerEndTouchTest,
	"Elysium.Substrate.DyingTriggerEndTouch", GElysiumTestFlags)
bool FElysiumDyingTriggerEndTouchTest::RunTest(const FString&)
{
	using namespace ElysiumEventTransportTests;

	// The closing asymmetry of entity_io.md's touch-dispatch recovery: `~CBaseEntity` reaches
	// CBaseEntity::PhysicsRemoveTouchedList, which calls PhysicsNotifyOtherOfUntouch for every link
	// — giving each OTHER entity its EndTouch — and then frees the link directly, WITHOUT
	// PhysicsRemoveToucher. The dying entity never receives its own EndTouch, so a trigger_once that
	// removes itself under a standing occupant produces no final OnEndTouch.
	//
	// Ours falls out of Kill() rather than out of a special case: bDead is set before the contact
	// release, so RouteBrushTouch releases the retained pair (a later re-entry stays an edge) and
	// then returns on the inert gate without dispatching OnTouchEnd.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__dying_trigger__");
	FElysiumEntityDef Once = TriggerDef(TEXT("trigger_once"), TEXT("oneshot"));
	Wire(Once, TEXT("OnStartTouch"), TEXT("starts"), TEXT("Add"), TEXT("1"));
	Wire(Once, TEXT("OnEndTouch"), TEXT("ends"), TEXT("Add"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(Once));
	// The control leaf: an ordinary trigger the player walks back out of, so the assertion below is
	// about death and not about ends being broken in general.
	FElysiumEntityDef Multi = TriggerDef(TEXT("trigger_multiple"), TEXT("revolving"), TEXT("0"));
	Wire(Multi, TEXT("OnEndTouch"), TEXT("control_ends"), TEXT("Add"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(Multi));
	Defs.Defs.Add(Counter(TEXT("starts")));
	Defs.Defs.Add(Counter(TEXT("ends")));
	Defs.Defs.Add(Counter(TEXT("control_ends")));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	World.Activate(0.0);

	// The player walks in and STAYS in: nothing but the removal can release this contact.
	Enter(World, TEXT("oneshot"));
	World.Tick(0.0);
	TestEqual(TEXT("the begin edge fired"), CounterValue(World, TEXT("starts")), 1.f);
	TestEqual(TEXT("no end edge yet — the player is still inside"),
		CounterValue(World, TEXT("ends")), 0.f);

	// SUB_Remove at +0.1, ticked past the deadline rather than exactly on it (NextThink is a float).
	World.Tick(0.15);
	TestNull(TEXT("the trigger removed itself"), World.FindByName(TEXT("oneshot")));
	World.Tick(0.2);
	TestEqual(TEXT("a self-removing trigger emits no final OnEndTouch to the occupant it dies under"),
		CounterValue(World, TEXT("ends")), 0.f);
	TestEqual(TEXT("and the release queued nothing that could deliver later"), World.Queue().Num(), 0);

	Enter(World, TEXT("revolving"));
	World.Tick(0.2);
	Leave(World, TEXT("revolving"));
	World.Tick(0.2);
	TestEqual(TEXT("an ordinary walk-out still fires OnEndTouch"),
		CounterValue(World, TEXT("control_ends")), 1.f);

	return true;
}

// =====================================================================================
// The per-wire accounting instrument (`ElysiumWireReport.h`): the tally that turns "are the map's
// events working?" into a number per authored output row, and the report that joins it back to the
// authored surface so a wire nothing ever reached is visible at all.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWireTallyTest,
	"Elysium.Substrate.WireTally", GElysiumTestFlags)
bool FElysiumWireTallyTest::RunTest(const FString&)
{
	using namespace ElysiumEventTransportTests;

	// Every counter on one def set, so the six numbers are asserted against each other rather than
	// one at a time: a fan-out wire has Delivered > Fired, a dead wire has neither, and a spent row
	// is a different finding from one nothing ever reached.
	//
	// The unknown-input row drives the stub tally; silence it so the deliberate gap does not warn.
	IConsoleVariable* Warn = IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.StubWarn"));
	const int32 PrevWarn = Warn ? Warn->GetInt() : 2;
	if (Warn) { Warn->Set(0); }
	ElysiumStub::ClearTally();
	ON_SCOPE_EXIT
	{
		if (Warn) { Warn->Set(PrevWarn); }
		ElysiumStub::ClearTally();
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__wire_tally__");
	FElysiumEntityDef Hub = Relay(TEXT("hub"));
	Wire(Hub, TEXT("OnTrigger"), TEXT("good"),  TEXT("Add"),   TEXT("1"));                    // 0
	Wire(Hub, TEXT("OnTrigger"), TEXT("fan_*"), TEXT("Add"),   TEXT("1"));                    // 1
	Wire(Hub, TEXT("OnTrigger"), TEXT("ghost"), TEXT("Add"),   TEXT("1"));                    // 2
	Wire(Hub, TEXT("OnTrigger"), TEXT("good"),  TEXT("Nudge"), TEXT("1"));                    // 3
	Wire(Hub, TEXT("OnTrigger"), TEXT("spent"), TEXT("Add"),   TEXT("1"), 0.f, /*Times*/ 1);  // 4
	Wire(Hub, TEXT("OnTrigger"), TEXT(""),      TEXT(""),      TEXT(""),  0.f, -1, TEXT("1"));// 5
	// `times 0` is the only shape that is spent before it ever fires, which is the one way the
	// classifier's Exhausted label is reachable — Fired == 0 with a refusal on the record.
	Wire(Hub, TEXT("OnTrigger"), TEXT("good"),  TEXT("Add"),   TEXT("1"), 0.f, /*Times*/ 0);  // 6
	Defs.Defs.Add(MoveTemp(Hub));
	Defs.Defs.Add(Counter(TEXT("good")));
	Defs.Defs.Add(Counter(TEXT("fan_a")));
	Defs.Defs.Add(Counter(TEXT("fan_b")));
	Defs.Defs.Add(Counter(TEXT("spent")));
	FElysiumEntityDef Quiet = Relay(TEXT("quiet"));
	Wire(Quiet, TEXT("OnTrigger"), TEXT("good"), TEXT("Add"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(Quiet));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	const FElysiumEntity* HubEnt = World.FindByName(TEXT("hub"));
	const FElysiumEntity* QuietEnt = World.FindByName(TEXT("quiet"));
	if (!TestNotNull(TEXT("hub resolved"), HubEnt) || !TestNotNull(TEXT("quiet resolved"), QuietEnt))
	{
		return false;
	}
	auto WireOn = [](const FElysiumEntity* Source, int32 Row)
	{
		FElysiumWireRef W;
		W.SourceIndex = Source->Handle.Index;
		W.Output = FName(TEXT("OnTrigger"));
		W.Row = Row;
		return W;
	};
	auto Tally = [&World, &WireOn, HubEnt](int32 Row) { return World.WireTallyFor(WireOn(HubEnt, Row)); };

	Trigger(World, TEXT("hub"));
	World.Tick(0.0);

	// --- (a) the six counters, each on the row that produces it -----------------------------
	TestEqual(TEXT("a plain wire fired once"), Tally(0).Fired, 1);
	TestEqual(TEXT("...and delivered once"), Tally(0).Delivered, 1);

	// A targetname is non-unique and a trailing-`*` fans out, so one fire is many deliveries. This
	// is the asymmetry the instrument exists to record: Delivered is per receiver, Fired per row.
	TestEqual(TEXT("a fan-out wire still fires once"), Tally(1).Fired, 1);
	TestEqual(TEXT("...and delivers once per match"), Tally(1).Delivered, 2);

	TestEqual(TEXT("a dead-target wire fires"), Tally(2).Fired, 1);
	TestEqual(TEXT("...and resolves to nothing"), Tally(2).UnknownTarget, 1);
	TestEqual(TEXT("...which is not a delivery"), Tally(2).Delivered, 0);

	TestEqual(TEXT("a wrong-input wire fires"), Tally(3).Fired, 1);
	TestEqual(TEXT("...reaches a live target that refuses it"), Tally(3).UnknownInput, 1);
	TestEqual(TEXT("...which is also not a delivery"), Tally(3).Delivered, 0);

	TestEqual(TEXT("a times=1 wire fires on its one budget"), Tally(4).Fired, 1);
	TestEqual(TEXT("...and has not been refused yet"), Tally(4).TimesExhausted, 0);

	// The field-6 half is only reachable with a script host, which hangs off the game state — null
	// on a bare substrate world, so DeliverEvent's forward tap never runs here. What IS observable
	// headless is that the row still fires and still queues: PythonForwarded distinguishes "the
	// script failed" from "the script never happened", and this is the second case.
	TestEqual(TEXT("a Python-carrying row fires like any other"), Tally(5).Fired, 1);
	TestEqual(TEXT("...but forwards nothing with no host installed"), Tally(5).PythonForwarded, 0);

	TestEqual(TEXT("a times=0 row is refused without firing"), Tally(6).Fired, 0);
	TestEqual(TEXT("...and the refusal is the only witness it was reached"), Tally(6).TimesExhausted, 1);

	TestTrue(TEXT("a wire nothing reached has no tally row at all"),
		World.WireTallies().Find(WireOn(QuietEnt, 0)) == nullptr);
	TestTrue(TEXT("...and reads all-zero anyway"),
		World.WireTallyFor(WireOn(QuietEnt, 0)).IsUntouched());

	// --- (b) the second fire: only the spent row changes its answer -------------------------
	Trigger(World, TEXT("hub"));
	World.Tick(0.0);
	TestEqual(TEXT("the spent row does not fire again"), Tally(4).Fired, 1);
	TestEqual(TEXT("...and the refusal is counted against its own identity"),
		Tally(4).TimesExhausted, 1);
	TestEqual(TEXT("...so its one delivery stands"), Tally(4).Delivered, 1);
	TestEqual(TEXT("the unlimited row fired twice"), Tally(0).Fired, 2);
	TestEqual(TEXT("and the receiver agrees with the tally"), CounterValue(World, TEXT("good")), 2.f);
	TestEqual(TEXT("the fan-out receivers too"), CounterValue(World, TEXT("fan_a")), 2.f);
	TestEqual(TEXT("the spent row's receiver took exactly one"),
		CounterValue(World, TEXT("spent")), 1.f);

	// --- (c) the report: the authored surface, whether or not it did anything ---------------
	TArray<FElysiumWireReportRow> Report;
	World.BuildWireReport(Report);
	TestEqual(TEXT("every authored row is reported, fired or not"), Report.Num(), 8);

	const FElysiumWireReportRow* QuietRow = Report.FindByPredicate(
		[&](const FElysiumWireReportRow& R) { return R.SourceName == TEXT("quiet"); });
	if (TestNotNull(TEXT("the never-fired wire is in the report"), QuietRow))
	{
		TestTrue(TEXT("with an all-zero tally"), QuietRow->Tally.IsUntouched());
		TestEqual(TEXT("and the NeverFired label"), (int32)ElysiumWireOutcome(*QuietRow),
			(int32)EElysiumWireOutcome::NeverFired);
		TestEqual(TEXT("its authored target rides along"), QuietRow->Target, FString(TEXT("good")));
		TestFalse(TEXT("and it is authored, not runtime-spawned"), QuietRow->bRuntimeSource);
	}

	// The per-output ordinal an offline inventory counts in: all seven hub rows hang off OnTrigger,
	// so their OutputRow is 0..6 in authoring order while Wire.Row is the def-array position.
	if (Report.Num() == 8)
	{
		TestEqual(TEXT("the report is in (entity, def row) order"), Report[3].Wire.Row, 3);
		TestEqual(TEXT("...and states the per-output ordinal beside it"), Report[3].OutputRow, 3);
		TestEqual(TEXT("the Python row carries its source"), Report[5].Python, FString(TEXT("1")));
		TestTrue(TEXT("...and knows it has one"), Report[5].HasPython());
		TestFalse(TEXT("...with no I/O target"), Report[5].HasTarget());
		TestEqual(TEXT("the times=1 row reports its authored budget"), Report[4].AuthoredTimes, 1);
	}

	// Outcome per row, most-diagnostic-first: a wire that both delivered and hit a dead target is a
	// broken wire, not a working one.
	auto OutcomeOf = [&Report](int32 Index)
	{
		return Report.IsValidIndex(Index) ? (int32)ElysiumWireOutcome(Report[Index]) : -1;
	};
	TestEqual(TEXT("row 0 delivered"), OutcomeOf(0), (int32)EElysiumWireOutcome::Delivered);
	TestEqual(TEXT("row 1 delivered (twice over)"), OutcomeOf(1), (int32)EElysiumWireOutcome::Delivered);
	TestEqual(TEXT("row 2 is a dead wire"), OutcomeOf(2), (int32)EElysiumWireOutcome::UnknownTarget);
	TestEqual(TEXT("row 3 reached a receiver that refused it"), OutcomeOf(3),
		(int32)EElysiumWireOutcome::UnknownInput);
	TestEqual(TEXT("row 4 delivered before it was spent"), OutcomeOf(4),
		(int32)EElysiumWireOutcome::Delivered);
	// Fired, nothing resolved: no I/O target to deliver to and no host to forward to. PythonOnly is
	// the same row WITH a host, which is why the two labels are separate.
	TestEqual(TEXT("row 5 fired and resolved nothing headless"), OutcomeOf(5),
		(int32)EElysiumWireOutcome::Pending);
	TestEqual(TEXT("row 6 was spent before it ever fired"), OutcomeOf(6),
		(int32)EElysiumWireOutcome::Exhausted);

	const FElysiumWireSummary Summary = ElysiumWireSummarize(Report);
	TestEqual(TEXT("the whole authored surface is the denominator"), Summary.Authored, 8);
	TestEqual(TEXT("nothing here was runtime-spawned"), Summary.RuntimeRows, 0);
	TestEqual(TEXT("six rows produced a queued delivery"), Summary.Fired, 6);
	TestEqual(TEXT("three resolved cleanly"), Summary.FullyDelivered, 3);
	TestEqual(TEXT("one named nothing"), Summary.UnknownTarget, 1);
	TestEqual(TEXT("one was refused by its receiver"), Summary.UnknownInput, 1);
	TestEqual(TEXT("one is still unresolved"), Summary.Pending, 1);
	TestEqual(TEXT("one was spent"), Summary.Exhausted, 1);
	TestEqual(TEXT("one was never reached"), Summary.NeverFired, 1);
	TestEqual(TEXT("the labels partition the authored surface"),
		Summary.FullyDelivered + Summary.UnknownTarget + Summary.UnknownInput + Summary.Pending
			+ Summary.Exhausted + Summary.NeverFired, Summary.Authored);
	TestEqual(TEXT("one row carries Python"), Summary.PythonRows, 1);
	TestEqual(TEXT("...which reached no host"), Summary.PythonForwarded, 0);

	// The instrument measures one run, and a reset starts a new one without reloading the map.
	World.ResetWireTallies();
	TestEqual(TEXT("a reset empties the tally"), World.WireTallies().Num(), 0);
	World.BuildWireReport(Report);
	TestEqual(TEXT("...but not the authored surface"), Report.Num(), 8);
	TestEqual(TEXT("...which now reads as never fired"),
		ElysiumWireSummarize(Report).NeverFired, 8);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWireIdentitySaveTest,
	"Elysium.Substrate.WireIdentitySave", GElysiumTestFlags)
bool FElysiumWireIdentitySaveTest::RunTest(const FString&)
{
	using namespace ElysiumEventTransportTests;

	// The identity rides the queued record (FElysiumSaveVersion::WireIdentity) so a delayed
	// delivery that lands on the far side of a save still attributes to the row that produced it
	// rather than reading as an unattributed event. The TALLY is deliberately not saved: it measures
	// one session, and a restore that claimed the writing run's firing history would answer a
	// question nobody asked. Both halves are asserted here, because the second is what makes the
	// first legible.
	auto MakeDefs = []()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__wire_identity_save__");
		FElysiumEntityDef Source = Relay(TEXT("src"));
		Wire(Source, TEXT("OnTrigger"), TEXT("sink"), TEXT("Add"), TEXT("1"), /*Delay*/ 0.5f);
		Defs.Defs.Add(MoveTemp(Source));
		Defs.Defs.Add(Counter(TEXT("sink")));
		return Defs;
	};

	FElysiumEntityWorld Before(nullptr, nullptr);
	Before.Load(MakeDefs());
	Before.Activate(0.0);

	const FElysiumEntity* SrcEnt = Before.FindByName(TEXT("src"));
	if (!TestNotNull(TEXT("src resolved"), SrcEnt))
	{
		return false;
	}
	FElysiumWireRef Authored;
	Authored.SourceIndex = SrcEnt->Handle.Index;
	Authored.Output = FName(TEXT("OnTrigger"));
	Authored.Row = 0;

	Trigger(Before, TEXT("src"));
	Before.Tick(0.0);
	TestEqual(TEXT("the row fired"), Before.WireTallyFor(Authored).Fired, 1);
	TestEqual(TEXT("...and is still pending at its half-second delay"), Before.Queue().Num(), 1);
	TestEqual(TEXT("...having delivered nothing yet"), Before.WireTallyFor(Authored).Delivered, 0);

	FElysiumMapSnapshot Frozen;
	Before.Freeze(Frozen);
	if (TestEqual(TEXT("the pending record was frozen"), Frozen.Queue.Num(), 1))
	{
		TestTrue(TEXT("carrying the wire it came from"), Frozen.Queue[0].Wire == Authored);
	}

	// Through the archive, not just through memory: the schema is the thing under test, so the
	// snapshot is written and read back exactly as a save file would carry it.
	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
		Ar << Frozen;
	}
	FElysiumMapSnapshot Read;
	{
		FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
		Ar << Read;
	}
	if (TestEqual(TEXT("the record survived the archive"), Read.Queue.Num(), 1))
	{
		TestTrue(TEXT("with its wire identity intact"), Read.Queue[0].Wire == Authored);
		TestTrue(TEXT("...and the identity is set, not a default"), Read.Queue[0].Wire.IsSet());
	}

	FElysiumEntityWorld After(nullptr, nullptr);
	After.Load(MakeDefs());
	After.ApplySnapshot(Read);
	After.Activate(0.0);

	TestEqual(TEXT("the restored world starts its own measurement"), After.WireTallies().Num(), 0);
	TestTrue(TEXT("...so the row it inherited reads untouched"),
		After.WireTallyFor(Authored).IsUntouched());
	TestEqual(TEXT("the delayed record came back"), After.Queue().Num(), 1);

	After.Tick(0.6);
	const FElysiumWireTally Restored = After.WireTallyFor(Authored);
	TestEqual(TEXT("the post-restore delivery attributes to the SAME authored row"),
		Restored.Delivered, 1);
	// Fired stays zero: the fire happened in the run that wrote the save. The two counters are what
	// makes a restored delivery legible as one, rather than as a wire that fired out of nowhere.
	TestEqual(TEXT("...while its fire belongs to the run that wrote the save"), Restored.Fired, 0);
	TestEqual(TEXT("the receiver actually took it"), CounterValue(After, TEXT("sink")), 1.f);
	TestEqual(TEXT("the queue drained"), After.Queue().Num(), 0);
	TestEqual(TEXT("and nothing was unattributed"), After.WireTallies().Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLogicStateSaveTest,
	"Elysium.Substrate.LogicStateSave", GElysiumTestFlags)
bool FElysiumLogicStateSaveTest::RunTest(const FString&)
{
	using namespace ElysiumEventTransportTests;

	// The three logic leaves whose derived state is not a registered keyfield, so a restore without
	// their own Serialize comes back to the Spawn() seed and diverges on the NEXT input rather than
	// visibly at load. Each case therefore asserts the divergence, not the field: the observable is
	// what the restored entity does next.
	auto DebugRow = [](const FElysiumEntity* Ent, const TCHAR* Key)
	{
		if (!Ent)
		{
			return FString(TEXT("<no entity>"));
		}
		TArray<TPair<FString, FString>> State;
		Ent->GetDebugState(State);
		for (const TPair<FString, FString>& Row : State)
		{
			if (Row.Key == Key)
			{
				return Row.Value;
			}
		}
		return FString(TEXT("<no row>"));
	};

	// --- (a) math_counter: the OnHitMax edge latch ------------------------------------------
	{
		auto MakeDefs = []()
		{
			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__counter_edge_save__");
			FElysiumEntityDef Meter = Counter(TEXT("meter"));
			Meter.Keys.Add(TEXT("min"), TEXT("0"));
			Meter.Keys.Add(TEXT("max"), TEXT("3"));
			Wire(Meter, TEXT("OnHitMax"), TEXT("maxhits"), TEXT("Add"), TEXT("1"));
			Defs.Defs.Add(MoveTemp(Meter));
			Defs.Defs.Add(Counter(TEXT("maxhits")));
			return Defs;
		};

		FElysiumEntityWorld Before(nullptr, nullptr);
		Before.Load(MakeDefs());
		Before.Activate(0.0);

		Before.AcceptInput(TEXT("meter"), FName(TEXT("Add")), FElysiumVariant::Float(3.0f),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		Before.Tick(0.0);
		TestEqual(TEXT("reaching the bound is an edge"), CounterValue(Before, TEXT("maxhits")), 1.f);
		TestEqual(TEXT("and the latch says so"),
			DebugRow(Before.FindByName(TEXT("meter")), TEXT("At bound")), FString(TEXT("max")));

		FElysiumMapSnapshot Clamped;
		Before.Freeze(Clamped);

		FElysiumEntityWorld After(nullptr, nullptr);
		After.Load(MakeDefs());
		After.ApplySnapshot(Clamped);
		After.Activate(0.0);
		TestEqual(TEXT("the value came back at the bound"), CounterValue(After, TEXT("meter")), 3.f);
		TestEqual(TEXT("...and so did the latch that says it was already there"),
			DebugRow(After.FindByName(TEXT("meter")), TEXT("At bound")), FString(TEXT("max")));

		// The divergence: a restored-false latch would read this in-bound Set as a fresh crossing.
		After.AcceptInput(TEXT("meter"), FName(TEXT("Add")), FElysiumVariant::Float(1.0f),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		After.Tick(0.0);
		TestEqual(TEXT("a Set that stays clamped does not re-fire OnHitMax"),
			CounterValue(After, TEXT("maxhits")), 1.f);

		// ...and the latch is still a latch, not a permanent mute.
		After.AcceptInput(TEXT("meter"), FName(TEXT("SetValue")), FElysiumVariant::Float(1.0f),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		After.Tick(0.0);
		TestEqual(TEXT("stepping off the bound clears it"),
			DebugRow(After.FindByName(TEXT("meter")), TEXT("At bound")), FString(TEXT("no")));
		After.AcceptInput(TEXT("meter"), FName(TEXT("Add")), FElysiumVariant::Float(5.0f),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		After.Tick(0.0);
		TestEqual(TEXT("crossing back in fires again"), CounterValue(After, TEXT("maxhits")), 2.f);
	}

	// --- (b) logic_case_toggle: the advanced current-case pointer ---------------------------
	{
		auto MakeDefs = []()
		{
			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__case_toggle_save__");
			FElysiumEntityDef Toggle;
			Toggle.Classname = TEXT("logic_case_toggle");
			Toggle.TargetName = TEXT("toggle");
			Toggle.Keys.Add(TEXT("Case01"), TEXT("a"));
			Toggle.Keys.Add(TEXT("Case02"), TEXT("b"));
			Toggle.Keys.Add(TEXT("Case03"), TEXT("c"));
			Toggle.Keys.Add(TEXT("InitialCase"), TEXT("1"));
			Wire(Toggle, TEXT("OnCase01"), TEXT("c1"), TEXT("Add"), TEXT("1"));
			Wire(Toggle, TEXT("OnCase02"), TEXT("c2"), TEXT("Add"), TEXT("1"));
			Wire(Toggle, TEXT("OnCase03"), TEXT("c3"), TEXT("Add"), TEXT("1"));
			Defs.Defs.Add(MoveTemp(Toggle));
			Defs.Defs.Add(Counter(TEXT("c1")));
			Defs.Defs.Add(Counter(TEXT("c2")));
			Defs.Defs.Add(Counter(TEXT("c3")));
			return Defs;
		};
		auto Advance = [](FElysiumEntityWorld& World)
		{
			World.AcceptInput(TEXT("toggle"), FName(TEXT("InValueDelta")), FElysiumVariant::Int(1),
				FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
			World.Tick(0.0);
		};

		FElysiumEntityWorld Before(nullptr, nullptr);
		Before.Load(MakeDefs());
		Before.Activate(0.0);

		// Spawn prepositions the pointer one configured slot BEHIND InitialCase, so the first
		// +1 delta lands on case 01.
		Advance(Before);
		TestEqual(TEXT("the first delta selects the initial case"),
			CounterValue(Before, TEXT("c1")), 1.f);

		FElysiumMapSnapshot Advanced;
		Before.Freeze(Advanced);

		FElysiumEntityWorld After(nullptr, nullptr);
		After.Load(MakeDefs());
		After.ApplySnapshot(Advanced);
		After.Activate(0.0);
		TestEqual(TEXT("the restored pointer reads where it was left"),
			DebugRow(After.FindByName(TEXT("toggle")), TEXT("Current case")), FString(TEXT("01")));
		// The receivers restore with the run's history on them, so the divergence below is read as a
		// change from these values rather than from zero.
		TestEqual(TEXT("case 01's receiver came back with its hit"),
			CounterValue(After, TEXT("c1")), 1.f);
		TestEqual(TEXT("...and case 02's with none"), CounterValue(After, TEXT("c2")), 0.f);

		// The divergence: a pointer back at its InitialCase seed would select case 01 a second time.
		Advance(After);
		TestEqual(TEXT("the next delta continues from the restored case"),
			CounterValue(After, TEXT("c2")), 1.f);
		TestEqual(TEXT("...and does not restart at the initial one"),
			CounterValue(After, TEXT("c1")), 1.f);
	}

	// --- (c) func_brush: the runtime Enable/Disable latch ------------------------------------
	{
		auto MakeDefs = []()
		{
			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__func_brush_save__");
			FElysiumEntityDef Gate;
			Gate.Classname = TEXT("func_brush");
			Gate.TargetName = TEXT("gate");
			// Authored ENABLED: StartDisabled is a saved keyfield and would restore the answer on its
			// own, which is exactly the confusion this case has to avoid.
			Gate.Keys.Add(TEXT("StartDisabled"), TEXT("0"));
			Defs.Defs.Add(MoveTemp(Gate));
			return Defs;
		};

		FElysiumEntityWorld Before(nullptr, nullptr);
		Before.Load(MakeDefs());
		Before.Activate(0.0);
		Before.Tick(0.0);   // the one-shot think that seats the authored solidity
		TestEqual(TEXT("an authored-enabled brush collides"),
			DebugRow(Before.FindByName(TEXT("gate")), TEXT("Collides")), FString(TEXT("yes")));

		Before.AcceptInput(TEXT("gate"), FName(TEXT("Disable")), FElysiumVariant::Void(),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		TestEqual(TEXT("Disable drops it"),
			DebugRow(Before.FindByName(TEXT("gate")), TEXT("Collides")), FString(TEXT("no")));

		FElysiumMapSnapshot Off;
		Before.Freeze(Off);

		// The control: the same defs with no restore come back enabled, so the assertions below are
		// about the payload and not about func_brush defaulting off.
		FElysiumEntityWorld Fresh(nullptr, nullptr);
		Fresh.Load(MakeDefs());
		Fresh.Activate(0.0);
		Fresh.Tick(0.0);
		TestEqual(TEXT("a fresh load of the same map is enabled"),
			DebugRow(Fresh.FindByName(TEXT("gate")), TEXT("Enabled")), FString(TEXT("yes")));

		FElysiumEntityWorld After(nullptr, nullptr);
		After.Load(MakeDefs());
		After.ApplySnapshot(Off);
		After.Activate(0.0);
		TestEqual(TEXT("the restored brush is disabled"),
			DebugRow(After.FindByName(TEXT("gate")), TEXT("Enabled")), FString(TEXT("no")));
		// The physical half. ApplySnapshot's tail calls OnDormancyChanged once every restored field
		// and leaf byte has landed, and func_brush routes that through ApplyBrushSolidity — so the
		// body's collision switch is re-seated from the restored latch rather than left at the value
		// the spawn-time think wrote. Headless there is no body (no owning actor), so `Collides` is
		// that same decision read one step before it reaches SetDormant.
		TestEqual(TEXT("...and its collision was re-applied, not left at the spawn value"),
			DebugRow(After.FindByName(TEXT("gate")), TEXT("Collides")), FString(TEXT("no")));

		// The restored think must not undo it either: the one-shot already ran before the save.
		After.Tick(1.0);
		TestEqual(TEXT("and no later think re-enables it"),
			DebugRow(After.FindByName(TEXT("gate")), TEXT("Collides")), FString(TEXT("no")));

		After.AcceptInput(TEXT("gate"), FName(TEXT("Toggle")), FElysiumVariant::Void(),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		TestEqual(TEXT("a Toggle on the restored latch turns it back on"),
			DebugRow(After.FindByName(TEXT("gate")), TEXT("Collides")), FString(TEXT("yes")));
	}

	return true;
}

} // namespace ElysiumEntityIOTests

#endif // WITH_DEV_AUTOMATION_TESTS
