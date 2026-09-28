# StartTask19 — slot 442 `StartTask`, base, Troika and species (story 8, pass I)

Walked prose for the 26 `rule` rows of family StartTask19 (plus `CNPC_VCop::StartTask 0x10371b70`
from Damaged19). The bodies are walked in four part files, one per porter lane; this file indexes
them and records what the integration recovered across the lanes. Source of truth: the packet
`$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/families-19-29/StartTask19-READING.md` and the
listings (`vtmb_asm`). Pass C folds these sections into `schedule-kernel.md`.

| part | rows | port | tests |
|---|---|---|---|
| [StartTask19-Base.md](StartTask19-Base.md) | `0x102827f0 CAI_BaseNPC::StartTask` (107 arm-table entries) | `ElysiumNpcBaseStartTask19.cpp` | `NpcKernelStartTask19.Base.*` |
| [StartTask19-TroikaA.md](StartTask19-TroikaA.md) | `0x102a1910 CAI_BaseNPCTroika::StartTask`: prologue, arms `[0x102a1943, 0x102a5046)` | `ElysiumNpcStartTask19.cpp` | `NpcKernelStartTask19.*` |
| [StartTask19-TroikaB.md](StartTask19-TroikaB.md) | `0x102a1910`: arms `[0x102a5046, 0x102a77f7]`, the two shared tails, the base forward | `ElysiumNpcStartTask19_2.cpp` | `NpcKernelStartTask19.TroikaTail.*` |
| [StartTask19-Species.md](StartTask19-Species.md) | the 25 species overrides, `CNPC_VCop 0x10371b70`, FrenzyShadow `0x10375f50` | `ElysiumNpcStartTask19Species.cpp`, `ElysiumNpcFrenzyShadow.cpp` | `NpcKernelStartTask19.Species.*` |

## The dispatch, checked against the image

- Base `0x102827f0`: `iTask - 1` into the byte table `0x10287138` (0x120 entries), then the arm
  table `0x10286f8c` (107 entries). Arms `0x00..0x69` take 153 ids; entry `0x6a` (`0x10286f63`,
  `DevMsg("No StartTask entry for %s\n")`, the task left running) takes the other 166 and every id
  past the table (`0x1028280e JA`).
- Troika `0x102a1910`: `id - 5 > 0x144` goes to the base (`0x102a77e2`); otherwise the byte table
  `0x102a7ab8` (index `id - 5`) into the arm table `0x102a77f8` (176 entries). 74 ids reach the 68
  arms below `0x102a5046` (the first switch); 115 reach arms at or past it (`StartTaskTroikaTail`),
  among them the shared tails `0x102a66d7` (ids `0x8b`, `0x125`: complete) and `0x102a77ea` (ids
  `0x05`, `0xea`, `0xeb`: left running); the remaining 136 in-range ids take `0x102a77e2`, the base
  forward, which is `StartTaskTroikaTail`'s `default:`. The two switches are disjoint and together
  complete.

## `0x102ecd20` CAI_Navigator::SetGoal (one body)

Every StartTask arm that routes, and family Script19's builders, reach one port body
(`FElysiumNpcBase::StartTaskSetGoal`); the Troika halves and Script19 only convert their goal
literals into its record.

1. Nav `+0x8` := the owner's PATHING hull `+0x156c`, `+0xc` := curtime (`0x102ecd2c`), then navigator
   slot 7 (`0x102eea70`, the reset).
2. SetGoal flag 1 → `0x102f28a0` (below). Else flag 2 → the path's target handle and dest words
   reset (`path+0x30..+0x3c`).
3. Goal `[5]` (the MOVEMENT activity) `!= -1` → `0x102ee250` (`path+0x2c`).
4. The tolerance, `path+0x28`: `[8] == -2.0` (`_DAT_1049d980`) the pathing hull's width
   (`0x102d61b0`: `row+0x18 - row+0xc`); `[8] != -1.0` the literal; `[8] == -1.0` keeps the path's
   own unless it is `0.0`, when the hull width is written and, for an entity goal (type 1
   `m_hTargetEnt`, 2 `GetEnemy`, 7 `GetBestSeeUnknown`) whose entity is an NPC, averaged with that
   NPC's `m_eHull` width (`* 0.5`, `0x102ecebd`).
5. Path `+0x40` := hull * 0.5; the arrival-direction triple `[11..13]`; path `+0x8`/`+0x4` := `[14]`/`[15]`.
6. Goal flag 2 → a node route (`0x102f3c10` / `0x102f41b0` / `0x102fd240`) and return, bypassing
   `0x102f1dc0`.
7. Path goal type := `[0]`, `path+0x60` := `[9]`; the target entity by type; the dest words when
   `[1..3]` is not the default triple `0x1093404c..54`, else the node `[4]` (`0x102ee9c0`).
8. `0x102f1dc0` (the route build, below). Refused: flag 4 → `0x102f28a0`; return false. Built: goal
   flag 1 → `0x102e0b40` + `0x102e2020` toward the path goal; `0x102f13d0(this, 1)`; arrival
   activity `[6]`, else arrival sequence `[7]`, else arrival activity 1.

**`0x102f1dc0`** — with `m_afMemory` bit `0x20` clear: a built route (`0x102f2330`) clears the bit and,
unless slot 529 `IsCurTaskContinuousMove` answers true, completes the task through the navigator's
slot 2 (`0x102623c0` → `TaskComplete(false)`), answering true; a refused one fails the task
`OnNavFailed(0xc, 1)` (`0x102f1f00`) when nav `+0x40` is `0.0`, else sets bit `0x20`, `+0x4c :=
curtime + +0x44`, `+0x48 := curtime + +0x40`, and answers false. With the bit set: past `+0x48`
→ `OnNavFailed(0xc, 1)`; past `+0x4c` → rebuild, whose success clears the bit and completes the
task unless the current task is `0x6e` (`0x1028a150`); else false.

**`0x102f28a0`** — nav `+0x40`, `+0x44`, `+0x48`, `+0x4c` := 0, `m_afMemory &= ~0x20`, then the path
reset `0x1030bb30` (its tolerance `+0x28` among it). **`0x102886f0`** (`TASK_SET_ROUTE_SEARCH_TIME`)
is the only writer of `+0x40`; nothing but `0x102f28a0` writes `+0x44`.

`path+0x28` is written also by `0x102ee1c0` (base `TASK_SET_TOLERANCE_DISTANCE` `0x10286c84`,
`TASK_SET_MELEE_TOLERANCE_DISTANCE` `0x10286cd4`, the Troika tolerance tails, the species
`0x9f` arms); it is not `m_flGoalTolerance` (`+0x6320`), which only the Troika/species arms write
themselves. `path+0x20` is written by `0x102f2fe0` (the tolerance tails, the LKP chase arms).

## Helpers the lanes shared

- `0x10289ee0` RestartIdealActivity: `m_Activity (+0xfec) == act` → `m_Activity = 0`
  (`0x10289eee`); then `SetIdealActivity(act)` (`0x10289efc` → `0x10272650`).
- `0x1028a150` GetCurTask: the running schedule's task at `m_iScheduleIndex`; NULL with no schedule.
  The port's steps carry retail's global task ids, so the running step answers it.
- `0x102dfed0` CAI_Enemies::GetLastKnownPosition: the entity's record `+0xc`; else the LAST record
  flagged `+0x34` with `DevWarning(2, "Asking LastKnownPosition for enemy (%s) that's not in my memory
  (using danger pos)!!\n")` (`0x1060e248`); else `vec3_origin` (`DAT_1070d1b0`, zeroed by
  `0x101370b0`) with `DevWarning(2, "Asking LastKnownPosition for enemy (%s) that's not in my
  memory!!\n")` (`0x1060e1f8`). Before either warning it notifies `this+0` vtable `+0xe8` or
  `0x10316bc0(this+4)` by `this+8` (unrecovered target).
- `0x102d1180` (the hint's LOS endpoint): `m_nNodeID (+0x5e4) == -1` → the hint's `GetAbsOrigin`;
  else the network node's position `0x102f46d0(DAT_1093407c, ...)`.
- `0x10278220` TestLateralCover and `0x102784a0` FindLateralCover are `CAI_BaseNPC`'s, one port body
  each (`StartTaskTestLateralCover` / `StartTaskFindLateralCover`), reached by the base arm `0x48`,
  the Troika arms `0x83..0x85`, and `FindCoverFromEnemy`.

**Unrecovered:** `0x102f13d0`; `0x1030b550` / `0x1030b5b0` (arrival activity / sequence); the path
byte `+0x10` (`0x102ee2c0` clears it, `0x102ee2e0` reads it); the BSS goal defaults `0x1093404c..54`,
`0x10923a30`, `0x10934060..68`; `0x102e0290`'s second vector (read by the target-lead query as a
velocity, answered as a position by the Troika arms); the `0x102dfed0` miss notification target;
the node network (`0x102f3c10`, `0x102f41b0`, `0x102fd240`, `0x102f46d0`, `0x102ee9c0`).
