# Green Room arena

A controlled combat stage for watching one retail NPC behaviour at a time, driven by console verbs
and the `elysium` MCP server. It is a harness, not a map: nothing in it is retail content. Source, all
under `Source/ElysiumUE/Private/Debug/`: `ElysiumArenaSpec.{h,cpp}` (the room as values),
`ElysiumArenaBuilder.{h,cpp}` (collision, NavMesh, entity rows), `ElysiumGreenRoomArena.cpp` (build and
teardown inside the lab), `ElysiumGreenRoomConsole.cpp` (the verbs).

## 1. What the arena is

`ElysiumArena::Build()` states the room in Source units; the builder multiplies by `ElysiumMove::U`.

| Part | Value (spec) |
|---|---|
| Floor and walls | Square room, `RoomHalfExtent` 512 units; floor top at Z 0; four walls 320 high, outside the interior |
| Cover block | One solid at the centre, `BlockHalfWidth` 96, `BlockHeight` 96 (above a ~64 standing eye, so a body behind it is untraceable) |
| Anchors (8) | `intersting_place` rows named `arena_<name>`. Face anchors `cover_north/south/east/west` (rating 10, 24 units off the block face, back to it); corner anchors `corner_ne/nw/se/sw` (rating 4) |
| Pads (6) | `north`, `east`, `west`, `far_ne`, `far_nw`, `behind_cover`; all face the middle. The cover scenario uses `far_ne` |
| Cover nodes (4) | In network order: `cover_low_north` (type 101), `cover_low_south` (101), `cover_corner_ne` (10200), `cover_corner_nw` (10200). Only type and group are authored; distances, angle range and rating (3, then 2.5) are `CAI_Hint::Spawn`'s per-type defaults |
| Player seats | Start: south wall, facing the block. Cover seat: (-300, 683) cm facing +X toward `far_ne`. `gr_los on` seat: on the `far_ne` to centre line, `BlockHalfWidth` + 150 cm past the centre |

Anchors are the ambient destination system and are not cover. The cover nodes are the real thing: the
Troika cover search (`0x102b7110` via `FindHintByClassMask(8, 1, radius)`) walks the `ai_hint`s they make.

NavMesh (`ElysiumArenaBuilder.cpp`, `EnsureArenaNavMesh` and `BuildNavigation`): the project ini takes the
engine initial build lock and makes Recast `DynamicModifiersOnly`, and a stage has no baked mesh to adopt.
The builder creates or reuses the `Human` agent's mesh, makes it `Dynamic`, calls
`RemoveNavigationBuildLock(InitialLock, NoRebuild)`, and spawns a padded `NavMeshBoundsVolume`. Without the
release, `Build()` refuses (flags `0x8`) and the gunman never spawns. The build is asynchronous;
`ElysiumArena::IsNavigationReady` is the poll.

Stage entity world: `gr_scenario cover` calls `AElysiumMapActor::RebuildStageWorld`
(`Private/Map/ElysiumMapActorLifecycle.cpp`). It tears the old entity world down as `EndPlay` does, then
runs the map sequence: places adopted (the node rows become the AI network), `Load(defs)`, the player
entity, the activation barrier, `Activate`. The defs are the 8 anchors, 4 node rows and the gunman, so an
NPC spawns exactly as on a map. The Unreal floor and NavMesh actors survive. It is refused unless the stage
runtime is `Active`.

## 2. Launch

| Command | Effect |
|---|---|
| `uv run elysium gr --arena` | Lab in arena mode with the Cog lab window |
| `uv run elysium gr --arena --headless` | Adds `-GreenRoomHeadless`: no window, Cog takes no input. For MCP-driven runs |

Flags are assembled in `pipeline/src/elysium_pipeline/unreal.py`; headless keeps every `elysium.gr_*`
verb (`ElysiumCogSubsystem.cpp`). Drive it through the same `elysium` MCP server, one call at a time.
Console verbs go through `elysium_console_exec` (`command`); inputs through `elysium_entity_fire`
(`target`, `input`, `param`).

## 3. Verbs

| Verb | What it does |
|---|---|
| `elysium.gr_scenario cover [weapon] [body] [--runtime]` | Enters arena mode, rebuilds the stage world, stages `arena_gunman` (defaults `item_w_thirtyeight`, `regular_cop`) on `far_ne`, seats the player. A second call replaces everything. `--runtime` stands the same rows one at a time through `SpawnRuntimeEntity` |
| `elysium.gr_hints` | One line per hint on the world's hint list |
| `elysium.gr_hints --validate [npc]` | Default `arena_gunman`. A cursor-free dry run of the tactical cover search (`0x102b7110`, flags 8, cover mask 1, radius `CoverRadius`): per hint the first failing gate (not live, unusable `0x102d14c0`, class mask, distance, slot 566 `FValidateHintType`, eye trace), the validator's reason string and the numbers it computed (dist, enemy projection, facing vs good/bad range), then the hint the walk would return. Writes neither the cursor nor a claim (`ElysiumGreenRoomConsole.cpp` `ValidateHints`; `DryRunHintByClassMask` in `Substrate/ElysiumNpcBaseHints.inl`) |
| `elysium.ai_debug_npc <name\|index\|none>` | Retail `ai_hint_focus_npc` (`0x10085480`), `DAT_10925444`: the four hint validators print their reason strings for that NPC. No argument shows the selection |
| `elysium.npc_trace <name\|index\|none>` | Alias of `ai_debug_npc` that also turns on retail's `npc_task_text` prints (`0x10087b70`): "Schedule: %s", "Task: %s", "Break condition -> %s", "TaskFail -> %s", plus per-condition Set/Clear lines. Every line goes through retail's trace formatter (`0x1028d990`): name, curtime, message, CONDS list, memory and flag letters, NAV (`Substrate/ElysiumNpcBaseTrace.cpp`). Selecting a new NPC empties the ring |
| `elysium.npc_trace_tail [n]` | Last n (default 60) entries of the world's 512-entry trace ring, oldest first, between retail's BEGIN/END BUFFER DUMP lines (`ent_trace_dump_buffer`). Registrations: `ElysiumEntityDebugSubsystem.cpp` |
| `elysium.gr_los on\|off` | Moves the player behind the block (on) or back to the cover seat (off) |
| `elysium.npc_brief <name\|index> [...]` | About 10 lines per NPC instead of `ent_dump`'s ~450 fields (`ElysiumEntityDebugSubsystem.cpp`) |

Output shapes (shortened; values are illustrative):

```
gr_scenario: stage world rebuilt through Load - 13 def(s) (8 anchor(s), 4 node row(s), 1 gunman), AI network 4 node(s), ...
  node cover_low_north  entity 8  node id 0  class mask 1  type 101  group mask 1
gr_hints: 4 hint(s) listed, AI network 4 node(s), curtime 26.10
  #8 cover_low_north  type 101  mask 1  node 0  disabled 0  owner none  next use 0.00  rating=2.50  origin (...)
#12 arena_gunman(npc_VHumanCombatant) live hidden=0 origin=683 683 0
  schedule: SCHED_TROIKA_STEP_BACK_RANGE_ATTACK1 ...      enemy: #13 ... state_flags=0x8f
  hint: node=none cover_obj=#13 at_cover=0 shoot_at=none
  player: ... relationship=... vision=1118 in_range=1
```

`npc_brief` lines: identity, `state`, `schedule`, `enemy`, `conditions`, `hint`, `body`, `player`, `think`,
`weapon`. A non-NPC prints line 1 and `not an NPC`.

Generic `elysium_entity_fire` inputs that matter here:

| target | input | param |
|---|---|---|
| `arena_gunman` | `SetRelationship` | `player D_HT 5` |
| `arena_gunman` | `TakeDamage` | `1000` |
| `arena_gunman` | `TeleportToEntity` | `<name>` |

## 4. Recipes

Observe a cover claim (from the real run of 2026-09-30):
1. `elysium.gr_scenario cover`, once navigation is ready (see limits). Check the summary shows 4 node rows and no `MISBOUND`.
2. `elysium.npc_trace arena_gunman`. No `gr_los` step is needed.
3. Within ~8 s the trace shows `SCHED_TROIKA_TAKE_COVER_HINT (0x9b)` and `npc_brief arena_gunman` shows `hint: node=11 cover_corner_nw owner=#12`.
4. `elysium.gr_hints` shows that hint's `owner`; `next use` is still 0.
5. About 10 s later a release (next use = t + 5.0, the path-failure delay) and a claim of the NEXT hint after the cursor, `cover_corner_ne`. `gr_hints` again: read `owner` and `next use`.
6. A refusal you did not expect: `elysium.gr_hints --validate` names the gate and the validator's numbers. `elysium.npc_trace_tail 100` gives the schedule, break and task-fail history around it.

Add a scenario:
- The room lives in `ElysiumArenaSpec.cpp` `Build()`, constants in `ElysiumArenaSpec.h`. A node row is an
  `FNode`: `Name` (the hint targetname), `FeetCm`, `YawDeg` (Unreal-native), `HintType`, `GroupId`. Nodes are
  appended in network order, so position is the node id. Keep `Elysium.Substrate.ArenaSpec` passing.
- `NodeRow` emits the map-shaped def (`AuthoredRow`: `origin` in Source inches with Y negated back, `angles "0 <-yaw> 0"`)
  plus `hinttype`, `group_id`, `nodeid`. `NodePlaceRows` builds the matching AI-network rows.
- A gunman def is `CoverGunmanRow` in `ElysiumGreenRoomConsole.cpp`: classname `npc_VHumanCombatant`, targetname
  `arena_gunman`, map-row keys `model` (retail path `models/<unit>.mdl`), `stattemplate`, `additionalequipment`,
  `alternateequipment`, `player_reaction "D_HT 5"`, `hint_groups` 1..32, `npc_perception 3`, `stay_entrenched 0`,
  `allow_kick_hint_use 1`, `squadname`, `spawnflags 4`, `StartHidden 0`, `percent_occluded_*`.
- Register a new scenario name beside `elysium.gr_scenario` and hand its defs and rows to `RebuildStageWorld`.

## 5. Known limits

Sources: `docs/specs/0018-world-ai-infrastructure/story8/arena-run.md` and briefs S1, S3, S4 in the same folder.
The run recorded there predates some fixes; re-check before relying on a finding.

- Range band: the weapon range words are now retail's constructor values (`CWeaponRanged` 150 / 1024) and slot 365 is ported, so `CAN_RANGE_ATTACK1` raises. The old `.38` `TOO_FAR` finding (range 35 = 89 cm) is superseded.
- Cover nodes are authored from the validator's bands (`ElysiumArenaSpec.cpp`): a corner node (type 10200) wants the enemy 43-60 degrees off the node's facing; a low node (101) wants the enemy within ~30 degrees with the block between. Read the numbers with `gr_hints --validate` before moving a node.
- Animation events are placeholders: `DispatchAnimEvents 0x10098c80` is a stub, so `TASK_RANGE_ATTACK1` waits on an activity that never finishes and the gunman holds task 5. A missing shot is not a cover defect.
- `gr_los on` distance versus vision: the occluded seat is placed for a gunman standing on `far_ne`. A gunman that has chased the player keeps line of sight. The open seat is about 983 cm from the pad, inside `npc_perception 3` vision (~1118 cm); the old `north` pad was 1951 cm off and out of range.
- `arena-run.md` finding 1 (`SearchForCoverHint` a stub that never writes `m_pHintNode`) is superseded: it now forwards to `FindTacticalHintNode` (`ElysiumNpcSchedule.cpp`). The 2026-09-30 run saw live claims (recipe above).
- `gr_scenario` needs navigation ready: while `IsArenaNavigationReady` is false it stages nothing; repeat it once ready.
- Stale anchor handles: the map path does not refresh `FStanding.Anchors` and `FStanding.Nodes`. After a rebuild they hold handles from the old world, and `Teardown` kills whatever those handles resolve to. `Load` hands each def its def-array index, so a stale handle can alias a new entity.
- Runtime path (`--runtime`) differs from the map path: each row runs Spawn, PostSpawn and Activate at creation in the already-active world, not `Load`'s all-Spawn, all-PostSpawn, then `Activate` barrier. A second `Load` over a live world would alias handles, which is why the map path builds a fresh world.
