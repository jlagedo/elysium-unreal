# Brief A0 — V3a's seam commit (one agent: writes the seam, builds once, commits once)

Read `README.md` here (all of it; §3 is your job) and `packets.md` (V3r's findings). You are the
only V3a agent that writes and builds before the wave: the method's step 1. **The seam changes no
behaviour**: after your commit every record that passed still passes and every V3 record is still
red. Your commit is reviewed by Fable (`brief-A0-review.md`) before the wave starts.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBase.h`
- `Source/ElysiumUE/Public/ElysiumPlayer.h` (the `FElysiumCombatCharacter` class, line ~924)
- `Source/ElysiumUE/Private/Substrate/ElysiumCombatCharacter.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcKernelShapeMap.cpp`
- `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.{h,cpp}`,
  `Source/ElysiumUE/Private/Debug/ElysiumArenaScenarioRunner.{h,cpp}`, `Arena/README.md`
- `Arena/scenarios/world/script_aischedule_walk.json` (new), `Arena/scenarios/world/dialog_use_hold.json`
  (new), `Arena/scenarios/world/script_dialog_hold.json`, and the `known_red` field (only) of
  `cover.json` and the V3 records listed in README §4.

## The job

1. **`m_scriptState +0x5d70` on the NPC.** In `FElysiumNpcBase` (beside the other `CAI_BaseNPC`
   words): `int32 ScriptState = 0;` with `GetScriptState()` / `SetScriptState(int32)`. Comment:
   retail word, writers `PossessEntity 0x101a7880`, `StartTask 0x102827f0` (`0x62` → 0, `99` → 2),
   `ScriptEntityCancel 0x101a7170` (→ 3), `CineCleanup 0x1027d170` (→ 0); readers `0x102c2430`,
   `TaskMovementComplete 0x10273f01`, the `0x60`/`0x66` arms. "Not written yet: the cine holds it as
   `NpcScriptState` until V3c." Move the shape-map row `0x5d70` (`ElysiumNpcKernelShapeMap.cpp:181`,
   today bound to `FElysiumScriptedCharacter::ScriptPhase`) onto it, in the macro form the
   neighbouring `FElysiumNpcBase` rows use. Do not touch the generated bindings: they regenerate at
   V3's close.
2. **`m_hDialogPartner +0xfe8` on the combat character.** In `FElysiumCombatCharacter`:
   `FElysiumEntityHandle DialogPartner;`, `GetDialogPartner()`, and `SetDialogPartner(const
   FElysiumEntityHandle&)` whose body (in `ElysiumCombatCharacter.cpp`) is empty with the comment
   "`CBaseCombatCharacter::SetDialogPartner 0x10107050`: filled in V3d from packet R1". If the shape
   map carries `CBaseCombatCharacter` rows, bind `0xfe8` to it; if it does not, report it.
3. **H12, a dialogue answer / close action** for the records: `do: "dialog_choose"` with `index`
   (a whole number ≥ 0, the response row as the screen lists it) or `"end": true` (retail's pick −1,
   `game_runtime.md` § "Retail conversation chain": "Pick −1 releases"). It goes through the same
   door the conversation screen's choice uses (find it from `ElysiumEntityWorldDialogue.cpp`'s
   choice handling); it never closes a session by calling a teardown the player cannot reach. A
   record whose `dialog_choose` finds no open session is an `error` (the harness could not act),
   stated in `Arena/README.md`'s action table. Add `_selftest/dialog_choose_none.json` with
   `expect_fail` proving the error path is reported, not silently passed.
4. **The records** (state retail; `known_red` names the sub-story that turns each green):
   - `script_dialog_hold.json`: correct the `about` (`0x6a` is `SCHED_TROIKA_RUN_DIALOG`; the task is
     `TASK_RUN_DIALOG 0xb9`); the input's schedule as packet R2 settled it; add the release half:
     `dialog_choose end` at a time after `holds`, then `output OnDialogEnd` (from `0x102c0360`), the
     program's `taskdone task_run_dialog`, a new selection; `never` `OnDialogEnd` twice (`at_most`
     1). `known_red`: "V3d: …".
   - `dialog_use_hold.json` (new): the player at use range of `Hunter1` (the same `from_map` row),
     `console "elysium.cmd +use"` then `-use`: `PlayerUse 0x10167850` clears the schedule and pushes
     `SCHED_TROIKA_RUN_DIALOG (0x6a)`; `task_run_dialog`; state stays `Idle` (a dialogue writes no
     state); `dialog_choose end` → `OnDialogEnd` once → reselect. Order from packet R2 item 3.
   - `script_aischedule_walk.json` (new): `sm_hub_1`'s blueblood-alley `aiscripted_schedule`
     (`authored-control.md:243`: mode 1, forcestate 0, a named goal) and its target, `from_map`
     verbatim; fire `StartSchedule`; expect the executor's install: `SCHED_TROIKA_IDLE_PATROL
     (0x46)` (base `2 IDLE_WALK` through slot 440, `authored-control.md:313-319`), `task_patrol_path`,
     a `move goal` at the goal entity, `arrived`, then a reselection; `never taskfail`. Find the two
     row names with `uv run elysium research` / the reach lists, as V1's brief-W did.
   - `known_red` of `cover`, the three patrols → "V3a: …"; the place records of README §4 → "V3b:
     …"; `script_walk_to_mark` → "V3c: …"; `input_startplayerdialogremote` → "V3d: …". Change no
     expectation of those records.
5. **Build once** (`uv run elysium build`; wait on it, generous timeout or its completion
   notification). A second build only for your own compile break. Then run `uv run elysium arena`
   with the records you touched plus `control_sequence` (by name), and `uv run elysium test` (the
   default tier) once. Acceptance: everything that passed before passes; the four V3 records you
   wrote parse and are `expected-fail`; the self-test passes.
6. **Commit once**: `feat(npc,arena): V3a seam -- m_scriptState and m_hDialogPartner words, H12
   dialog_choose, the V3 records`. Do not push.

## Not yours

No behaviour change anywhere: no writer of `ScriptState` or `DialogPartner`, no change to the
arbiter, the executors, the cine, the dialogue. No `ElysiumNpc.{h,cpp}`.

## Rules

README § "Rules for every agent of V3" (you are the one agent allowed a build here). The query
budget: 10 s warns, 60 s stops; never read a file over ~200 KB whole. Text through Grep / Read /
Glob. Wait on a build or a run by its completion notification, never a sleep or polling loop.

## Report (≤300 words)

The commit hash, the build's wall time, the run's verdicts for the records you touched, the default
tier's totals, the row names you chose for `script_aischedule_walk`, and anything you could not do.
