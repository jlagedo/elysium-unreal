#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <limits>

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumStub.h"
#include "Misc/ScopeExit.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcTestHull.h"
#include "Substrate/ElysiumNpcUsedHullBits.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29d, family **Motor10** — `CAI_Motor`, `CAI_Navigator`, the standoff behaviour and goal,
// the hull probe, the obstructing-door body and the shoot target. One case per `rule` row of
// `checklist-10-18.md`, each derived from the decompiled C or the listing, with the address it came
// from named beside it.
//
// Where an input is a SEAM the case says so and asserts that the seam was ASKED and that the
// refusal was the recovered one — the only honest assertion available until the seam has a source.
// This family has almost nothing but seams: there is no navigator, no node graph, no move probe, no
// hull table, no path object, no goal entity and no behaviour object in this substrate.

static constexpr EAutomationTestFlags GElysiumNpcKernelMotor10Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Source units into this world's centimetres, Y negated — the inverse of the bodies' own
	// conversion, so a case can place a body at a retail distance. Prefixed because the module
	// builds adaptive-unity and this anonymous namespace is merged with the other suites'.
	FVector Motor10PortUnits(double X, double Y, double Z)
	{
		return FVector(X * ElysiumMove::U, -Y * ElysiumMove::U, Z * ElysiumMove::U);
	}

	FVector Motor10SourceUnits(const FVector& Cm)
	{
		return FVector(Cm.X / ElysiumMove::U, -Cm.Y / ElysiumMove::U, Cm.Z / ElysiumMove::U);
	}

	// One NPC and one door-shaped entity, quiet, so nothing competes with the pass a case drives.
	struct FMotor10Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;
		FElysiumEntity* Door = nullptr;

		FMotor10Fixture()
			: World([]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("motor10_kernel"), 4711);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpc(TEXT("guard"), FVector(200.f, 0.f, 0.f));
					Builder.AddEntity(TEXT("func_door"), TEXT("door"), FVector(600.f, 0.f, 0.f));
					return Builder;
				}())
		{
			Npc = World.Npc(TEXT("guard"));
			Door = World.World.FindByName(TEXT("door"));
			FElysiumNpcWorldFixture::Quiet({ Npc });
		}
	};
}

// =================================================================================================
// `0x10273070` — `SetHullSizeNormal`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotor10HullNormalTest,
	"Elysium.Substrate.NpcKernelMotor10.SetHullSizeNormal", GElysiumNpcKernelMotor10Flags)
bool FElysiumNpcKernelMotor10HullNormalTest::RunTest(const FString&)
{
	FMotor10Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	F.Npc->bIsUsingSmallHull = false;
	F.Npc->SetSizeCalls = 0;
	F.Npc->SetHullSizeNormal(false);
	TestEqual(TEXT("0x10273070 with the small hull clear and no force does nothing"),
		F.Npc->SetSizeCalls, 0);

	// The gate: SET *or* forced.
	F.Npc->bIsUsingSmallHull = true;
	F.Npc->SetHullSizeNormal(false);
	TestEqual(TEXT("a SET small-hull flag opens the gate"), F.Npc->SetSizeCalls, 1);
	TestFalse(TEXT("...and the flag is cleared"), F.Npc->bIsUsingSmallHull);

	F.Npc->SetHullSizeNormal(true);
	TestEqual(TEXT("the force byte opens it with the flag already clear"), F.Npc->SetSizeCalls, 2);

	// `SetHullSizeNormal` sizes the body from the hull table's FULL pair (`0x102d6100` /
	// `0x102d6120`). The NPC stands on hull 0, so that is HUMAN_HULL's 26 x 72 box.
	TestEqual(TEXT("UTIL_SetSize is handed HUMAN_HULL's full mins"),
		F.Npc->LastSetSizeMinsUnits, FVector(-13.0, -13.0, 0.0));
	TestEqual(TEXT("...and its full maxs"),
		F.Npc->LastSetSizeMaxsUnits, FVector(13.0, 13.0, 72.0));

	// The small twin reads the SAME row's other pair (`0x102d6140` / `0x102d6160`), never a
	// different hull id.
	F.Npc->SetHullSizeSmall(true);
	TestEqual(TEXT("the small twin is handed HUMAN_HULL's small mins"),
		F.Npc->LastSetSizeMinsUnits, FVector(-8.0, -8.0, 0.0));
	TestEqual(TEXT("...and its small maxs"),
		F.Npc->LastSetSizeMaxsUnits, FVector(8.0, 8.0, 72.0));
	F.Npc->SetHullSizeNormal(true);

	// The VPhysics rebuild is gated on `+0x36c`, and the `+0x5f2d` clear happens on BOTH arms — the
	// `MOV byte [ESI+0x5f2d],0` at `1027312a` sits before the `JZ` at `10273131`.
	F.Npc->VPhysicsHullRebuilds = 0;
	F.Npc->bHasVPhysicsObject = false;
	F.Npc->bIsUsingSmallHull = true;
	F.Npc->SetHullSizeNormal(false);
	TestEqual(TEXT("no physics object, no rebuild"), F.Npc->VPhysicsHullRebuilds, 0);
	TestFalse(TEXT("...but the flag is still cleared"), F.Npc->bIsUsingSmallHull);

	F.Npc->bHasVPhysicsObject = true;
	F.Npc->bIsUsingSmallHull = true;
	F.Npc->SetHullSizeNormal(false);
	TestEqual(TEXT("a live physics object rebuilds the VPhysics hull (0x10272f40)"),
		F.Npc->VPhysicsHullRebuilds, 1);
	return true;
}

// =================================================================================================
// `0x10273180` — `SetHullSizeSmall`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotor10HullSmallTest,
	"Elysium.Substrate.NpcKernelMotor10.SetHullSizeSmall", GElysiumNpcKernelMotor10Flags)
bool FElysiumNpcKernelMotor10HullSmallTest::RunTest(const FString&)
{
	FMotor10Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// The gate is INVERTED: it runs when the small hull is NOT already in use.
	F.Npc->bIsUsingSmallHull = false;
	F.Npc->SetSizeCalls = 0;
	TestTrue(TEXT("0x10273180 answers 1"), F.Npc->SetHullSizeSmall(false));
	TestEqual(TEXT("...and it resized"), F.Npc->SetSizeCalls, 1);
	TestTrue(TEXT("...and set m_fIsUsingSmallHull"), F.Npc->bIsUsingSmallHull);

	// **The refused path still answers 1.** `return CONCAT31(uVar3, 1)` is outside the `if`.
	TestTrue(TEXT("a second call is refused and STILL answers 1"), F.Npc->SetHullSizeSmall(false));
	TestEqual(TEXT("...having resized nothing"), F.Npc->SetSizeCalls, 1);

	// ...unless forced.
	TestTrue(TEXT("the force byte opens the inverted gate"), F.Npc->SetHullSizeSmall(true));
	TestEqual(TEXT("...and it resized again"), F.Npc->SetSizeCalls, 2);

	// The physics gate is the same one.
	F.Npc->VPhysicsHullRebuilds = 0;
	F.Npc->bHasVPhysicsObject = true;
	F.Npc->bIsUsingSmallHull = false;
	F.Npc->SetHullSizeSmall(false);
	TestEqual(TEXT("a live physics object rebuilds the VPhysics hull here too"),
		F.Npc->VPhysicsHullRebuilds, 1);
	return true;
}

// =================================================================================================
// `0x10278650` — `GetShootTarget`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotor10ShootTargetTest,
	"Elysium.Substrate.NpcKernelMotor10.GetShootTarget", GElysiumNpcKernelMotor10Flags)
bool FElysiumNpcKernelMotor10ShootTargetTest::RunTest(const FString&)
{
	FMotor10Fixture F;
	if (F.Npc == nullptr || F.Door == nullptr)
	{
		AddError(TEXT("no NPC or no door"));
		return false;
	}

	// Arm 2 first, because it is the one an untouched body takes: no shoot-target override and no
	// enemy, so the answer is `AngleVectors(GetAngles(), &forward) + posSrc`.
	F.Npc->ShootTargetOverride = FElysiumEntityHandle();
	F.Npc->Angles = FVector::ZeroVector;
	const FVector PosSrc(10.0, 20.0, 30.0);
	const FVector NoEnemy = F.Npc->GetShootTarget(PosSrc, false, false);
	// Zero angles: `forward = (cos0*cos0, cos0*sin0, -sin0)` = `(1, 0, 0)`.
	TestEqual(TEXT("0x10278650 with no enemy answers forward + posSrc"), NoEnemy,
		FVector(11.0, 20.0, 30.0));

	// The forward is `0x10139610`'s, with `_DAT_1044eb08` the degrees-to-radians scale. A 90 degree
	// yaw turns `(1,0,0)` into `(0,1,0)`.
	F.Npc->Angles = FVector(0.f, 90.f, 0.f);
	const FVector YawedForward = FElysiumNpcBase::AngleVectorsForward(F.Npc->Angles);
	TestTrue(TEXT("AngleVectors at yaw 90 answers +Y"),
		YawedForward.Equals(FVector(0.0, 1.0, 0.0), 1e-5));
	// A 90 degree PITCH answers `-Z`, which is the sign retail's `-s2` carries.
	F.Npc->Angles = FVector(90.f, 0.f, 0.f);
	TestTrue(TEXT("AngleVectors at pitch 90 answers -Z (retail's -sin(pitch))"),
		FElysiumNpcBase::AngleVectorsForward(F.Npc->Angles).Equals(FVector(0.0, 0.0, -1.0), 1e-5));
	F.Npc->Angles = FVector::ZeroVector;

	// Arm 1: a resolving `m_hShootTargetOverride` (+0x5ba8) wins outright and **ignores all three
	// arguments**.
	F.Door->Origin = Motor10PortUnits(123.0, -45.0, 67.0);
	F.Npc->ShootTargetOverride = F.Door->Handle;
	const FVector Overridden = F.Npc->GetShootTarget(PosSrc, true, true);
	TestTrue(TEXT("0x10278650 answers the override's GetAbsOrigin verbatim"),
		Overridden.Equals(FVector(123.0, -45.0, 67.0), 1e-3));
	TestTrue(TEXT("...and the posSrc argument is not in the answer at all"),
		!Overridden.Equals(FVector(123.0 + 10.0, -45.0 + 20.0, 67.0 + 30.0), 1e-3));

	// A STALE override handle falls through to the enemy arms, because retail validates it against
	// `PTR_DAT_10566458` before it reads the origin.
	F.Npc->ShootTargetOverride = FElysiumEntityHandle();
	TestEqual(TEXT("a cleared override falls back to the no-enemy arm"),
		F.Npc->GetShootTarget(PosSrc, false, false), FVector(11.0, 20.0, 30.0));

	// The stat arm is a SEAM and answers 0, which is not 5, so the `-30.0` Z offset is never
	// applied. `_DAT_104994e0` is negative — the target moves DOWN, not up.
	TestEqual(TEXT("the type-3 CVStatList seam answers 0 (0x102012d0 on an empty list)"),
		FElysiumNpcBase::EnemyTypedStatValue(*F.Door, 0xb), 0);
	return true;
}

// =================================================================================================
// `0x102984a0` — slot 531 `OnObstructingDoor`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotor10ObstructingDoorTest,
	"Elysium.Substrate.NpcKernelMotor10.OnObstructingDoor", GElysiumNpcKernelMotor10Flags)
bool FElysiumNpcKernelMotor10ObstructingDoorTest::RunTest(const FString&)
{
	FMotor10Fixture F;
	if (F.Npc == nullptr || F.Door == nullptr)
	{
		AddError(TEXT("no NPC or no door"));
		return false;
	}

	FElysiumNpcBase::FLocalMoveGoal Goal;
	int32 Result = 0x7f;

	// Arm 0: a NULL door warns and answers false without touching the result.
	Goal.MaxDistanceUnits = 100.f;
	TestFalse(TEXT("0x102984a0 refuses a null door"),
		F.Npc->OnObstructingDoor(&Goal, nullptr, 10.f, &Result));
	TestEqual(TEXT("...and writes no result"), Result, 0x7f);

	// The gate: the body runs only when `moveGoal->maxDist >= distClear`.
	Goal.MaxDistanceUnits = 5.f;
	TestFalse(TEXT("a goal shorter than the clearance is not obstructed"),
		F.Npc->OnObstructingDoor(&Goal, F.Door, 10.f, &Result));
	TestEqual(TEXT("...and still writes nothing"), Result, 0x7f);

	// Arm 1: the door we are already holding. `m_hOpeningDoor` is **+0x5d24**, which the shape map
	// binds as `OpeningDoor` — NOT `+0x644c`, which is `m_eAlternateAI`.
	Goal.MaxDistanceUnits = 100.f;
	F.Npc->OpeningDoor = F.Door->Handle;
	F.Npc->DoorBlockWrites.Empty();
	Result = 0x7f;
	TestTrue(TEXT("the door already in m_hOpeningDoor (+0x5d24) is claimed"),
		F.Npc->OnObstructingDoor(&Goal, F.Door, 10.f, &Result));
	TestEqual(TEXT("...with result 0"), Result, 0);
	if (TestEqual(TEXT("...and one flag write"), F.Npc->DoorBlockWrites.Num(), 1))
	{
		TestTrue(TEXT("...which is the CLEAR, 0x100f0e70"), F.Npc->DoorBlockWrites[0].bClear);
	}

	// Arm 2 is unreachable: `ConnectedSquad()` answers null in this substrate, which is retail's own
	// `m_pSquad == NULL` arm. Stated rather than faked.
	TestTrue(TEXT("there is no squad object, so the squad-focus arm cannot run"),
		F.Npc->ConnectedSquad() == nullptr);

	// Arm 3: a SCRIPTED body (slot 464 `GetState() == 4`) whose capabilities do NOT carry the full
	// `0xd00`. The spawn chain now runs (`CNPC_VHuman::Spawn` `0x10384690` adds `0xc200d00`), so the
	// case clears the three bits to open the arm (integrator correction, story 8 L08). The state is
	// pushed through the one public writer this leaf has — an `aiscripted_schedule`'s `forcestate`,
	// which is `Mind.RequestState` and admits `Scripted`.
	F.Npc->OpeningDoor = FElysiumEntityHandle();
	F.Npc->CapabilityWord &= ~0xd00;
	FElysiumScriptedScheduleOrder Order;
	F.Npc->BeginScriptedSchedule(Order, /*bHasForcedState=*/true, EElysiumNpcState::Scripted);
	if (!TestEqual(TEXT("the body is in retail NPC_STATE_SCRIPT (4)"),
		static_cast<int32>(F.Npc->GetState()), static_cast<int32>(EElysiumNpcState::Scripted)))
	{
		return false;
	}
	F.Npc->DoorBlockWrites.Empty();
	F.Npc->AlternateAi = 0;
	Result = 0x7f;
	// A clearance at or above `_DAT_10451acc` = **64.0** takes the arm WITHOUT arming the
	// alternate AI — the test is a strict `<`.
	TestTrue(TEXT("a scripted body claims the door"),
		F.Npc->OnObstructingDoor(&Goal, F.Door, 64.f, &Result));
	TestEqual(TEXT("...with result 0"), Result, 0);
	TestEqual(TEXT("...and 64.0 exactly does NOT arm the alternate AI"), F.Npc->AlternateAi, 0);
	TestFalse(TEXT("...and does not take the door"), F.Npc->OpeningDoor.IsSet());

	// Under 64.0 it does arm it, with `curtime + _DAT_10449258` = **3.0**.
	Result = 0x7f;
	F.Npc->DoorBlockWrites.Empty();
	const double Now = F.Npc->World != nullptr ? F.Npc->World->NowSeconds() : 0.0;
	TestTrue(TEXT("a clearance under 64.0 claims it too"),
		F.Npc->OnObstructingDoor(&Goal, F.Door, 63.9f, &Result));
	TestEqual(TEXT("...with result 0"), Result, 0);
	TestEqual(TEXT("...m_eAlternateAI (+0x644c) = 4"), F.Npc->AlternateAi, 4);
	TestEqual(TEXT("...m_flAlternateAIExpireTimer (+0x6450) = curtime + 3.0"),
		F.Npc->AlternateAiExpireTime, Now + 3.0, 1e-6);
	TestTrue(TEXT("...and m_hOpeningDoor takes the door"), F.Npc->OpeningDoor.IsSet());
	F.Npc->BeginScriptedSchedule(Order, /*bHasForcedState=*/true, EElysiumNpcState::Idle);
	F.Npc->OpeningDoor = FElysiumEntityHandle();
	F.Npc->AlternateAi = 0;

	// Arm 4: `0x1027f550` refuses a body whose capabilities do not carry the full `0xd00`, and its
	// refusal ORs `0x8` into the door's block word FIRST — so the arm produces TWO writes, `0x8`
	// from the gate and `0x4` from this arm.
	F.Npc->DoorBlockWrites.Empty();
	Result = 0x7f;
	TestTrue(TEXT("0x1027f550 refusing still claims the door"),
		F.Npc->OnObstructingDoor(&Goal, F.Door, 10.f, &Result));
	TestEqual(TEXT("...with result -2 (AIMR_BLOCKED_WORLD)"), Result, -2);
	if (TestEqual(TEXT("...and two flag writes"), F.Npc->DoorBlockWrites.Num(), 2))
	{
		TestEqual(TEXT("...0x8 from the capability gate inside 0x1027f550"),
			static_cast<int32>(F.Npc->DoorBlockWrites[0].Bits), 0x8);
		TestEqual(TEXT("...then 0x4 from the arm itself"),
			static_cast<int32>(F.Npc->DoorBlockWrites[1].Bits), 0x4);
	}

	// `0x1027f550` on its own: a NULL door writes NOTHING at all, which is the one arm of it that
	// does not touch the flag word.
	F.Npc->DoorBlockWrites.Empty();
	TestFalse(TEXT("0x1027f550 refuses a null door"), F.Npc->CanOpenDoorNow(nullptr));
	TestEqual(TEXT("...and writes no flag"), F.Npc->DoorBlockWrites.Num(), 0);

	// The retry stamp is family Senses' `+0x640` seam and answers 0.0, which is never above
	// `curtime`, so the `0x10` refusal cannot fire here.
	TestEqual(TEXT("the door retry stamp (+0x640) seam answers 0.0"),
		F.Npc->DoorNextTryTime(*F.Door), 0.0);

	// Arm 5 is where every capability-less body actually ends: `TEST AH,0xd` is ANY of `0xd00`, and
	// with a zero capability word the body answers FALSE having written nothing more. It is not
	// reachable past arm 4 without a capability word, which this substrate has no writer for — the
	// arm is exercised through the seam it depends on being stated.
	TestEqual(TEXT("with 0xd00 cleared (above), CapabilitiesGet() carries none of it, which closes arms 3-5"),
		F.Npc->CapabilitiesGet() & 0xd00, 0);

	// The pathfinder and the splice are seams: `0x10304130` answers NOT FOUND, which is the arm
	// that reaches the door-type split.
	F.Npc->Motor10Seams.BuildLocalRouteAsks = 0;
	TestFalse(TEXT("CAI_Pathfinder::BuildLocalRoute (0x10304130) answers no waypoint"),
		F.Npc->BuildLocalRouteThroughDoor(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 0x30));
	TestEqual(TEXT("...and the ask is recorded"), F.Npc->Motor10Seams.BuildLocalRouteAsks, 1);
	TestFalse(TEXT("the path splice (0x10319f30) answers false"), F.Npc->SplicePathWaypoint(1));

	// The door's toggle state is the real word (+0x4f8) since 0018/7: a fresh door is closed,
	// `TS_AT_BOTTOM` = 1.
	TestEqual(TEXT("the door toggle-state (+0x4f8) reads the door: closed = 1"),
		F.Npc->RetailDoorToggleState(*F.Door), 1);

	// The NaN gate: an unordered compare RUNS the body (`TEST AH,5; JNP`), which is the arm the
	// listing takes and a `>=` in C would not.
	Goal.MaxDistanceUnits = std::numeric_limits<float>::quiet_NaN();
	F.Npc->OpeningDoor = F.Door->Handle;
	Result = 0x7f;
	TestTrue(TEXT("a NaN maxDist still runs the body (the compare is unordered)"),
		F.Npc->OnObstructingDoor(&Goal, F.Door, 10.f, &Result));
	TestEqual(TEXT("...and the held-door arm answers 0"), Result, 0);
	return true;
}

// =================================================================================================
// `0x102d72f0` — `CAI_TestHull::Spawn`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotor10TestHullSpawnTest,
	"Elysium.Substrate.NpcKernelMotor10.TestHullSpawn", GElysiumNpcKernelMotor10Flags)
bool FElysiumNpcKernelMotor10TestHullSpawnTest::RunTest(const FString&)
{
	// The hull pick, every arm, as a pure function.
	bool bFallback = false;

	// A ZERO mask short-circuits — `TEST EBX,EBX; JLE` — to hull 0 **without** the fallback call.
	TestEqual(TEXT("0x102d72f5: a zero used-hull mask answers hull 0"),
		FElysiumNpcTestHull::PickHull(0, [](int32) { return 0xff; }, bFallback), 0);
	TestFalse(TEXT("...without taking the 22-miss fallback"), bFallback);

	// The test is SIGNED, so the shipped `.data` initialiser `0xffffffff` short-circuits the same
	// way. That is the correction this family made to the walk.
	TestEqual(TEXT("a NEGATIVE mask (the shipped 0xffffffff) also answers hull 0 at once"),
		FElysiumNpcTestHull::PickHull(-1, [](int32 Hull) { return Hull == 5 ? 0x20 : 0; }, bFallback),
		0);
	TestFalse(TEXT("...and still skips the fallback"), bFallback);

	// A positive mask walks 0..21 and takes the FIRST intersection.
	TestEqual(TEXT("the first hull whose bits intersect the mask wins"),
		FElysiumNpcTestHull::PickHull(0x28, [](int32 Hull) { return 1 << Hull; }, bFallback), 3);
	TestFalse(TEXT("...no fallback"), bFallback);
	TestEqual(TEXT("a later-only bit still wins when it is the only one"),
		FElysiumNpcTestHull::PickHull(0x20, [](int32 Hull) { return 1 << Hull; }, bFallback), 5);

	// The walk stops at 22 — hull 22's bit is never reached.
	TestEqual(TEXT("22 misses fall back to hull 0"),
		FElysiumNpcTestHull::PickHull(1 << 22, [](int32 Hull) { return 1 << Hull; }, bFallback), 0);
	TestTrue(TEXT("...through the fallback, which ORs 0 (a no-op)"), bFallback);
	TestEqual(TEXT("...and the no-op OR left the mask alone"), ElysiumNpcUsedHullBits::Get(), 0);

	// Hull 21 is the LAST index the walk reaches.
	TestEqual(TEXT("hull 21 is inside the walk"),
		FElysiumNpcTestHull::PickHull(1 << 21, [](int32 Hull) { return 1 << Hull; }, bFallback), 21);

	// The two mask writers.
	ElysiumNpcUsedHullBits::Clear();
	TestEqual(TEXT("0x102f9900 clears the mask"), ElysiumNpcUsedHullBits::Get(), 0);
	ElysiumNpcUsedHullBits::Add(0x6);
	ElysiumNpcUsedHullBits::Add(0x8);
	TestEqual(TEXT("0x102f9920 ORs bits in"), ElysiumNpcUsedHullBits::Get(), 0xe);
	ElysiumNpcUsedHullBits::Clear();

	// The whole body, on its own class (story 5 fold A1): no classname builds a test hull, so the
	// case constructs the C++ type directly, as retail's graph-build code does.
	FElysiumNpcTestHull Hull;
	Hull.HullKind = 9;
	Hull.Health = 1;
	Hull.Flags = 0;
	Hull.RetailSolidFlags = 0;
	Hull.RetailSolidType = 0;
	Hull.RetailSolidSets = 0;
	Hull.SetSizeCalls = 0;
	Hull.bUnknown5f44 = true;
	Hull.bIsUsingSmallHull = true;

	ElysiumStub::ClearTally();
	ON_SCOPE_EXIT { ElysiumStub::ClearTally(); };
	// Through the entity's slot 103, as the world's spawn pass calls it.
	static_cast<FElysiumEntity&>(Hull).Spawn();

	TestEqual(TEXT("0x102d72f0 picks hull 0 through the seam"), Hull.HullKind, 0);
	TestEqual(TEXT("...and resizes the bounds through 0x10273070"), Hull.SetSizeCalls, 1);
	TestFalse(TEXT("...which cleared m_fIsUsingSmallHull"), Hull.bIsUsingSmallHull);
	TestEqual(TEXT("SetSolid(SOLID_BBOX = 2)"), Hull.RetailSolidType, 2);
	TestEqual(TEXT("...once"), Hull.RetailSolidSets, 1);
	TestEqual(TEXT("AddSolidFlags ORs FSOLID_NOT_SOLID (0x4)"),
		static_cast<int32>(Hull.RetailSolidFlags & 0x4), 0x4);
	TestEqual(TEXT("slot 93 SetMoveType(MOVETYPE_FLY = 4, ...)"), Hull.RetailMoveType, 4);
	TestEqual(TEXT("...with MOVECOLLIDE_DEFAULT = 0"), Hull.RetailMoveCollide, 0);
	TestEqual(TEXT("m_iHealth (+0x210) = 0x32 = 50"), Hull.Health, 50);
	TestEqual(TEXT("AddFlag(0x40000)"), Hull.Flags & 0x40000, 0x40000);
	TestFalse(TEXT("byte [+0x5f44] = 0"), Hull.bUnknown5f44);

	// The tail is a JMP to slot 66 `Hide()`. Slot 66 is still one of story 29c's generated stubs, so
	// what is observable is the stub tally — which is exactly the claim: the tail dispatched.
	TArray<ElysiumStub::FTally> Tally;
	ElysiumStub::CollectTally(Tally);
	const ElysiumStub::FTally* HideRow = Tally.FindByPredicate(
		[](const ElysiumStub::FTally& Row)
		{
			return Row.Kind == TEXT("slot") && Row.Surface == TEXT("CBaseEntity::Hide");
		});
	if (TestNotNull(TEXT("the tail JMP to slot 66 Hide() dispatched"), HideRow))
	{
		TestEqual(TEXT("...exactly once"), HideRow->Count, 1);
	}
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
