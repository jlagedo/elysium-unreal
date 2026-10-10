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
| `fixtures` | array | the typed fixture catalog, staged at scenario zero before any action or probe (below) |
| `script` | array | timed actions |
| `expect` | array | ordered expectations over the trace |
| `never` | array | events that fail the run whenever they appear |
| `probes` | array | state read at a time or at the end |
| `notes` | string | free text, ignored |

A record with no `expect`, `never` or `probes` is refused: it asserts nothing.

**Scenario time zero** is the stage world's `Activate` on the arena host (the map actor's ready
broadcast, `AElysiumMapActor::ActivateRuntime`; every event of the activation pass is at 0) and the
run's start on a map host.

Each fresh arena starts its game clock at **1.0 before entity `Load`**, with the existing seed/reset
order (engine `0x200f5bb4..c4`). `NPCInit` / `StartNPC` (`0x10273390` / `0x10273ad0`) branch on
`curtime <= 1`; do not inject draws to preserve a former zero-clock record. Ready can be later
(engine `0x200f5170` runs startup frames); the pre-init fence and ready observation are distinct.
Map revisits select their frozen clock and checkpoint loads select saved time (`0x200975f0`).
Resumed checkpoints restore RNG position without reseeding the record. Scenario elapsed time is
an accumulated simulation interval across map epochs; it never rewinds with the map clock. A
no-world travel gap adds no simulation seconds and has its own wall timeout.

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
staging refuses the record if it is not there. `rows` and `from_map` are refused there. `spawn` is allowed with explicit map coordinates,
through the same authored-row parser and `SpawnRuntimeEntity` factory (`0x101a2e40`).

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
| `fire` | `target`, `input`, `param`, optional `activator` | one queued input per live entity of that name, as `elysium_entity_fire` (a JSON number marshals Int/Float, a bool Bool) |
| `kill` | `target` | `fire` with `Kill` |
| `console` | `command` | any console command |
| `spawn` | `row` | one more row through `SpawnRuntimeEntity` (map host requires coordinate `at`) |
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

## The test instrument

Four general pieces every layer's records use (`docs/specs/layers/harness.md`). A record states what
retail does: the setup, the action, the observable outcome and its timing, citing the retail address
that defines the outcome.

**`fixtures`** -- a typed catalog, staged at scenario zero. Each entry is `{ "id", "kind", ... }`; the id
is unique in the record, the kind one of `ElysiumArenaScenario::FixtureKinds()`. Staging writes one
`script` event per fixture at time 0 (`fixture <id> <kind> staged keys=<n>`). Kinds:

| `kind` | Configuration | Staged as |
|---|---|---|
| `keyvalues` | `values`: an object of keys to string, number or boolean values (read as a map row spells them) | a controlled KeyValues table, read back by `entity_field` on `who: "fixture:<id>"`, `field` the key |
| `text` | `text`: a string, handed whole (JSON escapes spell newlines, quotes and backslashes) | raw text a reader is given as the buffer it would have read from a file: the argument of `entity_call KeyValues_Lex` / `KeyValues_Parse`, or the bytes a `KeyValues_LoadFile` name answers with; `entity_field` on `fixture:<id>` answers `field: "text"`. Staged as `fixture <id> text staged chars=<n>` |
| `sound_folder` | `config`: `{"categories": <n>, "counts": [<int>...], "root": <node>}`, a node `{"label", "key", "mask": [<0|1>...], "children": [<node>...], "siblings": [<node>...]}` (a sibling is the parent's next child, linked through `+0x08`) | the VSound folder index's owner `T` (`Source/ElysiumUE/Private/Audio/ElysiumSoundFolderIndex.h`: `*(T + 8) + 0x14` the category count, `*(T + 0xc)` the counts, the root node at `T + 0x10`), with the harness's retail-shaped answer to the L2 hook `FUN_101f4530` (`sum(counts[0..cat-1]) + idx`); the argument of `entity_call VSoundFolder_AddRange` / `VSoundFolder_Find`; `entity_field` on `fixture:<id>` answers `field: "mask"` (`index`: a node label; the bytes joined by commas) and `field: "count"` (`index`: the category). Staged as `fixture <id> sound_folder staged categories= counts= total= root=<mask>`; a malformed `config` stages nothing (`fixture <id> sound_folder failed: <why>`) |

A fixture is named by an `entity_call` argument (`{"fixture": "<id>"}`) or an `entity_field` `who`. An
unknown kind, a duplicate id, or a reference to a fixture the record does not declare is a parse error.
Later stories add their kinds (save blocks, sound tables, physics bodies...) in the slice that stages them.

**`entity_call`** (a `script` action: `target`, `function`, optional `args`) -- invokes one retail entry
point at its time: an input, `Use`, a touch, a think, spawn and activate, damage through the damage
entry. Never an internal helper: a record that calls a helper tests the port's structure, not the game.
`args` are booleans, numbers, strings or `{"fixture": id}`. `function` is checked against
`ElysiumArenaScenario::EntityCallAllowlist()` when the action runs; a story adds the entry points its
records drive (with their dispatch in `RunAction`). A call outside the allowlist is
refused as a script failure (`entity_call '<f>' is not an allowlisted retail entry point`): `error` on an
ordinary record, a `script` failure on an `expect_fail` one. Traced as `script`:
`entity_call <function>(<args>)`, entity column the target.

| `function` | `target` | `args` | What runs | Result (`script`, entity column the target) |
|---|---|---|---|---|
| `KeyValues_Lex` | a utility name (`keyvalues`), the `who` of its events | `[{"fixture": <text id>}]` | the token wrapper `0x101f2f30` over the text until the cursor it writes back is NULL -- how `0x101f2360` / `0x101f2180` consume a buffer; each token is a `kv_token` site | `entity_call KeyValues_Lex done calls=<wrapper calls>` |
| `KeyValues_Parse` | as above | `[{"fixture": <text id>}, "<target name>"]`; the second argument optional: the node `0x101f2e20` names after the file and hands `0x101f2180` in ECX; absent, no target | `0x101f2180`'s root loop (R7 on; the file arms are the loader's) with `kv_root` sites and `kv_leaf` sites from `0x101f2360` | `entity_call KeyValues_Parse done roots=<n> [<name>(<children>) ...] target=<name>(<children>)|null` |
| `KeyValues_LoadFile` | the NAME of the caller's node (`0x102480f0`'s ECX: `SoundScheme` for `FUN_1022a930`), made on first use and kept for the run; the `who` of its events | `["<file name>", <cache 0|1>, {"fixture": <text id>}?]`: the name the loader receives, its `cache` argument, and the bytes the engine file system answers for that name -- absent, the deployed corpus through the scheme resolver, or no such file | the KeyValues FILE loader `0x102480f0` (`ElysiumKeyValuesLoader.h`) with pathID 0: the text cache `0x102482f0` when `cache` and `.txt`, else its own open; then the root loop (`kv.root` sites) and `0x10248510` (`kv.pair` sites) | `entity_call KeyValues_LoadFile done result=<0|1> roots=<n> [<name>(<children>) ...] target=<name>(<children>)` |
| `KeyValues_GetInt` | a node an earlier `KeyValues_LoadFile` filled | `["<key>", <default>]` | `0x10248bb0` (through `FindKey 0x10248900`): `kv.find`, `kv.getint` sites | `entity_call KeyValues_GetInt done key=<k> result=<n>` |
| `KeyValues_GetString` | as above | `["<key>", "<default>"]` | `0x10248cd0`: `kv.find`, `kv.getstr branch`, on a numeric node `kv.find` + `kv.setstr` (the writeback through `0x102490e0`), `kv.getstr return` | `entity_call KeyValues_GetString done key=<k> result=<text>` |
| `KeyValues_SetString` | as above | `["<key>", "<value>"]` | `0x102490e0`: `kv.find` (create), `kv.setstr` | `entity_call KeyValues_SetString done key=<k> value=<text>` |
| `KeyValues_Chain` | as above | `["<other target>"]` | the harness-built fallback link `+0x18` (`0x10248900` arm 3 searches it; the corpus shows no retail writer) | `entity_call KeyValues_Chain done chain=<other>` |
| `Activate` | a live entity's targetname | none | vslot 113 `Activate` on that entity, as `ServerActivate` `0x1011aaf0` calls it once per level activation (the port's restore barrier runs the pass again): the second pass a record models to re-resolve a cached handle (`CAmbientGeneric::vfunc113` `0x101ac9c0`) | `entity_call Activate done` |
| `VSoundFolder_AddRange` | a utility name (`vsound`), the `who` of its events | `[{"fixture": <sound_folder id>}, <category>, <hi>]` | the owner's `AddRange` `0x101f4330`: `counts[cat] < hi + 1` -> store, then `FUN_101f3ba0` over the tree (`folder_addrange`, `folder_insert` sites) | `entity_call VSoundFolder_AddRange done cat= hi= count= total=` |
| `VSoundFolder_Find` | as above | `[{"fixture": <sound_folder id>}, <key>, <category>, <index>]` | the owner's `Find` `0x101f42d0`: `key == -1` -> NULL, else `FUN_101f3b00` (`folder_find` sites) | `entity_call VSoundFolder_Find done key= cat= idx= node=<label|null>` |

**`entity_field`** (a `probes` entry: `who`, `field`, optional `to`, `index`, `member`, one comparison) --
reads a retail field by its **retail name** (`m_iName`, `m_iHealth`, the datamap name or the ledger's
field name), so a probe survives the port renaming its members. The comparison's value states the type;
a field that answers another type fails the probe, and so does a field with no adapter (the reason lists
the known ones). Read-only. Adapters: `m_iName`, `m_iClassname`, `m_iHealth`, `m_spawnflags`,
`m_nRenderMode`, `m_lifeState`, `m_vecOrigin` (`member`: `x`, `y` or `z`), and **any row of the entity's
class datamap by the name the class registers it under** (`ElysiumAddClassField`: the retail key, as
`radius`, or the retail member, as `m_iSoundLevel`), typed by the row (Int/Float a number, Bool a bool,
String/Handle a string). A story adds a named adapter in `FElysiumArenaScenarioRunner::ReadEntityField`
only for a retail value no datamap row or witness exposes.

**`retail_site`** (a trace kind) -- emitted at the port line that carries a retail address, by
`FElysiumEntityWorld::EmitRetailSite(Entity, Tag, RetailFn, RetailVa, Phase, Payload)`, at the semantic
event a record measures (`entry`, a branch, a write, a callback, `return`). Text:
`tag=<tag> fn=<retail symbol> va=0x<VA> phase=<phase> <ordered typed payload>`; the payload states the
retail values at that point, never port-only state. An `expect` or `never` of kind `retail_site` takes
`site`, the event's `tag=` token (`{"kind": "retail_site", "site": "accept_input", "match": "va=0x100abc90"}`);
`match` is the usual substring or regex over the whole text. Same-time events keep their emission order.
Sites so far: `accept_input` (`CBaseEntity::AcceptInput` `0x100abc90`, phase `dispatch`, payload
`input= param= activator=`); `ambient_level` (`FUN_101ac570` `0x101ac570`: `entry radius= everywhere=`,
`branch positive=`, `return level=`) and `ambient_store` (`CAmbientGeneric::Spawn` `0x101ac326`, `write
field=m_iSoundLevel value=`); `kv_token` (`FUN_10247280` `0x10247280`: `entry selector= table= cursor=`,
`return token= quoted= cursor=<offset|null>`; `return cursor=null out=untouched quoted=0` for a NULL
cursor in); `kv_leaf` (`FUN_101f2360` `0x101f2360`, `write key= type=<0..3> [num=] [str=]`); `kv_root`
(`FUN_101f2180` `0x101f2180`: `entry file= target=<set|null>`, `branch arm=<reuse|new> name=`, `error
file= expecting={ got={ dropped=<token>`, `return result=1 roots=`); the KeyValues FILE loader's
family (`ElysiumKeyValuesLoader.h`): `kv.load` (`FUN_102480f0` `0x102480f0`: `entry file= cache=`, `hook
needle= result=`, `open ok= size=`, `return result= roots=`; and `FUN_102482f0` `0x102482f0` `open ok=
size= interned=`, the cache's own open on a miss), `kv.root` (`FUN_102480f0`: `branch index= name=
reused= brace= [dropped=]`), `kv.pair` (`FUN_10248510` `0x10248510`: `branch key= value= quoted= [ei=
ef=] type=<0|1|2|unset> [block=1]`), `kv.find` (`FUN_10248900` `0x10248900`: `return key= create=
source=<own|chain|created|none> name=`), `kv.getint` (`FUN_10248bb0` `0x10248bb0`: `return key= type=
default= result=`), `kv.getstr` (`FUN_10248cd0` `0x10248cd0`: `branch key= type= formatted=`, `return
key= result= default=<0|1>`), `kv.setstr` (`FUN_102490e0` `0x102490e0`: `write key= new= type=0`).; the ambient initialization
(`walks/L0-r005.md`): `kv_key` (`CAmbientGeneric::vfunc110` `0x101ada80`, `write key= value= [mirror=]`,
the stored dpv word per handled key), `spawn_empty` (`CAmbientGeneric::Spawn` `0x101ac4a0`, `branch
origin= level=`, before the warning and the removal), `spawn_arm` (`CAmbientGeneric::Spawn` `0x101ac49a`,
`branch m_flNextThink=0 m_pfnThink= m_pfnUse= m_fActive= m_fLooping= m_nSndFlags= m_hSoundSource=`, the
words at the tail jump), `precache_arm` (`CAmbientGeneric::Precache` `0x101ac930`: `branch precache=
name=`, `write field=m_fActive value=`), `dpv_pass` (`FUN_101ad0f0` `0x101ad0f0`: `entry m_iHealth=
preset=`, `return volrun= vol= pitchrun= pitch= spinup= fadein= pitchfrac= volfrac= cspincount=`); the
ambient's playback (`walks/L0-r006.md`): `ambient_activate` (`CAmbientGeneric::vfunc113` `0x101ac9c0`,
`write field=m_hSoundSource value=<targetname|self> via=<cached|lookup|self> m_fActive=`, `write
field=m_flNextThink value=curtime+0.1`), `ambient_think` (`FUN_101acb70` `0x101acb70`: `branch idle=1`,
`branch stop=<pitch|volume> ...`, `return vol= pitch= flags= changed= pitchfrac= volfrac=`), `ambient_use`
(`CAmbientGeneric::Use` `0x101ad470`: `entry useType= value= m_fActive= m_fLooping=`, `branch
arm=<set|cspinup|off|start> ...`), `ambient_pitch` (`CAmbientGeneric::InputPitch` `0x101ac690`, `write
field=m_dpv[0x3c] value= param=`), `ambient_volume` (`CAmbientGeneric::InputVolume` `0x101ac7d0`, `write
field=m_dpv[0x4c] value= param=`), `ambient_emit` (the `FUN_101cdac0` call, tagged with the CALLER's
`fn=`/`va=`: `emit flags= vol= level= pitch= origin= source=<targetname|self> name=`), `ai_sound_owner`
(`FUN_101ad9a0` `0x101ad9a0`, `return owner=<targetname|null> via=<cached|lookup> name=`),
`ai_sound_insert` (`CAmbientGeneric::Use` `0x101ad470` U4: `insert type= origin= source= volume= level=
duration= owner=`, `branch refused=owner type=`); `sound_origin` (`CBaseEntity::GetSoundEmissionOrigin`
`0x100a9eb0`, `return origin= abs_origin=`); the VSound folder index (`Audio/ElysiumSoundFolderIndex.h`):
`folder_addrange` (`FUN_101f4330` `0x101f4330`, `branch cat= hi= count= grow= lo=`), `folder_insert`
(`FUN_101f3ba0` `0x101f3ba0`: `entry node= cat= hi= lo= total= a= b= old=`, `warning node= b= total=`,
`return node= mask=`), `folder_find` (`FUN_101f3b00` `0x101f3b00`: `branch node= arm=<sibling|children>
key= [flat= hit=|children=]`; `FUN_101f42d0` `0x101f42d0`: `return key= cat= idx= node=<label|null>`). A
pure function reports through `IElysiumRetailSiteSink` (`ElysiumRetailSite.h`); a utility with no
entity names its target in the entity column (`FElysiumNamedRetailSites`).

Rules: `entity_call` drives only what retail exposes to the world; `retail_site` events are emitted where
the retail address they name is ported and state retail values; `entity_field` reads retail names;
a record states the retail outcome and cites its address.

## `expect`

```json
{ "label": "covers", "who": "arena_gunman", "kind": "schedule", "match": "SCHED_TROIKA_TAKE_COVER_HINT (", "by": 8.0 }
```

- `kind`: one of the trace kinds of `docs/specs/0002-npc-ai/stories/wave2/seam.md` (`schedule`,
  `task`, `taskdone`, `taskfail`, `break`, `cond+`, `cond-`, `state`, `sequence`, `seqfinished`,
  `animevent`, `move`, `damage`, `death`, `corpse`, `hint+`, `hint-`, `output`, `input`,
  `stealthkill`, `retail_site`), with the text its table states, or one of the runner's own two:
  `script` (above) and `removed` (below). Read the
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
| `_selftest/selftest_fixture_stages` | a `fixtures` entry stages at zero and `entity_field` reads it, and a live entity's retail fields, back |
| `_selftest/selftest_entity_call_refused` | an `entity_call` outside the (empty) allowlist is refused as a script failure, never run |
| `_selftest/selftest_entity_field_fails` | an `entity_field` probe on a field with no retail adapter fails the run |
| `_selftest/selftest_retail_site_matches` | a `retail_site` event carries tag, retail function and address, and `expect` / `never` match on `site` |
| `audio/l0_ambient_radius_level` | `0x101ac570` at `CAmbientGeneric::Spawn` (`0x101ac321` / `0x101ac326`): eleven radii (and one everywhere flag) to their `m_iSoundLevel`, each arm traced, the stored words probed (L0.audio.ambient-radius-level) |
| `audio/l0_ambient_init` | `CAmbientGeneric` initialization: the dpv keys through `vfunc110` `0x101ada80` (clamps, spin/fade transforms, mirrors), `Spawn` `0x101ac310` (the empty-sound removal, `m_fLooping`, `m_nSndFlags`, the tail words), `Precache` `0x101ac930` (the engine-precache gate, `m_fActive`) and the dpv pass `FUN_101ad0f0` (health volume, presets 1 and 7, cspinup, the pitch 101 rule, `fadein 2` -> 2560, `fadein 3` -> 1536, spawnflags 1 -> level 0) over fourteen ambients, every `m_dpv` word probed by its offset, and the activation pass `vfunc113` `0x101ac9c0` on a runtime spawn (L0.audio.ambient-init) |
| `audio/l0_ambient_source_resume` | `CAmbientGeneric::vfunc113` `0x101ac9c0` is `Activate` (slot 113), run by `ServerActivate` `0x1011aaf0` once per activation: the named source bound by lookup, the self fallback, the flags-`0x8` emission and the first think only when active; a killed source silences the cached handle (a `Pitch` writes but emits nothing) until the next pass, which finds nothing and binds self; a valid cache looks nothing up (L0.audio.ambient-source-resume) |
| `audio/l0_ambient_pitch` | `CAmbientGeneric::InputPitch` `0x101ac690`: the float clamp `[0, 255]` and truncation into `m_dpv[0x3c]`, the change-pitch emission through the cached handle with no `m_fActive` gate (a start-silent ambient emits its pitch change too); 125, 300 -> 255, `abc` -> 0, -5 -> 0 (L0.audio.ambient-pitch) |
| `audio/l0_ai_sound_owner` | `FUN_101ad9a0` `0x101ad9a0` and Use `0x101ad470` U4: the owner resolved and cached on the first start, a second PlaySound on an active ambient returning at U0, the stale cache re-resolved only by a fresh start (StopSound, PlaySound), the refusal for a NULL owner outside CARCASS/FLINCH, the level clamp written back, the insert at the SOURCE entity's origin with `VolumeLevels[level]` (L0.audio.ai-sound-owner) |
| `audio/l0_sound_emission_origin` | `CBaseEntity::GetSoundEmissionOrigin` `0x100a9eb0` (slot 222) answers slot 192 `WorldSpaceCenter` through the dispatch: `CreateCorpse` `0x1032c0e0`'s burn arm (a Kindred's death) dispatches it twice for its PAS filters, each return a centre above the entity's origin and never the origin itself (L0.audio.sound-emission-origin) |
| `audio/l0_sound_folder_index` | the VSound folder index: `FUN_101f3ba0` `0x101f3ba0` through `AddRange` `0x101f4330` (the walk's worked example `[2, 3]` + `AddRange(0, 3)`, the tree expanded node by node, the no-grow arm, the tail arm) and `FUN_101f3b00` `0x101f3b00` through `Find` `0x101f42d0` (the sibling walk regardless of key, children never searched on a key match, `key -1`) on a `sound_folder` fixture (L0.audio.sound-folder-index) |
| `audio/l0_keyvalues_lexer` | the tokenizer `0x10247280` through its wrapper `0x101f2f30`, tables `0x102473e0` / `0x1023eff0`: eighteen texts to their token, quote and cursor sequences (L0.audio.keyvalues-lexer) |
| `audio/l0_keyvalues_tree` | the file reader `0x101f2180` and block parser `0x101f2360`: root reuse and chaining, the missing-brace error, the top-level `}`, the empty key, and the strtol/strtod leaf typing (L0.audio.keyvalues-tree) |
| `audio/l0_scheme_file_load` | the KeyValues FILE loader `0x102480f0` (the `CSoundScheme::Precache` call), its wrapper `0x102482b0`, block parser `0x10248510` and text cache `0x102482f0`: the cache hit and miss, the loader's own open, no `.txt`, a missing file, every top-level token a root, the first-byte brace test, the typed leaves, and `sound/Schemes/SP_Tutorial_City.txt` itself (L0.audio.scheme-file-load) |
| `audio/l0_keyvalues_access` | the loader class's accessors `0x10248900` / `0x10248bb0` / `0x10248cd0` / `0x102490e0`: first match, case folding, the fallback chain, `atoi` and `__ftol` conversions, the `%d` / `%f` writeback and the create arm (L0.audio.keyvalues-access) |
| `_selftest/never_after_ignores` | a `never` opened `after` a label does not count a match before it |
| `_selftest/never_within_holds`, `never_within_trips` | a `never` closed `within` seconds of its label ignores a match after the window and fails the run on one inside it |
| `combat/cover_move_shoot` | the run-and-gun: a gunman running to cover fires from an overlay layer's own 3031 (`0x102e8560` -> `AddGesture 0x100991b0` -> `0x10098cd0` -> `Shot 0x102387b0`; spec 0002 V4o) |
| `combat/ranged_open_fire`, `ranged_sustained_fire` | the standing shot is the attack sequence's 3031, the weapon's next-attack stamp is the shot's own write (`0x1023891b`), and an NPC's clip is never spent |
| `_selftest/dialog_choose_none` | a `dialog_choose` with no open conversation is reported as an action the harness could not run, never passed |
| `_selftest/player_reset_a`, `player_reset_b` | the player's posture and wielded item do not leak into the next record of the boot (`player_crouch`, the player probes) |
| `_selftest/stage_failed_a`, `stage_failed_b` | a stage that goes Failed (`a`, `error` by design, so parked as `.json.parked`: restore it to run the pair) does not stop the next record staging fresh and passing (`b`) |

A `fire` action may name a live `activator` (including `player`). It reaches the ordinary input queue: retail InputTakeDamage `0x102c29a0` uses the caller as inflictor and activator as attacker. Missing named activators are errors. Without an activator, positive damage returns at `0x10265f64` before raising damage conditions.

## V6 transactions and exact fences

Harness transport follows engine `0x20096010` / `0x200975f0` and server reverse post-restore
`0x1011a620`. An action at `t` or `after` is submitted once. Dependent actions wait for the exact
capture/storage/apply/readiness result. Refusal, decode error, wrong map, unavailable witness,
missing pre-Load rebind or wall timeout is a script error (a `script` failure for `expect_fail`).
A write accepted for asynchronous storage is not a successful save. Label matches, never counts,
fired actions and zero-player setup survive replacement; the runner is not restarted. Retired
removal baselines are cleared, so restored entities produce no synthetic removal sweep.

| `do` | Required fields | Optional fields / host |
|---|---|---|
| `save` | `slot` | `checkpoint` with nonempty `fields`, `timeout` (wall seconds, `(0,60]`, default 60); either host |
| `load` | `slot` | `checkpoint` (must be written in the same slot), `timeout`; either host |
| `fresh_map` | `map` | map host; fresh destination without discarding other revisits |
| `travel` | `map` | `landmark`; map host; normal travel/revisit |
| `restore_compare` | `checkpoint`, nonempty `fields` | compares cached apply-before-think values, never a later tick |

Slots are logical names prefixed with `__arena_v6_` in native storage, including map-host saves.
They never overwrite the user's latest slot and are never deleted. Map hosts use the real
`RequestSave` / `Load` / `FreshLoad` / `Travel` gates. The direct `elysium.load` alias remains an
independent integrator witness; a console trace is not transaction completion.

```json
{
  "script": [
    {"t":1,"do":"save","slot":"path","checkpoint":"mid",
     "fields":[{"who":"walker","field":"task.index"},
               {"who":"walker","field":"task.task_started","tolerance":0.000001}]},
    {"t":2,"do":"load","slot":"path","checkpoint":"mid"},
    {"t":2,"do":"restore_compare","checkpoint":"mid",
     "fields":[{"who":"walker","field":"task.index"},
               {"who":"walker","field":"task.task_started","tolerance":0.000001}]}
  ],
  "probes": [
    {"at":"applied","who":"world","probe":"witness",
     "field":"coordinator.normal.count","equals":0},
    {"at":"end","who":"walker","probe":"witness",
     "field":"task.index","checkpoint":"mid"}
  ]
}
```

This is a fragment: the ordinary name/stage/duration/staging and behavior expectations are required.
A checkpoint name is single-assignment. Comparison fields must have been captured. Unknown fields,
wrong scalar types, duplicate words, unknown checkpoints, irrelevant transaction keys and unsupported
hosts are parse failures. A timed comparison before its capture/write/load is a runtime refusal.
`fields` entries are exactly `{who, field, tolerance?}`. Handles use stable `#index` identities;
`who:"#12"` explicitly distinguishes a retained corpse from a later same-name maker child.
Boolean/name/identity equality is exact and case-sensitive. Numeric tolerance is only for stated
native conversion error, not lost frames. TIME equality uses source and destination bases, including
wait-zero, move/shoot-never and NextThinkSR sentinel rules (`0x101a0a80` / `0x101a2a30`). Weapon
next-attack stamps and layer event cursors remain FLOAT. Base LastEventCheck remains TIME.

`probe:"witness"` requires `field` and one ordinary typed comparison, or `checkpoint` for saved-word
equality (no literal comparison). `at` accepts `end`, a scenario time, or `pre_init`, `captured`,
`applied`, `ready`. Fence probes run synchronously at the first named fence; an absent fence fails.
Saved equality uses the cached apply sample even when the assertion is scheduled at the end.
Pre-init reads the selected map clock before entity initialization; ready reports activation latency.
The initial Green Room pre-init probe supports its captured clock only; other pre-init words must
use a transaction reconstruction fence. `stage_pre_init` retains that measured clock/draw sample.
Trace TSV retains its first four columns and appends `world_time`, `epoch`, `map`. Transaction
fences print the operation id, phase and NpcSchedule stream position without drawing. `stage_ready`
prints the initial ready position. Integrator `thinkfence` taps report actual first-think entry;
`makerattempt` taps must report admission synchronously, not a sample from the next runner tick.

The closed witness vocabulary is defined in `ElysiumArenaScenario::WitnessType`:

- World: `clock`, `map`, `world_generation`, `identity`.
- Task: `task.id`, `index`, `status`, `failure`, `started`, `task_started`, `wait`, `move_wait`
  (each after `task.`); callback: `callback`, `callback.saved`, `think.next`, `hidden`, `script.owner`.
- Base animation: `anim.sequence`, `cycle`, `rate`, `time`, `previous_time`,
  `last_event`, `ground_speed`, `yaw_speed`, `finished`, `past_half` (after `anim.`).
- Each `layer.0` through `layer.3`: `flags`, `finished`, `sequence`, `cycle`, `rate`, `weight`,
  `weight_max`, `blend_in`, `blend_out`, `activity`, `auto_kill`, `last_event`. Auto-kill is boolean;
  the other native row words are numeric (`0x10098c80`).
- `move_shoot.active`, `next`, `burst`, `min_burst`, `max_burst`, `pause_min`, `pause_max`, `initial_delay`; `weapon.owner`, `reload`, `jam`,
  `interrupt`, `next_primary`, `next_secondary`, `idle` (after their respective prefix).
- `nav.type`, `flags`, `target`, `arrival`, `retry_interval`, `retry_duration`, `retry_next`, `timeout`;
  `nav.goal.x/y/z`; `memory.enemy`, `memory.last_seen`, `memory.position.x/y/z`;
  `damage.attacker`, `damage.sum`, `damage.time`, `damage.position.x/y/z`.
- `place.identity`, `capacity`, `count`, `failed_attempts`, `in_use`, `ring_index`, `reservations`,
  `releases`; `place.destination.x/y/z`, `place.bounds_min.x/y/z`, `place.bounds_max.x/y/z`;
  `place.marker.<nonnegative index>.occupant`, `.min.x/y/z`, `.max.x/y/z`;
  `place.ring.0..3.time`, `.min.x/y/z`, `.max.x/y/z`.
- `senses.can_sense`, `gathered`, `pass`, `sighted`; `dialog.open`, `partner`, `partner_live`;
  `los.player`, `pvs`, `cache`, `last_clear`.
- `coordinator.normal/player/boss.count`, `.cap`, `.members` read the actual world-owned lists.

Unavailable dispatch sources fail with their field name. The integrator installs exact read-only
NPC, motor, memory, place and weapon accessors through `SetHostAdapter`; the harness never invents
values to turn a record green. Epoch, gather-pass and the rebuilt Sighted list are literal diagnostics excluded from snapshot
equality. Raw pointers, transient routes/caches, shoot-at reroll, rendered animation labels and opaque
archive bytes are excluded. Coordinator membership is observed fresh, not archived or synthesized
(`0x1023ae40` → `0x1023b840` → `0x1025d880`); the integrator must first confirm that retail chain.

Green Room provenance is nonshipping and distinct from gameplay map identity. The normal payload
codec/storage carries a snapshot named `__arena_checkpoint_v6` plus `__arena_provenance_v6` containing
original defs, exact node/network words and original stage seat. Metadata uses runtime-def records
in that envelope only, never runtime entities. Load decodes the same codec, restores session blocks,
selects saved/tagged time, rebuilds actual stage bodies/admissions, applies the SAME snapshot core,
and waits for the barrier. Saved player placement outranks the original seat. A malformed envelope,
missing native factory provenance, partial snapshot apply or unavailable body blocks acceptance.
Production Load still rejects this stage identity as unbaked. Existing `EnterStageWorld` establishes
the real GameFlow session; an absent in-session/player gate is reported rather than bypassed.

## V6 Green Room fixtures

These are controlled setup followed by ordinary runtime consumers, never map inputs or generic
member setters. Every setup is traced. Missing runtime adapters are unavailable, not successful.

| `do` | Required fields | Consumer / reason controlled staging is needed |
|---|---|---|
| `npc_single_round_finish_reload` | `target` | real NPC WeaponFinishReload (`0x1028918d` → `0x10255077`); ordinary NPC shots do not spend clips |
| `corrupt_checkpoint` | `checkpoint`, `control`: `crc`, `missing_cine`, `missing_target`, `missing_path` | decoded checkpoint-header adapter then ordinary OnRestore (`0x1027bf50`); corruption is not a player input |
| `invalid_marker` | `target` | remove only fixture membership then ordinary consistency (`0x10299a80`) |
| `restore_base` | `checkpoint`, numeric `param` | tagged restore-context destination base (`0x20097d00`), never arbitrary NPC timestamp writes |
| `no_ragdoll_death` | `target` | MiscFlag `0x80000` fixture, then normal death (`0x1032c0e0`); no guessed callback |
| `damage_memory` | `target`, `attacker` (current enemy), `control`: `unknown_no_see`, `known`, `see_enemy` | real memory/SEE_ENEMY setup then ordinary damage_packet (`0x10265ed0`) |
| `reserve_spot` | `target` (place), `attacker` (NPC), `control`: `normal`, `full`, `occupied`, `exhausted_clearance` | actual PickSpotFor (`0x102da0d0`); hull/collision services must answer live |
| `startnpc_ground_gate` | `target`, `control`: `normal`, `fly`, `swim`, `capability4` | actual StartNPC (`0x10273ad0`), full hull sweep/motor; ordinary authored spawnflags/bounds stay row keys |

`damage_packet` retains nonnegative scalar `param` (zero included), required `target`, and optional `attacker`
(default `none`, also accepted explicitly). Optional `inflictor` is a real named entity or `none`. The scalar
packet currently lacks the independent +0x28 inflictor seam: a named inflictor requires the runtime
adapter, with an exact unavailable error until wired. No synthetic CVDmg_t changes scalar admission.
M1's makerattempt tap includes refusal gate/live/global/box and every enumerated candidate's stable
index/flags/life/bounds (`0x1034b580` / `0x101cc9e0`); do not add an alive filter or an RNG draw.

Required resume witnesses also consume `ValidateWitnessAdmission`'s class/model/native-sequence/
asset/recipe/admission ledger at scenario zero. Missing assets/continuations block acceptance.
Only controlled records may substitute an already baked donor proving the identical arm; authored
`map_tutorial_unhide_thug3` remains thug_3. Clock records must be remeasured alone and paired at
original predicates/windows, with pre-init/ready/first-think stream positions, before closure.

Tests are authored under `Elysium.Arm.V6.ArenaPersistence` and not run by the coder. Integrator owns
the real `persistence_refused_save`, `persistence_missing_load`, `persistence_world_rebind` records,
both-host activation/admission measurements and all behavior records. No acceptance is inferred
from these parser/transaction-law tests.
