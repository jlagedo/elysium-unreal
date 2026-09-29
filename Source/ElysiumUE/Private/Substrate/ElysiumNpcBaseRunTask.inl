// Story 0019/8 (29e under the strict verdict), family **RunTask19** -- `CAI_BaseNPC`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpcBase` by `Substrate/ElysiumNpcBase.h`; the definitions are in
// `ElysiumNpcBaseRunTask.cpp`, or generated in the slot files for a slot body.
//
// Owns (RunTask19's `rule` rows): 0x10288780 CAI_BaseNPC::RunTask.
//
// Lane L05 (pass I). Every declaration below is a SEAM or a motor/navigator primitive the two
// RunTask spines call and the port had no body for; each names the retail call it stands for.
// The three searches that admitted each one are in the lane report (`pass-i/L05-report.md`).

// --- `CAI_Motor` yaw primitives (`m_pMotor`, `+0x5d44`) --------------------------------------------
//
// The motor's ideal yaw is `MotorIdealYaw` (motor `+0x34`, family Lifecycle19). These four bodies are
// the only writers the RunTask spines use; before this lane the port had empty seams over three of
// them (`SetAlternateAiIdealYaw`, `SetMotorHintYaw`, `ReleaseMotorHintYaw` -- listed for the
// integrator to redirect here).

/** `CAI_Motor` `+0x2c` -- the yaw-integration clock `0x102e1e20` reads (a negative stamp restarts the
 *  integration at `curtime - 0.1`). `0x102e0b40` writes `-1.0` and nothing else. */
float MotorYawClock = 0.f;

/** `CAI_Motor` `+0x38` -- the per-call yaw speed `0x102e1c10` stores when its speed argument is
 *  neither `-1.0` nor the `-2.0` "keep the current speed" sentinel (`_DAT_10462978`). */
float MotorYawSpeedWord = 0.f;   // read by `MotorUpdateYaw`, `PrepareMoveRequest` and `MotorThinkUpkeep` as the mover's turn rate (0019/6)

/** How many times `0x102e1e20` ran; the tests assert the turn happened. */
int32 MotorUpdateYawCalls = 0;

/** `FUN_102e0b40` `0x102e0b40`, the whole body: `motor+0x2c = -1.0` (the yaw clock reset). */
void MotorMoveStop();

/** `FUN_102e1c10` `0x102e1c10` -- set the ideal yaw (`motor+0x34`) and update. The `+0x28`
 *  animation-movement latch (`BaseScheduleHost.bMotorAnimationMovement`) flips it by 180; the
 *  `+0x1c == 180.0` immediate-assign arm is taken (no port motor carries a clamped max-yaw word,
 *  `0x102e0a80`); a speed of `-1.0` runs `0x102e1cf0` (`MotorStoreMaxYawSpeed`) and any other
 *  speed but `-2.0` is stored at `+0x38`; both arms end in `UpdateYaw(-1)`. */
void MotorSetIdealYawAndUpdate(float YawDegrees, float YawSpeed);

/** `FUN_102e1cf0` `0x102e1cf0` -- motor `+0x38` := the outer's slot 516 `MaxYawSpeed()` (the SDK's
 *  `RecalculateYawSpeed`). Reached from `0x102e1c10`'s `-1.0` arm and from the tail of
 *  `SetActivityAndSequence` `0x10272490` (`0x10272569` / `0x10272575`). */
void MotorStoreMaxYawSpeed();

/** The turn rate the mover is handed for a retail yaw speed, degrees per second. **Recovered from
 *  the SDK, not from this binary** (the corpus was down at 0019/6): `CAI_Motor::UpdateYaw(int
 *  yawSpeed)` takes the speed as an INT and turns by `AI_ClampYaw(yawSpeed * 10.0, ...)`, i.e. the
 *  ladder is in degrees per tenth of a second -- the unit `Rules.txt`'s `Ming_Xiao_Info` names for
 *  `TurnSpeedNormal` / `TurnSpeedAttack`, which slot 516 `0x10394930` returns verbatim. The turning
 *  arm's 1.0 floor is why an int truncation never reaches 0. Whether `0x102e1e20` keeps both is
 *  **unrecovered** until the corpus reads it. */
static float MotorYawRateDegPerS(float RetailYawSpeed);

/** `FUN_102e20b0` `0x102e20b0` -- `0x102e2750` (the yaw from this body to `TargetCm`) then
 *  `0x102e1c10(yaw, speed)`. */
void MotorSetIdealYawToTargetAndUpdate(const FVector& TargetCm, float YawSpeed);

/** `FUN_102e1e20` `0x102e1e20` -- `UpdateYaw(speed)`: step the body's yaw toward `motor+0x34` and
 *  stamp the yaw clock. **Named modernization**: the step itself is Unreal's (`IElysiumNpcMotor::Face`
 *  turns the capsule at the rate it is handed), so the substrate writes the clock and hands the mover
 *  the ideal yaw and retail's rate (`-1` = the stored `+0x38`, as `UpdateYaw(-1)` reads it);
 *  `_DAT_1044fac0` and `0x102e1d10`'s clamp are not run. */
void MotorUpdateYaw(int32 YawSpeed);

// --- `CAI_Navigator` (`m_pNavigator`, `+0x5d34`) ------------------------------------------------------

// `FUN_102ee6a0` (`m_pPath && m_pPath->+0x24`) is family SaveRestore10's `NavigatorGoalIsActive()`
// and `FUN_102ee620`'s non-zero test is family Motor's `NavIsGoalActive()` (`0x102ee680` is exactly
// `NEG/SBB/NEG` over the same `0x100113d8` call); both are called, not re-declared.

/** `FUN_102ee220` `0x102ee220` -- `UpdateGoalPos(pos)`: re-aim the navigator's goal at a new point.
 *  **SEAM**: the mover exposes no goal re-aim; the point is recorded and nothing moves. */
FVector NavUpdatedGoalCm = FVector::ZeroVector;
int32 NavUpdateGoalCalls = 0;
void NavUpdateGoalPos(const FVector& GoalCm);

/** `FUN_102ee250` `0x102ee250` -- `m_pPath->m_movementActivity (+0x2c) = act`. The word is
 *  `FElysiumNpcNavigator::MovementActivity` on the Troika line (every live NPC); a base-only
 *  NPC (a director) has no navigator word here and the write is dropped. */
void NavSetMovementActivity(int32 Activity);

/** `FUN_102ee270` `0x102ee270` -- `ClearGoal`: `Motor->ClearNavigationGoal()` and the tally the
 *  landed bodies keep (`NavigationGoalClears`). */
void NavClearGoal();

// --- The remaining calls with no port body -------------------------------------------------------------

// `FUN_101d1800` `0x101d1800` -- `UTIL_FindClientInPVS(edict)`, the base `TASK_WAIT_PVS` test -- is
// family Conditions19's `Conditions19ClientInPvs()` (one body for the one call; the L05 integration
// folded this lane's second body into it).

/** `FUN_10279420` `0x10279420` -- the "flat bounding box" test `TASK_DIE` asks before resizing the
 *  hull. **SEAM**: answers false (the `(4,4,1)/(-4,-4,0)` arm), the arm every NPC without a
 *  collision box takes; the retail predicate is unrecovered. */
bool DeathHullIsFlat() const;

/** `FUN_101babc0` `0x101babc0` -- `CSoundEnt::InsertSound(type, origin, volume, duration, owner)`.
 *  **SEAM**: no AI-sound insertion into the game-sound store takes a raw type; recorded. */
int32 InsertedAiSoundType = 0;
int32 InsertedAiSoundVolume = 0;
float InsertedAiSoundDuration = 0.f;
void InsertAiSound(int32 Type, const FVector& OriginCm, int32 Volume, float Duration);

/** `FUN_102695d0` `0x102695d0` -- `SUB_StartFadeOut`. **SEAM**: counted; the fade think is not built. */
int32 StartFadeOutCalls = 0;
void StartFadeOut();

/** The active weapon's `m_bInReload` (`CBaseCombatWeapon +0x898`) write and its slot 322
 *  (vtable `+0x508`), `TASK_RELOAD`'s finish. **SEAM**: `FElysiumWeapon` carries no reload state;
 *  counted. */
int32 WeaponFinishReloadCalls = 0;
void WeaponFinishReload(FElysiumEntity& Weapon);

/** `FUN_102521f0` `0x102521f0` on the `__RTDynamicCast`-to-`CBaseCombatWeapon` of `m_hTargetEnt`
 *  (`TASK_WEAPON_PICKUP`): the weapon's owner (`+0x88c`) as its combat character, through the
 *  port weapon's `OwnerCharacter()`; null when the target is not a weapon or is unowned. */
FElysiumEntity* TargetWeaponOwner(FElysiumEntity* Target) const;

/** `CBaseEntity::GetFlags() & FL_ONGROUND` (`0x100b3700`, bit 1). The entity's `Flags` word carries
 *  the bit (`NpcKernelLifecycle19Shared::GFlOnGround`), but no producer keeps it on an NPC body, so
 *  this reads the mover's grounded sample, as the landed `StopMovingTask` does; a body with no
 *  motor answers the `Flags` bit. */
bool IsOnGroundFlag() const;

// `+0x200 m_lifeState` as retail's raw id is the entity's `LifeState` (`ElysiumEntity.h`; one word; the
// L05 integration folded this lane's second copy into it): the base death arm writes 2
// (`0x10288ff7`), the Troika death arm reads/clears 1 (`0x102abc05`), Werewolf `0x15f` writes 2.
