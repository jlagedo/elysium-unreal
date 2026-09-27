// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- `CAI_BaseNPCTroika`'s
// helper declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`; the definitions are in
// `ElysiumNpcStartTask19.cpp`, or generated in the slot files for a slot body.
//
// Owns (StartTask19's `rule` rows): 0x102a1910 CAI_BaseNPCTroika::StartTask.
//
// Lane L01 (story 8 pass I) owns the dispatch prologue and the arms whose start address lies in
// `[0x102a1943, 0x102a5046)`; lane L02 owns `StartTaskTroikaTail` (the arms from `0x102a5046`, in
// `ElysiumNpcStartTask19_2.cpp`), whose `default:` is the base forward. Walked prose:
// `docs/vtmb/npc-ai/story8/StartTask19-TroikaA.md`.

// --- The dispatch ----------------------------------------------------------------------------------

/** The second half of `0x102a1910`'s one `switch`: every in-range task id whose arm starts at or past
 *  `0x102a5046`, and the base forward (`0x102a77e2`, `CAI_BaseNPC::StartTask 0x102827f0`) as its
 *  `default:`. DEFINED by lane L02 in `ElysiumNpcStartTask19_2.cpp`. `Task` is the
 *  `const FElysiumScheduleStep*` slot 442 received. */
int32 StartTaskTroikaTail(void* Task);

// --- The shared tails ------------------------------------------------------------------------------

/** `0x102a66d7` -- the `break` tail: `TaskComplete(false)` (`0x10273e80`), return. Answers 0, the
 *  slot's `int` return every exit of `0x102a1910` leaves undefined (`RET 4` with EAX clobbered). */
int32 StartTask19Complete();

/** A `TaskFail` site of `0x102a1910`: `+0x1b44 = "E:\Vampire\main\dlls\AI_BaseNPCTroika.cpp"`,
 *  `+0x1b48 = Line`, then slot 448 (`vtable +0x700`) with `Reason`. The pair is ABSENT in the shape
 *  map (the debug ring is dead); the landed convention is one `RecordScheduleEvent` row with the
 *  retail line. `Line == 0` is a site that writes no pair (`0x102a1eef`, `0x102a6ba5`). */
int32 StartTask19Fail(int32 Line, int32 Reason);

/** `AI_NavGoal_t`, the 0x40-byte literal every goal arm builds on its stack before
 *  `CAI_Navigator::SetGoal` (`0x102ecd20`). Field offsets are the ones `0x102ecd20` reads
 *  (`param_1[0..0xf]`). SOURCE units. The arrival-direction triple (`+0x2c..+0x34`, always the
 *  `DAT_10934060..68` sentinel in this body) and the two trailing words (`+0x38`/`+0x3c`, always 0)
 *  are constant across every arm here and are not carried. */
struct FStartTask19NavGoal
{
	int32 Type = 0;                              // +0x00 GoalType_t: 2 enemy, 4 location, 6 cover
	                                             //       route, 7 best unknown, 8 interesting place
	FVector DestUnits = FVector::ZeroVector;     // +0x04
	int32 DestNode = -1;                         // +0x10
	int32 Activity = -1;                         // +0x14 -1 keeps the movement activity
	int32 ArrivalActivity = -1;                  // +0x18
	int32 ArrivalSequence = -1;                  // +0x1c
	float Tolerance = -1.f;                      // +0x20 -1 AIN_DEF_TOLERANCE, -2 AIN_HULL_TOLERANCE
	int32 GoalFlags = 0;                         // +0x24
	FElysiumEntityHandle Target;                 // +0x28 (`DAT_10923dd8` in every literal here)
};

/** `DAT_1049a1ac` = -1.0 and `DAT_1049a1b0` = -2.0, the two tolerance sentinels `0x102ecd20`
 *  compares `+0x20` against. */
static constexpr float StartTask19DefaultTolerance = -1.f;
static constexpr float StartTask19HullTolerance = -2.f;

/** `CAI_Navigator::SetGoal` (`0x102ecd20`) over the port's mover, which IS this runtime's navigator
 *  (family Motor's standing fact). Resolves the goal's destination by type the way `0x102ecd20`
 *  does (2 the enemy, 1 `m_hTargetEnt`, 7 `GetBestSeeUnknown`, anything else `DestUnits`), its
 *  tolerance (`-2` the pathing hull's width, `-1` the navigator's own goal tolerance or else the
 *  hull's), takes the `Schedule` body claim (the port's arbitration), and asks `IElysiumNpcMotor::
 *  MoveTo` at the gait the goal's activity names (`0x13` ACT_RUN runs, anything else walks). The
 *  answer is `MoveTo`'s: false when there is no motor, no destination, a refused claim or a refused
 *  path -- retail's `0x102f1dc0` route-build failure. Every call is recorded for the tests. */
bool StartTask19SetGoal(const FStartTask19NavGoal& Goal, uint32 SetGoalFlags);
int32 StartTask19SetGoalCalls = 0;
FStartTask19NavGoal StartTask19LastGoal;
uint32 StartTask19LastGoalFlags = 0;

/** `0x102a76a4` -- `SetGoal(goal, 0)` with the answer dropped, then `0x102a76a9`:
 *  `m_flMoveWaitFinished (+0x5cf0) = curtime + task->flTaskData`; return, task RUNNING. */
int32 StartTask19GoalThenMoveWait(const FStartTask19NavGoal& Goal, float TaskData);

/** `0x102a4186` -- the shared follower/directed goal: type 4, `DestUnits`, destNode -1, activity
 *  `0x13` ACT_RUN, tolerance -2, then `SetGoal(goal, SetGoalFlags)` with the answer dropped; return,
 *  task RUNNING. */
int32 StartTask19SharedRunGoal(const FVector& DestUnits, uint32 SetGoalFlags);

/** `0x102a44d1` -- the turn tail: motor stop-turn (`0x102e0b40`), `0x102e2020(motor, &point, 0)` (the
 *  motor's ideal yaw toward `PointUnits`), slot 572 `SetTurnActivity`; return WITHOUT completing.
 *  `0x102e2020` is the port motor's `Face` (the ideal-yaw write), under the `Schedule` body claim. */
int32 StartTask19TurnTail(const FVector& PointUnits);
int32 StartTask19MotorStopTurns = 0;          // `0x102e0b40`, `CAI_Motor` stop-turn
int32 StartTask19MotorFaces = 0;              // `0x102e2020`
FVector StartTask19LastFacePointUnits = FVector::ZeroVector;

// --- Retail helpers these arms call that had no named port body ------------------------------------

/** `0x102784a0` -- the lateral-cover search `TASK_FIND_FAST_COVER_FROM_ENEMY` /
 *  `TASK_FIND_FORWARD_COVER_FROM_ENEMY` test first (`(threatEye, threat)`): this body's own origin,
 *  then left/right of it at 48-unit steps five times, each through `TestLateralCover 0x10278220`
 *  (sight from the threat's eye, `IsValidCover` slot 548, the motor's `MoveLimit`, then `SetGoal`,
 *  whose refusal raises `TaskFail(0xc)` synchronously). Lifted verbatim from the port's
 *  `FElysiumNpc::FindCoverFromEnemy` (base `0x10283558`'s lambda), which the integrator redirects
 *  here. True when a lateral point took the goal. */
bool FindLateralCover(const FVector& ThreatEyeCm, const FElysiumEntity& Threat);

/** `0x102d61b0(hull)` -- `hullTable[hull]+0x18 - +0xc`, the hull's X width, answered from the
 *  replayed `NAI_Hull` table (`RetailHullExtents`, full box). SOURCE units. */
float StartTask19HullWidthUnits(int32 Hull) const;

/** `CAI_Navigator` `0x102ee250(nav, act)` (thunk `0x10006fd7`) -- the path's movement activity
 *  (`path+0x2c`), `ScheduleHost.NavigationActivity`, pushed onto the mover as its gait (`0x13`
 *  ACT_RUN runs, anything else walks) through `IElysiumNpcMotor::SetTravelGait`. */
void StartTask19SetMovementActivity(int32 Activity);

/** SEAM for `0x102f2ea0` on the navigator -- "the body stands at the goal": the planar distance to the
 *  goal under the path's tolerance and the height gap under slot 522 `StepHeight`, then the
 *  navigator's arrival virtual (`vtable +0x20`). Answered by the port mover's own arrival sample
 *  (`SampleMotorIntoEntity() == Reached`), which is the same fact measured by the engine. */
bool StartTask19NavArrived();

/** `CAI_MoveProbe` `0x102e7880` as `TASK_WAIT_FOR_MOVEMENT`'s rescue calls it: `(goal, 0x202400b, 1.0,
 *  -1024.0, &out, &hitEntity)`. The port's floor probe for that address is family Lifecycle19's
 *  `MoveProbeFloorDrop` (answers the found-floor arm, point unchanged); the floor it finds is world
 *  geometry, so `bOutLandedOnNonNpc` is true exactly when it answers. */
bool StartTask19TeleportProbe(FVector& InOutUnits, bool& bOutLandedOnNonNpc);

/** `CAI_Enemies` `0x102e0290(enemy, &lkp, &second)` (thunk `0x1000366b`, on slot 541 `GetEnemies()`):
 *  walk the memory for the enemy's record and copy its `+0xc` (the last known position) and `+0x18`
 *  (the second vector, the last SEEN position in the SDK record's layout) out; false with the
 *  `"Asking LastKnownPosition for enemy..."` DevWarning when there is none. The port's
 *  `FElysiumNpcEnemyMemory` IS `CAI_Enemies`: the record's `LastPosition` answers both (the port's
 *  record keeps one position). The retail fallback to a record flagged `+0x34` has no port flag. */
bool StartTask19EnemyLkp(const FElysiumEntity& Enemy, FVector& OutLkpUnits, FVector& OutSeenUnits) const;

/** The navigator's two tolerance words the tolerance tails write: `0x102ee1c0(nav, tol)` (the path's
 *  goal tolerance, `path+0x28`, which `SetGoal`'s `-1` literal keeps) and `0x102f2fe0(nav, dist)` (the
 *  arrival distance). SOURCE units. `m_flGoalTolerance` (`+0x6320`) is `ScheduleHost.GoalToleranceCm`
 *  and is written by the arms themselves. */
void StartTask19SetNavTolerances(float GoalToleranceUnits, float ArrivalDistanceUnits);
float StartTask19NavGoalToleranceUnits = 0.f;
float StartTask19NavArrivalDistanceUnits = 0.f;

// --- The seams (each after the three searches in the L01 report) -------------------------------------

/** `CAI_Navigator::FindCoverPos` (`0x102edc80`, reached through thunk `0x1001334f`):
 *  `(threatOrigin, threatEye, minDist, maxDist, &out, threat)`. Wired to
 *  `IElysiumNpcMotor::FindNodeCover`, which carries ONE radius: `MaxUnits` goes through and
 *  `MinUnits` is recorded and not applied (the motor's node search has no inner radius). */
bool StartTask19FindCoverPos(const FVector& ThreatOriginUnits, const FVector& ThreatEyeUnits,
	float MinUnits, float MaxUnits, FVector& OutUnits);
float StartTask19LastCoverMinUnits = 0.f;
float StartTask19LastCoverMaxUnits = 0.f;
int32 StartTask19CoverSearches = 0;

/** SEAM for `0x102edd50` (thunk `0x10002bfd`), the forward-cover search `TASK_FIND_FORWARD_COVER_FROM_
 *  ENEMY` runs along `m_vecForward` (`+0x6290`): `(threatOrigin, threatEye, &forward, 0.0,
 *  CoverRadius()*0.25, &out, threat)`. No node graph stands here and the motor carries no forward
 *  search; answers false (retail's "no cover" arm). */
bool StartTask19FindForwardCover(const FVector& ThreatOriginUnits, const FVector& ThreatEyeUnits,
	const FVector& ForwardDir, float MinUnits, float MaxUnits, FVector& OutUnits);

/** SEAM for the two line-of-fire sweeps `TASK_FIND_FLANK_NODE_TO_ENEMY` and
 *  `TASK_GET_DIRECTED_PATH_TO_ENEMY_*` run: `0x102edaa0` (thunk `0x10013ade`) `(from, to, min, max,
 *  1.0, 0, &out)` and, with `Forward` non-null, `0x102ed9c0` (thunk `0x10015901`) `(from, to, min,
 *  max, 1.0, &forward, 0, &out)`. No node graph; answers false (the "no shoot position" arm). */
bool StartTask19FindLosPosition(const FVector& FromUnits, const FVector& ToUnits, float MinUnits,
	float MaxUnits, const FVector* ForwardDir, FVector& OutUnits);
int32 StartTask19LosSearches = 0;

/** SEAM for `0x102ee530` (thunk `0x1000ef43`) -- the navigator's arrival-direction write
 *  `TASK_FIND_FLANK_NODE_TO_ENEMY` ends on. The mover carries no arrival direction; recorded. */
void StartTask19SetArrivalDirection(const FVector& DirectionUnits);
int32 StartTask19ArrivalDirectionWrites = 0;

/** SEAM for `0x102edbb0`, the A* backaway search `TASK_FIND_FOLLOWER_BACKAWAY_ASTAR` runs
 *  `(bossOrigin, walkTo - 10.0, 50000.0, &out)`. No node graph; answers false. */
bool StartTask19FindBackawayAStar(const FVector& FromUnits, float MinUnits, float MaxUnits,
	FVector& OutUnits);

/** SEAM for `0x102ee300` (the directed-path point `TASK_GET_DIRECTED_PATH_TO_ENEMY_*` asks for:
 *  `(this, from, to, distance, &out)`). No node graph; answers false. */
bool StartTask19DirectedPathPoint(const FVector& FromUnits, const FVector& ToUnits, float Distance,
	FVector& OutUnits);

/** The `CAI_Pathfinder` (`m_pPathfinder +0x5d3c`) hunt pair `TASK_CREATE_HUNT_PATROL_LIST` /
 *  `TASK_FIND_HUNT_PATROL_TARGET` call: `0x10306700(this, origin, target, 256.0)` and
 *  `0x10306f60(this, origin, target, 256.0, &m_vecHuntPatrolTarget)`. SEAM: no pathfinder stands
 *  here (the same gap `FElysiumNpcFrenzyShadow::BuildHuntRoute` records on its own class); both
 *  answer false, retail's `TaskFail(0x20)` arm. */
bool StartTask19BuildHuntPatrolList(const FVector& OriginUnits, const FVector* TargetUnits,
	float RadiusUnits);
bool StartTask19FindHuntPatrolTarget(const FVector& OriginUnits, const FVector* TargetUnits,
	float RadiusUnits, FVector& OutUnits);

/** SEAM for `0x102aa640(this, &m_sppPatrolPath)` -- the patrol-point goal `TASK_GET_PATH_TO_PATROL_
 *  POINT(_HUNT)` forwards to, which owns the task's complete/fail. It is lane L10's row (Script19,
 *  `FUN_102aa640`) and has no port body in this lane's tree; the integrator redirects this to it.
 *  Until then it answers retail's missing-path arm, `TaskFail(0x1d)`. */
void StartTask19PatrolPointGoal(bool bHunt);

// --- The weapon words these arms read (`CBaseCombatWeapon`, no port member) ---

/** The active weapon's `+0x5a0` slot (slot 360, `0x1014f930`) capability word, as the port can
 *  answer it: `ElysiumNpcCond::CapabilityBits(WeaponCapability)` -- retail's own `0x18000` melee
 *  and `0x2000` ranged values, the two bits the port's item record decodes. Any other bit (the
 *  kick's `0x40000000`) answers clear. */
uint32 StartTask19WeaponCapabilityWord() const;

/** SEAM for the active weapon's burst pair `+0x3a4` / `+0x3a8` (`m_nMinBurst` / `m_nMaxBurst` by
 *  `TASK_RANGE_ATTACK1`'s use). `FElysiumWeapon` carries neither; both answer 1, the count the
 *  flag-disabled arm writes. */
int32 StartTask19WeaponMinBurst() const;
int32 StartTask19WeaponMaxBurst() const;

/** SEAM for the active weapon's range words `+0x8b8`/`+0x8bc` (the two minimum ranges) and
 *  `+0x8c0`/`+0x8c4` (the two maximum ranges). `FElysiumWeapon` stands none; all four answer
 *  0.0, so the `2000.0` default is what an armed body's maximum is replaced by. */
float StartTask19WeaponRangeWord(int32 Offset) const;

/** SEAM for weapon slot `0x518` (`TASK_MELEE_ATTACK1`) / `0x51c` (`TASK_MELEE_ATTACK2`), the
 *  weapon's own swing entry. Wired to `FElysiumWeapon::AttackIntent` (Primary for `0x518`,
 *  Secondary for `0x51c`), which is the port's weapon transaction the old runner pressed. */
void StartTask19WeaponSwing(bool bSecondary);
int32 StartTask19WeaponSwings = 0;

/** SEAM for the kick's `__RTDynamicCast(weapon, 0, 0x1055f710, 0x105da324, 0)` and the cast
 *  weapon's slot `0x5d0` `(0x53, 0, 0)`. No port weapon class answers that cast; answers false. */
bool StartTask19KickWeaponCast() const;
int32 StartTask19KickDispatches = 0;

/** SEAM for `0x10252450(weapon, bSecondary)` (the weapon's next-attack stamp) plus `0x102c5730(this,
 *  weapon)` (this NPC's own delay), `TASK_WAIT_ATTACK_TIME1/2`'s deadline. Neither word stands on
 *  `FElysiumWeapon`; answers `curtime`, so the arm's `m_flWaitFinished <= curtime` test completes. */
double StartTask19WeaponNextAttackTime(bool bSecondary) const;

// --- Dialogue / player (`m_hClosestPlayer +0x628c`) ---

/** `m_hClosestPlayer` resolved (`0x102c6420` / `0x102c64d0`) -- `Senses.Memory.ClosestPlayer`. */
FElysiumEntity* StartTask19ClosestPlayer() const;

/** SEAM for `0x102c1400(this)`, the dialogue activity `TASK_RUN_DIALOG` asks for (-1 = none). The
 *  dialogue upkeep is `FElysiumNpcDialogue`'s and exposes no retail activity; answers -1. */
int32 StartTask19DialogActivity();

/** `CBaseCombatCharacter::AddExpressionForEvent` (`0x101072b0`, thunk `0x1000afc9`) with an event
 *  index in `[0, 2)`. SEAM: this runtime names its expressions (`DefExpression`) and carries no
 *  per-event expression table; recorded. */
void StartTask19AddExpressionForEvent(int32 EventIndex);
int32 StartTask19ExpressionEvents = 0;
int32 StartTask19LastExpressionEvent = INDEX_NONE;

/** SEAM for slot `0x678` on the closest player (`TASK_START_PLAYER_DIALOG`: `player->vtable[0x678]
 *  (this)`), the player's start-talking entry. Wired to `OpenConversation` with the player's handle
 *  (the port's one conversation door). */
void StartTask19PlayerStartDialog(FElysiumEntity& Player);

// --- The two console variables `TASK_TEST1` / `TASK_TEST2` read ---

/** `debug_test_switch1` (object `0x10924678`, read through `DAT_1092467c`) and `debug_test_switch2`
 *  (object `0x10924630`, `DAT_10924634`), "Toggles stuff for the test task." (`0x1028b5f0` /
 *  `0x1028b680`). Neither is a row of `ElysiumNpcKernelTunables::EConVar`, so the two `m_nValue`
 *  words (`+0x2c`) stand here, at the shared default literal `0x105399a0` ("0"). Retail reads
 *  them as `cv->vtable[4]() ? 0 : cv->m_nValue`; the port's console variables are never commands. */
int32 StartTask19DebugTestSwitch1 = 0;
int32 StartTask19DebugTestSwitch2 = 0;

// --- The arm table (for the tests and the walked prose) ---

/** One arm of this lane's range: the retail task id, its registrar name (`FUN_10316ff0`), and the
 *  arm's start address. The test resolves every name through the corpus's task namespace and
 *  requires the id this switch compares against. */
struct FStartTask19ArmRow
{
	int32 TaskId;
	const TCHAR* TaskName;
	const TCHAR* Arm;
};
static TConstArrayView<FStartTask19ArmRow> StartTask19ArmRows();

// --- Counters for the seams that record and do nothing else ---

int32 StartTask19DebugBoxes = 0;              // `0x10142aa0` (TASK_TEST1's debug box)
int32 StartTask19WeaponHideCalls = 0;         // weapon slot 66 `+0x108` (TASK_TEST1)
int32 StartTask19WeaponUnhideCalls = 0;       // weapon slot 67 `+0x10c` (TASK_TEST1)
int32 StartTask19SetAbsOriginCalls = 0;       // slot 216, `TASK_WAIT_FOR_MOVEMENT`'s rescue
