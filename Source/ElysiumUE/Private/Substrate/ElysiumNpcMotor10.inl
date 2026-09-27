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

// --- `AILocalMoveGoal_t`, the block the two `CAI_Motor` step bodies read --------------------------

// --- The `CAI_Motor` seams this family adds ------------------------------------------------------

// --- The `CAI_Navigator` seams this family adds --------------------------------------------------

/** `thunk_FUN_102ee3f0(navigator)` = `navigator->+0x30->+0x2c` — the MOVEMENT activity of the
 *  current route, which `MoveNormal` pushes through owner slot 310 `SetActivity` before it enacts.
 *  Family TroikaHelpers stands `NavCurrentLinkActivity` over the same retail call for
 *  `StopScheduledMove`; this calls THAT rather than adding a second answer to one question. */

// --- `CAI_StandoffGoal`, the goal ENTITY -----------------------------------------------------------
//
// Three of this family's rows fill slots on `CAI_StandoffGoal`'s own 246-slot `CBaseEntity`-line
// table — 180 `UpdateOnRemove`, 241 `InputActivate`, 243 `InputDeactivate`. **There is no
// `ai_goal_standoff` entity in this runtime**, exactly as there is no `CAI_StandoffBehavior`, so
// these land the way family Lifecycle landed `StandoffSelect`: the goal's own words as a typed view
// and the three bodies as PURE statics over it. Every threshold and every arm is then exercised
// without inventing a goal-entity store.

// --- `CAI_StandoffBehavior#22`, the activity translation -------------------------------------------

// --- `CAI_TestHull`, the hull probe ----------------------------------------------------------------
//
// **NAMED DECISION, on the spelling.** A reviewer suggested `FElysiumAiTestHull::Spawn` rather than
// a method on `FElysiumNpc`. This family keeps it on `FElysiumNpc` as `TestHullSpawn`, on three
// facts. (1) `CAI_TestHull` is a class on the `CAI_BaseNPC` line in retail — `vtmb_slot 464` and
// `vtmb_slot 513` list it filling `CAI_BaseNPC::GetState` and `CAI_BaseNPC::CapabilitiesGet`
// alongside every `CNPC_V*` leaf — so its `Spawn` is an NPC-line body and `FElysiumNpc` is the class
// that carries NPC-line bodies. (2) The port ALREADY treats it as one: story 29c-1's
// `FJumpTunableSpecies` table carries a `CAI_TestHull` row (`0x102d72d0`, a 1024/1024/1024 hull) and
// `MotorTests.Tunables` exercises it by name. (3) A new `FElysiumAiTestHull` would be a substrate
// class with one method, no spawner, no caller and no state of its own, which is the "second owner
// for one body" 29c-1 declined for `FNavigator`. The overlay row therefore keeps
// `FElysiumNpc::TestHullSpawn` and this comment is the argument.

static void RetailClearUsedHullBits();
static void RetailAddUsedHullBits(int32 Bits);

/** `CAI_TestHull::Spawn`'s hull pick, `0x102d72f5`–`0x102d732e`, as a PURE function so every arm is
 *  reachable from a test without a hull table. `HullBits` is `NAI_Hull::Bits`.
 *
 *  Retail, from the listing: read the used mask; **`TEST mask,mask; JLE`** — a SIGNED test, so a
 *  zero OR NEGATIVE mask short-circuits straight to hull 0 WITHOUT the fallback call; otherwise
 *  walk `i = 0 .. 21` and take the first `i` whose `NAI_Hull::Bits(i)` intersects the mask; after
 *  22 misses call `AddUsedHullBits(0)` — which ORs zero and is a no-op — and answer 0. */
static int32 TestHullPickHull(int32 UsedHullBits, TFunctionRef<int32(int32)> HullBits,
	bool& bOutTookFallback);

/** `CAI_TestHull::Spawn` `0x102d72f0`, slot 103 on `CAI_TestHull`. */
void TestHullSpawn();

/** SEAM for `CCollisionProperty::SetSolid(SOLID_BBOX = 2)` (`this+0x270`, under a
 *  `"CBaseEntity::SetSolid"` scope-trace frame) and `AddSolidFlags(word[+0x2b4] | 4)` — retail reads
 *  the CURRENT 16-bit solid-flag word, ORs `FSOLID_NOT_SOLID` (`0x4`) into it and passes the whole
 *  thing back to `AddSolidFlags`, which ORs it again. Family Motor's `RetailIsStandable` already
 *  records that this substrate carries no solid type and no solid flags; these three record what
 *  was asked. */
int32 RetailSolidType = 0;
uint32 RetailSolidFlags = 0;
int32 RetailSolidSets = 0;

/** SEAM for slot 93 `SetMoveType(MOVETYPE_FLY = 4, MOVECOLLIDE_DEFAULT = 0)`. Slot 93 is a generated
 *  stub on this line and `FElysiumEntity` carries no move type (family Motor's `RetailIsStandable`
 *  states the same); the pair is recorded. */
int32 RetailMoveType = 0;
int32 RetailMoveCollide = 0;

/** SEAM for `this->+0x5f44 = 0` (a BYTE store, `102d7449`). The shape map binds `+0x5f44` as an
 *  output block (`ELYSIUM_NPC_WORD_IMPLICIT`, "outputs are fired by name"), which cannot be the
 *  target of a one-byte zero, so **what `+0x5f44` is at byte granularity is unrecovered**. The
 *  store is carried under its offset and read by the test alone. */
bool bTestHullByte5f44 = false;

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

