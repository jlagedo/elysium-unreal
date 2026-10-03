# Brief A — T5: the scenario record, the runner, the headless arena run

Read `README.md` and `seam.md` here first. You own the scenario core and both of its hosts.

## What exists

- The arena: `Debug/ElysiumArenaSpec.{h,cpp}` (the room as values), `ElysiumArenaBuilder.{h,cpp}`
  (`Stand`, `AuthoredRow`, `AnchorRow`, `NodeRow`, `NodePlaceRows`, `IsNavigationReady`),
  `ElysiumArenaCast.{h,cpp}` (`SpawnAuthored`, …).
- `elysium.gr_scenario cover` (`Debug/ElysiumGreenRoomConsole.cpp:858`): a hand-written C++ scenario
  — `CoverGunmanRow`, the cover seat, then `AElysiumMapActor::RebuildStageWorld(Defs, NodePlaceRows,
  Seat)` (`Map/ElysiumMapActorLifecycle.cpp:256`), which tears the stage's entity world down and
  runs the map sequence (places adopted, `Load`, the player entity, the barrier, `Activate`).
- **The lab needs a real RHI** (`UElysiumMapSubsystem::EnsureGreenRoomLab` refuses under
  `GUsingNullRHI`). The headless precedent is `FElysiumCastRun` (`Debug/ElysiumCastRun.{h,cpp}`,
  `-ElysiumCast`): `-nullrhi -unattended -UseFixedTimeStep`, the stage world, `ElysiumArena::Stand`,
  a ticker, courses one after another, `RequestExitWithStatus`. It is armed in
  `UElysiumMapSubsystem` (`Map/ElysiumMapSubsystem.cpp:175`). `Debug/ElysiumComposeRun.cpp:327`
  states how a body is made to pose when nothing renders it.

## Job

1. **The record** — `Debug/ElysiumArenaScenario.{h,cpp}`: a plain struct and its JSON reader
   (`FJsonSerializer`), no world. Every parse error names the file and the field. The schema, which
   you also write down as `Arena/README.md`:

   ```json
   {
     "name": "cover",
     "about": "what retail behaviour this proves, with its address or schedule text",
     "stage": "arena",
     "seed": 1,
     "duration": 40.0,
     "known_red": "",
     "expect_fail": false,
     "player": { "at": "cover_seat", "armed": false, "notarget": false },
     "cast": [
       { "name": "arena_gunman", "classname": "npc_VHumanCombatant", "at": "far_ne", "face": "player",
         "body": "regular_cop", "keys": { "stattemplate": "TutorialThug" } }
     ],
     "rows": [ { "classname": "path_corner", "name": "p1", "at": [200, 0, 0], "keys": {} } ],
     "from_map": { "map": "sp_tutorial_1", "names": ["thug_1", "pt1"], "anchor": "thug_1", "at": "far_ne" },
     "script": [
       { "t": 5.0, "do": "player_teleport", "at": "cover_behind" },
       { "after": "covers", "delay": 1.0, "do": "fire", "target": "arena_gunman", "input": "TakeDamage", "param": "1" }
     ],
     "expect": [
       { "label": "covers", "who": "arena_gunman", "kind": "schedule", "match": "SCHED_TROIKA_TAKE_COVER_HINT", "by": 8.0 },
       { "who": "arena_gunman", "kind": "hint+", "match": "cover_corner", "within": 2.0 }
     ],
     "never": [ { "who": "arena_gunman", "kind": "death", "match": "" } ],
     "probes": [ { "at": "end", "who": "arena_gunman", "probe": "alive", "equals": true } ]
   }
   ```

   - `at`: a pad, anchor, node or seat name of the arena spec (`start`, `cover_seat`,
     `cover_behind` are the seats), or `[x, y, z]` arena-relative centimetres. `face`: `player`, a
     name, or a yaw.
   - `cast`: a row is `ElysiumArena::AuthoredRow` plus `keys`, nothing the arena adds silently.
     `body` is resolved to the `model` key the way `CoverGunmanModel` does. `keys` are ordinary map
     keyvalues; the record states every one (move `CoverGunmanRow`'s keys into `cover.json`).
   - `rows`: any further entity rows, loaded with the cast through `Load`.
   - `from_map`: the named rows of the map's baked `DA_<map>_Entities` (find the asset's reader;
     `UElysiumMapEntities`), verbatim, translated so `anchor` stands at `at` and every row keeps
     its offset from it; Z snapped to the arena floor. A name the map does not carry is an error.
   - `script`: actions at an absolute scenario time `t` or `after` a labelled expectation's match
     plus `delay`. Actions: `player_teleport`, `player_walk` (through the input replay door
     `gr_walk` uses, if it is reachable without the lab; else leave the action parsed and answer
     "unsupported in this host" as an error), `fire` (an entity input, as `elysium_entity_fire`),
     `console` (any console verb), `spawn` (one more row at run time, through `SpawnRuntimeEntity`),
     `kill`.
   - `expect`: ordered. Each is met by the first event at or after the previous expectation's match
     with that `who` (targetname), `kind` (`seam.md`) and `match` (a case-sensitive substring;
     `"regex": true` for a regex). `by` is scenario seconds; `within` is seconds after the previous
     match. Unmet at its deadline, the scenario fails there.
   - `never`: an event that fails the scenario whenever it appears (optionally `until`).
   - `probes`: read at `"end"` or at a time: a closed list through named accessors — `alive`,
     `schedule`, `state`, `health`, `enemy`, `hint`, `has_condition`, `on_ground`, `distance_to`
     (with `less` / `greater`). An unknown probe is a parse error.
   - Scenario time zero is the stage world's `Activate`.
2. **The runner** — `Debug/ElysiumArenaScenarioRunner.{h,cpp}`: given a world that is staged and
   active, installs the trace sink (`seam.md`), records every event, fires the script, evaluates
   expectations, `never` and probes each tick, and ends at the first failure, when everything is
   met, or at `duration`. It writes the trace file and returns the result record of `seam.md`. It
   includes no lab type (`FElysiumGreenRoomRun`): both hosts hand it the world, the map actor and
   the arena spec. The `seed` is applied to the world's random streams before `Activate` (find how
   the `NpcSchedule` stream is seeded; if a stream cannot be seeded from outside, say so in the
   report rather than adding a hook into the kernel).
3. **The staging** — one function both hosts call: record → defs (anchors, node rows, cast, rows,
   `from_map` rows), place rows and seat → `RebuildStageWorld`. Seating the pawn without the lab's
   helper: `SeatPlayerAt` in the console file shows the four steps; `FElysiumMoveRun` seats a pawn
   headless.
4. **The headless host** — `Debug/ElysiumArenaRun.{h,cpp}`, `FElysiumArenaRun`, armed in
   `UElysiumMapSubsystem` beside the cast run; the launch contract is `seam.md`'s. Arena host:
   enter the stage world, `Stand` the arena once, wait for `IsNavigationReady` (bounded; a harness
   error past it), then per record: stage, wait for the barrier, run, write. Map host
   (`-ElysiumMap=`): no arena; the record's `cast` names entities the map already has (a `cast`
   row with only `name`), `rows` / `from_map` are refused, `at` takes map coordinates or a
   targetname; the map is not rebuilt between records, so the launcher (lane B) runs map records
   one per boot unless a record sets `"shares_map": true`. Then `index.json` and
   `RequestExitWithStatus`.
   **Bodies must animate headless**: a sequence has to advance and finish with nothing rendering,
   or every animation expectation is red for the wrong reason. Read how the NPC's sequence clock is
   advanced (the body's clip phase; `AElysiumNpcBody`, `ElysiumNpcAnim.cpp`, the compose run's note)
   and make the run's bodies tick their pose under `-nullrhi`. State in the report what you found.
5. **The lab host** — `elysium.gr_scenario <name>` stages and runs the same record in the lab
   (renderer on, real time) and logs each expectation as it is met or missed; `elysium.gr_scenario`
   alone lists the records. The hard-coded `cover` body, `CoverGunmanRow` and the `--runtime` path
   go; `gr_hints`, `gr_los` and the rest stay.
6. **The records** — under `Arena/scenarios/`:
   - `cover.json`: the 2026-09-30 run (`docs/harness/green-room-arena.md` § 4): the gunman on
     `far_ne`, the player at the cover seat; expect `SCHED_TROIKA_TAKE_COVER_HINT` and a `hint+`
     within ~8 s, then — what retail does and the port does not — the cover sequence finishing
     (`seqfinished`) and a range attack (`task` / `taskdone` for `TASK_RANGE_ATTACK1`).
     `known_red: "1: the body arbiter refuses the kernel's sequence (ElysiumNpcAnim.cpp:402-431)"`.
     Read the schedule texts under `Content/ElysiumCorpus/` for the program's real task order
     before writing the expectations; cite the schedule in `about`.
   - `control_sequence.json`: a path-free program whose finite activity must finish headless (a
     standing NPC flinching on `TakeDamage` is the candidate; read its schedule text first). It
     proves the host animates. No `known_red`: if it would be red, find out why before the wave
     ends and report it.
   - `_selftest/must_fail.json` (an expectation nothing can meet, `expect_fail: true`) and
     `_selftest/bound_trips.json` (a real event with a deadline it cannot make, `expect_fail: true`).
7. **The doc** — `docs/harness/green-room-arena.md` rewritten for the records, both hosts and
   `uv run elysium arena`; its stale "known limits" struck.

## Files you own

`Source/ElysiumUE/Private/Debug/ElysiumArena*` (existing and new), `ElysiumGreenRoomConsole.{h,cpp}`,
`ElysiumGreenRoomArena.cpp`, `ElysiumGreenRoomRun.{h,cpp}` and `ElysiumGreenRoomShared.h` (only what
the staging needs), `Private/Map/ElysiumMapSubsystem.cpp` and `Public/ElysiumMapSubsystem.h` (arming
the run), `ElysiumMapActor*` (only if `RebuildStageWorld` must change; say what and why),
`Arena/**`, `docs/harness/green-room-arena.md`. Not `Public/ElysiumEntityWorld.h`, `Substrate/**`,
`Debug/ElysiumEntityDebugSubsystem.cpp`, `Debug/ElysiumMcp*` or any Python (lane B); not `Tests/**`
(lane C). If the repository policy (`uv run elysium doctor`, the commit hook) refuses a top-level
`Arena/`, do not move it yourself: say so in the report.
