// Content-free Substrate automation: entity-world round trip, container payload, and schema coverage.
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
#include "Tests/ElysiumEntityDebugStateTestHelpers.h"
#include "Tests/ElysiumOverlapTestProbe.h"
#include "Tests/ElysiumSaveTestHelpers.h"
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
namespace ElysiumSaveTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
}

namespace ElysiumSaveTestHelpers
{
FElysiumEntityDefs MakeSaveTestDefs()
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__save_test__");

	FElysiumEntityDef Relay;
	Relay.Classname = TEXT("logic_relay");
	Relay.TargetName = TEXT("relay1");
	{
		FElysiumOutputDef Wire;
		Wire.Name = TEXT("OnTrigger");
		Wire.Target = TEXT("counter1");
		Wire.Input = TEXT("Add");
		Wire.Param = TEXT("5");
		Wire.Times = 2;
		Relay.Outputs.Add(Wire);
	}
	Defs.Defs.Add(MoveTemp(Relay));

	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Counter.Keys.Add(TEXT("max"), TEXT("100"));
	Defs.Defs.Add(MoveTemp(Counter));

	FElysiumEntityDef Timer;
	Timer.Classname = TEXT("logic_timer");
	Timer.TargetName = TEXT("timer1");
	Timer.Keys.Add(TEXT("RefireTime"), TEXT("5"));
	Defs.Defs.Add(MoveTemp(Timer));

	return Defs;
}

TArray<uint8> ElysiumSaveDigest(const FElysiumMapSnapshot& Snapshot)
{
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
	FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
	Ar << const_cast<FElysiumMapSnapshot&>(Snapshot);
	return Bytes;
}

float SaveTestCounterValue(const FElysiumEntity* Entity)
{
	return ElysiumEntityDebugTest::CounterValue(Entity);
}
}

namespace ElysiumSaveTests
{
using ElysiumSaveTestHelpers::ElysiumSaveDigest;
using ElysiumSaveTestHelpers::MakeSaveTestDefs;
using ElysiumSaveTestHelpers::SaveTestCounterValue;

// =====================================================================================
// Persistence needs a freeze/rebuild/apply/re-freeze digest round trip on a bare world, payload
// version and integrity gates, and schema rules that let a payload survive a later build.
// =====================================================================================

// ============================================================================================
// CLogicRelay::InputTrigger (FUN_101364e0): spawnflag 0x1 removes the relay once it has fired, and
// without 0x2 a fired relay locks out re-entry until its longest delayed output has gone out.
// ============================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLogicRelayLifetimeTest,
	"Elysium.Substrate.LogicRelayLifetime", GElysiumTestFlags)
bool FElysiumLogicRelayLifetimeTest::RunTest(const FString&)
{
	auto AddRelay = [](FElysiumEntityDefs& Defs, const TCHAR* Name, const TCHAR* Spawnflags)
	{
		FElysiumEntityDef Relay;
		Relay.Classname = TEXT("logic_relay");
		Relay.TargetName = Name;
		if (Spawnflags) { Relay.Keys.Add(TEXT("spawnflags"), Spawnflags); }
		FElysiumOutputDef Wire;
		Wire.Name = TEXT("OnTrigger");
		Wire.Target = TEXT("counter1");
		Wire.Input = TEXT("Add");
		Wire.Param = TEXT("5");
		Wire.Times = -1;
		Relay.Outputs.Add(MoveTemp(Wire));
		Defs.Defs.Add(MoveTemp(Relay));
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__relay_lifetime__");
	AddRelay(Defs, TEXT("plain"), nullptr);
	AddRelay(Defs, TEXT("oneshot"), TEXT("1"));
	{
		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Defs.Defs.Add(MoveTemp(Counter));
	}

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	auto CounterValue = [&World]() -> float
	{
		return SaveTestCounterValue(World.FindByName(TEXT("counter1")));
	};

	FElysiumEntity* Plain = World.FindByName(TEXT("plain"));
	FElysiumEntity* OneShot = World.FindByName(TEXT("oneshot"));
	if (!TestNotNull(TEXT("plain relay resolved"), Plain)
		|| !TestNotNull(TEXT("oneshot relay resolved"), OneShot))
	{
		return false;
	}

	auto Trigger = [&World](FElysiumEntity* Relay, double Now)
	{
		World.EnqueueInput(TEXT("!self"), FName(TEXT("Trigger")), FElysiumVariant::Void(),
			/*Delay*/ 0.0, FElysiumEntityHandle::Invalid(), Relay->Handle);
		World.Tick(Now);
	};

	// --- The refire lockout ---------------------------------------------------------------------
	Trigger(Plain, 0.0);
	TestEqual(TEXT("the relay fired once"), CounterValue(), 5.0f);

	// A second Trigger inside the lockout is swallowed. The relay's OnTrigger rows carry no delay,
	// so the queued EnableRefire is due at 0.001.
	Trigger(Plain, 0.0);
	TestEqual(TEXT("a re-trigger inside the lockout is swallowed"), CounterValue(), 5.0f);

	World.Tick(0.002);   // deliver EnableRefire
	Trigger(Plain, 0.002);
	TestEqual(TEXT("the relay fires again once the lockout has lifted"), CounterValue(), 10.0f);

	// --- Remove on fire -------------------------------------------------------------------------
	Trigger(OneShot, 0.002);
	TestEqual(TEXT("the one-shot relay fired"), CounterValue(), 15.0f);
	TestNull(TEXT("a REMOVE_ON_FIRE relay is gone from name lookup"),
		World.FindByName(TEXT("oneshot")));

	// Its handle no longer resolves, so a second Trigger addressed at it goes nowhere.
	Trigger(OneShot, 0.002);
	TestEqual(TEXT("and cannot fire a second time"), CounterValue(), 15.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSaveRoundTripTest, "Elysium.Substrate.SaveRoundTrip",
	GElysiumTestFlags)
bool FElysiumSaveRoundTripTest::RunTest(const FString&)
{
	// --- Build, mutate through the real chokepoints, freeze ------------------------------------
	FElysiumEntityWorld A(/*Owner*/ nullptr, /*GameState*/ nullptr);
	A.Load(MakeSaveTestDefs());
	A.Activate(0.0);

	FElysiumEntity* Relay = A.FindByName(TEXT("relay1"));
	FElysiumEntity* Counter = A.FindByName(TEXT("counter1"));
	FElysiumEntity* Timer = A.FindByName(TEXT("timer1"));
	if (!TestNotNull(TEXT("relay1"), Relay) || !TestNotNull(TEXT("counter1"), Counter)
		|| !TestNotNull(TEXT("timer1"), Timer))
	{
		return false;
	}

	// One wire delivery: the counter advances and the relay's `times` counts down.
	A.EnqueueInput(TEXT("!self"), FName(TEXT("Trigger")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), Relay->Handle);
	for (int32 i = 0; i < 4; ++i) { A.Tick(0.0); }
	TestEqual(TEXT("counter advanced before the freeze"), SaveTestCounterValue(Counter), 5.0f);

	// The dormancy switch, a runtime-created entity, and a delayed event still pending.
	Timer->ScriptHide();
	FElysiumEntityDef Spawned;
	Spawned.Classname = TEXT("math_counter");
	Spawned.TargetName = TEXT("runtime1");
	const FElysiumEntityHandle SpawnedHandle = A.SpawnRuntimeEntity(MoveTemp(Spawned));
	TestTrue(TEXT("runtime entity created"), A.Resolve(SpawnedHandle) != nullptr);

	A.EnqueueInput(TEXT("counter1"), FName(TEXT("Add")), FElysiumVariant::Int(3), /*Delay*/ 30.0,
		FElysiumEntityHandle::Invalid(), Relay->Handle);
	// Two: this delayed Add, plus the relay's own refire lockout. Triggering relay1 above queued an
	// EnableRefire at max(OnTrigger delay) + 0.001 = 0.001, and the ticks above all run at t=0, so it
	// is never due and is still pending here.
	TestEqual(TEXT("both events still pending at freeze time"), A.Queue().Num(), 2);

	FElysiumMapSnapshot First;
	A.Freeze(First);
	TestEqual(TEXT("the frozen map names itself"), First.MapName, FString(TEXT("__save_test__")));
	TestEqual(TEXT("the pending deliveries are in the snapshot"), First.Queue.Num(), 2);
	// The omission rule: only entities that differ from a fresh build of their own def are written.
	TestTrue(TEXT("something was recorded"), First.Entities.Num() >= 3);
	TestTrue(TEXT("but not more than the world holds"), First.Entities.Num() <= A.NumEntities());

	// --- Rebuild from the same defs, apply, re-freeze, compare digests -------------------------
	FElysiumEntityWorld B(/*Owner*/ nullptr, /*GameState*/ nullptr);
	B.Load(MakeSaveTestDefs());
	const int32 AppliedCount = B.ApplySnapshot(First);
	B.Activate(0.0);
	TestEqual(TEXT("every record applied"), AppliedCount, First.Entities.Num());

	FElysiumEntity* CounterB = B.FindByName(TEXT("counter1"));
	FElysiumEntity* TimerB = B.FindByName(TEXT("timer1"));
	if (!TestNotNull(TEXT("counter1 after restore"), CounterB)
		|| !TestNotNull(TEXT("timer1 after restore"), TimerB))
	{
		return false;
	}
	TestEqual(TEXT("the counter's value survived"), SaveTestCounterValue(CounterB), 5.0f);
	TestTrue(TEXT("the hidden timer is still hidden"), TimerB->IsHidden());
	TestNotNull(TEXT("the runtime entity restored under its own name"), B.FindByName(TEXT("runtime1")));
	TestEqual(TEXT("the queue was replaced, not appended to"), B.Queue().Num(), 2);
	// Address the delayed Add by name rather than by queue position — the relay's 0.001s refire
	// lockout sorts ahead of it.
	const FElysiumIOEvent* Delayed = B.Queue().Pending().FindByPredicate(
		[](const FElysiumIOEvent& E) { return E.Input == FName(TEXT("Add")); });
	if (TestNotNull(TEXT("the delayed Add survived the round trip"), Delayed))
	{
		TestEqual(TEXT("its fire time is absolute and unchanged"), Delayed->FireTime, 30.0);
		// §6 — a saved handle is re-stamped against the live epoch, so it resolves again.
		if (const FElysiumEntity* Caller = B.Resolve(Delayed->Caller))
		{
			TestEqual(TEXT("the caller handle re-resolved to the entity that queued it"),
				Caller->Handle.Index, Relay->Handle.Index);
		}
		else
		{
			AddError(TEXT("the restored caller handle did not resolve"));
		}
	}

	FElysiumMapSnapshot Second;
	B.Freeze(Second);
	// §10's round trip is a digest comparison rather than a semantic diff, and it can be because
	// §8 makes the walk order stable: entities by index, fields sorted by name.
	TestEqual(TEXT("freeze -> rebuild -> apply -> freeze is byte-identical"),
		ElysiumSaveDigest(Second), ElysiumSaveDigest(First));

	// --- Absent entities: what left with the player does not come back -------------------------
	FElysiumMapSnapshot WithAbsent = First;
	WithAbsent.AbsentEntities.Add(Counter->Handle.Index);
	FElysiumEntityWorld C(/*Owner*/ nullptr, /*GameState*/ nullptr);
	C.Load(MakeSaveTestDefs());
	C.ApplySnapshot(WithAbsent);
	TestNull(TEXT("an absent entity is not re-materialised"), C.FindByName(TEXT("counter1")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSavePayloadTest, "Elysium.Substrate.SavePayload",
	GElysiumTestFlags)
bool FElysiumSavePayloadTest::RunTest(const FString&)
{
	// A payload with something in every block.
	FElysiumSavePayload Payload;
	Payload.Session.ClockNow = 123.5;
	Payload.Session.Globals.Emplace(TEXT("Story_State"), FElysiumVariant::Int(-4));
	Payload.Session.Globals.Emplace(TEXT("Tut_Jack"), FElysiumVariant::Int(2));
	Payload.Session.Quests.Emplace(TEXT("tutorial"), 3);
	Payload.Session.RngSessionSeed = 4242;
	ElysiumRng::SeedAll(4242);
	ElysiumRng::Stream(EElysiumRngStream::OneOfSet).GetUnsignedInt();
	ElysiumRng::Snapshot(Payload.Session.Rng);

	Payload.Player.Name = TEXT("Carmilla");
	Payload.Player.Sheet.SetClan(7);
	Payload.Player.Sheet.SetMale(false);
	Payload.Player.Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, 3);
	Payload.Player.Sheet.SetBase(EElysiumTraitContainer::Disciplines, /*Celerity*/ 3, 2);
	Payload.Player.Money = 250;
	Payload.Player.ArmorSlot = 4;
	Payload.Player.Health = 61;
	Payload.Player.MaxHealth = 100;
	Payload.Player.Law.Criminal = 2;
	Payload.Player.ExperienceLog.Add({ TEXT("xp_tutorial"), 0 });
	// The journal rides with the record (9.4d) — the quest map is the Session block's, so a payload
	// that loses these rows keeps the states and forgets the order they were taken in.
	Payload.Player.Journal.Add({ TEXT("Arthur Knox"), /*Table*/ 4, /*Quest*/ 0, /*State*/ 2,
		/*Order*/ 1, /*bUnread*/ true });
	// `m_iCurrQuestLogArea` — the quest log's hub tab is player state in VtMB, not the panel's, so
	// it has to survive a save the way the sheet does.
	Payload.Player.QuestLogArea = 1;

	FElysiumMapSnapshot Snap;
	Snap.MapName = TEXT("sp_tutorial_1");
	Snap.DefCount = 12;
	Snap.QueueNextSerial = 9;
	{
		FElysiumEntityState S;
		S.Index = 4;
		S.ClassName = FName(TEXT("math_counter"));
		S.TargetName = TEXT("counter1");
		S.NextThink = ELYSIUM_NEVER_THINK;
		S.Fields.Emplace(FName(TEXT("startvalue")), FElysiumVariant::Int(5));
		Snap.Entities.Add(MoveTemp(S));
	}
	{
		FElysiumIOEvent E;
		E.FireTime = 30.0;
		E.Target = TEXT("counter1");
		E.Input = FName(TEXT("Add"));
		E.Param = FElysiumVariant::Int(3);
		E.PythonSrc = TEXT("Tut_Advance()");
		E.Caller = FElysiumEntityHandle(4, 77);
		E.Serial = 8;
		Snap.Queue.Add(MoveTemp(E));
	}
	Payload.Maps.Add(Snap.MapName, MoveTemp(Snap));
	Payload.World.CurrentMap = TEXT("sp_tutorial_1");
	Payload.World.PlayerOrigin = FVector(10, 20, 30);
	Payload.World.PlayerYaw = 45.0f;
	Payload.World.bHasPlacement = true;
	Payload.World.VisitedMaps.Add(TEXT("sp_tutorial_1"));

	// --- Write / read ---------------------------------------------------------------------------
	TArray<uint8> Bytes;
	FString Error;
	if (!TestTrue(TEXT("the payload writes"), ElysiumSave::Write(Payload, Bytes, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("the prologue declares this build's schema"),
		ElysiumSave::PeekVersion(Bytes), (int32)FElysiumSaveVersion::Latest);

	FElysiumSavePayload Back;
	if (!TestTrue(TEXT("and reads back"), ElysiumSave::Read(Bytes, Back, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("the clock survived"), Back.Session.ClockNow, 123.5);
	TestEqual(TEXT("G survived"), Back.Session.Globals.Num(), 2);
	TestEqual(TEXT("in order, case-sensitively by key"), Back.Session.Globals[0].Key,
		FString(TEXT("Story_State")));
	TestEqual(TEXT("the quest map survived"), Back.Session.Quests.Num(), 1);
	TestEqual(TEXT("the RNG stream states survived"), Back.Session.Rng.Num(), Payload.Session.Rng.Num());
	TestEqual(TEXT("the clan survived"), Back.Player.Sheet.Clan(), 7);
	TestFalse(TEXT("and the sex"), Back.Player.Sheet.IsMale());
	TestEqual(TEXT("an attribute slot survived"),
		Back.Player.Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Strength), 3);
	TestEqual(TEXT("and so did a slot in another container"),
		Back.Player.Sheet.GetCurrent(EElysiumTraitContainer::Disciplines, 3), 2);
	TestEqual(TEXT("every container came back at its compiled width"),
		Back.Player.Sheet.Base[(uint8)EElysiumTraitContainer::Attributes].Num(),
		ElysiumSheetSlotCount(EElysiumTraitContainer::Attributes));
	TestEqual(TEXT("the PC's name survived"), Back.Player.Name, FString(TEXT("Carmilla")));
	TestEqual(TEXT("the authored player body slot survived"), Back.Player.ArmorSlot, 4);
	TestEqual(TEXT("and the quest log's hub tab"), Back.Player.QuestLogArea, 1);
	TestEqual(TEXT("money survived"), Back.Player.Money, 250);
	TestEqual(TEXT("the law counters survived"), Back.Player.Law.Criminal, 2);
	TestEqual(TEXT("the journal survived"), Back.Player.Journal.Num(), 1);
	if (Back.Player.Journal.Num() == 1)
	{
		const FElysiumAssignedQuest& Row = Back.Player.Journal[0];
		TestEqual(TEXT("with its title"), Row.Title, FString(TEXT("Arthur Knox")));
		TestEqual(TEXT("its quest address"), Row.Table, 4);
		TestEqual(TEXT("its state"), Row.State, 2);
		TestEqual(TEXT("and its display order"), Row.Order, 1);
		TestTrue(TEXT("and the unread marker"), Row.bUnread);
	}
	TestEqual(TEXT("one map snapshot"), Back.Maps.Num(), 1);
	if (const FElysiumMapSnapshot* Read = Back.Maps.Find(TEXT("sp_tutorial_1")))
	{
		TestEqual(TEXT("its entity record survived"), Read->Entities.Num(), 1);
		if (Read->Entities.Num() == 1 && Read->Entities[0].Fields.Num() == 1)
		{
			TestEqual(TEXT("with its field"), Read->Entities[0].Fields[0].Value.ToInt(), 5);
		}
		TestEqual(TEXT("its queue survived"), Read->Queue.Num(), 1);
		if (Read->Queue.Num() == 1)
		{
			// §6 — a deferred script survives as its SOURCE STRING, which is how ScheduleTask does
			// in the original too.
			TestEqual(TEXT("including the deferred script source"), Read->Queue[0].PythonSrc,
				FString(TEXT("Tut_Advance()")));
			TestEqual(TEXT("the serial is preserved, not re-minted"), (int32)Read->Queue[0].Serial, 8);
			// The epoch is dropped on the way out; re-stamping is the applier's job.
			TestEqual(TEXT("a saved handle keeps its index"), Read->Queue[0].Caller.Index, 4);
			TestEqual(TEXT("and loses its epoch"), (int32)Read->Queue[0].Caller.Epoch, 0);
		}
	}
	TestEqual(TEXT("the world placement survived"), Back.World.PlayerYaw, 45.0f);

	// --- The snapshot's leaf schema -------------------------------------------------------------
	// A snapshot built in memory carries `Latest`, because `CaptureState` writes its blobs at
	// `Latest`, and the payload has to bring that number back — a leaf's own version gate reads it,
	// and a blob carries no version of its own.
	if (const FElysiumMapSnapshot* BackSnap = Back.Maps.Find(TEXT("sp_tutorial_1")))
	{
		TestEqual(TEXT("the snapshot's leaf schema survived the round trip"),
			BackSnap->SchemaVersion, (int32)FElysiumSaveVersion::Latest);
	}
	else
	{
		AddError(TEXT("the map snapshot did not survive the round trip"));
	}

	// The other half, and the one that matters for an old save: a payload written before the field
	// existed has to answer with the FILE's version rather than with this build's, because that is
	// what its blobs were written at. Built manually for the same reason the v6 player stream below
	// is — asking the current writer to emit a pre-26 snapshot would test today's field list.
	{
		FElysiumMapSnapshot Legacy;
		Legacy.MapName = TEXT("sp_tutorial_1");
		Legacy.DefCount = 12;
		Legacy.QueueNextSerial = 9;
		Legacy.SchemaVersion = FElysiumSaveVersion::Latest;   // must NOT survive; the file decides

		TArray<uint8> LegacyBytes;
		{
			FMemoryWriter Writer(LegacyBytes, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::NpcDisciplines);
			// The pre-26 field list, in order. Every earlier gate is already on at 25.
			Ar << Legacy.MapName << Legacy.DefCount << Legacy.FrozenAt;
			Ar << Legacy.Entities;
			Ar << Legacy.AbsentEntities;
			Ar << Legacy.Queue << Legacy.QueueNextSerial;
			Ar << Legacy.QueueLastEnqueue;
			Ar << Legacy.Fade;
			Ar << Legacy.Weather;
		}

		FMemoryReader Reader(LegacyBytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::NpcDisciplines);
		FElysiumMapSnapshot Restored;
		Ar << Restored;
		TestFalse(TEXT("a pre-26 snapshot reads without running off the end"), Ar.IsError());
		TestEqual(TEXT("...and reports its blobs' schema as the file's own version"),
			Restored.SchemaVersion, (int32)FElysiumSaveVersion::NpcDisciplines);
		TestEqual(TEXT("...with the fields before it intact"), Restored.DefCount, 12);
	}

	// Determinism: the same state writes the same bytes (§8).
	TArray<uint8> Again;
	TestTrue(TEXT("a second write succeeds"), ElysiumSave::Write(Payload, Again, Error));
	TestEqual(TEXT("two writes of one state are byte-identical"), Again, Bytes);

	// --- The integrity gates -------------------------------------------------------------------
	FElysiumSavePayload Rejected;
	TArray<uint8> Garbage;
	Garbage.AddZeroed(64);
	TestFalse(TEXT("a foreign blob is refused"), ElysiumSave::Read(Garbage, Rejected, Error));
	TestTrue(TEXT("with a readable reason"), Error.Contains(TEXT("Elysium payload")));

	TArray<uint8> Truncated(Bytes.GetData(), 8);
	TestFalse(TEXT("a truncated payload is refused"), ElysiumSave::Read(Truncated, Rejected, Error));

	// `ScriptedBody` writes the cutscene body state mid-record inside two leaf blocks — a scene's
	// frozen cast and a beat's NPC claim — so an older payload would read those bytes as the fields
	// that followed them. It is therefore a breaking schema, not an additive one, and it carries the
	// floor up with it.
	TestEqual(TEXT("the floor is the scripted-body schema"),
		(int32)FElysiumSaveVersion::MinSupported, (int32)FElysiumSaveVersion::ScriptedBody);
	// `Feeding` appends an in-progress feed to the END of the player record and reads it behind its
	// own version, so it is additive: a `ScriptedBody` payload restores with no feed rather than
	// being refused, and the floor stays where the last breaking schema left it.
	TestTrue(TEXT("feeding is additive, so it did not move the floor"),
		(int32)FElysiumSaveVersion::MinSupported < (int32)FElysiumSaveVersion::Feeding);
	// `EventClock` adds the queue's backward-clock guard state beside the queue it guards. The field
	// sits mid-record but is written and read behind its own version, so a `Feeding` payload skips
	// those bytes and restores with the guard at its default — additive for the same reason, and the
	// floor stays put again.
	TestTrue(TEXT("the event-clock guard is additive, so the floor did not move with it"),
		(int32)FElysiumSaveVersion::MinSupported < (int32)FElysiumSaveVersion::EventClock);
	// `WireIdentity` appends the authored output row a pending queue record came from to the END of
	// the event record, read behind its own version — so an `EventClock` payload restores with an
	// unset wire rather than being refused. Additive again, and again the floor stays where the last
	// breaking schema left it.
	TestTrue(TEXT("wire identity remains additive"),
		(int32)FElysiumSaveVersion::MinSupported < (int32)FElysiumSaveVersion::WireIdentity);
	// `NpcMaker` appends owner/notification state to the NPC leaf behind its own version, `NpcMind`
	// the resumable state/body intent after it, `NpcSchedule` the running idle schedule,
	// `NpcSocial` the independent relationship table, `Activation` the entity lifecycle latch, and
	// `NpcSenses` the sensory memory at the very end of the NPC leaf, and `NpcCognition` the
	// repeated-damage window plus the eluded marker at the end of that memory record. Each is
	// additive and reads behind its own version, so the supported floor stays at v9 and a legacy
	// entity restores without invented state. The gathered condition set is deliberately NOT in
	// this list: conditions are rebuilt from memory on the first think after a load, which is what
	// the recovered pass does on every think anyway.
	//
	// The equality below is a tripwire, not a fact about npc_maker: it fails the moment a version is
	// appended without this block being extended, which is exactly when someone should be made to
	// think about whether the new field is additive and what an old payload does without it.
	//
	// `Law` is the one entry in this list that is NOT purely appended: the activity-channel
	// deadlines and act counts sit mid-record, inside `operator<<(FElysiumLawState&)` between the XP
	// accumulators and `bUnkillable`. They are still additive because that operator gates them on
	// this version and a `Stealth` payload skips those bytes entirely, restoring three bare levels
	// with the "no deadline" sentinel — which is exactly what the pre-law build wrote. The
	// police-response block that came with it is an ordinary append to the end of the record.
	//
	// `NpcWitness` appends the NPC's retained law witness block — the two channels' processed
	// counts, witnessed records and ignore deadlines, plus the flee-only flag and the Nosferatu
	// deadline — to the END of the NPC leaf behind its own version. Additive: a `Law` payload
	// restores an NPC that has witnessed nothing with its three windows at the spawn-zero default.
	//
	// `NpcDisciplines` appends the NPC's own discipline state — the targeted effects a cast tracked
	// on it, their groups and expiry serials, and the per-record recovery deadlines — to the END of
	// the NPC leaf behind its own version. Additive: an `NpcWitness` payload restores an NPC
	// carrying no discipline state, which is exactly what the build before it wrote.
	//
	// `WeaponAnimEvent` writes which route a staged weapon transaction is waiting on — the playing
	// clip's own sequence event, or the `ContactEventCycle` estimate the accept already queued. The
	// flag sits mid-record inside the weapon leaf's swing block, behind its own version.
	//
	// **A leaf's version gate is only meaningful because the snapshot now records the schema its
	// blobs were written at.** `FElysiumEntityState::LeafState` is opaque bytes replayed through a
	// private archive that has no version of its own, so before `FElysiumMapSnapshot::SchemaVersion`
	// every leaf gate from `NpcMaker` onwards read as `Latest` no matter how old the payload was —
	// which byte-shifts an old blob rather than skipping the field. With the schema recorded, an
	// `NpcDisciplines` blob is replayed at 25, its gate is false, and the flag defaults to the
	// estimate route, which is the only route a build before it could have written.
	// `Elysium.Substrate.Weapons.LeafSchema` is that mechanism end to end.
	// `WeaponSwingClipOwner` appends the bank that owns the staged swing's resolved clip to the END
	// of that same swing block, behind its own version. Additive: a `WeaponAnimEvent` payload
	// restores a swing with no owner, and only the contact's diagnostic line is poorer for it — the
	// blocked reaction itself is addressed by the body stem and `ClipLabel`, which v26 also carries.
	TestEqual(TEXT("the newest schema is the one this test knows about"),
		(int32)FElysiumSaveVersion::Latest, (int32)FElysiumSaveVersion::WeaponSwingClipOwner);
	for (const TPair<const TCHAR*, int32>& Appended : {
		TPair<const TCHAR*, int32>(TEXT("npc_maker ownership"), (int32)FElysiumSaveVersion::NpcMaker),
		TPair<const TCHAR*, int32>(TEXT("npc mind state"), (int32)FElysiumSaveVersion::NpcMind),
		TPair<const TCHAR*, int32>(TEXT("npc schedule"), (int32)FElysiumSaveVersion::NpcSchedule),
		TPair<const TCHAR*, int32>(TEXT("npc social state"), (int32)FElysiumSaveVersion::NpcSocial),
		TPair<const TCHAR*, int32>(TEXT("activation lifecycle"), (int32)FElysiumSaveVersion::Activation),
		TPair<const TCHAR*, int32>(TEXT("npc sensory memory"), (int32)FElysiumSaveVersion::NpcSenses),
		TPair<const TCHAR*, int32>(TEXT("npc cognition memory"), (int32)FElysiumSaveVersion::NpcCognition),
		TPair<const TCHAR*, int32>(TEXT("npc combat loadout and detected-attack memory"),
			(int32)FElysiumSaveVersion::NpcCombat),
		TPair<const TCHAR*, int32>(TEXT("the player's discipline block"),
			(int32)FElysiumSaveVersion::Disciplines),
		TPair<const TCHAR*, int32>(TEXT("the player's stealth surface and raw modifier aggregate"),
			(int32)FElysiumSaveVersion::Stealth),
		TPair<const TCHAR*, int32>(TEXT("the law deadlines/act counts and the police-response block"),
			(int32)FElysiumSaveVersion::Law),
		TPair<const TCHAR*, int32>(TEXT("the NPC's retained law witness block"),
			(int32)FElysiumSaveVersion::NpcWitness),
		TPair<const TCHAR*, int32>(TEXT("the NPC's tracked discipline effects"),
			(int32)FElysiumSaveVersion::NpcDisciplines),
		TPair<const TCHAR*, int32>(TEXT("the weapon transaction's commit route"),
			(int32)FElysiumSaveVersion::WeaponAnimEvent),
		TPair<const TCHAR*, int32>(TEXT("the staged swing's clip owner"),
			(int32)FElysiumSaveVersion::WeaponSwingClipOwner) })
	{
		TestTrue(*FString::Printf(TEXT("%s is additive"), Appended.Key),
			(int32)FElysiumSaveVersion::MinSupported < Appended.Value);
	}

	// Build the exact v6 player byte stream (which has no ArmorSlot field) and read it through the
	// current operator. This is deliberately manual: asking the current writer to emit v6 would
	// test today's field list rather than the historical layout.
	{
		TArray<uint8> LegacyBytes;
		{
			FMemoryWriter Writer(LegacyBytes, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::History);
			FElysiumPlayerRecord Legacy = Payload.Player;
			Ar << Legacy.Name << Legacy.Sheet << Legacy.Money;
			Ar << Legacy.Health << Legacy.MaxHealth;
			Ar << Legacy.ExperienceLog << Legacy.Effects << Legacy.EmailFlags;
			Ar << Legacy.ExperienceRemainder << Legacy.LifetimeExperience;
			// `FElysiumLawState`'s own operator writes its cycle-10b deadline/count fields whenever
			// the archive is saving, regardless of the declared version (correct for a real save,
			// which is always written at `Latest`) — so reproducing v6's byte-for-byte shape means
			// writing only its three bare levels directly rather than delegating to that operator.
			Ar << Legacy.Law.Criminal << Legacy.Law.Supernatural << Legacy.Law.Investigate;
			Ar << Legacy.bUnkillable << Legacy.Journal << Legacy.QuestLogArea
				<< Legacy.HistoryId;
		}
		FElysiumPlayerRecord Migrated;
		Migrated.ArmorSlot = 5;
		FMemoryReader Reader(LegacyBytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::History);
		Ar << Migrated;
		TestEqual(TEXT("a v6 player migrates to armor slot zero"), Migrated.ArmorSlot, 0);
		TestEqual(TEXT("v6 fields after the inserted slot stay aligned"), Migrated.Health,
			Payload.Player.Health);
	}

	// 13.2 — the discipline block round-trips on the player record. It is appended behind its own
	// version at the END of the record, so it is additive: an older supported payload restores with
	// no disciplines rather than being refused.
	{
		FElysiumPlayerRecord Cast = Payload.Player;
		Cast.DisciplineMap = TEXT("__save_test__");
		Cast.SelectedDiscipline = 7;
		Cast.SelectedTier = 3;
		Cast.DisciplineCastCount = 4;
		Cast.Disciplines.EndTime[7] = 42.5;
		Cast.Disciplines.ExpirySerial[7] = 9;
		Cast.Disciplines.Groups[7].Add(TEXT("Discipline (Fortitude3)"));
		Cast.Disciplines.SerialCounter = 9;
		Cast.Disciplines.Recovery.Add(TEXT("Thaumaturgy_Bloodshield"), 60.0);
		FElysiumActiveDisciplineEffect Effect;
		Effect.Record = TEXT("Thaumaturgy_Bloodshield");
		Effect.HitTable = TEXT("Hit_Player_Human");
		Effect.Effects.Add(TEXT("Discipline (Thaumaturgy-Bloodshield)"));
		Effect.EndTime = -1.0;
		Effect.Serial = 8;
		Effect.bRemoveOnTakeDamage = true;
		Cast.Disciplines.TargetEffects.Add(MoveTemp(Effect));

		TArray<uint8> RecordBytes;
		{
			FMemoryWriter Writer(RecordBytes, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
			Ar << Cast;
		}
		FElysiumPlayerRecord Restored;
		FMemoryReader Reader(RecordBytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
		Ar << Restored;

		TestEqual(TEXT("the selection survives"), Restored.SelectedDiscipline, 7);
		TestEqual(TEXT("...with its remembered tier"), Restored.SelectedTier, 3);
		TestEqual(TEXT("...and the cast counter"), Restored.DisciplineCastCount, 4);
		TestEqual(TEXT("the block is scoped to the map its owned events ride"),
			Restored.DisciplineMap, FString(TEXT("__save_test__")));
		TestEqual(TEXT("a native state's deadline survives"), Restored.Disciplines.EndTime[7], 42.5);
		TestEqual(TEXT("...with the serial its owned queue event carries"),
			Restored.Disciplines.ExpirySerial[7], 9);
		TestEqual(TEXT("...and the groups that activation installed"),
			Restored.Disciplines.Groups[7].Num(), 1);
		TestEqual(TEXT("the serial counter survives, so a restore cannot reissue a live serial"),
			Restored.Disciplines.SerialCounter, 9);
		TestEqual(TEXT("the recovery deadline survives"), Restored.Disciplines.Recovery.Num(), 1);
		if (TestEqual(TEXT("the tracked targeted effect survives"),
			Restored.Disciplines.TargetEffects.Num(), 1))
		{
			const FElysiumActiveDisciplineEffect& RestoredEffect = Restored.Disciplines.TargetEffects[0];
			TestEqual(TEXT("...naming its record"), RestoredEffect.Record,
				FString(TEXT("Thaumaturgy_Bloodshield")));
			TestTrue(TEXT("...its authored infinite duration"), RestoredEffect.IsInfinite());
			TestTrue(TEXT("...and its interruption flag"), RestoredEffect.bRemoveOnTakeDamage);
		}
	}

	// Corrupt/future body indices cannot escape the authored M_Body0..5 range.
	{
		FElysiumPlayerRecord Invalid = Payload.Player;
		Invalid.ArmorSlot = 99;
		TArray<uint8> RecordBytes;
		{
			FMemoryWriter Writer(RecordBytes, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
			Ar << Invalid;
		}
		FElysiumPlayerRecord Clamped;
		FMemoryReader Reader(RecordBytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
		Ar << Clamped;
		TestEqual(TEXT("a loaded armor slot clamps to the authored maximum"), Clamped.ArmorSlot, 5);
	}

	// A payload stamped below the floor: rejected with a reason, never half-read.
	{
		TArray<uint8> TooOld = Bytes;
		FMemoryWriter Patch(TooOld, /*bIsPersistent*/ true);
		uint32 Magic = ElysiumSaveMagic;
		int32 Version = FElysiumSaveVersion::MinSupported - 1;
		Patch << Magic << Version;
		TestFalse(TEXT("a below-floor schema is refused"), ElysiumSave::Read(TooOld, Rejected, Error));
		TestTrue(TEXT("naming the floor"), Error.Contains(TEXT("floor")));
	}
	{
		TArray<uint8> TooNew = Bytes;
		FMemoryWriter Patch(TooNew, /*bIsPersistent*/ true);
		uint32 Magic = ElysiumSaveMagic;
		int32 Version = FElysiumSaveVersion::Latest + 1;
		Patch << Magic << Version;
		TestFalse(TEXT("a future schema is refused"), ElysiumSave::Read(TooNew, Rejected, Error));
		TestTrue(TEXT("saying so"), Error.Contains(TEXT("newer build")));
	}

	// --- The readable dump ------------------------------------------------------------------
	TArray<FString> Lines;
	ElysiumSave::Describe(Payload, Lines);
	TestTrue(TEXT("the dump names a G flag"),
		Lines.ContainsByPredicate([](const FString& L) { return L.Contains(TEXT("session.G.Story_State")); }));
	TestTrue(TEXT("and the map's queued delivery"),
		Lines.ContainsByPredicate([](const FString& L) { return L.Contains(TEXT("queue @30.000")); }));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSaveSchemaTest, "Elysium.Substrate.SaveSchema",
	GElysiumTestFlags)
bool FElysiumSaveSchemaTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("has no saved field 'a_field_from_the_future'"),
		EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("is math_counter here but was logic_relay when saved"),
		EAutomationExpectedErrorFlags::Contains, 1);
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();

	// --- The field flags ARE the enumeration (§4) ----------------------------------------------
	const FElysiumClassDesc* CounterDesc = Reg.Find(FName(TEXT("math_counter")));
	if (!TestNotNull(TEXT("math_counter is registered"), CounterDesc))
	{
		return false;
	}
	const TArray<FName> SaveNames = Reg.SaveFields(*CounterDesc);
	TestTrue(TEXT("the chain walk reaches a leaf field"), SaveNames.Contains(FName(TEXT("startvalue"))));
	TestTrue(TEXT("and a base one"), SaveNames.Contains(FName(TEXT("health"))));
	// Sorted, because §8's digest comparison needs an order that is not a hash map's.
	TArray<FName> Sorted = SaveNames;
	Sorted.Sort(FNameLexicalLess());
	TestEqual(TEXT("the walk is sorted"), SaveNames, Sorted);
	// Derived shadows base, so a name appears once however many tables in the chain carry it.
	const TSet<FName> Unique(SaveNames);
	TestEqual(TEXT("and carries no duplicates"), Unique.Num(), SaveNames.Num());

	// A field registered with neither flag is in neither consumer's list. The player's law counters
	// are the case: their setter is a deliberate no-op and their durable home is the Player block.
	if (const FElysiumClassDesc* PlayerDesc = Reg.Find(ElysiumPlayerClassName()))
	{
		if (const FElysiumFieldAccessor* Law = Reg.FindField(*PlayerDesc, FName(TEXT("criminal_level"))))
		{
			TestFalse(TEXT("a read-only law counter is not save-walked"), Law->bSave);
			TestFalse(TEXT("nor keyable"), Law->bKeyable);
		}
	}

	// --- A field name this build no longer knows is skipped, not fatal (§4) --------------------
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__schema_test__");
	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Defs.Defs.Add(MoveTemp(Counter));

	FElysiumMapSnapshot Snapshot;
	Snapshot.MapName = TEXT("__schema_test__");
	Snapshot.DefCount = 1;
	{
		FElysiumEntityState S;
		S.Index = 0;
		S.ClassName = FName(TEXT("math_counter"));
		S.TargetName = TEXT("counter1");
		S.NextThink = ELYSIUM_NEVER_THINK;
		S.Fields.Emplace(FName(TEXT("startvalue")), FElysiumVariant::Int(11));
		S.Fields.Emplace(FName(TEXT("a_field_from_the_future")), FElysiumVariant::Int(1));
		Snapshot.Entities.Add(MoveTemp(S));
	}

	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));
	TestEqual(TEXT("the record still applied"), World.ApplySnapshot(Snapshot), 1);
	World.Activate(0.0);
	TestEqual(TEXT("the known field landed"),
		SaveTestCounterValue(World.FindByName(TEXT("counter1"))), 11.0f);

	// --- A record whose class changed under the save is skipped, not misapplied ----------------
	FElysiumMapSnapshot Wrong = Snapshot;
	Wrong.Entities[0].ClassName = FName(TEXT("logic_relay"));
	Wrong.Entities[0].Fields.Reset();
	Wrong.Entities[0].Fields.Emplace(FName(TEXT("startvalue")), FElysiumVariant::Int(99));

	FElysiumEntityDefs Defs2;
	Defs2.MapName = TEXT("__schema_test__");
	FElysiumEntityDef Counter2;
	Counter2.Classname = TEXT("math_counter");
	Counter2.TargetName = TEXT("counter1");
	Defs2.Defs.Add(MoveTemp(Counter2));
	FElysiumEntityWorld Other(/*Owner*/ nullptr, /*GameState*/ nullptr);
	Other.Load(MoveTemp(Defs2));
	TestEqual(TEXT("the mismatched record is skipped"), Other.ApplySnapshot(Wrong), 0);
	Other.Activate(0.0);
	TestEqual(TEXT("and nothing was written"),
		SaveTestCounterValue(Other.FindByName(TEXT("counter1"))), 0.0f);

	// --- The owned RNG streams restore their position (§8) -------------------------------------
	ElysiumRng::SeedAll(1234);
	ElysiumRng::Stream(EElysiumRngStream::Dice).RandRange(0, 9);
	TArray<ElysiumRng::FState> State;
	ElysiumRng::Snapshot(State);
	const int32 Next = ElysiumRng::Stream(EElysiumRngStream::Dice).RandRange(0, 9);
	ElysiumRng::Stream(EElysiumRngStream::Dice).RandRange(0, 9);
	ElysiumRng::Restore(State);
	TestEqual(TEXT("a restored stream continues the saved sequence"),
		ElysiumRng::Stream(EElysiumRngStream::Dice).RandRange(0, 9), Next);

	ElysiumRng::SeedAll(5678);
	ElysiumRng::Stream(EElysiumRngStream::NpcMaker).FRandRange(1.0f, 2.0f);
	ElysiumRng::Snapshot(State);
	const double NextMakerRetry =
		ElysiumRng::Stream(EElysiumRngStream::NpcMaker).FRandRange(1.0f, 2.0f);
	ElysiumRng::Stream(EElysiumRngStream::NpcMaker).FRandRange(1.0f, 2.0f);
	ElysiumRng::Restore(State);
	TestEqual(TEXT("the named npc_maker stream continues its saved retry sequence"),
		ElysiumRng::Stream(EElysiumRngStream::NpcMaker).FRandRange(1.0f, 2.0f),
		NextMakerRetry);

	// --- The stream table itself: every enumerator is named, seeded and carried ------------------
	// The block is a length-prefixed array and Restore truncates, so adding a stream is not a format
	// change — but a stream with no name would hand the readable dump a null, and a stream the
	// snapshot did not carry would silently reset across a load. Both are checked over the whole
	// table rather than per stream, so the next enumerator is covered the day it is added.
	{
		const int32 StreamCount = static_cast<int32>(EElysiumRngStream::Count);
		bool bEveryStreamNamed = true;
		for (int32 i = 0; i < StreamCount; ++i)
		{
			const TCHAR* Named = ElysiumRng::Name(static_cast<EElysiumRngStream>(i));
			bEveryStreamNamed = bEveryStreamNamed && Named != nullptr && *Named != TEXT('\0');
		}
		TestTrue(TEXT("every declared stream has a readable name"), bEveryStreamNamed);
		TestEqual(TEXT("the flinch's own stream is named"),
			FString(ElysiumRng::Name(EElysiumRngStream::Reaction)), FString(TEXT("Reaction")));

		ElysiumRng::SeedAll(0x52454143);
		TArray<int32> FirstPass;
		for (int32 i = 0; i < StreamCount; ++i)
		{
			FirstPass.Add(
				ElysiumRng::Stream(static_cast<EElysiumRngStream>(i)).RandHelper(MAX_int32));
		}
		ElysiumRng::SeedAll(0x52454143);
		bool bReproduced = true;
		for (int32 i = 0; i < StreamCount; ++i)
		{
			bReproduced = bReproduced
				&& ElysiumRng::Stream(static_cast<EElysiumRngStream>(i)).RandHelper(MAX_int32)
					== FirstPass[i];
		}
		TestTrue(TEXT("one session seed reproduces every stream"), bReproduced);

		TArray<ElysiumRng::FState> Whole;
		ElysiumRng::Snapshot(Whole);
		TestEqual(TEXT("the snapshot carries one entry per declared stream"),
			Whole.Num(), StreamCount);
		const int32 AfterRestore =
			ElysiumRng::Stream(EElysiumRngStream::Reaction).RandHelper(MAX_int32);
		ElysiumRng::Stream(EElysiumRngStream::Reaction).RandHelper(MAX_int32);
		ElysiumRng::Restore(Whole);
		TestEqual(TEXT("...and restoring it puts the new stream back where it stood"),
			ElysiumRng::Stream(EElysiumRngStream::Reaction).RandHelper(MAX_int32), AfterRestore);
	}

	return true;
}

} // namespace ElysiumSaveTests

#endif // WITH_DEV_AUTOMATION_TESTS
