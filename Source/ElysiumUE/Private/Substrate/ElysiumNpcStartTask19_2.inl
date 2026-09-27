// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- `CAI_BaseNPCTroika`'s
// helper declarations for the SECOND part of `StartTask 0x102a1910` (lane L02, arms
// `[0x102a5046, 0x102a77f7]`).
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, next to
// `ElysiumNpcStartTask19.inl`; the definitions are in `ElysiumNpcStartTask19_2.cpp`. Every name
// carries the `TaskTail` prefix so it can never collide with the first part's helpers.
//
// A SEAM below answers "nothing" (or retail's admitting value) and names the retail call or word
// it stands for; each was admitted only after the three searches in
// `$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/pass-i/L02-report.md`.

// --- Retail words no port system carried ----------------------------------------------------------

/** SEAM word: `+0x0fd4 m_iLastDisciplineHitBy` (`CBaseCombatCharacter`, datamap). Read by
 *  `TASK_PLAY_PARTIAL_RESIST_ACTIVITY` / `TASK_PLAY_FULL_RESIST_ACTIVITY` (`0x102a521d`,
 *  `0x102a5283`). No port producer writes it yet (the discipline hit path owns the write). */
int32 LastDisciplineHitBy = 0;

/** SEAM word: `+0x0fdc m_iLastStoredDmg` (`CBaseCombatCharacter`). `TASK_DO_DAMAGE` (`0x102a5095`)
 *  reads it when its operand is negative. No port producer writes it yet. */
int32 LastStoredDamage = 0;

/** SEAM word: `+0x0200 m_lifeState` (`CBaseEntity`). `TASK_SET_DYING` (`0x102a778e`) writes 1
 *  (`LIFE_DYING`). The runtime carries death in the mind's dead state and `FElysiumEntity::bDead`
 *  (the reap flag), neither of which is this word; it is held here verbatim. */
int32 LifeStateRetail = 0;

// --- The navigator goal literal (`AI_NavGoal_t`, built by `0x102a9c80` / `0x102a9d20` /
// `0x102a9dc0`) ----------------------------------------------------------------------------------

struct FTaskTailNavGoal
{
	int32 Type = 0;                              // +0x00 GoalType
	FVector DestUnits = FVector::ZeroVector;     // +0x04 dest, SOURCE units
	int32 DestNode = INDEX_NONE;                 // +0x10
	int32 Activity = INDEX_NONE;                 // +0x14
	float Tolerance = 0.f;                       // +0x20 (-1 AIN_DEF_TOLERANCE, -2 AIN_HULL_TOLERANCE)
	uint32 GoalFlags = 0;                        // +0x24
	FElysiumEntityHandle Target;                 // +0x28
};

/** The last goal `TaskTailNavSetGoal` was handed and the `SetGoal` flags beside it -- the recovered
 *  half of every goal arm, readable by the tests. */
FTaskTailNavGoal TaskTailLastNavGoal;
uint32 TaskTailLastNavGoalFlags = 0;
int32 TaskTailNavGoalCalls = 0;

/** `CAI_Navigator::SetGoal` (`0x102ecd20`) as this runtime's navigator is reached: the body's motor
 *  (`IElysiumNpcMotor::MoveTo`), the same service `GetPathToEnemy` drives, under the schedule body
 *  claim. The goal is recorded first. Answers false with no motor, a refused claim or a refused
 *  path -- retail's "no route" answer. */
bool TaskTailNavSetGoal(const FTaskTailNavGoal& Goal, uint32 SetGoalFlags);

/** SEAM for `0x102ee410` / `0x102ee530` -- the navigator's arrival activity and arrival direction
 *  (`m_pPath` words). The mover keeps neither; recorded only. */
int32 TaskTailArrivalActivity = INDEX_NONE;
float TaskTailArrivalYaw = 0.f;

/** SEAM for `0x102a9c60` -> `0x102a9f20` -> `0x102a9ee0` / `0x102a9f00` -- the current waypoint,
 *  replaced by its successor when one exists, and its position. Answers false (no waypoint). */
bool TaskTailNavFacingWaypoint(FVector& OutPositionUnits) const;

/** SEAM for `0x102edaa0` -- the navigator's LOS-position finder `TASK_GET_PATH_TO_SAVEPOSITION_LOS`
 *  asks (`from, to, 0.0, 4096.0, 1.0, 1, &out`). No node graph here; answers false. */
bool TaskTailFindLosPosition(const FVector& FromUnits, const FVector& ToUnits, FVector& OutUnits) const;

/** SEAM for `FindLateralCover(threatEye, threat)` (`0x102784a0`, `RET 8`): the lateral-cover search
 *  (origin, then five 48-unit steps each side) that sets its own goal on success. Its port body lives
 *  only inline inside `FElysiumNpc::FindCoverFromEnemy` (`ElysiumNpc.cpp`); answers false, the arm
 *  that goes on to the node-cover search. */
bool TaskTailFindLateralCover(const FVector& ThreatEyeUnits, const FElysiumEntity* Threat);

/** SEAM for `0x102d11f0` -- the hint's facing direction the cover arm hands to the navigator.
 *  Answers false (no hint store). */
bool TaskTailHintDirection(FVector& OutDirection) const;

// --- Motor and facing ----------------------------------------------------------------------------

/** `0x102e0b40` -- `motor+0x2c = -1.0`, the motor's turn hold reset. The port's motor stands it as
 *  `ResetSteering` (the landed `MaintainSchedule` / `TaskFail` reading). */
void TaskTailMotorStopTurn();

/** `0x10288670` -- the motor's `SetIdealYaw`: flip by 180 under `motor+0x28`
 *  (`bMotorAnimationMovement`), then store `m_IdealYaw` (`MotorIdealYaw`) and turn the body toward
 *  it (`IElysiumNpcMotor::Face`, the motor service `FaceSavePosition` uses). */
void TaskTailMotorSetIdealYaw(float YawDegrees);

/** `0x102e2020` -- the motor's ideal yaw toward a point (`0x102e2750` is the yaw to the point),
 *  then the same tail as `0x10288670`. SOURCE units. */
void TaskTailMotorFacePosition(const FVector& PositionUnits);

/** `0x10297940` -- face a point WITH a turn animation: stop-turn, ideal yaw toward the point, the
 *  face-anim pick `FUN_10297a20`, `PLAYING_FACE_ANIM` (`0x8000000`) into `m_bfAINPCFlags`, then
 *  ideal yaw = `GetAbsAngles().y + m_flFaceYawDiff`. SOURCE units. */
void TaskTailFaceWithAnim(const FVector& PositionUnits);

/** `0x102e0a80`-side yaw: `GetAbsAngles()[1]` (`0x102885f0` + `0x102a9600(1)`). */
float TaskTailAbsYaw() const;

// --- Anim, gesture, cover-anim helpers ------------------------------------------------------------

/** SEAM for `0x10099250` -- `AddGesture(act, 2.45, 0)`: a gesture layer whose rate is
 *  `f(seq) / duration` (layer `+0x744`). The overlay tier plays no numbered gestures here; the
 *  request is recorded. */
int32 TaskTailLastGestureActivity = INDEX_NONE;
float TaskTailLastGestureDuration = 0.f;
int32 TaskTailGestureCalls = 0;

/** `0x102a1560` / `0x102a1590` / `0x102a15c0` / `0x102a15f0` / `0x102a1620` -- the five cover-anim
 *  restarts: the hint-activity lookup (`0x102a13d0` .. `0x102a1510`, `HintNodeActivity`), and when
 *  it answers an activity, `RestartIdealActivity(act)` and true; else false. */
bool TaskTailRestartHintActivity(EElysiumHintActivityQuery Query);

/** `0x10345480` -- the ideal sequence's own activity index when the model has one
 *  (`m_nSequence +0x6f0` resolved through the studio header), else -1. No studio header is readable
 *  from the kernel; SEAM answering -1. */
int32 TaskTailFlyingIdleSequence() const;

// --- Enemy, memory, cover -------------------------------------------------------------------------

/** Slot 167 `GetEnemy()` (`0x101a67e0`) dispatched as retail does from a Troika body -- the
 *  base-pointer const overload, which the Troika's non-const slot 168 hides by name. */
FElysiumEntity* TaskTailEnemy167() const;

/** `CAI_Enemies::GetLastKnownPosition` (`0x102dfed0`) through slot 541: the memory record's
 *  position for `Target`, else the zero vector (retail's miss arm). SOURCE units. The eluded-record
 *  fallback and its two `DevWarning`s are not reproduced (no record carries `+0x34`). */
FVector TaskTailEnemyLkpUnits(const FElysiumEntity* Target) const;

/** SEAM for `0x1025df40(m_pAttackCoordinator, this, enemy)` -- the coordinator's side pick for a
 *  melee circle. `+0x65e8` is an index with no object behind it; answers 0, retail's "no opinion"
 *  and the arm that draws a random side. */
int32 TaskTailCoordinatorCircleSide(const FElysiumEntity* Enemy) const;

/** SEAM for `GetActiveWeapon()->+0x8b8` -- the active weapon's minimum range, SOURCE units. No
 *  weapon record carries it (the neighbouring `+0x8c0` is `ActiveWeaponMaxRangeUnits`); false. */
bool TaskTailWeaponMinRangeUnits(float& OutRangeUnits) const;

/** SEAM for `cvar_debug_circle_dist_override` (`0x10924890`, "Overrides the distance guys want to be
 *  outside the melee combat."), read `IsCommand() ? 0.0 : m_fValue` through `0x102a9640`. It is not
 *  in the generated ConVar table (`ElysiumNpcKernelTunables.h`); its default cell `0x105399a0` is
 *  unrecovered, so this answers 0.0 -- the value that leaves the override off. */
static float TaskTailCircleDistOverride();

// --- Misc services ---------------------------------------------------------------------------------

/** SEAM for `0x102ca2a0` on the act registry `DAT_109253f8` -- register an external supernatural
 *  act (`"CSActs: ... External %s act level %d"`). No act registry stands here; recorded. */
int32 TaskTailSupernaturalActs = 0;
float TaskTailLastSupernaturalActDuration = 0.f;

/** SEAM for `CSoundEnt::InsertSound(8, pos, volume, 10.0, flag, this)` (`0x101bac90`) with the
 *  volume and flag of row `0x1b` of `DAT_1072bc20` (`0x102a9ea0` / `0x102a9ec0`). The row's
 *  category name is unrecovered, so it cannot be emitted on the game-sound bus; recorded. */
int32 TaskTailDangerSounds = 0;

/** SEAM for `0x102c41b0(this, name, 0, 0, 0)` -- the particle dispatch the knockback land arm fires
 *  (`"impact_dust_emitter"`). Recorded by name. */
TArray<FString> TaskTailParticleDispatches;

/** SEAM for `0x102d0910(hint, this)` -- the hint's `OnHintUsed`-style output (`hint+0x5bc`) and its
 *  target lookup. No hint entity stands here; counted. */
int32 TaskTailHintFires = 0;

/** SEAM for `gEntList.FindEntityByName(NULL, name)` (`0x100f7770` on `0x106eb5d8`) the kick-hint
 *  arm resolves its prop through; answers the named entity from the world, or none. */
FElysiumEntityHandle TaskTailFindEntityByName(const FString& Name) const;

/** SEAM for `0x102a9c40` -- the hint's target name (`+0x468 m_strTargetName`), empty with no hint. */
FString TaskTailHintTargetName() const;

/** SEAM for `0x102b52a0(this, 1, 1)` -- `TASK_CLEAR_HATRED`'s reset (memory bits `0x08038000`, the
 *  ideal state back to IDLE with its `+0x1b3c` trace). The body is Boss19's (lane L12, "ResetAiState");
 *  this stands until it lands and the integrator redirects the call. Counted. */
void TaskTailResetAiState(int32 First, int32 Second);
int32 TaskTailResetAiStateCalls = 0;
FIntPoint TaskTailLastResetAiStateArgs = FIntPoint::ZeroValue;

/** SEAM for `0x102b6890(this, prop)` -- the physics kick applied to a prop. Counted, with the prop. */
int32 TaskTailKicks = 0;
FElysiumEntityHandle TaskTailLastKicked;

/** SEAM for the activity copy-prop family `0x1018e790` / `0x1018e9d0` / `0x1018eab0` /
 *  `0x1018eb50` / `0x1018ecf0` / `0x1018ec20` (the `activity_copy_prop` entity's own spawn / count /
 *  random / fadeout / forward / nivbed calls). No such entity class stands; each is recorded by
 *  address, and the fadeout answers 0 (the arm that keeps the task running). */
int32 TaskTailCopyPropCalls = 0;
uint32 TaskTailLastCopyPropCall = 0;
int32 TaskTailCopyPropFadeout(uint32 RawOperand);

/** SEAM for `0x102c4c50(this, &vel, height, origin, target)` -- the jump arc solve `TASK_JUMP`
 *  hands to motor vtable `+0x18`. No solver here; answers false, the velocity left zero. */
bool TaskTailSolveJump(FVector& OutVelocityUnits) const;
int32 TaskTailJumpApplies = 0;

/** SEAM for `0x102ae310` -- the interesting-place weapon holster/unhide policy
 *  (`m_bHolsterWeapon +0x571`, `GetState`, the closest player, weapon slot `+0x4ec`) that
 *  `TASK_FACE_INTEREST` runs first. Not ported (no hide/unhide surface on the weapon here); counted. */
int32 TaskTailHolsterChecks = 0;

/** An interesting place by entity index (`+0x6300`'s cached form), or null. */
FElysiumInterestingPlace* TaskTailPlaceAt(int32 EntityIndex) const;

/** The movement-activity tail `0x102a5904`: `0x102ee250(nav, act)`, `0x102a98e0(this, 2)`
 *  (`m_afMemory &= ~bits_MEMORY_INCOVER`), `TaskComplete(false)`. */
void TaskTailMovementActivity(int32 Activity);

/** The look tail `0x102a7744`: `0x10297940(pos)` then `0x102a18a0(task)`. SOURCE units. */
void TaskTailLookAt(const FVector& PositionUnits, float TaskSeconds, double Now);

/** The facing tail `0x102a63fc`: `0x10288670(motor, yaw)`, return running. */
void TaskTailFaceYaw(float YawDegrees) { TaskTailMotorSetIdealYaw(YawDegrees); }
