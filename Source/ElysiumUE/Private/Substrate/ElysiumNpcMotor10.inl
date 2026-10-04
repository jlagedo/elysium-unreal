// Story 29d, family **Motor10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual and this family only defines it. Exactly ONE of this
// family's twelve rows is such a slot — `0x102984a0`, slot 531 `OnObstructingDoor`. The other
// eleven fill slots on **their own object's** vtable (`CAI_Motor`'s 21-slot table, `CAI_Navigator`'s
// 18-slot table, `CAI_StandoffBehavior`'s 29-slot behaviour table, `CAI_StandoffGoal`'s 246-slot
// `CBaseEntity`-line goal entity) or no slot at all, so their port names are coined on
// `FElysiumNpc` and that is correct — they are not NPC slots and are never spelled `hand:`.
//
// The definitions are in `Substrate/ElysiumNpcMotor10.cpp` and the tests in
// `Tests/ElysiumNpcKernelMotor10Tests.cpp`. The walked prose is
// `docs/vtmb/npc-ai/schedule-kernel.md` § "Story 29d, family Motor10".
//
// This family EXTENDS story 29c-1's family **Motor** (`ElysiumNpcMotor.inl`) rather than
// standing a second set of motor seams: `Navigator`, `MotorSeams`, `KernelHullTrace`,
// `RetailHullExtents`, `HullKind`, `OnObstructingDoorBase`, `ResumeScheduledMove` and
// `MotorMinStoppingDistanceUnits` are all that family's and are called, not restated. It also
// reuses family **TroikaHelpers**' `TroikaMotor.MoveInterval` (`CAI_Motor+0x30`), its
// `FUN_102e19e0` (`CAI_Motor#18 MoveFacing`) and its `FUN_102eee40` (`CAI_Navigator#17`), family
// **Lifecycle**'s `FStandoffWords`, family **Conditions**' `OpeningDoorFacingPoint` /
// `StartOpeningDoor`, family **Senses**' `SquadFocus` / `DoorBlockFlags`, family **Positions**'
// `EnemyLastKnownPosition`, and family **TroikaHelpers**' `StopScheduledMove`.
//
// THE STANDING FACT OF THIS FAMILY is family Motor's, unchanged: **this substrate has no
// navigator, no node graph, no move probe, no hull table and no path object.** Every retail input
// of that kind is asked through a named accessor that answers NOTHING and cites the retail call,
// and where retail's own refusal arm is the admitting one the seam answers the admitting value so
// nothing is silently refused.

// --- `CAI_TestHull`, the hull probe ----------------------------------------------------------------
//
// `CAI_TestHull::Spawn` `0x102d72f0` is its own class's slot 103, `FElysiumNpcTestHull::Spawn`
// (`Substrate/ElysiumNpcTestHull.h`, story 5 fold A1). The used-hull mask it reads is the free
// functions of `Substrate/ElysiumNpcUsedHullBits.h`; the solid record it writes is `FElysiumEntity`'s
// `m_Collision` seam (`ElysiumEntitySlotBodies.inl`).

// --- The two hull-size bodies -----------------------------------------------------------------------

// --- The non-slot bodies of this family -------------------------------------------------------------

/** `CAI_BaseNPCTroika::OnObstructingDoor` `0x102984a0`, slot 531 — the Troika-line body. The base
 *  branch (`0x1027dc80`) is family Motor's `OnObstructingDoorBase` and is a DIFFERENT body at the
 *  same slot on the `CAI_BaseNPC` line; neither calls the other.
 *
 *  The generated virtual's signature is the generator's — `bool OnObstructingDoor(void*,
 *  FElysiumEntity*, float, void*)` — because the slot table types the first and fourth arguments as
 *  `AILocalMoveGoal_t*` and `AIMoveResult_t*`, which the generator has no port type for. The body
 *  casts them to `FLocalMoveGoal*` and `int32*`. */

/** SEAM for `door->+0x4f8 m_toggle_state` on an ARBITRARY door — the word arms 5 and 7 of
 *  `OnObstructingDoor` read. Distinct from the seam a parallel family stands for the door this NPC
 *  is already HOLDING (`m_hOpeningDoor`): this one is asked of the door the move goal ran into, and
 *  the two questions have different answers the day the mover's phase is mapped. **SEAM**: this
 *  runtime's doors are `FElysiumMover` and carry a phase rather than Source's four-state toggle;
 *  answers **0**, which is the state arm 7 treats as "give up quietly". */
int32 RetailDoorToggleState(const FElysiumEntity& Door) const;

/** `0x100f0e70(door)` — `door->+0x644 = 0`, clearing the door's NPC-block flag word, and
 *  `0x100f0e90(door, bits)` — `door->+0x644 |= bits`. Family **Senses** stands the READER of that
 *  word (`DoorBlockFlags`) for `OnDoorBlocked`; these are its two WRITERS and are named separately
 *  because they are two different retail functions. **SEAM**: `FElysiumEntity` carries no such
 *  word, so the writes are recorded per door handle and the four bit values this body passes
 *  (`0x1`, `0x4`, `0x40`) are the recovered half. */
struct FDoorBlockWrite
{
	FElysiumEntityHandle Door;
	uint32 Bits = 0;       // 0 for the CLEAR (`0x100f0e70`)
	bool bClear = false;
};
mutable TArray<FDoorBlockWrite> DoorBlockWrites;
void ClearDoorBlockFlags(FElysiumEntity& Door);
void AddDoorBlockFlags(FElysiumEntity& Door, uint32 Bits);

/** `thunk_FUN_1027f550(this, door)` `0x1027f550` — "may I open this door at all?", the gate arm 6
 *  of `OnObstructingDoor` refuses on. Ported in full; its two inputs are slot 513
 *  `CapabilitiesGet` and the door's own retry stamp at `+0x640`:
 *
 *      if (!door) return false;                                  // and NO flag write
 *      if ((CapabilitiesGet() & 0xd00) != 0xd00) { door->+0x644 |= 0x8;  return false; }
 *      if (door->+0x640 > curtime)               { door->+0x644 |= 0x10; return false; }
 *      return true;
 *
 *  The retry stamp is family Senses' `+0x640` seam, which answers 0 and therefore never blocks. */
bool CanOpenDoorNow(FElysiumEntity* Door);

/** SEAM for `door->+0x640` — the "do not try me again before" stamp `CanOpenDoorNow` compares
 *  against `curtime`. Family Senses stands the WRITER (`SetDoorNextTryTime`); this is the read, and
 *  it answers **0.0**, which is never above `curtime` and therefore never refuses. */
double DoorNextTryTime(const FElysiumEntity& Door) const;

/** SEAM for `thunk_FUN_10304130(m_pPathfinder, origin, &point, 0, 0x30, -1, 1, 0.0, 0)` —
 *  `CAI_Pathfinder::BuildLocalRoute` (the VProf scope names it at `0x10611514`), the node search arm
 *  8 of `OnObstructingDoor` runs to find a waypoint through the door. **SEAM**: family Motor's
 *  standing fact — no pathfinder, no node graph — so this answers **null**, which is retail's own
 *  NOT-FOUND arm and is the one that reaches the door-type split. Counted. */
int32 BuildLocalRouteWaypoints = 0;
bool BuildLocalRouteThroughDoor(const FVector& FromUnits, const FVector& ToUnits, int32 RouteFlags);

/** SEAM for `thunk_FUN_10319f30(navigator->+0x30 + 0x24, waypoint)` — splice the waypoint the
 *  search found into the navigator's live path, answering whether the splice took. Unreachable
 *  while `BuildLocalRouteThroughDoor` answers null; declared so the arm above it has a real call to
 *  make the day a path object stands, and answers **false**. */
bool SplicePathWaypoint(int32 Waypoint);

// --- The two `CAI_Motor` step bodies and `CAI_Navigator#12` ------------------------------------------

// --- `CAI_Motor` slot 18 `MoveFacing` `0x102e19e0` and its caller's facing step (0002 V4b) -----------

/** The two `AILocalMoveGoal_t` words slot 18 reads: `dir` (`move+0x0c`, the unit direction the step
 *  travels) and `facing` (`move+0x18`, written by `MoveGroundExecute 0x10264680` on its copy of the
 *  move as `UTIL_YawToVector(yaw)`). THIS world's axes (Y negated), as slot 15's blend is; the yaw
 *  helpers read them back into retail's frame. */
struct FMotorMoveFacingGoal
{
	FVector Dir = FVector::ZeroVector;      // move+0x0c
	FVector Facing = FVector::ZeroVector;   // move+0x18
};

/** `0x102e2820(motor, m_nSequence, "move_yaw")`: does the playing sequence carry the `move_yaw` pose
 *  parameter? The bridge row's fan binds it (`FSequenceDescriptorRow::FanParameter`); a row with no
 *  fan, or none at all, answers false. */
bool MotorSequenceHasMoveYaw() const;

/** `0x102e1c10(yaw, -1.0)` as slot 18 issues it, the kernel's words: the ideal yaw (`motor+0x34`,
 *  through the `+0x28` half-turn latch), `MaxYawSpeed` re-read into `motor+0x38` (`0x102e1cf0`) and
 *  the yaw clock's stamp (`motor+0x2c`, `UpdateYaw 0x102e1e20`). The turn itself is the travelling
 *  body's (K1): it is not asked to `Face` while its move request is live. */
void MotorMoveReissueYaw(float YawDegrees);

/** `CAI_Motor` slot 18 `0x102e19e0`, the SDK's `MoveFacing`. Without `move_yaw` on the sequence the
 *  ideal yaw is `AngleMod(VecToYaw(move.dir))`. With it, the heading is `normalize(facingDir * w +
 *  move.facing * (1 - w))` (`facingDir`, `w` from slot 15 `0x102e2180`), the ideal yaw its quantised
 *  yaw, and `m_flDesiredMoveYaw (+0x63ec) = -UTIL_AngleDiff(VecToYaw(move.dir), GetAngles().y)`. */
void MotorMoveFacing(const FMotorMoveFacingGoal& Move);

/** The facing step of `CAI_HumanoidMotor` vfunc 19 `0x10264680` (`MoveGroundExecute`), slot 18's one
 *  caller: `move.facing = UTIL_YawToVector(yaw)` with `yaw` the turn script's answer (the body's,
 *  `IElysiumNpcMotor::GetNpcMoveFacingYaw`) or the current yaw, `AngleMod`-quantised; slot 18; then
 *  `m_flGroundSpeed (+0x654) = GetSequenceGroundSpeed(m_nSequence)` (`0x10264841/46`). The step
 *  itself (the velocity script, `MoveGroundStep 0x102e0bd0`) is the body's (K1). */
void MotorMoveGroundExecuteFacing();

