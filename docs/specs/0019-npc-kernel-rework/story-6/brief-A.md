# Lane A · the Debug families

Read `README.md` in this directory first. You own, and only you may edit (all under
`Source/ElysiumUE/Private/` unless stated):

- `Substrate/*Debug*` — every file whose name contains `Debug` (16 files: `ElysiumNpcBaseDebug.cpp/.inl`,
  `BaseDebug2.cpp`, `BaseDebug10.cpp/.inl`, `BaseDebug10_2.cpp`, `ElysiumNpcDebug.cpp/.inl`, `Debug2.cpp`,
  `Debug10.cpp/.inl`, `Debug10_2.cpp`, `DebugShared.h`, `Debug10Shared.h`, `Debug10_2Shared.h`, `Debug2Shared.h`)
- `Substrate/ElysiumNpcBaseMotor.cpp`, `ElysiumNpcBaseRunTask.cpp`, `ElysiumNpcStartTask_2.cpp`,
  `ElysiumNpcRunAiSpecies.cpp`, `ElysiumNpcMisc2Species.cpp`, `ElysiumNpcBaseBoss.cpp`,
  `ElysiumNpcBaseStartTask.cpp`, `ElysiumNpcRunTaskSpecies.cpp`, `ElysiumNpcRunTask.cpp`,
  `ElysiumNpcThink.cpp`, `ElysiumNpcThinkSpecies.cpp`, `ElysiumNpcConditions10.cpp`,
  `ElysiumNpcCombat10.cpp`, `ElysiumNpcKernelBaseHelpers.cpp/.inl/.h` (`ElysiumNpcKernelBaseHelpersShared.h` too),
  `ElysiumNpcGeometry.cpp`, `ElysiumNpcBaseLifecycle.cpp`
- `Tests/ElysiumNpcKernelDebugTests.cpp`, `Tests/ElysiumNpcKernelDebug10Tests.cpp`, and the test
  files of the Substrate files above **only for the tests that exercise what you delete**.
- Any `*.h` under `Substrate/` or `Private/Debug/` whose only content is declarations of what you delete.
- The module's `.Build.cs` is not yours; deleting a `.cpp` needs no edit there.

Not yours (another lane this wave): `*Select*`, `ElysiumNpcBaseDamage.inl`, `ElysiumNpcCop.*`,
every other species file, `ElysiumNpcSchedule.cpp`, `ElysiumNpcClosure.cpp`, `ElysiumAnimatingSlotBodies.cpp`.
If a debug call site sits in one of those, list it under "Needs another owner" with file:line.

## What the survey found (verify, then act)

- No Debug file is wholly dead by ledger count: each cites 4–13 `dead` retail addresses; the
  rest of its functions are port-only helpers with no ledger row that exist to serve those
  dead bodies. The whole family goes: `docs/vtmb/npc-kernel/delete-list.md` "Why" column
  says "172 debug and diagnostics (the overlays, the ring, the trace formatters, the stamps,
  slots 451 / 546 / 409 name lookups)".
- Live (`rule`) callers into the Debug files, which must be cut or re-homed first:
  - `EmitDevMsg` / `EmitDebugMsg` (`BaseDebug.cpp:45/51`) from `BaseMotor.cpp:1205/1250`,
    `BaseRunTask.cpp:522/711/886`, `StartTask_2.cpp:887/1297/1416`, `RunAiSpecies.cpp:626`,
    `Misc2Species.cpp:204/333`, `BaseBoss.cpp:65`. These are retail `DevMsg` prints: delete the
    print statement, keep the rule around it byte for byte otherwise.
  - `TaskName` (a `dead` row) from `BaseRunTask.cpp:885`, `BaseStartTask.cpp:1896`,
    `RunTaskSpecies.cpp:1949` — inside prints; the print goes.
  - `TroikaNPCThinkDebugPre` (`0x10292500`, dead) from `Think.cpp:424`; `DumpDebugLogRing` from
    Camera's `NPCThink` (`ThinkSpecies.cpp:135-138`). The ring (`AppendDebugLogLine 0x1027ef20`)
    has no storage and only forwards to `EmitDevMsg`; the dump byte `+0x5b55` is absent.
  - Helpers that are NOT debug and are used by rule bodies for a value — **move, do not delete**:
    `RetailBonePosition` (→ `ElysiumNpcGeometry.cpp`, declared where Geometry's other helpers
    are; callers `RunTask.cpp:935`, `StartTask_2.cpp:591`, `Misc2Species.cpp:532`),
    `NavNearestNodeToNpc` (→ `ElysiumNpcKernelBaseHelpers`; caller `BaseStartTask.cpp:2084`),
    `SequenceDescriptor` (`RunTask.cpp:879` — decide: if it only feeds a print, the print goes
    and it is dead; if it feeds a value, move it to BaseHelpers).
    `RetailFieldOfViewDot` (caller `BaseSelect.cpp:255`) and `PlayerHeightenedAlert` /
    `PlayerCopsInPursuitCount` (callers `Cop.cpp`, `SelectSpecies.cpp`) are lane B's to move; you
    delete their definitions from the Debug files only after confirming lane B's brief names
    them (it does) — leave a one-line note in your report that B carries the new definitions.
    **Coordination rule:** since B moves them into files it owns, you delete the Debug
    definitions; the orchestrator's build joins the two.
  - `DebugTraceByte` read at `Conditions10.cpp:718` inside the dead `BuildConditionDebugString`: both go.
  - `MotorResetToDefault` (`BaseLifecycle.cpp`, dead) is called by `NavigatorMoveStep`
    (`BaseMotor.cpp:1187`). Read retail `0x102efe30`-area / the navigator's `Move` to decide: if
    retail's navigator calls it and it writes motor words a later rule reads, it is a live
    observer → report for re-verdict, do not delete. If it only resets words nothing reads, delete
    body and call.
- `ent_trace_conditions` ships `"1"` (a story-4 ConVar) and gates a debug trace with no output
  device: the tunable row stays (it is data); the consumer print goes.
- Combat10: `SecureUnhashLevel(0x3cf445af)` at `Combat10.cpp:413` inside the rule
  `PreSelectIdealState` — the SafeDisc scrambler over a plain integer. Replace with the literal
  `2` and a comment `// retail: CSecureType<int> unhash of 0x3cf445af == 2 (SafeDisc), stored plain`.
  Keep the existing pin test (`Combat10Tests.cpp:712`) if it still compiles against the literal;
  delete the scrambler helper and its agreement test only if nothing else uses it (ManBat's hint
  word still uses the scramble at `StartTaskSpecies.cpp:1611` and three readers — **not yours,
  leave it**; report it).

## Job

1. Cut every live call into the Debug files as above (prints deleted; value helpers moved).
2. Delete the 16 Debug files and the two Debug test files, plus any header that only declared them.
3. Delete tests elsewhere that exist only to exercise a deleted debug body.
4. Confirm with `rg` that no file under `Source/` still references a deleted symbol or includes a
   deleted header. Do not build.
5. `targets-A.tsv`: every `dead` address whose body you removed → `-`. Get the addresses from
   `delete-list.md` (the "Port sites" column names your files) and from `// 0x1027ef20`-style
   citations in the files you delete.

## Deliver

`report-A.md` (≤300 words): files deleted, prints cut (count by file), helpers moved (from → to),
rows refused with the retail reason, "Needs another owner" list, and the `targets-A.tsv` row count.
