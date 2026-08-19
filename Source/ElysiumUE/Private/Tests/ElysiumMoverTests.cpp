#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumBrushComponent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumMover.h"
#include "Tests/ElysiumTestServices.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"

static constexpr EAutomationTestFlags GElysiumMoverTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRotatingDoorHandednessTest,
	"Elysium.Substrate.RotatingDoorHandedness", GElysiumMoverTestFlags)

bool FElysiumRotatingDoorHandednessTest::RunTest(const FString&)
{
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

	auto DoorHull = []()
	{
		// An asymmetric leaf extending from its hinge along local +X makes the swing direction
		// observable from the embodied tip, rather than merely inspecting a cached target angle.
		FElysiumConvexHull Hull;
		for (float X : { 0.0f, 100.0f })
		{
			for (float Y : { -5.0f, 5.0f })
			{
				for (float Z : { -100.0f, 100.0f })
				{
					Hull.Vertices.Emplace(X, Y, Z);
				}
			}
		}
		return Hull;
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__rotating_door_handedness__");
	auto AddDoor = [&Defs, &DoorHull](const TCHAR* Name, int32 Model, const FVector& Origin,
		int32 SpawnFlags)
	{
		FElysiumEntityDef Door;
		Door.Classname = TEXT("func_door_rotating");
		Door.TargetName = Name;
		Door.Origin = Origin;
		Door.Model = Model;
		Door.Hulls.Add(DoorHull());
		Door.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Model));
		Door.Keys.Add(TEXT("distance"), TEXT("90"));
		Door.Keys.Add(TEXT("speed"), TEXT("90"));
		Door.Keys.Add(TEXT("wait"), TEXT("-1"));
		Door.Keys.Add(TEXT("spawnflags"), FString::FromInt(SpawnFlags));
		Defs.Defs.Add(MoveTemp(Door));
	};
	AddDoor(TEXT("normal"), 1, FVector::ZeroVector, 0);
	AddDoor(TEXT("reversed"), 2, FVector(500.0f, 0.0f, 0.0f), 0x2);

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumDoorBase* Normal = World.FindByName(TEXT("normal"))
		? World.FindByName(TEXT("normal"))->AsDoorBase() : nullptr;
	FElysiumDoorBase* Reversed = World.FindByName(TEXT("reversed"))
		? World.FindByName(TEXT("reversed"))->AsDoorBase() : nullptr;
	if (!TestNotNull(TEXT("normal rotating door resolved"), Normal)
		|| !TestNotNull(TEXT("reversed rotating door resolved"), Reversed)
		|| !TestNotNull(TEXT("normal rotating door has a body"), Normal ? Normal->Body : nullptr)
		|| !TestNotNull(TEXT("reversed rotating door has a body"), Reversed ? Reversed->Body : nullptr))
	{
		return false;
	}

	Normal->InputOpen(FElysiumEntityHandle());
	Reversed->InputOpen(FElysiumEntityHandle());
	World.Tick(1.0);

	const FVector NormalTip = Normal->Body->GetRelativeRotation().RotateVector(FVector(100.0f, 0.0f, 0.0f));
	const FVector ReversedTip = Reversed->Body->GetRelativeRotation().RotateVector(FVector(100.0f, 0.0f, 0.0f));
	TestTrue(TEXT("a normal Source +90 swing reflects to Unreal -Y"),
		NormalTip.Equals(FVector(0.0f, -100.0f, 0.0f), 0.1f));
	TestTrue(TEXT("REVERSE negates the Source swing before reflection"),
		ReversedTip.Equals(FVector(0.0f, 100.0f, 0.0f), 0.1f));
	TestEqual(TEXT("normal door reaches its open state"), Normal->State(),
		FElysiumDoorBase::EToggleState::AtTop);
	TestEqual(TEXT("reversed door reaches its open state"), Reversed->State(),
		FElysiumDoorBase::EToggleState::AtTop);

	return true;
}

// =====================================================================================
// Locked-door input matrix (retail CBaseDoor, vampire.dll).
//
// Reference: research/event-surface/doors-receivers-findings.md (D010 note, D020, D031, D032,
// D033) + door-divergence-ledger.md rows DL07/DL09/DL12. The retail truth these assert:
//
//   * CBaseDoor has NO OnLockedUse output — that output belongs to prop_switch. A door fires it
//     never, on any path (D010 note).
//   * The direct `Open` input on a locked door refuses SILENTLY: no sound, no output, no motion
//     (D031, FUN_100f0170 opens iff `!IsDoorLocked`).
//   * The direct `Toggle` input on a locked door does nothing: no sound, no output, no motion
//     (D033, FUN_100f0210 gates on `IsDoorLocked` at the top).
//   * The `+use` path on a locked door plays ONLY the `locked` sound — no output, no motion
//     (D020 step-8 locked branch). It never reaches DoorActivate, so a locked *open* door is not
//     closed by +use.
//   * `Close` (FUN_100f00a0) has NO lock check: a direct `Close` input closes a locked door.
//
// The mover sound MANIFEST is a Content-tier disk dependency, so at the content-free Substrate
// tier `SoundSubs` is empty and no `locked` WAV is submitted regardless of path — the sound
// EMISSION POINT is not observable here. These tests therefore assert the output-and-motion
// matrix (the phantom-output removal and the no-motion rule), which is the observable core of the
// slice, plus that the input paths that must be silent submit no audio.
// =====================================================================================

namespace ElysiumLockedDoorTests
{
	FElysiumConvexHull DoorHull()
	{
		// An asymmetric leaf from the hinge along local +X — a real body so motion is observable.
		FElysiumConvexHull Hull;
		for (float X : { 0.0f, 100.0f })
		{
			for (float Y : { -5.0f, 5.0f })
			{
				for (float Z : { -100.0f, 100.0f })
				{
					Hull.Vertices.Emplace(X, Y, Z);
				}
			}
		}
		return Hull;
	}

	// One authored output row targeting `sink_counter` so FireOutput records a `fire` line for it.
	void Wire(FElysiumEntityDef& Def, const TCHAR* Output)
	{
		FElysiumOutputDef Row;
		Row.Name = Output;
		Row.Target = TEXT("sink_counter");
		Row.Input = TEXT("Add");
		Row.Param = TEXT("1");
		Row.Delay = 0.f;
		Row.Times = -1;
		Def.Outputs.Add(MoveTemp(Row));
	}

	// A func_door_rotating carrying rows for every door output whose absence these tests assert, so a
	// stray fire on any of them is recorded by the sink. `SpawnFlags` sets LOCKED / START_OPEN.
	FElysiumEntityDef LockedDoor(const TCHAR* Name, int32 Model, int32 SpawnFlags)
	{
		FElysiumEntityDef Door;
		Door.Classname = TEXT("func_door_rotating");
		Door.TargetName = Name;
		Door.Model = Model;
		Door.Hulls.Add(DoorHull());
		Door.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Model));
		Door.Keys.Add(TEXT("distance"), TEXT("90"));
		Door.Keys.Add(TEXT("speed"), TEXT("90"));
		Door.Keys.Add(TEXT("wait"), TEXT("-1"));   // stay open once opened (no autoclose to race)
		Door.Keys.Add(TEXT("spawnflags"), FString::FromInt(SpawnFlags));
		Wire(Door, TEXT("OnLockedUse"));   // the phantom output — must NEVER fire from a door
		Wire(Door, TEXT("OnOpen"));
		Wire(Door, TEXT("OnClose"));
		Wire(Door, TEXT("OnFullyOpen"));
		Wire(Door, TEXT("OnFullyClosed"));
		return Door;
	}

	FElysiumEntityDef Counter(const TCHAR* Name)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("math_counter");
		Def.TargetName = Name;
		Def.Keys.Add(TEXT("min"), TEXT("0"));
		Def.Keys.Add(TEXT("max"), TEXT("1000"));
		return Def;
	}

	constexpr int32 SF_DOOR_START_OPEN = 0x1;
	constexpr int32 SF_DOOR_LOCKED     = 0x800;
}

// A locked door refuses the direct `Open` and `Toggle` inputs silently and inertly, and never fires
// OnLockedUse (which CBaseDoor does not own). The `+use` path likewise fires no output and does not
// move. (Fails against the pre-fix code, which fired OnLockedUse and beeped from InputOpen.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLockedDoorRefusesSilentlyTest,
	"Elysium.Substrate.LockedDoorRefusesSilently", GElysiumMoverTestFlags)
bool FElysiumLockedDoorRefusesSilentlyTest::RunTest(const FString&)
{
	using namespace ElysiumLockedDoorTests;

	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
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

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__locked_door_refuses__");
	Defs.Defs.Add(LockedDoor(TEXT("locked"), 1, SF_DOOR_LOCKED));
	Defs.Defs.Add(Counter(TEXT("sink_counter")));

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	TUniquePtr<FElysiumOrderedIOSink> OwnedSink = MakeUnique<FElysiumOrderedIOSink>();
	FElysiumOrderedIOSink* Sink = OwnedSink.Get();
	World.AddSink(MoveTemp(OwnedSink));
	World.Activate(0.0);

	FElysiumEntity* DoorEntity = World.FindByName(TEXT("locked"));
	FElysiumDoorBase* Door = DoorEntity ? DoorEntity->AsDoorBase() : nullptr;
	if (!TestNotNull(TEXT("locked door resolved"), Door)
		|| !TestNotNull(TEXT("locked door has a body"), Door ? Door->Body : nullptr))
	{
		return false;
	}
	TestTrue(TEXT("the LOCKED spawnflag locked the door"), Door->bLocked);
	TestEqual(TEXT("the door starts closed"), Door->State(),
		FElysiumDoorBase::EToggleState::AtBottom);

	// --- direct Open input: silent, no output, no motion ---------------------------------------
	Door->InputOpen(FElysiumEntityHandle());
	World.Tick(1.0);
	TestEqual(TEXT("locked Open does not move the door"), Door->State(),
		FElysiumDoorBase::EToggleState::AtBottom);
	TestFalse(TEXT("locked Open schedules no motion"), Door->IsMoving());
	TestEqual(TEXT("locked Open fires no OnOpen"), Sink->CountOf(TEXT("fire"), TEXT("OnOpen")), 0);
	TestEqual(TEXT("a door never fires OnLockedUse on Open"),
		Sink->CountOf(TEXT("fire"), TEXT("OnLockedUse")), 0);
	TestEqual(TEXT("locked Open is silent (no voice submitted)"), Services.Count(TEXT("Submit")), 0);

	// --- direct Toggle input: nothing at all ---------------------------------------------------
	Door->InputToggle(FElysiumEntityHandle());
	World.Tick(1.0);
	TestEqual(TEXT("locked Toggle does not move the door"), Door->State(),
		FElysiumDoorBase::EToggleState::AtBottom);
	TestFalse(TEXT("locked Toggle schedules no motion"), Door->IsMoving());
	TestEqual(TEXT("locked Toggle fires no OnOpen"), Sink->CountOf(TEXT("fire"), TEXT("OnOpen")), 0);
	TestEqual(TEXT("locked Toggle fires no OnClose"), Sink->CountOf(TEXT("fire"), TEXT("OnClose")), 0);
	TestEqual(TEXT("a door never fires OnLockedUse on Toggle"),
		Sink->CountOf(TEXT("fire"), TEXT("OnLockedUse")), 0);
	TestEqual(TEXT("locked Toggle is silent (no voice submitted)"),
		Services.Count(TEXT("Submit")), 0);

	// --- +use path: no output, no motion (the `locked` sound is Content-tier, not asserted) -----
	Door->DoorUse(FElysiumEntityHandle());
	World.Tick(1.0);
	TestEqual(TEXT("locked +use does not move the door"), Door->State(),
		FElysiumDoorBase::EToggleState::AtBottom);
	TestFalse(TEXT("locked +use schedules no motion"), Door->IsMoving());
	TestEqual(TEXT("locked +use fires no OnOpen"), Sink->CountOf(TEXT("fire"), TEXT("OnOpen")), 0);
	TestEqual(TEXT("a door never fires OnLockedUse on +use"),
		Sink->CountOf(TEXT("fire"), TEXT("OnLockedUse")), 0);

	return true;
}

// A locked door resting OPEN is not closed by +use (retail Use returns on the locked branch before
// DoorActivate). A direct `Close` input, which retail leaves un-lock-checked, still closes it — so
// the fix must not add a lock gate to Close. (The +use half fails against the pre-fix code, which
// routed +use -> Toggle -> Close and closed the locked open door.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLockedOpenDoorCloseMatrixTest,
	"Elysium.Substrate.LockedOpenDoorCloseMatrix", GElysiumMoverTestFlags)
bool FElysiumLockedOpenDoorCloseMatrixTest::RunTest(const FString&)
{
	using namespace ElysiumLockedDoorTests;

	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
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

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__locked_open_door_close__");
	// START_OPEN | LOCKED: spawns resting open and locked.
	Defs.Defs.Add(LockedDoor(TEXT("locked_open"), 1, SF_DOOR_START_OPEN | SF_DOOR_LOCKED));
	Defs.Defs.Add(Counter(TEXT("sink_counter")));

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	TUniquePtr<FElysiumOrderedIOSink> OwnedSink = MakeUnique<FElysiumOrderedIOSink>();
	FElysiumOrderedIOSink* Sink = OwnedSink.Get();
	World.AddSink(MoveTemp(OwnedSink));
	World.Activate(0.0);
	World.Tick(0.1);   // run the START_OPEN seat think

	FElysiumEntity* DoorEntity = World.FindByName(TEXT("locked_open"));
	FElysiumDoorBase* Door = DoorEntity ? DoorEntity->AsDoorBase() : nullptr;
	if (!TestNotNull(TEXT("locked open door resolved"), Door)
		|| !TestNotNull(TEXT("locked open door has a body"), Door ? Door->Body : nullptr))
	{
		return false;
	}
	TestTrue(TEXT("the door is locked"), Door->bLocked);
	TestEqual(TEXT("START_OPEN rests the door open"), Door->State(),
		FElysiumDoorBase::EToggleState::AtTop);

	// --- +use on a locked OPEN door: stays open, no OnClose, no motion --------------------------
	Door->DoorUse(FElysiumEntityHandle());
	World.Tick(1.0);
	TestEqual(TEXT("+use does not close a locked open door"), Door->State(),
		FElysiumDoorBase::EToggleState::AtTop);
	TestFalse(TEXT("+use on a locked open door schedules no motion"), Door->IsMoving());
	TestEqual(TEXT("+use on a locked open door fires no OnClose"),
		Sink->CountOf(TEXT("fire"), TEXT("OnClose")), 0);
	TestEqual(TEXT("a door never fires OnLockedUse from an open +use"),
		Sink->CountOf(TEXT("fire"), TEXT("OnLockedUse")), 0);

	// --- direct Close input: retail has no lock check, so it DOES close --------------------------
	Sink->Reset();
	Door->InputClose(FElysiumEntityHandle());
	TestEqual(TEXT("a direct Close on a locked door begins closing (no lock gate)"), Door->State(),
		FElysiumDoorBase::EToggleState::GoingDown);
	TestTrue(TEXT("the direct Close actually issued a move"), Door->IsMoving());
	TestEqual(TEXT("the direct Close fires OnClose once"),
		Sink->CountOf(TEXT("fire"), TEXT("OnClose")), 1);

	return true;
}

// =====================================================================================
// ResolveToggleStateFromTransform — the unconditional +use state-resync (retail CBaseDoor::Use
// step 5, vt +0x3e0; CRotDoor FUN_100f2520 / CBaseDoor FUN_100eff90).
//
// Reference: research/event-surface/door-largeitems-spec.md TARGET 2 step T2.5 (grade CONFIRMED,
// 0.001 tolerance PE-verified). On every +use, before the admission/locked decision, the door
// recomputes its toggle state from the LIVE body transform: within 0.001 per-component of the
// closed endpoint -> AtBottom; of the open endpoint -> AtTop; otherwise the mid-motion state is
// left as-is. This re-stamps a door whose body never physically moved back to its true endpoint,
// which is what lets the following toggle re-open it (the stale-AtTop tutorial-door bug).
// =====================================================================================

namespace ElysiumDoorResyncTests
{
	using ElysiumLockedDoorTests::DoorHull;

	// A plain unlocked func_door_rotating with a real body, no wired outputs (the mapping test reads
	// only State()). distance 90 / speed 90 / wait -1 so one Tick opens it and it stays open.
	FElysiumEntityDef RotatingDoor(const TCHAR* Name, int32 Model)
	{
		FElysiumEntityDef Door;
		Door.Classname = TEXT("func_door_rotating");
		Door.TargetName = Name;
		Door.Model = Model;
		Door.Hulls.Add(DoorHull());
		Door.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Model));
		Door.Keys.Add(TEXT("distance"), TEXT("90"));
		Door.Keys.Add(TEXT("speed"), TEXT("90"));
		Door.Keys.Add(TEXT("wait"), TEXT("-1"));
		Door.Keys.Add(TEXT("spawnflags"), TEXT("0"));
		return Door;
	}

	AActor* SpawnMoverOwner(FAutomationTestBase* Test, UWorld* EngineWorld)
	{
		AActor* Owner = EngineWorld ? EngineWorld->SpawnActor<AActor>() : nullptr;
		if (!Test->TestNotNull(TEXT("mover owner spawned"), Owner))
		{
			return nullptr;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("MoverRoot"));
		Owner->SetRootComponent(Root);
		Root->RegisterComponent();
		Owner->AddInstanceComponent(Root);
		return Owner;
	}
}

// (a) The direct mapping: closed pose -> AtBottom, open pose -> AtTop, a genuine mid-pose leaves the
// GoingUp/GoingDown state untouched, a component comfortably inside the 0.001 tolerance snaps and one
// 0.01 off (an order of magnitude outside) does not. (Pre-fix ResolveToggleStateFromTransform does
// not exist, so this file does not compile
// against the pre-fix header — the mapping is new behaviour by construction.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorResyncMappingTest,
	"Elysium.Substrate.DoorResyncMapping", GElysiumMoverTestFlags)
bool FElysiumDoorResyncMappingTest::RunTest(const FString&)
{
	using namespace ElysiumDoorResyncTests;
	using EToggleState = FElysiumDoorBase::EToggleState;

	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	AActor* Owner = SpawnMoverOwner(this, TestWorld.GetTestWorld());
	if (!Owner)
	{
		return false;
	}

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__door_resync_mapping__");
	Defs.Defs.Add(RotatingDoor(TEXT("door"), 1));

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* Entity = World.FindByName(TEXT("door"));
	FElysiumDoorBase* Door = Entity ? Entity->AsDoorBase() : nullptr;
	if (!TestNotNull(TEXT("door resolved"), Door)
		|| !TestNotNull(TEXT("door has a body"), Door ? Door->Body : nullptr))
	{
		return false;
	}

	const FElysiumEntityHandle NoActivator;
	auto SnapBody = [Door](const FVector& Loc, const FRotator& Rot)
	{
		Door->Body->SetRelativeLocationAndRotation(Loc, Rot, /*bSweep*/ false);
	};

	// Capture the two real endpoints off the body: closed is the spawn pose, open is where a full
	// swing lands. A rotating door keeps its origin, so location is constant across both.
	const FVector  ClosedLoc = Door->Body->GetRelativeLocation();
	const FRotator ClosedRot = Door->Body->GetRelativeRotation();
	Door->InputOpen(NoActivator);
	World.Tick(2.0);   // 90deg at 90deg/s completes in 1s; 2s is margin
	if (!TestEqual(TEXT("the door opened fully"), Door->State(), EToggleState::AtTop))
	{
		return false;
	}
	const FVector  OpenLoc = Door->Body->GetRelativeLocation();
	const FRotator OpenRot = Door->Body->GetRelativeRotation();

	// 1) Body at the closed pose -> AtBottom (observed as a change away from the current AtTop).
	SnapBody(ClosedLoc, ClosedRot);
	Door->ResolveToggleStateFromTransform();
	TestEqual(TEXT("closed pose resolves to AtBottom"), Door->State(), EToggleState::AtBottom);

	// 2) Body at the open pose -> AtTop.
	SnapBody(OpenLoc, OpenRot);
	Door->ResolveToggleStateFromTransform();
	TestEqual(TEXT("open pose resolves to AtTop"), Door->State(), EToggleState::AtTop);

	// 3) Body genuinely between the endpoints -> the mid-motion state is left untouched. Establish a
	// clean GoingDown, snap the body to the half-swing, and confirm the resync does not move it.
	Door->InputClose(NoActivator);
	if (!TestEqual(TEXT("InputClose enters GoingDown"), Door->State(), EToggleState::GoingDown))
	{
		return false;
	}
	const FRotator MidRot(
		(ClosedRot.Pitch + OpenRot.Pitch) * 0.5f,
		(ClosedRot.Yaw   + OpenRot.Yaw)   * 0.5f,
		(ClosedRot.Roll  + OpenRot.Roll)  * 0.5f);
	SnapBody(ClosedLoc, MidRot);   // ~45deg from either endpoint
	Door->ResolveToggleStateFromTransform();
	TestEqual(TEXT("a mid-swing pose leaves GoingDown untouched"), Door->State(), EToggleState::GoingDown);

	// 4) A single component comfortably inside the 0.001 tolerance still snaps (<= tolerance). The
	// exact-boundary delta is float-fragile — after the SetRelativeRotation round trip the actual
	// component delta can land just above 0.001f — and retail's inclusive/exclusive behaviour at
	// precisely 0.001 is neither RE-determined nor observable (real doors sit at ~0 or far away), so
	// this stays well inside the band rather than on its edge.
	SnapBody(ClosedLoc, ClosedRot + FRotator(0.f, 0.0005f, 0.f));
	Door->ResolveToggleStateFromTransform();
	TestEqual(TEXT("a pose inside the resync tolerance snaps to AtBottom"), Door->State(),
		EToggleState::AtBottom);

	// 5) A component 0.01 off the endpoint does NOT snap — the mid-motion state is preserved.
	Door->InputOpen(NoActivator);   // AtBottom -> GoingUp, a clean mid-motion state
	if (!TestEqual(TEXT("InputOpen enters GoingUp"), Door->State(), EToggleState::GoingUp))
	{
		return false;
	}
	SnapBody(ClosedLoc, ClosedRot + FRotator(0.f, 0.01f, 0.f));
	Door->ResolveToggleStateFromTransform();
	TestEqual(TEXT("0.01 off the closed pose does not snap (state stays GoingUp)"), Door->State(),
		EToggleState::GoingUp);

	return true;
}

// (b) The live-bug reproduction: a door believing it is AtTop while its body sits at the CLOSED pose
// (it never physically moved). A +use must resync the belief to AtBottom and then OPEN — firing
// OnOpen and entering GoingUp. Pre-fix, +use on a stale-AtTop door routes toggle -> InputClose and
// tries to close (no OnOpen, state GoingDown), which is the dropped-open bug this fixes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorStaleAtTopReopensTest,
	"Elysium.Substrate.DoorStaleAtTopReopens", GElysiumMoverTestFlags)
bool FElysiumDoorStaleAtTopReopensTest::RunTest(const FString&)
{
	using namespace ElysiumDoorResyncTests;
	using EToggleState = FElysiumDoorBase::EToggleState;

	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	AActor* Owner = SpawnMoverOwner(this, TestWorld.GetTestWorld());
	if (!Owner)
	{
		return false;
	}

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__door_stale_attop__");
	// An unlocked rotating door with OnOpen wired to a sink counter (LockedDoor with no LOCKED bit).
	Defs.Defs.Add(ElysiumLockedDoorTests::LockedDoor(TEXT("stale"), 1, /*SpawnFlags*/ 0));
	Defs.Defs.Add(ElysiumLockedDoorTests::Counter(TEXT("sink_counter")));

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	TUniquePtr<FElysiumOrderedIOSink> OwnedSink = MakeUnique<FElysiumOrderedIOSink>();
	FElysiumOrderedIOSink* Sink = OwnedSink.Get();
	World.AddSink(MoveTemp(OwnedSink));
	World.Activate(0.0);

	FElysiumEntity* Entity = World.FindByName(TEXT("stale"));
	FElysiumDoorBase* Door = Entity ? Entity->AsDoorBase() : nullptr;
	if (!TestNotNull(TEXT("door resolved"), Door)
		|| !TestNotNull(TEXT("door has a body"), Door ? Door->Body : nullptr))
	{
		return false;
	}

	const FElysiumEntityHandle NoActivator;
	const FVector  ClosedLoc = Door->Body->GetRelativeLocation();
	const FRotator ClosedRot = Door->Body->GetRelativeRotation();

	// Drive a real open so the state legitimately reaches AtTop, then force the stale scenario: put
	// the body back at the closed pose without touching the state. Now the door BELIEVES it is open
	// while its body has (as far as anything can observe) never left closed.
	Door->InputOpen(NoActivator);
	World.Tick(2.0);
	if (!TestEqual(TEXT("the door reached AtTop"), Door->State(), EToggleState::AtTop))
	{
		return false;
	}
	Door->Body->SetRelativeLocationAndRotation(ClosedLoc, ClosedRot, /*bSweep*/ false);
	Sink->Reset();   // discard the OnOpen from the legitimate open above

	// +use on the stale-AtTop door: the resync must snap the belief to AtBottom, then the toggle
	// opens it. Post-fix: OnOpen fires once and the door enters GoingUp. Pre-fix (no resync): the
	// toggle would InputClose a door it thinks is open — GoingDown, no OnOpen — so these fail.
	Door->DoorUse(NoActivator);
	TestEqual(TEXT("stale +use re-opens the door (GoingUp)"), Door->State(), EToggleState::GoingUp);
	TestTrue(TEXT("stale +use issued an opening move"), Door->IsMoving());
	TestEqual(TEXT("stale +use fires OnOpen exactly once"),
		Sink->CountOf(TEXT("fire"), TEXT("OnOpen")), 1);
	TestEqual(TEXT("stale +use fires no OnClose"),
		Sink->CountOf(TEXT("fire"), TEXT("OnClose")), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
