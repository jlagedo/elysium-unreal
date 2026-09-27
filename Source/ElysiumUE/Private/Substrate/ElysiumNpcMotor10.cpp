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
//     This mirrors `ElysiumNpcMotor.cpp`'s own convention exactly — two copies of a two-line
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
//     short-circuits to hull 0 without the 22-miss fallback. (The body is now its own class's,
//     `FElysiumNpcTestHull::Spawn`, story 5 fold A1.)
//   * `CAI_Motor+0x3c` is `m_vecVelocity` (the datamap says so), not a facing-queue count.
//   * `_DAT_1044e658` is the **double 0.01** and `_DAT_104994e0` is **-30.0f**, both read out of
//     the pinned image; the oracle had the first listed as unrecovered.

#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcMotor10Shared.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"

namespace
{
	// --- The recovered constants, by address ----------------------------------------------------

	// `_DAT_10451acc`, `00 00 80 42`: **64.0f**, the clearance below which a scripted NPC gives the
	// door to the alternate AI.
	constexpr float GMotor10DoorAlternateAiClearance = 64.0f;
	// `_DAT_10449258`, `00 00 40 40`: **3.0f**, how long that alternate-AI mode lives.
	constexpr float GMotor10DoorAlternateAiDuration = 3.0f;

	// `0x10601e9c` and its `"no weapon"` fallback at `0x10601edc`.
	const TCHAR* const GMotor10LowAimWarning =
		TEXT("NPC in standoff lacks needed low aim activity (%s)");

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
	constexpr int32 GMotor10AimrBlockedWorld = -2;
}

// =================================================================================================
// The `CAI_Motor` seams.
// =================================================================================================

// =================================================================================================
// The `CAI_Navigator` seams.
// =================================================================================================

// =================================================================================================
// `0x102efd50` — `CAI_Navigator::MoveNormal`'s gate.
// =================================================================================================

// =================================================================================================
// `0x102efaa0` — `CAI_Navigator::MoveNormal`, `CAI_Navigator#12`.
// =================================================================================================

// =================================================================================================
// `0x102e14a0` / `0x102e1560` / `0x102e1760` — the two `CAI_Motor` step bodies.
// =================================================================================================

// =================================================================================================
// `0x10273070` / `0x10273180` — the two hull-size bodies.
// =================================================================================================

// =================================================================================================
// `0x102c87a0` / `0x102c8830` / `0x102cdc50` — `CAI_StandoffGoal`, the goal entity.
// =================================================================================================

// =================================================================================================
// `0x102c79e0` — `CAI_StandoffBehavior#22`, the activity translation.
// =================================================================================================

// =================================================================================================
// `0x10278650` — `CAI_BaseNPC::GetShootTarget`.
// =================================================================================================

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
			*OutResult = NpcKernelMotor10Shared::GMotor10AimrOk;
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
				*OutResult = NpcKernelMotor10Shared::GMotor10AimrOk;
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
			*OutResult = NpcKernelMotor10Shared::GMotor10AimrOk;
		}
		ClearDoorBlockFlags(*Door);
		return true;
	}

	// Arm 7: the pathfinder looks for a waypoint from this body's origin (slot 217) to that point,
	// with flags `0x30`, hull `-1`, `1`, `0.0` and `0`.
	const FVector OriginUnits = NpcKernelMotor10Shared::Motor10SourceOf(Origin);
	if (BuildLocalRouteThroughDoor(OriginUnits, NpcKernelMotor10Shared::Motor10SourceOf(NavPointCm), GMotor10DoorRouteFlags))
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
			*OutResult = NpcKernelMotor10Shared::GMotor10AimrOk;
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
