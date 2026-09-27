# Script19 — the script directors and the Troika's scripted helpers (story 8, pass I)

Walked prose for the 20 `rule` rows of family Script19. Source of truth: the packet
`$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/families-19-29/Script19-READING.md` and the
listings (`vtmb_asm`). Pass C folds these sections into `authored-control.md`, `schedule-kernel.md`
and `lifecycle.md`. Port: `ElysiumScriptedSequence.cpp`, `ElysiumAiScriptedSequence.cpp`,
`ElysiumAiScriptedSchedule.cpp`, `ElysiumNpcBaseScript19.cpp`, `ElysiumNpcScript19.cpp`,
`ElysiumNpcScript19Species.cpp`; tests `Elysium.Substrate.NpcKernelScript19.*`.

`0x101a7140 CCineNPC::UpdateOnRemove` (19 bytes) has no section: base `UpdateOnRemove 0x1027ca30`
direct (`0x101a7143`), then `ScriptEntityCancel 0x101a7170(this)` (`0x101a7149`).

## `0x101a8c30` CancelScript (130 bytes)

1. `DevMsg(2, "Cancelling script: %s\n", m_iszPlay ?: "")` (`0x101a8c34..0x101a8c4b`).
2. `m_iName` (`+0x26c`, the cine's OWN targetname — not `m_target +0x20c`) null → `ScriptEntityCancel(this)`
   and return (`0x101a8c5c` / `0x101a8c5f` / `0x101a8c69`).
3. Otherwise `FindEntityByName(NULL, m_iName)` and every next match (`0x101a8c76`, `0x101a8ca4`),
   `ScriptEntityCancel` on each (`0x101a8c82`); the name is re-read each pass, the empty string
   substituted when null (`0x101a8c92`).

Reads `+0x5f48`, `+0x26c`. Writes nothing itself (the cancel zeroes each cine's `+0x5f70`).
**Unrecovered:** none.

## `0x101a8640` Finish / PostIdleDone (396 bytes)

1. Hold arm: `m_iszPostIdle (+0x5f4c)` set (`0x101a864d`) AND spawnflag `0x100` (`0x101a865c`) AND
   `m_hNextCine (+0x5f94)` not live (`0x101a8671..0x101a8690`) → `DevMsg(2, "Post Idle %s finished\n")`
   with the post-idle of the NPC's OWN `m_hCine` re-resolved from `npc+0x5d74` (`0x101a8696..0x101a86d5`;
   a stale handle reads `[NULL+0x5f4c]` and faults — the port prints the empty string), `0x1027f270(2)`
   (empty), `npc->m_scriptState := 2` (`0x101a86e7`), slot 584 `(npc, m_iszPostIdle, 0)` (`0x101a86ff`),
   RETURN (`0x101a8708`) before any cleanup.
2. Spawnflag 4 clear (`0x101a8712`) → `ThinkSet(SUB_Remove)` and `m_flNextThink = curtime + 0.1`
   (`0x101a871f` / `0x101a8733`).
3. `CineCleanup 0x1027d170(npc)` (`0x101a873f`), slot 586 `FixScriptNPCSchedule(npc)` (`0x101a8749`).
4. `m_hNextCine` live (`0x101a8758..0x101a877d`) and either not `this` or spawnflag 4 set
   (`0x101a87a9` / `0x101a87b2`) → `SetTarget(next, npc)` (`0x101a87b7`) and next's slot 583 (`0x101a87c0`).

**Unrecovered:** none.

## `0x1027d0a0` ExitScriptedSequence (153 bytes)

1. `m_lifeState (+0x200) == 1 LIFE_DYING` (`0x1027d0a0` / `0x1027d0a7`) → `+0x1b3c/+0x1b40 =
   AI_BaseNPC.cpp:0x2a70`, `m_IdealNPCState (+0x5cc4) := 7` (`0x1027d0a9..0x1027d0bd`), answer FALSE
   (`0x1027d0c7`); `m_hCine` stays.
2. `m_hCine (+0x5d74)` -1 / stale / null → answer TRUE with nothing cancelled (`0x1027d0d4` /
   `0x1027d0f6` / `0x1027d0fc` → `0x1027d135`).
3. Live → `CancelScript` on it (`0x1027d125`), TRUE (`0x1027d12a`). The re-read's null-call arm
   (`0x1027d12e` / `0x1027d130`, `CancelScript(NULL)`) is unreachable: nothing runs between the reads.

Port: `m_lifeState` has no word; DYING is the death transaction between `Event_Killed`
(`bDeathReported`) and `TASK_DIE`'s commit (`bDeathCommitted`). **Unrecovered:** none.

## `0x1029f460` BuildPatrolPath (277 bytes)

Args `(cell, repeat, type, schedule, nodeIds, bReplace)`; the cell is `m_sppPatrolPath +0x658c` or
`m_sppPatrolPathHunt +0x6594` (owned byte `+0`, `CAI_PatrolPath*` `+4`).

1. `m_NPCState == 7` → return (`0x1029f46b`); null cell → return (`0x1029f477`).
2. `bReplace` clear (`0x1029f486`): an existing path skips straight to step 5 (`0x1029f4d1`); an empty
   cell allocates (`0x10307d30`, `0x1029f4d3`), writes the owned byte (`0x1029f4dd`), resets
   (`0x10307aa0`: `type := -1, schedule := 0, count := 0`), FORCES `type := 0` and `repeat := 0`
   (`0x1029f4ea`). A dry pool leaves a null path that the reset then faults on.
3. `bReplace` set: allocate when empty (`0x1029f48c`, owned byte `0x1029f494`); still null →
   `DevMsg("Failed to create patrol path for %s\n")` and return (`0x1029f49c..0x1029f4b6`); else reset
   and `repeat := arg` (`0x1029f4c9`).
4. `type := arg` (`0x10307b40`, `0x1029f4f4`).
5. Append every id to the `-1` terminator (`0x10307bf0`, `0x1029f500..0x1029f51b`).
6. `current := min(count - 1, DAT_1049df28[type])` (`0x10307b60` / `0x10307c20`); the type table
   `DAT_1049df20` rows are `{id, name, start, step, next}` = `{0,"0",0,1,0}`, `{1,"1",0x7fff,-1,1}`,
   `{2,"2",0,1,3}`, `{3,"3",0x7fff,-1,2}` — types 1 and 3 start at the last node.
7. Non-zero `schedule` → `path+4 := schedule` (`0x1029f531`).
8. `path+4` non-zero → `0x1029f650(this, this+0x658c)` (always the PATROL cell, `0x1029f547`), stamp
   `AI_BaseNPCTroika.cpp:0x27a1` (`0x1029f54c` / `0x1029f556`), `SetSchedule 0x102ae750(path+4, 0)`
   (`0x1029f56b`).

The pool (`0x10307d30`): 32 slots of `0x114` bytes at `0x10934158`, in-use bytes `DAT_109363d8`,
cursor `DAT_109363f8`; allocation scans from the cursor and bumps the CURSOR by one; a cursor above
`0x1f` is `Error("Patrol path pool is dry.  It will store up to %d paths.  Change
PATROL_PATH_POOL_MAX_PATHS to increase this amount.\n", 0x20)`. `0x10307db0` frees a slot and lowers the
cursor to it; `0x10307d00` resets it. **Unrecovered:** the network node ids the inputs pass
(`0x102d2900`) — the port stands a node id as the patrol hint's entity index.

## `0x1038b1a0` ManBatOverrideMoveFly (366 bytes) and `0x1038b120` OverrideMove (96 bytes)

`0x1038b120`: navigator type (`0x1027d990`) 2 → `0x1038b1a0(interval)`, TRUE (`0x1038b12b..0x1038b13c`);
otherwise `0x1042fbf0(ladder(+0x6670)) == 0x1042fbf0(0xfa0b0699)` (`0x1038b13f..0x1038b179`).

`0x1038b1a0`:
1. `cvar_manbat_stun` (`0x1093b858`, "manbat_stun", default "0"): not a command and `m_nValue` set
   (`0x1038b1a6..0x1038b1c1`) → stamp `NPC_VManBat.cpp:0x1ad`, `0x102ae750(0x15b, 0)`, `ConVar::SetValue(0)`,
   return (`0x1038b1cc..0x1038b1f6`).
2. Interval above 1.0 → 1.0 (`0x1038b1f9..0x1038b20c`; NaN is kept).
3. `0x1038b370(this, &vel, interval)` (`0x1038b220`), `SetAbsVelocity(vel)` (`0x1038b240`).
4. `m_Activity (+0xfec)` in `{0x30, 0xb0, 0x4b, 0x1171}` → return (`0x1038b24e..0x1038b26d`).
5. `0x102e1c10(motor, VecToYaw(vel), -1.0)` (`0x1038b283` / `0x1038b28d`): the `+0x28` flip, the
   `+0x1c == 180` direct write else `0x102e0a80`; the rate `-1.0` equals `_DAT_104492dc` and takes
   `0x102e1cf0` (motor `+0x38 := MaxYawSpeed()`) — it does NOT leave the rate alone — then `0x102e1e20(-1)`.
   `0x102e1cf0` is the same call on the same `m_pMotor (+0x5d44)` that `SetActivityAndSequence 0x10272490`
   ends in (`0x10272569` / `0x10272575`); the port counts both on one seam (`NavigatorActivityNotices`).
6. `GetAngles` with pitch `:= VecToPitch(vel)` (`0x101d2ce0`) through `SetAngles` (`0x1038b296..0x1038b2ca`).
7. `m_flFlapTimer (+0x6678) <= curtime` (`0x1038b2d6..0x1038b2e4`) → `0x1038e720(vel)` (`0x1038b301`).

`0x1038e720` (the selector): refuses activities `0x28, 0x30, 0xb0, 0x4b, 0x1171`; `vel.z >= 30.0`
(`_DAT_104492a8` f32 — the VELOCITY's z, not the interval) → `0x1038e640` (act `0x22`, 2.3 s); else
`turn = VecToYaw(vel) - angles.yaw` (+360 when negative); `!(turn < 30) && !(turn > 330)` — an unordered
turn stays in the band (`0x1038e7cb` leaves on C0 alone, `0x1038e7da` on C0=C3=0) — (f64 cells `0x1044dcf0`,
`0x104bc6a0`) → `0x1038e6a0` (act `0x116d`, 0.2 s) below 180 (`0x10452918`) else `0x1038e6e0` (`0x116e`,
0.2 s); otherwise activity `0x24` → `0x1038e640`, anything else `0x1038e670` (`0x24`, 4.0 s).
`UTIL_VecToYaw 0x101d2c70` answers 0 for a vertical vector and wraps into `[0, 360)`; `UTIL_VecToPitch
0x101d2ce0` answers 180 (z < 0) / -180 (else) for a vertical vector, else `atan2(-z, len2d)`.
**Unrecovered:** the motor `+0x1c` word (the port's `MotorIdealYaw` takes the direct write).

## `0x101a7880` CCineNPC PossessEntity (1577 bytes)

1. `m_hTargetEnt (+0x5ce4)` → entity → `+0x94`; any miss returns (`0x101a7891..0x101a78ca`).
2. NPC `m_bRanAI (+0x1b4c)` clear → the eleven-line `DevMsg` block with the "that has not run it's AI
   yet....." line (`0x101a78da..0x101a794f`).
3. NPC `m_hCine (+0x5d74)` live (`0x101a795d..0x101a7987`) → queue arm: the current cine's live
   `m_hNextCine` is kicked (`SetTarget(kicked, NULL)` `0x101a7af7`, `DevMsg(2, "script \"%s\" kicking
   script \"%s\" out of the queue\n")` `0x101a7b13`), our handle goes into its `+0x5f94`
   (`0x101a7b5a`), return. The `Msg`s under the debug ConVar `DAT_1072bb84` are dead.
4. The take: `0x100b5190` (`+0xf4`) → `0x101a77a0` (`0x101a7c30`); `m_interruptable (+0x5f90)` clear →
   `0x1026d130` (`0x101a7c41`); empty `m_iszNextScript (+0x5f58)` → `m_hNextCine := -1` (`0x101a7c50`);
   `m_pGoalEnt`, `m_hCine`, `SetTarget(npc, this)` (`0x101a7c5a..0x101a7c72`); the save block `+0x5f78`
   movetype, `+0x5f7c` movecollide, `+0x5f80` solid, `+0x5f84` solid flags, `+0x5f88` effects
   (`0x101a7c7b..0x101a7cbd`; the effects word is the NPC's `m_fEffects +0x19c`, which `CineCleanup
   0x1027d170` writes back at `0x1027d271` / `0x1027d279`); NPC `+0x98` → slot 614, `+0x5f8c := npc+0x14b8`, OR `0x40` under spawnflag
   `0x1000` (`0x101a7cc3..0x101a7cf6`); `npc->m_fEffects |= ours` (`0x101a7d0a`).
5. `m_fMoveTo (+0x5f60)`, table `0x101a7eac`: 1/2/3 → state 4/5/6 each with `DelayStart(1)`; 4 →
   teleport (slot 181 to our origin with NULL angles and a zero velocity; `0x102e0b40`; the motor's
   ideal yaw from our yaw with the `+0x28` flip; `SetLocalAngularVelocity(0)`; `EF_NOINTERP`; NPC yaw :=
   ours, pitch/roll kept) and FALLS THROUGH; 0/5 → state 1; above 5 skips.
6. `AI scripted.cpp:0x2c8`, `m_IdealNPCState := 4` (`0x101a7e84..0x101a7e98`).

**Unrecovered:** the director's own `m_fEffects` (no port word; spec 0003), so the OR at `0x101a7d0a` adds
nothing here.

## `0x101a8460` SequenceDone (371 bytes)

1. Debug-ConVar `Msg` and `0x101a6ec0` (`0x101a8463..0x101a850b`) — dead.
2. `m_iszPostIdle` empty OR `m_hNextCine` live (`0x101a8518..0x101a8545`) → `Finish` (`0x101a857b`);
   else `0x1027f270(2)`, `npc->m_scriptState := 2` (`0x101a8554`), slot 584 `(npc, m_iszPostIdle, 0)`
   (`0x101a856c`).
3. `m_OnEndSequence (+0x5fb4)` through `0x100cd660` LAST and unconditionally, activator `+0x10c` or NULL,
   caller `this` (`0x101a8580..0x101a85c9`).

**Unrecovered:** none.

## `0x101a8890` AllowInterrupt (123 bytes)

1. Spawnflag `0x20` → return (`0x101a889a`).
2. No target or no `+0x94` → `+0x5f90 := arg` (`0x101a88aa..0x101a88d7` → `0x101a8900`).
3. Latch 1, arg 0 → `0x1026d130`, latch 0, return (`0x101a88e7..0x101a88f4`); latch 0, arg 1 →
   `0x10007ea0` (`0x101a88fb`); then `+0x5f90 := arg` (`0x101a8900`).

**Unrecovered:** its caller's arm (`HandleAnimEvent 0x10274e30`, spec 0003).

## `0x101a9080` CCineAI PossessEntity (899 bytes)

`0x101a7880`'s order without the queue arm (a standing cine is overwritten), without the `m_hNextCine`
clear and without `DelayStart`; the warning block skips the "has not run" line (`0x101a90d9..0x101a9147`).
Switch (`0x101a9404`): 0/5 → state 1; 1/2/3 → 4/5/6; 4 → the teleport, state 1 and `RemoveFlag(FL_ONGROUND)`
(`0x101a9388..0x101a939b`); above 5 → `DevWarning(2, "aiscript:  invalid Move To Position value!")`
(`0x101a93a9`). Then `DevMsg(2, "\"%s\" found and used\n")` (`0x101a93c1`), read `m_NPCState` BEFORE
writing `scripted.cpp:0x554` and ideal 4 (`0x101a93c7..0x101a93e7`); a state that was already 4 →
`0x10280de0(0x2e)` (`0x101a93f8`). **Unrecovered:** the Troika slot-440 translation of `0x2e` (spec 0003).

## `0x101a9790` CCineAISchedule PossessEntity (233 bytes)

Target → `+0x94` or return (`0x101a979d..0x101a97d5`); `m_bRanAI` clear → the warning block without the
"has not run" line (`0x101a97e6..0x101a9854`); `m_interruptable` clear → `0x1026d130` (`0x101a9866`); slot
586 `(npc)` (`0x101a9870`). Writes nothing on either object itself. **Unrecovered:** none.

## `0x101a82d0` StartSequence (315 bytes) and `0x101a9510` CCineAI StartSequence (139 bytes)

`m_sequenceStarted (+0x5f91) := 1` FIRST (`0x101a82da` / `0x101a9517`). A null name with
`completeOnEmpty` → `SequenceDone(npc)` and answer 0 (`0x101a82f0..0x101a82f9`) — `CCineAI` answers 1
(`0x101a9532`); a null name without it continues with `""`. `LookupSequence` into `m_nSequence`; -1 →
`Warning("%s: unknown scripted sequence \"%s\"\n")` / `"...aiscripted..."` and sequence 0; `m_flCycle :=
0`; `ResetSequenceInfo`; the `CCineNPC` body's debug `Msg` tail (`0x101a8358..0x101a83fe`) is dead;
answer 1. **Unrecovered:** none (the port plays the clip by name — no studio header).

## `0x10278220` TryMoveToHiddenPosition (505 bytes)

1. Trace `threat → candidate + m_vecViewOffset (+0x184..0x18c)`, mask `0x2804091`, filter
   `CTraceFilterSimpleTwoEnt(this, param_3)` (`0x10278239..0x102782b8`); the `0x10738960` overlay is a
   developer line.
2. Fraction == 1.0 → FALSE (`0x102782f1..0x10278303`: `TEST AH,0x44 / JNP` jumps on C3 alone).
3. Slot 548 `IsValidCover(candidate, NULL)` false → FALSE (`0x10278310` / `0x10278318`).
4. `0x102e6d70` on `m_pMoveProbe` from slot 220 to the candidate, mask `0x202400b`, 100.0; non-zero
   `fStatus` → FALSE (`0x10278359` / `0x10278365`).
5. Goal `{type 4, candidate, activity 0x13, tolerance -1.0 (_DAT_104994a0), flags 0, target
   DAT_1090fdb4}` through `SetGoal(goal, 1)`; its `AL` is the answer (`0x102783ca..0x102783fd`). Flag 1
   (`0x102ecd74`) runs `0x102f28a0` first, which zeroes navigator `+0x40..+0x4c` and, through `0x1030bb30`,
   the path's tolerance `+0x28`: the -1.0 "keep" word therefore always resolves to the hull width here.

`SetGoal 0x102ecd20` writes the path's tolerance (`CAI_Path +0x28`, `0x102ecec7`), never
`m_flGoalTolerance (+0x6320)`. A refused route (`0x102f1dc0`) with navigator `+0x40
m_timePathRebuildMax == 0.0` (`0x102f1ee8`) calls `OnNavFailed(0xc)` (`0x102f1f00` → `0x102eeae0`):
`TaskFail(0xc)` inside `SetGoal`, whatever the caller then does with the FALSE. `+0x40` is written only by
`TASK_SET_ROUTE_SEARCH_TIME` (`0x102886f0`, from `StartTask 0x102827f0`) and zeroed by `0x102f28a0`; the
navigator constructor `0x102eca50` does not write it.

**Unrecovered:** `DAT_1090fdb4`'s identity (a null handle in practice).

## `0x102800c0` ScheduledMoveToGoalEntity / `0x102801e0` ScheduledFollowPath (213 bytes each)

`0x10280de0(schedule)`; `m_pGoalEnt (+0x5de8) := goal`; goal `{type 4 (0x102800ec; 0x102801e0: type 3
GOALTYPE_PATHCORNER, 0x10280205), goal->slot 217 origin (0x102801e0: slot 220), [4..7] -1 except [5] := param_3 = the MOVEMENT ACTIVITY (9 / 0x13 / 0x22 from
0x101a98c0) — not a goal type, tolerance 128.0 (0x102801e0: -1.0), flags 1, target DAT_10923a2c}`; slot 563
`(goal, &dest, &tolerance, &param_1)`; `SetGoal(goal, 0)`, whose `AL` is returned (the caller
`0x101a98c0` tests it). **Unrecovered:** `DAT_10923a2c` (zero-initialised; a null handle).

## `0x102aa640` / `0x102aa860` IssuePatrolMove (432 / 299 bytes)

`0x102aa640` (StartTask `0x7a..0x7e` on `+0x658c` / `+0x6594`): no cell or no path → `+0x1b48 = 0x3d39`,
`TaskFail(0x1d)`; node `nodes[current]` == -1 → `0x3d65`, `0x1d`; outside `network (+0x5d34)->+0x2c`'s
count → `++DAT_106c994c`; out of range or a null node → `0x3d60`, `0x1d`; else goal `{4, GetPosition(node,
m_eHull), activity -1, tolerance -1.0, flags 0}` through `SetGoal(goal, 2)`: TRUE → `TaskComplete(0)`, FALSE
→ `DevWarning(2, "%s can't reach patrol point\n")`, `0x3d55`, `TaskFail(0xc)`.

`0x102aa860` (RunTask `0x7a` / `0x7b`): no path → `0x3d73`, `TaskFail(0x1d)`; node -1 → return silently;
out of range → `++DAT_106c994c` and a NULL node into `GetPosition` (a retail fault; the port returns);
goal `{4, position, -1, NAI_Hull::Width(m_eHull), 0}` through `SetGoal(goal, 0)`, answer ignored — but a
refused route still fails the task inside `SetGoal` (`OnNavFailed(0xc)`, above).
**Unrecovered:** the AI network (0018 story 4); the port stands a node id as a patrol hint's entity index.

## `0x1037c1c0` CNPC_VGhoulCroucher::ScriptHide (232 bytes)

Scope-trace push keyed on `m_iName` (`0x1037c1c5..0x1037c227`); `CAI_BaseNPCTroika::ScriptHide
0x102c1ce0` direct (`0x1037c229`); `m_hBurningParticle (+0x6670)` live (`0x1037c237..0x1037c25e`) →
slot 77 on the particle (`0x1037c286`); the re-validation's null-receiver arm (`0x1037c295`) is dead; the
handle is not cleared. **Unrecovered:** none.
