# `CAI_BaseNPCTroika::StartTask` `0x102a1910` — the dispatch and the arms in `[0x102a1943, 0x102a5046)`

Spec 0019 story 8 pass I, lane L01. Slot 442 on the Troika line (`FElysiumNpc::StartTaskSlot442`,
`Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask19.cpp`). The arms from `0x102a5046` on are
lane L02's (`StartTaskTroikaTail`, `ElysiumNpcStartTask19_2.cpp`). Pass C folds this section into
`schedule-kernel.md`. Every statement cites the listing; the per-arm long form is
`walk-19-29-pack-13.md` in the research tree, corrected where listed at the end.

## The contract

`param_1` is the compiled `Task_t` (`[0]` task id, `[1]` the operand word). Every arm leaves by one of:
`TaskComplete(false)` (`0x10273e80`, the shared `break` tail `0x102a66d7`, or `0x102a4e51` inside
this range); `TaskFail(reason)` (slot 448, `vtable +0x700`), preceded at every site but three by
`+0x1b44 = "E:\Vampire\main\dlls\AI_BaseNPCTroika.cpp"` (`0x105da024`) and `+0x1b48 = line`; a plain
return to `0x102a77ea` that leaves the task RUNNING for `RunTask` (`0x102aacf0`); or the base forward.
The three fail sites with no line pair are `0x102a1eef` (`TaskFail(0x1a)`), `0x102a6ba5`
(`TaskFail(0xe)`) and the dialog pair `0x102a4dcc` (`0x17`) / `0x102a4e67` (`0x15`).

## The dispatch — `0x102a1910`

`EDI = task` (`0x102a191a`); `ECX = [EDI] - 5` (`0x102a1925`); `CMP ECX,0x144; JA 0x102a77e2`
(`0x102a1928`): an id past `0x149` goes to `CAI_BaseNPC::StartTask 0x102827f0` with the task
pointer. Otherwise `MOV DL,[ECX+0x102a7ab8]` (the 325-byte index table) and
`JMP [EDX*4+0x102a77f8]` (176 arm addresses, ascending id). Ghidra prints the index as the case
label.

## The shared tails this range uses

| tail | body |
|---|---|
| `0x102a66d7` | `TaskComplete(false)`, return |
| `0x102a4e51` | the same, reached from `0x102a33cf`, `0x102a4928`, `0x102a4e51` |
| `0x102a44d1` | motor stop-turn `0x102e0b40`; `0x102e2020(motor, &point, 0)`; slot 572 `SetTurnActivity`; return RUNNING |
| `0x102a76a4` | `SetGoal(goal, 0)` (answer dropped), then `0x102a76a9` `m_flMoveWaitFinished (+0x5cf0) = curtime + data`; return RUNNING |
| `0x102a4186` | goal literal type 4, activity `0x13`, tolerance `-2.0` (`0x1049a1b0`), then `SetGoal(goal, flags)` with the caller's pushed flags; answer dropped; RUNNING. `0x102a4178` stores the two literal words `+0x24`/`+0x3c` first |
| `0x102a42c0` / `0x102a42c7` / `0x102a42df` | write `m_flGoalTolerance (+0x6320)`, `0x102ee1c0(nav, tol)` (the path's goal tolerance), `0x102f2fe0(nav, +0x6320)` (the arrival distance), `TaskComplete`. `0x102a42c7` is entered with a HALVED value for `0x102ee1c0` only |
| `0x102a46d0` | `+0x1b44` file, `TaskFail(0x1f)`, then `CAI_BaseNPC::AutoMovement 0x10280a50` |
| `0x102a6ba5` | `TaskFail(0xe)` |

The goal literal (`AI_NavGoal_t`, 0x40 bytes) is read by `SetGoal 0x102ecd20` as: `+0x00` type
(1 `m_hTargetEnt`, 2 the enemy, 7 slot 586 `GetBestSeeUnknown`, anything else the destination),
`+0x04` destination, `+0x10` destNode, `+0x14` movement activity (`-1` keeps it, else
`0x102ee250`), `+0x18`/`+0x1c` arrival activity/sequence, `+0x20` tolerance (`-2` hull width
`0x102d61b0(+0x156c)`; `-1` keeps the path's `+0x28`, or the hull width when that is zero), `+0x24`
goal flags (bit 1 routes node to node through `0x102f3c10`/`0x102f41b0`/`0x102fd240`), `+0x28` the
target entity (`DAT_10923dd8` in every literal here but `0x102a4d8f`), `+0x2c..+0x34` the arrival
direction (`DAT_10934060..68`). The second `SetGoal` argument's bit 2 calls `0x102f28a0` on a refused
route.

## The arms, ascending task id

**`0x06` TASK_SUGGEST_STATE — `0x102a1b2b`.** Stamps the selector pair `+0x1b3c/+0x1b40 = 0x2dff`;
`m_IdealNPCState (+0x5cc4) = (int)data` (`0x102a1b43`/`0x102a1b4e`). `m_bfNPCFrenziedFlags (+0x5b84)
& 1` (`0x102a1b54`): ideal 1 -> line `0x2e05`, ideal `0xb` (`0x102a1ba6`); ideal 3 -> line `0x2e06`,
ideal `0xb` (`0x102a1b76`); any other -> complete. Not frenzied: `m_bNoAlertState (+0x65f6)` and
ideal 3 -> line `0x2e0e`, ideal 1 (`0x102a1bed`). Every arm completes.

**`0x0f` TASK_GET_PATH_TO_ENEMY — `0x102a33f9`.** Slot 530 `IsUnreachable(GetEnemy())` -> line
`0x3101`, `TaskFail(0xc)`. No enemy -> line `0x3109`, `TaskFail(6)`. Goal type 2, destination the
`DAT_1093404c` sentinel, tolerance -1, `SetGoal(goal, 0)`: true completes; false ->
`DevWarning(2, "GetPathToEnemy failed!!\n")`, `RememberUnreachable(GetEnemy())` (`0x10274080`), line
`0x311a`, `TaskFail(0xc)`.

**`0x16` TASK_GET_PATH_TO_HINTNODE — `0x102a371d`.** `m_pHintNode (+0x5ddc) == 0` -> line `0x314a`,
`TaskFail(4)`. `0x102b6120(this, &pos, 0)` (the lean offset), goal type 4, activity `0x13`,
tolerance -1, `SetGoal(goal, 0)`, RUNNING, answer dropped.

**`0x2e` TASK_FACE_ENEMY — `0x102a4417`.** `m_hShootTargetOverride (+0x5ba8)` resolved -> its
`GetAbsOrigin`; else the enemy's LKP (`GetEnemies()->0x102dfed0`). Slot 364 `FInAimCone(point)`:
true completes (`0x102a44cb`), false is the turn tail at that point.

**`0x2f` TASK_FACE_HINTNODE — `0x102a382a`.** Motor stop-turn. `m_pHintNode` is dereferenced
UNGUARDED (`0x102a3841 CMP [hint+0x5dc],0x27d8`). Type `0x27d8`: yaw = `0x102d12e0(hint)` `+45.0`
(`0x1049949c`) when `m_bLeaningLeft (+0x63fd)`, `-45.0` otherwise; other types: the hint yaw as is.
`motor+0x28` set flips it: `< 180 ? +180 : -180` (`0x1044c3a8`). `motor+0x1c == 180.0f` stores it
straight to `motor+0x34` (`0x102a38a9`), else `motor+0x34 = 0x102e0a80(motor, yaw)` (`0x102a399f`).
Slot 572, RUNNING.

**`0x34` TASK_RANGE_ATTACK1 — `0x102a4505`.** A weapon AND `m_bfAINPCFlags2 (+0x14bc)` bit 15
(`DISABLE_BURST_FIRE`) clear (`NOT; TEST AH,AH; JNS`) -> `m_iBurstFireCount (+0x6490) =
RandomInt(data+0x3a4, data+0x3a8)` over the weapon's data (`0x10003d91`); otherwise 1. RUNNING.

**`0x35` TASK_RANGE_ATTACK2 — `0x102a4576`.** `m_flLastAttackTime (+0x5d9c) = curtime`,
`RestartIdealActivity(0x1b)`, RUNNING.

**`0x36`/`0x37` TASK_MELEE_ATTACK1/2 — `0x102a45c6`.** Motor stop-turn. A weapon whose slot `+0x5a0`
carries `0x18000` -> `m_flLastAttackTime = curtime`, weapon slot `+0x51c` for `0x37` else `+0x518`,
`AutoMovement`, RUNNING. Otherwise line `0x3302` -> `0x102a46d0`.

**`0x3e`/`0x3f` TASK_SPECIAL_ATTACK1/2 — `0x102a459a` / `0x102a45b0`.** `RestartIdealActivity(0x5e)` /
`(0x5f)`, RUNNING.

**`0x4b` TASK_SET_ACTIVITY — `0x102a1c0f`.** `act = (int)data`; 0 writes `m_Activity (+0xfec) = 0`,
else `SetIdealActivity(act)` (`0x10272650`). `m_flWaitFinished (+0x5db4) = curtime + 1.0`. Slot 464
`GetState() == 4` -> `AdvanceToIdealActivity 0x102726a0`. No exit: RUNNING either way.

**`0x4e` TASK_SET_TOLERANCE_DISTANCE — `0x102a4289`.** `m_flGoalTolerance = 0x102d61b0(m_eHull
+0x1568) * 0.5` (`0x10449270`, double), `+= ResolveTaskDistance(data)`, tail `0x102a42c0`.
`0x102d61b0(hull)` is `hullTable[hull]+0x18 - +0xc`, the hull's X width.

**`0x4f` TASK_SET_TOLERANCE_DISTANCE_ABS — `0x102a42fa`.** `m_flGoalTolerance =
ResolveTaskDistance(data)`, the same two writes, complete.

**`0x5a` TASK_FIND_BACKAWAY_FROM_SAVEPOSITION — `0x102a1c6a`.** `d = ResolveTaskDistance(data)`;
`0x102edae0(nav, &m_vSavePosition (+0x5dd0), d, 64.0, &out)`; false -> line `0x2e3f`,
`TaskFail(7)`. Goal type 4, activity `0x13`, tolerance -1, `SetGoal(goal, 0)`: true completes, false
-> line `0x2e4a`, `TaskFail(0xc)`.

**`0x6e` TASK_WAIT_FOR_MOVEMENT — `0x102a1dcc`.** (1) `0x102ee2e0(nav)` reads `CAI_Path+0x10`, a byte;
set -> `0x102bf7e0`, which clears that byte (`0x102ee2c0` -> `0x1030bea0 MOV byte [path+0x10],0`)
and sets `m_bShouldMove`. (2) `0x102ee620(nav)` = `path+0x5c`, the goal TYPE `SetGoal` stores through
`0x1030ba50` (with `path+0x58 = 1`): zero -> `m_bShouldMove = 0`, complete, `0x102ee270` (clear the
goal). (3) else `0x102ee6a0` (path and its `+0x24` current waypoint both live) false ->
`m_bShouldMove = 0`, `SetIdealActivity(0x1027a6c0())`, complete. (4) else `0x102f2ea0` (planar
distance² to the goal, stored at `nav+0x14`, under the path tolerance and the height gap under slot
522, then the navigator's `+0x20`) true -> `m_bShouldMove = 0`, complete; false -> `m_bShouldMove =
1`, slot 528 `ValidateNavGoal`. Then, after every arm: `FCOMP curtime` against
`m_flTeleportMoveTimer (+0x65dc)`, `TEST AH,0x41; JP` — returns when `curtime > timer`, so the
rescue runs INSIDE the window: `0x102e7880(moveProbe, 0x102ee140(nav), 0x202400b, 1.0, -1024.0,
&out, &hit)` false -> `TaskFail(0x1a)`; no hit entity, or a hit whose `+0x94` (`m_pBaseNPC`) is set
-> `TaskFail(0xe)`; otherwise slot 216 `SetAbsOrigin(out)`.

**`0x79` TASK_GET_PATH_TO_BESTUNKNOWN — `0x102a3599`.** Slot 586 handle dead -> line `0x3127`,
`TaskFail(0x21)`. Goal type 7, tolerance -1, `SetGoal(goal, 0)`: true completes; false ->
`DevWarning(2, "GetPathToBestUnknown failed!!\n")`, `0x10274080(this, u)`, line `0x3134`,
`TaskFail(0xc)`.

**`0x7a`/`0x7b` TASK_GET_PATH_TO_PATROL_POINT(_HUNT) — `0x102a39be` / `0x102a39d9`.**
`0x102aa640(this, &m_sppPatrolPath)` with the smart pointer's address, `+0x658c` / `+0x6594`; the
helper owns the exit.

**`0x7c` TASK_GET_FULL_PATROL_PATH — `0x102a39f4`.** `p = [+0x6590]` (the pointee); null -> line
`0x3186`, `TaskFail(0x1d)`. `node = p[p[+0x10]*4 + 0x14]`; -1 -> RUNNING. The navigator's node array
(`nav+0x2c`, count `[0]`, entries `[1]`) bounds-checks it; out of range increments
`DAT_106c994c` and yields null — and `0x102fb0d0 CAI_Node::GetPosition(m_eHull)` is then called on
null (a crash). Goal type 4, destination the node position, destNode -1, activity -1, tolerance -1,
flags 0, `SetGoal(goal, 2)`: true completes; false -> `DevWarning(2, "%s can't reach patrol
point\n", GetDebugName())`, line `0x31a0`, `TaskFail(0xc)`.

**`0x7d`/`0x7e` TASK_NEXT_PATROL_POINT(_HUNT) — `0x102a3b91` / `0x102a3bac`.**
`0x102aa9e0(this, &m_sppPatrolPath(+0x658c) / Hunt(+0x6594))`.

**`0x7f`/`0x80` TASK_GET_DIRECTED_PATH_TO_ENEMY_LKP(_RND) — `0x102a3d48`.** No enemy -> line
`0x3205`, `TaskFail(6)`. `d` = the RAW operand; `0x80` only: `d = RandomFloat(d*0.5, d)`.
`GetEnemies()->0x102e0290(enemy, &lkp, &seen)` false -> line `0x323a`, `TaskFail(6)`.
`bOk = 0x102ee300(nav, this, &lkp, &seen, d, &point)`. The literal (type 4, activity -1, tolerance
-1), then slot 563 `TranslateEnemyChasePosition(enemy, &goal.dest, &goal.tolerance, &tol)` with `tol`
a copy of `m_flGoalTolerance` — unconditionally. `bOk && SetGoal(goal, 2)` -> `0x102ee1c0(tol)`,
`0x102f2fe0(tol)`, complete; otherwise `DevWarning(2, "GetDirectedPathToEnemyLKP failed!!\n")`,
`0x10274080(this, GetEnemy())`, line `0x3235`, `TaskFail(0xc)`.

**`0x81` TASK_GET_DIRECTED_PATH_TO_ENEMY_LKP_LOS — `0x102a3f84`.** No enemy -> `0x3245`,
`TaskFail(6)`; no LKP -> `0x3276`, `TaskFail(6)`; `0x102ee300(nav, this, &lkp, &seen, data, &dir)`
false -> `0x3271`, `TaskFail(0xb)`. Range: max 2000.0 (`0x44fa0000`), min 0; armed: max =
`max(w+0x8c0, w+0x8c4)`, min = `min(w+0x8b8, w+0x8bc)` (the SMALLER, `0x102a40a3 AND 0x4100; JNZ`);
max clamped down to `m_flDistTooFar (+0x5de4)`. `0x102edaa0(nav, &dir, &dir+enemy view offset
(+0x184), min, max, 1.0, 0, &out)`: false -> RUNNING with no fail; true -> `0x102a4186` flags 2.

**`0x83`/`0x84` TASK_GET_PATH_TO_FLEE_NODE / TASK_GET_PATH_TO_COWER_NODE — `0x102a2882`.** Target =
enemy or this. `d = ResolveTaskDistance(data)`, `max = d + 8192.0` (`0x1049ae70`), `mid = (max +
d) * 0.5`. A held hint -> `ClearHintNode(1.0)`. `m_pHintNode = 0x102d1af0(this, 0x2774, 2, max, 0,
0)`; found: `0x102d1350(hint, this)` refused -> `m_pHintNode = 0`; claimed -> `0x102d1180(hint,
this, &pos)`, goal type 4, activity `0x13`, tolerance -1, `SetGoal(goal, 0)`: true -> slot 16's
extents into `m_vecSavedSleepExtents (+0x65d0)`, `SetAbsoluteAttackExtents((40,40,80))`
(`0x1009b060`: `SetAttackExtents(abs - (maxs-mins)*0.5)`), complete; false -> `ClearHintNode(5.0)`.
Still holding a hint -> return (`0x102a2a68`). `0x84` only: `m_bfAINPCFlags |= 0x200`
(`COWER_PATH`). `0x102edc80(nav, target->GetOrigin(), target->EyePosition(), mid, max, &out,
target)`, retried with `(d, max)`; both false -> line `0x2fe8`, `TaskFail(0x18)`. Success: goal type
6, activity `0x13`, tolerance -2, `SetGoal(goal, 0)`, RUNNING.

**`0x85` TASK_GET_PATH_TO_COWER_NODE_SAVE_POS — `0x102a2bd8`.** `COWER_PATH` set unconditionally
(`0x102a2bfc`). From = `m_vSavePosition`, to = it + this body's view offset (`+0x184..+0x18c`).
`0x102edc80(from, to, (max+d)*0.5, max, &out, this)` then `(from, to, d, max, &out, this)`; both
false -> line `0x3023`, `TaskFail(0x18)`. Goal type 6, activity `0x13`, tolerance -2, flags 0,
RUNNING.

**`0x86` TASK_FIND_FOLLOWER_BACKAWAY_SIMPLE — `0x102a2d9b`.** `m_hFollowerBoss (+0x647c)` dead ->
line `0x3051`, `TaskFail(0x29)`. `yaw = UTIL_VecToYaw(self - boss)` (`0x1000612c`) +
`RandomFloat(-45, 45)`; candidate = `SELF + m_flFollowerDistanceBackAway (+0x6484) *
UTIL_YawToVector(yaw)` (`0x1000ecd2`; the sum's base is `[ESP+0x28]`, this body's origin). A 14-dword
trace is zeroed, `0x102e6d70(moveProbe, 0, self, candidate, 0x202400b, 0, 100.0, 0, &trace, 0, 0)`:
false -> line `0x304c`, `TaskFail(7)`; true -> `0x102a4178` with flags 0.

**`0x87` TASK_FIND_FOLLOWER_BACKAWAY_NODE — `0x102a2f94`.** Boss dead -> `0x306f`, `TaskFail(0x29)`.
`0x102edae0(nav, bossOrigin, m_flFollowerDistanceWalkTo (+0x6488) - 10.0, 50000.0, &out)`: false ->
`0x306a`, `TaskFail(7)`; true -> `0x102a4186` flags 0.

**`0x88` TASK_FIND_FOLLOWER_BACKAWAY_ASTAR — `0x102a3096`.** The same through `0x102edbb0`; lines
`0x308c` / `0x3087`.

**`0x9a` TASK_MELEE_KICK — `0x102a464d`.** Motor stop-turn; `cast = __RTDynamicCast(weapon, 0,
0x1055f710, 0x105da324, 0)`. Weapon AND `slot(+0x5a0) >> 30 & 1` AND cast -> `m_flLastAttackTime`,
`cast->+0x5d0(0x53, 0, 0)`, `AutoMovement`, RUNNING; otherwise line `0x331d` -> `0x102a46d0`.

**`0x9f` TASK_SET_MELEE_TOLERANCE_DISTANCE — `0x102a434a`.** `hull = enemy ? enemy+0x9c ->
+0x1568 : 0`. Armed: `m_flGoalTolerance = 0x102d61b0(hull) * 0.5 + weapon+0x8c0 * data` (RAW
operand); unarmed: `+ ResolveTaskDistance(data)` into `0x102a42c0`. Complete.

**`0xa0` TASK_FIND_FAST_COVER_FROM_ENEMY — `0x102a20dc`.** Threat = enemy or this.
`0x102784a0(threat->EyePosition(), threat)` true -> `m_flMoveWaitFinished = curtime + data`,
complete. Else `0x102edc80(nav, threat->GetOrigin(), threat->EyePosition(), 0, 150.0, &out,
threat)`: false -> `0x2ef8`, `TaskFail(8)`; true -> goal type 6, activity `0x13`, tolerance -2,
`0x102a76a4`.

**`0xa1` TASK_FIND_FORWARD_COVER_FROM_ENEMY — `0x102a232e`.** The same lateral test, then
`0x102edd50(nav, origin, eye, &m_vecForward (+0x6290), 0.0, CoverRadius() * 0.25, &out, threat)`:
false -> `0x2f32`, `TaskFail(8)`; true -> goal type 6, `0x102a76a4`.

**`0xa2` TASK_FIND_COVER_FROM_SAVEPOSITION — `0x102a2231`.** `0x102edc80(nav, &m_vSavePosition,
&m_vSavePosition, 32.0, CoverRadius(), &out, this)`: false -> `0x2f0b`, `TaskFail(8)`; true -> goal
type 4, activity `0x13`, tolerance -2, `0x102a76a4`.

**`0xa3` TASK_FIND_FLANK_NODE_TO_ENEMY — `0x102a24b1`.** No enemy -> `0x2f3c`, `TaskFail(6)`. LKP
(`0x102dfed0`); range as `0x81` (max 2000 / `max(0x8c0,0x8c4)`, min `min(0x8b8,0x8bc)`, clamp to
`m_flDistTooFar`). `curtime - 0x102e0150(enemy) >= 2.0` (`0x10452dc4`) -> `0x102edaa0(nav, &lkp,
&lkp+enemy view offset, min, max, 1.0, 0, &out)`; fresher -> the enemy's forward
(`0x10139610(enemy->GetAbsAngles())`) and `0x102ed9c0(..., min, max, 1.0, &fwd, 0, &out)`. False ->
`0x2f71`, `TaskFail(0xb)`. True -> goal type 4, activity `0x13`, tolerance -2, `SetGoal(goal, 2)`,
then `0x102ee530(nav, lkp - out)`; RUNNING.

**`0xa4` TASK_FIND_INTERESTING_PLACE — `0x102a1f23`.** `m_pInterestingPlace (+0x62ec) =
PickRandomInterestingPlace(this)` (`0x102db590`); null -> `0x2eaf`, `TaskFail(0x22)`.
`PickSpotFor(place, this, &m_vecInterestingPlace (+0x62f0), 1)` (`0x102da0d0`): false -> `+0x62ec =
0`, `0x2eaa`, `TaskFail(0x22)`; true completes.

**`0xa5` TASK_GET_PATH_TO_INTERESTING_PLACE — `0x102a1fc3`.** No place -> `0x2ed0`,
`TaskFail(0x22)`. Goal type 8, destination `m_vecInterestingPlace`, activity -1, tolerance -1,
`SetGoal(goal, 0)`: complete / `0x2ecb`, `TaskFail(0xc)`.

**`0xa6` TASK_PAUSE_MOVING — `0x102a1f06`.** `0x102bf770(this)`, complete.

**`0xa7` TASK_TEST1 — `0x102a1943`.** With a weapon: `debug_test_switch1` (`DAT_1092467c`,
`!IsCommand() && m_nValue`) -> weapon `+0x10c`, else `+0x108`. `origin.x/y += RandomFloat(-200,
200)`; `0x10142aa0` draws a ±2 box; then the turn tail at that point.

**`0xa8` TASK_TEST2 — `0x102a1a60`.** `v = debug_test_switch2` (`DAT_10924634`); `DEC; CMP 3; JA`,
four-entry table `0x102a7c00`: 1 -> 5, 2 -> `0x1118`, 3 -> `0x19`, 4 -> `0x58`, else 1;
`RestartIdealActivity(v)`, complete.

**`0xae`/`0xaf` TASK_CREATE_HUNT_PATROL_LIST / TASK_FIND_HUNT_PATROL_TARGET — `0x102a3bc7` /
`0x102a3c5d`.** Slot 168 (`+0x2a0`) target's origin or null; `m_pPathfinder (+0x5d3c)->0x10306700
(this, origin, target, 256.0)` / `0x10306f60(..., &m_vecHuntPatrolTarget (+0x645c))`: complete /
`0x31cd` / `0x31e8`, `TaskFail(0x20)`.

**`0xb0`/`0xb1` TASK_WAIT_ATTACK_TIME1/2 — `0x102a337d`.** No weapon -> complete.
`m_flWaitFinished = 0x10252450(weapon, id == 0xb1) + 0x102c5730(this, weapon)`; `<= curtime` ->
complete (`0x102a4e51`); a held hint -> RUNNING; else `RestartIdealActivity(5)`, RUNNING.

**`0xb9` TASK_RUN_DIALOG — `0x102a496b`.** `a = 0x102c1400(this)`; -1 completes; else slot 310
`SetActivity(a)`, motor stop-turn, `0x102e1e20(motor, -1)`, RUNNING.

**`0xba`/`0xbc` TASK_RUN_DISPOSITION / TASK_SPECIAL_IDLE_ACTIVITY — `0x102a49bc`.**
`m_flWaitFinished = curtime + data`, RUNNING. **`0xbb`/`0xbd` — `0x102a49da`.** `= RandomFloat(0,
data) + curtime`, RUNNING.

**`0xbe` TASK_ADD_EVENT_EXPRESSION — `0x102a4a07`.** `i = (int)data`; `0 <= i < 2` ->
`AddExpressionForEvent(i)` (`0x101072b0`), complete; else `"Invalid event expression: %d\n"` with `i`
(through the print import `[0x109f364c]`, not the DevWarning import `[0x109f3658]`), complete.

**`0xc4` TASK_SET_PRESERVE_PATH — `0x102a3cfa`.** `(int)data` non-zero -> `m_bfAINPCFlags |= 8`, else
`&= ~8`; complete.

**`0xc5` TASK_SET_ENEMY_ELUDED — `0x102a46fa`.** No enemy -> `0x3333`, `TaskFail(6)`;
`GetEnemies()->0x102dfd90(enemy)`, complete. **`0xc6` TASK_SET_TARGET_ELUDED — `0x102a4763`.**
`m_hTargetEnt (+0x5ce4)` dead -> `0x3341`, `TaskFail(1)`; the same write, complete.

**`0xc7` TASK_GET_PATH_TO_ENEMY_CLOSEST — `0x102a4812`.** No enemy -> `0x334c`, `TaskFail(6)`. Goal
type 4, destination `enemy->GetOrigin()` (slot 220), activity -1, tolerance -1, goal flags 2,
`SetGoal(goal, 0)`: complete (`0x102a4e51`) / `DevWarning(2, "GetPathToEnemy failed!!\n")`,
`0x335a`, `TaskFail(0xc)`.

**`0xcf`/`0xd0` TASK_SET_INSIDE/OUTSIDE_INTERRUPT_DIST — `0x102a4a5b` / `0x102a4a97`.**
`i = (int)ResolveTaskDistance(data)`; `+0x6324` / `+0x6328` = `(float)(i*i)` (`IMUL`); complete.

**`0xd3`/`0xd4`/`0xd5` interrupt time — `0x102a4ad3` / `0x102a4afb` / `0x102a4b2e`.**
`m_flInterruptTime (+0x632c) = curtime + data` / `+= RandomFloat(0, data)` / `= 0`; complete.

**`0xd6` TASK_WALK_RUN_PATH — `0x102a4b4e`.** `d = ResolveTaskDistance(data)`; `d*d <= nav+0x14`
-> `act = TranslateActivity(0x13)`; otherwise `act` is an UNINITIALISED stack word
(`0x102a4b83 MOV EDI,[ESP+0x104]`). `0x10295460(act, -1) == -1` -> `TranslateActivity(9)`.
`0x102ee250(nav, act)`, `m_afMemory (+0x5d8c) &= ~2`, complete.

**`0xd7` TASK_WALK_RUN_PATH_COMBAT_SOUND — `0x102a4bd8`.** `+0x60b4` (the `m_BestSound` record's
second word) is 1 or `0x10` AND `TranslateActivity(0x13) != -1` AND its sequence exists -> run; else
`TranslateActivity(9)`. The same two writes, complete.

**`0xd8` TASK_GET_PATH_TO_PLAYER_FOR_DIALOG — `0x102a4c7a`.** `m_hMoveTargetEnt (+0x6240) =
closest player ? its handle : -1`. Goal type 4, tolerance -1, target = `0x100d1590(&m_hMoveTargetEnt)`,
destination = the move target's `GetOrigin` — called through a NULL pointer when the handle is dead
(`0x102a4d74`). `GetNavigator()->SetGoal(goal, 0)`, RUNNING.

**`0xd9` TASK_WALK_RUN_PATH_FOR_DIALOG — `0x102a4db7`.** `0x102c6460(&m_hClosestPlayer, 0)` (the
handle is null) -> `TaskFail(0x17)` with no line pair. `dist = 0x102a9570(selfOrigin,
playerOrigin)`; `dist >= m_flSpecialDistanceAccum (+0x5bac)` or no ACT_WALK sequence -> ACT_RUN, or
`TaskFail(0x15)` (no pair) when it has none; else ACT_WALK. `0x102ee250(nav, act)`, `0x102a98e0(this,
2)` (`m_afMemory &= ~2`), complete.

**`0xda` TASK_START_PLAYER_DIALOG — `0x102a4e7e`.** The closest player and slot 295 `CanTalk(p)` ->
`p->+0x678(this)`, complete; any miss is the break tail — complete, never a fail.

**`0xdb` TASK_SET_TOLERANCE_DIST_DLG — `0x102a4c47`.** `m_flGoalTolerance = 160.0 +
ResolveTaskDistance(data)`; `0x102a42c7` with HALF of it for `0x102ee1c0`, the whole for
`0x102f2fe0`; complete.

**`0xdc` TASK_DIE_IF_PLAYER_CANT_SEE — `0x102a3198`.** `TaskComplete(false)` FIRST (`0x102a31ca`). No
closest player -> the removal. Else `ResolveTaskDistance(data)` against `m_flPlayerDist (+0x6264)`:
`TEST AH,0x41; JP` returns when the distance is GREATER. Else trace own eye (slot 220 + `+0x184`) to
the player's, mask `0x4091` (`0x1004f7a0`, `0x101d3190`, enginetrace `+0x10`; a debug line when
`0x10005b87(0x10738960)`); fraction `== 1.0` -> return. Removal: `0x102b53d0(this, 0, "Leaving
interesting place (TASK_DIE_IF_PLAYER_CANT_SEE)")` then `UTIL_Remove(this)` (`0x101cd940`).

**`0xdd` TASK_KNOCKOUT — `0x102a3339`.** `m_bfAINPCFlags |= 0x440a0000`, `RestartIdealActivity(0x1050)`,
RUNNING. **`0xde` TASK_UNKNOCKOUT — `0x102a3364`.** `RestartIdealActivity(0x1052)`, RUNNING.

**`0xe0` TASK_DO_JUMP_ACTIVITY — `0x102a4ec7`.** Slot 310 `SetActivity(0x1089)`, RUNNING.

**`0xe1` TASK_DO_LOOP_ACTIVITY — `0x102a4ee3`.** `a = (int)data`; `0x10272130(this, a, &seq, …)`;
`seq > 0` (`JG`) -> slot 310 `SetActivity(a)`, RUNNING; else complete.

**`0xe2`/`0xe3` TASK_DO_BLEND(_LOOP)_ACTIVITY — `0x102a4f38` / `0x102a4fb1`.** Two byte-identical
bodies. `cycle = 0`; slot 271 `FindLayerByOwner(a - 1) != -1` -> `cycle = m_AnimOverlay[layer].m_flCycle`
(`+0x740 + layer*0x30`), slot 274 `RemoveLayerByOwner(a - 1)`. `0x10272130(a)`; `seq > 0` -> slot 310
`SetActivity(a)`, `m_flCycle (+0x6f8) = cycle`, RUNNING; else complete.

## Corrections to the older record

- `0x102a1b4e`: the ideal state is `+0x5cc4`, not `+0x200`; `0x102a1c41`: `m_flWaitFinished` is
  `+0x5db4`, not `+0x5cc4`.
- `0x102a1e7d`: the teleport rescue runs while `curtime <= m_flTeleportMoveTimer`, not after it.
- `0x102a1dd2`: `0x102ee2e0` tests `CAI_Path+0x10` and `0x102ee2c0` CLEARS it (`0x1030bea0`); it is
  not `IsGoalSet`/`StopMoving`. `0x102ee620` is the goal type `path+0x5c` (`0x1030ba50`).
- `0x102a1a86`: TASK_TEST2's table is `0x102a7c00`, four entries; the 22-entry reading ran into the
  neighbouring table.
- `0x102a259c` / `0x102a40a3`: the minimum range is the SMALLER of `+0x8b8`/`+0x8bc`.
- `0x102a2ebb`: the follower backaway candidate is built from this body's origin, not the boss's.
- `0x102a3a2d..3b29`: TASK_GET_FULL_PATROL_PATH's goal activity is -1, not 0.
- `0x102a39be` / `0x102a3b91`: the patrol arms pass `&m_sppPatrolPath` at `+0x658c` (the smart
  pointer), whose pointee is `+0x6590`.
- `0x102a4bb3`: `m_afMemory` is `+0x5d8c`; `0x102a98e0(this, 2)` is `m_afMemory &= ~2`.
- `0x102a454c`: `m_iBurstFireCount` is `+0x6490`; `+0x6320` is `m_flGoalTolerance`.
- `0x102a4f0c`: TASK_DO_LOOP_ACTIVITY tests the resolved SEQUENCE (`> 0`), not a count.
- `0x102a4a3c`: the invalid-expression line goes through the import `[0x109f364c]`, not `DevWarning`'s `[0x109f3658]`.
- `0x102a3551` / `0x102a492e`: the string is `"GetPathToEnemy failed!!\n"` (two `!`).

**Unrecovered:** the `CAI_Path+0x10` byte's meaning (`0x102ee2e0`); what `0x102edae0` /
`0x102edbb0` / `0x102edd50` / `0x102edaa0` / `0x102ed9c0` / `0x102ee300` search internally; the
weapon words `+0x3a4`/`+0x3a8`/`+0x8b8..+0x8c4` and slots `+0x518`/`+0x51c`/`+0x5d0` by name; the
kick cast's target class `0x105da324`; `0x10252450` and `0x102c5730`; `0x102c1400`; the second
vector `0x102e0290` copies (`record+0x18`, read as the last SEEN position); the `+0x60b4` values 1 and
`0x10` by name; `debug_test_switch1/2`'s default string `0x105399a0`; `0x10142aa0`'s colour
arguments' meaning beyond a debug box.
