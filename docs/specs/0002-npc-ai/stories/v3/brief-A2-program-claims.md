# Brief A2 — V3a: the navigator never refuses for a claim; the two `IsGoalActive` readers

Read `README.md` here (§1, §2 M3, M17, §4 V3a, §6). Runs beside A1. You never build.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseStartTask.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcScript.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBase.h`, `ElysiumNpcBase.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcMaintain.cpp` — lines 175-176 only
- `Source/ElysiumUE/Private/Tests/ElysiumNpcCombatTests.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelSelectTests.cpp`

## The job

1. **`SetGoal 0x102ecd20`, `SetRandomGoal 0x102ed430`, `AdvancePath 0x102f0400` fail only on the
   route** (findings B rows 9–10). Delete the claim and the `bScriptedOrderHolds` bypass at
   `ElysiumNpcBaseStartTask.cpp:2181-2229` and `:2537-2538`, and `ElysiumNpcScript.cpp:627-640`
   (the claim and the "a refused claim is not a route failure" arm). The listing's own failure arms
   stay exactly as they are. The `AcquireScheduleBody` declaration goes with A1; you only stop calling it.
2. **`bMoveIssued`'s readers that stand for `IsGoalActive 0x102ee6a0`** (findings B row 28): at
   `ElysiumNpcStartTask.cpp:1827` (`TaskWalkRunPath 0x102a4b4e`'s activity choice) and
   `ElysiumNpcBaseStartTask.cpp:2363`, read the listing at the cited address; where retail reads the
   navigator (`nav+0x14`, the head waypoint, `0x102f2ea0`), read `NavIsGoalActive()`
   (`ElysiumNpcBaseMotor.cpp:101`) or the navigator word the listing names. Where the port uses the
   member as the motor's own "a leg was issued" bookkeeping (the dedupe at
   `ElysiumNpcScript.cpp:627`, the `NavIssueLeg` writes), leave it. Rewrite the member's comment at
   `ElysiumNpcBase.h:652`: motor bookkeeping, no retail word, not to be read for a retail decision.
3. **`bWalkingAnimation`**: delete the member (`ElysiumNpcBase.h:681`) and its writes in your files
   (`ElysiumNpcBase.cpp:344`, `ElysiumNpcMaintain.cpp:176`). A1 deletes `ElysiumNpc.cpp`'s.
4. **Tests** (decision: port-only tests deleted, not converted): delete `NpcCombat.Chase` and
   `NpcCombat.Death` (`ElysiumNpcCombatTests.cpp`; owner assertions at `835`, `867`, `1742`;
   findings F tags both `[port]`) — if either also pins a retail address no arena record covers,
   delete only its owner assertions and say so. In
   `NpcKernelSelect19.PatrolOutranksUseInteresting_0x102af6b6` delete only the owner assertions
   (`ElysiumNpcKernelSelectTests.cpp:512, 520`); the selection it pins is retail's.

## Not yours

`ElysiumNpc.{h,cpp}` and `ElysiumNpcAnim.cpp` (A1). The rest of `ElysiumNpcMaintain.cpp` (V3b's).
No record.

## Rules

README § "Rules for every agent of V3". The query budget: 10 s warns, 60 s stops; never read a file
over ~200 KB whole (read `ElysiumNpcBaseStartTask.cpp` by ranges). Text through Grep / Read / Glob.
No sleep, no polling loop. Do not build, do not commit.

## Report (≤300 words)

Per file: what changed and the retail address at each change; for each `bMoveIssued` reader, which
retail word it now reads or why it stays; the tests deleted (names) and the assertions cut (lines).
