# Arena scenario records

One JSON record per scenario under `Arena/scenarios/**/*.json`. Tracked text: adding or tuning one
needs no build. Read by `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.cpp`, staged by
`ElysiumArenaStage.cpp`, run by `ElysiumArenaScenarioRunner.cpp` in two hosts:

| Host | How | What |
|---|---|---|
| headless | `uv run elysium arena [names…]` (`-ElysiumArena`, `-nullrhi`, fixed step) | every record, one boot per stage, report under `$ELYSIUM_WORK_ROOT/reports/arena/<timestamp>/` |
| lab | `elysium.gr_scenario <name>` in `uv run elysium gr --arena` | one arena record, rendered, real time; each expectation logged as met or missed; trace in `Saved/Elysium/_arena/lab/` |

The reader is strict: an unknown field, a misspelled kind or probe, a `after` naming no label is a
parse error naming the file and the field (`cover.json: expect[2].kind: ...`). A record that does
not parse is reported, never run.

## Top level

| Field | Type | Meaning |
|---|---|---|
| `name` | string, required | `[A-Za-z0-9_-]+`, unique across all records; names the trace file and is what the launcher selects |
| `about` | string, required | the retail behaviour it proves, with its address or schedule text |
| `stage` | string | `arena` (default) or `map:<map>` |
| `seed` | whole number ≥ 0 | `ElysiumRng::SeedAll(seed)` (every stream, `NpcSchedule` included) and the engine's `FMath::Rand`/`SRand`, immediately before the stage's `Load` (arena) or the run (map) |
| `duration` | seconds, required | the run ends here at the latest |
| `known_red` | string | a red this record is expected to show: its failure reports `expected-fail`, its pass `unexpected-pass` (the red is fixed: remove the field) |
| `expect_fail` | bool | inverts pass and fail (the harness's own self-tests) |
| `shares_map` | bool | map records only: may share a boot with the other map records of its map. Without it the launcher boots the record alone, because a map host does not rebuild the map between records |
| `player` | object | the player's seat (below) |
| `cast` | array | the characters (below) |
| `rows` | array | further entity rows, loaded with the cast (arena only) |
| `from_map` | object | rows of a baked map, verbatim (arena only) |
| `script` | array | timed actions |
| `expect` | array | ordered expectations over the trace |
| `never` | array | events that fail the run whenever they appear |
| `probes` | array | state read at a time or at the end |
| `notes` | string | free text, ignored |

A record with no `expect`, `never` or `probes` is refused: it asserts nothing.

**Scenario time zero** is the stage world's `Activate` on the arena host (the map actor's ready
broadcast, `AElysiumMapActor::ActivateRuntime`; every event of the activation pass is at 0) and the
run's start on a map host.

## Places

`at` is a name or `[x, y, z]` centimetres.

- Arena host: a seat (`start`, `cover_seat`, `cover_behind`), a pad (`north`, `east`, `west`,
  `far_ne`, `far_nw`, `behind_cover`), an anchor (`cover_north` … `corner_sw`) or a node
  (`cover_low_north`, `cover_low_south`, `cover_corner_ne`, `cover_corner_nw`) of
  `ElysiumArenaSpec.cpp`; coordinates are arena-relative. A named place carries its own yaw.
- Map host: a targetname (its origin, yaw 0) or map coordinates.

`face`: `"player"` (toward the player's seat), a place name, or a yaw in degrees (Unreal-native).
Absent: the place's own yaw.

## `player`

`{ "at": "cover_seat", "face": ..., "armed": false, "notarget": false }`. `at` defaults to `start` on
the arena host and to "where the map put it" on a map host. `armed: true` gives the cast harness's
whole arsenal (`ElysiumArenaCast::ArmPlayerWithArsenal`), `"armed": "<item classname>"` one item, at
scenario time zero. `notarget: true` is refused as unsupported: `FL_NOTARGET` has no reader in this
runtime yet.

## Rows: `cast`, `rows`, a `spawn` action's `row`

```json
{ "name": "arena_gunman", "classname": "npc_VHumanCombatant", "at": "far_ne", "face": "player",
  "body": "regular_cop", "keys": { "stattemplate": "TutorialThug" } }
```

A row is `ElysiumArena::AuthoredRow` (the `origin` and `angles` a baked map row carries) plus `keys`,
ordinary map keyvalues, and nothing the arena adds: the record states every key. `body` is a stem,
alias, source path or `vtmb:model:` id, written as the `model` key a map row carries
(`models/<unit>.mdl`); give `body` or a `model` key, not both. `origin`, `angles`, `targetname` and
`classname` are not keys (they are `at`, `face`, `name`, `classname`). `StartHidden` `"1"` makes the
def born hidden, as the bake does. A non-standalone `info_node*` row takes the next AI-network node
after the room's four, in def order.

Def order on the arena host: the room's 8 anchors, its 4 cover-node rows, `rows`, `from_map`,
`cast`. On a map host a `cast` row is `{ "name": ... }` only: an entity the map already has; the
staging refuses the record if it is not there. `rows`, `from_map` and `spawn` are refused there.

## `from_map`

```json
{ "map": "sp_tutorial_1", "names": ["thug_1", "pt1"], "anchor": "thug_1", "at": "far_ne" }
```

Every row of `DA_<map>_Entities` whose targetname is in `names`, verbatim and in the map's order,
moved so `anchor` stands at `at` and every other row keeps its offset from it; Z on the arena floor
(the `origin` key rewritten to match). A name the map does not carry is an error; so is a sky row or
a brush entity (its mesh and collision belong to its map: run it on that map's host).

## `script`

Each action runs at `"t": <scenario seconds>` or `"after": "<expect label>"` plus `"delay"`.

| `do` | Fields | Effect |
|---|---|---|
| `player_teleport` | `at`, `face` | the seat's four steps (mover reset, feet to centre, control yaw, camera reseed) |
| `player_walk` | `at` | the player walks there through the input router's replay door (`gr_walk`'s player arm), one command a frame, until within 32 cm |
| `fire` | `target`, `input`, `param` | one queued input per live entity of that name, as `elysium_entity_fire` (a JSON number marshals Int/Float, a bool Bool) |
| `kill` | `target` | `fire` with `Kill` |
| `console` | `command` | any console command |
| `spawn` | `row` | one more row through `SpawnRuntimeEntity` (arena only) |

An action that cannot run in this host (`player_walk` with no input router, an unresolvable target)
ends the run as `error`.

## `expect`

```json
{ "label": "covers", "who": "arena_gunman", "kind": "schedule", "match": "SCHED_TROIKA_TAKE_COVER_HINT (", "by": 8.0 }
```

- `kind`: one of the trace kinds of `docs/specs/0002-npc-ai/stories/wave2/seam.md` (`schedule`,
  `task`, `taskdone`, `taskfail`, `break`, `cond+`, `cond-`, `state`, `sequence`, `seqfinished`,
  `animevent`, `move`, `damage`, `death`, `corpse`, `hint+`, `hint-`, `output`, `input`), with the text
  its table states. Read the event texts there before matching them: a schedule is
  `NAME (0xNN)`, a task `name (id) operand` and a finished task its bare name -- in the corpus's
  lower case (`task_range_attack1`), as the "Task: %s" print names it --, a sequence
  `<label> rate=<n>` (`rate=0` when nothing plays it), a finished sequence its label (`seq 0` for
  row 0, which plays nothing and finishes on its first advance).
- `who`: a targetname (case-insensitive); empty matches any entity.
- `match`: a case-sensitive substring of the text; empty matches any text. `"regex": true` makes it
  an ICU regular expression (`^task_range_attack1$`, `^(?!seq 0$)`).
- **Ordered.** Expectation `k` is met by the first event not already consumed, at or after
  expectation `k-1`'s match time (zero for the first) and at or before `k`'s deadline. An event in
  the same instant as the previous match counts: one think may claim a hint and then install the
  schedule that wants it.
- **Deadline:** `by` (scenario seconds), `within` (seconds after the previous match), the earlier of
  the two, never past `duration`. Unmet at its deadline, the run fails there.
- `label` names an expectation for `script` `after`.

Two kinds need care: `state` does not cover the body arbiter's own flips inside the mind, and
`damage` is emitted even when no damage was committed. Do not make a verdict depend on either.

## `never`

`{ "who": "arena_gunman", "kind": "death", "match": "", "until": 30.0 }`: fails the run whenever such
an event appears in `[0, until]` (`until` absent: the whole run). A record with an open `never` runs to
its duration.

## `probes`

```json
{ "at": "end", "who": "arena_gunman", "probe": "alive", "equals": true }
```

`at`: `"end"` or a scenario time (not past `duration`). `who`: a targetname, or `player`. Exactly one
comparison: `equals`, `match` (string contains), `less`, `greater` (numbers).

| `probe` | Answers | Extra field |
|---|---|---|
| `alive` | bool: not dead and life state alive | |
| `schedule` | string: the running schedule's authored name, `SCHED_NONE` | |
| `state` | string: the mind's state (`Idle`, `Alert`, `Combat`, `Scripted`, `Prone`, `Dead`) | |
| `health` | number | |
| `enemy` | string: the committed enemy's targetname, `none` | |
| `hint` | string: the claimed hint's targetname (`m_pHintNode`), `none` | |
| `has_condition` | bool | `condition`: a table name (`SEE_ENEMY` or `COND_SEE_ENEMY`) or number |
| `on_ground` | bool: the body's motor reports a floor | |
| `distance_to` | number, centimetres | `to`: a targetname, `player`, or a place |

An unknown probe, a comparison the answer's type cannot take, or a probe that cannot be read (no
such entity, not an NPC) fails.

## The run and its verdict

The runner ends at the first failure (the earliest of an expectation past its deadline, a `never`
that appeared, a timed probe that did not hold), when everything is met (every expectation, every
timed probe, every `never` window closed), or at `duration`; the end probes are read then.

| `result` | When |
|---|---|
| `pass` | everything held |
| `fail` | something did not |
| `expected-fail` | a fail on a record with `known_red` |
| `unexpected-pass` | a pass on a record with `known_red` |
| `error` | the harness could not run it (staging refused, an action unsupported, the stage never activated) |

`expect_fail: true` inverts pass and fail before `known_red` is applied.

The headless host writes `<out>/index.json` (`seam.md` § "The launch contract"; each failure's
`first_unmet` carries `section` — `expect`, `never` or `probe` — and a `reason`) and
`<out>/<name>.trace.tsv`: every event of the run, `time name kind text`, time in scenario seconds.

## The records

| Record | Proves |
|---|---|
| `cover` | the tactical cover program, red at known red 1 |
| `control_sequence` | the headless host animates: a path-free program's finite activity finishes |
| `_selftest/must_fail` | an expectation nothing meets fails the run |
| `_selftest/bound_trips` | a deadline is a deadline: a real event after it does not count |
