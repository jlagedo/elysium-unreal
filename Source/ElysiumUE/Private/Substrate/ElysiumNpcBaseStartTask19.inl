// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- `CAI_BaseNPC`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpcBase` by `Substrate/ElysiumNpcBase.h`; the definitions are in
// `ElysiumNpcBaseStartTask19.cpp`, or generated in the slot files for a slot body.
//
// Owns (StartTask19's `rule` rows): 0x102827f0 CAI_BaseNPC::StartTask.
//
// Lane L03 (pass I). Everything below is a service `CAI_BaseNPC::StartTask` (`0x102827f0`) calls
// that the port had no body for, declared here rather than in a hot header. Each names the retail
// function or word it stands for; the ones marked **SEAM** answer "nothing" (the value retail's own
// refusal arm takes) because the substrate has no source for them yet.

/**
 * `AI_NavGoal_t`, the sixteen-dword (`0x40`-byte) record every path arm of `0x102827f0` builds on its
 * stack and hands to `CAI_Navigator::SetGoal` (`0x102ecd20`). Field order is retail's, read off the
 * stack stores (`0x102840bb..0x10284131` for the fullest one, arm `0x06`):
 *
 *   [0] type, [1..3] dest, [4] destNode, [5] movement activity, [6] arrival activity,
 *   [7] arrival sequence, [8] tolerance, [9] flags, [10] pTarget, [11..13] three defaults
 *   (`0x10934060/64/68`), [14..15] two zeros.
 *
 * `SetGoal` reads [5] into `SetMovementActivity` (`0x102ee250`), [6] into `0x1030b550` and [7] into
 * `0x1030b5b0` (`0x102ecd20` tail) — so [5] is the MOVEMENT activity, not the arrival one the pass-R
 * walk called it. The default `dest` triple (`0x1093404c/50/54`) and default `pTarget`
 * (`0x10923a30`) are BSS words the static initialiser writes (unrecovered); `bDestSet` false and an
 * unset `Target` stand for them.
 */
struct FStartTaskNavGoal
{
	int32 Type = 0;                         // [0] GoalType_t: 1 target, 2 enemy, 3 path corner, 4 location, 6 location-nearest-node
	FVector DestCm = FVector::ZeroVector;   // [1..3], centimetres (port space)
	bool bDestSet = false;                  // false = the default triple 0x1093404c..54
	int32 DestNode = INDEX_NONE;            // [4]
	int32 MovementActivity = INDEX_NONE;    // [5]
	int32 ArrivalActivity = INDEX_NONE;     // [6]
	int32 ArrivalSequence = INDEX_NONE;     // [7]
	float ToleranceUnits = -1.f;            // [8] -1.0 (0x1049a160) keep the path's, -2.0 (0x1049a164) hull
	int32 GoalFlags = 0;                    // [9]
	FElysiumEntityHandle Target;            // [10] pTarget; unset = the default 0x10923a30
};

/**
 * What the StartTask navigator services did on this NPC, for the tests and the trace. None of it is
 * retail state except where the comment names an offset; the counters are the recording seams a
 * case asserts the calls through (the brief's "calls it makes").
 */
struct FStartTaskNavRecord
{
	int32 SetGoalCalls = 0;
	FStartTaskNavGoal LastGoal;
	int32 LastSetGoalFlags = 0;
	bool bLastSetGoalResult = false;
	FVector LastSetGoalDestCm = FVector::ZeroVector;
	float LastSetGoalToleranceUnits = 0.f;
	int32 ClearGoalCalls = 0;                          // 0x102ee270
	int32 MovementActivitySets = 0;                    // 0x102ee250
	int32 LastMovementActivity = INDEX_NONE;
	int32 ArrivalActivitySets = 0;                     // 0x102ee410
	int32 LastArrivalActivity = INDEX_NONE;
	int32 ArrivalDirectionSets = 0;                    // 0x102ee530 / 0x102ee550
	FVector LastArrivalDirection = FVector::ZeroVector;
	bool bLastArrivalDirectionIsAngles = false;
	int32 MotorYawHolds = 0;                           // 0x102e0b40 (`motor+0x2c = -1.0`)
	int32 PathGoalFlagClears = 0;                      // 0x102ee2c0 (`path+0x10 = 0`)
	int32 CoverSearches = 0;                           // 0x102edc80
	int32 LosSearches = 0;                             // 0x102edaa0
	int32 RandomGoalRequests = 0;                      // 0x102ed940
	int32 WanderGoalRequests = 0;                      // 0x102ed540
	float LastSearchMinUnits = 0.f;
	float LastSearchMaxUnits = 0.f;
	FVector LastSearchThreatCm = FVector::ZeroVector;
	FVector LastSearchThreatEyeCm = FVector::ZeroVector;
	int32 LateralCoverTests = 0;                       // 0x10278220
	int32 PoseParameterZeroes = 0;                     // slot 345 `SetPoseParameter("move_yaw", 0, 0)`
	int32 DevMessages = 0;                             // DevMsg / DevWarning transcriptions
	FString LastDevMessage;
	int32 CrashGuards = 0;                             // arms where retail dereferences a null
	int32 WeaponSearches = 0;                          // 0x10333ad0
	int32 HintLockAttempts = 0;                        // 0x102d1350
};

FStartTaskNavRecord StartTaskNav;

/** `CAI_Navigator +0x40` — the route search time `SetRouteSearchTime` (`0x102886f0`) writes and the
 *  route builder `0x102f1dc0` reads: 0 fails a missing route at once (`OnNavFailed(0xc)`), anything
 *  else defers it and sets `m_afMemory` bit `0x20`. Seconds. */
float NavRouteSearchTime = 0.f;

/** `CAI_Path +0x20` — the scalar `0x102f2fc0` reads and `0x102f2fe0` writes around slot 563
 *  `TranslateEnemyChasePosition` in `TASK_GET_PATH_TO_ENEMY_LKP`. Retail name unrecovered. */
float NavPathScalar20 = 0.f;

/** `CBaseEntity::m_lifeState` (`+0x200`) as `TASK_DIE`'s start arm (`0x1028680c`) writes it: 1 =
 *  LIFE_DYING. The NPC line has no other owner of the word (the player's is `FElysiumPlayer::
 *  LifeState`); the mind's dead state is this runtime's account of the rest of the lifecycle. */
int32 LifeStateRetail = 0;

/** `CAI_Navigator::SetGoal` (`0x102ecd20`) as the StartTask arms drive it, onto this runtime's
 *  navigator (`IElysiumNpcMotor`). `SetGoalFlags` is the call's second argument (0, 2, or 4). On a
 *  built route the navigator's own slot 2 completes the task unless slot 529 says the task is a
 *  continuous move (`0x102f1dc0`); on a refused one with no route search time it fails it with
 *  `0xc` through `OnNavFailed` (`0x102eeae0`). */
bool StartTaskSetGoal(const FStartTaskNavGoal& Goal, int32 SetGoalFlags);

/** `CAI_Navigator::ClearGoal` (`0x102ee270`). */
void StartTaskClearGoal();

/** `CAI_Navigator::SetMovementActivity` (`0x102ee250`): `path+0x2c = activity`. */
void StartTaskSetMovementActivity(int32 Activity);

/** `CAI_Navigator::SetArrivalActivity` (`0x102ee410`). **SEAM** on the write side: the mover has no
 *  arrival activity; recorded. */
void StartTaskSetArrivalActivity(int32 Activity);

/** `SetArrivalDirection(const Vector&)` (`0x102ee530`). **SEAM** on the write side: the mover has
 *  no arrival direction; recorded. */
void StartTaskSetArrivalDirection(const FVector& Direction);

/** `SetArrivalDirection(const QAngle&)` (`0x102ee550`). **SEAM** on the write side: recorded. */
void StartTaskSetArrivalDirectionAngles(const FVector& ArrivalAngles);

/** `CAI_Navigator::GetCurWaypointPos` (`0x102ee5e0` → `0x1030b7c0`). **SEAM**: the mover keeps no
 *  waypoint list; the commanded goal (`FElysiumNpc::MoveGoal`) stands for the current waypoint while
 *  a route is out, the NPC's own origin when none is. */
FVector StartTaskCurWaypointPos() const;

/** `0x102e0b40` — `m_pMotor+0x2c = -1.0`, the yaw-speed hold every facing arm opens with (reached
 *  through thunk `0x10009980`). **SEAM**: no motor word; counted. */
void StartTaskMotorHoldYaw();

/** The motor's ideal-yaw store as `0x10288670` / the tail of `0x102e2020` write it: flip by 180
 *  when `motor+0x28` is set, then `motor+0x1c == 180.0f` stores straight into `motor+0x34`
 *  (`MotorIdealYaw`) while anything else goes through `0x102e0a80`. No `+0x1c` word stands here, so
 *  the direct store is what runs (the same call `NPCInit` makes). */
void StartTaskMotorSetIdealYaw(float Yaw);

/** `CAI_Motor::SetIdealYawToTarget(pos, 0)` (`0x102e2020`): `CalcIdealYaw` (slot 515, through
 *  `0x102e2750`) then the store above. */
void StartTaskMotorSetIdealYawToTarget(const FVector& TargetCm);

/** `UTIL_AngleMod` as `0x10288590` inlines it: `((int)(yaw * 65536/360) & 0xffff) * 360/65536`. */
static float StartTaskAngleMod(float Yaw);

/** `AngleVectors` (`0x10139610`) over retail angles (pitch, yaw, roll in degrees), answered in this
 *  runtime's position space (Source Y reflected, as `FElysiumNpc::FindCoverFromEnemy`'s right step
 *  is). Either output may be null. */
static void StartTaskAngleVectors(const FVector& Angles, FVector* OutForward, FVector* OutRight);

/** The weapon range clamp arms `0x0a`, `0x0d`, `0x10` and `0x1a` repeat verbatim: no active weapon
 *  gives 0.0 / 2000.0; one gives `max(+0x8c0, +0x8c4)` (ties to `+0x8c4`) and `min(+0x8b8, +0x8bc)`
 *  (ties to `+0x8bc`); then `max = min(max, m_flDistTooFar)`. Source units. */
void StartTaskWeaponRange(float& OutMinUnits, float& OutMaxUnits) const;

/** **SEAM** for the active weapon's four range words `+0x8b8 +0x8bc +0x8c0 +0x8c4` (Source units).
 *  No port weapon record carries them; answers false and leaves the four at zero. */
bool StartTaskWeaponRangeWords(const FElysiumEntity& Weapon, float OutWords[4]) const;

/** `CAI_Navigator::FindCoverPos` (`0x102edc80`): the port's navigator answers node cover through
 *  `IElysiumNpcMotor::FindNodeCover`, which takes the MAXIMUM radius only — the minimum is recorded
 *  and not honoured (named divergence). */
bool StartTaskFindCoverPos(const FVector& ThreatCm, const FVector& ThreatEyeCm, float MinUnits,
	float MaxUnits, FVector& OutCm);

/** **SEAM** for `CAI_Navigator::FindLosPos` (`0x102edaa0`), called by all four arms as
 *  `(threatPos, threatEye, min, max, 1.0 (0x3f800000), false, &out)` — no line-of-sight node search
 *  stands here; answers false (retail's "Couldn't find shoot position" arm). */
bool StartTaskFindLosPos(const FVector& ThreatCm, const FVector& ThreatEyeCm, float MinUnits,
	float MaxUnits, FVector& OutCm);

/** **SEAM** for `CAI_Navigator::SetRandomGoal` (`0x102ed940` → `0x102ed430`) — no node graph;
 *  answers false. */
bool StartTaskSetRandomGoal(float DistanceUnits, const FVector& Direction);

/** **SEAM** for `CAI_Navigator::SetWanderGoal` (`0x102ed540`: five `RandomFloat(min, max)` /
 *  `RandomFloat(0, 359.99)` tries, then `SetRandomGoal(1.0, vec3_origin)`) — no node graph; answers
 *  false. */
bool StartTaskSetWanderGoal(float MinUnits, float MaxUnits);

/** `CAI_BaseNPC::FindLateralCover` (`0x102784a0`): `TestLateralCover` at `GetOrigin()`, then five
 *  pairs of lateral steps of `48.0` units (`_DAT_10447ee8`) along the right vector, left first. */
bool StartTaskFindLateralCover(const FVector& ThreatEyeCm, const FElysiumEntity* Ignore);

/** `CAI_BaseNPC::TestLateralCover` (`0x10278220`): the eye-height ray from the threat's eye must be
 *  BLOCKED (mask `0x2804091`), slot 548 `IsValidCover` must accept the point, `MoveLimit` must reach
 *  it, and then a type-4 goal there (movement activity `0x13`, tolerance `-1.0`) is set with
 *  `SetGoal(.., 1)`, whose answer is the test's. */
bool StartTaskTestLateralCover(const FVector& ThreatEyeCm, const FVector& PointCm,
	const FElysiumEntity* Ignore);

/** `CAI_Enemies::GetLastKnownPosition` (`0x102dfed0`) over this NPC's memory (slot 541 answers the
 *  NPC's own store while squads are absent): the entity's record, else the last position-only
 *  record with its DevWarning, else the zero vector with the other DevWarning. */
FVector StartTaskLastKnownPosition(const FElysiumEntity* Entity);

/** `CAI_BaseNPC::GetFlinchActivity` (`0x10265970`): the last hit group (`+0x1594`) picks
 *  `0x66/0x68/0x69/0x6a/0x6b/0x6c` for groups 1/3/4/5/6/7, else `0x49`; a model with no sequence for
 *  the pick answers `0x49`. */
int32 StartTaskFlinchActivity();

/** The `Activity:` operand of a task as retail's `(int)flTaskData`. Retail's parser stored
 *  `ActivityList_IndexForName(name)` (`0x1025d760`) there; this runtime's parser stores a session
 *  registry id, so the name is recovered from the corpus registry and turned into a retail number
 *  through the port's one name→activity seam (`ActivityIdForName`, which answers -1). */
int32 StartTaskActivityOperand(const FElysiumScheduleStep& Step) const;

/** `0x102d12e0(hint)` — the hint's yaw: a hint bound to a network node (`m_nNodeID +0x5e4 != -1`)
 *  answers the node's yaw through family Hints' `HintYaw` seam; an unbound one answers its own
 *  `GetAngles().y` (slot 221), which `FHintWords::Angles` carries. False when the hint does not
 *  resolve or the node seam refuses. */
bool StartTaskHintYaw(int32 HintNode, float& OutYaw) const;

/** `0x102d11f0(hint, &out)` — `AngleVectors` of `0x102d12e0`'s yaw: the hint's facing, which the
 *  cover arms hand to `SetArrivalDirection`. Answers whether the yaw was known. */
bool StartTaskHintFacing(int32 HintNode, FVector& OutDirection) const;

/** **SEAM** for `CBaseCombatCharacter::Weapon_FindUsable` (`0x10333ad0`) — answers null. */
FElysiumEntity* StartTaskWeaponFindUsable(const FVector& ExtentsUnits);

/** **SEAM** for `CBaseCombatCharacter::ChooseBestMeleeWeapon` (`0x10337230`) — answers false. */
bool StartTaskChooseBestMeleeWeapon();

/** **SEAM** for `CBaseCombatCharacter::ChooseBestRangedWeapon` (`0x10337300`) — answers false. */
bool StartTaskChooseBestRangedWeapon();

/** One retail `DevMsg` / `DevWarning` from this body, verbatim, onto the schedule trace. */
void StartTaskDevMessage(const FString& Message);
