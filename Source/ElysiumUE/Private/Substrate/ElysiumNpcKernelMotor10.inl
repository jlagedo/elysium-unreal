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
// The definitions are in `Substrate/ElysiumNpcKernelMotor10.cpp` and the tests in
// `Tests/ElysiumNpcKernelMotor10Tests.cpp`. The walked prose is
// `docs/vtmb/npc-ai/schedule-kernel.md` § "Story 29d, family Motor10 — …".
//
// This family EXTENDS story 29c-1's family **Motor** (`ElysiumNpcKernelMotor.inl`) rather than
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
//
// Retail's `AILocalMoveGoal_t` as much of it as `CAI_Motor#19` (`0x102e14a0`), its helper
// `0x102e1560` and `CAI_Motor#20` (`0x102e1760`) reach. Four words, all read from the listing:
//
//   `+0x0c..+0x14`  `dir`            the unit direction the step travels
//   `+0x28`         `maxDist`        the distance the goal still has room for
//   `+0x34`         `pMoveTarget`    the entity the goal EXPECTS to be blocked by
//   `+0x38`         `flags`          bit `0x2` is the only one `0x102e1560` tests
//
// SOURCE units, as every retail position word in this port is. Carried as a declared view rather
// than as NPC state because a move goal is the CALLER's block, not the body's: retail builds one on
// the stack per interval and hands it down.
struct FLocalMoveGoal
{
	FVector DirUnits = FVector::ZeroVector;   // +0x0c / +0x10 / +0x14
	float MaxDistanceUnits = 0.f;             // +0x28
	FElysiumEntity* ExpectedBlocker = nullptr;// +0x34
	uint32 Flags = 0;                         // +0x38, bit 0x2
};

/** `AIMoveTrace_t`, the 0x38-byte (14-dword) record `0x102e6d70` zeroes and fills. The layout is
 *  read out of `0x102e6d70`'s own initialiser, which writes `[0]`, `[1..3]`, `[4..6]`, `[7]` and
 *  `[9]` by index before dispatching on the trace kind:
 *
 *    `+0x00` `fStatus`          an `AIMoveResult_t`; the trace's own answer is `fStatus >= 0`
 *    `+0x04` `vEndPosition`     seeded to the START point
 *    `+0x10` `vHitNormal`       seeded to `vec3_origin` (`DAT_1070d1b0`)
 *    `+0x1c` `pObstruction`     seeded null
 *    `+0x20` `flDistObstructed`
 *    `+0x24` `flTotalDist`      seeded 0
 *    `+0x28` an `EHANDLE`, `+0x2c`, `+0x30`, `+0x34` — VtMB's four extra words
 *
 *  **`+0x2c` and `+0x30` have no recovered meaning and `CAI_Motor#20` does not copy them out.**
 *  That is not an oversight in the port: the listing's copy block at `102e189c`–`102e18ee` writes
 *  `+0x00`, `+0x04`, `+0x08`, `+0x0c`, `+0x10`, `+0x14`, `+0x18`, `+0x1c`, `+0x20`, `+0x24`, the
 *  `EHANDLE` at `+0x28` through its assignment operator, and `+0x34` — and nothing else. */
struct FMotorMoveTrace
{
	int32 Status = 0;                                  // +0x00 fStatus
	FVector EndPositionUnits = FVector::ZeroVector;    // +0x04
	FVector HitNormal = FVector::ZeroVector;           // +0x10
	FElysiumEntity* Obstruction = nullptr;             // +0x1c
	float DistObstructedUnits = 0.f;                   // +0x20
	float TotalDistUnits = 0.f;                        // +0x24
	FElysiumEntityHandle ObstructionHandle;            // +0x28
	int32 Word2c = 0;                                  // +0x2c  NOT copied out by slot 20
	int32 Word30 = 0;                                  // +0x30  NOT copied out by slot 20
	int32 Word34 = 0;                                  // +0x34
};

/** `CAI_Motor::m_vecVelocity`, `CAI_Motor+0x3c` — the datamap names it (`vtmb_fields CAI_Motor`:
 *  `+0x3c m_vecVelocity undefined4[3]`, `+0x54 m_facingQueue`), and both step bodies write all
 *  three components of it from the goal direction scaled by the current speed. SOURCE units.
 *
 *  **A CORRECTION TO A NEIGHBOUR'S NOTE, recorded and not edited into its file.** Family
 *  TroikaHelpers' `FTroikaMotorSeams::FacingQueueCount` is commented "`CAI_Motor+0x54
 *  m_facingQueue`'s live count, `CAI_Motor+0x3c`". The queue is at `+0x54`; `+0x3c` is
 *  `m_vecVelocity` and is a `Vector`, not a count. The two members therefore stand for two
 *  different words and the offset in that comment is the one that is wrong. */
FVector MotorVelocityUnits = FVector::ZeroVector;

// --- The `CAI_Motor` seams this family adds ------------------------------------------------------

/** `thunk_FUN_102e6d70(motor->+0x68, kind, start, end, mask, 0, extent, 0, &trace, filter, 0)` —
 *  `CAI_MoveProbe`'s trace entry, dispatched on `kind` (0 ground, 1, **2** the hull sweep slot 20
 *  runs, 3) and answering `trace.fStatus >= 0`.
 *
 *  **SEAM**: family Motor already records that nothing in this substrate traces a hull for the
 *  kernel (`KernelHullTrace`). This is the SECOND trace entry — a different retail function with a
 *  trace-kind selector and a full `AIMoveTrace_t` out param — so it is named rather than folded
 *  into that one. It performs retail's own initialisation of the record, which is the half that IS
 *  recovered, and then answers the CLEAR result: `fStatus` 0 (`AIMR_OK`), the end position seeded
 *  to the START, a zero normal, no obstruction and `flTotalDist` 0. `fStatus >= 0`, so the answer
 *  is **true** — the admitting value. Counted. */
bool MotorMoveTraceSweep(int32 Kind, const FVector& StartUnits, const FVector& EndUnits, int32 Mask,
	float ExtentUnits, const void* Filter, FMotorMoveTrace& OutTrace) const;

/** `thunk_FUN_102e0bd0(this, &end, goal->+0x34, -1.0, 1, nearGoal, pMoveTrace, filter)` — the apply
 *  half of `0x102e1560`, which steps the body toward `EndUnits` and answers an `AIMoveResult_t`.
 *
 *  **SEAM**, and deliberately NOT family Motor's `MotorApplyIntervalMovement`: that one stands for
 *  the SAME retail address reached from `AutoMovement` (`0x10280a50`) with an entirely different
 *  argument list — a root-motion delta and a yaw — and folding the two would claim one call where
 *  retail makes two. This one answers **0**, which is retail's own refusal value, so `0x102e1560`
 *  takes its `slot +0x28` arm exactly as it would for a step the mover declined. Counted. */
int32 MotorStepToPoint(const FVector& EndUnits, const FElysiumEntity* ExpectedBlocker,
	bool bNearGoal, FMotorMoveTrace* OutTrace, const void* Filter);

/** `(*(this+0x10))->slot 6 (+0x18)(goal)` — the arm `0x102e1560` takes when the step distance is at
 *  or below `_DAT_1044fab0` (**0.0**): the LOCAL NAVIGATOR (`CAI_Motor+0x10`) is asked whether the
 *  goal is already satisfied, and the answer is folded to `1` / `0`. **SEAM**: `m_pLocalNavigator`
 *  (`+0x5d38`) is an `ELYSIUM_NPC_WORD_CHAIN` row onto the one mover, which keeps no goal, so this
 *  answers **false** and the body answers `0` — retail's own answer for a local navigator that did
 *  not take the goal. Counted. */
bool MotorLocalNavigatorTakesGoal(const FLocalMoveGoal& Goal) const;

/** `this->slot 10 (+0x28)()` on the MOTOR's own vtable — the refusal hook `0x102e1560` dispatches
 *  when `0x102e0bd0` answers 0, before returning 0 itself. **SEAM**: `CAI_Motor` slot 10 has no
 *  recovered body in this band and there is no motor object to dispatch on; the call is counted so
 *  a case can state that the refusal arm ran. Its retail identity is **unrecovered**. */
void MotorOnMoveExecuteFailed();

// --- The `CAI_Navigator` seams this family adds --------------------------------------------------

/** `thunk_FUN_102efd50(navigator)` — `CAI_Navigator::MoveNormal`'s own gate, ported in full rather
 *  than seamed because every word it touches already exists here:
 *
 *      int routeType = thunk_FUN_1030bc00(nav->+0x30);     // the CURRENT route's nav type
 *      int navType   = nav->+0x18;                          // family Motor's `Navigator.NavType`
 *      if (routeType == 0) {
 *          if (navType != 0) {
 *              DevMsg("Warning: NPC appears to have wro…");
 *              if (navType == 1) nav->+0x20->slot 8 ();     // the JUMP teardown
 *              else if (navType == 3) nav->+0x20->slot 5 ();// the CLIMB teardown
 *              thunk_FUN_102eeba0(nav, 0);                  // NavSetType(NAV_GROUND)
 *          }
 *      } else if (routeType == 2 && navType != 2) {
 *          return false;                                    // a FLY route under a non-fly nav type
 *      }
 *      thunk_FUN_102f13d0(nav, 0);
 *      nav->+0x51 = 0;
 *      return true;
 *
 *  `RouteNavType` is the seam: there is no route object, so it answers retail's own `0`. */
bool NavigatorMoveGate();

/** `thunk_FUN_1030bc00(navigator->+0x30)` — the nav type of the route's CURRENT waypoint. **SEAM**:
 *  family TroikaHelpers' `NavPathSample` already records that there is no `CAI_Path` here; this
 *  answers **0** (`NAV_GROUND`), which is the value that takes the gate's first arm and lets the
 *  move through. */
int32 RouteNavType() const;

/** `nav->+0x20->slot 8` and `nav->+0x20->slot 5` — the two teardowns the gate runs when the route
 *  has gone to ground under a jump or a climb nav type. `nav+0x20` is `m_pMotor`, snapshotted by
 *  `CAI_Navigator::vfunc3` (family Motor's `NavSnapshotOwnerPointers`), and neither slot has a
 *  recovered body in this band. **SEAM**: counted; both retail identities are **unrecovered**. */
void NavigatorJumpTeardown();
void NavigatorClimbTeardown();

/** `thunk_FUN_102f13d0(navigator, 0)` — the call every passing arm of the gate makes just before it
 *  clears `navigator+0x51`. **SEAM**: unrecovered body, counted. */
void NavigatorMoveGatePass();

/** `navigator+0x51` — the byte the gate clears and `MoveNormal`'s tail tests before it dispatches
 *  navigator slot 6. Retail name **unrecovered**; it is the navigator's own word and lives here
 *  beside family Motor's `FNavigator` rather than inside it, because that view is 29c-1's and this
 *  is a 29d word. */
bool bNavigatorByte51 = false;

/** `navigator->slot 16 (+0x40)(&result)` — the override `MoveNormal` offers the move to before it
 *  does anything itself. Retail seeds `result` with `-4` (`AIMR_ILLEGAL`) BEFORE the call and
 *  returns whatever the override left there when it answers true. **SEAM**: no navigator subclass
 *  here; answers false, so the seeded `-4` is never returned and the body runs its own path. */
bool NavigatorMoveOverride(int32& InOutResult);

// Family TroikaHelpers' `FNavMoveInfo` (`CAI_Navigator#17`, `0x102eee40`) is what the enact takes,
// and `ElysiumNpcKernelTroikaHelpers.inl` is included AFTER this file in `ElysiumNpc.h`'s
// alphabetical block. A nested class may be declared here and defined later in the same class body,
// which is what this line does; it is not a second type.
struct FNavMoveInfo;

/** `navigator->slot 15 (+0x3c)(&moveInfo, argument)` — the ENACT: the move-info block family
 *  TroikaHelpers builds through `FUN_102eee40` (navigator slot 17) is handed back to the navigator
 *  to perform, and the `AIMoveResult_t` it answers is `MoveNormal`'s. **SEAM**: answers **0**
 *  (`AIMR_OK`), which is the admitting value — it is the arm that lets `MoveNormal` run its restore
 *  and its slot-6 tail, and a refusal here would hide both. Counted, with the argument recorded. */
int32 NavigatorEnactMove(const FNavMoveInfo& Info, int32 Argument);

/** `navigator->slot 6 (+0x18)()` — the tail `MoveNormal` runs when the enact answered `AIMR_OK` and
 *  `navigator+0x51` is clear. **SEAM**: unrecovered body, counted. */
void NavigatorMoveTail();

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

/** One `CAI_StandoffGoal`'s own words, by offset. */
struct FStandoffGoalWords
{
	/** `+0x0484 m_aggressiveness` — the value both input bodies clamp. The valid range is `[0,4]`
	 *  and **5 is a sentinel that is exempt from the warning and from the clamp**. */
	int32 Aggressiveness = 0;

	/** `+0x0480 m_flags` (`CAI_GoalEntity`'s). Bit `0x1` is ACTIVE — set by `InputActivate`, cleared
	 *  by `InputDeactivate`, and the bit `UpdateOnRemove` tests. Bit `0x2` is "the actor list has
	 *  been resolved once". */
	uint32 Flags = 0;

	/** The intrusive membership in the global goal list `DAT_106eb5d8` (`0x100f6d80` adds at the
	 *  node at `+0x450`, `0x100f6e40` removes). No goal list stands here; the membership is carried
	 *  as the one observable fact the add and the remove leave behind. */
	bool bOnGoalList = false;

	/** `+0x0474` actor count over the `+0x0468` handle array, and the per-actor dispatches of
	 *  `EnableGoal` (vtable `+0x3d0`) and `DisableGoal` (`+0x3d4`). No actors resolve here, so both
	 *  tallies stay at the number of actors the view was given. */
	int32 ActorCount = 0;
	int32 EnableGoalCalls = 0;
	int32 DisableGoalCalls = 0;

	/** `0x102cd4a0` (resolve, sets flag bit `0x2`) and `0x102cd3b0` (refresh, when bit `0x2` already
	 *  stands). Counted so a case can state which of the two the input took. */
	int32 ActorResolves = 0;
	int32 ActorRefreshes = 0;

	/** The `DevMsg("Invalid aggressiveness value %d\n", …)` both inputs emit, with the value they
	 *  emitted it FOR — which is the PRE-clamp one. */
	int32 InvalidWarnings = 0;
	int32 LastInvalidValue = 0;

	/** The `inputdata_t` `UpdateOnRemove` builds and hands to slot 243, and the tail it always runs.
	 *  See `StandoffGoalUpdateOnRemove` for why three of that block's six words stay uninitialised. */
	int32 ReleaseInputs = 0;
	bool bUpdateOnRemoveTailRan = false;
};

/** `CAI_GoalEntity::InputActivate` (`0x102cd650`), the routine `CAI_StandoffGoal#241` tails into.
 *  Ported in full — every word it touches is in `FStandoffGoalWords`:
 *
 *      if (flags & 0x1) return;                       // already active: nothing at all
 *      goalList.Insert(this);                          // 0x100f6d80 over DAT_106eb5d8, node +0x450
 *      if (flags & 0x2) RefreshActors();               // 0x102cd3b0
 *      else { ResolveActors(); flags |= 0x2; }         // 0x102cd4a0
 *      flags |= 0x1;
 *      for (i = 0; i < +0x474; ++i) actor[i]->EnableGoal();   // vtable +0x3d0
 */
static void GoalEntityInputActivate(FStandoffGoalWords& Goal);

/** `CAI_GoalEntity::InputDeactivate` (`0x102cdb70`), the exact inverse and the routine
 *  `CAI_StandoffGoal#243` tails into:
 *
 *      if (!(flags & 0x1)) return;
 *      if (flags & 0x2) RefreshActors(); else { ResolveActors(); flags |= 0x2; }
 *      flags &= ~0x1;
 *      for (i = 0; i < +0x474; ++i) actor[i]->DisableGoal();  // vtable +0x3d4
 *      goalList.Remove(this);                                  // 0x100f6e40
 */
static void GoalEntityInputDeactivate(FStandoffGoalWords& Goal);

/** `CAI_StandoffGoal::vfunc241` `0x102c87a0` — the aggressiveness clamp in front of
 *  `GoalEntityInputActivate`. */
static void StandoffInputActivate(FStandoffGoalWords& Goal);

/** `CAI_StandoffGoal::vfunc243` `0x102c8830` — **byte-for-byte the same clamp**, differing only in
 *  calling `0x102cdb70` instead of `0x102cd650`. The two are one clamp shared by the activate and
 *  the deactivate input, which is why the clamp itself is a third function below and not copied. */
static void StandoffInputDeactivate(FStandoffGoalWords& Goal);

/** The clamp both inputs run, `0x102c87ab`–`0x102c87f5`. Answers nothing; it writes
 *  `Aggressiveness`, `InvalidWarnings` and `LastInvalidValue`. */
static void StandoffClampAggressiveness(FStandoffGoalWords& Goal);

/** `CAI_StandoffGoal::UpdateOnRemove` `0x102cdc50`, slot 180. */
static void StandoffGoalUpdateOnRemove(FStandoffGoalWords& Goal);

// --- `CAI_StandoffBehavior#22`, the activity translation -------------------------------------------

/** `CAI_StandoffBehavior::vfunc22` `0x102c79e0` — the standoff's `TranslateActivity`. `Words` is
 *  family Lifecycle's `FStandoffWords`, whose `Posture` IS the behaviour's `+0x1c`; `Activity` is
 *  the incoming activity number and the answer is the outgoing one. `INDEX_NONE` means "fall
 *  through to `CAI_Behavior::vfunc22`", the base this runtime does not carry — the same spelling
 *  family Lifecycle used for `StandoffSelect`'s fall-through. */
int32 StandoffTranslateActivity(FStandoffWords& Words, int32 Activity);

/** SEAM for `owner->slot 569 (+0x8e4)(hintNode)` — the activity a hint of type `0x65` asks the NPC
 *  for. Slot 569 is a generated stub on this line and family Hints records that hints are node
 *  INDICES here with no type store, so this answers `INDEX_NONE`; the arm then leaves the incoming
 *  activity alone and never latches posture 2. Counted. */
int32 HintActivityForNode(int32 HintNode) const;

/** SEAM for `CBaseCombatCharacter::Weapon_OwnsThisType(name, 0)` (`0x102c7aa4` /
 *  `0x102c7ad7`) — does this body carry a weapon of the named class? The two names are
 *  `"weapon_smg1"` (`0x10601ef8`) and `"weapon_pistol"` (`0x10601ee8`), read out of the listing.
 *  **SEAM**: answers false, so the low-aim arm falls through to its own DevMsg. */
bool WeaponOwnsThisType(const TCHAR* WeaponClassname) const;

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

/** `NAI_Hull::Bits(hull)` = `PTR_DAT_1060a750[hull][0]` (`0x102d6210`) — the used-hull bit of one
 *  hull id. **SEAM**: family Motor already records that there is no hull table here
 *  (`RetailHullExtents`); this answers **0** for every id, which makes every candidate miss. */
static int32 RetailHullBits(int32 Hull);

/** `NAI_Hull::Name(hull)` = `PTR_DAT_1060a750[hull][1]` (`0x102d6230`) — the hull's printable name,
 *  the sixth line of `SetHullSizeNormal`'s ERROR block. **SEAM**: answers an empty string. */
static FString RetailHullName(int32 Hull);

/** The global used-hull mask `DAT_10610be8` and its two writers, `0x102f9900` (clear to 0, and
 *  `DAT_1093412c` with it) and `0x102f9920` (`|= bits`), read back by `0x102f9950`.
 *
 *  The mask is `.data` whose on-disk initialiser is `0xffffffff` and whose first runtime write is
 *  `0x102f9900`'s zero, at the start of the node-graph build. **Nothing in this runtime builds a
 *  node graph and nothing precaches a hull**, so the accumulator here starts at **0** — retail's own
 *  post-clear, pre-precache value — and the two writers are ported so the pick below has the lever
 *  its own arms need. Static, because the mask is global in retail. */
static int32 RetailUsedHullBits();
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

/** `0x10273070` `CAI_BaseNPC::SetHullSizeNormal(bool force)` — nineteen direct callers plus two
 *  outside, the widest-called body in this band. Answers nothing; retail's `RET 0x4` leaves no
 *  value. */
void SetHullSizeNormal(bool bForce);

/** `0x10273180` `CAI_BaseNPC::SetHullSizeSmall(bool force)` — the twin with the gate INVERTED, and
 *  it answers **1 unconditionally**, including on the path where the gate refused and nothing
 *  changed. That is retail's and is reproduced. */
bool SetHullSizeSmall(bool bForce);

/** SEAM for `thunk_FUN_101cf390(this, mins, maxs)` = `UTIL_SetSize` — the bounds write both hull
 *  bodies make. `FElysiumEntity` carries no collision box (family Motor's `RetailCollisionExtents`
 *  records the same gap), and the mins/maxs come off family Motor's `RetailHullExtents`, which
 *  answers the ZERO box, so what is recovered is that the write happened and with what. SOURCE
 *  units. */
FVector LastSetSizeMinsUnits = FVector::ZeroVector;
FVector LastSetSizeMaxsUnits = FVector::ZeroVector;
int32 SetSizeCalls = 0;

/** SEAM for `thunk_FUN_10272f40(this)` — `CAI_BaseNPC::SetupVPhysicsHull`, the VPhysics shadow
 *  rebuild both hull bodies run when `m_pPhysicsObject` (`+0x36c`) is live. **SEAM**: there is no
 *  VPhysics shadow here; counted. */
int32 VPhysicsHullRebuilds = 0;

/** `+0x036c`, the physics object pointer both hull bodies gate the rebuild on. **SEAM**: this
 *  substrate stands no physics object on the kernel surface, so it answers false and the rebuild is
 *  skipped — which is retail's own arm for an NPC with no `VPhysicsGetObject()`. Carried as a bool
 *  a case can raise, because the gate is the recovered half. */
bool bHasVPhysicsObject = false;

/** The six-line ERROR block `SetHullSizeNormal` prints when `(NAI_Hull::Bits(m_eHull) & usedBits)
 *  != NAI_Hull::Bits(m_eHull)` — i.e. when this body's hull was never precached. The six literals
 *  are read out of the listing at `102730a2`–`102730e2`. Counted, with the hull it complained
 *  about. */
int32 HullNotPrecachedWarnings = 0;
int32 LastHullNotPrecached = 0;

// --- The non-slot bodies of this family -------------------------------------------------------------

/** `0x10278650` — **`CAI_BaseNPC::GetShootTarget(posSrc, bNoisy, bFlag2)`**, and NOT the standoff
 *  anchor the checklist's row named. The reading that settles it: the call at `102786f1`–`10278709`
 *  is `this->slot 541 GetEnemies()` (no arguments, so the two pushes around it belong to the NEXT
 *  call) then `CAI_Enemies::GetLastKnownPosition(&lkp, enemy)` (`0x102dfed0`, whose own DevWarning
 *  is `"Asking LastKnownPosition for ene…"`); the enemy comes from `this->slot 167 GetEnemy()`; the
 *  offset comes from `enemy->slot 197 BodyTarget(posSrc, bNoisy, bFlag2)`; and the cached handle at
 *  `+0x5ba8` the body short-circuits on is the shape map's own `ShootTargetOverride`.
 *
 *  In retail's order:
 *
 *    1. `m_hShootTargetOverride` (`+0x5ba8`) resolving → **that entity's `GetAbsOrigin()`**, and
 *       the three arguments are ignored entirely.
 *    2. No enemy (slot 167) → `AngleVectors(GetAngles(), &forward)` (slot 221, `0x10139610`) and
 *       the answer is `forward + posSrc`.
 *    3. An enemy → `lkp + (BodyTarget(posSrc, bNoisy, bFlag2) - enemy->GetAbsOrigin())`, with the
 *       body target's **Z raised by `_DAT_104994e0` = -30.0** first when the enemy's per-class
 *       type-3 `CVStatList_t` answers **5** for stat `0xb`.
 *
 *  SOURCE units in, SOURCE units out. */
FVector GetShootTarget(const FVector& PosSrcUnits, bool bNoisy, bool bFlag2) const;

/** SEAM for `0x10278650`'s stat lookup — `enemy->+0x9c` (its player record) then its `+0x13bc`
 *  count / `+0x13c0` table walked for the first list whose `+0x10` is **3**, then
 *  `CVStatList_t::GetValue(0xb)` (`0x102012d0`); an entity with no type-3 list falls back to the
 *  lazily-built EMPTY global `DAT_109f0b40`, whose `GetValue` answers 0. Family **Sounds10**
 *  records the same join for `FireBullets`' ranged skill and takes the same refusal: this
 *  runtime's sheet carries no `CVStatList_t` keyed by retail's list type, so the join is
 *  **unrecovered** and the answer is **0** — which is not 5, so the Z offset is not applied. */
static int32 EnemyTypedStatValue(const FElysiumEntity& Enemy, int32 StatId);

/** SEAM for slot 221 `CBaseEntity::GetAngles()` (`0x100b3110`, vtable `+0x374`) in SOURCE degrees —
 *  the angles arm 2 turns into a forward vector. Slot 221 is a generated stub on this line, so this
 *  answers this body's own `FElysiumEntity::Angles`, which is the port's equivalent word, as a
 *  SOURCE `(pitch, yaw, roll)` triple in degrees. */
FVector RetailGetAnglesDegrees() const;

/** `0x10139610` `AngleVectors(angles, &forward, NULL, NULL)`, forward only, read off the decompiled
 *  body: with `_DAT_1044eb08` the degrees-to-radians scale,
 *  `forward = (cos(pitch)cos(yaw), cos(pitch)sin(yaw), -sin(pitch))`. SOURCE axes. */
static FVector AngleVectorsForward(const FVector& AnglesDegrees);

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
bool BuildLocalRouteThroughDoor(const FVector& FromUnits, const FVector& ToUnits, int32 Flags);

/** SEAM for `thunk_FUN_10319f30(navigator->+0x30 + 0x24, waypoint)` — splice the waypoint the
 *  search found into the navigator's live path, answering whether the splice took. Unreachable
 *  while `BuildLocalRouteThroughDoor` answers null; declared so the arm above it has a real call to
 *  make the day a path object stands, and answers **false**. */
bool SplicePathWaypoint(int32 Waypoint);

// --- The two `CAI_Motor` step bodies and `CAI_Navigator#12` ------------------------------------------

/** `CAI_Motor#19` `0x102e14a0` `MoveGroundExecute`, which no class overrides. 129 bytes and four
 *  statements, and it RETURNS the value of `0x102e1560` — the decompiler types it `void`, but the
 *  listing's `CALL 0x10012116; POP…; RET 0xc` leaves `EAX` alone. */
int32 MoveGroundExecute(const FLocalMoveGoal& Goal, FMotorMoveTrace* OutTrace, const void* Filter);

/** `0x102e1560`, the helper `MoveGroundExecute` tails into. Not a `rule` row of its own and owned by
 *  no other family — it is `CAI_Motor`'s and this is `CAI_Motor`'s family — so it is ported here
 *  under the SDK's spelling for the same shape rather than left as a call into nothing. `Speed` and
 *  `StepUnits` are the two numbers slot 19 computed. */
int32 MotorMoveGroundExecuteWalk(FLocalMoveGoal& Goal, float Speed, float StepUnits,
	FMotorMoveTrace* OutTrace, const void* Filter);

/** `CAI_Motor#20` `0x102e1760` `MoveGroundStep`. Three stack arguments: the move goal, the caller's
 *  `AIMoveTrace_t` out param (**the second**, which is where the trace record is copied — the move
 *  goal is never written back) and the trace filter. */
int32 MotorMoveGroundStep(FLocalMoveGoal& Goal, FMotorMoveTrace* OutTrace, const void* Filter);

/** `CAI_Motor::GetCurSpeed` `0x102e12c0`, whose whole body is `JMP [[owner]+0x3e0]` — a tail jump to
 *  the OWNER's slot 248 `GetIdealSpeed`. Both step bodies read it, so it is one method rather than
 *  two dispatches spelled differently. */
float MotorCurSpeed() const;

/** `UTIL_SetOrigin(owner, trace.vEndPosition, true)` — `0x101cf5c0`, whose body is `entity->slot 62
 *  SetLocalOrigin(vec)` then `PhysicsTouchTriggers()` when the third argument is non-zero.
 *
 *  **A CORRECTION.** The checklist's walk of `0x102e1760` calls this `NDebugOverlay::Line`. It is
 *  not: `docs/vtmb/npc-kernel/layout.md`'s `m_vecGrappleSavedOrigin` row already names `0x101cf5c0`
 *  "the SetAbsOrigin helper", and the body itself is the two statements above. The non-`4`/`0` arm
 *  of `MoveGroundStep` therefore **moves the body to the trace endpoint** — it is the step, not a
 *  debug line. */
void MotorSetOriginToTraceEnd(const FVector& EndPositionUnits);

/** `CAI_Navigator::MoveNormal` `0x102efaa0`, `CAI_Navigator#12`. **Returns an `AIMoveResult_t`,
 *  not a distance**: the gate's refusal is `MOV EAX,0xfffffffc` = `-4` `AIMR_ILLEGAL` (which the
 *  decompiler prints as `-NAN` because it typed the return `float`), the speed arm answers `0` =
 *  `AIMR_OK`, and every other exit is the enact's own answer. `Argument` is the one stack word
 *  retail forwards to navigator slot 15. */
int32 NavigatorMoveNormal(int32 Argument);

/** What this family's seams were ASKED, so a test can assert that a body reached its call and that
 *  the refusal was the recovered one. Read by the test suite and by nothing else. Separate from
 *  family Motor's `MotorSeams` so that family's own cases keep their exact tallies. */
struct FMotor10SeamLedger
{
	int32 MoveTraceSweeps = 0;            // `0x102e6d70`
	int32 LastMoveTraceKind = INDEX_NONE;
	int32 LastMoveTraceMask = 0;
	float LastMoveTraceExtent = 0.f;
	int32 StepToPointCalls = 0;           // `0x102e0bd0`
	FVector LastStepEndUnits = FVector::ZeroVector;
	int32 LocalNavigatorAsks = 0;         // `motor->+0x10` slot 6
	int32 MoveExecuteFailures = 0;        // `motor` slot 10
	int32 SetOriginCalls = 0;             // `UTIL_SetOrigin 0x101cf5c0`
	FVector LastSetOriginUnits = FVector::ZeroVector;
	int32 NavGateJumpTeardowns = 0;       // `nav->+0x20` slot 8
	int32 NavGateClimbTeardowns = 0;      // `nav->+0x20` slot 5
	int32 NavGateWrongTypeWarnings = 0;   // the gate's DevMsg
	int32 NavGatePasses = 0;              // `0x102f13d0`
	int32 NavMoveOverrideAsks = 0;        // navigator slot 16
	int32 NavEnactCalls = 0;              // navigator slot 15
	int32 LastNavEnactArgument = 0;
	int32 NavMoveTails = 0;               // navigator slot 6
	int32 HintActivityAsks = 0;           // owner slot 569
	int32 WeaponOwnsAsks = 0;             // Weapon_OwnsThisType
	int32 LowAimWarnings = 0;             // `0x1027e590` "NPC in standoff lacks needed low aim…"
	int32 BuildLocalRouteAsks = 0;        // `0x10304130`
	int32 SplicePathAsks = 0;             // `0x10319f30`
};
mutable FMotor10SeamLedger Motor10Seams;
