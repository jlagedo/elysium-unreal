# Arena scenario records

One JSON record per scenario under `Arena/scenarios/**/*.json`. Tracked text: adding or tuning one
needs no build. Read by `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.cpp`, staged by
`ElysiumArenaStage.cpp`, run by `ElysiumArenaScenarioRunner.cpp` in two hosts:

| Host | How | What |
|---|---|---|
| headless | `uv run elysium arena [names…]` (`-ElysiumArena`, `-nullrhi`, fixed step) | every record, one boot per stage (a map record alone unless `shares_map`), report under `$ELYSIUM_WORK_ROOT/reports/arena/<timestamp>/`. A map boot is seeded at New Game by `-ArenaSeed=<its first record's seed>`; the arena host re-seeds before each record's `Load` |
| lab | `elysium.gr_scenario <name>` in `uv run elysium gr --arena` | one arena record, rendered, real time; each expectation logged as met or missed; trace in `Saved/Elysium/_arena/lab/` |

The reader is strict: an unknown field, a misspelled kind or probe, a `after` naming no label, a field
the entry's `do` or `probe` does not take is a parse error naming the file and the field
(`cover.json: expect[2].kind: ...`). A record that does not parse is reported, never run.

## Top level

| Field | Type | Meaning |
|---|---|---|
| `name` | string, required | `[A-Za-z0-9_-]+`, unique across all records; names the trace file and is what the launcher selects |
| `about` | string, required | the retail behaviour it proves, with its address or schedule text |
| `stage` | string | `arena` (default) or `map:<map>` |
| `seed` | whole number ≥ 0 (default 0) | `ElysiumRng::SeedAll(seed)` (every stream, `NpcSchedule` included) and the engine's `FMath::Rand`/`SRand`, immediately before the stage's `Load` (arena) or the run (map). A map host activates its map once, at boot, before any record runs, so the launcher also passes the boot's **first** record's seed as `-ArenaSeed`, and the boot's New Game seeds from it instead of the clock: a map record booted alone replays whole for its seed (every NPC's first think, every logic timer drawn at activation). In a `shares_map` boot each later record re-seeds at its start but runs on the map the earlier ones left, so it replays only as part of that boot, in that order |
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

Each rebuilt arena starts its game clock at 0 before `Load`, along with the record's seed. Retail
`NPCInit` / `StartNPC` (`0x10273390` / `0x10273ad0`) branch on the first second of `curtime` and
draw a startup delay there. Inheriting the previous record's clock changes the shared animation /
schedule stream's draw order even when `SeedAll` resets it. Map hosts keep their live map clock.

A `never` state ban judges an established NPC. Its first `None -> …` state edge establishes it
and is excluded from state bans, while remaining in the trace and available to `expect`.
Retail Troika `NPCInit` writes NONE directly (`0x1029a0f5`) and base init writes ideal IDLE
(`0x10273473`). The first `MaintainSchedule` calls real `SetState` (`0x10281b63 -> 0x1026e340`)
before selecting its program, including the state-change hooks; admission must retain those words.
Subsequent state edges, including another edge in that same think, count normally. Other event
kinds keep their existing windows.

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
| `player_crouch` | `on` (bool, required) | one press of the duck key through the same replay door as `player_walk` (merged into the walk's command when both run), then a frame with the key up. The duck is retail's toggle on the press edge (`CGameMovement::Duck` `0x10126fd0`): no press when the body is already heading for `on` (ducked or lowering for `true`, standing or rising for `false`); a stand-up with no headroom is swallowed, as a player's is |
| `light_pin` | `value` (required): a number in `[0, 1]`, or `null` | pins the player's normalized body light through retail's `debug_stealth_light` (`0x109384d8`; set to `value * 10`, so the light reads `value`); `null` sets it back to `-1`, off. The run's end releases a pin it set |
| `dialog_choose` | `index` (a whole number ≥ 0, the response row as the open turn lists it) or `"end": true`, exactly one | the player answers the open conversation through the conversation screen's own doors: a row through `PlayerDialogChoose` (retail `CDialog::Pick` `0x100e4bd0`), `end` -- retail's pick -1, which releases (`docs/vtmb/game_runtime.md` § Retail conversation chain, item 4) -- through `PlayerDialogAdvance`, the Continue, whose close is the port's `CDialog::Release` (`0x100e5240`). Never a teardown the player cannot reach |

An action that cannot run in this host (`player_walk` or `player_crouch` with no input router, an
unresolvable target, `light_pin` with no `debug_stealth_light` registered, a `dialog_choose` with no
open conversation, an `index` past the turn's rows, or a pick or release the open turn refuses -- a
disabled row, an automatic transition pending) ends the run as `error`. On an `expect_fail` record
(a harness self-test) it fails the run instead, `first_unmet.section` `script`, so the self-test
that states a refused action passes.

Every action that runs is written to the trace first, as kind `script`: the entity column is its
target (`player` for the player's actions and `light_pin`, the row's name for `spawn`, `-` for
`console`), the text its `do` and arguments (`fire SetRelationship "player D_HT 5"`,
`player_walk north`, `player_crouch on`, `light_pin 0.2`, `light_pin release`, `dialog_choose 0`,
`dialog_choose end`; `player` is the entity column of the last two).

## `expect`

```json
{ "label": "covers", "who": "arena_gunman", "kind": "schedule", "match": "SCHED_TROIKA_TAKE_COVER_HINT (", "by": 8.0 }
```

- `kind`: one of the trace kinds of `docs/specs/0002-npc-ai/stories/wave2/seam.md` (`schedule`,
  `task`, `taskdone`, `taskfail`, `break`, `cond+`, `cond-`, `state`, `sequence`, `seqfinished`,
  `animevent`, `move`, `damage`, `death`, `corpse`, `hint+`, `hint-`, `output`, `input`,
  `stealthkill`), with the text its table states, or one of the runner's own two: `script` (above)
  and `removed` (below). Read the
  event texts there before matching them: a schedule is `NAME (0x<n>)`, the class-local id in lower-case
  hex with no padding (`SCHED_IDLE_STAND (0x1)`; `NAME (<n>)`, the global id in decimal, when the
  class has no local one), a stealth-kill query `<victim or none> <admit|refuse> gate=<gate>` (`ray`,
  `valid_target`, `deaf_arc`, `can_grapple`), a task `name (id) operand` and a finished task its bare name -- in the corpus's
  lower case (`task_range_attack1`), as the "Task: %s" print names it --, a sequence
  `<label> rate=<n>` (`rate=0` when nothing plays it), a finished sequence its label (`seq 0` for
  row 0, which plays nothing and finishes on its first advance).
- `who`: a targetname (case-insensitive); empty matches any entity. `player` is the player entity,
  whatever targetname it answers to (its events are named `!player` in the trace file).
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

Kinds the harness adds to or widens from `seam.md`'s table (spec 0002 V4a, H21 / H22):

| Kind | Emitted when | Text |
|---|---|---|
| `animevent` | an animation event below the server ceiling (id < 5000) is dispatched to ANY animating entity: an NPC, the player (`who: "player"`), a prop. Retail dispatches from four sites only (`PostRun 0x1026c7c0`, `CBasePlayer::PostThink 0x1016be10`, the weapon's slot 369 `0x1024efa0`, `CCameraAnimated`'s think `0x10071840`) and never for a prop; the tap is inside the dispatcher (`ElysiumAnimEvents::DispatchBase` / `DispatchLayer`, `0x10091880` / `0x10098cd0`) since spec 0002 V4a, so a clip a body plays outside its entity's own sequence emits nothing | `<event id> <options>` |
| `damage` | also at the PLAYER's damage commit (`CBasePlayer::OnTakeDamage 0x10163020`'s health apply), with `who: "player"` | `<applied damage> type=<bits> from=<attacker targetname or none>` |
| `removed` | an entity left the entity world: `UTIL_Remove 0x101cd940`'s port (`FElysiumEntity::Kill`), after which it answers no name lookup and fires no output. The runner's own, read between frames (so its time is the frame's), one line per entity; `who` is the name it answered to. Usable in `expect` / `never` with `by` / `within` / `after` as any kind | the entity's handle, `#<index>` |

## `never`

```json
{ "who": "arena_gunman", "kind": "taskfail", "match": "", "after": "chases", "delay": 0.5, "until": 30.0, "at_most": 1 }
```

`who`, `kind`, `match` and `regex` as an `expect`'s. The entry counts the matching events inside its
window and fails the run at match number `at_most + 1`, at that event's time.

| Field | Meaning |
|---|---|
| `from` | scenario seconds: the window opens here. Not past `duration` or `until` |
| `after` | an `expect` label: the window opens at that expectation's match, plus `delay`. A label that is never met never opens the window (the unmet expectation fails the run on its own); an event before the match is not counted |
| `delay` | seconds after `after`'s match; only with `after` |
| `until` | scenario seconds: the window closes here (absent: the run's duration) |
| `within` | seconds: the window closes this long after it opens at its `after` label (the label's match plus `delay`), never past `duration`. Only with `after`; `within` and `until` exclude each other. A label matched more than once is its FIRST match, as everywhere, so one entry pins one occurrence (the first wait after `waits`; label the second one to pin it too). The run may end once the window has closed |
| `at_most` | a whole number ≥ 0, the matches the window tolerates (absent: 0, the first match fails) |

`from` and `after` exclude each other; neither opens the window at zero. The failure's `reason` in
`index.json` states the count and the resolved bounds (`appeared at t=4.20: match 2 where at most 1 may appear
in [1.50, 30.00]: ...`). A record with an open `never` runs to its duration.

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
| `on_ground` | bool: the MOTOR CAPSULE's floor answer (`AElysiumNpcBody::SampleFloor`). A dead body's motor is frozen at the death spot with its collision off, so this reads **false on every corpse**: ask `corpse_on_floor` there | |
| `corpse_on_floor` | bool: the drawn mesh's `Bip01 Pelvis` BONE (never the component's location, which is below the floor once the body lies, and never the capsule) is within `max_height` of the floor under it (a downward trace against world geometry) **and at rest**: the pelvis moved under 5 cm/s between the runner's last two reads. Never the physics sleep state, which a ragdoll here does not reach | `max_height`: centimetres, required, per the record's body and measured (with Unreal's default capsule asset `regular_cop` rests at 16.2 cm and `bum_male` at 32.9 cm) |
| `exists` | bool: a live (not removed) entity of that name is in the entity world. The one probe a missing entity answers | |
| `speed2d` | number, cm/s: the body's horizontal speed (the motor's velocity) | |
| `move_yaw` | number, degrees: the body's movement yaw relative to its facing (the motor's published sample, `MoveYawVelocity`) | |
| `ground_speed` | number, cm/s: the kernel's `m_flGroundSpeed +0x654` (written by `StudioFrameAdvance 0x1008f120` and `ResetSequenceInfo 0x10090950`, cm/s) | an NPC only |
| `distance_to` | number, centimetres | `to`: a targetname, `player`, or a place |
| `player_weapon` | string: the player's active item's classname (`Inventory.Active`, what `HasWeaponEquipped` compares), `none` | `who: "player"` only |
| `player_crouched` | bool: `FL_DUCKING` (`IElysiumEmbodiment::IsPlayerDucking`: ducked or rising), what the stealth eligibility and the grapple admission read | `who: "player"` only |
| `player_grappling` | bool: paired in a grapple (feed, stealth kill) whose partner still resolves (`IsGrappling`) | `who: "player"` only |

An unknown probe, a comparison the answer's type cannot take, or a probe that cannot be read (no
such entity, not an NPC, no motor, no drawn mesh with the bone, a pelvis read fewer than twice, no
floor under it) fails.

V4c adds read-only `same_team` and `swing_recorded_hit` boolean probes (`to` names the other
entity), `team_symbol`, `wounds`, `health_cap`, `npc_flags1`, `spawn_flags`, `render_alpha`,
`render_mode` and `activity` numeric probes, and `one_hit_kill` (boolean). These read the actual
character words: wounds/cap are sheet slots 0x0f/0x11, and ONE_HIT_KILL is flags1 0x40000000.
`swinghit` traces the hit-list insertion at 0x10343e37 / 0x10343b16; `meleeimpact` traces entry
to 0x102579f0. Both name the victim and print `from=<attacker>`. They let the team record prove
rejection before contact, independently of the later damage gate. `deathcaller` records the
actual health-threshold or ONE_HIT_KILL call site, with the latter's packet, flags, wounds,
cap and schedule, before Event_Killed changes them.

The arena-only `seed_health` action takes `target` and a positive integer `param` (the sheet
base cap). It seeds base Max_Health and zero wounds through the same sheet API as arm fixtures,
then recomputes the sheet and synchronizes engine health. Base 100000 measures effective 99999
under the shipped sheet bounds. This is measurement setup for 0x1032ef60, not a retail input:
the SAVE-only datamap fields cannot be assigned through Python, and NPC Spawn seeds the sheet
after map keyvalues. All subsequent damage uses the ordinary runtime packet path.

The arena-only `damage_packet` action takes `target`, a live named `attacker` (including `player`),
and nonnegative numeric `param`. It submits that scalar packet with the real attacker handle to
the victim's ordinary `OnTakeDamage` chain (0x1032ef60 → 0x10265ed0 → 0x102bee60). Zero is a
packet too: unlike the scalar `TakeDamage` input, it is not refused before admission. Its damage
conditions can interrupt a cower schedule; stage it before cower when measuring ONE_HIT_KILL.

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
| `error` | the harness could not run it (staging refused, an action unsupported, the stage failed or did not activate within 60 s wall) |

A record that ends `error` before its stage activated does not end the boot: the next record is
staged on a released stage and runs.

`expect_fail: true` inverts pass and fail before `known_red` is applied.

The headless host writes `<out>/index.json` (`seam.md` § "The launch contract"; each failure's
`first_unmet` carries `section` — `expect`, `never`, `probe`, or `script` on an `expect_fail`
record — and a `reason`) and
`<out>/<name>.trace.tsv`: every event of the run, `time name kind text`, time in scenario seconds.

## The records

| Record | Proves |
|---|---|
| `cover` | the tactical cover program through the shot (green since V3a) |
| `control_sequence` | the headless host animates: a path-free program's finite activity finishes |
| `world/anim_footsteps_walk`, `world/anim_player_footsteps`, `combat/anim_player_weapon_event_firearm`, `_melee`, `world/anim_prop_event` | the animation events of an NPC, the player and a prop: who dispatches what (guards across spec 0002 V4a's move of the dispatch into each entity's own think) |
| `combat/ranged_friend_in_line_of_fire` | slot 562's line of fire: a gunman with a non-hated body between him and his enemy raises `WEAPON_BLOCKED_BY_FRIEND` (`0x1024f3d0`) and holds his fire until 1.5 s after the last raise (`+0x5b88`) (spec 0002 V5a-3) |
| `combat/verbs_feed_victim_dispatch` | the feed victim's paired clip is its kernel sequence (`SetGrappleActivity 0x1032a100`): its own think dispatches the clip's 4007 and no idle row is re-picked over it |
| `combat/corpse_removed_unseen`, `corpse_kept_seen`, `corpse_kindred_burns`, `corpse_pedestrian_stays`, `corpse_fades` | the four corpse-removal clocks (`removed`) |
| `_selftest/must_fail` | an expectation nothing meets fails the run |
| `_selftest/bound_trips` | a deadline is a deadline: a real event after it does not count |
| `_selftest/never_at_most_holds`, `never_at_most_trips` | `at_most` tolerates its bound and fails the run at the match past it |
| `_selftest/never_after_ignores` | a `never` opened `after` a label does not count a match before it |
| `_selftest/never_within_holds`, `never_within_trips` | a `never` closed `within` seconds of its label ignores a match after the window and fails the run on one inside it |
| `combat/cover_move_shoot` | the run-and-gun: a gunman running to cover fires from an overlay layer's own 3031 (`0x102e8560` -> `AddGesture 0x100991b0` -> `0x10098cd0` -> `Shot 0x102387b0`; spec 0002 V4o) |
| `combat/ranged_open_fire`, `ranged_sustained_fire` | the standing shot is the attack sequence's 3031, the weapon's next-attack stamp is the shot's own write (`0x1023891b`), and an NPC's clip is never spent |
| `_selftest/dialog_choose_none` | a `dialog_choose` with no open conversation is reported as an action the harness could not run, never passed |
| `_selftest/player_reset_a`, `player_reset_b` | the player's posture and wielded item do not leak into the next record of the boot (`player_crouch`, the player probes) |
| `_selftest/stage_failed_a`, `stage_failed_b` | a stage that goes Failed (`a`, `error` by design, so parked as `.json.parked`: restore it to run the pair) does not stop the next record staging fresh and passing (`b`) |
