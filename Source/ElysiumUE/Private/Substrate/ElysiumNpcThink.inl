// Story 0019/8 (29e under the strict verdict), family **Think19** -- `CAI_BaseNPCTroika`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`; the definitions are in
// `ElysiumNpcThink.cpp`, or generated in the slot files for a slot body.
//
// Owns (Think19's `rule` rows): 0x10298070 CAI_BaseNPCTroika::UpdateCharacter, 0x10292de0
// CAI_BaseNPCTroika::NPCThink.

// --- `CAI_BaseNPCTroika::NPCThink` (`0x10292de0`), phase by phase ---------------------------------
// The slot body (`NPCThink`, slot 431) calls these in retail's order; they are separate members so a
// case can drive one block without the others. Each carries the instruction addresses of its arms.

/** The "Set1" block (`0x10292e9d`..`0x10293271`), run only on a due normal think: the closest
 *  player, the enemy triple and its move-facing, `AngleVectors`, `SetPlayerLOS`,
 *  `CacheInterruptConditions`, `AutoMovement`, the hint upkeep, the shoot-at hint and the
 *  `"Scream_Death"` roll. */
void Think19NormalSet1(double Now);

/** Set1's enemy triple (`0x10292f03`..`0x1029306a`): `m_flEnemyDist`, `m_flEnemyHeightDiff`,
 *  `m_flEnemyLastKnownDist` (Source units), 20000.0 each with no live enemy, and the slot-517
 *  move-facing under `debug_allow_move_facing` and `flags2 & MOVE_FACE_ENEMY` on the enemy arm. */
void Think19EnemyTriple();

/** Set1's hint upkeep (`0x102930bc`..`0x10293171`): `m_flOccludedDelay` from the cover or the normal
 *  keyfield, and `ClearHintNode(5.0)` + `SetCondition(0x29)` for an invalid hint or one whose cover
 *  object is my enemy with condition 0x2e or 0x48 standing. */
void Think19HintUpkeep();

/** Set1's shoot-at hint (`0x10293177`..`0x102931c9`): `m_hShootTargetOverride` re-resolved from
 *  `m_pShootAtHint`, or both cleared when slot 566 refuses the hint. */
void Think19ShootAtHint();

/** Set1's tail (`0x102931c9`..`0x1029326c`): under `m_bfNPCFrenziedFlags & 0x8000`, a
 *  `RandomInt(0, 99) < 1` roll plays the `"Scream_Death"` VSound concept (channel 2, 1.0, 1.25). */
void Think19ScreamDeathRoll();

/** The "Set2" block and the AI block (`0x102932b0`..`0x10293643`), run only on a due normal think:
 *  `ResolveStandingOnHead`, the fall-to-ground stub, the under-ground debug check, the `move_yaw`
 *  pose, `DISAPPEAR`, then the AI console gate; accepted: `RunAlternateAI` / slot 432 `RunAI`,
 *  `PostRun`, `PerformMovement`, `CalcNextMoveThink`, `CalcNextAIThink`. Answers FALSE when the gate
 *  refused -- the think then returns WITHOUT the tail (`0x10293447` -> `0x102937c6`). */
bool Think19NormalSet2(double Now, float NormalInterval);

/** The tail every pass that got past the gate reaches (`0x1029364a`..`0x102936e0`): slot 312 and
 *  `FinishTalking` on a due update think, `CalcNextUpdateThink`, `CalcNextNormalThink`,
 *  `m_flNextThink = min(update, normal)`, the `m_bJumping` override, the debug overlay. */
void Think19Tail(double Now, bool bUpdateDue, float UpdateInterval);

/** SEAM for `0x102bfe10`, the "I'm floating" / "got below the ground" check under the ConVar
 *  `debug_track_under_ground` (`0x10924d20`, default `"0"`): a move-probe floor trace whose two arms
 *  are `DevMsg(2, ...)` and the debug-ring dump -- no state. Counted. */
void Think19TrackUnderGround(float NormalInterval);
int32 Think19TrackUnderGroundCalls = 0;

/** `debug_track_under_ground` (`ConVar` object `0x10924d20`, pointer `0x10924d24`, constructed by
 *  `0x1028bcc0` with the shared default literal `DAT_105399a0`, "0"). Not a row of the kernel ConVar
 *  table (a hot header); read as `IsCommand() ? 0 : m_nValue`. */
static int32 Think19DebugTrackUnderGroundConVar();

/** SEAM for `0x1029bd40`, the closest-player distance screen text under the ConVar at `0x10923fec`
 *  (`debug_npc_map`, not a row of the kernel table): a debug overlay, counted. */
void Think19DebugDistanceOverlay();
int32 Think19DebugDistanceOverlayCalls = 0;

/** `DISAPPEAR`'s engine PVS test `0x101d1a90(player, this)`: false for a null player; a headless
 *  world (no embodiment) shares one PVS. */
bool Think19InPlayerPvs(const FElysiumEntity* Player) const;

// --- `CAI_BaseNPCTroika::UpdateCharacter` (`0x10298070`), slot 312 ------------------------------

/** Slot 312's Troika body, taking retail's float interval (`curtime - m_flLastUpdateThink`). The boss
 *  registry (register while `m_bIsBossMonster` and slot 464 answers COMBAT, rebuild without me once
 *  I am no longer a boss) and then `CBaseCombatCharacter::UpdateCharacter(dt)`. Slot 312 has no NPC
 *  species override (`vtmb_slot 312`), so this is not virtual. Replaces `UpdateCharacter(double)`. */
void UpdateCharacterRetail(float IntervalSeconds);

/** SEAM for `CBaseCombatCharacter::UpdateCharacter` (`0x103246d0`): the discipline visuals, slot 313,
 *  `UpdateVampHeal_HOT`, `UpdateExpressions`, the eye direction (slot 333 / the scripted
 *  maintainer), slots 314 / 315 and the `m_nRenderFX` expiries. Each of those runs on its own owner
 *  in this runtime (the gaze in `FElysiumCombatCharacter`'s eye pass, the heal pulse, the discipline
 *  visuals), none of them from here; this counts the call and does nothing. */
void Think19CombatCharacterUpdateCharacter(float IntervalSeconds);
int32 Think19CombatUpdateCharacterCalls = 0;
float Think19LastUpdateCharacterInterval = 0.f;

/** The two-slot global boss table `DAT_109247e0` / `DAT_109247e4` and its signed-BYTE count
 *  `DAT_10924fb8`. Process-wide in retail and here; the one retail reader is
 *  `CBasePlayer::UpdateClientActionState` `0x101755d0` (no port reader yet). */
static FElysiumEntityHandle Think19BossRegistry[2];
static int8 Think19BossRegistryCount;

/** `CWorld::vfunc113` (`0x1023bc20`)'s tail: both slots `-1`, the count 0 -- the world activate. */
static void Think19ResetBossRegistry();
