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
#include "Substrate/ElysiumNpcFacingShared.h"
#include "Substrate/ElysiumMover.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcLog.h"

namespace
{
	// --- The recovered constants, by address ----------------------------------------------------

	// `_DAT_10451acc`, `00 00 80 42`: **64.0f**, the clearance below which a scripted NPC gives the
	// door to the alternate AI.
	constexpr float GMotor10DoorAlternateAiClearance = 64.0f;
	// `_DAT_10449258`, `00 00 40 40`: **3.0f**, how long that alternate-AI mode lives.
	constexpr float GMotor10DoorAlternateAiDuration = 3.0f;

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
// `0x102984a0` — `CAI_BaseNPCTroika::OnObstructingDoor`, slot 531.
// =================================================================================================

int32 FElysiumNpc::RetailDoorToggleState(const FElysiumEntity& Door) const
{
	// `door->+0x4f8 m_toggle_state`: `FElysiumDoorBase::EToggleState` carries retail's exact
	// numbering (0 AT_TOP, 1 AT_BOTTOM, 2 GOING_UP, 3 GOING_DOWN). A non-door answers AT_TOP, the
	// rest state arm 7 gives up quietly on (retail never hands slot 531 a non-door).
	const FElysiumDoorBase* DoorBase = const_cast<FElysiumEntity&>(Door).AsDoorBase();
	return DoorBase != nullptr ? static_cast<int32>(DoorBase->State()) : GMotor10DoorToggleAtTop;
}

void FElysiumNpc::ClearDoorBlockFlags(FElysiumEntity& Door)
{
	// `0x100f0e70` — `door->+0x644 = 0`, eleven bytes and nothing else. The write lands on the
	// door (`FElysiumDoorBase::ClearNpcFailedFlags`); the trace row stays for the suites that
	// assert the call order.
	FDoorBlockWrite Write;
	Write.Door = Door.Handle;
	Write.Bits = 0;
	Write.bClear = true;
	DoorBlockWrites.Add(Write);
	if (FElysiumDoorBase* DoorBase = Door.AsDoorBase())
	{
		DoorBase->ClearNpcFailedFlags();
	}
}

void FElysiumNpc::AddDoorBlockFlags(FElysiumEntity& Door, uint32 Bits)
{
	// `0x100f0e90` — `door->+0x644 |= param_1` (`FElysiumDoorBase::AddNpcFailedFlags`), traced.
	FDoorBlockWrite Write;
	Write.Door = Door.Handle;
	Write.Bits = Bits;
	Write.bClear = false;
	DoorBlockWrites.Add(Write);
	if (FElysiumDoorBase* DoorBase = Door.AsDoorBase())
	{
		DoorBase->AddNpcFailedFlags(Bits);
	}
}

double FElysiumNpc::DoorNextTryTime(const FElysiumEntity& Door) const
{
	// `door->+0x640` (`FElysiumDoorBase::NpcFailedTimer`): MAX-written by `0x100f0e30` (family
	// Senses' `SetDoorNextTryTime`), zeroed by `DoorHitTop`. A non-door answers 0.0, never above
	// `curtime`.
	const FElysiumDoorBase* DoorBase = const_cast<FElysiumEntity&>(Door).AsDoorBase();
	return DoorBase != nullptr ? DoorBase->NpcFailedTimer : 0.0;
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
	// `CAI_Pathfinder::BuildLocalRoute` (`0x10304130`, VProf scope at `0x10611514`), called as
	// `(pathfinder, GetAbsOrigin(), &standPos, 0, 0x30, -1, 1, 0.0, 0)` -- the goal is
	// `GetNPCOpenData`'s `StandPos` (findings § 1), the end flags `0x30` (`bits_WP_TO_DOOR |
	// bits_WP_DONT_SIMPLIFY`, which `SpliceDoorWaypoint` stamps on the waypoint).
	//
	// NAMED MODERNIZATION (0018/7): the NavMesh route stands for the local route. The body's own
	// agent is asked for a complete route from where it stands to the stand point
	// (`IElysiumNpcMotor::QueryRoute`, the default filter -- the local route prices nothing); a
	// route found is retail's FOUND arm, and no route (or no mesh to ask) its NOT-FOUND arm.
	// `FromUnits` is the body's own origin, which is where the query starts.
	(void)FromUnits;
	(void)RouteFlags;
	++Motor10Seams.BuildLocalRouteAsks;
	if (Motor == nullptr)
	{
		return false;
	}
	FElysiumNpcRouteQuery Query;
	Query.DestCm = FVector(ToUnits.X * ElysiumMove::U, -ToUnits.Y * ElysiumMove::U, ToUnits.Z * ElysiumMove::U);
	FElysiumNpcRouteAnswer Answer;
	return Motor->QueryRoute(Query, Answer) && Answer.bReachable;
}

bool FElysiumNpc::SplicePathWaypoint(int32 Waypoint)
{
	// The index form the generated signature carried. The splice slot 531 makes is
	// `SpliceDoorWaypoint` (the waypoint's position and door, which an index cannot carry); this
	// form is kept, counted and answering false, for the kernel suite that pins it.
	(void)Waypoint;
	++Motor10Seams.SplicePathAsks;
	return false;
}

bool FElysiumNpc::SpliceDoorWaypoint(const FVector& StandPosCm, const FElysiumEntity& Door)
{
	// `0x10319f30(path+0x24 slot, newRoute)`: `0x1031a0e0(newRoute, oldHead)` walks to the new
	// route's last waypoint, clears its goal bit (8) and links the old chain after it (a same-node
	// merge needs a node; the door waypoint's `wp+0x10` is -1, so it never merges); then the new
	// route is the head (`0x10319fe0`). The local route to a stand point is the one waypoint.
	++Motor10Seams.SplicePathAsks;
	constexpr int32 DoorWaypointFlags = 0x30;                                // WP_TO_DOOR | DONT_SIMPLIFY
	constexpr int32 GoalWaypointFlag = 0x8;
	TArray<FPedestrianLeg> Chain;
	FPedestrianLeg DoorLeg;
	DoorLeg.DestCm = StandPosCm;
	DoorLeg.Flags = DoorWaypointFlags;
	DoorLeg.Door = Door.Handle;                                              // wp+0x24
	Chain.Add(DoorLeg);
	if (PedestrianLegs.Num() > 0)
	{
		Chain.Append(PedestrianLegs);
	}
	else if (Navigator.bHasHeadWaypoint)
	{
		// A route with no leg list: its one head is the leg the body walks.
		FPedestrianLeg Head;
		Head.DestCm = Navigator.bHeadLegRequestSet ? Navigator.HeadLegRequest.DestinationCm : Navigator.GetGoalPos();
		Head.Flags = Navigator.bHeadIsGoal ? GoalWaypointFlag : 0;
		Chain.Add(Head);
	}
	PedestrianLegs = MoveTemp(Chain);
	Navigator.bHasHeadWaypoint = true;
	Navigator.bHeadIsGoal = PedestrianLegs.Num() == 1;
	// The leg to the stand point, with the route's own request words re-aimed. A refused leg leaves
	// the head standing with no request; the next move step reads it as the body giving up.
	if (Navigator.bHeadLegRequestSet)
	{
		FElysiumNpcMoveRequest Leg = Navigator.HeadLegRequest;
		Leg.DestinationCm = StandPosCm;
		bMoveIssued = NavIssueLeg(Leg);
	}
	return true;
}

void FElysiumNpc::NavAdvanceDoorWaypoint()
{
	// `0x102f0400`, after `0x102a0bc0`: `if (wp+0x28 & 0x10)`.
	constexpr int32 DoorWaypointFlag = 0x10;
	if (PedestrianLegs.Num() == 0 || (PedestrianLegs[0].Flags & DoorWaypointFlag) == 0)
	{
		return;
	}
	FElysiumEntity* const DoorEntity = World != nullptr ? World->Resolve(PedestrianLegs[0].Door) : nullptr;
	if (DoorEntity == nullptr)
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s trying to open a door that has been removed"),
			*DebugString());                                                 // 0x10610080 DevMsg
		return;
	}
	FElysiumDoorBase* const Door = DoorEntity->AsDoorBase();                 // +0xa4
	if (Door == nullptr)
	{
		return;
	}
	if (CanOpenDoorNow(DoorEntity)                                           // 0x1027f550
		&& Door->State() == FElysiumDoorBase::EToggleState::AtBottom)        // +0x4f8 == 1
	{
		EnterAlternateAi();                                                  // 0x10298800
	}
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
	// &data, door->+0x4f8 == 2)` (`FElysiumDoorBase::GetNPCOpenData`, the whole struct). A sliding
	// door (`0x100f0ef0`) always answers -1. A refusal (`data.Activity == -1`) writes the RESULT
	// pointer — **not** the move goal, which is what the decompiler's `*param_1 = 0` claims and what
	// the listing at `10298725` (`MOV EDX,[ESP+0x3c]`, i.e. argument 4) settles.
	const bool bWait = RetailDoorToggleState(*Door) == GMotor10DoorToggleGoingUp;
	const FElysiumDoorBase* DoorBase = Door->AsDoorBase();
	const FElysiumDoorNpcOpenData OpenData = DoorBase != nullptr
		? DoorBase->GetNPCOpenData(this, bWait) : FElysiumDoorNpcOpenData();
	if (OpenData.Activity == INDEX_NONE)
	{
		if (OutResult != nullptr)
		{
			*OutResult = NpcKernelMotor10Shared::GMotor10AimrOk;
		}
		ClearDoorBlockFlags(*Door);
		return true;
	}

	// Arm 7: the pathfinder looks for a waypoint from this body's origin (slot 217) to the open
	// data's `StandPos` (`1029864c`), with flags `0x30`, hull `-1`, `1`, `0.0` and `0`.
	const FVector OriginUnits = NpcKernelMotor10Shared::Motor10SourceOf(Origin);
	if (BuildLocalRouteThroughDoor(OriginUnits, NpcKernelMotor10Shared::Motor10SourceOf(OpenData.StandPosCm), GMotor10DoorRouteFlags))
	{
		// FOUND: stamp the door handle into `waypoint+0x24` and splice it into the navigator's path
		// at `navigator->+0x30 + 0x24` (`102986da`). Retail's splice answers 1 always; a failed one
		// would fall straight out with FALSE and no further write.
		++BuildLocalRouteWaypoints;
		if (!SpliceDoorWaypoint(OpenData.StandPosCm, *Door))
		{
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

// =================================================================================================
// `0x102e19e0` — `CAI_Motor` slot 18, the SDK's `MoveFacing`; and the facing step of its one caller,
// `CAI_HumanoidMotor` vfunc 19 `0x10264680` (`MoveGroundExecute`). Spec 0002 V4b, packet R1 item 1.
// =================================================================================================

bool FElysiumNpc::MotorSequenceHasMoveYaw() const
{
	// `0x102e2820(motor, m_nSequence (+0x6f0), "move_yaw")` (`102e1a33`). The studio header's pose
	// parameter table is the bridge row's fan here: the one pose parameter a baked sequence binds.
	const FSequenceDescriptorRow* const Row = SequenceDescriptorRow(SequenceNumber);
	return Row != nullptr && Row->FanCells >= 2
		&& Row->FanParameter.Equals(TEXT("move_yaw"), ESearchCase::IgnoreCase);
}

void FElysiumNpc::MotorMoveReissueYaw(float YawDegrees)
{
	// `0x102e1c10(yaw, -1.0)` (`102e1a6a`, `102e1b2d`; the speed is the pushed `0xbf800000`). The
	// same words `MotorSetIdealYawAndUpdate` writes for a task: the `+0x28` animation-movement latch
	// turns the yaw half a turn, the `+0x1c == 180.0` arm stores it directly (the clamped arm
	// `0x102e0a80` needs a max-yaw word the port motor does not carry).
	float Ideal = YawDegrees;
	if (BaseScheduleHost.bMotorAnimationMovement)
	{
		Ideal = Ideal < ElysiumNpcTunables::OneEighty ? Ideal + ElysiumNpcTunables::OneEighty
			: Ideal - ElysiumNpcTunables::OneEighty;
	}
	MotorIdealYaw = Ideal;                                                   // motor+0x34
	++TroikaMotor.MoveReissues;
	TroikaMotor.LastReissueYaw = Ideal;
	TroikaMotor.LastReissueSpeed = -1.0f;
	// The speed is `-1.0`: `0x102e1cf0` re-reads slot 516 `MaxYawSpeed` into `motor+0x38`, every
	// step. `MotorThinkUpkeep` hands a changed word to the travelling body (`SetYawSpeed`).
	MotorStoreMaxYawSpeed();                                                 // 0x102e1cf0
	// `UpdateYaw(-1)` `0x102e1e20`: `m_flLastYawTime (motor+0x2c) = curtime`. Its `SetLocalAngles`
	// is the body's own turn while it travels (K1: the kernel computes the words, the body moves --
	// it orients along its path, or to the facing point `MotorHandFacingTarget` hands it, at the
	// `+0x38` rate). `Face` is the turn-in-place request and is not issued under a live move.
	MotorYawClock = static_cast<float>(World != nullptr ? World->NowSeconds() : 0.0);
}

void FElysiumNpc::MotorMoveFacing(const FMotorMoveFacingGoal& Move)
{
	// `102e19f9`: owner slot 526 (`+0x838`) `OverrideMoveFacing(move, m_flMoveInterval (motor+0x30))`
	// true -> return. Slot 526 is `CAI_BaseNPC::OverrideMoveFacing 0x1027d9f0` on all 77 classes that
	// fill it, no override, and its body returns false (packet S5 item 1): a constant false, so the
	// test never returns early and nothing is asked here.

	// `102e1a0c`: `flMoveYaw = UTIL_VecToYaw(move.dir)` (`0x101d2c70` over `move+0x0c`).
	const float MoveYaw = NpcKernelFacingShared::RetailVecToYaw(Move.Dir);
	// `102e1a24`: `0x102e2790(motor, m_nSequence)`, the sequence's own move yaw -- discarded
	// (`102e1a31 FSTP ST0`).
	if (!MotorSequenceHasMoveYaw())                                          // 102e1a33 0x102e2820
	{
		// `102e1a3d..102e1a6a`: `SetIdealYawAndUpdate(AngleMod(flMoveYaw), -1)`. `AngleMod` is the
		// 16-bit quantisation (`x 0x1044ffe0`, `& 0xffff`, `x 0x1044ffdc`). Done.
		MotorMoveReissueYaw(StartTaskAngleMod(MoveYaw));
		return;
	}
	// `102e1a80`: the motor's own slot 15 (`+0x3c`, `0x102e2180`): the facing queue's direction and,
	// in ST0, its influence `w`. An empty queue answers the zero vector and 0.0.
	float Influence = 0.f;
	double RangeCm = 0.0;
	const FVector FacingDir = MotorFacingQueueBlend(Influence, RangeCm);
	// `102e1a83..102e1ae6`: `dir = facingDir * w + move.facing (move+0x18) * (1 - w)` (the 1.0 is
	// the float `0x104454c0`); `102e1aea` `VectorNormalize`. With an empty queue it is `move.facing`,
	// whole.
	FVector Dir = FacingDir * Influence + Move.Facing * (ElysiumNpcTunables::One - Influence);
	Dir = Dir.GetSafeNormal();                                               // zero stays zero
	// `102e1af7..102e1b2d`: `SetIdealYawAndUpdate(AngleMod(UTIL_VecToYaw(dir)), -1)`.
	MotorMoveReissueYaw(StartTaskAngleMod(NpcKernelFacingShared::RetailVecToYaw(Dir)));
	// `102e1b37` owner slot 221 `GetAngles().y`; `102e1b4c` `UTIL_AngleDiff(flMoveYaw, yaw)`
	// (`0x1013d580`); `102e1b5d FCHS`: the MOVE direction against the body's yaw, negated -- not the
	// heading.
	const float Diff = NpcKernelFacingShared::FacingRetailAngleDiff(MoveYaw, static_cast<float>(Angles.Y));
	// `102e1b57`: `owner+0x98`, the Troika self-cast, resolves on every `FElysiumNpc` (this class):
	// `102e1b6c` `m_flDesiredMoveYaw (+0x63ec) = -diff`. Its one reader is `0x102bf310`
	// (`Think19NormalSet2`), which writes the `move_yaw` pose parameter from it. The other arm
	// (`102e1b85`, `0x102e27d0` `SetPoseParameter("move_yaw")` on a base-only NPC) has no class here.
	ScheduleHost.DesiredMoveYaw = -Diff;
}

void FElysiumNpc::MotorMoveGroundExecuteFacing()
{
	// `CAI_HumanoidMotor` vfunc 19 `0x10264680` copies the move (0x1f dwords), overwrites the copy's
	// `facing` (`+0x18`) with `UTIL_YawToVector(yaw)` (`0x101d2f40`) and calls slot 18 on the copy
	// (`1026482f CALL [EDX+0x48]`).
	FMotorMoveFacingGoal Move;

	// `move.dir` (`move+0x0c`): the unit direction the step travels. The body moves (K1), so the
	// step's direction is the body's own velocity; a body that has not started moving yet travels
	// toward the waypoint it was sent to (the next path corner where the follower names one, else the
	// head leg's destination).
	if (Motor != nullptr)
	{
		const FVector BodyVelocity = Motor->SampleNavigation().VelocityCmPerSecond;
		Move.Dir = FVector(BodyVelocity.X, BodyVelocity.Y, 0.0).GetSafeNormal();
	}
	if (Move.Dir.IsZero())
	{
		const FVector TargetCm = (bNavLastFactsValid && NavLastFacts.bHasNextCorner)
			? NavLastFacts.NextCornerCm
			: (Navigator.bHeadLegRequestSet ? Navigator.HeadLegRequest.DestinationCm : Navigator.GetGoalPos());
		Move.Dir = (TargetCm - Origin).GetSafeNormal();
	}

	// The yaw: the turn script (`owner+0x6038`, count `+0x6044`; `0x102627e0`: the direction from
	// each waypoint to the next, eased through corners) interpolated at `m_flMoveInterval`, or with
	// one entry or none the current yaw (`GetLocalAngles().y`, owner `+0x36c`); `AngleMod`-quantised.
	// The path is the body's (K1), so the script's answer is asked of it; the seam's yaw is THIS
	// world's (the frame `IElysiumNpcMotor::Face` takes), negated back into retail's.
	float Yaw = static_cast<float>(Angles.Y);
	float BodyYaw = 0.f;
	if (Motor != nullptr && Motor->GetNpcMoveFacingYaw(BodyYaw))
	{
		Yaw = -BodyYaw;
	}
	Yaw = StartTaskAngleMod(Yaw);
	// `UTIL_YawToVector` `0x101d2f40`: `(cos, sin, 0)` in Source's axes; Y reflected into this
	// world's.
	const double YawRad = FMath::DegreesToRadians(static_cast<double>(Yaw));
	Move.Facing = FVector(FMath::Cos(YawRad), -FMath::Sin(YawRad), 0.0);

	MotorMoveFacing(Move);                                                   // 1026482f slot 18

	// `10264841 CALL GetSequenceGroundSpeed(m_nSequence)` / `10264846 FSTP [ESI+0x654]`: the second
	// `m_flGroundSpeed` write, right after slot 18 -- the same pose-weighted read `StudioFrameAdvance`
	// makes (only this word; `+0x560` is not rewritten here).
	GroundSpeed = SequenceGroundSpeedAt(SequenceNumber, PoseParameterWrites);
}
