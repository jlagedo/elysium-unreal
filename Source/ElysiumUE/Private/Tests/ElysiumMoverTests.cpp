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
// Activator-relative swing — retail CRotDoor::OpenAwayFromEntity (FUN_100f3390) + ComputeSwingData
// (FUN_100f19b0). Reference: research/event-surface/swing-centres-findings.md ITEM 1 and
// door-largeitems-spec.md TARGET 1 (both 100%-CONFIRMED). A rotating door swings toward whichever of
// its two swing reference centres is FARTHER from the activator's world centre, i.e. it opens away
// from the activator; the swing MAGNITUDE stays the door's configured `distance`. The blocked-latch
// inversion is HELD (retail field identity unrecovered) and not exercised here.
//
// Red/green: pre-fix IssueMoveToOpen always swung the fixed forward OpenRot (yaw -90 -> tip on -Y)
// regardless of the activator, so the south-activator case (which must now swing to +Y) fails against
// the pre-fix code; the mirror across the two sides is the observable contract. The handedness test
// above stays green because it opens with NO activator, which falls back to the same forward pose.
// =====================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRotatingDoorOpensAwayFromActivatorTest,
	"Elysium.Substrate.RotatingDoorOpensAwayFromActivator", GElysiumMoverTestFlags)
bool FElysiumRotatingDoorOpensAwayFromActivatorTest::RunTest(const FString&)
{
	using EToggleState = FElysiumDoorBase::EToggleState;

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

	// An asymmetric leaf from the hinge along local +X. The tip makes the swing observable, and the
	// door's two swing centres straddle the Y axis, so an activator on -Y vs +Y picks opposite sides.
	auto DoorHull = []()
	{
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
	Defs.MapName = TEXT("__rotating_door_open_away__");

	FElysiumEntityDef Door;
	Door.Classname = TEXT("func_door_rotating");
	Door.TargetName = TEXT("door");
	Door.Origin = FVector::ZeroVector;
	Door.Model = 1;
	Door.Hulls.Add(DoorHull());
	Door.Keys.Add(TEXT("model"), TEXT("*1"));
	Door.Keys.Add(TEXT("distance"), TEXT("90"));
	Door.Keys.Add(TEXT("speed"), TEXT("90"));
	Door.Keys.Add(TEXT("wait"), TEXT("-1"));   // stay open; close explicitly between the two cases
	Door.Keys.Add(TEXT("spawnflags"), TEXT("0"));
	Defs.Defs.Add(MoveTemp(Door));

	// Two activators, one clearly on each side of the hinge along Y. A math_counter is a bodiless
	// point entity whose live Origin is seeded from its def — exactly the world position the swing
	// resolution reads off the activator.
	auto AddActivator = [&Defs](const TCHAR* Name, const FVector& Origin)
	{
		FElysiumEntityDef A;
		A.Classname = TEXT("math_counter");
		A.TargetName = Name;
		A.Origin = Origin;
		A.Keys.Add(TEXT("min"), TEXT("0"));
		A.Keys.Add(TEXT("max"), TEXT("1000"));
		Defs.Defs.Add(MoveTemp(A));
	};
	AddActivator(TEXT("south"), FVector(0.0f, -300.0f, 0.0f));
	AddActivator(TEXT("north"), FVector(0.0f,  300.0f, 0.0f));

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* DoorEntity = World.FindByName(TEXT("door"));
	FElysiumDoorBase* DoorPtr = DoorEntity ? DoorEntity->AsDoorBase() : nullptr;
	FElysiumEntity* South = World.FindByName(TEXT("south"));
	FElysiumEntity* North = World.FindByName(TEXT("north"));
	if (!TestNotNull(TEXT("door resolved"), DoorPtr)
		|| !TestNotNull(TEXT("door has a body"), DoorPtr ? DoorPtr->Body : nullptr)
		|| !TestNotNull(TEXT("south activator resolved"), South)
		|| !TestNotNull(TEXT("north activator resolved"), North))
	{
		return false;
	}

	const FRotator ClosedRot = DoorPtr->Body->GetRelativeRotation();
	auto TipY = [&]()
	{
		return DoorPtr->Body->GetRelativeRotation().RotateVector(FVector(100.0f, 0.0f, 0.0f)).Y;
	};
	auto SwingDegrees = [&]()
	{
		return (float)FMath::RadiansToDegrees(
			ClosedRot.Quaternion().AngularDistance(DoorPtr->Body->GetRelativeRotation().Quaternion()));
	};

	// --- Activator on -Y: the leaf opens toward +Y (away from the activator). --------------------
	DoorPtr->InputOpen(South->Handle);
	World.Tick(2.0);   // 90deg at 90deg/s completes in 1s; 2s is margin
	TestEqual(TEXT("south-opened door reaches AtTop"), DoorPtr->State(), EToggleState::AtTop);
	TestTrue(TEXT("an activator on -Y swings the leaf toward +Y (open away)"), TipY() > 90.0f);
	TestTrue(TEXT("the swing magnitude equals the configured distance (90 deg)"),
		FMath::IsNearlyEqual(SwingDegrees(), 90.0f, 0.5f));

	// Close back to the hinge so the mirror case starts from the same closed pose.
	DoorPtr->InputClose(South->Handle);
	World.Tick(4.0);
	TestEqual(TEXT("the door closes back to AtBottom"), DoorPtr->State(), EToggleState::AtBottom);

	// --- Activator on +Y: the SAME door now opens toward -Y (mirrored direction). ----------------
	DoorPtr->InputOpen(North->Handle);
	World.Tick(6.0);
	TestEqual(TEXT("north-opened door reaches AtTop"), DoorPtr->State(), EToggleState::AtTop);
	TestTrue(TEXT("an activator on +Y swings the leaf toward -Y (mirror of the -Y case)"),
		TipY() < -90.0f);
	TestTrue(TEXT("the mirrored swing magnitude is still the configured distance (90 deg)"),
		FMath::IsNearlyEqual(SwingDegrees(), 90.0f, 0.5f));

	return true;
}

// =====================================================================================
// Activator-relative swing GATES — retail CRotDoor::DoorGoUp(bPropagateLinked, bResolveSwing)
// (FUN_100f3030) reaches OpenAwayFromEntity ONLY when the activator resolves AND !SF_DOOR_ONEWAY
// (0x10) AND bResolveSwing != 0. Reference: door-largeitems-spec.md T1.4 (CONFIRMED) and
// swing-centres-findings.md ITEM 1. Two gates the base swing omits:
//
//   1. bResolveSwing: the only retail caller passing 0 is CRotDoor::Blocked (the block-reverse), which
//      re-opens FIXED-FORWARD, never activator-relative. In this runtime that is the OnMoveBlocked ->
//      DoorGoUp(LastActivator, false) reissue. Driving OnMoveBlocked needs a live pawn blocker seated
//      in the swept arc, so the seam is exercised directly: DoorGoUp(activator, false) yields OpenRot
//      while DoorGoUp(activator, true) yields the activator-relative BackRot for the SAME activator.
//   2. SF_DOOR_ONEWAY: a ONEWAY door forces fixed-forward even with a resolved activator.
//
// The activator sits on -Y, which (per the OpensAwayFromActivator test above) is the side whose base
// activator-relative swing is BackRot (tip on +Y). The fixed-forward OpenRot is a normal +90 Source
// swing, tip on -Y. So "activator-relative" reads as TipY > +90 and "fixed-forward" as TipY < -90.
// =====================================================================================

// The block-reverse seam accessor (friended by FElysiumDoorBase). DoorGoUp(_, false) is otherwise only
// reachable through OnMoveBlocked, which requires a pawn blocker.
struct FElysiumDoorTestAccess
{
	static void DoorGoUp(FElysiumDoorBase& Door, const FElysiumEntityHandle& Activator, bool bResolveSwing)
	{
		Door.DoorGoUp(Activator, bResolveSwing);
	}
};

namespace ElysiumSwingGateTests
{
	// An asymmetric leaf from the hinge along local +X: the tip makes the swing direction observable,
	// and the door's two swing centres straddle the Y axis so an activator on -Y picks the BackRot side.
	FElysiumConvexHull DoorHull()
	{
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

	FElysiumEntityDef RotatingDoor(const TCHAR* Name, int32 Model, int32 SpawnFlags)
	{
		FElysiumEntityDef Door;
		Door.Classname = TEXT("func_door_rotating");
		Door.TargetName = Name;
		Door.Origin = FVector::ZeroVector;
		Door.Model = Model;
		Door.Hulls.Add(DoorHull());
		Door.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Model));
		Door.Keys.Add(TEXT("distance"), TEXT("90"));
		Door.Keys.Add(TEXT("speed"), TEXT("90"));
		Door.Keys.Add(TEXT("wait"), TEXT("-1"));   // stay open; close explicitly between cases
		Door.Keys.Add(TEXT("spawnflags"), FString::FromInt(SpawnFlags));
		return Door;
	}

	FElysiumEntityDef Activator(const TCHAR* Name, const FVector& Origin)
	{
		FElysiumEntityDef A;
		A.Classname = TEXT("math_counter");   // a bodiless point entity; its live Origin seeds from the def
		A.TargetName = Name;
		A.Origin = Origin;
		A.Keys.Add(TEXT("min"), TEXT("0"));
		A.Keys.Add(TEXT("max"), TEXT("1000"));
		return A;
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

// GATE 1 — the block-reverse open is fixed-forward. For the SAME activator (on -Y, the BackRot side),
// DoorGoUp(_, true) swings activator-relative to BackRot (tip +Y), but DoorGoUp(_, false) — the
// OnMoveBlocked reissue — swings the fixed-forward OpenRot (tip -Y). Red pre-fix: DoorGoUp took no
// bResolveSwing arg and always resolved activator-relative, so the block-reverse picked BackRot (and
// this seam does not compile against the pre-fix signature — the gate is new by construction).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRotatingDoorBlockReverseFixedForwardTest,
	"Elysium.Substrate.RotatingDoorBlockReverseFixedForward", GElysiumMoverTestFlags)
bool FElysiumRotatingDoorBlockReverseFixedForwardTest::RunTest(const FString&)
{
	using namespace ElysiumSwingGateTests;
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
	Defs.MapName = TEXT("__rotating_door_block_reverse__");
	Defs.Defs.Add(RotatingDoor(TEXT("door"), 1, /*SpawnFlags*/ 0));
	Defs.Defs.Add(Activator(TEXT("south"), FVector(0.0f, -300.0f, 0.0f)));   // -Y => base swing picks BackRot

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* DoorEntity = World.FindByName(TEXT("door"));
	FElysiumDoorBase* Door = DoorEntity ? DoorEntity->AsDoorBase() : nullptr;
	FElysiumEntity* South = World.FindByName(TEXT("south"));
	if (!TestNotNull(TEXT("door resolved"), Door)
		|| !TestNotNull(TEXT("door has a body"), Door ? Door->Body : nullptr)
		|| !TestNotNull(TEXT("south activator resolved"), South))
	{
		return false;
	}

	auto TipY = [&]()
	{
		return Door->Body->GetRelativeRotation().RotateVector(FVector(100.0f, 0.0f, 0.0f)).Y;
	};

	// --- bResolveSwing == true: the same activator resolves activator-relative to BackRot (tip +Y). ---
	FElysiumDoorTestAccess::DoorGoUp(*Door, South->Handle, /*bResolveSwing*/ true);
	World.Tick(2.0);   // 90deg at 90deg/s completes in 1s; 2s is margin
	TestEqual(TEXT("resolve-swing open reaches AtTop"), Door->State(), EToggleState::AtTop);
	TestTrue(TEXT("with bResolveSwing the -Y activator swings the leaf activator-relatively to +Y (BackRot)"),
		TipY() > 90.0f);

	// Close back to the hinge so the block-reverse case starts from the same closed pose.
	Door->InputClose(South->Handle);
	World.Tick(4.0);
	TestEqual(TEXT("the door closes back to AtBottom"), Door->State(), EToggleState::AtBottom);

	// --- bResolveSwing == false: the block-reverse reissue ignores the activator, swinging fixed-forward
	//     OpenRot (tip -Y), NOT the activator-relative BackRot. This is the gate under test. ------------
	FElysiumDoorTestAccess::DoorGoUp(*Door, South->Handle, /*bResolveSwing*/ false);
	World.Tick(6.0);
	TestEqual(TEXT("block-reverse open reaches AtTop"), Door->State(), EToggleState::AtTop);
	TestTrue(TEXT("the block-reverse open is fixed-forward OpenRot (tip -Y), not activator-relative BackRot"),
		TipY() < -90.0f);

	return true;
}

// GATE 2 — a ONEWAY door ignores the activator. With SF_DOOR_ONEWAY (0x10) set and the activator on the
// BackRot side (-Y), the normal player/logic open path (InputOpen) still swings the fixed-forward
// OpenRot (tip -Y). Red pre-fix: ChooseOpenTarget had no ONEWAY gate, so it resolved activator-relative
// to BackRot (tip +Y); green post-fix: the early return forces OpenRot.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRotatingDoorOneWayIgnoresActivatorTest,
	"Elysium.Substrate.RotatingDoorOneWayIgnoresActivator", GElysiumMoverTestFlags)
bool FElysiumRotatingDoorOneWayIgnoresActivatorTest::RunTest(const FString&)
{
	using namespace ElysiumSwingGateTests;
	using EToggleState = FElysiumDoorBase::EToggleState;

	constexpr int32 SF_DOOR_ONEWAY = 0x10;

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
	Defs.MapName = TEXT("__rotating_door_oneway__");
	Defs.Defs.Add(RotatingDoor(TEXT("door"), 1, SF_DOOR_ONEWAY));
	Defs.Defs.Add(Activator(TEXT("south"), FVector(0.0f, -300.0f, 0.0f)));   // -Y => base swing would pick BackRot

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* DoorEntity = World.FindByName(TEXT("door"));
	FElysiumDoorBase* Door = DoorEntity ? DoorEntity->AsDoorBase() : nullptr;
	FElysiumEntity* South = World.FindByName(TEXT("south"));
	if (!TestNotNull(TEXT("door resolved"), Door)
		|| !TestNotNull(TEXT("door has a body"), Door ? Door->Body : nullptr)
		|| !TestNotNull(TEXT("south activator resolved"), South))
	{
		return false;
	}

	auto TipY = [&]()
	{
		return Door->Body->GetRelativeRotation().RotateVector(FVector(100.0f, 0.0f, 0.0f)).Y;
	};

	// Normal open path with a resolved activator on the BackRot side; ONEWAY forces fixed-forward.
	Door->InputOpen(South->Handle);
	World.Tick(2.0);
	TestEqual(TEXT("the ONEWAY door reaches AtTop"), Door->State(), EToggleState::AtTop);
	TestTrue(TEXT("a ONEWAY door swings fixed-forward OpenRot (tip -Y) despite the -Y activator"),
		TipY() < -90.0f);

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
