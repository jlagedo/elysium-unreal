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
#include "Substrate/ElysiumNpcKernelClassLookup.h"
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

	// The self-check runs UNCONDITIONALLY and BEFORE the gate: with `NAI_Hull::Bits` answering 0 and
	// the used mask 0, `(0 & 0) != 0` is false — so an un-precached hull is NOT reported for a hull
	// whose bits are themselves zero. That is retail's arithmetic, not a port softening.
	FElysiumNpc::RetailClearUsedHullBits();
	F.Npc->bIsUsingSmallHull = false;
	F.Npc->HullNotPrecachedWarnings = 0;
	F.Npc->SetSizeCalls = 0;
	F.Npc->SetHullSizeNormal(false);
	TestEqual(TEXT("0x10273070 with the small hull clear and no force does nothing"),
		F.Npc->SetSizeCalls, 0);
	TestEqual(TEXT("...and the seam's zero bits make the precache check pass"),
		F.Npc->HullNotPrecachedWarnings, 0);

	// The gate: SET *or* forced.
	F.Npc->bIsUsingSmallHull = true;
	F.Npc->SetHullSizeNormal(false);
	TestEqual(TEXT("a SET small-hull flag opens the gate"), F.Npc->SetSizeCalls, 1);
	TestFalse(TEXT("...and the flag is cleared"), F.Npc->bIsUsingSmallHull);

	F.Npc->SetHullSizeNormal(true);
	TestEqual(TEXT("the force byte opens it with the flag already clear"), F.Npc->SetSizeCalls, 2);

	// `RetailHullExtents` is family Motor's seam and answers the ZERO box, so `UTIL_SetSize` is
	// asked with a degenerate hull. That is the recovered refusal, asserted rather than worked
	// around.
	TestEqual(TEXT("the hull table seam hands UTIL_SetSize a zero mins"),
		F.Npc->LastSetSizeMinsUnits, FVector::ZeroVector);
	TestEqual(TEXT("...and a zero maxs"), F.Npc->LastSetSizeMaxsUnits, FVector::ZeroVector);

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

	// The ERROR block: raise the used mask so `(bits & used) != bits` becomes true for a hull whose
	// bits are 0 — it cannot, because 0 & anything is 0. The arm that CAN fire is the one where the
	// hull table answers a non-zero bit the mask does not carry, which this substrate's seam cannot
	// produce; the test states that rather than faking one.
	FElysiumNpc::RetailAddUsedHullBits(0x7fffffff);
	F.Npc->HullNotPrecachedWarnings = 0;
	F.Npc->bIsUsingSmallHull = true;
	F.Npc->SetHullSizeNormal(false);
	TestEqual(TEXT("the precache ERROR block cannot fire while NAI_Hull::Bits answers 0"),
		F.Npc->HullNotPrecachedWarnings, 0);
	FElysiumNpc::RetailClearUsedHullBits();
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

	// No self-check on this body: the ERROR block is `SetHullSizeNormal`'s alone.
	F.Npc->HullNotPrecachedWarnings = 0;
	F.Npc->bIsUsingSmallHull = false;
	F.Npc->SetHullSizeSmall(false);
	TestEqual(TEXT("the small-hull body has no precache self-check"),
		F.Npc->HullNotPrecachedWarnings, 0);

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
	const FVector YawedForward = FElysiumNpc::AngleVectorsForward(F.Npc->Angles);
	TestTrue(TEXT("AngleVectors at yaw 90 answers +Y"),
		YawedForward.Equals(FVector(0.0, 1.0, 0.0), 1e-5));
	// A 90 degree PITCH answers `-Z`, which is the sign retail's `-s2` carries.
	F.Npc->Angles = FVector(90.f, 0.f, 0.f);
	TestTrue(TEXT("AngleVectors at pitch 90 answers -Z (retail's -sin(pitch))"),
		FElysiumNpc::AngleVectorsForward(F.Npc->Angles).Equals(FVector(0.0, 0.0, -1.0), 1e-5));
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
		FElysiumNpc::EnemyTypedStatValue(*F.Door, 0xb), 0);
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

	FElysiumNpc::FLocalMoveGoal Goal;
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
	// `0xd00`. `CapabilitiesGet()` answers 0 on an untouched body, so the arm is open. The state is
	// pushed through the one public writer this leaf has — an `aiscripted_schedule`'s `forcestate`,
	// which is `Mind.RequestState` and admits `Scripted`.
	F.Npc->OpeningDoor = FElysiumEntityHandle();
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
	TestEqual(TEXT("CapabilitiesGet() answers 0 on an untouched body, which closes arms 3-5"),
		F.Npc->CapabilitiesGet(), 0);

	// The pathfinder and the splice are seams: `0x10304130` answers NOT FOUND, which is the arm
	// that reaches the door-type split.
	F.Npc->Motor10Seams.BuildLocalRouteAsks = 0;
	TestFalse(TEXT("CAI_Pathfinder::BuildLocalRoute (0x10304130) answers no waypoint"),
		F.Npc->BuildLocalRouteThroughDoor(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 0x30));
	TestEqual(TEXT("...and the ask is recorded"), F.Npc->Motor10Seams.BuildLocalRouteAsks, 1);
	TestFalse(TEXT("the path splice (0x10319f30) answers false"), F.Npc->SplicePathWaypoint(1));

	// The door's toggle state seam answers 0 (`TS_AT_TOP`), which is one of the two states arm 7
	// gives up quietly on, and which also makes `m_bOpeningDoorWait` false.
	TestEqual(TEXT("the door toggle-state (+0x4f8) seam answers 0"),
		F.Npc->RetailDoorToggleState(*F.Door), 0);

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
// `0x102c79e0` — `CAI_StandoffBehavior#22`, the activity translation.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotor10StandoffActivityTest,
	"Elysium.Substrate.NpcKernelMotor10.StandoffTranslateActivity", GElysiumNpcKernelMotor10Flags)
bool FElysiumNpcKernelMotor10StandoffActivityTest::RunTest(const FString&)
{
	FMotor10Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	FElysiumNpc::FStandoffWords Words;

	// Posture 0 with no hint: nothing matches, so the body falls through to `CAI_Behavior::vfunc22`
	// — the base this runtime does not carry, spelled `INDEX_NONE`.
	Words.Posture = 0;
	TestEqual(TEXT("0x102c79e0 at posture 0 falls through to the base"),
		F.Npc->StandoffTranslateActivity(Words, 1), INDEX_NONE);

	// Posture 2 turns ACT_IDLE (1) into 8.
	Words.Posture = 2;
	TestEqual(TEXT("posture 2 turns activity 1 into 8"),
		F.Npc->StandoffTranslateActivity(Words, 1), 8);

	// Posture 2 turns 9 into 0x12 ONLY when `SelectHeaviestSequence(0x12, -1)` is non-negative.
	// That is family TroikaHelpers' seam and answers `INDEX_NONE`, so the arm refuses and the body
	// falls through — retail's own answer for a body with no such sequence.
	TestEqual(TEXT("SelectHeaviestSequence is a seam and answers -1"),
		F.Npc->SelectHeaviestSequence(0x12, INDEX_NONE), INDEX_NONE);
	TestEqual(TEXT("...so posture 2 with activity 9 falls through rather than answering 0x12"),
		F.Npc->StandoffTranslateActivity(Words, 9), INDEX_NONE);
	// Any other activity at posture 2 also falls through.
	TestEqual(TEXT("posture 2 with an unrelated activity falls through"),
		F.Npc->StandoffTranslateActivity(Words, 0x40), INDEX_NONE);

	// Posture 1 turns 8 into 1, and ONLY 8.
	Words.Posture = 1;
	TestEqual(TEXT("posture 1 turns activity 8 into 1"),
		F.Npc->StandoffTranslateActivity(Words, 8), 1);
	TestEqual(TEXT("...and leaves activity 1 to the base"),
		F.Npc->StandoffTranslateActivity(Words, 1), INDEX_NONE);

	// Posture 3, activity 8 or 1: SMG first, pistol second, the log last. `Weapon_OwnsThisType` is
	// a seam answering false, so both weapon arms are skipped and the body answers **1** after
	// logging "NPC in standoff lacks needed low aim activity (%s)" — the literal at `0x10601e9c`,
	// which the checklist's walk reads as "low cover animation".
	Words.Posture = 3;
	F.Npc->Motor10Seams.WeaponOwnsAsks = 0;
	F.Npc->Motor10Seams.LowAimWarnings = 0;
	TestEqual(TEXT("posture 3 with activity 8 answers 1 after the low-aim warning"),
		F.Npc->StandoffTranslateActivity(Words, 8), 1);
	TestEqual(TEXT("...having asked for both weapons, in order"),
		F.Npc->Motor10Seams.WeaponOwnsAsks, 2);
	TestEqual(TEXT("...and logged once"), F.Npc->Motor10Seams.LowAimWarnings, 1);
	TestEqual(TEXT("posture 3 with activity 1 takes the same arm"),
		F.Npc->StandoffTranslateActivity(Words, 1), 1);
	TestEqual(TEXT("...and posture 3 with anything else falls through"),
		F.Npc->StandoffTranslateActivity(Words, 9), INDEX_NONE);

	// The two activity ids are runtime-registered in uninitialised `.data` and stay unrecovered.
	TestEqual(TEXT("DAT_10925390 is unrecovered"), FElysiumNpc::StandoffLowAimActivitySmg(),
		INDEX_NONE);
	TestEqual(TEXT("DAT_10925388 is unrecovered"), FElysiumNpc::StandoffLowAimActivityPistol(),
		INDEX_NONE);

	// The hint arm: `m_pHintNode` must exist AND its `m_nHintType` be `0x65`. Family Debug10's
	// `HintOverlayWords` is the type reader and answers false, so the arm is skipped and slot 569
	// is never asked — which also means posture 0 is never promoted to 2 here.
	Words.Posture = 0;
	F.Npc->ScheduleHost.HintNode = 7;
	F.Npc->Motor10Seams.HintActivityAsks = 0;
	TestEqual(TEXT("a claimed hint with no readable type leaves the activity alone"),
		F.Npc->StandoffTranslateActivity(Words, 1), INDEX_NONE);
	TestEqual(TEXT("...and slot 569 is never asked"), F.Npc->Motor10Seams.HintActivityAsks, 0);
	TestEqual(TEXT("...and the posture is not promoted"), Words.Posture, 0);
	F.Npc->ScheduleHost.HintNode = INDEX_NONE;
	return true;
}

// =================================================================================================
// `0x102c87a0` / `0x102c8830` / `0x102cdc50` — `CAI_StandoffGoal`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotor10StandoffActivateTest,
	"Elysium.Substrate.NpcKernelMotor10.StandoffInputActivate", GElysiumNpcKernelMotor10Flags)
bool FElysiumNpcKernelMotor10StandoffActivateTest::RunTest(const FString&)
{
	// The clamp: in range, nothing happens.
	FElysiumNpc::FStandoffGoalWords Goal;
	Goal.Aggressiveness = 3;
	Goal.ActorCount = 2;
	FElysiumNpc::StandoffInputActivate(Goal);
	TestEqual(TEXT("0x102c87a0 leaves an in-range aggressiveness alone"), Goal.Aggressiveness, 3);
	TestEqual(TEXT("...and warns about nothing"), Goal.InvalidWarnings, 0);
	// `0x102cd650` ran exactly once: the goal is on the list, active, and the actors resolved.
	TestTrue(TEXT("...and the goal is on the global list (0x100f6d80 over DAT_106eb5d8)"),
		Goal.bOnGoalList);
	TestEqual(TEXT("...with the ACTIVE bit 0x1 set"), static_cast<int32>(Goal.Flags & 0x1), 1);
	TestEqual(TEXT("...and bit 0x2, the actors-resolved latch"),
		static_cast<int32>(Goal.Flags & 0x2), 2);
	TestEqual(TEXT("...resolved once through 0x102cd4a0"), Goal.ActorResolves, 1);
	TestEqual(TEXT("...refreshed never"), Goal.ActorRefreshes, 0);
	TestEqual(TEXT("...and EnableGoal (vtable +0x3d0) per actor"), Goal.EnableGoalCalls, 2);

	// A SECOND activate on an already-active goal does **nothing at all** — no list insert, no
	// actor pass. The clamp still runs in front of it.
	FElysiumNpc::StandoffInputActivate(Goal);
	TestEqual(TEXT("a second activate resolves nothing"), Goal.ActorResolves, 1);
	TestEqual(TEXT("...refreshes nothing"), Goal.ActorRefreshes, 0);
	TestEqual(TEXT("...and enables nothing"), Goal.EnableGoalCalls, 2);

	// The sentinel 5 is EXEMPT from the warning and from the clamp.
	FElysiumNpc::FStandoffGoalWords Sentinel;
	Sentinel.Aggressiveness = 5;
	FElysiumNpc::StandoffInputActivate(Sentinel);
	TestEqual(TEXT("5 is a sentinel, not an invalid value"), Sentinel.Aggressiveness, 5);
	TestEqual(TEXT("...and warns about nothing"), Sentinel.InvalidWarnings, 0);

	// Negative clamps to 0, and the warning carries the PRE-clamp value.
	FElysiumNpc::FStandoffGoalWords Low;
	Low.Aggressiveness = -3;
	FElysiumNpc::StandoffInputActivate(Low);
	TestEqual(TEXT("a negative aggressiveness clamps to 0"), Low.Aggressiveness, 0);
	TestEqual(TEXT("...with one warning"), Low.InvalidWarnings, 1);
	TestEqual(TEXT("...naming the pre-clamp value"), Low.LastInvalidValue, -3);
	TestTrue(TEXT("...and 0x102cd650 still ran exactly once"), Low.bOnGoalList);

	// Over 4 clamps to 4. 6 is over the sentinel and is NOT exempt.
	FElysiumNpc::FStandoffGoalWords High;
	High.Aggressiveness = 6;
	FElysiumNpc::StandoffInputActivate(High);
	TestEqual(TEXT("6 clamps to 4"), High.Aggressiveness, 4);
	TestEqual(TEXT("...with one warning naming 6"), High.LastInvalidValue, 6);

	// The boundaries are inclusive: 0 and 4 are both valid.
	FElysiumNpc::FStandoffGoalWords Edge;
	Edge.Aggressiveness = 0;
	FElysiumNpc::StandoffInputActivate(Edge);
	TestEqual(TEXT("0 is valid"), Edge.InvalidWarnings, 0);
	FElysiumNpc::FStandoffGoalWords Edge4;
	Edge4.Aggressiveness = 4;
	FElysiumNpc::StandoffInputActivate(Edge4);
	TestEqual(TEXT("4 is valid"), Edge4.InvalidWarnings, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotor10StandoffDeactivateTest,
	"Elysium.Substrate.NpcKernelMotor10.StandoffInputDeactivate", GElysiumNpcKernelMotor10Flags)
bool FElysiumNpcKernelMotor10StandoffDeactivateTest::RunTest(const FString&)
{
	// An INACTIVE goal: `0x102cdb70` does nothing at all, and the clamp in front of it still runs.
	FElysiumNpc::FStandoffGoalWords Goal;
	Goal.Aggressiveness = -1;
	Goal.ActorCount = 3;
	FElysiumNpc::StandoffInputDeactivate(Goal);
	TestEqual(TEXT("0x102c8830 runs the SAME clamp as slot 241"), Goal.Aggressiveness, 0);
	TestEqual(TEXT("...with one warning"), Goal.InvalidWarnings, 1);
	TestEqual(TEXT("...but an inactive goal resolves nothing"), Goal.ActorResolves, 0);
	TestEqual(TEXT("...and disables nothing"), Goal.DisableGoalCalls, 0);

	// Activate, then deactivate: the exact inverse, and the actor pass REFRESHES rather than
	// resolves because bit 0x2 is already latched.
	FElysiumNpc::FStandoffGoalWords Live;
	Live.Aggressiveness = 2;
	Live.ActorCount = 3;
	FElysiumNpc::StandoffInputActivate(Live);
	FElysiumNpc::StandoffInputDeactivate(Live);
	TestEqual(TEXT("the ACTIVE bit is cleared"), static_cast<int32>(Live.Flags & 0x1), 0);
	TestTrue(TEXT("...the actors-resolved latch survives"), (Live.Flags & 0x2) != 0);
	TestEqual(TEXT("...the second pass REFRESHED (0x102cd3b0) rather than resolved"),
		Live.ActorRefreshes, 1);
	TestEqual(TEXT("...resolving stayed at one"), Live.ActorResolves, 1);
	TestEqual(TEXT("...DisableGoal (vtable +0x3d4) ran per actor"), Live.DisableGoalCalls, 3);
	TestFalse(TEXT("...and the goal left the list (0x100f6e40), LAST"), Live.bOnGoalList);

	// The clamp is shared byte-for-byte, so the same edges hold.
	FElysiumNpc::FStandoffGoalWords Sentinel;
	Sentinel.Aggressiveness = 5;
	FElysiumNpc::StandoffInputDeactivate(Sentinel);
	TestEqual(TEXT("the sentinel 5 is exempt here too"), Sentinel.Aggressiveness, 5);
	TestEqual(TEXT("...and warns about nothing"), Sentinel.InvalidWarnings, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotor10StandoffUpdateOnRemoveTest,
	"Elysium.Substrate.NpcKernelMotor10.StandoffGoalUpdateOnRemove", GElysiumNpcKernelMotor10Flags)
bool FElysiumNpcKernelMotor10StandoffUpdateOnRemoveTest::RunTest(const FString&)
{
	// Bit 0 of `m_flags` (+0x480) CLEAR: the release is skipped entirely and only the tail runs.
	FElysiumNpc::FStandoffGoalWords Idle;
	Idle.ActorCount = 2;
	FElysiumNpc::StandoffGoalUpdateOnRemove(Idle);
	TestEqual(TEXT("0x102cdc50 with the ACTIVE bit clear issues no release"), Idle.ReleaseInputs, 0);
	TestEqual(TEXT("...and disables nobody"), Idle.DisableGoalCalls, 0);
	TestTrue(TEXT("...but CBaseEntity::UpdateOnRemove still runs"), Idle.bUpdateOnRemoveTailRan);

	// Bit 0 SET: the goal's own slot 243 is dispatched with the stack `inputdata_t`, and the tail
	// runs after it.
	FElysiumNpc::FStandoffGoalWords Live;
	Live.Aggressiveness = 1;
	Live.ActorCount = 2;
	FElysiumNpc::StandoffInputActivate(Live);
	FElysiumNpc::StandoffGoalUpdateOnRemove(Live);
	TestEqual(TEXT("an ACTIVE goal issues exactly one release"), Live.ReleaseInputs, 1);
	TestEqual(TEXT("...which is slot 243, so the actors are disabled"), Live.DisableGoalCalls, 2);
	TestEqual(TEXT("...and the ACTIVE bit is cleared"), static_cast<int32>(Live.Flags & 0x1), 0);
	TestFalse(TEXT("...and the goal left the list"), Live.bOnGoalList);
	TestTrue(TEXT("...and the tail still ran"), Live.bUpdateOnRemoveTailRan);

	// The release goes through slot 243, which carries the aggressiveness clamp — so a goal with an
	// out-of-range aggressiveness warns on the way out too.
	FElysiumNpc::FStandoffGoalWords Bad;
	Bad.Aggressiveness = 9;
	FElysiumNpc::StandoffInputActivate(Bad);
	Bad.InvalidWarnings = 0;
	Bad.Aggressiveness = -2;
	FElysiumNpc::StandoffGoalUpdateOnRemove(Bad);
	TestEqual(TEXT("the release runs slot 243's clamp"), Bad.Aggressiveness, 0);
	TestEqual(TEXT("...and warns"), Bad.InvalidWarnings, 1);
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
		FElysiumNpc::TestHullPickHull(0, [](int32) { return 0xff; }, bFallback), 0);
	TestFalse(TEXT("...without taking the 22-miss fallback"), bFallback);

	// The test is SIGNED, so the shipped `.data` initialiser `0xffffffff` short-circuits the same
	// way. That is the correction this family made to the walk.
	TestEqual(TEXT("a NEGATIVE mask (the shipped 0xffffffff) also answers hull 0 at once"),
		FElysiumNpc::TestHullPickHull(-1, [](int32 Hull) { return Hull == 5 ? 0x20 : 0; }, bFallback),
		0);
	TestFalse(TEXT("...and still skips the fallback"), bFallback);

	// A positive mask walks 0..21 and takes the FIRST intersection.
	TestEqual(TEXT("the first hull whose bits intersect the mask wins"),
		FElysiumNpc::TestHullPickHull(0x28, [](int32 Hull) { return 1 << Hull; }, bFallback), 3);
	TestFalse(TEXT("...no fallback"), bFallback);
	TestEqual(TEXT("a later-only bit still wins when it is the only one"),
		FElysiumNpc::TestHullPickHull(0x20, [](int32 Hull) { return 1 << Hull; }, bFallback), 5);

	// The walk stops at 22 — hull 22's bit is never reached.
	TestEqual(TEXT("22 misses fall back to hull 0"),
		FElysiumNpc::TestHullPickHull(1 << 22, [](int32 Hull) { return 1 << Hull; }, bFallback), 0);
	TestTrue(TEXT("...through the fallback, which ORs 0 (a no-op)"), bFallback);
	TestEqual(TEXT("...and the no-op OR left the mask alone"), FElysiumNpc::RetailUsedHullBits(), 0);

	// Hull 21 is the LAST index the walk reaches.
	TestEqual(TEXT("hull 21 is inside the walk"),
		FElysiumNpc::TestHullPickHull(1 << 21, [](int32 Hull) { return 1 << Hull; }, bFallback), 21);

	// The two mask writers.
	FElysiumNpc::RetailClearUsedHullBits();
	TestEqual(TEXT("0x102f9900 clears the mask"), FElysiumNpc::RetailUsedHullBits(), 0);
	FElysiumNpc::RetailAddUsedHullBits(0x6);
	FElysiumNpc::RetailAddUsedHullBits(0x8);
	TestEqual(TEXT("0x102f9920 ORs bits in"), FElysiumNpc::RetailUsedHullBits(), 0xe);
	FElysiumNpc::RetailClearUsedHullBits();

	// The whole body.
	FMotor10Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	F.Npc->HullKind = 9;
	F.Npc->Health = 1;
	F.Npc->Flags = 0;
	F.Npc->RetailSolidFlags = 0;
	F.Npc->RetailSolidType = 0;
	F.Npc->RetailSolidSets = 0;
	F.Npc->SetSizeCalls = 0;
	F.Npc->bTestHullByte5f44 = true;
	F.Npc->bIsUsingSmallHull = true;

	ElysiumStub::ClearTally();
	ON_SCOPE_EXIT { ElysiumStub::ClearTally(); };
	F.Npc->TestHullSpawn();

	TestEqual(TEXT("0x102d72f0 picks hull 0 through the seam"), F.Npc->HullKind, 0);
	TestEqual(TEXT("...and resizes the bounds through 0x10273070"), F.Npc->SetSizeCalls, 1);
	TestFalse(TEXT("...which cleared m_fIsUsingSmallHull"), F.Npc->bIsUsingSmallHull);
	TestEqual(TEXT("SetSolid(SOLID_BBOX = 2)"), F.Npc->RetailSolidType, 2);
	TestEqual(TEXT("...once"), F.Npc->RetailSolidSets, 1);
	TestEqual(TEXT("AddSolidFlags ORs FSOLID_NOT_SOLID (0x4)"),
		static_cast<int32>(F.Npc->RetailSolidFlags & 0x4), 0x4);
	TestEqual(TEXT("slot 93 SetMoveType(MOVETYPE_FLY = 4, ...)"), F.Npc->RetailMoveType, 4);
	TestEqual(TEXT("...with MOVECOLLIDE_DEFAULT = 0"), F.Npc->RetailMoveCollide, 0);
	TestEqual(TEXT("m_iHealth (+0x210) = 0x32 = 50"), F.Npc->Health, 50);
	TestEqual(TEXT("AddFlag(0x40000)"), F.Npc->Flags & 0x40000, 0x40000);
	TestFalse(TEXT("byte [+0x5f44] = 0"), F.Npc->bTestHullByte5f44);

	// The tail is a JMP to slot 66 `Hide()`. Slot 66 is still one of story 29c's generated stubs, so
	// what is observable is the stub tally — which is exactly the claim: the tail dispatched.
	TArray<ElysiumStub::FTally> Tally;
	ElysiumStub::CollectTally(Tally);
	const ElysiumStub::FTally* HideRow = Tally.FindByPredicate(
		[](const ElysiumStub::FTally& Row)
		{
			return Row.Kind == TEXT("slot") && Row.Surface == TEXT("CAI_BaseNPCTroika::Hide");
		});
	if (TestNotNull(TEXT("the tail JMP to slot 66 Hide() dispatched"), HideRow))
	{
		TestEqual(TEXT("...exactly once"), HideRow->Count, 1);
	}
	return true;
}

// =================================================================================================
// `0x102e14a0` — `CAI_Motor#19 MoveGroundExecute` (and `0x102e1560`).
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotor10MoveGroundExecuteTest,
	"Elysium.Substrate.NpcKernelMotor10.MoveGroundExecute", GElysiumNpcKernelMotor10Flags)
bool FElysiumNpcKernelMotor10MoveGroundExecuteTest::RunTest(const FString&)
{
	FMotor10Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	FElysiumNpc::FLocalMoveGoal Goal;
	Goal.DirUnits = FVector(1.0, 0.0, 0.0);
	Goal.MaxDistanceUnits = 100.f;

	// `GetCurSpeed` `0x102e12c0` is a tail jump to the owner's slot 248 `GetIdealSpeed`, so the two
	// answer the same number.
	TestEqual(TEXT("0x102e12c0 is the owner's slot 248 GetIdealSpeed"), F.Npc->MotorCurSpeed(),
		F.Npc->GetIdealSpeed());

	// `step = (|m_vecVelocity| + speed) * m_flMoveInterval * 0.5`. With the velocity at (3,4,0) the
	// magnitude is 5, and with `GetIdealSpeed` answering 0 on an untouched body the step is
	// `5 * interval * 0.5`.
	F.Npc->MotorVelocityUnits = FVector(3.0, 4.0, 0.0);
	F.Npc->TroikaMotor.MoveInterval = 2.f;
	F.Npc->Motor10Seams.StepToPointCalls = 0;
	F.Npc->Motor10Seams.LocalNavigatorAsks = 0;
	F.Npc->Motor10Seams.MoveExecuteFailures = 0;

	const float Speed = F.Npc->GetIdealSpeed();
	const float ExpectedStep = static_cast<float>((5.0 + Speed) * 2.0 * 0.5);
	const FVector OriginUnits = Motor10SourceUnits(F.Npc->Origin);

	const int32 Answer = F.Npc->MoveGroundExecute(Goal, nullptr, nullptr);

	// The step (5.0 with a zero ideal speed) is BELOW the goal's 100.0, so `0x102e1560` zeroes the
	// whole interval and leaves the step alone.
	TestEqual(TEXT("0x102e1560 zeroes m_flMoveInterval when the step fits"),
		F.Npc->TroikaMotor.MoveInterval, 0.f);
	// `m_vecVelocity = goal.dir * speed`.
	TestEqual(TEXT("m_vecVelocity (+0x3c) = dir * speed"), F.Npc->MotorVelocityUnits,
		FVector(Speed * 1.0, 0.0, 0.0));

	if (ExpectedStep > 0.f)
	{
		TestEqual(TEXT("the step went through 0x102e0bd0"), F.Npc->Motor10Seams.StepToPointCalls, 1);
		TestTrue(TEXT("...to origin + step * dir"),
			F.Npc->Motor10Seams.LastStepEndUnits.Equals(
				OriginUnits + ExpectedStep * Goal.DirUnits, 1e-3));
		// The seam answers 0, which is retail's refusal: motor slot 10 runs and the body answers 0.
		TestEqual(TEXT("the refused step dispatches motor slot 10 (+0x28)"),
			F.Npc->Motor10Seams.MoveExecuteFailures, 1);
		TestEqual(TEXT("...and 0x102e14a0 returns 0, not void"), Answer, 0);
	}
	else
	{
		// A zero step is at or below `_DAT_1044fab0` (0.0), so the LOCAL NAVIGATOR answers instead.
		TestEqual(TEXT("a zero step asks the local navigator (motor+0x10 slot 6)"),
			F.Npc->Motor10Seams.LocalNavigatorAsks, 1);
		TestEqual(TEXT("...which refuses, so the answer is 0"), Answer, 0);
	}

	// The epsilon arm, driven directly: a step of exactly 0.0 is NOT above `_DAT_1044fab0`.
	FElysiumNpc::FLocalMoveGoal Near;
	Near.DirUnits = FVector(1.0, 0.0, 0.0);
	Near.MaxDistanceUnits = 10.f;
	F.Npc->Motor10Seams.LocalNavigatorAsks = 0;
	F.Npc->Motor10Seams.StepToPointCalls = 0;
	TestEqual(TEXT("0x102e1560 with a zero step asks the local navigator"),
		F.Npc->MotorMoveGroundExecuteWalk(Near, 1.f, 0.f, nullptr, nullptr), 0);
	TestEqual(TEXT("...exactly once"), F.Npc->Motor10Seams.LocalNavigatorAsks, 1);
	TestEqual(TEXT("...and never steps"), F.Npc->Motor10Seams.StepToPointCalls, 0);

	// The OVERSHOOT arm, without the goal flag `0x2`: the interval is scaled by
	// `1.0 - maxDist/step` and the step is clamped to `maxDist`.
	F.Npc->TroikaMotor.MoveInterval = 4.f;
	FElysiumNpc::FLocalMoveGoal Over;
	Over.DirUnits = FVector(1.0, 0.0, 0.0);
	Over.MaxDistanceUnits = 10.f;
	F.Npc->Motor10Seams.LastStepEndUnits = FVector::ZeroVector;
	F.Npc->MotorMoveGroundExecuteWalk(Over, 1.f, 40.f, nullptr, nullptr);
	TestEqual(TEXT("an overshoot scales the interval by (1.0 - maxDist/step)"),
		F.Npc->TroikaMotor.MoveInterval, static_cast<float>((1.0 - 10.0 / 40.0) * 4.0), 1e-4f);
	TestTrue(TEXT("...and the step is clamped to maxDist"),
		F.Npc->Motor10Seams.LastStepEndUnits.Equals(
			Motor10SourceUnits(F.Npc->Origin) + 10.0 * Over.DirUnits, 1e-3));

	// With the goal flag `0x2` the interval is ZEROED instead of scaled, and the step is still
	// clamped.
	F.Npc->TroikaMotor.MoveInterval = 4.f;
	Over.Flags = 0x2;
	F.Npc->MotorMoveGroundExecuteWalk(Over, 1.f, 40.f, nullptr, nullptr);
	TestEqual(TEXT("goal flag 0x2 zeroes the interval outright"), F.Npc->TroikaMotor.MoveInterval,
		0.f);
	return true;
}

// =================================================================================================
// `0x102e1760` — `CAI_Motor#20 MoveGroundStep`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotor10MoveGroundStepTest,
	"Elysium.Substrate.NpcKernelMotor10.MotorMoveGroundStep", GElysiumNpcKernelMotor10Flags)
bool FElysiumNpcKernelMotor10MoveGroundStepTest::RunTest(const FString&)
{
	FMotor10Fixture F;
	if (F.Npc == nullptr || F.Door == nullptr)
	{
		AddError(TEXT("no NPC or no door"));
		return false;
	}

	FElysiumNpc::FLocalMoveGoal Goal;
	Goal.DirUnits = FVector(0.0, 1.0, 0.0);
	Goal.MaxDistanceUnits = 3.f;
	F.Npc->MotorVelocityUnits = FVector(0.0, 6.0, 0.0);
	F.Npc->TroikaMotor.MoveInterval = 2.f;
	F.Npc->Motor10Seams = FElysiumNpc::FMotor10SeamLedger();

	const float Speed = F.Npc->GetIdealSpeed();
	// `m_vecVelocity` is rewritten to `dir * speed` BEFORE the magnitude is taken, so the
	// magnitude is `|dir| * speed`, not the 6.0 the case seeded.
	const double Magnitude = FMath::Abs(static_cast<double>(Speed));
	const float RawStep = static_cast<float>((Magnitude + Speed) * 2.0 * 0.5);
	const FVector OriginUnits = Motor10SourceUnits(F.Npc->Origin);

	FElysiumNpc::FMotorMoveTrace Out;
	Out.Status = 0x7f;
	const int32 Answer = F.Npc->MotorMoveGroundStep(Goal, &Out, nullptr);

	TestEqual(TEXT("m_vecVelocity (+0x3c) = goal dir * GetCurSpeed"), F.Npc->MotorVelocityUnits,
		FVector(0.0, Speed * 1.0, 0.0));

	// The sweep's constants, all four read off the listing.
	TestEqual(TEXT("one hull sweep through 0x102e6d70"), F.Npc->Motor10Seams.MoveTraceSweeps, 1);
	TestEqual(TEXT("...with trace kind 2"), F.Npc->Motor10Seams.LastMoveTraceKind, 2);
	TestEqual(TEXT("...mask 0x202400b"), F.Npc->Motor10Seams.LastMoveTraceMask, 0x202400b);
	TestEqual(TEXT("...and extent 100.0 (0x42c80000)"), F.Npc->Motor10Seams.LastMoveTraceExtent,
		100.0f);

	// `step <= maxDist` zeroes the interval and leaves the step; the step here is `speed * interval`
	// which is 0 on an untouched body, so it fits inside 3.0.
	if (RawStep <= Goal.MaxDistanceUnits)
	{
		TestEqual(TEXT("a step inside maxDist zeroes m_flMoveInterval"),
			F.Npc->TroikaMotor.MoveInterval, 0.f);
	}

	// The record IS copied into the SECOND argument, and the move goal is never written back.
	TestEqual(TEXT("the trace record is copied into the caller's AIMoveTrace_t"), Out.Status, 0);
	TestEqual(TEXT("...with vEndPosition seeded to the START (0x102e6d70's own init)"),
		Out.EndPositionUnits, OriginUnits);
	TestEqual(TEXT("...and the move goal's maxDist is untouched"), Goal.MaxDistanceUnits, 3.f);

	// `flTotalDist` is 0 and the step is 0, so `|0 - 0| <= 0.1` holds — and `return 4` ALSO needs
	// the goal's expected blocker to be NON-ZERO. It is null, so the answer is **0**.
	TestEqual(TEXT("a null expected blocker (+0x34) answers 0, not 4"), Answer, 0);

	// With a blocker set but the trace reporting a different one (null), the answer is still 0.
	Goal.ExpectedBlocker = F.Door;
	F.Npc->TroikaMotor.MoveInterval = 0.f;
	TestEqual(TEXT("a blocker the trace did not report also answers 0"),
		F.Npc->MotorMoveGroundStep(Goal, nullptr, nullptr), 0);

	// A NULL out param is allowed and nothing is copied — the copy block is inside `TEST EDI,EDI`.
	TestEqual(TEXT("a null out param is allowed"),
		F.Npc->MotorMoveGroundStep(Goal, nullptr, nullptr), 0);

	// The LAST arm: force the distance mismatch by giving the goal a maxDist smaller than the step,
	// so the step is clamped to a non-zero length while the trace still reports 0 travelled.
	F.Npc->Motor10Seams = FElysiumNpc::FMotor10SeamLedger();
	F.Npc->MotorVelocityUnits = FVector(0.0, 0.0, 0.0);
	F.Npc->TroikaMotor.MoveInterval = 0.f;
	FElysiumNpc::FLocalMoveGoal Far;
	Far.DirUnits = FVector(1.0, 0.0, 0.0);
	Far.MaxDistanceUnits = -5.f;    // any step > maxDist takes the clamp; 0 > -5
	const FVector Before = Motor10SourceUnits(F.Npc->Origin);
	const int32 Moved = F.Npc->MotorMoveGroundStep(Far, nullptr, nullptr);
	// step becomes -5, |0 - (-5)| = 5 > 0.1, so the body takes `UTIL_SetOrigin` and
	// `1 + 2 * (status < 0)` with status 0 — **1**.
	TestEqual(TEXT("a step the trace did not travel answers 1 + 2*(status < 0) = 1"), Moved, 1);
	TestEqual(TEXT("...through UTIL_SetOrigin (0x101cf5c0), NOT NDebugOverlay::Line"),
		F.Npc->Motor10Seams.SetOriginCalls, 1);
	TestTrue(TEXT("...to the trace's end position, which the seam seeded to the start"),
		F.Npc->Motor10Seams.LastSetOriginUnits.Equals(Before, 1e-3));
	TestTrue(TEXT("...and the body really moved there"),
		Motor10SourceUnits(F.Npc->Origin).Equals(Before, 1e-3));
	return true;
}

// =================================================================================================
// `0x102efaa0` — `CAI_Navigator#12 MoveNormal`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotor10NavigatorMoveNormalTest,
	"Elysium.Substrate.NpcKernelMotor10.NavigatorMoveNormal", GElysiumNpcKernelMotor10Flags)
bool FElysiumNpcKernelMotor10NavigatorMoveNormalTest::RunTest(const FString&)
{
	FMotor10Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// The gate, `0x102efd50`. The route nav type is a seam answering 0 (`NAV_GROUND`), so a nav type
	// of 0 passes straight through with no warning and no teardown.
	F.Npc->Motor10Seams = FElysiumNpc::FMotor10SeamLedger();
	F.Npc->NavSetType(0);
	F.Npc->bNavigatorByte51 = true;
	TestTrue(TEXT("0x102efd50 passes for a ground route under a ground nav type"),
		F.Npc->NavigatorMoveGate());
	TestEqual(TEXT("...warning about nothing"), F.Npc->Motor10Seams.NavGateWrongTypeWarnings, 0);
	TestEqual(TEXT("...calling 0x102f13d0 once"), F.Npc->Motor10Seams.NavGatePasses, 1);
	TestFalse(TEXT("...and clearing navigator+0x51"), F.Npc->bNavigatorByte51);

	// A JUMP nav type (1) under a ground route warns, runs the jump teardown and resets the type.
	F.Npc->Motor10Seams = FElysiumNpc::FMotor10SeamLedger();
	F.Npc->NavSetType(1);
	TestTrue(TEXT("a jump nav type under a ground route still passes"), F.Npc->NavigatorMoveGate());
	TestEqual(TEXT("...after one warning"), F.Npc->Motor10Seams.NavGateWrongTypeWarnings, 1);
	TestEqual(TEXT("...and the jump teardown (nav+0x20 slot 8)"),
		F.Npc->Motor10Seams.NavGateJumpTeardowns, 1);
	TestEqual(TEXT("...and NO climb teardown"), F.Npc->Motor10Seams.NavGateClimbTeardowns, 0);
	TestEqual(TEXT("...and the nav type is reset to 0"), F.Npc->NavGetType(), 0);

	// A CLIMB nav type (3) takes the other teardown.
	F.Npc->Motor10Seams = FElysiumNpc::FMotor10SeamLedger();
	F.Npc->NavSetType(3);
	TestTrue(TEXT("a climb nav type passes too"), F.Npc->NavigatorMoveGate());
	TestEqual(TEXT("...through the climb teardown (nav+0x20 slot 5)"),
		F.Npc->Motor10Seams.NavGateClimbTeardowns, 1);
	TestEqual(TEXT("...and not the jump one"), F.Npc->Motor10Seams.NavGateJumpTeardowns, 0);

	// Nav type 2 (FLY) under a ground route warns but runs NEITHER teardown — the two `if`s name 1
	// and 3 only.
	F.Npc->Motor10Seams = FElysiumNpc::FMotor10SeamLedger();
	F.Npc->NavSetType(2);
	TestTrue(TEXT("a fly nav type under a ground route passes"), F.Npc->NavigatorMoveGate());
	TestEqual(TEXT("...with a warning"), F.Npc->Motor10Seams.NavGateWrongTypeWarnings, 1);
	TestEqual(TEXT("...and neither teardown"),
		F.Npc->Motor10Seams.NavGateJumpTeardowns + F.Npc->Motor10Seams.NavGateClimbTeardowns, 0);

	// The whole body. With the gate passing, the override refusing and the enact answering
	// `AIMR_OK`, the answer is 0 and the tail runs.
	F.Npc->NavSetType(0);
	F.Npc->Motor10Seams = FElysiumNpc::FMotor10SeamLedger();
	F.Npc->bNavigatorByte51 = false;
	const int32 SavedSequence = F.Npc->SequenceNumber;
	const int32 Answer = F.Npc->NavigatorMoveNormal(0x1234);

	TestEqual(TEXT("0x102efaa0 answers an AIMoveResult_t, and AIMR_OK here"), Answer, 0);
	TestEqual(TEXT("...having asked the override (navigator slot 16) once"),
		F.Npc->Motor10Seams.NavMoveOverrideAsks, 1);
	TestEqual(TEXT("...enacted once (navigator slot 15)"), F.Npc->Motor10Seams.NavEnactCalls, 1);
	TestEqual(TEXT("...forwarding this body's own stack word"),
		F.Npc->Motor10Seams.LastNavEnactArgument, 0x1234);
	TestEqual(TEXT("...and run the tail (navigator slot 6) with +0x51 clear"),
		F.Npc->Motor10Seams.NavMoveTails, 1);
	TestEqual(TEXT("the restore put m_nSequence (+0x6f0) back"), F.Npc->SequenceNumber,
		SavedSequence);

	// `navigator+0x51` SET suppresses the tail.
	F.Npc->Motor10Seams = FElysiumNpc::FMotor10SeamLedger();
	F.Npc->bNavigatorByte51 = true;
	// The gate clears the byte on its way through, so the suppression is only observable when the
	// gate itself is bypassed — which is the OVERRIDE arm.
	F.Npc->NavigatorMoveNormal(0);
	TestFalse(TEXT("the gate always clears navigator+0x51 on its way through"),
		F.Npc->bNavigatorByte51);

	// The gate's own refusal value is **-4 AIMR_ILLEGAL**, not a distance and not -NAN. It is only
	// reachable for a FLY route under a non-fly nav type, and `RouteNavType` is a seam answering 0,
	// so the arm is unreachable here; the constant is asserted where it is produced.
	TestEqual(TEXT("the route nav-type seam answers 0 (NAV_GROUND), so the gate never refuses"),
		F.Npc->RouteNavType(), 0);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
