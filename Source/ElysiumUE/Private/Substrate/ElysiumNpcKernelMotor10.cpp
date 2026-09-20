// Story 29d, family **Motor10** — `CAI_Motor`, `CAI_Navigator`, the standoff behaviour and goal,
// the hull probe, the obstructing-door body and the shoot target. Twelve rows of
// `checklist-10-18.md`, layers 10–18, ported arm by arm in retail's order.
//
// Every threshold below was read out of the pinned retail `vampire.dll` at its cited address (image
// base `0x10000000`), so the numbers are recovered facts. The walked prose is
// `docs/vtmb/npc-ai/schedule-kernel.md` § "Story 29d, family Motor10".
//
// Two conventions govern the whole file and are stated once.
//
//   * **Units.** Every retail constant here is in SOURCE units, and every position a retail body
//     reads is `GetAbsOrigin()` in the same. This world's `Origin` is Unreal centimetres with Y
//     negated (`bsp.source_to_unreal`). `SourceOf` / `PortOf` below are the one place that
//     conversion happens; every body works in retail's own numbers so a threshold stays checkable.
//     This mirrors `ElysiumNpcKernelMotor.cpp`'s own convention exactly — two copies of a two-line
//     helper rather than a cross-family dependency on a file-local function.
//   * **The seams.** There is no navigator, no node graph, no move probe, no hull table, no path
//     object, no goal entity and no behaviour object in this substrate. Each such input is asked
//     through a named accessor that answers NOTHING and cites the retail call, and where retail's
//     own refusal arm is the ADMITTING one the seam answers the admitting value so nothing is
//     silently refused. A body is never rewritten around a missing input.
//
// **WHAT THIS FAMILY'S READING CORRECTED**, all of it from the listing or the decompiled C, and all
// of it recorded in the walked prose and in the story's verdict file:
//
//   * `0x10278650` is **`GetShootTarget`**, not a standoff anchor — slot 541 is `GetEnemies()` and
//     `0x102dfed0` is `CAI_Enemies::GetLastKnownPosition`.
//   * `0x101cf5c0` is **`UTIL_SetOrigin`**, not `NDebugOverlay::Line`, so `CAI_Motor#20`'s last arm
//     MOVES the body to the trace endpoint.
//   * `CAI_Navigator::MoveNormal` returns an **`AIMoveResult_t`**, not a distance; its gate answers
//     `-4` `AIMR_ILLEGAL` and its speed arm `0` `AIMR_OK`.
//   * `m_hOpeningDoor` is **`+0x5d24`**, not `+0x644c` (which is `m_eAlternateAI`).
//   * `OnObstructingDoor`'s "-1 nav position" arm writes the **result pointer** (arg 4), not the
//     move goal; and the successful-splice arm writes `moveGoal->maxDist = distClear`.
//   * `CAI_StandoffGoal::UpdateOnRemove`'s `inputdata_t` sets `+0x14` to `-1`, not `+0x0c`.
//   * `CAI_StandoffBehavior#22`'s log is `"NPC in standoff lacks needed low aim activity (%s)"`.
//   * `CAI_TestHull::Spawn`'s used-mask test is **signed** (`JLE`), so a zero or negative mask
//     short-circuits to hull 0 without the 22-miss fallback.
//   * `CAI_Motor+0x3c` is `m_vecVelocity` (the datamap says so), not a facing-queue count.
//   * `_DAT_1044e658` is the **double 0.01** and `_DAT_104994e0` is **-30.0f**, both read out of
//     the pinned image; the oracle had the first listed as unrecovered.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"

namespace
{
	// --- The recovered constants, by address ----------------------------------------------------

	// `_DAT_10449270`, a QWORD load (`FMUL double ptr`) in both step bodies: **0.5**.
	constexpr double GMotor10StepHalf = 0.5;
	// `_DAT_104454c0`, `00 00 80 3f`: **1.0f**, the numerator of `1.0 - maxDist/step`.
	constexpr float GMotor10One = 1.0f;
	// `_DAT_104454c4`, `00 00 00 00`: **0.0f**, `MoveNormal`'s ideal-speed floor.
	constexpr float GMotor10Zero = 0.0f;
	// `_DAT_104493d0`, a QWORD (`FCOMP double ptr`): **0.1**, slot 20's distance slack.
	constexpr double GMotor10StepSlack = 0.1;
	// `_DAT_1044fab0`, eight zero bytes: **0.0**, the step distance `0x102e1560` must exceed before
	// it moves at all.
	constexpr float GMotor10StepEpsilon = 0.0f;
	// `_DAT_1044e658`, `7b 14 ae 47 e1 7a 84 3f`, a QWORD (`FCOMP double ptr`): **0.01**. Read out
	// of the pinned image; `docs/vtmb/npc-ai/lifecycle.md` lists it as unrecovered.
	constexpr double GMotor10MoveNormalEpsilon = 0.01;
	// `_DAT_10451acc`, `00 00 80 42`: **64.0f**, the clearance below which a scripted NPC gives the
	// door to the alternate AI.
	constexpr float GMotor10DoorAlternateAiClearance = 64.0f;
	// `_DAT_10449258`, `00 00 40 40`: **3.0f**, how long that alternate-AI mode lives.
	constexpr float GMotor10DoorAlternateAiDuration = 3.0f;
	// `_DAT_104994e0`, `00 00 f0 c1`: **-30.0f**, the Z the shoot target is raised by when the
	// enemy's type-3 stat list answers 5 for stat 0xb.
	constexpr float GMotor10ShootTargetZOffset = -30.0f;
	// `_DAT_1044eb08` — degrees to radians, `0x10139610`'s scale.
	constexpr float GMotor10DegToRad = 0.01745329251994329577f;

	// The hull sweep `CAI_Motor#20` runs: kind 2, mask `0x202400b`, extent 100.0f (`0x42c80000`,
	// pushed as a literal at `102e1871`).
	constexpr int32 GMotor10StepTraceKind = 2;
	constexpr int32 GMotor10StepTraceMask = 0x202400b;
	constexpr float GMotor10StepTraceExtent = 100.0f;

	// `CAI_TestHull::Spawn`'s five constants.
	constexpr int32 GMotor10TestHullCount = 22;          // `CMP EDI,0x16`
	constexpr int32 GMotor10SolidBbox = 2;               // `SOLID_BBOX`
	constexpr uint32 GMotor10SolidNotSolid = 0x4;        // `FSOLID_NOT_SOLID`
	constexpr int32 GMotor10MoveTypeFly = 4;             // `MOVETYPE_FLY`
	constexpr int32 GMotor10MoveCollideDefault = 0;
	constexpr int32 GMotor10TestHullHealth = 0x32;       // 50
	// `0x40000` — the flag `ElysiumCameraAnimated.cpp` records beside `MakeDormant`, i.e. dormancy.
	constexpr int32 GMotor10FlagDormant = 0x40000;

	// `CAI_StandoffGoal`'s aggressiveness range and its exempt sentinel.
	constexpr int32 GMotor10AggressivenessMin = 0;
	constexpr int32 GMotor10AggressivenessMax = 4;
	constexpr int32 GMotor10AggressivenessSentinel = 5;
	// `CAI_GoalEntity::m_flags` (+0x480): bit 0 ACTIVE, bit 1 "actors resolved once".
	constexpr uint32 GMotor10GoalFlagActive = 0x1;
	constexpr uint32 GMotor10GoalFlagActorsResolved = 0x2;

	// `CAI_StandoffBehavior#22`'s numbers, all literals in the listing.
	constexpr int32 GMotor10HintTypeLowAim = 0x65;       // `CMP [EDI+0x5dc],0x65`
	constexpr int32 GMotor10ActIdle = 1;
	constexpr int32 GMotor10ActCoverLow = 8;
	constexpr int32 GMotor10ActWalk = 9;
	constexpr int32 GMotor10ActRun = 0x12;
	constexpr int32 GMotor10PostureCoverLow = 1;
	constexpr int32 GMotor10PostureStand = 2;
	constexpr int32 GMotor10PostureLowAim = 3;
	// `0x10601ef8` and `0x10601ee8`, read out of the listing.
	const TCHAR* const GMotor10WeaponSmg1 = TEXT("weapon_smg1");
	const TCHAR* const GMotor10WeaponPistol = TEXT("weapon_pistol");
	// `0x10601e9c` and its `"no weapon"` fallback at `0x10601edc`.
	const TCHAR* const GMotor10LowAimWarning =
		TEXT("NPC in standoff lacks needed low aim activity (%s)");
	const TCHAR* const GMotor10NoWeapon = TEXT("no weapon");

	// `OnObstructingDoor`'s capability mask, tested twice: once for "NOT the full 0xd00" and once
	// for "ANY of 0xd00" (`TEST AH,0xd`).
	constexpr int32 GMotor10DoorCapabilityMask = 0xd00;
	// The bit values the body ORs into the door's block word `+0x644`.
	constexpr uint32 GMotor10DoorBlockedBySquad = 0x1;
	constexpr uint32 GMotor10DoorBlockedByPolicy = 0x4;
	constexpr uint32 GMotor10DoorBlockedNoRoute = 0x40;
	// `0x1027f550`'s own two bits.
	constexpr uint32 GMotor10DoorBlockedNoCapability = 0x8;
	constexpr uint32 GMotor10DoorBlockedRetryPending = 0x10;
	// `0x10304130`'s flag word at `1029864a`.
	constexpr int32 GMotor10DoorRouteFlags = 0x30;
	// Retail's `NPC_STATE_SCRIPT`, the value slot 464 `GetState` must answer for arm 3.
	constexpr int32 GMotor10StateScript = 4;
	// `CBaseDoor::m_toggle_state` values arm 7 gives up quietly on.
	constexpr int32 GMotor10DoorToggleAtTop = 0;
	constexpr int32 GMotor10DoorToggleGoingUp = 2;
	// `AIMoveResult_t`, retail's own numbering.
	constexpr int32 GMotor10AimrIllegal = -4;
	constexpr int32 GMotor10AimrBlockedWorld = -2;
	constexpr int32 GMotor10AimrOk = 0;
	constexpr int32 GMotor10AimrChangeType = 4;
	// `CAI_Navigator`'s own activity number the ideal-speed floor arm tests `m_Activity` against.
	constexpr int32 GMotor10MoveNormalActivityGate = 2;

	// --- Units ----------------------------------------------------------------------------------

	// This world's centimetres into retail's Source units, Y negated. The same two lines as
	// `ElysiumNpcKernelMotor.cpp`'s `SourceOf`; both are file-local so neither family owns the
	// other's file.
	FVector Motor10SourceOf(const FVector& Cm)
	{
		return FVector(Cm.X / ElysiumMove::U, -Cm.Y / ElysiumMove::U, Cm.Z / ElysiumMove::U);
	}

	FVector Motor10PortOf(const FVector& Units)
	{
		return FVector(Units.X * ElysiumMove::U, -Units.Y * ElysiumMove::U,
			Units.Z * ElysiumMove::U);
	}

	// The global used-hull mask `DAT_10610be8`. `.data`, initialised on disk to `0xffffffff` and
	// zeroed by `0x102f9900` at the start of every node-graph build; nothing in this runtime builds
	// one, so it stands at retail's post-clear value. File-static because it is global in retail.
	int32 GMotor10UsedHullBits = 0;
	// `DAT_1093412c`, the second word `0x102f9900` clears with it. Nothing reads it in this band, so
	// what it IS stays unrecovered; it is cleared here because the retail body clears it.
	int32 GMotor10UsedHullCompanion = 0;
}

// =================================================================================================
// The `CAI_Motor` seams.
// =================================================================================================

bool FElysiumNpc::MotorMoveTraceSweep(int32 Kind, const FVector& StartUnits,
	const FVector& EndUnits, int32 Mask, float ExtentUnits, const void* Filter,
	FMotorMoveTrace& OutTrace) const
{
	// `thunk_FUN_102e6d70(motor->+0x68, kind, start, end, mask, 0, extent, 0, &trace, filter, 0)`.
	// Retail's own head, before it dispatches on `kind`, is the record's initialisation and it is
	// ported here because it is the half that IS recovered:
	//
	//     trace[9] = 0;                       // +0x24 flTotalDist
	//     trace[7] = 0;                       // +0x1c pObstruction
	//     trace[4..6] = vec3_origin;          // +0x10 vHitNormal, DAT_1070d1b0
	//     trace[0] = 0;                       // +0x00 fStatus
	//     trace[1..3] = *start;               // +0x04 vEndPosition, seeded to the START
	//
	// and every arm answers `trace.fStatus >= 0`.
	(void)EndUnits;
	(void)Filter;
	OutTrace = FMotorMoveTrace();
	OutTrace.EndPositionUnits = StartUnits;

	++Motor10Seams.MoveTraceSweeps;
	Motor10Seams.LastMoveTraceKind = Kind;
	Motor10Seams.LastMoveTraceMask = Mask;
	Motor10Seams.LastMoveTraceExtent = ExtentUnits;

	// **SEAM**: nothing in this substrate sweeps a hull for the kernel (family Motor's
	// `KernelHullTrace` states the same for the other trace entry). The record keeps retail's own
	// initialisation, which IS the clear result, and `fStatus >= 0` is therefore true — the
	// admitting answer, so nothing downstream is silently refused.
	return OutTrace.Status >= 0;
}

int32 FElysiumNpc::MotorStepToPoint(const FVector& EndUnits, const FElysiumEntity* ExpectedBlocker,
	bool bNearGoal, FMotorMoveTrace* OutTrace, const void* Filter)
{
	// `thunk_FUN_102e0bd0(this, &end, goal->+0x34, -1.0, 1, nearGoal, pMoveTrace, filter)`.
	// **SEAM**: this is a DIFFERENT call from family Motor's `MotorApplyIntervalMovement`, which
	// stands for the same retail address reached from `AutoMovement` with a root-motion delta and a
	// yaw. Answering 0 is retail's own refusal value, so `0x102e1560` takes its `slot +0x28` arm.
	(void)ExpectedBlocker;
	(void)bNearGoal;
	(void)OutTrace;
	(void)Filter;
	++Motor10Seams.StepToPointCalls;
	Motor10Seams.LastStepEndUnits = EndUnits;
	return 0;
}

bool FElysiumNpc::MotorLocalNavigatorTakesGoal(const FLocalMoveGoal& Goal) const
{
	// `(*(motor+0x10))->slot 6 (+0x18)(goal)`. **SEAM**: `m_pLocalNavigator` (+0x5d38) is an
	// `ELYSIUM_NPC_WORD_CHAIN` row onto the one mover, which keeps no goal.
	(void)Goal;
	++Motor10Seams.LocalNavigatorAsks;
	return false;
}

void FElysiumNpc::MotorOnMoveExecuteFailed()
{
	// `this->slot 10 (+0x28)()` on `CAI_Motor`'s own table. **SEAM**, retail identity unrecovered.
	++Motor10Seams.MoveExecuteFailures;
}

float FElysiumNpc::MotorCurSpeed() const
{
	// `CAI_Motor::GetCurSpeed` `0x102e12c0`, whose whole body is `JMP [[owner]+0x3e0]` — the
	// owner's slot 248 `GetIdealSpeed`.
	return GetIdealSpeed();
}

void FElysiumNpc::MotorSetOriginToTraceEnd(const FVector& EndPositionUnits)
{
	// `UTIL_SetOrigin(owner, trace.vEndPosition, true)` `0x101cf5c0`:
	//     entity->slot 62 (+0xf8) SetLocalOrigin(vec);
	//     if (bFireTriggers) entity->PhysicsTouchTriggers();
	// NOT `NDebugOverlay::Line` — `docs/vtmb/npc-kernel/layout.md`'s `m_vecGrappleSavedOrigin` row
	// already names this address "the SetAbsOrigin helper", and the body above is the whole of it.
	// The move is REAL and is performed.
	//
	// `SetRuntimeOrigin` is this runtime's slot 62: it writes the authoritative field and then runs
	// the body-follow hook. `PhysicsTouchTriggers` has no kernel-side counterpart here and is left
	// as the one unported half, named.
	++Motor10Seams.SetOriginCalls;
	Motor10Seams.LastSetOriginUnits = EndPositionUnits;
	SetRuntimeOrigin(Motor10PortOf(EndPositionUnits));
}

// =================================================================================================
// The `CAI_Navigator` seams.
// =================================================================================================

int32 FElysiumNpc::RouteNavType() const
{
	// `thunk_FUN_1030bc00(navigator->+0x30)`. **SEAM**: family TroikaHelpers' `NavPathSample`
	// records that there is no `CAI_Path` here; 0 (`NAV_GROUND`) is the value that takes the gate's
	// first arm and lets the move through.
	return 0;
}

void FElysiumNpc::NavigatorJumpTeardown()
{
	// `navigator->+0x20 (m_pMotor) -> slot 8 (+0x20)()`. **SEAM**, retail identity unrecovered.
	++Motor10Seams.NavGateJumpTeardowns;
}

void FElysiumNpc::NavigatorClimbTeardown()
{
	// `navigator->+0x20 (m_pMotor) -> slot 5 (+0x14)()`. **SEAM**, retail identity unrecovered.
	++Motor10Seams.NavGateClimbTeardowns;
}

void FElysiumNpc::NavigatorMoveGatePass()
{
	// `thunk_FUN_102f13d0(navigator, 0)`. **SEAM**, retail identity unrecovered.
	++Motor10Seams.NavGatePasses;
}

bool FElysiumNpc::NavigatorMoveOverride(int32& InOutResult)
{
	// `navigator->slot 16 (+0x40)(&result)`. **SEAM**: no navigator subclass here, so the seeded
	// `-4` is never returned and the body runs its own path.
	(void)InOutResult;
	++Motor10Seams.NavMoveOverrideAsks;
	return false;
}

int32 FElysiumNpc::NavigatorEnactMove(const FNavMoveInfo& Info, int32 Argument)
{
	// `navigator->slot 15 (+0x3c)(&moveInfo, argument)`. **SEAM**: `AIMR_OK` is the ADMITTING
	// value — it is what lets `MoveNormal` reach its restore arm and its slot-6 tail, both of which
	// are recovered behaviour a refusal here would hide.
	(void)Info;
	++Motor10Seams.NavEnactCalls;
	Motor10Seams.LastNavEnactArgument = Argument;
	return GMotor10AimrOk;
}

void FElysiumNpc::NavigatorMoveTail()
{
	// `navigator->slot 6 (+0x18)()`. **SEAM**, retail identity unrecovered.
	++Motor10Seams.NavMoveTails;
}

// =================================================================================================
// `0x102efd50` — `CAI_Navigator::MoveNormal`'s gate.
// =================================================================================================

bool FElysiumNpc::NavigatorMoveGate()
{
	// `FUN_102efd50`, in retail's order.
	const int32 RouteType = RouteNavType();          // thunk_FUN_1030bc00(nav->+0x30)
	const int32 NavTypeNow = NavGetType();           // nav->+0x18, family Motor's word

	if (RouteType == 0)
	{
		if (NavTypeNow != 0)
		{
			// `DevMsg(s_Warning__NPC_appears_to_have_wro_1060ff68)` — "Warning: NPC appears to have
			// wro…", the string the corpus truncates.
			++Motor10Seams.NavGateWrongTypeWarnings;
			UE_LOG(LogElysiumNpcEnt, Verbose,
				TEXT("%s Nav: 0x102efd50 warning: NPC appears to have wrong nav type %d for a "
					"ground route"), *DebugString(), NavTypeNow);
			if (NavTypeNow == 1)
			{
				NavigatorJumpTeardown();
			}
			else if (NavTypeNow == 3)
			{
				NavigatorClimbTeardown();
			}
			NavSetType(0);                           // thunk_FUN_102eeba0(nav, 0)
		}
	}
	else if (RouteType == 2 && NavTypeNow != 2)
	{
		// A FLY route under a non-fly nav type: the ONLY refusal, and note it happens WITHOUT the
		// `0x102f13d0` call and WITHOUT clearing `+0x51`.
		return false;
	}

	NavigatorMoveGatePass();                         // thunk_FUN_102f13d0(nav, 0)
	bNavigatorByte51 = false;                        // nav->+0x51 = 0
	return true;
}

// =================================================================================================
// `0x102efaa0` — `CAI_Navigator::MoveNormal`, `CAI_Navigator#12`.
// =================================================================================================

int32 FElysiumNpc::NavigatorMoveNormal(int32 Argument)
{
	// The scope-trace frame ("CAI_Navigator::MoveNormal", `0x1060ff48`) brackets the whole body and
	// is pushed and popped on every exit. This runtime carries no VProf stack, so the frame is the
	// one part of the body with no port counterpart; the ORDER underneath it is what matters and is
	// reproduced exactly.

	// 1. The gate. `MOV EAX,0xfffffffc` — **-4, `AIMR_ILLEGAL`**. The decompiler prints it as
	//    `-NAN` because it typed the return `float`; the listing returns it in `EAX` and every
	//    other exit of this body is an integer `AIMoveResult_t` too.
	if (!NavigatorMoveGate())
	{
		return GMotor10AimrIllegal;
	}

	// 2. The override. Retail SEEDS the result with `-4` before the call (`102efb27`) and returns
	//    whatever the override left there when it answers true.
	int32 OverrideResult = GMotor10AimrIllegal;
	if (NavigatorMoveOverride(OverrideResult))
	{
		return OverrideResult;
	}

	// 3. The snapshot, in the listing's order: slot 248 `GetIdealSpeed` FIRST, then `m_Activity`
	//    (+0xfec), then `m_nSequence` (+0x6f0), then slot 217 `GetAbsOrigin`.
	const float SpeedBefore = GetIdealSpeed();                       // owner slot 248 (+0x3e0)
	const int32 SavedActivity = ActivityNumber;                      // +0x0fec m_Activity
	const int32 SavedSequence = SequenceNumber;                      // +0x06f0 m_nSequence
	const FVector SavedOriginUnits = Motor10SourceOf(Origin);        // owner slot 217 (+0x364)

	// 4. Push the ROUTE's movement activity through owner slot 310 `SetActivity`. The activity is
	//    `thunk_FUN_102ee3f0(nav)` = `nav->+0x30->+0x2c`, which family TroikaHelpers already stands
	//    as `NavCurrentLinkActivity` — called rather than answered a second way.
	SetActivity(NavCurrentLinkActivity());                           // owner slot 310 (+0x4d8)

	// 5. Re-read the ideal speed and refuse the move when it has fallen to the floor while the
	//    CURRENT activity is 2. `FCOMP _DAT_104454c4 (0.0f); TEST AH,0x41; JP` — C0 or C3, so the
	//    test is `speed <= 0.0` and NOT `speed < 0.0`, and a NaN speed skips the arm.
	const float SpeedAfter = GetIdealSpeed();
	if (!(SpeedAfter > GMotor10Zero) && !FMath::IsNaN(SpeedAfter)
		&& ActivityNumber == GMotor10MoveNormalActivityGate)
	{
		return GMotor10AimrOk;                                       // `XOR EAX,EAX`, not `0.0f`
	}

	// 6. Zero the two move structs — 14 dwords at `ESP+0x68` and then 31 dwords at `ESP+0x24`.
	//    **The second REP STOSD covers the first**: `[E0-0x7c, E0)` contains `[E0-0x38, E0)`. That
	//    is retail's own redundancy and there is nothing for the port to keep but the fact, because
	//    the block below is built from scratch by slot 17 anyway.
	FNavMoveInfo Info = FUN_102eee40();                              // navigator slot 17 (+0x44)

	// 7. Enact. Note the argument order at `102efbfc`: the move-info block is pushed LAST, so it is
	//    the first parameter and this function's own stack word is the second.
	const int32 Result = NavigatorEnactMove(Info, Argument);         // navigator slot 15 (+0x3c)
	if (Result != GMotor10AimrOk)
	{
		return Result;
	}

	// 8. The restore. TWO independent tests, and the checklist's walk folds them into one: the
	//    FIRST is on the ideal speed SAVED at step 3 (`FLD [ESP+0x14]`, `102efc11`), the second on
	//    how far the body actually moved. Both are `FCOMP double [0x1044e658]` with `TEST AH,5; JP`,
	//    which is a strict `<` with NaN taking the skip.
	if (SpeedBefore < static_cast<float>(GMotor10MoveNormalEpsilon))
	{
		const FVector MovedUnits = Motor10SourceOf(Origin) - SavedOriginUnits;
		if (MovedUnits.Size() < GMotor10MoveNormalEpsilon)
		{
			SequenceNumber = SavedSequence;                          // +0x06f0, written DIRECTLY
			SetActivity(SavedActivity);                              // owner slot 310
		}
		// `MOV ECX,[ESP+0x10]; TEST ECX,ECX; JNZ` — the result is re-tested here. It is 0 on every
		// path that reaches this point, so the branch is dead; it is written out because it is in
		// the body.
		if (Result != GMotor10AimrOk)
		{
			return Result;
		}
	}

	// 9. The tail, reached whether or not the restore arm ran.
	if (!bNavigatorByte51)
	{
		NavigatorMoveTail();                                         // navigator slot 6 (+0x18)
	}
	return Result;
}

// =================================================================================================
// `0x102e14a0` / `0x102e1560` / `0x102e1760` — the two `CAI_Motor` step bodies.
// =================================================================================================

int32 FElysiumNpc::MoveGroundExecute(const FLocalMoveGoal& Goal, FMotorMoveTrace* OutTrace,
	const void* Filter)
{
	// `CAI_Motor#19` `0x102e14a0`, four statements and a tail call. It RETURNS `0x102e1560`'s
	// answer: `CALL 0x10012116; POP EDI; POP ESI; POP ECX; RET 0xc` leaves `EAX` alone, which the
	// decompiler's `void` signature loses.
	FLocalMoveGoal Working = Goal;

	// 1. `this->slot 18 (+0x48)(goal)` — `CAI_Motor::MoveFacing`, family TroikaHelpers' body.
	FUN_102e19e0(Working.DirUnits);

	// 2. `flIdealSpeed = thunk_FUN_102e12c0(this)`.
	const float Speed = MotorCurSpeed();

	// 3. `step = (|m_vecVelocity| + flIdealSpeed) * m_flMoveInterval * 0.5`. The 0.5 is
	//    `_DAT_10449270`, a **double** (`FMUL double ptr`), and the multiply happens in the x87's
	//    80-bit stack before the result is stored back as a float.
	const double Magnitude = FMath::Sqrt(
		static_cast<double>(MotorVelocityUnits.X) * MotorVelocityUnits.X
		+ static_cast<double>(MotorVelocityUnits.Y) * MotorVelocityUnits.Y
		+ static_cast<double>(MotorVelocityUnits.Z) * MotorVelocityUnits.Z);
	const float StepUnits = static_cast<float>(
		(Magnitude + Speed) * TroikaMotor.MoveInterval * GMotor10StepHalf);

	// 4. `thunk_FUN_102e1560(this, goal, speed, step, arg2, arg3)`.
	return MotorMoveGroundExecuteWalk(Working, Speed, StepUnits, OutTrace, Filter);
}

int32 FElysiumNpc::MotorMoveGroundExecuteWalk(FLocalMoveGoal& Goal, float Speed, float StepUnits,
	FMotorMoveTrace* OutTrace, const void* Filter)
{
	// `FUN_102e1560`, arm by arm.
	//
	// 1. The interval clamp. Note the polarity: a step that FITS inside the goal's remaining
	//    distance consumes the WHOLE interval, so the remainder is zeroed; a step that overshoots
	//    consumes only the fraction that fits.
	bool bNearGoal;
	if (StepUnits <= Goal.MaxDistanceUnits)
	{
		bNearGoal = false;
		TroikaMotor.MoveInterval = 0.f;                     // this->+0x30 = 0
	}
	else
	{
		bNearGoal = true;
		if ((Goal.Flags & 0x2) != 0)
		{
			// The flag-0x2 arm zeroes the interval outright rather than scaling it.
			TroikaMotor.MoveInterval = 0.f;
		}
		else
		{
			TroikaMotor.MoveInterval =
				(GMotor10One - Goal.MaxDistanceUnits / StepUnits) * TroikaMotor.MoveInterval;
		}
		StepUnits = Goal.MaxDistanceUnits;
	}

	// 2. `m_vecVelocity = goal.dir * speed`, per axis, and note the ORDER the listing writes them
	//    in: Y is read into a temporary BEFORE X is stored, so an aliased goal cannot be observed
	//    half-written. Nothing here aliases, and the result is the same vector.
	MotorVelocityUnits = FVector(Speed * Goal.DirUnits.X, Speed * Goal.DirUnits.Y,
		Speed * Goal.DirUnits.Z);

	// 3. The step itself, only above `_DAT_1044fab0` = 0.0 — a STRICT `<`, so a zero-length step
	//    takes the local-navigator arm.
	if (GMotor10StepEpsilon < StepUnits)
	{
		// `pfVar5 = owner->slot 220 (+0x370) GetOrigin()`, then `end = origin + step * dir`.
		const FVector OriginUnits = Motor10SourceOf(Origin);
		const FVector EndUnits = OriginUnits + StepUnits * Goal.DirUnits;
		const int32 Answer = MotorStepToPoint(EndUnits, Goal.ExpectedBlocker, bNearGoal, OutTrace,
			Filter);
		if (Answer == 0)
		{
			MotorOnMoveExecuteFailed();                     // this->slot 10 (+0x28)()
			return 0;
		}
		return Answer;
	}

	// 4. At or below the epsilon the LOCAL NAVIGATOR answers instead, folded to 1 / 0.
	return MotorLocalNavigatorTakesGoal(Goal) ? 1 : 0;
}

int32 FElysiumNpc::MotorMoveGroundStep(FLocalMoveGoal& Goal, FMotorMoveTrace* OutTrace,
	const void* Filter)
{
	// `CAI_Motor#20` `0x102e1760`, read from the listing.
	//
	// 1. `this->slot 18 (+0x48)(goal)` — `MoveFacing` FIRST, on the goal, before anything else.
	FUN_102e19e0(Goal.DirUnits);

	// 2. `m_vecVelocity = goal.dir * GetCurSpeed()`.
	const float Speed = MotorCurSpeed();                    // thunk_FUN_102e12c0
	MotorVelocityUnits = FVector(Speed * Goal.DirUnits.X, Speed * Goal.DirUnits.Y,
		Speed * Goal.DirUnits.Z);

	// 3. `step = (sqrt(v.v) + speed) * m_flMoveInterval * 0.5` — the velocity just written, dotted
	//    with itself. `_DAT_10449270` is a **double**.
	const double Magnitude = FMath::Sqrt(
		static_cast<double>(MotorVelocityUnits.X) * MotorVelocityUnits.X
		+ static_cast<double>(MotorVelocityUnits.Y) * MotorVelocityUnits.Y
		+ static_cast<double>(MotorVelocityUnits.Z) * MotorVelocityUnits.Z);
	float StepUnits = static_cast<float>(
		(Magnitude + Speed) * TroikaMotor.MoveInterval * GMotor10StepHalf);

	// 4. The clamp. `FCOMP; FNSTSW AX; AND EAX,0x4100; JNZ` tests C3 (equal) and C0 (less), so the
	//    jump is `step <= maxDist` — and on THAT arm the step is left alone and only the interval
	//    is zeroed.
	if (StepUnits <= Goal.MaxDistanceUnits)
	{
		TroikaMotor.MoveInterval = 0.f;
	}
	else
	{
		TroikaMotor.MoveInterval =
			(GMotor10One - Goal.MaxDistanceUnits / StepUnits) * TroikaMotor.MoveInterval;
		StepUnits = Goal.MaxDistanceUnits;
	}

	// 5. The sweep. The start point is **slot 220 `GetOrigin`** (`vt+0x370` on the outer at `+0x4`),
	//    not slot 217 `GetAbsOrigin`; `VectorMA(origin, step, dir)` builds the endpoint; and the
	//    0x38-byte record is zeroed and filled by `0x102e6d70` on the motor's `+0x68` with kind 2,
	//    mask `0x202400b`, extent 100.0 and this body's THIRD argument as the filter.
	const FVector OriginUnits = Motor10SourceOf(Origin);    // slot 220 GetOrigin
	const FVector EndUnits = OriginUnits + StepUnits * Goal.DirUnits;   // VectorMA
	FMotorMoveTrace Trace;
	MotorMoveTraceSweep(GMotor10StepTraceKind, OriginUnits, EndUnits, GMotor10StepTraceMask,
		GMotor10StepTraceExtent, Filter, Trace);

	// 6. The copy. Into the SECOND argument — the caller's `AIMoveTrace_t` out param — and only
	//    when it is non-null. **The move goal is never written back.** `+0x2c` and `+0x30` are not
	//    copied; see `FMotorMoveTrace` for the listing's own field list.
	if (OutTrace != nullptr)
	{
		OutTrace->Status = Trace.Status;
		OutTrace->EndPositionUnits = Trace.EndPositionUnits;
		OutTrace->HitNormal = Trace.HitNormal;
		OutTrace->Obstruction = Trace.Obstruction;
		OutTrace->DistObstructedUnits = Trace.DistObstructedUnits;
		OutTrace->TotalDistUnits = Trace.TotalDistUnits;
		OutTrace->ObstructionHandle = Trace.ObstructionHandle;
		OutTrace->Word34 = Trace.Word34;
	}

	// 7. `|trace.flTotalDist - step| <= 0.1` (`_DAT_104493d0`, a **double**; `TEST AH,0x41; JP`
	//    makes it `<=` with NaN taking the other arm) — the step travelled its whole length.
	if (FMath::Abs(static_cast<double>(Trace.TotalDistUnits) - StepUnits) <= GMotor10StepSlack)
	{
		// `return 4` ALSO requires the goal's expected blocker to be NON-ZERO: `MOV EBX,[EBX+0x34];
		// TEST EBX,EBX; JZ -> return 0`, and only then `CMP EBP,EBX`.
		if (Goal.ExpectedBlocker == nullptr)
		{
			return GMotor10AimrOk;
		}
		if (Trace.Obstruction != Goal.ExpectedBlocker)
		{
			return GMotor10AimrOk;
		}
		return GMotor10AimrChangeType;                      // 4
	}

	// 8. Otherwise the body MOVES to the trace endpoint — `UTIL_SetOrigin(owner, trace+0x04, 1)`,
	//    `0x101cf5c0`, which is not a debug line — and answers `1 + 2 * (trace.fStatus < 0)`.
	MotorSetOriginToTraceEnd(Trace.EndPositionUnits);
	return 1 + 2 * (Trace.Status < 0 ? 1 : 0);
}

// =================================================================================================
// `0x10273070` / `0x10273180` — the two hull-size bodies.
// =================================================================================================

void FElysiumNpc::SetHullSizeNormal(bool bForce)
{
	// `FUN_10273070`, in retail's order.
	//
	// 1. The self-check, which runs UNCONDITIONALLY and before the gate:
	//        bits  = NAI_Hull::Bits(m_eHull);              // 0x102d6210
	//        used  = NAI_Hull::GetUsedHullBits();          // 0x102f9950 -> DAT_10610be8
	//        again = NAI_Hull::Bits(m_eHull);              // called a SECOND time
	//        if ((bits & used) != again) { six DevMsg lines }
	//    The second call is retail's; it makes the test `(bits & used) != bits`, i.e. "this body's
	//    hull was never precached".
	const int32 Bits = RetailHullBits(HullKind);
	const int32 Used = RetailUsedHullBits();
	const int32 BitsAgain = RetailHullBits(HullKind);
	if ((Bits & Used) != BitsAgain)
	{
		++HullNotPrecachedWarnings;
		LastHullNotPrecached = HullKind;
		// `"\n****ERROR***\n"`, `"%s is using hull %s which has not been precached.\n"` (the entity
		// through `GetDebugName`, the hull through `NAI_Hull::Name` `0x102d6230`), `"Be sure that
		// this class's GetUsedHullBits function includes that hull type.\n"`, `"If it already is,
		// this entity is probably spawned after the node graph is created.\n"`, `"Have the class
		// responsible for spawning it include it.\n"`, `"****ERROR***\n\n"`.
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("%s Hull: ****ERROR*** %s is using hull %s which has not been precached "
				"(0x10273070)"), *DebugString(), *DebugString(), *RetailHullName(HullKind));
	}

	// 2. The gate: SET or forced.
	if (!bIsUsingSmallHull && !bForce)
	{
		return;
	}

	// 3. `UTIL_SetSize(this, NAI_Hull::Mins(m_eHull), NAI_Hull::Maxs(m_eHull))`. The listing
	//    evaluates MAXS first (`0x102d6120`, `1000a993`) and MINS second (`0x102d6100`, `1000fd44`)
	//    and then pushes mins before maxs, so the CALL order and the ARGUMENT order differ; the
	//    extents come off one table either way.
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	RetailHullExtents(HullKind, EElysiumHullExtents::Full, MinsUnits, MaxsUnits);      // family Motor's seam: the zero box
	LastSetSizeMinsUnits = MinsUnits;
	LastSetSizeMaxsUnits = MaxsUnits;
	++SetSizeCalls;

	// 4. The clear happens whether or not the physics rebuild does — `MOV byte [ESI+0x5f2d],0` sits
	//    before the `JZ` at `10273131`.
	bIsUsingSmallHull = false;

	// 5. `if (m_pPhysicsObject (+0x36c)) SetupVPhysicsHull();`
	if (bHasVPhysicsObject)
	{
		++VPhysicsHullRebuilds;
	}
}

bool FElysiumNpc::SetHullSizeSmall(bool bForce)
{
	// `FUN_10273180`, the twin. The gate is INVERTED — it runs when the small hull is NOT already
	// in use — and there is no self-check.
	if (!bIsUsingSmallHull || bForce)
	{
		// `UTIL_SetSize(this, NAI_Hull::SmallMins(m_eHull), NAI_Hull::SmallMaxs(m_eHull))`
		// (`0x102d6140` / `0x102d6160`) — the SMALL pair of the same hull's row, not a smaller
		// hull. For two rows it is the wider of the two: TZIMISCE1's small box is 45 against its
		// full 35, so "small hull" is retail's name for the alternate box, not a shrink.
		FVector MinsUnits = FVector::ZeroVector;
		FVector MaxsUnits = FVector::ZeroVector;
		RetailHullExtents(HullKind, EElysiumHullExtents::Small, MinsUnits, MaxsUnits);
		LastSetSizeMinsUnits = MinsUnits;
		LastSetSizeMaxsUnits = MaxsUnits;
		++SetSizeCalls;

		bIsUsingSmallHull = true;
		if (bHasVPhysicsObject)
		{
			++VPhysicsHullRebuilds;
		}
	}
	// `return CONCAT31(uVar3, 1)` — **1 unconditionally**, including on the path where the gate
	// refused and nothing changed. Retail's, and reproduced.
	return true;
}

// =================================================================================================
// `0x102d72f0` — `CAI_TestHull::Spawn`, and the hull table it picks from.
// =================================================================================================

int32 FElysiumNpc::RetailHullBits(int32 Hull)
{
	// `NAI_Hull::Bits(hull)` = `PTR_DAT_1060a750[hull][0]` (`0x102d6210`). **SEAM**: no hull table.
	(void)Hull;
	return 0;
}

FString FElysiumNpc::RetailHullName(int32 Hull)
{
	// `NAI_Hull::Name(hull)` = `PTR_DAT_1060a750[hull][1]` (`0x102d6230`). **SEAM**: no hull table.
	(void)Hull;
	return FString();
}

int32 FElysiumNpc::RetailUsedHullBits()
{
	// `FUN_102f9950` — `return DAT_10610be8;`.
	return GMotor10UsedHullBits;
}

void FElysiumNpc::RetailClearUsedHullBits()
{
	// `FUN_102f9900` — `DAT_10610be8 = 0; DAT_1093412c = 0;`. What the companion word IS stays
	// **unrecovered**; it is cleared because the retail body clears it.
	GMotor10UsedHullBits = 0;
	GMotor10UsedHullCompanion = 0;
}

void FElysiumNpc::RetailAddUsedHullBits(int32 Bits)
{
	// `FUN_102f9920` — `DAT_10610be8 |= param_1;`.
	GMotor10UsedHullBits |= Bits;
}

int32 FElysiumNpc::TestHullPickHull(int32 UsedHullBits, TFunctionRef<int32(int32)> HullBits,
	bool& bOutTookFallback)
{
	// `0x102d72f5`–`0x102d732e`, from the listing.
	bOutTookFallback = false;

	// `TEST EBX,EBX; JLE 0x102d7330` — a SIGNED test, and the target is the store with `EDI` still
	// zero. A mask of 0 OR of any value with the top bit set therefore takes hull 0 immediately and
	// never reaches the fallback call. The shipped `.data` initialiser for the mask is
	// `0xffffffff`, which is exactly such a value.
	if (UsedHullBits <= 0)
	{
		return 0;
	}

	for (int32 Index = 0; Index < GMotor10TestHullCount; ++Index)
	{
		if ((UsedHullBits & HullBits(Index)) != 0)
		{
			return Index;
		}
	}

	// 22 misses: `PUSH 0; CALL AddUsedHullBits; XOR EDI,EDI`. The OR of zero is a no-op and is
	// performed anyway, because the body performs it.
	RetailAddUsedHullBits(0);
	bOutTookFallback = true;
	return 0;
}

void FElysiumNpc::TestHullSpawn()
{
	// `CAI_TestHull::Spawn` `0x102d72f0`, slot 103 on `CAI_TestHull`. See
	// `ElysiumNpcKernelMotor10.inl` for why this lands on `FElysiumNpc` rather than on a new leaf.

	// 1. The hull pick, then `m_eHull` (+0x1568). The store happens at `102d7334`, before the
	//    `SetHullSizeNormal` call whose argument was already pushed.
	bool bTookFallback = false;
	HullKind = TestHullPickHull(RetailUsedHullBits(), [](int32 Hull) { return RetailHullBits(Hull); },
		bTookFallback);

	// 2. `0x10273070(this, 0)` — resize the bounds to that hull, NOT forced.
	SetHullSizeNormal(false);

	// 3. `SetSolid(SOLID_BBOX = 2)` on `m_Collision` (+0x270), under a `"CBaseEntity::SetSolid"`
	//    scope-trace frame.
	RetailSolidType = GMotor10SolidBbox;
	++RetailSolidSets;

	// 4. `AddSolidFlags(word[+0x2b4] | 4)` — retail reads the CURRENT 16-bit solid-flag word,
	//    zero-extends it, ORs `FSOLID_NOT_SOLID` in and passes the WHOLE thing to `AddSolidFlags`,
	//    which ORs it again. The double-OR is retail's and is reproduced.
	const uint32 CurrentFlags = RetailSolidFlags & 0xffffu;
	RetailSolidFlags |= (CurrentFlags | GMotor10SolidNotSolid);

	// 5. `slot 93 SetMoveType(MOVETYPE_FLY = 4, MOVECOLLIDE_DEFAULT = 0)`.
	RetailMoveType = GMotor10MoveTypeFly;
	RetailMoveCollide = GMotor10MoveCollideDefault;

	// 6. `m_iHealth (+0x210) = 0x32` — **50** — then `AddFlag(0x40000)`. The health store is at
	//    `102d7438`, after the flag's PUSH and before the call, so the health lands first.
	Health = GMotor10TestHullHealth;
	Flags |= GMotor10FlagDormant;

	// 7. `byte [+0x5f44] = 0`. The shape map binds `+0x5f44` as an output block, which a one-byte
	//    zero cannot be, so what this byte IS stays **unrecovered**.
	bTestHullByte5f44 = false;

	// 8. `JMP [vtable + 0x108]` — a TAIL jump to slot 66 `Hide()`, so `Hide`'s answer is `Spawn`'s
	//    and nothing runs after it.
	Hide();
}

// =================================================================================================
// `0x102c87a0` / `0x102c8830` / `0x102cdc50` — `CAI_StandoffGoal`, the goal entity.
// =================================================================================================

void FElysiumNpc::GoalEntityInputActivate(FStandoffGoalWords& Goal)
{
	// `CAI_GoalEntity::InputActivate` `0x102cd650`.
	if ((Goal.Flags & GMotor10GoalFlagActive) != 0)
	{
		// Already active: the body returns with NOTHING done — no list insert, no actor pass.
		return;
	}
	// `0x100f6d80` over the global goal list `DAT_106eb5d8`, intrusive node at `+0x450`.
	Goal.bOnGoalList = true;
	if ((Goal.Flags & GMotor10GoalFlagActorsResolved) != 0)
	{
		++Goal.ActorRefreshes;                               // 0x102cd3b0
	}
	else
	{
		++Goal.ActorResolves;                                // 0x102cd4a0
		Goal.Flags |= GMotor10GoalFlagActorsResolved;
	}
	Goal.Flags |= GMotor10GoalFlagActive;
	// `for (i = 0; i < +0x474; ++i) actor[i]->vtable[+0x3d0] EnableGoal()`, each handle validated
	// through `PTR_DAT_10566458` first. No actors resolve here, so the tally is the count the view
	// was given.
	Goal.EnableGoalCalls += Goal.ActorCount;
}

void FElysiumNpc::GoalEntityInputDeactivate(FStandoffGoalWords& Goal)
{
	// `CAI_GoalEntity::InputDeactivate` `0x102cdb70`, the exact inverse.
	if ((Goal.Flags & GMotor10GoalFlagActive) == 0)
	{
		return;
	}
	if ((Goal.Flags & GMotor10GoalFlagActorsResolved) != 0)
	{
		++Goal.ActorRefreshes;
	}
	else
	{
		++Goal.ActorResolves;
		Goal.Flags |= GMotor10GoalFlagActorsResolved;
	}
	Goal.Flags &= ~GMotor10GoalFlagActive;
	// `vtable[+0x3d4] DisableGoal()` per actor, and the list removal (`0x100f6e40`) LAST.
	Goal.DisableGoalCalls += Goal.ActorCount;
	Goal.bOnGoalList = false;
}

void FElysiumNpc::StandoffClampAggressiveness(FStandoffGoalWords& Goal)
{
	// `0x102c87ab`–`0x102c87f5`, shared byte-for-byte by slots 241 and 243.
	const int32 Value = Goal.Aggressiveness;
	if ((Value < GMotor10AggressivenessMin || GMotor10AggressivenessMax < Value)
		&& Value != GMotor10AggressivenessSentinel)
	{
		// `DevMsg("Invalid aggressiveness value %d\n", m_aggressiveness)` with the PRE-clamp value.
		++Goal.InvalidWarnings;
		Goal.LastInvalidValue = Value;
		if (Goal.Aggressiveness < GMotor10AggressivenessMin)
		{
			Goal.Aggressiveness = GMotor10AggressivenessMin;
			return;
		}
		if (GMotor10AggressivenessMax < Goal.Aggressiveness)
		{
			Goal.Aggressiveness = GMotor10AggressivenessMax;
		}
	}
}

void FElysiumNpc::StandoffInputActivate(FStandoffGoalWords& Goal)
{
	// `CAI_StandoffGoal::vfunc241` `0x102c87a0`. The negative arm RETURNS from inside the warning
	// block after calling the backing routine, and the tail calls it too — so it runs EXACTLY ONCE
	// on every path, which is the shape the clamp above preserves.
	StandoffClampAggressiveness(Goal);
	GoalEntityInputActivate(Goal);                           // thunk_FUN_102cd650
}

void FElysiumNpc::StandoffInputDeactivate(FStandoffGoalWords& Goal)
{
	// `CAI_StandoffGoal::vfunc243` `0x102c8830` — the same clamp, and the only difference is which
	// backing routine it ends on.
	StandoffClampAggressiveness(Goal);
	GoalEntityInputDeactivate(Goal);                         // thunk_FUN_102cdb70
}

void FElysiumNpc::StandoffGoalUpdateOnRemove(FStandoffGoalWords& Goal)
{
	// `CAI_StandoffGoal::UpdateOnRemove` `0x102cdc50`, slot 180.
	//
	// `TEST byte [ESI+0x480],0x1` — bit 0 of `m_flags`, the ACTIVE bit.
	if ((Goal.Flags & GMotor10GoalFlagActive) != 0)
	{
		// The 0x20-byte block is an `inputdata_t`, and only the `variant_t` inside it is
		// initialised — which is the `variant_t` constructor being inlined:
		//
		//     block+0x08 = 0           // variant_t's union (iVal), `MOV [ESP+0xc],EAX`
		//     block+0x18 = 0           // variant_t::fieldType = FIELD_VOID, `MOV [ESP+0x1c],EAX`
		//     block+0x14 = -1          // variant_t::eVal = INVALID_EHANDLE, after the PUSH
		//
		// **A CORRECTION**: the checklist's walk puts the `-1` at `+0x0c`. The `MOV dword ptr
		// [ESP+0x1c],0xffffffff` at `102cdc72` is issued AFTER `PUSH ECX`, so its address is
		// `block+0x14`, not `block+0x0c`.
		//
		// `pActivator` (+0x00), `pCaller` (+0x04) and `nOutputID` (+0x1c) are **left
		// uninitialised** — retail hands slot 243 three words of stack garbage. Nothing this body
		// reaches reads them, which is why it ships.
		++Goal.ReleaseInputs;
		StandoffInputDeactivate(Goal);                       // the goal's OWN slot 243 (+0x3cc)
	}
	// `CALL 0x1000cb94` — `CBaseEntity::UpdateOnRemove`, the tail, run whether or not the release
	// did.
	Goal.bUpdateOnRemoveTailRan = true;
}

// =================================================================================================
// `0x102c79e0` — `CAI_StandoffBehavior#22`, the activity translation.
// =================================================================================================

int32 FElysiumNpc::HintActivityForNode(int32 HintNode) const
{
	// **SEAM** for `owner->slot 569 (+0x8e4)(hintNode)`. Slot 569 is a generated stub on this line
	// and family Hints records that hints are node INDICES here with no type store.
	(void)HintNode;
	++Motor10Seams.HintActivityAsks;
	return INDEX_NONE;
}

int32 FElysiumNpc::StandoffLowAimActivitySmg()
{
	// **SEAM** for `DAT_10925390`, a runtime-registered activity id in uninitialised `.data`.
	return INDEX_NONE;
}

int32 FElysiumNpc::StandoffLowAimActivityPistol()
{
	// **SEAM** for `DAT_10925388`, the same.
	return INDEX_NONE;
}

bool FElysiumNpc::WeaponOwnsThisType(const TCHAR* WeaponClassname) const
{
	// **SEAM** for `CBaseCombatCharacter::Weapon_OwnsThisType(name, 0)`.
	(void)WeaponClassname;
	++Motor10Seams.WeaponOwnsAsks;
	return false;
}

int32 FElysiumNpc::StandoffTranslateActivity(FStandoffWords& Words, int32 Activity)
{
	// `CAI_StandoffBehavior::vfunc22` `0x102c79e0`, arms in retail's order.

	// 1. The hint arm. `(*(this+4))[0x1777]` is owner `+0x5ddc` `m_pHintNode`, and its `+0x5dc`
	//    `m_nHintType` must be `0x65`. Slot 569 is asked TWICE: once into a local, and again to
	//    replace an incoming activity of 1. Both calls are made in retail and both are made here.
	if (ScheduleHost.HintNode != INDEX_NONE)
	{
		int32 HintType = INDEX_NONE;
		float HintYaw = 0.f;
		float NodeYaw = 0.f;
		FVector HintOriginUnits = FVector::ZeroVector;
		// Family Debug10's `HintOverlayWords` is the `m_nHintType` reader; it answers false, so the
		// arm is skipped exactly as it is for a body with no claimed hint.
		if (HintOverlayWords(HintType, HintYaw, NodeYaw, HintOriginUnits)
			&& HintType == GMotor10HintTypeLowAim)
		{
			const int32 First = HintActivityForNode(ScheduleHost.HintNode);
			if (Activity == GMotor10ActIdle)
			{
				Activity = HintActivityForNode(ScheduleHost.HintNode);
			}
			if (Words.Posture == 0 && First == GMotor10ActCoverLow)
			{
				Words.Posture = GMotor10PostureStand;
			}
		}
	}

	// 2. Posture 2.
	if (Words.Posture == GMotor10PostureStand)
	{
		if (Activity == GMotor10ActIdle)
		{
			return GMotor10ActCoverLow;                      // `MOV EAX,0x8`
		}
		if (Activity == GMotor10ActWalk)
		{
			// `SelectHeaviestSequence(owner, 0x12, -1)` must answer a NON-NEGATIVE index.
			if (SelectHeaviestSequence(GMotor10ActRun, INDEX_NONE) >= 0)
			{
				return GMotor10ActRun;                       // `MOV EAX,0x12`
			}
		}
	}

	// 3. Posture 1. The listing re-reads `+0x1c` at `102c7a6f`, so the posture 2 arm's own write
	//    (there is none on this path) could not be observed here either way.
	if (Words.Posture == GMotor10PostureCoverLow && Activity == GMotor10ActCoverLow)
	{
		// `POP…; RET 0x4` with `EAX` still holding `[ESI+0x1c]` — which is 1.
		return GMotor10PostureCoverLow;
	}

	// 4. Posture 3, the low-aim arm.
	if (Words.Posture == GMotor10PostureLowAim
		&& (Activity == GMotor10ActCoverLow || Activity == GMotor10ActIdle))
	{
		// `DAT_10925390` and `DAT_10925388` are two registered activity ids in uninitialised
		// `.data` — the same `.data` `FireBullets`' skill table sits in — so their VALUES are
		// **unrecovered** and the seam answers `INDEX_NONE` for both. The ORDER is recovered: SMG
		// first, pistol second, the log last.
		if (WeaponOwnsThisType(GMotor10WeaponSmg1))
		{
			const int32 SmgActivity = StandoffLowAimActivitySmg();
			if (SelectHeaviestSequence(SmgActivity, INDEX_NONE) >= 0)
			{
				return SmgActivity;
			}
		}
		if (WeaponOwnsThisType(GMotor10WeaponPistol))
		{
			const int32 PistolActivity = StandoffLowAimActivityPistol();
			if (SelectHeaviestSequence(PistolActivity, INDEX_NONE) >= 0)
			{
				return PistolActivity;
			}
		}
		// `thunk_FUN_1027e590(owner, "NPC in standoff lacks needed low aim activity (%s)\n",
		// weapon ? weapon->GetClassname() : "no weapon")`. **A CORRECTION**: the checklist's walk
		// reads the literal as "low cover animation"; `0x10601e9c` is "low aim activity", and the
		// `"no weapon"` fallback at `0x10601edc` is the third argument the walk does not mention.
		++Motor10Seams.LowAimWarnings;
		const FElysiumEntity* Weapon = ActiveWeaponEntity();   // 29c-1's seam: answers null
		const FString WeaponName = (Weapon != nullptr && Weapon->Def != nullptr)
			? Weapon->Def->Classname : FString(GMotor10NoWeapon);
		// The literal is `GMotor10LowAimWarning` (`0x10601e9c`), written out here because UE's
		// format-string sanitiser requires a literal at the call site.
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("%s Standoff: NPC in standoff lacks needed low aim activity (%s) (0x102c79e0)"),
			*DebugString(), *WeaponName);
		return GMotor10ActIdle;                              // `MOV EAX,0x1`
	}

	// 5. Everything left falls to `CAI_Behavior::vfunc22`, the base this runtime does not carry.
	return INDEX_NONE;
}

// =================================================================================================
// `0x10278650` — `CAI_BaseNPC::GetShootTarget`.
// =================================================================================================

int32 FElysiumNpc::EnemyTypedStatValue(const FElysiumEntity& Enemy, int32 StatId)
{
	// **SEAM**: `enemy->+0x9c` then its `+0x13bc` / `+0x13c0` list table, the first entry whose
	// `+0x10` is 3, then `CVStatList_t::GetValue(statId)` (`0x102012d0`). Family Sounds10 records
	// the same join for `FireBullets` and takes the same refusal: no `CVStatList_t` container keyed
	// by retail's list type stands here, so the join is **unrecovered** and the answer is 0 — which
	// is also retail's own answer through the empty lazily-built `DAT_109f0b40`.
	(void)Enemy;
	(void)StatId;
	return 0;
}

FVector FElysiumNpc::RetailGetAnglesDegrees() const
{
	// **SEAM** for slot 221 `CBaseEntity::GetAngles()` (`0x100b3110`), which is a generated stub on
	// this line. `FElysiumEntity::Angles` is the port's equivalent word.
	return Angles;
}

FVector FElysiumNpc::AngleVectorsForward(const FVector& AnglesDegrees)
{
	// `0x10139610`'s forward, with `_DAT_1044eb08` the degrees-to-radians scale:
	//     c1 = cos(a[1] * RAD), s1 = sin(a[1] * RAD);      // yaw
	//     c2 = cos(a[0] * RAD), s2 = sin(a[0] * RAD);      // pitch
	//     forward = (c2 * c1, c2 * s1, -s2);
	const float Pitch = AnglesDegrees.X * GMotor10DegToRad;
	const float Yaw = AnglesDegrees.Y * GMotor10DegToRad;
	return FVector(FMath::Cos(Pitch) * FMath::Cos(Yaw), FMath::Cos(Pitch) * FMath::Sin(Yaw),
		-FMath::Sin(Pitch));
}

FVector FElysiumNpc::GetShootTarget(const FVector& PosSrcUnits, bool bNoisy, bool bFlag2) const
{
	// `FUN_10278650`, in retail's order. See `ElysiumNpcKernelMotor10.inl` for why this is
	// `GetShootTarget` and not the standoff anchor the checklist's row named.

	// 1. `m_hShootTargetOverride` (+0x5ba8) resolving wins outright, and the three arguments are
	//    never looked at. The listing validates the handle TWICE against `PTR_DAT_10566458` — once
	//    for the gate, once to resolve — which is one redundant read and no behaviour.
	if (World != nullptr && ShootTargetOverride.IsSet())
	{
		if (const FElysiumEntity* Override = World->Resolve(ShootTargetOverride))
		{
			return Motor10SourceOf(Override->Origin);        // override->slot 217 GetAbsOrigin
		}
	}

	// 2. No enemy: the forward vector of this body's own angles, PLUS the source position.
	const FElysiumEntity* Enemy = GetEnemy();                // slot 167 (+0x29c)
	if (Enemy == nullptr)
	{
		const FVector ForwardUnits = AngleVectorsForward(RetailGetAnglesDegrees());
		return ForwardUnits + PosSrcUnits;
	}

	// 3. An enemy. `GetEnemies()` (slot 541, +0x874) then
	//    `CAI_Enemies::GetLastKnownPosition(&lkp, enemy)` (`0x102dfed0`). Family Positions already
	//    stands that pair as `EnemyLastKnownPosition`; it is called rather than answered twice.
	FVector LastKnownCm = FVector::ZeroVector;
	EnemyLastKnownPosition(LastKnownCm);
	const FVector LastKnownUnits = Motor10SourceOf(LastKnownCm);

	// 4. `enemy->slot 197 (+0x314) BodyTarget(posSrc, bNoisy, bFlag2)`.
	FVector BodyTargetUnits = Motor10SourceOf(
		const_cast<FElysiumNpc*>(this)->BodyTarget(Motor10PortOf(PosSrcUnits), bNoisy, bFlag2));

	// 5. The stat arm. `enemy->+0x9c` and its `+0xa8` must BOTH be non-null before the list walk
	//    runs at all; the walk then takes the first type-3 list, or the empty global, and asks it
	//    for stat `0xb`. An answer of exactly **5** raises the body target's Z by `_DAT_104994e0` =
	//    **-30.0** — a negative offset, so the target moves DOWN.
	if (EnemyTypedStatValue(*Enemy, 0xb) == 5)
	{
		BodyTargetUnits.Z += GMotor10ShootTargetZOffset;
	}

	// 6. `lkp + (bodyTarget - enemy->GetAbsOrigin())`.
	return LastKnownUnits + (BodyTargetUnits - Motor10SourceOf(Enemy->Origin));
}

// =================================================================================================
// `0x102984a0` — `CAI_BaseNPCTroika::OnObstructingDoor`, slot 531.
// =================================================================================================

int32 FElysiumNpc::RetailDoorToggleState(const FElysiumEntity& Door) const
{
	// **SEAM** for `door->+0x4f8 m_toggle_state` on an arbitrary door. This runtime's doors are
	// `FElysiumMover` and carry their own phase rather than Source's four-state toggle, so `0`
	// (`TS_AT_TOP` in retail's numbering, the rest state) is what a mover at rest answers.
	(void)Door;
	return GMotor10DoorToggleAtTop;
}

void FElysiumNpc::ClearDoorBlockFlags(FElysiumEntity& Door)
{
	// `0x100f0e70` — `door->+0x644 = 0`, eleven bytes and nothing else.
	FDoorBlockWrite Write;
	Write.Door = Door.Handle;
	Write.Bits = 0;
	Write.bClear = true;
	DoorBlockWrites.Add(Write);
}

void FElysiumNpc::AddDoorBlockFlags(FElysiumEntity& Door, uint32 Bits)
{
	// `0x100f0e90` — `door->+0x644 |= param_1`.
	FDoorBlockWrite Write;
	Write.Door = Door.Handle;
	Write.Bits = Bits;
	Write.bClear = false;
	DoorBlockWrites.Add(Write);
}

double FElysiumNpc::DoorNextTryTime(const FElysiumEntity& Door) const
{
	// **SEAM** for `door->+0x640`. Family Senses stands the WRITER (`SetDoorNextTryTime`) and
	// records that `FElysiumEntity` carries no such word; `0.0` is never above `curtime`.
	(void)Door;
	return 0.0;
}

bool FElysiumNpc::CanOpenDoorNow(FElysiumEntity* Door)
{
	// `FUN_1027f550`, ported in full.
	if (Door == nullptr)
	{
		// The null arm writes NO flag — `return in_EAX & 0xffffff00` and nothing else.
		return false;
	}
	if ((CapabilitiesGet() & GMotor10DoorCapabilityMask) != GMotor10DoorCapabilityMask)
	{
		AddDoorBlockFlags(*Door, GMotor10DoorBlockedNoCapability);   // 0x8
		return false;
	}
	const double NextTry = DoorNextTryTime(*Door);
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	// `FCOMP` with the equal bit carried: the refusal is a STRICT `>`, so a stamp exactly at
	// `curtime` passes.
	if (NextTry > Now)
	{
		AddDoorBlockFlags(*Door, GMotor10DoorBlockedRetryPending);   // 0x10
		return false;
	}
	return true;
}

bool FElysiumNpc::BuildLocalRouteThroughDoor(const FVector& FromUnits, const FVector& ToUnits,
	int32 RouteFlags)
{
	// **SEAM** for `CAI_Pathfinder::BuildLocalRoute` (`0x10304130`, VProf scope at `0x10611514`),
	// called as `(pathfinder, GetAbsOrigin(), &navPoint, 0, 0x30, -1, 1, 0.0, 0)`. Family Motor's
	// standing fact: no pathfinder and no node graph. Answering "no waypoint" is retail's own
	// NOT-FOUND arm, which is the one that reaches the door-type split below.
	(void)FromUnits;
	(void)ToUnits;
	(void)RouteFlags;
	++Motor10Seams.BuildLocalRouteAsks;
	return false;
}

bool FElysiumNpc::SplicePathWaypoint(int32 Waypoint)
{
	// **SEAM** for `thunk_FUN_10319f30(navigator->+0x30 + 0x24, waypoint)`. Unreachable while the
	// search above answers nothing; declared so the arm has a real call to make.
	(void)Waypoint;
	++Motor10Seams.SplicePathAsks;
	return false;
}

bool FElysiumNpc::OnObstructingDoor(void* MoveGoalBlock, FElysiumEntity* Door, float DistClear,
	void* ResultOut)
{
	// `CAI_BaseNPCTroika::FUN_102984a0` `0x102984a0`, slot 531 — the Troika-line body. The BASE
	// branch is `0x1027dc80` (family Motor's `OnObstructingDoorBase`) and neither calls the other.
	//
	// The generated signature types the first and fourth arguments `void*` because the slot table
	// types them `AILocalMoveGoal_t*` and `AIMoveResult_t*`, for which the generator has no port
	// type. `FLocalMoveGoal` and `int32` are those types.
	FLocalMoveGoal* Goal = static_cast<FLocalMoveGoal*>(MoveGoalBlock);
	int32* OutResult = static_cast<int32*>(ResultOut);

	// Arm 0: a NULL door.
	if (Door == nullptr)
	{
		// `DevMsg("\n\n**WARNING**\nNo door given to OnUpcomingDoor()\n**WARNING**\n\n")` — note
		// the retail NAME in the literal is `OnUpcomingDoor`, not `OnObstructingDoor`.
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("%s Door: **WARNING** No door given to OnUpcomingDoor() (0x102984a0)"),
			*DebugString());
		return false;
	}

	// The gate. `FLD [goal+0x28]; FCOMP distClear; TEST AH,5; JNP -> false` — the body runs when
	// `maxDist >= distClear`, and an UNORDERED compare (either side NaN) also runs it.
	if (Goal == nullptr)
	{
		// Retail would fault here. **A NAMED CRASH GUARD**, and the only divergence in this file:
		// the arm is unreachable from every retail caller, which always passes its own stack block.
		return false;
	}
	const bool bGateNaN = FMath::IsNaN(Goal->MaxDistanceUnits) || FMath::IsNaN(DistClear);
	if (!bGateNaN && !(Goal->MaxDistanceUnits >= DistClear))
	{
		return false;
	}

	// Arm 1: this is already the door we are holding (`m_hOpeningDoor`, **+0x5d24**).
	if (World != nullptr && OpeningDoor.IsSet() && World->Resolve(OpeningDoor) == Door)
	{
		if (OutResult != nullptr)
		{
			*OutResult = GMotor10AimrOk;
		}
		ClearDoorBlockFlags(*Door);                          // 0x100f0e70
		return true;
	}

	// Arm 2: the SQUAD's focus door. `m_iSquadDisconnected (+0x5bb0) <= 0 && m_pSquad (+0x5da4)` is
	// exactly `ConnectedSquad()`, and `0x103166b0` is `GetSquadFocus` — family Senses' seam.
	if (ConnectedSquad() != nullptr && SquadFocus() == Door)
	{
		if (OutResult != nullptr)
		{
			*OutResult = GMotor10AimrBlockedWorld;           // -2
		}
		AddDoorBlockFlags(*Door, GMotor10DoorBlockedBySquad);   // 0x100f0e90(door, 1)
		return true;
	}

	// Arm 3: a SCRIPTED body (slot 464 `GetState() == 4`) that does NOT carry the full `0xd00`
	// capability hands the door to the alternate AI.
	// `EElysiumNpcState::Scripted` IS retail's `NPC_STATE_SCRIPT` = 4; the mapping is family
	// Conditions' `CondRetailStateId`, and `GMotor10StateScript` names the retail number.
	static_assert(GMotor10StateScript == 4, "slot 464's arm compares against NPC_STATE_SCRIPT");
	if (GetState() == EElysiumNpcState::Scripted)
	{
		if ((CapabilitiesGet() & GMotor10DoorCapabilityMask) != GMotor10DoorCapabilityMask)
		{
			// `FLD distClear; FCOMP _DAT_10451acc (64.0f); TEST AH,5; JP` — a strict `<`.
			if (DistClear < GMotor10DoorAlternateAiClearance)
			{
				StopScheduledMove();                         // thunk_FUN_102bf770
				AlternateAi = 4;                             // +0x644c m_eAlternateAI
				const double Now = World != nullptr ? World->NowSeconds() : 0.0;
				AlternateAiExpireTime = Now + GMotor10DoorAlternateAiDuration;   // +0x6450
				OpeningDoor = Door->Handle;                // +0x5d24 = door->GetRefEHandle()
			}
			// Both paths out of the clearance test answer 0 / true.
			if (OutResult != nullptr)
			{
				*OutResult = GMotor10AimrOk;
			}
			ClearDoorBlockFlags(*Door);
			return true;
		}
	}

	// Arm 4: may this body open a door at all?
	if (!CanOpenDoorNow(Door))                               // thunk_FUN_1027f550
	{
		if (OutResult != nullptr)
		{
			*OutResult = GMotor10AimrBlockedWorld;           // -2
		}
		AddDoorBlockFlags(*Door, GMotor10DoorBlockedByPolicy);  // 0x100f0e90(door, 4)
		return true;
	}

	// Arm 5: `TEST AH,0xd` — ANY of the `0xd00` capability bits, not all of them. With none, the
	// body answers FALSE and writes nothing at all.
	if ((CapabilitiesGet() & GMotor10DoorCapabilityMask) == 0)
	{
		return false;
	}

	// Arm 6: ask the door where an NPC should stand to open it — `door->slot 246 (+0x3d8)(this,
	// &point, door->+0x4f8 == 2)`. Family Conditions stands that call as `OpeningDoorFacingPoint`
	// and it is called rather than seamed a second way. A refusal (`iStack_10 == -1`) writes the
	// RESULT pointer — **not** the move goal, which is what the decompiler's `*param_1 = 0` claims
	// and what the listing at `10298725` (`MOV EDX,[ESP+0x3c]`, i.e. argument 4) settles.
	const bool bWait = RetailDoorToggleState(*Door) == GMotor10DoorToggleGoingUp;
	FVector NavPointCm = FVector::ZeroVector;
	if (!OpeningDoorFacingPoint(*Door, bWait, NavPointCm))
	{
		if (OutResult != nullptr)
		{
			*OutResult = GMotor10AimrOk;
		}
		ClearDoorBlockFlags(*Door);
		return true;
	}

	// Arm 7: the pathfinder looks for a waypoint from this body's origin (slot 217) to that point,
	// with flags `0x30`, hull `-1`, `1`, `0.0` and `0`.
	const FVector OriginUnits = Motor10SourceOf(Origin);
	if (BuildLocalRouteThroughDoor(OriginUnits, Motor10SourceOf(NavPointCm), GMotor10DoorRouteFlags))
	{
		// FOUND: stamp the door handle into `waypoint+0x24` and splice it into the navigator's path
		// at `navigator->+0x30 + 0x24`.
		++BuildLocalRouteWaypoints;
		if (!SplicePathWaypoint(BuildLocalRouteWaypoints))
		{
			// A FAILED splice falls straight out with FALSE and no further write.
			return false;
		}
		OpeningDoor = Door->Handle;
		bOpeningDoorWait = RetailDoorToggleState(*Door) == GMotor10DoorToggleGoingUp;   // +0x5d30
		// **A CORRECTION**: the listing writes `[ECX+0x28] = [ESP+0x38]` where `ECX` is argument 1
		// (the MOVE GOAL) and `[ESP+0x38]` is argument 3 (`distClear`). The checklist's walk reads
		// this as "+0x28 of the block slot 0x3d8 returned"; it is the goal's own `maxDist`.
		Goal->MaxDistanceUnits = DistClear;
		if (OutResult != nullptr)
		{
			*OutResult = GMotor10AimrOk;
		}
		ClearDoorBlockFlags(*Door);
		return true;
	}

	// NOT FOUND. Door toggle states 0 and 2 give up quietly.
	const int32 ToggleState = RetailDoorToggleState(*Door);
	if (ToggleState == GMotor10DoorToggleAtTop || ToggleState == GMotor10DoorToggleGoingUp)
	{
		ClearDoorBlockFlags(*Door);
		return false;
	}

	// Any other state: take the door and try to start the open.
	OpeningDoor = Door->Handle;
	if (!StartOpeningDoor(*Door))                            // thunk_FUN_10298840
	{
		if (OutResult != nullptr)
		{
			*OutResult = GMotor10AimrBlockedWorld;           // -2
		}
		AddDoorBlockFlags(*Door, GMotor10DoorBlockedNoRoute);   // 0x100f0e90(door, 0x40)
		return true;
	}
	// SUCCESS answers FALSE and lets the door go again.
	OpeningDoor = FElysiumEntityHandle();                    // +0x5d24 = 0xffffffff
	ClearDoorBlockFlags(*Door);
	return false;
}
