# Lane C · the species and schedule residue

Read `README.md` in this directory first. You own, and only you may edit (under
`Source/ElysiumUE/Private/`):

- Every species file `Substrate/ElysiumNpc<Name>.cpp/.h` **except** `ElysiumNpcCop.*` (lane B) and
  **except** the motor / navigator bodies inside `ElysiumNpcWerewolf.cpp` and `ElysiumNpcMingXiao*.cpp`
  (those are `mechanism` rows for wave 3; you touch only their `dead` rows).
  Species files are the retail-class files: `Bach`, `Camera`, `ChangBros`, `Gargoyle`, `Hengeyokai`,
  `Hunter`, `MingXiao`, `MingXiaoTentacle`, `Newscaster`, `PlayerController`, `SabbatLeader`,
  `SheriffMan`, `Sounds`, `Sounds10`, `Tzimisce`, `TzimisceRunner`, `TzimisceHeadClaw`, `Werewolf`,
  `Werewolf2Species`, `WolfMorph`, `Yukie`, `Zombie`, `FrenzyShadow`, and the rest.
- `Substrate/ElysiumNpcSchedule.cpp`, `ElysiumNpcBasePrecache10.cpp`, `ElysiumNpcScheduleHost.cpp/.h`,
  `ElysiumNpcClosure.cpp`, `ElysiumNpcBaseHelpers2.cpp`, `ElysiumAnimatingSlotBodies.cpp`,
  `ElysiumCombatCharacterSlotBodies.cpp`, `ElysiumScriptedSequence.h`, `ElysiumNpcActivityTables.cpp`,
  `ElysiumNpcBaseSquad.cpp`, `ElysiumNpcBaseMisc.cpp` (dead rows only), `ElysiumNpcBaseBoss.cpp` is lane A's.
- Their tests under `Tests/`, only for the tests that exercise what you delete.

Not yours: `*Debug*` (A), `*Select*` / `BaseDamage.inl` / `Cop.*` (B), `BaseMotor*`, `Motor*`,
`Geometry.cpp`, `Conditions10.cpp`, `Combat10.cpp`, `Think*.cpp`, `RunTask*`, `StartTask*`,
`BaseLifecycle.cpp`, `KernelBaseHelpers*` (A), generated files.

## The list

Your rows are every standing `dead` row in `docs/vtmb/npc-kernel/delete-list.md` whose "Port
sites" column names one of your files. Build the list first:

```
rg -n '^\| `0x' docs/vtmb/npc-kernel/delete-list.md | rg -v '\| - \|' > /tmp/standing.txt   # then filter by your files
```

(use `$CLAUDE_JOB_DIR/tmp` or your own scratch, not the repo). The survey counts by file:
Schedule 13–16, Werewolf 9–15 (+.h 5), Camera 9–14, Sounds10 11–12, Tzimisce 5–10,
ScheduleHost 7 (+.h 5), MingXiao 9, Closure 9, Newscaster 8, BaseHelpers2 8, MingXiaoTentacle 7,
Hengeyokai 7, Zombie 6, AnimatingSlotBodies 11, SabbatLeader 5, Hunter 5, BaseSquad 5, and a tail.

What they are (survey sample; verify each row's "Why" before deleting):

- **Schedule.cpp:** slot 452 `LoadedSchedules` constants (`true`); story 3's corpus load error
  stands in for it. Its one live reader is the base `Precache` (`BasePrecache10.cpp:31`, a
  `mechanism` row): remove the read there so the slot can go (Precache's own body is wave 3's,
  touch only that line).
- **Camera, Sounds10:** empty overrides of slots with no dispatch site (496, 501, 506) and
  undispatched `KeyValue` slots 108/109.
- **Tzimisce:** `SquadSlotName`, `GetEventName` name lookups (slots 451 / 546 / 409 family).
- **Werewolf:** debug name lookups and hull / bbox overlay draws.
- **Twelve "stamp-only" overrides** whose whole content is a debug stamp around the body the
  class would inherit anyway (e.g. `ScheduleHost.cpp:101-107`, `Hengeyokai.cpp:248`): delete
  the override; the class inherits.
- **Every species `SelectIdealStateSelector = …` line** (about 20, e.g. `Bach.cpp:216`,
  `Cop.cpp:157` is B's): the word is going (lane B deletes its declaration). Delete the assignment
  line; the `SelectIdealState` body around it is a rule and stays.
- **`EmitDevMsg` / `EmitDebugMsg` prints** in your files: delete the print line, keep the rule.
- **Standoff bodies** (`CAI_StandoffBehavior` / `CAI_StandoffGoal`, 19 rows): `ai_goal_standoff`
  is authored by nothing; slot 455 answers 0 on every class. Delete bodies where they sit in your
  files; `HintOverlayWords`'s caller `StandoffTranslateActivity` (`BaseMotor10.cpp:818`) is wave
  3's file — list it under "Needs another owner".

## Also list, do not edit

The 115 `registry:N` and 77 `default:…` standing rows have no hand body; they close in the
ledger. For each distinct slot number among them write one line in `slots-C.tsv`:
`slot<TAB>dispatched|undispatched<TAB>evidence` where "dispatched" means some non-test port
body or the generated dispatcher calls that slot (grep `Slot<N>(` and the slot's port name from
`docs/vtmb/npc-kernel/slots.md`). The orchestrator uses it for the `DELETED` map rows.

## Job

1. Build your row list; for each row read its "Why" and the body; delete body and its tests;
   refuse and report any row with a live observer.
2. The `SelectIdealStateSelector` lines and the prints.
3. `rg` that nothing under `Source/` still names what you deleted (excluding generated files).
4. `targets-C.tsv` (`address<TAB>-<TAB>note`) and `slots-C.tsv`.

## Deliver

`report-C.md` (≤300 words): rows deleted by file, rows refused with the retail reason, stamp
lines cut, prints cut, "Needs another owner", both tsv row counts.
