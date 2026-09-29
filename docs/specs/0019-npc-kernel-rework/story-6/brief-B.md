# Lane B · the stamp words and the Select files

Read `README.md` in this directory first. You own, and only you may edit (under
`Source/ElysiumUE/Private/`):

- `Substrate/ElysiumNpcSelect.cpp`, `ElysiumNpcSelectSpecies.cpp`, `ElysiumNpcBaseSelect.cpp`,
  `ElysiumNpcBaseSelect.inl`, and any other `Substrate/*Select*` file
- `Substrate/ElysiumNpcBaseDamage.inl`
- `Substrate/ElysiumNpcCop.cpp`, `ElysiumNpcCop.h`
- `Tests/*Select*Tests.cpp`, `Tests/ElysiumNpcKernelCopTests.cpp` (or wherever Cop's tests are)

Not yours: any other species file (lane C owns them and deletes their
`SelectIdealStateSelector = …` lines), the Debug files (lane A deletes them; A also deletes the
Debug-file definitions of the helpers you re-home below), the generated `ElysiumNpcKernelShapeMap.cpp`.

## What the survey found (verify, then act)

- Two debug stamp words, verdicted `dead` by story 1 ("the ring and stamps and their words"),
  with no SAVE row (`docs/vtmb/npc-kernel/fields.md:1376,1379`):
  - `SelectScheduleSelector` (`+0x1b2c`, declared `BaseSelect.inl:18`), written by `SelectTrace`
    (`BaseSelect.cpp:72`) from about 290 sites across `Select.cpp`, `SelectSpecies.cpp`,
    `BaseSelect.cpp`. Read by nothing but the schedule-trace print.
  - `SelectIdealStateSelector` (`+0x1b38`, declared `BaseDamage.inl:8`), written by ~20 species
    `SelectIdealState` bodies (lane C cuts those lines) and by bodies in your files.
  - `m_TaskFailTrace` (`+0x1b44`) is already absent.
- Value helpers living in the Debug files that your files call and must keep:
  `RetailFieldOfViewDot` (called `BaseSelect.cpp:255`) → define it in `ElysiumNpcBaseSelect.cpp`
  (or its `.inl`), same body, same name; `PlayerHeightenedAlert`, `PlayerCopsInPursuitCount`
  (called `Cop.cpp:250/255`, `SelectSpecies.cpp:591`) → define them in `ElysiumNpcCop.cpp`,
  declared in `ElysiumNpcCop.h`. Copy the bodies verbatim from the Debug file
  (`rg -n "RetailFieldOfViewDot|PlayerHeightenedAlert|PlayerCopsInPursuitCount" Source/ElysiumUE/Private/Substrate/*Debug*`)
  before lane A deletes them (A runs concurrently; read now).
- `EmitDevMsg` / `EmitDebugMsg` prints in your files: delete the print line (retail `DevMsg`
  with no output device), keep the rule around it.

## Job

1. Delete `SelectTrace` and every call to it (mechanical: `ast-grep` or `sd`; the call is a
   statement — remove the statement, never the surrounding branch). Where `SelectTrace` was the
   only statement in a branch, the branch's retail behaviour was the schedule choice, not the
   stamp: keep the branch structure intact (an empty branch is fine if the else-chain depends on it).
2. Delete the two word declarations and every write to them in your files.
3. Re-home the three value helpers.
4. Delete the tests that assert a stamp value or a `SelectTrace` call; keep every test that
   asserts the schedule chosen.
5. `rg` that no file under `Source/` still names `SelectTrace`, `SelectScheduleSelector`,
   `SelectIdealStateSelector` **except** in species files (lane C's) and the generated shape map.
   List what remains under "Needs another owner" with file:line so the orchestrator can check
   lane C covered it.
6. `targets-B.tsv`: the `dead` addresses whose bodies you removed → `-` (from `delete-list.md`,
   rows whose "Port sites" name your files, plus `0x1027efb0`-style citations you delete).

## Deliver

`report-B.md` (≤300 words): sites removed (count by file), helpers re-homed, tests deleted /
kept (counts), the residual list for lane C, `targets-B.tsv` row count.
