# StartTask19 — `CAI_BaseNPC::StartTask` (`0x102827f0`), walked

Story 0019/8 pass I, lane L03. Slot 442 on the base line; port body
`FElysiumNpcBase::StartTaskSlot442` (`Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseStartTask19.cpp`),
helpers in `ElysiumNpcBaseStartTask19.inl`. Pass C folds this into `schedule-kernel.md`.

## `0x102827f0` CAI_BaseNPC::StartTask

18330 bytes, 4992 instructions. Callers: `MaintainSchedule` (`0x102817c0`) through slot 442, and
`CAI_BaseNPCTroika::StartTask` (`0x102a1910`) from its own default arm.

### Dispatch

`0x10282806`: `(iTask - 1) > 0x11f` (unsigned) jumps to the default arm `0x10286f63`; otherwise the
byte table `0x10287138[iTask - 1]` selects one of 107 entries of the arm table `0x10286f8c`
(`0x10282821`). The byte table was re-read from the image for this walk: 166 ids of `1..0x120` land on
the default (`0x45`, `0x4f`, and `0x78..0x11f` minus `0x9f`/`0xac`/`0xad`). `EDI` is preloaded with the
source-file string `0x105cde88` (`AI_BaseNPC_Schedule.cpp`) for every failure site.

### Shared services

- **Failure** — every site stores `+0x1b44 = file`, `+0x1b48 = line`, then calls slot 448
  `TaskFail(code)`. Three sites pass a string's address as the code: `"No Sound!"` `0x105ce038`,
  `"No sound in list"` `0x105ce024`, `"gah"` `0x105cdfe4`.
- **Completion** — `TaskComplete(0)` (`0x10273e80`) everywhere; a failure raised earlier on the pass
  blocks it.
- **The goal record** (`AI_NavGoal_t`, 16 dwords): `[0]` type, `[1..3]` dest, `[4]` destNode, `[5]`
  **movement** activity, `[6]` arrival activity, `[7]` arrival sequence, `[8]` tolerance, `[9]`
  flags, `[10]` pTarget, `[11..13]` defaults `0x10934060/64/68`, `[14..15]` zero. Read off
  `SetGoal` `0x102ecd20`: `[5]` goes to `SetMovementActivity` `0x102ee250`, `[6]` to `0x1030b550`,
  `[7]` to `0x1030b5b0`, the default arrival activity is 1. Tolerance `-2.0` (`0x1049a164`) is the
  hull width (`0x102d61b0`: hull row `+0x18 - +0xc`, maxs.y − mins.y); anything but `-1.0`
  (`0x1049a160`) is taken as given; `-1.0` keeps the path's tolerance (`path+0x28`) unless it is 0,
  then the hull width, averaged with the goal entity's hull for entity goals.
- **SetGoal completes and fails tasks itself.** The route builder `0x102f1dc0`: a built route
  clears `m_afMemory` bit `0x20` and, unless slot 529 `IsCurTaskContinuousMove` answers true, calls
  the navigator's slot 2 (`TaskComplete(0)` on the owner); a refused route with route search time
  (`nav+0x40`) `0.0` calls `OnNavFailed(0xc)` (`0x102eeae0` → `TaskFail(0xc)`), otherwise sets bit
  `0x20` and defers. So the arms below that "return without completing" after a `SetGoal` still
  complete through the navigator when the route is built.
- **The weapon range clamp** (arms `0x0a` mode 1, `0x0d`, `0x10`, `0x1a`): unarmed `0.0 .. 2000.0`;
  armed `max = max(+0x8c0, +0x8c4)` (ties to `+0x8c4`, `TEST AH,5 / JP`), `min = min(+0x8b8, +0x8bc)`
  (ties to `+0x8bc`, `AND 0x4100 / JNZ`); then `max = min(max, m_flDistTooFar +0x5de4)`.
- **The motor yaw store** (`0x10288670`, and the tail of `0x102e2020`): flip by 180 when
  `motor+0x28` is set (`+180` below 180, `−180` otherwise), then `motor+0x1c == 180.0f` stores
  straight into `motor+0x34`, else through `0x102e0a80`. Every facing arm opens with `0x102e0b40`
  (`motor+0x2c = −1.0`). `UTIL_AngleMod` is `0x10288590` (`((int)(y·65536/360) & 0xffff)·360/65536`).

### Arms, in arm-table order

| arm | address | task ids | body | exit |
|---|---|---|---|---|
| 0x00 | 0x10282828 | 1 RESET_ACTIVITY | `m_Activity +0xfec = 0` | complete |
| 0x01 | 0x10286505 | 2 WAIT, 4 WAIT_FACE_ENEMY | `m_flWaitFinished +0x5db4 = curtime + data`, no floor | running |
| 0x02 | 0x10286cd9 | 3 ANNOUNCE_ATTACK | — | complete |
| 0x03 | 0x10286f7d | 5, 0x68, 0x74 | the epilogue | running |
| 0x04 | 0x10286c0d | 6 SUGGEST_STATE | `+0x1b3c/40 = file:0xa9b`; `m_IdealNPCState +0x5cc4 = (int)data` | complete |
| 0x05 | 0x10283ed5 | 7 TARGET_PLAYER | `FindEntityByName("!player")`; null → fail 0x17 @0x67a; `SetTarget` | complete |
| 0x06 | 0x10283f36 | 8, 9, 0xa | target null → fail 1 @0x686; `dist < 1.0` → complete; activity 9 / 0x13 / `GetScriptCustomMoveActivity`; no sequence (except 0x18) → complete; target re-tested → fail 1 @0x6a7; goal type 1, [5] = activity, tol −1; in SCRIPT state `+0x5d7c != −1` → [6] = it, else `+0x5d80` set → [7] = `LookupSequence`; `SetGoal(..,4)` false → fail 0xc @0x6be, true → `SetArrivalDirection(target->GetAbsAngles())`; **every exit** runs the tail `0x102841f2`: `+0x5d7c = −1`, `+0x5d80 = 0` | complete (tail) |
| 0x07 | 0x10283dde | 0xb MOVE_TO_TARGET_RANGE | target null → fail 1 @0x669; `dist < 1.0` → complete | running |
| 0x08 | 0x1028611a | 0xc MOVE_AWAY_PATH | angles with yaw `motor+0x34 + 180`; dest = origin + fwd·`ResolveTaskDistance`; goal type 4, [5] 9, tol −1; `SetGoal(..,0)` → complete; else `FindCoverPos(origin, eye, 0, CoverRadius)` miss → fail 8 @0x94b; hit → type 4, [5] 0x13, `SetGoal` discarded; `m_flMoveWaitFinished = curtime + 2.0` | running |
| 0x09 | 0x102847a3 | 0xd SET_GOAL | `(int)data` through table `0x10287258`: 0 enemy (fail 6 @0x75f) type 2, target = enemy; 1 target (fail 1 @0x77f) type 1; 2 enemy LKP (fail 6 @0x76f) type 4; 3 target LKP in the enemy memory (fail 1 @0x78f) type 4; 4 save position type 4; `> 4` → complete; the five write `+0x5df4..+0x5e08`, then `SetMovementActivity(0x13)` | complete |
| 0x0a | 0x10284ae8 | 0xe GET_PATH_TO_GOAL | goal = stored type, target `+0x5df4`, tol −2; mode 0 stored point; 1 clamp + `FindLosPos(stored, aim)` (fail 0xb @0x7de); 2 lateral cover → move-wait + complete, else `FindCoverPos` hit → `SetGoal` (dest **not** copied) + move-wait, miss → fail 8 @0x807 **then** fail 0xc @0x816; other → fail 0xc @0x816; tail: `SetGoal` true → complete, false → fail 0xc @0x816 | per mode |
| 0x0b | 0x1028509b | 0xf GET_PATH_TO_ENEMY | `IsUnreachable(GetEnemy())` first → fail 0xc @0x83a; null → fail 6 @0x842; goal type 2 tol −1; false → `DevWarning "GetPathToEnemy failed!!"`, `RememberUnreachable`, fail 0xc @0x84f | complete |
| 0x0c | 0x10284349 | 0x10 ENEMY_LKP | `IsUnreachable` → fail 0xc @0x712; type 4 at LKP tol −1; `TranslateEnemyChasePosition(enemy, &dest, &tol, &scalar)` with `path+0x20`; `SetGoal(..,2)` true → `path+0x20 = scalar`, complete; false → DevWarning, unreachable mark, fail 0xc @0x724 | complete |
| 0x0d | 0x102844d1 | 0x11 ENEMY_LKP_LOS | null → fail 6 @0x72d; clamp; aim = LKP + enemy view offset; `FindLosPos(lkp, aim)` miss → fail 0xb @0x74e; type 4, [5] 0x13, tol −2, `SetGoal(..,2)`; `SetArrivalDirection(lkp − dest)` | running |
| 0x0e | 0x1028523a | 0x12 ENEMY_CORPSE | dest = LKP − fwd·64; type 4 tol −1; `SetGoal(..,2)` discarded | running |
| 0x0f | 0x10285387 | 0x13 GET_PATH_TO_PLAYER | `!player` unchecked; type 4 at `WorldSpaceCenter`, pTarget = player; `SetGoal` discarded | running |
| 0x10 | 0x1028545a | 0x14 ENEMY_LOS | null → fail 6 @0x86f; clamp; `FindLosPos(enemy origin, enemy eye)` miss → fail 0xb @0x891; type 4, [5] 0x13, tol −2; hint → `SetArrivalActivity(GetCoverActivity)`; `SetArrivalDirection(enemy origin − dest)` | running |
| 0x11 | 0x10285949 | 0x15 GET_PATH_TO_TARGET | null → fail 1 @0x8bf; type 4 at the target, pTarget = target, tol −1 | running |
| 0x12 | 0x10285a9e | 0x16 GET_PATH_TO_HINTNODE | no hint → fail 4 @0x8cf; type 4 at `0x102d1180(hint)`, [5] 0x13 | running |
| 0x13 | 0x10282afd | 0x17 | `+0x5db8 = GetOrigin`, `+0x5dc4 = GetAngles` | complete |
| 0x14 | 0x10282b5b | 0x18 | both from the zero vectors `0x1070d1b0` / `0x1070d9d0` | complete |
| 0x15 | 0x10282bb7 | 0x19 | `m_vSavePosition +0x5dd0 = GetOrigin` | complete |
| 0x16 | 0x10282bf1 | 0x1a | `GetBestSound` null → fail "No Sound!" @0x492; savepos = sound+0x20, plus twice the owner's slot 199 `GetVelocity` | complete |
| 0x17 | 0x10282cc7 | 0x1b | null enemy → fail 6 @0x4a7; savepos = enemy origin | complete |
| 0x18 | 0x10285bb0 | 0x1c | type 4 at `m_vecLastPosition`; false → fail 0xc @0x8df; true → `SetArrivalDirection(m_qaLastFacing)` | running |
| 0x19 | 0x10285cb9 | 0x1d | type 4 at save position; discarded | running |
| 0x1a | 0x1028570b | 0x1e | clamp; aim = savepos + OWN view offset; `FindLosPos` miss → fail 0xb @0x8b6; type 4, [5] 0x13, tol −2 | running |
| 0x1b | 0x10285d7f | 0x1f | `SetRandomGoal(ResolveTaskDistance, BodyDirection2D)` false → fail 0x18 @0x8f7 | complete |
| 0x1c / 0x1d | 0x10285df8 / 0x10285ef8 | 0x20 / 0x21 | best sound (fail 0x12 @0x905) / best scent (fail 0x13 @0x915); type 4 at sound+0x20 tol −1 | running |
| 0x1e | 0x102863f1 | 0x22 RUN_PATH | 0x13 if the model has it else 9; `m_afMemory &= ~2` | complete |
| 0x1f | 0x10286438 | 0x23 WALK_PATH | 0x22 on FLY/FLYGRAVITY when present, else 9, else 0x13; `&= ~2` | complete |
| 0x20..0x23 | 0x102864f1 / af / d0 / 0x10286523 | 0x24..0x27 | `m_bShouldMove = 1`; 9 / 9 / 0x13 / 0x13; the TIMED pair also stamp `m_flWaitFinished` | running |
| 0x24 | 0x10286556 | 0x28 STRAFE_PATH | `m_bShouldMove = 1`; 2-D right · (waypoint − origin), both normalised; `<= 0` → 0x37 else 0x38 | complete |
| 0x25 | 0x10284218 | 0x29 | `m_flMoveWaitFinished = curtime` | complete |
| 0x26 | 0x102867e5 | 0x2a SMALL_FLINCH | `SetIdealActivity(GetFlinchActivity 0x10265970)` | running |
| 0x27 | 0x10283cd5 | 0x2b FACE_IDEAL | hold; `SetTurnActivity` | running |
| 0x28 | 0x10283cf7 | 0x2c FACE_PATH | no goal → `DevWarning "No route to face!"`, fail 0xc @0x63e; hold; ideal yaw to the waypoint; `|DeltaIdealYaw| > 15.0` (double `0x1049a170`) → turn, else complete | either |
| 0x29 | 0x10286537 | 0x2d FACE_PLAYER | `m_flWaitFinished = curtime + data` | running |
| 0x2a | 0x10283c66 | 0x2e FACE_ENEMY | `FInAimCone(LKP)` → complete; else hold, yaw to LKP, turn | either |
| 0x2b | 0x10283a36 | 0x2f FACE_HINTNODE | hold; yaw `0x102d12e0(hint)` into the store; turn | running |
| 0x2c | 0x10282dfb | 0x30 | `SetIdealActivity(GetHintActivity(hint+0x5dc))` | running |
| 0x2d | 0x10283b9e | 0x31 FACE_TARGET | null → fail **1** @0x61c; hold; yaw to target; turn | running |
| 0x2e | 0x10283aad | 0x32 | hold; yaw to `m_vecLastPosition`; turn | running |
| 0x2f | 0x10283ae3 | 0x33 | hold; `AngleMod(GetAngles().y)` into the store | complete |
| 0x30..0x36 | 0x10284286 … 0x102842fb | 0x34..0x3f | `m_flLastAttackTime = curtime` (not RELOAD / SPECIAL); `RestartIdealActivity` 0x19, 0x1b, 0x4b, 0x4e, 0x54, 0x5e, 0x5f | running |
| 0x37 | 0x10282a17 | 0x40, 0x41 | hint held → complete; else `0x102d1af0(this, 0, type, 2000)`, null → fail 4 @0x45a; 0x40 returns, 0x41 falls into 0x39 | — |
| 0x38 | 0x10282d44 | 0x42 | `0x102d1420(hint, 0.0)`; hint = 0 | complete |
| 0x39 | 0x10282a79 | 0x43 | no hint → fail 4 @0x465; `0x102d1350` false → fail 0x11 @0x46d **and** hint = 0 | complete |
| 0x3a..0x3e | 0x102868a3 … | 0x44, 0x46..0x49 | `DevMsg "SOUND"` / slots 490, 489, 491, 488 | complete |
| 0x3f | 0x102868c9 | 0x4a | slot 508 `SpeakSentence((int)data)` | complete |
| 0x40 | 0x10284311 | 0x4b SET_ACTIVITY | nonzero → `SetIdealActivity`, zero → `m_Activity = 0` | running |
| 0x41 | 0x10282e27 | 0x4c SET_SCHEDULE | `0x102cc1f0` (slot 440 then 446, miss DevMsgs and takes schedule 1); null → fail 5 @0x507; `+0x1b2c = 1`, `+0x1b34 = 0x4fc`; `m_IdealSchedule = (int)data` untranslated; `SetSchedule 0x10280e50` | running |
| 0x42 / 0x45 | 0x10286c45 / 0x10286d58 | 0x4d / 0x51 | `m_failSchedule = (int)data` / `= 0` | complete |
| 0x43 | 0x10286c69 | 0x4e | `0x102ee1c0`: path `+0x28` = `ResolveTaskDistance(data)` | complete |
| 0x44 | 0x10286d1a | 0x50 | `nav+0x40 = (float)(int)data` | complete |
| 0x46 | 0x10282dde | 0x52..0x56 | `SetIdealActivity((int)data)` | running |
| 0x47 | 0x102838e7 | 0x57 | no sound → fail "No sound in list" @0x5eb; `FindCoverPos(sound, sound, (float)m_iVolume, CoverRadius)` miss → fail 8 @0x5fb; type 4, [5] 0x13, tol −2; move-wait | running |
| 0x48 | 0x10283558 | 0x58 | threat = enemy or self; lateral cover → move-wait + complete; `FindCoverPos(threat, threat eye, 0, CoverRadius)` miss → fail 8 @0x5a6; type 6, [5] 0x13, tol −2; hint arrival; move-wait | running |
| 0x49 | 0x102836fa | 0x59 | threat = `m_hEnemy` raw or self; lateral cover only; false → fail 8 @0x5c2; move-wait | complete |
| 0x4a | 0x10282eab | 0x5a | no enemy → fail 6 @0x510; `0x102edae0(&savepos, 0, 30000)` false → fail 7 @0x519; type 4 [5] 0x13 tol −1; false → fail 0xc @0x524 | complete |
| 0x4b..0x4d | 0x102833ac / 0x10283035 / 0x102831ea | 0x5b / 0x5c / 0x5d | fail 6 @0x566/0x52d/0x549; radii (0, CoverRadius) / (0, ResolveTaskDistance) / (ResolveTaskDistance, CoverRadius); fail 8 @0x57a/0x541/0x55e; type 6 [5] 0x13, tol −1/−1/−2; hint arrival | running |
| 0x4e | 0x102837c9 | 0x5e | `FindCoverPos(origin, eye, 0, CoverRadius)` miss → fail 8 @0x5d5; type 4 [5] 0x13 tol −2; move-wait | running |
| 0x4f | 0x10286801 | 0x5f, 0xdf | `ClearGoal`; `m_lifeState = 1` | running |
| 0x50 | 0x102868f2 | 0x60 | cine pre-idle set → slot 584 `StartSequence`, `strcmp(play, idle) == 0` → `m_flPlaybackRate = 0`; else `m_scriptState != 6` → `SetIdealActivity(1)` | running |
| 0x51 | 0x102869e0 | 0x61 | pick `m_iszPlay`, else `m_iszPostIdle`; `+0x5d7c = −1`, `+0x5d80 = 0`; `+0x5d7c = ActivityList_IndexForName`, −1 parks the name in `+0x5d80` | complete |
| 0x52 / 0x53 | 0x10286adb / 0x10286b0a | 0x62 / 0x63 | `HasMovement(GetSequence())` discarded, empty `0x1027f270`; `m_scriptState = 0` / `2` | running |
| 0x54 | 0x10286b2a | 0x64 | `DelayStart(cine, 0)` | complete |
| 0x55 | 0x10286b54 | 0x65 | target → slot 62 `SetOrigin(target->GetAbsOrigin())` | complete |
| 0x56 | 0x10286b96 | 0x66 | target → hold, `AngleMod(target->GetAngles().y)` into the store; `m_scriptState != 6` → turn; `ClearGoal` | running |
| 0x57 | 0x10283dae | 0x67 | `m_flWaitFinished = curtime + RandomFloat(0.1, data)` | running |
| 0x58 | 0x10282d71 | 0x69 STOP_MOVING | no goal → `m_bShouldMove = 0`, complete; else `ClearGoal`, `move_yaw` present → `SetPoseParameter(move_yaw, 0)` | either |
| 0x59 / 0x5a | 0x10282926 / 0x10282848 | 0x6a / 0x6b | hold; `AngleMod(AngleMod(yaw) ± data)` into the store; turn | running |
| 0x5b / 0x5c | 0x102829bd / 0x102829e9 | 0x6c / 0x6d | `m_afMemory |= / &= ~(int)data` | complete |
| 0x5d | 0x10286749 | 0x6e | `path+0x10` cleared; no waypoint → `m_bShouldMove = 0`, complete, `ClearGoal`; goal active → `m_bShouldMove = 1`, slot 528; else `m_bShouldMove = 0`, `SetIdealActivity(GetStoppedActivity)` | either |
| 0x5e | 0x102866bf | 0x6f | refresh; no goal or slot 251 finished → `m_bShouldMove = 0`, complete; else `= 1`, slot 528 | either |
| 0x5f | 0x10286d78 | 0x70 | `Weapon_FindUsable(1000³)`; `SetTarget`; null → fail 3 @0xaca | complete |
| 0x60 / 0x61 | 0x10286df5 / 0x102864d7 | 0x71 / 0x72 | `SetIdealActivity(0x5c)` / `SetMovementActivity(0x13)` | running |
| 0x62 | 0x10286e0b | 0x73 | `SetHullSizeSmall(0)` | complete |
| 0x63 | 0x10284e4a | 0x75 | ray origin+fwd·256 → −500 z, mask `0x46004003`; type 4 at endpos tol −1; `SetGoal(..,2)` true → complete ×2; false → fail "gah" @0x82f, then complete | complete |
| 0x64 | 0x10286ec2 | 0x76 WANDER | `SetWanderGoal(n/10000, n%10000)` false → fail 0x18 @0xb05 | complete |
| 0x65 | 0x102869a6 | 0x77 FREEZE | `m_flPlaybackRate = 0` | running |
| 0x66 | 0x10286c9f | 0x9f | no weapon → fail 3 @0xab3; `0x102ee1c0`: path `+0x28` = `(float)(int)(weapon+0x8c0 · data)` | complete |
| 0x67 / 0x68 | 0x10286e2a / 0x10286e76 | 0xac / 0xad | `ChooseBest{Melee,Ranged}Weapon` false → fail 0x1f @0xae6/0xaf1 | complete |
| 0x69 | 0x10285ff8 | 0x120 PATHCORNER | `m_target` empty → fail **0x13** @0x921; type 3 at `m_pGoalEnt->GetOrigin` (unchecked), [5] = fly ? 0x22 : 9, tol −1, flags 1; false → `DevWarning "Can't Create Route!"` | running |
| 0x6a | 0x10286f63 | the rest | `DevMsg "No StartTask entry for %s"` (slot 449) | running |

### Reads and writes (base words)

Reads `m_hTargetEnt +0x5ce4`, `m_hEnemy +0x5ce0` (arm 0x49 only; elsewhere slot 167), `m_hCine +0x5d74`,
`m_scriptState +0x5d70`, `+0x5d7c`/`+0x5d80`, `m_pHintNode +0x5ddc`, `m_vecLastPosition`,
`m_qaLastFacing`, `m_vSavePosition`, `+0x5df4..+0x5e04`, `m_flDistTooFar +0x5de4`, `m_target +0x20c`,
`m_pGoalEnt +0x5de8`, `m_LastHitGroup +0x1594`, `motor+0x28/+0x34/+0x1c`. Writes the fields listed in the
table plus `+0x1b2c..+0x1b48` (the trace words, ABSENT in the port).

### Retail defects reproduced

- `TASK_GET_PATH_TO_GOAL` mode 2 hands `SetGoal` a record whose dest was never filled (the cover
  point stays on the stack at `+0x7c`).
- `TASK_GET_PATH_TO_GOAL` mode 2's cover miss fails twice (8 then 0xc); the second reason stands.
- `TASK_GET_PATH_TO_PATHCORNER` fails an empty `m_target` with `FAIL_NO_SCENT`.
- `TASK_GET_DROPSHIP_DEPLOY_PATH` completes after failing (blocked) and twice on success.
- `TASK_LOCK_HINTNODE`'s claim failure also drops the hint.

### Port

Named divergences: `FindCoverPos` honours the maximum radius only (`IElysiumNpcMotor::FindNodeCover`);
the arrival activity / direction and the route-search deferral are recorded and not executed (the
mover has neither); `m_flPlaybackRate` / `path+0x2c` live on the Troika record
(`FElysiumNpc::SequencePlaybackRate`, `ScheduleHost.NavigationActivity`), so a base-only NPC has
neither; `path+0x28` is the base's `NavPathToleranceCm` (arms `0x43` / `0x66` write it through
`0x102ee1c0`, not `m_flGoalTolerance`); `SetGoal` claims the Troika's schedule body before commanding the mover.
Crash guards replace the unguarded reads of arms 0x0f, 0x2b, 0x2c, 0x50, 0x51, 0x54, 0x69 and the
null `Task_t`.

**Unrecovered:** the BSS default dest / pTarget words (`0x1093404c..54`, `0x10923a30`); the fourth
goal word's meaning beyond "arrival sequence"; `0x1030b550`/`0x1030b5b0`/`0x102f13d0` (arrival
activity / sequence setters, the navigator's post-route call); the deferred-route retry
(`0x102f1dc0`'s bit-`0x20` arm, `nav+0x44/+0x48/+0x4c`); `path+0x10`'s meaning (cleared by
`0x102ee2c0`); `path+0x20`'s meaning; the `+0x1c`-not-180 arm `0x102e0a80`; `CAI_Enemies`'s slot
`+0xe8` call on a missed `GetLastKnownPosition`; the retail activity numbering of an `Activity:`
operand (the port interns names); `motor+0x28`'s retail name.
