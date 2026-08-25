#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumBrushComponent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumLockable.h"
#include "Substrate/ElysiumMover.h"
#include "Tests/ElysiumTestServices.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"

static constexpr EAutomationTestFlags GElysiumMoverTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace ElysiumDoorCaseTests
{
	// One engine world per case, torn down with the case. The wrapper is deliberately NOT a static
	// held across cases: `~FTestWorldWrapper` destroys the world and forces a collection, and a
	// static would run that at DLL unload, after the engine that owns GC and the world list has
	// already shut down. The world is also not the cost here -- the whole Substrate tier executes
	// in about three seconds -- so sharing one would trade a real lifetime hazard, and cross-case
	// actor accumulation in a world these cases run traces against, for nothing measurable.
	bool OpenEngineWorld(FTestWorldWrapper& Wrapper, FAutomationTestBase* Test)
	{
		if (Wrapper.CreateTestWorld(EWorldType::Game) && Wrapper.BeginPlayInTestWorld())
		{
			return true;
		}
		Wrapper.ForwardErrorMessages(Test);
		return false;
	}
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

// The nine door-mover cases below each built and tore down their own UWorld as separate
// IMPLEMENT_SIMPLE_AUTOMATION_TEST entries. They are grouped into one IMPLEMENT_COMPLEX_AUTOMATION_TEST
// so a failure in one case is reported and isolated by name, and each case still owns its own engine
// world, FElysiumEntityWorld, services and defs outright.
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FElysiumDoorCasesTest, "Elysium.Substrate.Mover", GElysiumMoverTestFlags)

void FElysiumDoorCasesTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	static const TCHAR* Cases[] =
	{
		TEXT("RotatingDoorHandedness"),
		TEXT("RotatingDoorOpensAwayFromActivator"),
		TEXT("RotatingDoorBlockReverseFixedForward"),
		TEXT("RotatingDoorOneWayIgnoresActivator"),
		TEXT("LockedDoorRefusesSilently"),
		TEXT("LockedOpenDoorCloseMatrix"),
		TEXT("DoorResyncMapping"),
		TEXT("DoorStaleAtTopReopens"),
		TEXT("DoorUseGuardChain"),
	};
	for (const TCHAR* Case : Cases)
	{
		OutBeautifiedNames.Add(Case);
		OutTestCommands.Add(Case);
	}
}

bool FElysiumDoorCasesTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!ElysiumDoorCaseTests::OpenEngineWorld(TestWorld, this))
	{
		return false;
	}
	UWorld* EngineWorld = TestWorld.GetTestWorld();

	if (Parameters == TEXT("RotatingDoorHandedness"))
	{
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
	else if (Parameters == TEXT("RotatingDoorOpensAwayFromActivator"))
	{
		using EToggleState = FElysiumDoorBase::EToggleState;

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
	// GATE 1 — the block-reverse open is fixed-forward. For the SAME activator (on -Y, the BackRot side),
	// DoorGoUp(_, true) swings activator-relative to BackRot (tip +Y), but DoorGoUp(_, false) — the
	// OnMoveBlocked reissue — swings the fixed-forward OpenRot (tip -Y). Red pre-fix: DoorGoUp took no
	// bResolveSwing arg and always resolved activator-relative, so the block-reverse picked BackRot (and
	// this seam does not compile against the pre-fix signature — the gate is new by construction).
	else if (Parameters == TEXT("RotatingDoorBlockReverseFixedForward"))
	{
		using namespace ElysiumSwingGateTests;
		using EToggleState = FElysiumDoorBase::EToggleState;

		AActor* Owner = SpawnMoverOwner(this, EngineWorld);
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
	else if (Parameters == TEXT("RotatingDoorOneWayIgnoresActivator"))
	{
		using namespace ElysiumSwingGateTests;
		using EToggleState = FElysiumDoorBase::EToggleState;

		constexpr int32 SF_DOOR_ONEWAY = 0x10;

		AActor* Owner = SpawnMoverOwner(this, EngineWorld);
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
	// A locked door refuses the direct `Open` and `Toggle` inputs silently and inertly, and never fires
	// OnLockedUse (which CBaseDoor does not own). The `+use` path likewise fires no output and does not
	// move. (Fails against the pre-fix code, which fired OnLockedUse and beeped from InputOpen.)
	else if (Parameters == TEXT("LockedDoorRefusesSilently"))
	{
		using namespace ElysiumLockedDoorTests;

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
	else if (Parameters == TEXT("LockedOpenDoorCloseMatrix"))
	{
		using namespace ElysiumLockedDoorTests;

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
		// Retail fires OnClose twice on an admitted Close: once in InputClose (FUN_100f00a0), once
		// again inside DoorGoDown. Only the +use and Toggle paths fire it once, because neither goes
		// through the input handler.
		TestEqual(TEXT("the direct Close fires OnClose twice — input then motion start"),
			Sink->CountOf(TEXT("fire"), TEXT("OnClose")), 2);

		return true;
	}
	// (a) The direct mapping: closed pose -> AtBottom, open pose -> AtTop, a genuine mid-pose leaves the
	// GoingUp/GoingDown state untouched, a component comfortably inside the 0.001 tolerance snaps and one
	// 0.01 off (an order of magnitude outside) does not. (Pre-fix ResolveToggleStateFromTransform does
	// not exist, so this file does not compile
	// against the pre-fix header — the mapping is new behaviour by construction.)
	else if (Parameters == TEXT("DoorResyncMapping"))
	{
		using namespace ElysiumDoorResyncTests;
		using EToggleState = FElysiumDoorBase::EToggleState;

		AActor* Owner = SpawnMoverOwner(this, EngineWorld);
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
	else if (Parameters == TEXT("DoorStaleAtTopReopens"))
	{
		using namespace ElysiumDoorResyncTests;
		using EToggleState = FElysiumDoorBase::EToggleState;

		AActor* Owner = SpawnMoverOwner(this, EngineWorld);
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
	// CBaseDoor::Use's guard chain: the noopenwanted refusal, the mid-motion self-heal, and the
	// admission set that makes a moving door drop out. Bodiless — every assertion here is about state
	// and outputs, not geometry.
	else if (Parameters == TEXT("DoorUseGuardChain"))
	{
		using EToggleState = FElysiumDoorBase::EToggleState;

		// Embodied: a bodiless mover's BeginMove early-returns, so nothing is ever in flight and no
		// move ever completes. Every assertion below is about a move actually being under way.
		AActor* Owner = ElysiumDoorResyncTests::SpawnMoverOwner(this, EngineWorld);
		if (!Owner)
		{
			return false;
		}

		auto MakeDoor = [](const TCHAR* Name, int32 Model, const FVector& Origin, const TCHAR* SpawnFlags,
			const TCHAR* NoOpenWanted)
		{
			FElysiumEntityDef Def;
			Def.Classname = TEXT("func_door_rotating");
			Def.TargetName = Name;
			Def.Model = Model;
			Def.Origin = Origin;
			FElysiumConvexHull Hull;
			for (float X : { 0.0f, 100.0f })
			{
				for (float Y : { -5.0f, 5.0f })
				{
					for (float Z : { -100.0f, 100.0f })
					{
						Hull.Vertices.Emplace(Origin.X + X, Origin.Y + Y, Origin.Z + Z);
					}
				}
			}
			Def.Hulls.Add(MoveTemp(Hull));
			Def.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Model));
			Def.Keys.Add(TEXT("spawnflags"), SpawnFlags);
			Def.Keys.Add(TEXT("distance"), TEXT("90"));
			Def.Keys.Add(TEXT("wait"), TEXT("-1"));
			Def.Keys.Add(TEXT("speed"), TEXT("9"));   // 10 s for 90 deg — stays in flight across ticks
			Def.Keys.Add(TEXT("noopenwanted"), NoOpenWanted);
			return Def;
		};

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__door_use_guard_chain__");
		Defs.Defs.Add(MakeDoor(TEXT("wanted_door"), 1, FVector::ZeroVector, TEXT("256"), TEXT("1")));
		Defs.Defs.Add(MakeDoor(TEXT("plain_door"), 2, FVector(500, 0, 0), TEXT("256"), TEXT("0")));
		// PUSE | NO_AUTO_RETURN (0x100 | 0x20)
		Defs.Defs.Add(MakeDoor(TEXT("noreturn_door"), 3, FVector(1000, 0, 0), TEXT("288"), TEXT("0")));

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
		World.Activate(0.0);

		auto Door = [&World](const TCHAR* Name)
		{
			FElysiumEntity* E = World.FindByName(Name);
			return E ? E->AsDoorBase() : nullptr;
		};
		FElysiumDoorBase* Wanted = Door(TEXT("wanted_door"));
		FElysiumDoorBase* Plain = Door(TEXT("plain_door"));
		FElysiumDoorBase* NoReturn = Door(TEXT("noreturn_door"));
		FElysiumPlayer* Player = World.FindPlayer();
		if (!TestNotNull(TEXT("wanted door resolves"), Wanted)
			|| !TestNotNull(TEXT("plain door resolves"), Plain)
			|| !TestNotNull(TEXT("no-return door resolves"), NoReturn)
			|| !TestNotNull(TEXT("player resolves"), Player))
		{
			return false;
		}

		// --- Step 3: noopenwanted refuses only while the player is actually hunted -----------------
		TestEqual(TEXT("the noopenwanted keyfield parsed"), Wanted->bNoOpenWanted, true);
		Player->Police.CopsInPursuit = 0;
		Wanted->DoorUse(PlayerHandle);
		TestEqual(TEXT("noopenwanted does not refuse an unhunted player"), Wanted->State(),
			EToggleState::GoingUp);

		// Tick takes an ABSOLUTE time, so every wait below only moves forward. 90 deg at 9 deg/s is a
		// ten-second swing, which is what keeps a move observably in flight.
		Wanted->InputClose(PlayerHandle);
		World.Tick(30.0);   // let it settle closed
		TestEqual(TEXT("the wanted door is closed again"), Wanted->State(), EToggleState::AtBottom);
		Player->Police.CopsInPursuit = 1;
		Wanted->DoorUse(PlayerHandle);
		TestEqual(TEXT("noopenwanted refuses a hunted player outright"), Wanted->State(),
			EToggleState::AtBottom);
		TestFalse(TEXT("the refused door issued no move"), Wanted->IsMoving());
		Player->Police.CopsInPursuit = 0;

		// --- Steps 4 + 7: a +use on a moving door re-issues, then drops out of admission ------------
		Plain->DoorUse(PlayerHandle);
		TestEqual(TEXT("the plain door starts opening"), Plain->State(), EToggleState::GoingUp);
		World.Tick(30.5);   // half a second into a ten-second swing
		TestTrue(TEXT("the plain door is still moving"), Plain->IsMoving());
		Plain->DoorUse(PlayerHandle);
		// Step 4 re-issued the SAME direction, step 5 could not resolve a mid-travel body, and step 7
		// then refused the toggle: the door keeps opening rather than reversing.
		TestEqual(TEXT("a +use on a moving door does not reverse it"), Plain->State(),
			EToggleState::GoingUp);
		TestTrue(TEXT("the re-issued move is still in flight"), Plain->IsMoving());

		// --- Step 7: NO_AUTO_RETURN is admitted mid-motion, so it DOES reverse ----------------------
		NoReturn->DoorUse(PlayerHandle);
		TestEqual(TEXT("the no-return door starts opening"), NoReturn->State(), EToggleState::GoingUp);
		World.Tick(31.0);
		TestTrue(TEXT("the no-return door is still moving"), NoReturn->IsMoving());
		NoReturn->DoorUse(PlayerHandle);
		TestEqual(TEXT("NO_AUTO_RETURN admits a mid-motion +use and reverses"), NoReturn->State(),
			EToggleState::GoingDown);

		return true;
	}

	AddError(FString::Printf(TEXT("FElysiumDoorCasesTest: unrecognized case parameter '%s'"), *Parameters));
	return false;
}

// The output arity of the three inputs versus the +use path. Retail fires OnOpen/OnClose twice on
// an admitted Open/Close — once at the input, once at the motion start — while Toggle and +use
// reach the motion helpers directly and fire once. Easy to "fix" back by accident.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorInputOutputArityTest,
	"Elysium.Substrate.DoorInputOutputArity", GElysiumMoverTestFlags)
bool FElysiumDoorInputOutputArityTest::RunTest(const FString&)
{
	auto Counter = [](const TCHAR* Name)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("math_counter");
		Def.TargetName = Name;
		Def.Keys.Add(TEXT("min"), TEXT("0"));
		Def.Keys.Add(TEXT("max"), TEXT("100"));
		return Def;
	};

	// Outputs only fire down authored rows, so every door here wires OnOpen and OnClose at a
	// counter. Bodiless is fine — the arity is decided before any move is issued.
	auto ArityDoor = [](const TCHAR* Name)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("func_door");
		Def.TargetName = Name;
		Def.Keys.Add(TEXT("spawnflags"), TEXT("256"));
		Def.Keys.Add(TEXT("wait"), TEXT("-1"));
		for (const TCHAR* Output : { TEXT("OnOpen"), TEXT("OnClose") })
		{
			FElysiumOutputDef Row;
			Row.Name = Output;
			Row.Target = TEXT("sink");
			Row.Input = TEXT("Add");
			Row.Param = TEXT("1");
			Row.Times = -1;
			Def.Outputs.Add(MoveTemp(Row));
		}
		return Def;
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__door_input_output_arity__");
	Defs.Defs.Add(ArityDoor(TEXT("arity_door")));
	// A second leaf, left at rest, so the +use case runs the DoorActivate path rather than the
	// self-heal a mid-motion door would take.
	Defs.Defs.Add(ArityDoor(TEXT("use_door")));
	Defs.Defs.Add(Counter(TEXT("sink")));

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	TUniquePtr<FElysiumOrderedIOSink> OwnedSink = MakeUnique<FElysiumOrderedIOSink>();
	FElysiumOrderedIOSink* Sink = OwnedSink.Get();
	World.AddSink(MoveTemp(OwnedSink));
	World.Activate(0.0);

	FElysiumEntity* Entity = World.FindByName(TEXT("arity_door"));
	FElysiumDoorBase* Door2 = Entity ? Entity->AsDoorBase() : nullptr;
	if (!TestNotNull(TEXT("door resolves"), Door2))
	{
		return false;
	}

	Door2->InputOpen(PlayerHandle);
	TestEqual(TEXT("an admitted Open fires OnOpen twice — input then motion start"),
		Sink->CountOf(TEXT("fire"), TEXT("OnOpen")), 2);

	// Admission is `!= AtBottom`, so this Close is admitted from GoingUp.
	Sink->Reset();
	Door2->InputClose(PlayerHandle);
	TestEqual(TEXT("an admitted Close fires OnClose twice"),
		Sink->CountOf(TEXT("fire"), TEXT("OnClose")), 2);

	// Toggle reaches DoorGoUp/DoorGoDown directly, so it fires once.
	Sink->Reset();
	Door2->InputToggle(PlayerHandle);
	TestEqual(TEXT("Toggle fires its output exactly once"),
		Sink->CountOf(TEXT("fire"), TEXT("OnOpen")) + Sink->CountOf(TEXT("fire"), TEXT("OnClose")),
		1);

	// +use on a resting leaf likewise bypasses the input handlers.
	FElysiumEntity* UseEntity = World.FindByName(TEXT("use_door"));
	FElysiumDoorBase* UseDoor = UseEntity ? UseEntity->AsDoorBase() : nullptr;
	if (!TestNotNull(TEXT("use door resolves"), UseDoor))
	{
		return false;
	}
	Sink->Reset();
	UseDoor->DoorUse(PlayerHandle);
	TestEqual(TEXT("+use fires its output exactly once"),
		Sink->CountOf(TEXT("fire"), TEXT("OnOpen")) + Sink->CountOf(TEXT("fire"), TEXT("OnClose")),
		1);

	return true;
}

namespace ElysiumDoorKnobTests
{
	// A knob whose `difficulty` is non-zero seeds itself locked in FElysiumLockableEntity::Spawn.
	// Whether the DOOR is locked is a separate authority — the whole point of these cases.
	static FElysiumEntityDef Knob(const TCHAR* Name, const TCHAR* Parent, const TCHAR* Difficulty,
		const FVector& Origin = FVector::ZeroVector)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("prop_doorknob");
		Def.TargetName = Name;
		Def.Origin = Origin;
		Def.ModelMesh = TEXT("test_knob");
		Def.Keys.Add(TEXT("model"), TEXT("models/test/knob.mdl"));
		Def.Keys.Add(TEXT("parentname"), Parent);
		Def.Keys.Add(TEXT("difficulty"), Difficulty);
		Def.Keys.Add(TEXT("skilltype"), TEXT("1"));
		return Def;
	}

	static FElysiumEntityDef Door(const TCHAR* Name, const TCHAR* SpawnFlags)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("func_door_rotating");
		Def.TargetName = Name;
		Def.Keys.Add(TEXT("spawnflags"), SpawnFlags);
		Def.Keys.Add(TEXT("wait"), TEXT("-1"));
		return Def;
	}
}

// The regression for the tutorial keypad defect: `tutchopdoord` (spawnflags 256, no LOCKED bit)
// carries a prop_doorknob_electronic with `difficulty 1`, and a bare +use opened it because the
// door wrote its own unlocked state over the knob at attach time. Retail reads the other way —
// CBaseDoor::IsUseRefused (FUN_100eec70) consults the nearest knob and only falls back to the
// door's own byte when there is no knob, or no user.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorKnobLockAuthorityTest,
	"Elysium.Substrate.DoorKnobLockAuthority", GElysiumMoverTestFlags)
bool FElysiumDoorKnobLockAuthorityTest::RunTest(const FString&)
{
	using namespace ElysiumDoorKnobTests;

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__door_knob_authority__");
	// Unlocked door, locked knob — the tutchopdoord shape.
	Defs.Defs.Add(Door(TEXT("keypad_door"), TEXT("256")));
	Defs.Defs.Add(Knob(TEXT("keypad"), TEXT("keypad_door"), TEXT("1")));
	// Locked door, unlocked knob — the mirror. Retail lets the knob win here too.
	Defs.Defs.Add(Door(TEXT("open_knob_door"), TEXT("2304")));
	Defs.Defs.Add(Knob(TEXT("free_knob"), TEXT("open_knob_door"), TEXT("0")));
	// No knob at all: the door's own byte is still the authority.
	Defs.Defs.Add(Door(TEXT("plain_locked_door"), TEXT("2304")));
	// A non-character activator, to stand in for the relay/button/trigger that propagates itself.
	{
		FElysiumEntityDef Relay;
		Relay.Classname = TEXT("math_counter");
		Relay.TargetName = TEXT("not_a_character");
		Relay.Keys.Add(TEXT("min"), TEXT("0"));
		Relay.Keys.Add(TEXT("max"), TEXT("100"));
		Defs.Defs.Add(MoveTemp(Relay));
	}

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	AddExpectedError(TEXT("its attachment body is unavailable"),
		EAutomationExpectedErrorFlags::Contains, 2);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	World.Activate(0.0);

	auto FindDoor = [&World](const TCHAR* Name)
	{
		FElysiumEntity* E = World.FindByName(Name);
		return E ? E->AsDoorBase() : nullptr;
	};
	auto FindKnob = [&World](const TCHAR* Name)
	{
		FElysiumEntity* E = World.FindByName(Name);
		return E ? E->AsLockableEntity() : nullptr;
	};

	FElysiumDoorBase* KeypadDoor = FindDoor(TEXT("keypad_door"));
	FElysiumLockableEntity* Keypad = FindKnob(TEXT("keypad"));
	FElysiumDoorBase* OpenKnobDoor = FindDoor(TEXT("open_knob_door"));
	FElysiumLockableEntity* FreeKnob = FindKnob(TEXT("free_knob"));
	FElysiumDoorBase* PlainDoor = FindDoor(TEXT("plain_locked_door"));
	if (!TestNotNull(TEXT("keypad door resolves"), KeypadDoor)
		|| !TestNotNull(TEXT("keypad knob resolves"), Keypad)
		|| !TestNotNull(TEXT("open-knob door resolves"), OpenKnobDoor)
		|| !TestNotNull(TEXT("free knob resolves"), FreeKnob)
		|| !TestNotNull(TEXT("plain door resolves"), PlainDoor))
	{
		return false;
	}

	// The defect, stated directly: attaching to an unlocked door must not open the knob. Both of
	// these fail before the fix — RegisterDoorknob wrote the door's state over the knob's.
	TestTrue(TEXT("a difficulty knob stays locked on an unlocked door"), Keypad->IsUseLocked());
	TestTrue(TEXT("+use on the slab is refused by the knob"),
		KeypadDoor->IsUseRefused(PlayerHandle));
	// The reticle and the refusal are one predicate, so a knob-gated door cannot draw its unlocked
	// icon over a +use that will be refused.
	TestTrue(TEXT("the reticle reports the same refusal it will apply"), KeypadDoor->IsUseLocked());

	// The mirror: a locked door does not lock an unlocked knob, and the knob still decides. The
	// first fails before the fix; the second is the invariant that makes it meaningful.
	TestFalse(TEXT("a difficulty-0 knob stays unlocked on a locked door"), FreeKnob->IsUseLocked());
	TestTrue(TEXT("the door's own LOCKED byte survives, unread"), OpenKnobDoor->bLocked);
	TestFalse(TEXT("+use defers to the unlocked knob over the door's own locked byte"),
		OpenKnobDoor->IsUseRefused(PlayerHandle));

	// No knob: the door's byte is the authority, exactly as before.
	TestTrue(TEXT("a knobless locked door still refuses"), PlainDoor->IsUseRefused(PlayerHandle));

	// The null-activator fallthrough — GetNearestDoorknob returns null for a null user, so an
	// I/O-driven Open never consults the knob. This is what keeps every authored `Unlock -> door`
	// wire and every script-fired open working after the authority flip.
	const FElysiumEntityHandle NoActivator = FElysiumEntityHandle::Invalid();
	TestFalse(TEXT("a script-fired open bypasses the knob"), KeypadDoor->IsUseRefused(NoActivator));

	// Retail's user is the activator's CHARACTER sub-object, null for anything that is not one — so
	// a relay or trigger propagating itself as the activator is also "no user" and bypasses the
	// knob, exactly like a null handle. Without that rule those wires would start being gated.
	FElysiumEntity* Relay = World.FindByName(TEXT("not_a_character"));
	if (!TestNotNull(TEXT("non-character activator resolves"), Relay))
	{
		return false;
	}
	TestNull(TEXT("a math_counter is not a character"),
		reinterpret_cast<const void*>(Relay->AsCombatCharacter()));
	TestFalse(TEXT("a non-character activator bypasses the knob"),
		KeypadDoor->IsUseRefused(Relay->Handle));
	KeypadDoor->InputOpen(NoActivator);
	TestEqual(TEXT("script-fired Open still opens a knob-gated door"), KeypadDoor->State(),
		FElysiumDoorBase::EToggleState::GoingUp);

	// ...while a player-activated Open on the same door is refused.
	OpenKnobDoor->InputOpen(PlayerHandle);   // knob unlocked -> admitted
	TestEqual(TEXT("player Open admitted through an unlocked knob"), OpenKnobDoor->State(),
		FElysiumDoorBase::EToggleState::GoingUp);
	FElysiumDoorBase* SecondKeypad = KeypadDoor;
	SecondKeypad->InputToggle(PlayerHandle);
	TestEqual(TEXT("player Toggle refused by the locked knob leaves the state alone"),
		SecondKeypad->State(), FElysiumDoorBase::EToggleState::GoingUp);

	return true;
}

// Lock/Unlock are per-authority. A knob's Unlock writes the knob; a door's Unlock writes the door.
// Eleven authored wires in the corpus aim `Unlock` straight at a knob, and they must land there.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorKnobUnlockInputsTest,
	"Elysium.Substrate.DoorKnobUnlockInputs", GElysiumMoverTestFlags)
bool FElysiumDoorKnobUnlockInputsTest::RunTest(const FString&)
{
	using namespace ElysiumDoorKnobTests;

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__door_knob_unlock__");
	Defs.Defs.Add(Door(TEXT("gate"), TEXT("2304")));         // LOCKED door
	Defs.Defs.Add(Knob(TEXT("gate_knob"), TEXT("gate"), TEXT("5")));   // locked knob

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	AddExpectedError(TEXT("its attachment body is unavailable"),
		EAutomationExpectedErrorFlags::Contains, 1);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	World.Activate(0.0);

	FElysiumEntity* DoorEntity = World.FindByName(TEXT("gate"));
	FElysiumEntity* KnobEntity = World.FindByName(TEXT("gate_knob"));
	FElysiumDoorBase* LiveDoor = DoorEntity ? DoorEntity->AsDoorBase() : nullptr;
	FElysiumLockableEntity* LiveKnob = KnobEntity ? KnobEntity->AsLockableEntity() : nullptr;
	if (!TestNotNull(TEXT("door resolves"), LiveDoor) || !TestNotNull(TEXT("knob resolves"), LiveKnob))
	{
		return false;
	}
	TestTrue(TEXT("both authorities start locked"), LiveDoor->bLocked && LiveKnob->IsUseLocked());

	// Unlock the DOOR: its byte clears, the knob does not, and a player +use is still refused.
	LiveDoor->InputUnlock();
	TestFalse(TEXT("door Unlock clears the door byte"), LiveDoor->bLocked);
	TestTrue(TEXT("door Unlock does not reach the knob"), LiveKnob->IsUseLocked());
	TestTrue(TEXT("a locked knob still gates the door after the door unlocks"),
		LiveDoor->IsUseRefused(PlayerHandle));

	// Unlock the KNOB: the authored `Unlock -> knob` wire is what actually opens the way.
	LiveKnob->InputUnlock(PlayerHandle);
	TestFalse(TEXT("knob Unlock clears the knob"), LiveKnob->IsUseLocked());
	TestFalse(TEXT("with both clear the door admits a player +use"),
		LiveDoor->IsUseRefused(PlayerHandle));

	// Re-lock the knob alone and confirm the door byte was not dragged along.
	LiveKnob->InputLock();
	TestTrue(TEXT("knob Lock re-locks only the knob"), LiveKnob->IsUseLocked());
	TestFalse(TEXT("knob Lock leaves the door byte clear"), LiveDoor->bLocked);
	TestTrue(TEXT("the re-locked knob gates again"), LiveDoor->IsUseRefused(PlayerHandle));

	return true;
}

// GetNearestDoorknob (FUN_100ee950): Manhattan distance between world-space centres, ties to the
// second-registered knob. A double-knobbed door asks whichever handle the user is standing at.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorNearestKnobSelectionTest,
	"Elysium.Substrate.DoorNearestKnobSelection", GElysiumMoverTestFlags)
bool FElysiumDoorNearestKnobSelectionTest::RunTest(const FString&)
{
	using namespace ElysiumDoorKnobTests;

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__door_nearest_knob__");
	// Placed so Manhattan and Euclidean DISAGREE: from the origin the first knob is 100 away by
	// both metrics, while the second is 120 by Manhattan but only ~84.9 by Euclidean. Manhattan
	// must pick `inside`; a Euclidean implementation would pick `outside` and flip every assertion
	// below. Inside handle unlocked, outside locked — the "locked from the street" door.
	Defs.Defs.Add(Door(TEXT("double_door"), TEXT("256")));
	Defs.Defs.Add(Knob(TEXT("inside"), TEXT("double_door"), TEXT("0"), FVector(100.0, 0.0, 0.0)));
	Defs.Defs.Add(Knob(TEXT("outside"), TEXT("double_door"), TEXT("7"), FVector(0.0, 60.0, 60.0)));
	// A third door whose two knobs are exactly equidistant, to pin the tie-break direction.
	Defs.Defs.Add(Door(TEXT("tie_door"), TEXT("256")));
	Defs.Defs.Add(Knob(TEXT("tie_first"), TEXT("tie_door"), TEXT("0"), FVector(0.0, 50.0, 0.0)));
	Defs.Defs.Add(Knob(TEXT("tie_second"), TEXT("tie_door"), TEXT("9"), FVector(0.0, -50.0, 0.0)));

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	AddExpectedError(TEXT("its attachment body is unavailable"),
		EAutomationExpectedErrorFlags::Contains, 4);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	World.Activate(0.0);

	FElysiumEntity* DoorEntity = World.FindByName(TEXT("double_door"));
	FElysiumDoorBase* LiveDoor = DoorEntity ? DoorEntity->AsDoorBase() : nullptr;
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("door resolves"), LiveDoor) || !TestNotNull(TEXT("player resolves"), Player))
	{
		return false;
	}

	// Manhattan: inside = 100, outside = 0 + 60 + 60 = 120 -> inside wins.
	// Euclidean:  inside = 100, outside = sqrt(60^2 + 60^2) ~= 84.9 -> outside would win.
	Player->Origin = FVector::ZeroVector;
	const FElysiumLockableEntity* Near = LiveDoor->FindNearestDoorknob(PlayerHandle);
	if (!TestNotNull(TEXT("a knob is selected"), Near))
	{
		return false;
	}
	TestEqual(TEXT("the Manhattan-nearest knob is chosen, not the Euclidean-nearest"),
		Near->TargetName, FString(TEXT("inside")));
	TestFalse(TEXT("that unlocked handle admits the +use"), LiveDoor->IsUseRefused(PlayerHandle));

	// Standing on the far side, the locked handle is now nearest and the same door refuses.
	Player->Origin = FVector(0.0, 60.0, 60.0);
	const FElysiumLockableEntity* FarSide = LiveDoor->FindNearestDoorknob(PlayerHandle);
	if (!TestNotNull(TEXT("a knob is selected from the far side"), FarSide))
	{
		return false;
	}
	TestEqual(TEXT("the other handle is selected from the other side"), FarSide->TargetName,
		FString(TEXT("outside")));
	TestTrue(TEXT("the locked handle refuses the same door"), LiveDoor->IsUseRefused(PlayerHandle));

	// An exact draw goes to the second-registered knob (retail returns the 0x62c handle when its
	// distance is <= the 0x628 handle's). Here that knob is the locked one, so the tie is visible.
	FElysiumEntity* TieEntity = World.FindByName(TEXT("tie_door"));
	FElysiumDoorBase* TieDoor = TieEntity ? TieEntity->AsDoorBase() : nullptr;
	if (!TestNotNull(TEXT("tie door resolves"), TieDoor))
	{
		return false;
	}
	Player->Origin = FVector::ZeroVector;   // exactly 50 from each handle
	const FElysiumLockableEntity* Tie = TieDoor->FindNearestDoorknob(PlayerHandle);
	if (!TestNotNull(TEXT("a knob is selected on an exact tie"), Tie))
	{
		return false;
	}
	TestEqual(TEXT("an exact tie goes to the second-registered knob"), Tie->TargetName,
		FString(TEXT("tie_second")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
