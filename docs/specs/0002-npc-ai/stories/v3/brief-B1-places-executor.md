# Brief B1 — V3b: the ambient executor deleted; a place is held by its program and released by `0x102b53d0`

Read `README.md` here (§1 "places", §2 M5–M8, §4 V3b) and `packets.md` (R2 item 4,
`InputUseInteresting 0x102c2a70`). Retail's walk: `docs/vtmb/npc-ai/programs.md` § "Interesting
places: the selector, the programs, the wait" and § "Interesting-place eligibility";
`npc-ai/lifecycle.md` § "`CAI_BaseNPCTroika::UpdateOnRemove` — `0x1028d6e0`" (lines ~1834-1858, the
release `0x102b53d0`); `npc-ai/schedule-kernel.md` § "`TaskFail` and stopped special navigation"
(~358). Runs after V3a. You never build.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp`, `ElysiumNpc.h`
- `Source/ElysiumUE/Public/ElysiumSaveTypes.h` (one schema-version step)

## The job

1. **Delete the executor** (findings B rows 21–23): `ThinkAmbient` (`ElysiumNpc.cpp:2132-2240`),
   `BeginAmbientUse` (`2040-2076`), `BeginAmbientLeave` (`2078-2089`), `PlayAmbientActivity`
   (`1235-1288`, if no retail arm calls it — check `ElysiumNpcHints.cpp`, which uses
   `RestartIdealActivityId`), `ThinkAutonomous` (`993-1007`), `ThinkSchedulePolicy` (`961-991`),
   `FailedSpotIndices`, `AmbientLeaveAt`, `AmbientActivityCycle`, the `AmbientOwner` token
   (`ElysiumNpc.h:1161, 1174-1181`), and `EAmbientPhase::Moving` (the retail phases of `+0x6304` are
   0 none, 1 into, 2 idle, 3 outof: `ElysiumNpcHints.cpp:656, 670, 719, 803`). `RouteScheduleMaintenance`
   (`885-915`) keeps only `TickScriptWatchdog`, `ThinkInDialog`, `ThinkScriptOwned` (V3c/V3d's) and
   then calls `ThinkStanceOrIdle`; rewrite its STORY8-TWIN comment (the place survivor is gone).
2. **`ClaimAmbientSpot`** (`1121-1175`) is the port of `PickRandomInterestingPlace 0x102db590` +
   eligibility `0x102dad60` + `PickSpotFor 0x102da0d0` (claim 1) that `TASK_FIND_INTERESTING_PLACE`
   (`ElysiumNpcStartTask.cpp:1537-1554`) calls. Delete the `Mind.Acquire(Ambient)` and the
   `FailedSpotIndices` filter: "the pick keeps no memory of a failed place" (`programs.md:215`). Keep
   the name (B2 and the task arm call it); its comment names the three addresses.
3. **`FinishAmbientUse(bool bFireLeft)`** (`2091-2130`) becomes `0x102b53d0`'s port and nothing
   else: drop the `bStopMovement` parameter and the motor stop, the `Mind.Release`, the
   `bMoveIssued` write, `ReleaseAnimSegment` and `ResetAnimToIdle` (the executor's pose management).
   Keep what the walk lists (the visitor record, the claim release, `+0x62e8` / `+0x62ec`, the
   `INTERESTING_INTO` / `INTERESTING_LOST` bits) and port any arm of `0x102b53d0` the doc walks that
   the body lacks; an arm the doc does not hold stays out with an **Unrecovered** line. Update every
   caller in your files to the one-argument form (`573`, `606`, `1598`, `1770`, `2337`, `2525`; the
   executor's go with it). B2 updates `ElysiumNpcMaintain.cpp:166`, `ElysiumNpcRunTask.cpp:1107`,
   `ElysiumNpcSaveRestore10.cpp:67`.
4. **The release only where retail releases** (red 6): `ThinkSchedulePolicy`'s release goes with
   it; `InputUseInteresting` (`315-324`) writes `bUseInteresting` and nothing else
   (`0x102c2a70`, packet R2); `TaskFail`'s (`1770`) is retail's step 1 and stays. The pre-claim
   releases at `573`, `606`, `1598`, `2337` stay for now: they precede claims V3c and V3d delete.
5. **The external return**: delete the latch at `ScheduleDone` (`1829`); B2 deletes its member and
   readers.
6. **Save**: `SerializePatrolBlock` (`2823-2833`) drops `AmbientLeaveAt` and `AmbientActivityCycle`
   (port timers) and keeps `+0x6304`, `+0x62ec`, `+0x63d4`, `+0x62e8`; `RestorePatrolAndAmbient`
   (`2705-2723`) loses the `Moving` case; `RestoreMindState` (`2727-2756`) no longer restores an
   `Ambient` owner (restore `None`; the owner byte itself goes in V3d). One step of the save schema in
   `Public/ElysiumSaveTypes.h` (saves are disposable: no migration).
7. Debug: the executor's fields in `GetDebugState` (`2933-`) go.

## Not yours

`ElysiumNpcMaintain.*`, `ElysiumSchedule.*`, `ElysiumNpcRunTask.cpp`, `ElysiumNpcSaveRestore10.cpp`,
`ElysiumNpcHints.cpp`, the select (`ElysiumNpcSelect.cpp`, already retail). The beat, dialogue and
order claims (V3c/V3d). No test file (B2's).

## Rules

README § "Rules for every agent of V3". The query budget: 10 s warns, 60 s stops; never read a file
over ~200 KB whole (read `ElysiumNpc.cpp` by ranges). Text through Grep / Read / Glob. No sleep, no
polling loop. Do not build, do not commit.

## Report (≤300 words)

Per change, the retail address; what `0x102b53d0` does that the port now does and what stays
unrecovered; every line B2 needs; anything that still holds a place outside a program.
