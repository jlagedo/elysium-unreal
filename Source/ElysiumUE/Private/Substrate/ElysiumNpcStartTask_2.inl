// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- `CAI_BaseNPCTroika`'s
// helper declarations for the SECOND part of `StartTask 0x102a1910` (lane L02, arms
// `[0x102a5046, 0x102a77f7]`).
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, next to
// `ElysiumNpcStartTask.inl`; the definitions are in `ElysiumNpcStartTask_2.cpp`. Every name
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

/** `CAI_Navigator::SetGoal` (`0x102ecd20`): this half's goal literal (SOURCE units) handed to the ONE
 *  body, `FElysiumNpcBase::StartTaskSetGoal` (`ElysiumNpcBaseStartTask.cpp`). The goal is recorded
 *  first and converted; the body carries every arm (the route build `0x102f1dc0`, its complete through
 *  the navigator's slot 2 and its `OnNavFailed(0xc)`). The navigator services this half calls are the
 *  base's too: `StartTaskMotorHoldYaw` (`0x102e0b40`), `StartTaskMotorSetIdealYaw(ToTarget)`
 *  (`0x10288670` / `0x102e2020`), `StartTaskFindLateralCover` (`0x102784a0`), `StartTaskFindLosPos`
 *  (`0x102edaa0`), `StartTaskFindCoverPos` (`0x102edc80`), `StartTaskHintFacing` (`0x102d11f0`),
 *  `StartTaskSetArrivalActivity` / `StartTaskSetArrivalDirection` (`0x102ee410` / `0x102ee530`),
 *  `Conditions19LastKnownPosition` (`0x102dfed0`), `StartTaskAngleMod` (`0x10288590`), and the
 *  `m_lifeState` word `LifeState`. */
bool TaskTailNavSetGoal(const FTaskTailNavGoal& Goal, uint32 SetGoalFlags);

/** SEAM for `0x102a9c60` -> `0x102a9f20` -> `0x102a9ee0` / `0x102a9f00` -- the current waypoint,
 *  replaced by its successor when one exists, and its position. Answers false (no waypoint). */
bool TaskTailNavFacingWaypoint(FVector& OutPositionUnits) const;

// --- Motor and facing ----------------------------------------------------------------------------

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

/** SEAM for `0x1025df40(m_pAttackCoordinator, this, enemy)` -- the coordinator's side pick for a
 *  melee circle. `+0x65e8` is an index with no object behind it; answers 0, retail's "no opinion"
 *  and the arm that draws a random side. */
int32 TaskTailCoordinatorCircleSide(const FElysiumEntity* Enemy) const;

/** `GetActiveWeapon()->+0x8b8` (`m_fMinRange1`) -- the active weapon's minimum range, SOURCE units
 *  (`ElysiumWeapons::ItemRangeWords`; the neighbouring `+0x8c0` is `ActiveWeaponMaxRangeUnits`).
 *  False only when there is no active weapon. */
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

/** SEAM for `gEntList.FindEntityByName(NULL, name)` (`0x100f7770` on `0x106eb5d8`) the kick-hint
 *  arm resolves its prop through; answers the named entity from the world, or none. */
FElysiumEntityHandle TaskTailFindEntityByName(const FString& Name) const;

/** SEAM for `0x102a9c40` -- the hint's target name (`+0x468 m_strTargetName`), empty with no hint. */
FString TaskTailHintTargetName() const;

/** `0x102b52a0(this, 1, 1)`, `TASK_CLEAR_HATRED`'s reset, is family Boss19's `ResetAiState`. */

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

