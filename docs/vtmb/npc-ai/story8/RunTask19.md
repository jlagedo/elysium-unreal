# RunTask19 — slot 444, the RUN half of the task interpreter (story 0019/8, lane L05)

Walked from the listings (`vtmb_asm`) with the pass-R packet
(`families-19-29/RunTask19-READING.md`) and its chunk walks. Every body answers nothing useful;
the task's status is what it writes (`TaskComplete` `0x10273e80` = `fTaskStatus 4`, `TaskFail`
slot 448 = `m_bShouldMove 0`, `+0x5c50`, `COND_TASK_FAILED`). A body that returns without either
leaves the task **running**. Only `TaskFail` sites stamp the `+0x1b44`/`+0x1b48` file/line trace;
completions never do. Port: `ElysiumNpcBaseRunTask19.cpp`, `ElysiumNpcRunTask19.cpp`,
`ElysiumNpcRunTask19Species.cpp`.

Shared vocabulary: `0x102e0b40` writes `motor+0x2c = -1.0` (the yaw clock); `0x102e1c10(yaw, speed)`
stores the ideal yaw `motor+0x34` (flipped 180 under the `+0x28` latch; direct store when
`+0x1c == 180.0`), stores `speed` at `+0x38` unless it is -1.0 or -2.0, and ends in `UpdateYaw(-1)`
`0x102e1e20`; `0x102e20b0(pos, speed)` is `0x102e1c10(yaw-to(pos), speed)`. `0x102ee620` is the
path's goal type and `0x102ee680` is exactly its non-zero test; `0x102ee6a0` is "the path has a
waypoint". `0x102dfed0` answers the enemy memory's last-known position, `vec3_origin` on a miss.

## `0x10288780` CAI_BaseNPC::RunTask

Dispatch `id - 2 <= 0xaf` through byte table `0x10289794` into `0x10289724`; anything else, and
every in-range id the byte table sends there, is the default `0x102896f5`.

1. **2 / 0x67** (`0x10288bb6`): complete when `!(curtime < m_flWaitFinished)`.
2. **4 / 0xb0 / 0xb1** (`0x10288bc1`): `0x102e0b40`; LKP of slot 167 `GetEnemy`; when slot 364
   `FInAimCone(lkp)` refuses, `0x102e20b0(lkp, -2.0)`; then the wait test.
3. **5** (`0x10288b7b`): spawnflag 0x400 completes; else `UTIL_FindClientInPVS(edict)` `0x101d1800`
   completes on non-zero, runs otherwise.
4. **0xb** (`0x10288c43`): no live `m_hTargetEnt` → `TaskFail(1)` line 0xc09. `range = slot 418(data)`;
   `d = |goal - GetOrigin()|2D`. When `d < range`, or when `|goal - target|3D > range * 0.5`,
   `d` becomes the 2-D distance to the target and the goal is re-aimed at it (`0x102ee220`). Then
   `d < range` → complete + `ClearGoal` `0x102ee270`; else `act = slot 571(d)`, navigator movement
   activity `0x102ee250(act)`, `SetIdealActivity(act)`.
5. **0x1f / 0x68 / 0x76 / 0x77**: the epilogue `0x10289718` — running.
6. **0x24 / 0x27** (`0x10289614`): running while `curtime <= m_flWaitFinished` and the goal type is
   set; else `m_bShouldMove (+0x1a40) = 0`, complete, `ClearGoal`.
7. **0x25 / 0x26** (`0x10289599`): running while `slot 418(data) < |GetOrigin() - goal|3D`; else the
   same stop/complete/clear.
8. **0x2a** (`0x102891cf`): complete on slot 251 `IsActivityFinished`.
9. **0x2b / 0x2c / 0x2f / 0x31 / 0x32 / 0x66** (`0x10288b4c`) and **0x6a / 0x6b** (`0x102887ad`):
   `UpdateYaw(-1)`; complete on `FacingIdeal` `0x10278c80`.
10. **0x2d** (`0x10288a18`): player = engine `PEntityOfEntIndex(1)` else `(0)`, `CBaseEntity::Instance`;
    none → `TaskFail(0x17)` line 0xbc7. Else `0x102e0b40`, `0x102e20b0(player, -2.0)`, slot 572
    `SetTurnActivity`; complete only when `curtime > m_flWaitFinished` (strict) and
    `DeltaIdealYaw` `0x102e1f90` `< 10.0` (`_DAT_1044e664`).
11. **0x2e** (`0x102889b5`): `0x102e0b40`; `0x102e20b0(lkp, -1.0)`; complete on `FacingIdeal`.
12. **0x30** (`0x1028886f`): no `m_pHintNode` (`+0x5ddc`) → `TaskFail(4)` line 0xb5c and retail then
    faults reading the null hint's `m_hHintOwner`. A hint owned by someone else →
    `DevMsg("Hint node (%s) being used by non-owner!\n")`. Complete on `IsActivityFinished`.
13. **0x34..0x37 / 0x3e / 0x3f** (`0x102891f4`): `AutoMovement`, `0x102e0b40`, LKP; for id 0x34 or 0x38
    (0x38 never reaches this arm) with capability bit 0x20000000 and `FInAimCone(lkp)`, hold the
    current motor yaw `0x102e1c10(motor+0x34, -2.0)`; else `0x102e20b0(lkp, -2.0)`. Complete on
    `IsActivityFinished`.
14. **0x38 / 0x3d** (`0x102890f3`): `AutoMovement`; for 0x38 also stop and aim at `m_hEnemy`'s LKP
    (-2.0). On `IsActivityFinished`: with an active weapon `m_bInReload (+0x898) = 1`, weapon slot 322,
    `ClearCondition(0x40)`, `ClearCondition(0x41)`, complete; with none, complete.
15. **0x39..0x3c / 0x52 / 0x53** (`0x102891c8`): `AutoMovement`, then 0x2a's test.
16. **0x4b** (`0x102889a2`): complete when `m_nSequence (+0x6f0) == m_nIdealSequence (+0x5ccc)`.
17. **0x54 / 0x55 / 0x56** (`0x102887c6`): the face target is `m_hTargetEnt` for 0x56 and slot 167
    otherwise; when it exists `0x102e0b40` and `0x102e1c10(VecToYaw(target - GetOrigin()), -2.0)`.
    `AutoMovement`; complete on `IsActivityFinished`.
18. **0x5f** (`0x10288fc4`): on `IsActivityFinished` with `m_flCycle >= 1.0`: `m_lifeState = 2`,
    `ThinkSet(NULL)`, `m_flPlaybackRate = 0`, `UTIL_SetSize` to `(-4,-4,0)/(4,4,1)` or, when
    `0x10279420` answers true, to the collision mins and `(maxs.x, maxs.y, mins.z + 1)`; then
    `SUB_StartFadeOut` `0x102695d0` when slot 552 `ShouldFadeOnDeath`, else
    `CSoundEnt::InsertSound(0x20, GetOrigin(), 0x180, 30.0)`. Never completes.
19. **0x60** (`0x102892aa`): a live cine whose `IsTimeToStart` `0x101a7540` answers yes: complete,
    `StartScript` `0x101a81a0`, the (re-resolved) director's slot 584 `StartSequence(this, m_iszPlay,
    true)`, `ClearSchedule` when `m_bSequenceFinished`, `m_flPlaybackRate = 1.0`. A live cine not yet
    due runs. A dead handle → `DevMsg("Cine died!\n")`, complete.
20. **0x62** (`0x10289439`): `AutoMovement`; on `m_bSequenceFinished (+0x65c)`: `SequenceDone`
    `0x101a8460` on a live cine and complete; with none, complete.
21. **0x63** (`0x102894e2`): when the sequence finished, or the cine's `m_hNextCine (+0x5f94)` is live,
    `Finish` `0x101a8640` (null director on a dead handle). Retail reads `+0x5f94` of a null director.
22. **0x69** (`0x102888d4`): in `NAV_JUMP`: on the ground → `NAV_GROUND`; airborne with speed
    `> 0.01` → running; else `NAV_GROUND` and `TaskFail(0x1c)` line 0xb81 — and the arm goes on.
    Unless `NAV_CLIMB`: `SetIdealActivity(0x1027a6c0())`, `m_bShouldMove = 0`, `TaskComplete` (which
    a raised failure refuses).
23. **0x6e / 0x6f** (`0x10288f43`): while `(data == 0 || curtime - timeCurTaskStarted <= data)` and the
    goal type is set: a waypoint → slot 528 `ValidateNavGoal`; none → `m_bShouldMove = 0`,
    `SetIdealActivity(GetStoppedActivity)`. Otherwise stop, complete, `ClearGoal`.
24. **0x71** (`0x1028964b`): on `IsActivityFinished`, `m_hTargetEnt` cast to `CBaseCombatWeapon` and
    `0x102521f0` (its owner) non-null → `TaskFail(2)` line 0xd35; else complete.
25. **0x72** (`0x10288e80`): no target → `TaskFail(3)` line 0xc3a; target slot 97 owner set →
    `TaskFail(2)` line 0xc30; goal type set → running; else complete + `ClearGoal`.
26. **0x74** (`0x102896d7`): complete on `FL_ONGROUND`.
27. **default** (`0x102896f5`): `DevMsg("No RunTask entry for %s\n", slot 449 TaskName(id))`,
    **complete** (`0x10289713`).

**Unrecovered:** `0x10279420`'s predicate; `0x102ee220`'s re-aim (no port navigator goal);
`_DAT_1049a17c` (slot 571's walk/run split; the port's `Slot571` stands at 0.0, the packet reads 190.0).

## `0x102aacf0` CAI_BaseNPCTroika::RunTask

Dispatch `id - 2 <= 0x147` through byte table `0x102ac844` into the 57-entry table `0x102ac760`;
index 0x38 and every out-of-range id tail-call `CAI_BaseNPC::RunTask`. Arms by index:

- **0x00** `0x102aad61` (2, 0x67, 0x68): `0x102aab70` (with `m_bfAINPCFlags2 & 0x10` aim at the
  enemy's LKP, else with `& 0x20` at `m_hTargetEnt`, each only outside the aim cone, -2.0), then the base.
- **0x01** `0x102ab659` (4, 0xb0, 0xb1): aim at the live shoot-target override, else the enemy's LKP,
  else `TaskFail(6)` line 0x407c **and still aim at the uninitialised point**; steer unless in the aim
  cone (-2.0); the wait test.
- **0x02** `0x102aad7e` (5 `TASK_WAIT_PVS`): spawnflag 0x400 or `ShouldThinkFrequently` completes;
  else `0x101d1a90(m_hClosestPlayer, this)`: false → running; true → slot 614, `m_flLastThink` and
  the four `+0x6254..+0x6260` stamps = curtime, complete.
- **0x03** `0x102aae43` (0x2b, 0x31): `SetTurnActivity` unless `m_afMemory & 0x2000`, then the base.
- **0x04** `0x102aae61` (0x2e): the turn test; aim at the override or the enemy's LKP (-1.0); complete
  on `FacingIdeal`.
- **0x05** `0x102ab0a9` (0x34): `AutoMovement`, aim (-2.0). With `m_iBurstFireCount > 0`: no weapon →
  complete (and retail then reads the null weapon); next attack time `0x10252450` still ahead →
  running; else decrement, `0x102aaa60` (the hint idle re-arm; stamps `m_flLastAttackTime`) true →
  running, false → complete. With no burst, complete on the activity.
- **0x06** `0x102ab1e5` (0x35, 0x3e, 0x3f): `AutoMovement`, aim (-2.0), complete on the activity.
- **0x07** `0x102ab2b2` (0x36, 0x37, 0x8c, 0x9a): `AutoMovement`; with an enemy and slot 591, aim at
  its origin (-2.0); complete on the activity.
- **0x08** `0x102aad1f` (0x4b): complete on the ideal sequence, else the wait test.
- **0x09** `0x102ab4c5` (0x54..0x56): face `m_hTargetEnt` (0x56; a stale handle is a null deref) or
  the override or the enemy's LKP with `0x102e1c10(VecToYaw, -2.0)`; `AutoMovement`; the activity.
- **0x0a** `0x102abb90` (0x5f, 0xe9, 0xeb) and **0x28** `0x102abb89` (0xea, `BloodExplode` first):
  gate `(IsActivityFinished && m_flCycle >= 1.0) || m_IdealActivity == 1`; credit
  `m_hClosestPlayer` (self for 0xe9/0x5f); `m_lifeState` 1 → 0; `Die(credit, 0, 0)`; 0xe9/0xea slot
  402; 0xeb with non-zero data slot 77. Never completes.
- **0x0b** `0x102aaf2e` (0x6e): timeout (`data != 0 && data < elapsed`) or no goal type → stop,
  complete, `ClearGoal`; no waypoint → stop, stopped activity, **complete**; `0x102f2ea0` not arrived
  → slot 528; arrived → stop, complete.
- **0x0c / 0x0d** (0x7a / 0x7b): `0x102aa860` on `m_sppPatrolPath` / `m_sppPatrolPathHunt`.
- **0x0e** `0x102ab83c` (0x92, 0x95, 0x98, 0xe6, 0xec, 0xee, 0xef, 0xf0, 0x10f, 0x110, 0x113):
  `AutoMovement`, the activity. **0x0f** `0x102ab2a3` (0x93, 0xe0): the activity.
- **0x10** `0x102ac4ef` (0x94): unless `NAV_JUMP` on the ground: `0x102a0870`; the wall probe
  `0x102a0490` → `ACT 0x91` (linked sequence or restart), yaw from the wall normal, `0x102c4e30`,
  `m_KnockbackVelocity = normal * 100`, `SetSchedule(0x14e)`; motor `+0x30 = 0`; running while
  `vz >= 0` or `0x102c4eb0` refuses. Landing: linked sequence `0x10345480`, motor slot 8,
  `NAV_GROUND`, `m_bJumping = 0`, complete, `ACT 0x90`.
- **0x11** `0x102abea5` (0x96): `SetSchedule(0x14f)` once `m_fKnockbackWallHitFallTime` passes.
- **0x12** `0x102abedb` (0x97): the playing sequence's activity 0x91 finished → `ACT 0x92`; the same
  airborne test; landing → `ACT 0x93` then complete.
- **0x13** `0x102ac2ed` (0x99): `AutoMovement`, `Bip01 Spine2`; unfinished → stamp the bone time and
  position; finished → `0x102c4e80`, a `CVDmg_t` (dice 1, to-hit 1, source this), damage info
  `(this, this, 1.0, 0, 0, dmg, -1)` with force `normalize(last - bone) * (last time - curtime) *
  50000`, stat list 0 `SetBaseToStatValue(0xf, 0x11)`, slot 144, slot 403, complete.
- **0x14 / 0x16** (0x9c / 0x9e): complete on the activity, else `AutoMovement`. **0x15** (0x9d):
  finished → drain `m_QueuedBurnDamage` (take record 0, swap the last in), when alive stop and fade
  (2.0) every live `m_hBodyFireParticles[18]` whose `+0x484` is set, slot 616, complete.
- **0x17** `0x102ab900` (0xa7, 0x11d, 0x12e), **0x2a** `0x102ab63b` (0xf7): the turn test,
  `UpdateYaw(-1)`, `FacingIdeal`. **0x18** (0xb2): the same after `TaskFail(0x22)` line 0x4105 with
  no interesting place.
- **0x19** (0xb3): no patrol interest place → complete; else turn, `FacingIdeal` → `+0x6300 = +0x659c
  = 0`, complete. **0x1a** (0xb4): no place → `TaskFail(0x23)` line 0x4141; `0x102aa210` done → the
  holster (`place+0x571`), `LeaveInterestingPlace(1, "…(RunTask-WaitFinished)")`, complete. **0x1b**
  (0xb5): the patrol twin, firing `m_OnInterestingPlaceLeft` when arrived, `0x102da600`, clearing the
  place words.
- **0x1c** (0xb6, 0xb7): finished → navigator movement activity `TranslateActivity(9)`, complete.
  **0x1d** (0xb8): finished → clear flag 0x20000000, `MoveToBoneOriginAngles("Bip01")`, complete.
- **0x1e** (0xb9): `0x102c1400` activity → `SetActivity` + `UpdateYaw(-1)`; -1 → complete and
  `ClearCondition(0x6f)`. **0x1f** (0xba, 0xbb): slot 588, the wait test. **0x20** (0xbc, 0xbd): with
  a live closest player within 128 and `COND 0x5a`, on a finished activity restart `0x10f7` **when no
  weighted sequence exists** else slot 588; otherwise slot 588; the wait test.
- **0x21** (0xdd), **0x25** (0xe2, 0x149, after `AutoMovement`): the activity. **0x22** (0xde):
  finished and ideal 0x1068 → complete, flags `&= 0xbbf5ffff`; finished otherwise → restart 0x1068.
  **0x23** (0xdf): `Die(0,0,0)`. **0x24 / 0x26** (0xe1 / 0xe3): `SetActivity(ftol(data))` unless
  `m_bLastDisciplineResist` and finished → complete. **0x27** (0xe7): the ideal sequence.
  **0x29** (0xed): the epilogue — running.
- **0x2b** (0xf8..0xff, 0x148): `UpdateYaw(-1)`; when the wait passed or the activity finished and
  `FacingIdeal`, clear 0x08000000, complete. **0x2c** (0x107..0x109): finished → ideal 0xf1d →
  `SetIdealActivity(0x1052)`; else clear 0x4000, complete. **0x2d** (0x10a..0x10c): `0x102aab70`,
  `AutoMovement`, the activity. **0x2e** (0x10d): `AutoMovement`; override → aim; cover object is
  the enemy → LKP; else **complete and aim at an uninitialised point**; `FacingIdeal` → complete.
- **0x2f / 0x30** (0x116 / 0x117): finished → complete, clear 0x40000. **0x31** (0x118): ideal is the
  hint's `0x102a13d0` → `0x102a1620`; is `0x102a1510` with non-zero data → `0x102a15c0`; else
  complete, clear 0x40000.
- **0x32** (0x11b, 0x11c, 0x123): `AutoMovement`, aim at the enemy (-1.0), past the wait →
  `m_flDesiredMoveYaw = 0`, complete. **0x33** (0x122, 0x124): the same aim; finished and inside the
  wait without `COND 0xe`: `ACT 0x1121`, else 9, else `TaskFail(0x15)` line 0x4255, restart it;
  otherwise the yaw clear and complete.
- **0x34** (0x128): the 0x01 aim (`TaskFail(6)` line 0x4304); past the wait, the activity.
  **0x35** (0x137): `AutoMovement`, aim at the enemy (-1.0), the activity. **0x36** (0x139, 0x13b,
  0x13c): `UpdateYaw(-1)`, the activity. **0x37** (0x13a): unless landed: yaw from the local velocity
  (-1.0), motor `+0x30 = 0`, running while `vz >= 0` or `0x102c4eb0` refuses; landing: motor slot 8,
  `NAV_GROUND`, `m_bJumping = 0`, complete.

**Unrecovered:** `0x102f2ea0`'s tolerance source; `0x102a0870`, `0x102a0490`'s traces;
`0x103454c0`/`0x10345480`'s studio link words; `0x102c1400`; `0x102c4e80`; the arg of
`LeaveInterestingPlace` (`1` here); `place+0x571`'s holster slot 315 on the weapon.

## Species

- **`0x1035f940` CNPC_VAnimal** (Rat, Scurrying): 0x36/0x37 `AutoMovement`, face LKP (-2.0),
  activity; 0x89/0x8a the same, completing also when `curtime - m_flLastAttackTime > data`;
  0x8b/0x8e `AutoMovement`, activity; else Troika.
- **`0x10374a20` CNPC_VDog**: tasks 2/0x67 in `m_NPCState == 3` aim at `UTIL_GetLocalPlayer`'s origin
  (-2.0); always `CNPC_VAnimal::RunTask`.
- **`0x103e01d0` CNPC_VZombie**: 0x14c/0x14f/0x150 activity; 0x14e activity → complete,
  `AddMiscFlag(0x80000)`, non-virtual `CreateCorpse(&m_vecDeathForceVector, this+0x668c)`; 0x151
  wait passed or no goal → stop, complete, `ClearGoal`; 0x153 activity, else (AI type 7) aim the motor
  at the closest player (`0x102e2020` + `UpdateYaw(-1)`); else `CNPC_VAnimal`.
- **`0x10384ab0` CNPC_VHuman** (28 human classes): 0x89/0x8a as Animal; 0x8b the melee swing (enemy
  and its `+0x9c` combat view, `GetMeleeDiceRolls`; ideal 0x1157 → `IsMeleeSwingOver`, else
  `m_flCycle` against 0.5 with a roll (band `0x103498b0 == 0` arms completion) or 1.0 without, then
  the activity; finish → 0x1155 has a sequence and armed → complete, else `TaskFail(0x21)` line 0x1da);
  0x8d no enemy → complete, else wait for every event of the enemy's sequence; 0x8e..0x91 face when
  the enemy has a combat view, complete on the activity or `m_flNextAttack` passed; else Troika.
- **`0x103793e0` CNPC_VGargoyle**: 0x31 turn, `UpdateYaw(-1)`, `FacingIdeal`; 0x12f activity; else Human.
- **`0x1037b9f0` CNPC_VGhoulCroucher**: 0x14a activity; 0x14b activity → `m_bUnawareExited = 1`; else Human.
- **`0x103b38a0` CNPC_VTaxiDriver**: 0xb9 `0x102c1400 == -1` → complete, `ClearCondition(0x6f)`,
  `m_bFirstThink = 0`, `SetActivity(1)`; else Human.
- **`0x103c5f40` CNPC_VVampireBoss**: 0x14d `WaitForTransformation`; else Human.
- **`0x103af780` CNPC_VSheriffMan**: 0x154 swallowed (running); else VampireBoss.
- **`0x1035d8b0` CNPC_VAndreiBlood**: 0x150 `FacePlayerAdvance`; 0x151 `m_takedamage = 0` at cycle
  0.5, activity; 0x152 the unhide (slot 67, `m_takedamage 2`, counters reset), face, activity; 0x154
  activity → `m_bForceTeleport`, complete; 0x156 force, hit cap or 5 s since the wait start → complete;
  else VampireBoss.
- **`0x103612e0` CNPC_VAsianVampire**: 0x13a VampireBoss then restart 0x2d when not in it and
  `vz <= 250`; 0x150 `SetupJump(m_pHintNode)`, `m_bPathBlocked = 0`, complete; 0x151/0x152 running.
- **`0x1036bfc0` CNPC_VChangBros**: `StoreArenaCenter` first; 0x8b the Human swing variant (line
  0x295); 0x13a as AsianVampire at 500; 0x150/0x151/0x153/0x15d `UpdateYaw`, `AutoMovement`,
  activity; 0x156 until `m_fEnergyChargeTime`; 0x157 the energy ball at cycle 0.591; 0x15a the other
  brother's `ReadyForUnited` (none → `TaskFail(1)` line 0x255); 0x15b the emitters and the centre
  emitter at arena centre + 50; 0x15c the united time (none → `TaskFail(1)` line 0x26b).
- **`0x103a8990` CNPC_VSabbatLeader**: prologue `andrei_force_awaken` → `StartTransformation` with a
  player within 500 (2-D); 0x15c the dive-in (yaw from velocity, splash emitters and wav at
  `_DAT_1093c33c`, landing gravity 1, `NAV_GROUND`, slot 66, complete); 0x139 the retreat wav then
  VampireBoss; 0x13a landing stamps and the jump steer, then VampireBoss; 0x13c the normalized
  100-unit velocity toward the player (face speed 50), then VampireBoss; 0xbc/0xbd running in
  dialogue; 0x15d the emerge (slot 67, `m_fEffects &= ~0x20`, not-solid cleared, complete); 0x15e
  the warning time; 0x36/0x37/0x9a/0x160 face the enemy at speed 10, activity; 0x161/0x163
  activity; 0x162 one second after the task start.
- **`0x1038d130` CNPC_VManBat** (falls to `CAI_BaseNPC::RunTask` DIRECT): 0x14a aims at the SUM of
  its origin and the fly node's, activity → complete + flap; 0x14b arrived → teleport onto the node
  when the descrambled mode equals `fold(0xfa0b0695)`, clear the node, complete; 0x14c on the
  ground → stop, leave flight, complete, `ACT 0x1171`, `fall.wav`; 0x14d aim along velocity; 0x150
  land; 0x152/0x155/0x157 on arrival; 0x156 release the carried body; 0x158 flap; 0x159/0x160 the
  fly-by landing onto a live target (`TaskFail(1)` without the file trace otherwise); 0x15a the
  melee weapon attack (`TaskFail(0x1f)` line 0x4e1); 0x15b the spotlight kill; 0x15c the next
  script node (`TaskFail(4)` on a missing hint); 0x161 the throw (`ThrowModel`, target `Kill`); 0x162
  the fly-by sound; 0x163 the coast timer.
- **`0x10393930` CNPC_VMingXiao** / **`0x1039d750` CNPC_VMingXiaoTentacle**: the melee/throw arms
  (weapon switch back, throw release, attack timers from `0x103983d0`), the 0x8e event walk, the
  tentacle's flex blend `F%02d` over `[0, 4]` s and its type-19000 shoot-hint walk from the shared
  cursor `DAT_1093bd34`.
- **`0x103bb1e0` CNPC_VTzimisce** / **`0x10380cb0` CNPC_VHengeyokai**: the claw/grab arms
  (`0x103be8e0` / `0x103822a0`, `COND 0x1b`, `m_flTaskFailTimer`), the translated-activity
  completions (slots 375 → 381 → 376), the hint-usable test `TaskFail(0x1a)` line 0x911.
- **`0x103c3870` CNPC_VTzimisceRunner**: 0x122..0x124 `AutoMovement`, stop + face the enemy (-1.0),
  past the wait `m_flDesiredMoveYaw = 0`, complete.
- **`0x103cdfb0` CNPC_VWerewolf**: the hint movers (0x14b..0x14d), the anim-point snaps, the fake hull,
  `TeleportOut` + `SetSchedule(0x158)` on activity 0x10b, `m_lifeState 2` + `OnFinishCrushAnimation`
  (0x15f); its `TaskFail("Did not path out of player's sight")` passes the STRING as the reason code.

**Unrecovered:** `0x103498b0`'s thresholds; `IsMeleeSwingOver` / the event walk inputs (no studio
data at the kernel tier); `0x1039aa20`, `0x10398db0`, `0x10383470`, `0x1038c170`, `0x1038fe30`,
`0x1038e720`; the ConVar at `DAT_1093f9a4`; `_DAT_1093c33c`; `m_iWasOccluded`'s producer.
