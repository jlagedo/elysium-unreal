// Story 0019/8 (29e under the strict verdict), family **RunTask19** -- `CAI_BaseNPCTroika`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`; the definitions are in
// `ElysiumNpcRunTask19.cpp`, or generated in the slot files for a slot body.
//
// Owns (RunTask19's `rule` rows): 0x102aacf0 CAI_BaseNPCTroika::RunTask.
//
// Lane L05 (pass I). The helpers and seams the Troika spine calls that the port had no body for;
// each names the retail call it stands for, and the three searches that admitted each seam are in
// `pass-i/L05-report.md`.

// --- Non-slot helpers, ported ---------------------------------------------------------------------

/** `FUN_102aab70` `0x102aab70` -- the pre-dispatch aim of `TASK_WAIT` / `TASK_WAIT_RANDOM` /
 *  `TASK_WAIT_INDEFINITE` and the `TASK_PLAY_COVER_*` trio: with `m_bfAINPCFlags2 & 0x10` face the
 *  enemy's remembered position, else with `& 0x20` face `m_hTargetEnt`'s origin -- each only when slot
 *  364 `FInAimCone` refuses the point, at yaw speed -2.0. */
void FacePendingAimTarget();

/** `FUN_102c4e30` `0x102c4e30` -- stop the jump/knockback motion: slot 93 `SetMoveType(5, 0)`,
 *  `+0x3ec` (the entity gravity word) = 0, zero local angular velocity, zero absolute velocity. */
void JumpHaltMotion();

// --- Seams (the port has no body for the retail call; each answers retail's "nothing" arm) --------

// `FUN_102aa860` `0x102aa860(this, &m_sppPatrolPath | &m_sppPatrolPathHunt)`, the patrol goal step
// of `TASK_GET_PATH_TO_PATROL_POINT(_HUNT)`, is lane Script19's `IssuePatrolMoveRun` (the L05
// integration redirected the arms and dropped this lane's recording seam).

/** `FUN_102f2ea0` `0x102f2ea0` -- `CAI_Navigator`: the goal is within tolerance (2-D distance to the
 *  goal under the tolerance and the height gap within `StepHeight`), and on success it dispatches the
 *  navigator's slot 8. **SEAM**: the mover keeps no readable goal (`NavGoalPosition` answers none);
 *  answers false -- "not there yet" -- so `TASK_WAIT_FOR_MOVEMENT` validates the goal and keeps
 *  running, and the mover's own arrival clears the goal type. */
bool NavArrivedWithinTolerance();

/** `FUN_102a0870` `0x102a0870` -- with `COND 0x37`, make `m_hKnockbackHitEntity` (`+0x6010`) play its
 *  knockback reaction (`0x10344da0`, `GetKnockbackActivity`, its slot 320). **SEAM**: counted. */
int32 KnockbackHitEntityReactions = 0;
void KnockbackHitEntityReact();

/** `FUN_102a0490` `0x102a0490` -- with `COND 0x36`, the touch trace's plane normal and five hull
 *  traces up the wall (`0x201400b`); true when the wall is tall enough to fly into. **SEAM**: no
 *  touch trace or hull trace stands here; answers false (no wall), leaving `OutNormalUnits`
 *  untouched. SOURCE axes. */
bool KnockbackWallProbe(FVector& OutNormalUnits);

/** `FUN_103454c0` / `FUN_10345480` -- the current sequence's studio `+0x2ec` / `+0x2e8` linked
 *  sequence name looked up (`LookupSequence`), or -1. **SEAM**: the animating tier stands no
 *  sequence descriptors; answers -1, the arm that restarts the ideal activity instead. */
int32 SequenceLinkedNext() const;
int32 SequenceLinkedLand() const;

/** `FUN_102c4eb0` `0x102c4eb0` -- landed: `FL_ONGROUND`, else a hull trace one unit down from the
 *  origin that adopts the ground entity it hits (slot 208). The flag arm is `IsOnGroundFlag`;
 *  **SEAM** for the trace arm, which answers "no ground". */
bool KnockbackLanded();

/** `FUN_102c4e80` `0x102c4e80` -- the jump commit. **SEAM**: counted on `MotorSeams.SetupJumpCommits`
 *  (family VampireBoss's `CommitSetupJump` forwards here). */
void JumpCommit();

/** `FUN_102c1400` `0x102c1400` -- `TASK_RUN_DIALOG`'s activity for the current dialogue line, or -1
 *  when the dialogue is done. **SEAM**: the dialogue pump is not wired to the kernel (family
 *  Payphone seams the same address); answers -1, the arm that ends the task. */
int32 RunDialogActivity();

/** `CBaseCombatCharacter::BloodExplode` `0x1033d4b0` (`TASK_DIE_EXPLODE_GIB`). **SEAM**: counted. */
int32 BloodExplodeCalls = 0;
void BloodExplode();

/** `CBaseCombatCharacter::Die(credit, 0, 0)` (`0x103392c0`), the whole body, as the Troika death arms
 *  call it: a packet crediting `Credit`, `SetBaseToStatValue(0xf, 0x11)`, slot 144, slot 403 -- on a
 *  body whose `m_lifeState` is not LIFE_DEAD. The two flag bytes (`info+0x48/+0x49`) are 0 at every
 *  caller here and are not carried. The credit and a call count are kept for the tests. */
FElysiumEntityHandle LastDieCredit;
int32 RunTaskDieCalls = 0;
void Die(const FElysiumEntity* Credit);

/** `CAI_InterestingPlace +0x571 m_bHolsterWeapon` (key `holster_weapon`) and the weapon's slot 315
 *  (`+0x4ec`) the two interest arms call when it is set. **SEAM**: `FElysiumInterestingPlace` carries
 *  no holster word; answers false, and the holster is counted when reached. */
bool PlaceHolstersWeapon(const FElysiumInterestingPlace& Place) const;
int32 WeaponHolsterCalls = 0;

/** `+0x0fd8 m_bLastDisciplineResist` (`CBaseCombatCharacter`), read by `TASK_DO_LOOP_ACTIVITY` /
 *  `TASK_DO_BLEND_LOOP_ACTIVITY`. **SEAM** word: nothing in the port writes it. */
bool bLastDisciplineResist = false;

/** `+0x0178 m_flLastThink` (`CBaseEntity`), which `TASK_WAIT_PVS`'s pass stamps beside the four
 *  Troika think stamps. **SEAM** word: no port member carried it and nothing reads it. */
double EntityLastThink = 0.0;

// `+0x0ff8 m_hBodyFireParticles[18]`, the burn emitters `TASK_ON_FIRE_LOOP` stops, is family
// Conditions19's `BodyFireParticles` (one word; the L05 integration dropped this lane's second copy).
// The stop (the emitter's slot 242, then `0x100fbbb0(emitter, 2.0)`) is counted.
int32 BodyFireParticleStops = 0;

/** The damage force `TASK_MELEE_HIT_BY_FINISHING_MOVE` writes into its `CTakeDamageInfo` (`+0x10`):
 *  the bone's travel since the last sample, normalized, times the elapsed time times 50000.0. The
 *  port's damage info carries no force word; recorded. SOURCE axes. */
FVector FinishingMoveDamageForce = FVector::ZeroVector;

// --- The melee-swing reads the species `RunTask` bodies share (`CNPC_VHuman`, `CNPC_VMingXiao`,
// `CNPC_VMingXiaoTentacle`, `CNPC_VChangBros`) ------------------------------------------------------

/** `CBaseCombatCharacter::IsMeleeSwingOver` `0x10345330` on the enemy: with an active weapon, every
 *  event of the enemy's current sequence (up to 20) is at or below its `m_flCycle`. **SEAM**: the
 *  animating tier stands no sequence descriptors; answers true (the swing is over), the arm that lets
 *  the swing task end. */
bool EnemyMeleeSwingOver(const FElysiumCombatCharacter& Enemy) const;

/** `FUN_103498b0` `0x103498b0` -- the dice roll's band (0..4): the roll's margin against the four
 *  `_DAT_10739fa0..fac` defender thresholds of `rules.txt` (`ElysiumWeapons::ClassifyDefender`'s
 *  ladder, one lower); band 0 arms the swing task's completion. */
int32 MeleeRollBand(const FElysiumMeleeRoll& Roll) const;

/** The sequence-event walk `TASK 0x8d` (`CNPC_VHuman`) and `TASK 0x8e` (`CNPC_VMingXiao`,
 *  `CNPC_VMingXiaoTentacle`) run on the enemy: the projected cycle `(curtime - (m_flAnimTime - 0.1)) *
 *  GetSequenceCycleRate * m_flPlaybackRate + m_flCycle` against every event's cycle; true while one
 *  is still ahead. **SEAM**: no sequence descriptor stands at the kernel tier; answers false (every
 *  event has passed), the arm that completes. `bOutHasSequence` is retail's `GetSeqDesc != 0`; the
 *  seam answers true. */
bool EnemySequenceEventsPending(const FElysiumCombatCharacter& Enemy, bool& bOutHasSequence) const;
