# Green Room arena

A controlled combat stage for one retail NPC behaviour at a time, and the live test suite built on it.
It is a harness, not a map: nothing in it is retail content. A scenario is a JSON record under
`Arena/scenarios/` (schema: `Arena/README.md`) that two hosts run through one runner against the AI
trace (`FElysiumAiTraceEvent`, `docs/specs/0002-npc-ai/stories/wave2/seam.md`):

| Host | Command | Use |
|---|---|---|
| headless | `uv run elysium arena [names…]` | the suite: every record, nothing rendered, a report and a verdict |
| lab | `elysium.gr_scenario <name>` in `uv run elysium gr --arena [--headless]` | watching one record, rendered, in real time, over MCP |

Source, all under `Source/ElysiumUE/Private/Debug/`:

| File | Part |
|---|---|
| `ElysiumArenaSpec.{h,cpp}` | the room as values |
| `ElysiumArenaBuilder.{h,cpp}` | collision, NavMesh, the anchor and node rows |
| `ElysiumArenaScenario.{h,cpp}` | the record and its reader (no world) |
| `ElysiumArenaStage.{h,cpp}` | record → defs → `AElysiumMapActor::RebuildStageWorld`; places, faces, the player's seat |
| `ElysiumArenaScenarioRunner.{h,cpp}` | the trace sink, the script, the expectations, the verdict |
| `ElysiumArenaRun.{h,cpp}` | the headless host (`-ElysiumArena`), armed in `UElysiumMapSubsystem` |
| `ElysiumGreenRoomConsole.cpp` | the lab verbs, `gr_scenario` among them |

## 1. What the arena is

`ElysiumArena::Build()` states the room in Source units; the builder multiplies by `ElysiumMove::U`.

| Part | Value (spec) |
|---|---|
| Floor and walls | Square room, `RoomHalfExtent` 512 units; floor top at Z 0; four walls 320 high, outside the interior |
| Cover block | One solid at the centre, `BlockHalfWidth` 96, `BlockHeight` 96 (above a ~64 standing eye, so a body behind it is untraceable) |
| Anchors (8) | `intersting_place` rows named `arena_<name>`. Face anchors `cover_north/south/east/west` (rating 10, 24 units off the block face, back to it); corner anchors `corner_ne/nw/se/sw` (rating 4) |
| Pads (6) | `north`, `east`, `west`, `far_ne`, `far_nw`, `behind_cover`; all face the middle |
| Cover nodes (4) | In network order: `cover_low_north` (type 101), `cover_low_south` (101), `cover_corner_ne` (10200), `cover_corner_nw` (10200). Only type and group are authored; distances, angle range and rating (3, then 2.5) are `CAI_Hint::Spawn`'s per-type defaults |
| Player seats | `start`: south wall, facing the block. `cover_seat`: (-300, 683) cm facing +X toward `far_ne`. `cover_behind`: on the `far_ne` to centre line, `BlockHalfWidth` + 150 cm past the centre |

Anchors are the ambient destination system and are not cover. The cover nodes are the real thing: the
Troika cover search (`0x102b7110` via `FindHintByClassMask(8, 1, radius)`) walks the `ai_hint`s they make.

NavMesh (`ElysiumArenaBuilder.cpp`, `EnsureArenaNavMesh` and `BuildNavigation`): the project ini takes the
engine initial build lock and makes Recast `DynamicModifiersOnly`, and a stage has no baked mesh to adopt.
The builder creates or reuses the `Human` agent's mesh, makes it `Dynamic`, calls
`RemoveNavigationBuildLock(InitialLock, NoRebuild)`, and spawns a padded `NavMeshBoundsVolume`. The build is
asynchronous; `ElysiumArena::IsNavigationReady` is the poll, and both hosts stage nothing before it.

## 2. Staging a record (both hosts)

`ElysiumArenaStage::Stage` turns the record into the def list a map would carry -- the room's 8 anchors,
its 4 cover-node rows, the record's `rows`, its `from_map` rows, its `cast` -- each row
`ElysiumArena::AuthoredRow` plus the record's own keys and nothing else, with the room's AI network plus
one node per record node row. It seeds the random streams (`ElysiumRng::SeedAll(seed)`, the engine's
`FMath::Rand`/`SRand`), then hands both to `AElysiumMapActor::RebuildStageWorld`
(`Private/Map/ElysiumMapActorLifecycle.cpp`), which tears the stage's entity world down as `EndPlay` does
and runs the map sequence: places adopted, `Load(defs)`, the player entity, the activation barrier,
`Activate`. The Unreal floor and NavMesh actors survive; every record starts from a fresh world, so handles
of the previous one go stale by epoch. The runner installs its sink on the new world right after the
rebuild (a world's teardown clears its sink), and scenario time zero is that world's `Activate`.

A map record (`"stage": "map:<map>"`) stages nothing: its cast names the map's own entities, the map is
not rebuilt, the streams are seeded at the run's start, and zero is that start.

## 3. The headless suite: `uv run elysium arena`

`uv run elysium arena [names…] [--hz 60] [--json]` (the launcher is `pipeline/src/elysium_pipeline/
arena_suite.py`) reads every record's `name`, `stage` and `expect_fail`, boots the editor once per stage
(arena first, then one boot per map record unless it says `"shares_map": true`) with
`-ElysiumArena -ArenaScenarios=<names> -ArenaOut=<dir> -ArenaHz=<n> [-ElysiumMap=<map>] -nullrhi
-unattended -UseFixedTimeStep -FPS=<n>`, merges the hosts' reports into
`$ELYSIUM_WORK_ROOT/reports/arena/<timestamp>/index.json`, prints one line per scenario with the first
unmet expectation of each failure, and exits 7 on any `fail`, `unexpected-pass` or `error`.

The host (`FElysiumArenaRun`): the arena host enters the stage world (the boot's own plan if it is one, else
`UElysiumGameFlowSubsystem::EnterStageWorld`), stands the arena once with no entity world, waits for the
Recast graph (30 s, then a harness error), and per record stages, runs and writes `<name>.trace.tsv`; the
map host waits for its map to activate and runs its records against it. Then `index.json` and
`RequestExitWithStatus` (0 when no result is `fail`, `unexpected-pass` or `error`; 1 otherwise or on a
harness error). A requested name with no record of that host, and a record that does not parse, are
`error` results in the index. The stage it builds makes resident only what its records name
(`AElysiumMapActor::bStageWithoutCatalogue`): the lab's whole-catalogue residency is ~50 s no scenario
uses.

**Bodies pose with nothing rendering.** The kernel's sequence clock (`StudioFrameAdvance 0x1008f120`,
`m_bSequenceFinished`) advances on world time and the clip length the play reported, and needs no render.
What does is everything read off the anim instance: the clip phase the animation-event walk polls
(`UElysiumEntityBodies::GetBodyClipPhase`) and the bones an attachment reads. Under `-nullrhi` nothing is
ever "recently rendered", so the host puts every bodied entity's skeletal component on
`AlwaysTickPoseAndRefreshBones` with update-rate optimisation off, every frame of a run (the compose run's
rule). `control_sequence` is the host's own check of it.

## 4. The lab

| Command | Effect |
|---|---|
| `uv run elysium gr --arena` | Lab in arena mode with the Cog lab window |
| `uv run elysium gr --arena --headless` | Adds `-GreenRoomHeadless`: no window, Cog takes no input. For MCP-driven runs |

Flags are assembled in `pipeline/src/elysium_pipeline/unreal.py`; headless keeps every `elysium.gr_*` verb.
Drive it through the `elysium` MCP server, one call at a time: console verbs through `elysium_console_exec`
(`command`), inputs through `elysium_entity_fire` (`target`, `input`, `param`).

| Verb | What it does |
|---|---|
| `elysium.gr_scenario` | Lists the records (name, stage, duration, about, known red) and the one running |
| `elysium.gr_scenario <name>` | Enters arena mode, stages the record exactly as the suite does, runs it in real time, logs each expectation as met or missed and each action and probe, then the verdict; the trace lands in `Saved/Elysium/_arena/lab/<name>.trace.tsv`. A map record is refused (it runs on its map: `uv run elysium arena <name>`). A second call replaces everything |
| `elysium.gr_scenario --stop` | Ends the running record as `error` and writes its trace |
| `elysium.gr_hints` | One line per hint on the world's hint list |
| `elysium.gr_hints --validate [npc]` | Default `arena_gunman`. A cursor-free dry run of the tactical cover search (`0x102b7110`, flags 8, cover mask 1, radius `CoverRadius`): per hint the first failing gate (not live, unusable `0x102d14c0`, class mask, distance, slot 566 `FValidateHintType`, eye trace), the validator's reason string and the numbers it computed, then the hint the walk would return. Writes neither the cursor nor a claim |
| `elysium.ai_debug_npc <name\|index\|none>` | Retail `ai_hint_focus_npc` (`0x10085480`), `DAT_10925444`: the four hint validators print their reason strings for that NPC |
| `elysium.npc_trace <name\|index\|none>` | Alias of `ai_debug_npc` that also turns on retail's `npc_task_text` prints (`0x10087b70`): "Schedule: %s", "Task: %s", "Break condition -> %s", "TaskFail -> %s", plus per-condition Set/Clear lines, through retail's trace formatter (`0x1028d990`) |
| `elysium.npc_trace_tail [n]` | Last n (default 60) entries of the world's 512-entry trace ring, between retail's BEGIN/END BUFFER DUMP lines |
| `elysium.gr_los on\|off` | Moves the player to `cover_behind` (on) or back to `cover_seat` (off) |
| `elysium.npc_brief <name\|index> [...]` | About 10 lines per NPC: identity, `state`, `schedule`, `enemy`, `conditions`, `hint`, `body`, `player`, `think`, `weapon` |

## 5. Recipes

Watch the cover record:
1. `elysium.gr_scenario cover` once navigation is ready (otherwise it says so and stages nothing). The
   staging line names 8 anchors, 4 node rows, 1 cast and an AI network of 4 nodes.
2. `elysium.npc_trace arena_gunman` for retail's own prints beside the runner's lines.
3. The log shows `expect[0] met` (`SCHED_TROIKA_TAKE_COVER_HINT (0x9b)`) and `expect[1] met` (the
   `hint+`) within ~8 s; the first `missed` line is where the record is red, and the verdict line says
   `expected-fail` while its known red stands.
4. `elysium.gr_hints` for the claim's `owner` and `next use`; `elysium.gr_hints --validate` names the
   gate and the validator's numbers for a refusal you did not expect; `elysium.npc_trace_tail 100` gives
   the schedule, break and task-fail history around it.

Add a scenario:
- Write a record under `Arena/scenarios/` (`Arena/README.md`). Cite the retail program in `about`, read
  the schedule text under `Content/ElysiumCorpus/ai/schedules/` for the real task order, and read the
  trace kinds' texts (`seam.md`) before writing a `match`. No build: `elysium.gr_scenario <name>` in a
  running lab, or `uv run elysium arena <name>`.
- A behaviour the arena cannot stage (doors, crosswalks, a map's own paths) is a map record:
  `"stage": "map:<map>"` against the map's own entities, or `from_map` to stand the map's rows in the
  arena when they are point entities.
- The room itself lives in `ElysiumArenaSpec.cpp` `Build()`, constants in `ElysiumArenaSpec.h`; keep
  `Elysium.Arm.ArenaSpec` passing. A node row is an `FNode` (`Name`, `FeetCm`, `YawDeg`,
  `HintType`, `GroupId`), appended in network order. Cover nodes are authored from the validator's
  bands: a corner node (type 10200) wants the enemy 43-60 degrees off the node's facing; a low node (101)
  wants the enemy within ~30 degrees with the block between. Read the numbers with
  `gr_hints --validate` before moving a node.

## 6. Known limits

- The trace's `state` kind does not cover the body arbiter's own flips inside `FElysiumNpcMind`, and
  `damage` is emitted even when no damage was committed; no record's verdict depends on either.
- `cond+` / `cond-` are the gather's own delta. A clear outside the gather (`SetSchedule`'s) is not
  traced, so a held condition reappears as a fresh `cond+` after a schedule change, with no `cond-`
  before it.
- A kind whose producer is still a stub (`seam.md`, lane B's list) is never emitted; an expectation on
  it can only fail.
- `player.notarget` is refused: `FL_NOTARGET` has no reader in this runtime yet.
- `from_map` refuses brush and sky rows; their geometry belongs to their map's bake.
- `gr_los on` places the occluded seat for a gunman standing on `far_ne`; a gunman that has chased the
  player keeps line of sight.
- What is red today is the spec's list (`docs/specs/0002-npc-ai/spec.md` § "The known reds"); a record
  carries its red in `known_red`, which is removed when the red is fixed.
