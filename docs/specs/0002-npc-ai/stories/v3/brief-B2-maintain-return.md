# Brief B2 — V3b: `MaintainSchedule` reselects in the same pass; the release callers; the arm test

Read `README.md` here (§1 "what ends a program", §2 M8, §4 V3b, §6) and
`docs/vtmb/npc-ai/schedule-kernel.md` § "Maintain19 completion" and the `MaintainSchedule
0x102817c0` walk (`research section 0x102817c0`). Runs beside B1. You never build.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcMaintain.cpp`, `ElysiumNpcMaintain.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumSchedule.cpp`, `ElysiumSchedule.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcRunTask.cpp` (line 1107 only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSaveRestore10.cpp` (line 67 only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask.cpp` (the `0x102a1f23` arm's comment,
  `1539-1551`, only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelMaintainTests.cpp`

## The job

1. **The external-executor return** (findings B row 26): retail reselects inside the same pass
   (`0x10281be5`, `0x10281c46`). Delete `TakeExternalExecutorReturn` from the runner interface
   (`ElysiumSchedule.h:260`) and its three readers with their `Install(None)` adapters
   (`ElysiumSchedule.cpp:297-304, 385-392, 421-425`); the member and its accessor
   (`ElysiumNpcMaintain.inl:38, 45`; `ElysiumNpcMaintain.cpp:464-469`); the `use_interesting` None
   adapter in `SelectScheduleForMaintenance` (`ElysiumNpcMaintain.cpp:348-356`) — the Troika
   selector answers `0xff`/`0x100`/… for such an NPC (`ElysiumNpcSelect.cpp:592-612`), so the adapter
   has nothing left to stand for. The remaining flow at each site must be exactly retail's: verify it
   against the listing addresses already cited on those lines.
2. **`FinishAmbientUse(bool bFireLeft)`** (B1 drops `bStopMovement`): update the callers in your
   files — `ElysiumNpcMaintain.cpp:166` (`0x102a09a0`, `OnScheduleChange` under `!PRESERVE_PATH`),
   `ElysiumNpcRunTask.cpp:1107`, `ElysiumNpcSaveRestore10.cpp:67`. Each is a retail caller of
   `0x102b53d0`: say at the line which argument retail passes there.
3. **The `0x102a1f23` comment** (`ElysiumNpcStartTask.cpp:1539-1551`): it describes
   `ClaimAmbientSpot` as taking an arbiter claim; rewrite to the three retail addresses
   (`0x102db590`, `0x102dad60`, `0x102da0d0`). No code change there.
4. **Tests**: delete the `TakeExternalExecutorReturn` override of the fake runner
   (`ElysiumNpcKernelMaintainTests.cpp:164`). Add one arm test (first check that none exists —
   `ElysiumNpcCrosswalkTests.cpp` and `ElysiumNpcKernelMaintainTests.cpp`): `OnScheduleChange
   0x102a0940` releases the held place at `0x102a09a0` when `PRESERVE_PATH` is clear, and keeps it
   when `PRESERVE_PATH` is set (the crosswalk wait `0x100 → 0x102 → 0x100` depends on the second
   half). Name it `Elysium.Arm.NpcKernelMaintain.PlaceReleaseUnderPreservePath_0x102a0940`.

## Not yours

`ElysiumNpc.{h,cpp}` and the save schema (B1). The select, the interest arms, `ElysiumNpcHints.cpp`
(retail, untouched). No record.

## Rules

README § "Rules for every agent of V3". The query budget: 10 s warns, 60 s stops; never read a file
over ~200 KB whole. Text through Grep / Read / Glob. No sleep, no polling loop. Do not build, do not
commit.

## Report (≤300 words)

Per change, the retail address; the three reselect sites as they now read; the test added (name,
what it pins); any caller of the deleted interface outside your files (for the integrator).
