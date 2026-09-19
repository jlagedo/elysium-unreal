# 0018 world-ai-infrastructure — The world's AI infrastructure: Unreal navigation, authored places, the sound world and the groups every NPC queries

**Revised 2026-09-17, task-reader audit included:** Unreal owns movement pathfinding and
locomotion. AIN supplies special traversal and the runtime topology used to select gameplay
goals: hunt, cover, retreat and flank inspect neighboring nodes. Required graph data
uses compact baked records, not an actor per node/edge. This supersedes the 2026-09-15 actor-per-graph-record
decision and the universal nearest-component gate; see § Navigation boundary and story 3.

**Revised 2026-09-19:** story 2's scope is closed at five baked families — hints, places,
conversation places, makers and placed NPCs — with its bake pins; story 14 added for the four
AI logic entities no spec owned.

## Witness
Two maps, baked into their levels and standing in the editor before any NPC program runs on
them. `sp_tutorial_1`: the thug's alley — `pt1..pt3`, his `hint_groups`, the nine hull-0 jump
links, the ten graph components, the footstep and door sounds he hears. `sm_hub_1`: 38 placed
NPCs and 48 maker requests, the densest ambient population in the corpus — pedestrians walking
places and crosswalk hints, cops, makers cycling. Authored places, hints, patrol points and makers
are selectable baked actors; special traversal uses native navigation links. Compact navigation
records are inspectable in a cooked asset. Their queries are exercised independently of NPC
programs, with native movement tests for the pathfinding and traversal boundary.
Graph counts here describe the current packed exports; refresh those pins after validating the
selected patched graph. They do not assert that the installed patch uses the packed network.

## Scope
Everything an NPC queries but does not own: Unreal navigation and the retail node data its
callers observe; hint nodes; interesting places and their type table; patrol paths and patrol-point interest
records; the shared AI sound list and its volume table; squads; the attack coordinator and
standoff goals; the relationship defaults; makers; the player-law bus. For each object: its baked
actor or asset in the generated level, the substrate record the runtime builds from it at
activation, its query surface (exactly the calls the kernel's closure makes into it), and its
debugger view.

Owned elsewhere: the mind that consumes these queries — **0002**; the data seams, the class tree
and the verdict pass — **0019**; the player's law acts — **0005**, **0006** (this spec owns the
bus NPCs read, not the transaction that writes it); the scripted entities — **0003**.

Three rules, decided 2026-09-15 and narrowed for navigation on 2026-09-17:
- **No sidecars: the runtime reads only the project's own Content.** Two roots, both
  gitignored: the deployed corpus `Content/ElysiumCorpus/` — the retail bytes an
  `uv run elysium import <lane>` lifts out of the export units, where vdata, dialogue, sound
  and scenes already live — and the baked mounts `Plugins/ElysiumBaked/Content/` and
  `Content/ElysiumGenerated/`. The external export tree (`-ElysiumContentRoot`,
  `ElysiumContentPaths::Root()`, sixteen readers today against one `CorpusRoot()`) is
  offline-only; its remaining runtime readers — `.ents`, `.hulls`, `.dispcol`, the GLBs — are
  the migration still to finish, owned by the map lane's own later spec, not here. The
  placement rule, confirmed 2026-09-15: **what can stand on a map is baked onto the map as an
  actor; what cannot goes to the corpus; what is recovered from the binary is generated
  source.** The navigation exception is explicit: AIN nodes and ordinary edges describe a
  compiled network, and do not each require an actor. Hints, interesting places, patrol points
  and makers bake as actors; native nav links represent special traversal. Required network
  data lives in one cooked `UDataAsset` per map, referenced by its baked map content. Other
  entities keep the existing entity-table transport (`UElysiumMapEntities` or `.ents`, according
  to the map's migration state). Tables live under `CorpusRoot()`. The substrate reads these
  baked or deployed sources at activation. The bake and the
  import regenerate everything, so an edit made in the editor does not persist; an authored
  override layer would be a separate, named story. (`docs/vision.md`
  § "The build shape" still says the runtime builds "from the intermediates on disk"; that
  sentence is superseded by this decision and awaits the owner's edit.)
- **Every interaction reaches Unreal.** Walking, path following, traces, collision and line of
  sight go through Unreal's NavMesh, nav links and collision. Named by content → record;
  needed by the world → Unreal service. Keep only the AIN-derived data needed for node
  identities, hull positions, ordered adjacency, special links and validated route-admission
  rules. Preserve the recovered graph searches that choose gameplay goals; Unreal finds and
  follows the walking route to a chosen goal. No Source movement pathfinder, sector partition
  or hull-probe implementation is ported here. The sound list is
  a rule object and sound propagation is not reproduced.
- **A story lands only against its query surface.** Story 1 lists the calls the closure makes
  into each helper class; a story's tests are those calls against the baked witness maps, not
  a speculative API.

## Navigation boundary

Unreal builds a `RecastNavMesh` from the map's collision within `NavMeshBoundsVolume`s.
`AAIController` and path following request and follow routes; `CharacterMovement` moves the
NPC. AIN is not converted into Recast polygons or a chain of actors that an NPC must follow.
Epic's [navigation overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/basic-navigation-in-unreal-engine)
and [ANavLinkProxy](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AIModule/ANavLinkProxy)
describe the native services used at this boundary.

| Source information | Bake / runtime use | Representation |
|---|---|---|
| BSP hints, patrol points, places, makers | Preserve authored identity, position, properties and I/O | Native `AActor` subclasses with `UCLASS` / `UPROPERTY` |
| AIN jump edges for the supported NPC hull | Add hull Z offsets, convert endpoints once, retain source link identity and validate traversal in Unreal | Existing `AElysiumNavJumpLink : ANavLinkProxy`; one actor per selected connection |
| AIN node ids, type, position, relevant yaw, zone, hull offsets and resolved hint/patrol associations | Answer node-based goal and interest queries without reading exports in the game | Indexed `USTRUCT` records in one cooked navigation `UDataAsset` per map |
| Ordinary AIN edges, endpoint ids, ordered adjacency, link-info and 22 indexed hull-motion masks | Hunt, cover, retreat and flank select candidates dynamically from neighboring nodes | Compact link records and per-node adjacency in the same asset; no actor per node/edge |
| Derived per-hull/per-capability component labels | Diagnostics or a proven optimization for a particular admission branch | Optional derived data; cannot replace ordered topology or query-specific predicates |
| Remaining export metadata | Offline source evidence; add runtime fields only for recovered consumers | No blanket copy of opaque node tails or Source movement pathfinder |
| World collision, NPC blockers, doors and agent dimensions | Generate usable walkable surfaces and preserve obstacle interaction | Unreal collision, navigation areas/filters and movement services |

AIN translation is therefore **selective**. NavMesh supplies ordinary walking, but cannot
invent retail's named patrol-node identity or prove that a graph-dependent goal should succeed.
Conversely, the absence of an AIN connection cannot veto every native move: retail can build
a local route before consulting the graph (`0x102f2060 -> 0x10304130`). A component match is
also insufficient to guarantee success; endpoint binding, capability, node rejection, directional
jump checks and stale-link probes can refuse it (`0x10304e00`, `0x102f3c10`, `0x102ff960`).
The rule/engine split must keep the caller's failure code and ordering. Changing which
authored programs succeed is not authorized by this representation change.

The task-reader audit establishes a runtime topology requirement, independently of the remaining
route-admission questions. Hunt (`0x10306700`, `0x10306f60`), cover (`0x10301720`, `0x10302320`),
retreat (`0x10300b50`) and flank (`0x10302e50`) traverse adjacency to choose goals. Preserve
their candidate order, distinct predicates, runtime cooldown/ownership checks and observed RNG
draw sites. Unreal collision supplies their geometric questions; Unreal navigation moves to
the selected destinations. These searches must not be replaced by arbitrary NavMesh points.
Patrol name binding and the interesting-place eligibility pick do not themselves need adjacency.
The full export remains the offline record, including fields whose consumers are still open.
Endpoint admission, alternate movement-route costs and the remaining hint/cower families still
need recovery; that uncertainty does not block the independent BSP actor-class slice.

**Source pairing is a prerequisite for behavioral claims.** The current tutorial exports combine
an Unofficial Patch BSP with 203 node entities and a packed AIN with 116 nodes. The installed
loose patch AIN instead has 203 nodes / 429 links; the hub's patch AIN has 578 / 1,862 rather
than the export's 578 / 1,856. These are separate candidates, not proof of live selection.
BSP lump index, authored `nodeid` and network index differ; `CNodeEnt::Spawn 0x102d78d0` has distinct loaded-graph,
rebuild and standalone-hint branches. Use that lifecycle, with explicit unresolved associations,
not a dictionary keyed only by `nodeid` or an unrestricted nearest-position repair. Confirm which
network the selected install loads/rebuilds before treating a static graph as its authority.
Evidence and remaining gaps: `docs/vtmb/navigation-jump-links.md` § "Route selection and node
identity: correction (2026-09-17)".

## Entity I/O, Python and live state

The baked actor is the serialized starting data and world identity. At adoption it binds to
the existing entity world and class/input/field registry; `UCLASS` / `UPROPERTY` do not by
themselves expose the VtMB scripting API. Use 0019's generated external-name bindings and
one authoritative live entity/registry state shared by I/O, Python and NPC queries.

| Reference | Resolution contract |
|---|---|
| Entity `targetname` | Runtime entity lookup for I/O/Python; may be nonunique; preserve matching and entity delivery order, special targets and rename/removal behavior |
| Entity handle | One live entity; a bound Python input call addresses this handle rather than fanning out by name |
| Patrol hint `Group` | Exact-case, first match in retail hint-list order, type 10000 or 800; not `targetname`, and not filtered by hint enabled/owner/cooldown state |
| BSP entity index / authored `nodeid` / network index | Separate identities; preserve the loader's association and valid/absent/out-of-range outcomes |
| Unreal actor label / `AActor::Tags` | Editor organization / baked-family adoption; neither replaces VtMB entity or patrol names |

Static node/link records are cooked once. Hint disabled/owner/next-use state, node cooldown,
place occupancy/visitors, patrol iteration, search cursors and any recovered stale/rejected
link/node state are runtime state, keyed by the appropriate stable identity. Registries must
read that state, not stale copies of the actor's initial values. Preserve each query's predicate:
cover's hint-owner test differs from flank's disabled/cooldown/owner test. A global "enabled
nodes only" filter would change both patrol lookup and cover selection.

Python input calls execute synchronously; map-output actions and their Python payloads follow
the existing event queue. Field writes use the recovered write permissions and do not invent
input events. `EditAnywhere` does not grant game-Python write permission. Preserve hint `Kill`
as hide and place `Disable`'s visitor effects. Runtime entity classname/lifecycle must reflect
retail's replacement of node authoring entities with hints, alongside retained BSP provenance.
Witnesses: tutorial `logic_failed_blueblood -> blueblood_maker.Spawn` after 1.25 s; patch
`temple.py` enables `Bottleneck_Cover`; `chinatown.py` installs differently ordered patrols for
`gangster_up_1/2`. See `python_bridge.md`, `entity_io.md` and `navigation-jump-links.md`.

## Sources
- Oracle: `docs/vtmb/npc-ai/population.md` (the authored population), `senses.md` (the sound
  world, look and listen), `social.md` (squads, relationships), `schedule-kernel.md` (hints,
  interesting places, patrol paths, the standoff goal), `shape.md` § "The hint node's own words",
  § "The two hint searches and the cover forwards", § "The interesting-place wait and its loop";
  `docs/vtmb/navigation-jump-links.md`; `docs/vtmb/entity_io.md`; `docs/vtmb/vdata-catalog.md`;
  `docs/vtmb/stealth.md`; `docs/vtmb/footsteps.md`.
- Ledger: `docs/vtmb/npc-kernel/classes.md` § Helper classes, `functions.md` (the closure's
  callees on `CAI_Hint`, `CAI_InterestingPlace`, `CAI_Squad`, `CAI_PatrolPath`,
  `CAI_StandoffGoal`, the sound list and the coordinator), `entries.md` (who calls the kernel
  from outside).
- Exports (offline, consumed by the bake), V2 units: `maps/<map>.entities.glb`,
  `nav-graphs/<map>.glb`, `vdata/system/` (`interestingplacetypelist.txt`,
  `sound_volume_table.txt`, `stealth.txt`, `Rules.txt`, `npctemplate*.txt`).
- Runtime roots (what the game opens), all gitignored: the deployed corpus
  `Content/ElysiumCorpus/` (`uv run elysium import <lane>`, `ElysiumContentPaths::CorpusRoot()`,
  `pipeline/src/elysium_pipeline/importers/corpus_deploy.py`); the baked mounts
  `Plugins/ElysiumBaked/Content/` and `Content/ElysiumGenerated/` (`uv run elysium bake map`).
- Port precedent: the jump-link lane — `pipeline/src/elysium_pipeline/importers/map_jump_links.py`
  stages one record per AIN jump connection and the map bake places one actor per record
  (`Source/ElysiumUE/Private/Visual/ElysiumNavJumpLink.h`); `ElysiumEntityDefs` (runtime records
  from the entities export); `ElysiumInterestingPlaces.h` (the type table and the 5→0 rating
  pick); `ElysiumLaw.h` and `FElysiumLawEventBus`; `ElysiumNpcMaker.h`; `ElysiumRelationships.h`.

## Witness data
**The corpus, 22 maps.** 426 NPC spawn definitions over 19 classnames (317 placed, 109 maker
requests). `interesting_place_groups` on 416 definitions, 56 distinct groups; `hint_groups` on
423; `squadname` on 105 definitions, 28 squads. Densest maps: `la_hub_1` 68 + 40, `sm_hub_1`
38 + 48, `sm_warehouse_1` 53 + 2, `sp_theatre` 42, `sp_tutorial_1` 20 + 14.

**`sp_tutorial_1`, current exported graph.** Ten components (27/23/18/16/10/7/6/3/3/3 nodes); nine hull-0
jump links `22, 24, 30, 88, 110, 115, 147, 163, 218`; Jack's start has no node within 6000
units. `pt1` (`group_id 2`, `enabled 1`, `min_time 30`, `max_time 60`) at the thug's spawn,
`pt2` / `pt3` (`group_id 2`, `enabled 0`) down the alley. `thug_1`: `hint_groups 1..32`,
`interesting_place_groups 2`, `use_interesting 1`, no squad; his investigate inputs are the
player's footsteps (180/240 units) and doors; the `logic_gunfire` sounds are `sound_event 0`,
audio only. The alley has `squad_warehouse` (`thug_2`, `thug_3`); the full V2 entity unit
also authors `soc_int_squad_guard` (6 members) and `soc_int_squad_monk` (1), for three squad
names on the map.

**The census (story 1, landed 2026-09-16; `population.md` § "The AI infrastructure census").**
The export root holds 108 entity units and 100 nav graphs, not the 22 maps of the older
survey. Totals: 11,305 nodes, 29,075 links, 3,604 hull-0 jump links, 3,156 hint nodes (the
`info_node_*` family carrying `hinttype`, not `info_node_hint` alone), 1,293 places (retail
spells the classname `intersting_place`), 49 conversation places, 582 patrol points, 400
makers, 8,458 entity graph nodes, 875 placed squad members, 248 maker squad requests and 161
distinct squad names. `sm_hub_1`: 578 nodes, 1,856 links, 5 components (510 / 64 / 2 / 1 / 1),
274 hint nodes, 76 places, 34 patrol points, 48 makers and 2 squad names. `sp_tutorial_1`: 116
nodes, 234 links, 49 hint nodes, 29 places, 37 patrol points, 14 makers and 3 squad names.
Authored values are dirty — group 1 places carry `max_time` up to 23,523,235 s — and the
records must accept them as retail's float parse does. `info_node_patrol_point` carries
`target_name` and `ip_percent`: 0002/27's interest record. The query surface (§ "The query
surface of the helper classes") is 43 address-backed helper operations in seven object tables;
every row has a recovered caller and an explicit retail answer, including unnamed `FUN_`
bodies.

**Story 2's bake pins (2026-09-19, from the V2 entity units).** The census counts a patrol
point as a hint *and* a patrol point, so its two columns are not a sum: the baked families
are a disjoint partition and a patrol point is one hint actor.

| map | BSP rows | hints (patrol) | places | conversation | makers | NPCs | actors |
|---|---|---|---|---|---|---|---|
| `sp_tutorial_1` | 1,868 | 49 (37) | 29 | 0 | 14 | 20 | 112 |
| `sm_hub_1` | 2,597 | 274 (34) | 76 | 0 | 48 | 38 | 436 |
| `sp_soc_3` | 531 | 40 (25) | 13 | 2 | 0 | 15 | 70 |

Plain `info_node` rows (154 on the tutorial, 304 on the hub) get no actor. Every NPC
classname on the three maps is a registered runtime class.

## Stories
In build order. A story is done when its object is baked on both witness maps, its actor or
asset stands in the level, every query on its surface is tested against the baked level, and
its recovery is written in the oracle section it names.

- [x] **1. The infrastructure census and the query surface.** Landed 2026-09-16 (pass 3):
  `research/tooling/probes/ai_infra_census.py`, `ai_infra_surface.py`; the two sections in
  `population.md`. Pass 1 undercounted because the brief's classname rules were wrong
  (`intersting_place`, the `info_node_*` hint family); pass 2 corrected them; pass 3 retains
  `info_node`, maker-`NPCType` and squad dimensions and replaces the consumer-seed heuristic
  with the validated helper-operation surface.
  Retail: none — a reading of the exports and the ledger.
  Job: one survey over every entity export and nav graph in the V2 root: per map, the counts of
  `info_node`, `info_node_hint` by hint type, interesting places by group and enable state,
  `info_node_patrol_point`, makers by `NPCType`, squads, and every keyfield each carries; and,
  from `functions.md`, the calls the closure makes into each helper class — one table per
  object, "query, retail address, caller, what it answers". Lands as a section of
  `population.md` and the surface table every story below builds to; sizes are re-read from it.
  Size: S. Effort: Sonnet / medium.

- [x] **2. The baked infrastructure actors.** Scope closed 2026-09-19; landed 2026-09-19.
  Landed: `bake map --verify` passes on `sp_tutorial_1` (112 actors), `sm_hub_1` (436) and
  `sp_soc_3` (70); `Elysium.Content.InfraActors.*` pin the counts and assert byte-equal def parity;
  the three maps and non-baked `sm_warehouse_1` boot Active with every hint live as `ai_hint`; the
  tutorial's `logic_failed_blueblood -> blueblood_maker.Spawn` spawns through the adopted maker at
  +1.25 s. Staging covers 105 of the 108 entity units: `la_ventruetower_2`, `la_ventruetower_3` and
  `sp_giovanni_2b` are refused by the legacy lump reader every lane shares. Recovery:
  `shape.md` § "The live hint, stood", `navigation-jump-links.md` (the `CNodeEnt` rule),
  `population.md` § "Patrol tokens against the retail lookup", `seam_map_map.md` § "Import — AI
  infrastructure actors".
  Retail: a representation decision; the authored values and identity still follow the retail
  entity contract. `CNodeEnt::Spawn 0x102d78d0` removes the authoring entity on every branch
  and builds a `CAI_Hint` from the raw keyvalue block `CNodeEnt::ParseMapData 0x102d7890`
  stashes: `CNodeEnt 0x1060aaf8` carries seven keyfields, `CAI_Hint 0x106099f0` the rest
  (`group_id`, `ip_percent`, `target_name`, `StartHintDisabled`, `UserData`, the outputs).
  There is no patrol-point class — a patrol point is a `CAI_Hint` of type 10000. The maker
  hands its child every non-maker key through `m_sRefMapDataBuffer +0x76cc`. No keyfield in
  these families carries `INPUT`, so every one is read only to Python (`python_bridge.md`
  § "The write path").
  Families, a disjoint partition of the BSP rows, one actor per row: **hints** — every
  `info_node_*` / `info_hint` row carrying `hinttype`, patrol points included;
  **`intersting_place`**; **`intersting_place_conversation`**
  (`CAI_InterestingPlaceConverstation 0x1060c2c0`); **makers** — `npc_maker`,
  `npc_maker_fleshpile`, `npc_maker_zombie`; **placed NPCs** — every `npc_*` classname.
  Plain `info_node` rows and ordinary AIN nodes/edges get no actor; they are 3's cooked data.
  Other classes keep their current transport.
  Job, in order:
  1. Generated keyfields. Extend `gen_kernel_bindings` with `CAI_Hint`,
     `CAI_InterestingPlaceConverstation` and `CNPCMaker_Zombie` (the slice of 0019 story 2's
     open pass this story needs, landed here), and emit one `USTRUCT` of typed `UPROPERTY`
     fields per datamap chain — hint, place, conversation place, maker, NPC (the 268-row chain
     `CBaseEntity` … `CAI_BaseNPCTroika`). A property is named by its external, so the reflected
     name is the keyvalue; every default is zero (the `CAI_Hint` constructor `0x102d2e30` leaves
     its words zero) and display-only, because an actor emits only what it authored. Parsing is
     the runtime's own (`AiInfra/ElysiumKeyfieldAccess`: CRT `atoi`/`atof`, `ElysiumParseVec3`);
     the bake never re-implements `atof` in Python.
  2. Actors. Native `AActor` subclasses with `UCLASS`, `GENERATED_BODY` and those structs. The
     typed properties are authoritative. `AuthoredKeys` keeps every authored pair in order, repeats
     kept — datamap rows and the rest (`testflags`, patch spellings, a blank key) — and `Outputs`
     the output rows. The def is rebuilt by walking `AuthoredKeys`: a key whose property still
     holds what its string parses to re-emits the string byte for byte, a changed property emits
     its typed value, an unauthored row is emitted once non-zero. The maker adds a
     `ChildTemplate` (the NPC struct), so the child's keys are typed too and still re-emit as
     authored. Patrol `Group` is an `FString`: 328 exact-case tokens. Bake-only
     `UFUNCTION` setters take raw strings. Standard editor billboards/arrows, Details
     categories and Outliner folders; no custom visualizer or per-frame editor system. The
     actors are runtime content; only their visualization components are editor-only.
  3. Stage and bake. A new importer (`importers/map_ai_infra.py`) stages one record per row
     (index, family, classname, ordered keys, outputs) from the same legacy pair reading the
     entity table is built from, and refuses a family row whose reading differs from the unit's
     structured pairs inside the map-geometry manifest with a
     content hash and a level-recipe entry. The map bake places the actors; `bake --verify`
     fails on a missing, duplicate or wrong-family actor. Tags live in `ElysiumBakedTags`
     (one per family, plus the existing `EntityIndex(i)`); the jump link's undeclared
     `elysium.nav-jump` is declared with them. Nothing baked is committed.
  4. Adoption. `AdoptBakedLevel` buckets the actors by entity index. A replacement pass in
     `AElysiumMapActor::LoadMap` runs after the defs load and before the model-preload walk and
     `EntityWorld::Load`: it rewrites the def at the original BSP index in place, for both
     `UElysiumMapEntities` and `.ents` — never appended, filtered or renumbered. An actor
     standing on its def's origin leaves it as it is; one moved in the editor gives the def its
     location and a rewritten `origin` keyvalue (no row is staged in the skybox miniature, so no
     sky transform is involved); `Times` 0 → −1 once. A missing, duplicate or
     classname-mismatched actor fails the load and builds no entity world; a level baked without
     the infrastructure table leaves the pass inactive.
  5. Entity classes, per § Entity I/O, Python and live state: one live runtime state that
     every input, field and task reader observes. The hint class serves the whole hint family:
     generated bindings, `EnableHint` / `DisableHint` / `Walk` / `DontWalk` / `SetUserData`,
     `Kill` as hide (`0x102d08c0`), a hidden hint swallowing inputs, the `HintSpawn` defaults.
     `HintWords()` and `FindHintByName` read that state; patrol `Group` resolves exact-case,
     first match in hint-list order, type 10000 or 800. Node binding stays explicitly
     unresolved (`-1`) until 3, and every `HintWords()` caller is audited so no arm answers
     before its search (4) exists. The conversation place is registered with its bindings and
     three inputs. `npc_maker_zombie` is registered with its three fields. NPC placements feed
     the existing NPC classes; a classname with no runtime class stays the record it is today.
  Checks: spawn each class through `UWorld`, verify reflected fields and registered editor
  components; save/reload and compare fields, `AuthoredKeys` order and repeated outputs; apply →
  emit → parse round trips on dirty values (`23523235.0`, `.5`, `"0  0 72"`, `"-3496,92"`);
  index-preserving replacement over both transports; a maker child def identical with and
  without the actors; one runtime entity per def index before any spawn-time I/O. Bridge:
  name lookup fans out over a repeated targetname and a bound-handle call does not; a Python
  field write on any of these keyfields is refused; `DisableHint` and a place's `Disable`
  called from Python become visible to the NPC query; exact-case `Group` on two tokens
  differing only in case; tutorial `logic_failed_blueblood -> blueblood_maker.Spawn` still
  fires after 1.25 s through an adopted maker.
  Test maps: staging runs over all 108 entity units with no editor — every row in exactly one
  family, none dropped, order and repeats kept, every dirty value staged. Bake goals, each
  passing `bake map --verify`, a baked-level content test against the pins in § Witness data,
  a def-parity test (actor-rebuilt defs against the transport's at the same indices) and a
  load: `sp_tutorial_1` and `sm_hub_1` (required), `sp_soc_3` (the only V2 map with
  conversation places). The zombie and fleshpile makers, `info_hint` and `info_node_kick_over`
  stand on no V2 map yet; their real authored rows are the fixtures of runtime unit tests.
  Consumes: 0019 story 2 (its open-pass slice lands here). Provides: the actor the debugger
  (12) selects and the editor shows; the live hint entity to 4; the placed-NPC actor.
  Oracle: `shape.md` § "The hint node's own words" (the `CAI_Hint` / `CNodeEnt` datamap
  tables), `navigation-jump-links.md` (the `ParseMapData` stash), `population.md`
  § "Classnames outside the census families".
  Size: L. Effort: Opus / high (generator, pipeline stage, editor bake, runtime bind).

- [ ] **3. Unreal navigation and the required AIN data.** Absorbs 0002's stories 22 and 24.
  Retail: `SetGoal 0x102ecd20` selects among route branches, not one universal graph test.
  `0x102f2060` may accept a local route first; `BuildNodeRoute 0x10304e00` requires endpoint
  binding and node connectivity on its ordinary branch. Interesting-place goal type 8 takes
  the node-route path; patrol node lookup itself can fail before routing (`0x102aa640`).
  Preserve each caller's failure and timing, including `0x1d` for missing patrol nodes and
  `0x0c` for a refused route. See § Navigation boundary and the oracle's 2026-09-17 correction.
  Hull 0 is Source `(-13,-13,0)..(13,13,72)` with an 18-unit step. `InitLinks 0x102fb4e0`
  includes `MONSTERCLIP` and excludes `MOVEABLE`; `0x102ff960` checks link-off `0x1000`, hull
  motion intersected with capability, rejected nodes, directional jumps and stale links.
  `linkInfo & 0x2000` also affects alternate-route cost (`0x102fe9f0`), not just connectivity.
  Port today: `AElysiumNpcBody::MoveTo` calls `AAIController::MoveToLocation` with native
  pathfinding. `map_jump_links.py -> bake_map_v2.py -> bake_jump_links.py` already bakes
  `AElysiumNavJumpLink` smart links; native capsule/CharacterMovement handles flight and
  landing. There is no baked node-query/admission asset. The old spec's unconditional
  nearest-component check was an unproven approximation and is withdrawn.
  Job, in order:
  1. Verify the source pair and loaded/rebuilt network for each witness. Preserve BSP order,
     distinguish authored IDs from network IDs, reproduce the node/hint association lifecycle,
     and report unmatched nodes without inventing a nearest replacement. Structural export
     counts alone are not the verdict on patched-map reachability.
  2. Stage the node-query data in one cooked `UDataAsset` with typed `USTRUCT` rows: stable
     network index, node type, position/relevant yaw, zone, per-hull offsets, and entity/hint
     association; links with endpoint ids, link-info, all 22 indexed hull-motion masks, and
     adjacency in the order retail installs it. Decode named fields from the variable-width
     export; never equate an opaque tail offset or a derived component id with the retail zone.
     Retain map/source identity, used-hull bits and recipe version with the asset. Keep authored
     `nodeid` separately as provenance. Reference the asset from baked map content so cooking
     includes it; read it before node-dependent spawn
     and activation. No runtime GLB/AIN read, actor per ordinary node/edge, or per-node tick.
  3. Reuse the existing jump-link lane. Select by the actual supported hull/capabilities;
     keep all 22 source slots correctly indexed offline, add the appropriate ground-node Z
     offset, convert Source inches to Unreal cm once, and configure native traversal links.
     A shared AIN edge does not prove both jump directions are physically legal. Preserve the
     native directional probe, traversal state, cancellation and failure contract. Fly/climb
     edges need their own proven consumers; never reinterpret them as walking or jumping.
  4. Expose compact topology and shared runtime state to the recovered hunt/cover/retreat/flank
     goal selectors. Preserve per-query link/hint predicates, neighbor and tie order, node
     cooldown, rotating search cursors and observed RNG sequencing. The programs remain in
     0002; the shared query objects are here. Component labels may assist diagnostics or a
     proven admission optimization, but never replace these goal searches. Separately finish
     local-route gates, endpoint binding and alternate-route effects before declaring movement
     admission faithful. Same-component is not proof of a legal route. Unreal supplies geometry,
     traces, movement pathfinding and following; no second movement pathfinder is implemented.
  5. Configure native agent dimensions and NPC-blocking monsterclip geometry; verify door
     interaction without creating artificial navigation cuts. Consume cooked world/brush
     collision from the map lane. A complete Recast path may still take an unintended shortcut,
     so compare the relevant route restrictions as well as start/end connectivity.
  Acceptance: both witness levels save/reload the node data and link actors with no external
  export access; their BSP actor set contains no duplicate graph-marker actors. Validate node
  binding (including standalone hints, duplicate authored IDs and out-of-range associations),
  all hull indices, source-pair/recipe invalidation, and native link endpoints. Exercise a legal
  local move with no graph coverage, a graph-dependent refusal, a same-component move blocked
  by collision, different capabilities on the same hull, directed jump refusal, and door versus
  monsterclip behavior. Reuse the existing native jump-flight tests. Missing source data is
  distinct from a valid empty graph; neither becomes global permission or a blanket movement
  veto. Incomplete required data prevents publication of the affected navigation slice.
  Query tests must compare the selected node/list and state writes under controlled neighbor
  order, capabilities, hint ownership, node cooldown, query cursor and RNG; matching only the
  final reachable component is insufficient. Include shipped hunt setup, `SHOT_BY_UNKNOWN`,
  `MELEE_RETREAT` and flank program consumers. Use each family's own predicate, not one merged
  eligibility function. Preserve hint-list order separately from BSP/entity and neighbor order.
  Current packed-export census pins: tutorial has 116 nodes / 234 links / 9 hull-0 jumps and
  ten hull-0 ground-or-jump components; hub has 578 / 1,856 / 119 and seven components
  (509 / 63 / 2 / 1 / 1 / 1 / 1). Story 1's seven/five components mix all hulls and are not
  these profiles. Recompute pins for the verified source pair rather than forcing a patch graph
  to match packed counts. The unit's variable-width node tails are not fixed six-integer records.
  Provides: node-goal data and route outcomes to 0002 and 0003; Unreal supplies movement.
  Consumes: 2 for hint/patrol associations; the existing jump lane is independently reusable.
  Oracle: `navigation-jump-links.md`. Open: patched-install graph selection, complete endpoint
  filters and local-route gates, alternate-route cost/waypoint semantics, hint-path/cower query
  arms and the native door/monsterclip witness. Runtime adjacency is now a confirmed requirement;
  precise geometric constants and remaining task wrappers still need their own closure. This
  story remains incomplete until those required consumers are resolved; the actor-class slice
  does not claim to solve them.
  Size: M–L. Effort: Opus / high.

- [ ] **4. Hint nodes.**
  Retail: `CAI_Hint`'s datamap (`shape.md` § "The hint node's own words"), the three
  validators (`0x10295ed0`, `0x102961a0`, `0x10296c40`), `FValidateHintType` slot 566 with its
  ten species bodies, the two hint searches and the cover forwards (`0x10365780`, `0x103bfa50`,
  `0x10297430`, `0x102974f0`), `hint_groups` as a 1-based index list → mask (empty = all), the
  expiring lock list bosses keep on hints (`0x103662d0`, `0x10366400`, `0x10366490`).
  Gap: the port has the rule bodies (`ElysiumNpcKernelHints*.cpp`) and no registry under
  them — every search answers over nothing.
  Job: the hint registry as records (type, group mask, yaw, optional network-node association, in-use
  owner); the queries on the surface from 1 — nearest by type, group and distance; cover from
  enemy; shoot-at hint; lock and unlock with expiry; the validators rehomed onto the records.
  Graph-based cover/flank searches consume 3's topology and runtime node cooldowns. Cover's
  owner-only hint test and flank's full unusable test remain distinct. Python/I/O writes share
  this registry's state, including `EnableHint` / `DisableHint` and hide/unhide; hint `Kill`
  follows retail's hide override rather than removing its graph identity.
  The species halves of slot 566 stay with their classes in 0002.
  Provides: the searches to 0002's cover, kick, interest and alert families. Consumes: 2, 3.
  Oracle: `shape.md` (the four hint sections), `schedule-kernel.md` § "The three hint
  validators". Size: M. Effort: Opus / high.

- [ ] **5. Interesting places.**
  Retail: the place entity (`group_id`, `enabled`, `min_time` / `max_time`,
  `match_orientation` `+0x570`, holster `+0x571`, `DISAPPEAR`), eligibility `0x102dad60`, the
  type table `interestingplacetypelist.txt`, the 5→0 rating pick, `Enable` / `Disable`, the
  disable-or-kill walk over visitors (`0x102daac0`: a visitor carrying `DISAPPEAR` gets
  `flags2 |= 0x80000008`, any other `TaskFail(0x23)`, both then slot 614 on the visitor), the
  entry / loop / release trio (`0x102a9f40`, `0x102aa210`, `0x102da600`), and the outputs
  `OnInterestingPlaceArrived` / `Left` on the visitor.
  Gap: `FElysiumInterestingPlaceTable` and the rating pick exist; there is no place registry,
  no visitor list, no `Enable` / `Disable` walk; the port's ambient executor claims spots by
  its own rules (0002's 11 retires it).
  Job: the registry with claims and visitors; the selection query (group mask, eligibility,
  the pick); the walk; the trio as the registry's API; the two outputs. The programs
  `0xff` / `0x100` / `0x102` and the selector arms stay in 0002's story 11.
  Provides: the trio to 0002's 11 and 27. Consumes: 2, 4.
  Oracle: `schedule-kernel.md` § "Interesting places: the selector, the programs, the wait",
  § "Interesting-place eligibility", `shape.md` § "The interesting-place wait and its loop".
  Size: M. Effort: Opus / high.

- [ ] **6. Patrol paths and the patrol-point interest record.**
  Retail: `CAI_PatrolPath` and `info_node_patrol_point`; the interest record at `node->+0xa0`
  (`+0x468` the name of a `CAI_InterestingPlace`, `+0x46c` a 0–99 chance) resolved by
  `0x1029f730` and `0x1029f780`.
  Gap: nothing — no path record, no node record.
  Job: the path records and the point's interest record; the queries the patrol program asks
  (next point, hunt path, the record). The roll, the two interest tasks and the programs stay
  in 0002's 10g and 27.
  Consumes: 2, 3, 5. Oracle: `programs.md` § "Patrol paths, walked" and
  `navigation-jump-links.md` § "Route selection and node identity". Story 1 identifies
  `target_name` / `ip_percent`; the hint's network association and hull-adjusted destination
  come from 3. The path is the program's ordered goals; native navigation finds each walking
  route. Hunt-list builders use 3's ordered runtime adjacency and their recovered directional
  selection; retain their RNG and list/terminal-node distinction. `GET_FULL_PATROL_PATH` reads
  only the current node, while `TASK_PATROL_PATH` itself reads no graph/path object (2026-09-17
  task audit). Unrecovered: the upstream route expected by `TASK_PATROL_PATH`, remaining
  endpoint filters and any unclosed task wrappers; see the oracle's task-reader matrix.
  Size: S–M. Effort: Fable / medium; corpus pass on the node keys first.

- [ ] **7. The AI sound list and its volume table.**
  Retail: one shared list every emitted noise is written into — type, volume from
  `sound_volume_table.txt`, origin, owner, start and expiry — and the listen sweep
  (`0x102b1cd0`) reads; the nine per-NPC last-heard `CSound` slots (`+0x60b0..+0x6230`) are
  copies the sweep writes, never the list itself. Hearing is a query against the list:
  distance × hearing scalar × the stealth table, conditions raised 0.2–0.9 s late, never a
  memory record.
  Gap: the port has the per-NPC slots and the sweep (0002's 10a) and no shared list —
  footsteps and doors are wired to the NPC point to point.
  Job: the list as a world object with expiry; the producers (footsteps, doors, weapons,
  combat, physics danger, flinch, `ambient_generic` with `sound_event`) writing into it; the
  audibility query; 10a's sweep re-read from the list. A scene test emits a footstep at 180 /
  240 units and asserts what the thug's slots receive.
  Provides: the list to 0002's senses. Consumes: 2.
  Oracle: `senses.md` (the sound world), `docs/vtmb/footsteps.md`, `docs/vtmb/stealth.md`.
  Unrecovered: the list's capacity and eviction order.
  Size: M. Effort: Opus / high.

- [ ] **8. Squads.** The object half of 0002's story 17, moved here; 17 keeps the consumers.
  Retail: one shared `AI_Enemies` memory. Joining (`squadname` + `bits_CAP_SQUAD`, `InitSquad`
  `0x10273d30` / `SetSquad` `0x1029a930`) points `m_pEnemies` at `squad+8`; `GetEnemies()`
  (`0x10273e10`) diverts to `g_DisconnectedEnemies` while `m_iSquadDisconnected` is nonzero;
  `DisconnectFromSquad` (`0x1026d050`) wipes that global and increments; `ReconnectToSquad`
  (`0x1026d0c0`) decrements, re-adds at 0, clears `D_DISCONNECT_SQUAD`; `LeaveSquad` is empty.
  16 members, the 17th overwrites the 16th; `GetMember` returns NULL for all when member 0 is
  disconnected; `Event_Killed` compacts; membership rebuilt on restore from `squadname`. The
  strategy-slot namespace ships dead (do not build).
  Gap: no squad object — `ConnectedSquad()` answers null; the refcount lives on
  `ScheduleHost.SquadDisconnected`.
  Job: the squad object sharing 0002/5's record store; membership from `squadname`; the cap
  and the overwrite defect; the disconnect refcount and the disconnected global replacing the
  seams; the slot space. `COND_SQUAD_SEE_ENEMY`'s producer and the two tasks stay in 0002/17.
  Consumes: 0002/5. Oracle: `social.md` § "Squads, decoded". Unrecovered: the six `CAI_Squad`
  memory-forwarding wrappers (names only).
  Size: M. Effort: Opus / high.

- [ ] **9. The attack coordinator and the standoff goal.**
  Retail: Troika slots 600 / 601 acquire and release a melee slot on the coordinator for a
  `CBaseEntity*`, 608 binds it by name (`"Normal"`); `CAI_StandoffGoal`'s two inputs
  (`0x102c87a0`, `0x102c8830`), `UpdateOnRemove 0x102cdc50`,
  `CAI_StandoffBehavior::TranslateActivity 0x102c79e0`.
  Gap: "this substrate has no coordinator object" — the melee bodies are ported against a null.
  Job: the coordinator as a world object with named instances and melee slots; the standoff
  goal entity as a record with its inputs. The bodies that call them stay in 0002.
  Consumes: 2. Oracle: `shape.md` § "The tables" (600–616), `schedule-kernel.md` § "Story 29d,
  family Motor10" (the standoff sections). Unrecovered: the `"Normal"` instance's slot count
  and refill rule — a read of its body before this lands.
  Size: S–M. Effort: Opus / medium.

- [ ] **10. Makers and templates.**
  Retail: `CNPCMaker`, `CNPCMaker_Fleshpile`, `CNPCMaker_Zombie`; `npctemplate*.txt` (150
  declarations in 36 files); the child inherits `NPCTargetname`, `NPCSquadname`, the forwarded
  outputs; count, frequency and `Flag_*` lifecycle.
  Gap: `ElysiumNpcMaker` exists and spawns; its keyfields are hand-typed.
  Job: the maker as a baked actor the runtime adopts, keyfields from the datamap seam, the
  inheritance and lifecycle rules verified against the hub's 48 requests and the tutorial's 14.
  Consumes: 2, 0019 story 2. Oracle: `population.md` § "How a map defines an NPC",
  § "Templates and inheritance". Size: S–M. Effort: Sonnet / medium.

- [ ] **11. Relationship defaults and the player-law bus.**
  Retail: `AddClassRelationship`, `Rules.txt`; `SetRelationship` (native `D_*` table),
  `SetDisposition` (stance) and `reaction.txt` (RPG score) kept separate; the player's law
  levels, offenders and the closest-NPC cache on `CBasePlayer` (`0x101828b0`, `0x10182a90`).
  Gap: `ElysiumRelationships`, `ElysiumLaw` and the law event bus exist.
  Job: the class-relationship table loaded once from vdata; the bus as the world object NPCs
  read (levels, offender, location, timers). The acts stay in 0005 / 0006, the witnessing in
  0002.
  Oracle: `social.md`, `population.md` § "Player-law observation transaction". Size: S.
  Effort: Sonnet / medium.

- [ ] **12. The infrastructure debugger view.** Visual-only modernization, as 0002's 23.
  Job: the overlay draws hints (type, group, locked), places (enabled, claimant, visitors,
  wait), patrol paths, the sound list with expiry, squads and slots, coordinator slots, graph
  components and the last refusal. Select authored objects through their baked actors and
  inspect network data through the cooked asset; graph diagnostics do not require graph actors.
  Consumes: 2–9. Size: S. Effort: Sonnet / medium.

- [ ] **13. The hub at idle: the second witness.**
  Job: `sm_hub_1` and `sp_tutorial_1` on the V2 lane (0002's 22, moved here) with every
  object above baked; a scene test per map that opens the baked level and walks each record's
  queries against it — places per group, hints per type, node associations and route outcomes, audibility at a set of
  origins — and a played witness once 0002's idle programs land: pedestrians visit places, cops
  patrol, makers cycle, and each stood route branch preserves its success/failure contract while
  Unreal performs movement. Do not turn component boundaries into a universal movement ban.
  Consumes: everything above. Size: M. Effort: Sonnet / medium, then played.

- [ ] **14. The AI logic entities.** Added 2026-09-19: found by the classname scan beside
  story 2, outside story 1's census families, owned by no spec.
  Retail, bodies walked 2026-09-19 (`entity_io.md` § "The AI logic entities"):
  `CLogicNPCCondition 0x1057748c`
  (`logic_npc_condition`: `condition`, `target_npc`, input `Test`, outputs `OnTrue` /
  `OnFalse`); `CLogicSquadCondition 0x105775b0` (`logic_squad_condition`: `condition`,
  `squad_name`, the same input and outputs); `CAI_ChangeTarget 0x1059e044`
  (`ai_changetarget`: `m_iszNewTarget`, input `Activate`); `CAI_DynamicLink 0x10608f58`
  (`info_node_link`: `startnode`, `endnode`, `initialstate`, inputs `TurnOn` / `TurnOff`);
  and the conversation place's think pair (`WaitThink` / `TalkThink`,
  `CAI_InterestingPlaceConverstation 0x1060c2c0`), whose actor and class 2 stands.
  Witnesses, 12 rows in the corpus: `hw_tawni_1` — two `check_condition` rows test
  `COND_SEE_PLAYER` / `COND_HEAR_PLAYER` on `npc_tawni_boyfriend`, `OnTrue` runs
  `setSpotted()`, `SetRelationship player D_HT 5` and kills the checker;
  `la_ventruetower_1b` — `floor_2_vis_check` tests `COND_SEE_PLAYER` over squad `floor_2`,
  `OnTrue -> stealth_failed.Trigger`, and one `logic_npc_condition`; `la_bradbury_1` —
  `Evelyn_target` / `Mabellene_target` retarget their NPCs to `!player`; `sp_giovanni_4` —
  six links (`startnode 17 -> endnode 18`, `16 -> 19`, …). Conversation places: 49 rows on
  17 maps.
  Gap: none of the four is registered; they are inert records and their wires do nothing.
  Job: generate their bindings; register the classes against the walked bodies. They are
  point logic entities on the entity table, not baked actors. `info_node_link` sets and clears
  3's link-off bit `0x1000`, applied once when the graph is ready and edge-triggered after;
  `logic_squad_condition` reads 8's squad — any member, and nothing while member 0 is
  disconnected; both conditions resolve their id at Activate and test the raw `m_Conditions`.
  The conversation place is a `CPointEntity` sibling of the place, not a subclass: it writes
  `m_Mode` on 5's one-slot places, needs two occupied, and its too-close arm runs the place's
  `Disable` visitor walk; keep the pick's `RandomInt` draw count. `logic_npc_condition` and
  `ai_changetarget` wait on no story here. Tests use the authored rows above as fixtures.
  Consumes: 2, 3, 5, 8. Oracle: `population.md` § "Classnames outside the census families";
  each body's recovery lands in `entity_io.md`. Size: S. Effort: Sonnet / medium.

## Build order
The node/goal chain is 1 → 2 (relevant 0019 story 2 bindings before adoption) → 3 → 4 → 5 → 6.
Stories 7 and 9 can follow 2 independently; 8 waits for 0002/5; 10 waits for 2 and the relevant
bindings; 11 follows its existing data/services. The debugger (12) and full witness (13) follow
their listed dependencies. 14's condition and retarget entities wait on nothing here; its
link, squad and conversation arms follow 3, 8 and 5. Story 1 establishes the corpus sizes. 0018 runs beside 0019
and consumes 0002 only through the query surface: no story here adds a member to the NPC.
The native actor-class/reflection/save slice of 2 can precede binding completion; AIN selection,
node-query data and admission are 3's work, not prerequisites for constructing those classes.

## Seams
- Provides: the query surface to 0002 — hint searches, place selection and the trio, the next
  patrol point and its record, audible sounds, squad members and the shared memory, the melee
  slot, node-goal data and route outcomes; the baked actors and navigation asset to the debugger;
  the second witness. NavMesh/path following/CharacterMovement supply movement.
- Consumes: 0019's generated datamap bindings (every infrastructure keyfield) and tunables;
  0002/5's enemy record store; 0005 / 0006's law acts; the V2 export units through the map
  bake, never at runtime.
