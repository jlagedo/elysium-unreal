// `CAI_BaseNPC`'s declarations of the `Motor10` family (story 5 step 5),
// moved from `ElysiumNpcMotor10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseMotor10.cpp`.

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

/** `navigator+0x51` — the byte the gate clears and `MoveNormal`'s tail tests before it dispatches
 *  navigator slot 6. Retail name **unrecovered**; it is the navigator's own word and lives here
 *  beside family Motor's `FNavigator` rather than inside it, because that view is 29c-1's and this
 *  is a 29d word. */
bool bNavigatorByte51 = false;

// Family TroikaHelpers' `FNavMoveInfo` (`CAI_Navigator#17`, `0x102eee40`) is what the enact takes,
// and `ElysiumNpcTroikaHelpers.inl` is included AFTER this file in `ElysiumNpc.h`'s
// alphabetical block. A nested class may be declared here and defined later in the same class body,
// which is what this line does; it is not a second type.
struct FNavMoveInfo;

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

mutable FMotor10SeamLedger Motor10Seams;

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

/** `navigator->slot 16 (+0x40)(&result)` — the override `MoveNormal` offers the move to before it
 *  does anything itself. Retail seeds `result` with `-4` (`AIMR_ILLEGAL`) BEFORE the call and
 *  returns whatever the override left there when it answers true. **SEAM**: no navigator subclass
 *  here; answers false, so the seeded `-4` is never returned and the body runs its own path. */
bool NavigatorMoveOverride(int32& InOutResult);

/** `navigator->slot 15 (+0x3c)(&moveInfo, argument)` — the ENACT: the move-info block family
 *  TroikaHelpers builds through `FUN_102eee40` (navigator slot 17) is handed back to the navigator
 *  to perform, and the `AIMoveResult_t` it answers is `MoveNormal`'s. **SEAM**: answers **0**
 *  (`AIMR_OK`), which is the admitting value — it is the arm that lets `MoveNormal` run its restore
 *  and its slot-6 tail, and a refusal here would hide both. Counted, with the argument recorded. */
int32 NavigatorEnactMove(const FNavMoveInfo& Info, int32 Argument);

/** `navigator->slot 6 (+0x18)()` — the tail `MoveNormal` runs when the enact answered `AIMR_OK` and
 *  `navigator+0x51` is clear. **SEAM**: unrecovered body, counted. */
void NavigatorMoveTail();

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

/** SEAM for `DAT_10925390` and `DAT_10925388`, the two activity ids the low-aim arm answers with.
 *  Both live in **uninitialised `.data`** — the file's raw `.data` ends before either address, so
 *  they are registered at runtime by `ActivityList_RegisterSharedActivity` and their VALUES are
 *  **unrecovered**, exactly as family Sounds10 found for `FireBullets`' skill table. Both answer
 *  `INDEX_NONE`, which `SelectHeaviestSequence` (itself a seam answering `INDEX_NONE`) then refuses,
 *  so the arm falls through to its own DevMsg — retail's own path for a body with no such sequence. */
static int32 StandoffLowAimActivitySmg();

static int32 StandoffLowAimActivityPistol();

/** SEAM for `CBaseCombatCharacter::Weapon_OwnsThisType(name, 0)` (`0x102c7aa4` /
 *  `0x102c7ad7`) — does this body carry a weapon of the named class? The two names are
 *  `"weapon_smg1"` (`0x10601ef8`) and `"weapon_pistol"` (`0x10601ee8`), read out of the listing.
 *  **SEAM**: answers false, so the low-aim arm falls through to its own DevMsg. */
bool WeaponOwnsThisType(const TCHAR* WeaponClassname) const;

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

/** `0x10273070` `CAI_BaseNPC::SetHullSizeNormal(bool force)` — nineteen direct callers plus two
 *  outside, the widest-called body in this band. Answers nothing; retail's `RET 0x4` leaves no
 *  value. */
void SetHullSizeNormal(bool bForce);

/** `0x10273180` `CAI_BaseNPC::SetHullSizeSmall(bool force)` — the twin with the gate INVERTED, and
 *  it answers **1 unconditionally**, including on the path where the gate refused and nothing
 *  changed. That is retail's and is reproduced. */
bool SetHullSizeSmall(bool bForce);

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

