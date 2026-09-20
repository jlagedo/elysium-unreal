# 0018 world-ai-infrastructure — The world's AI infrastructure: Unreal navigation, authored places, the sound world and the groups every NPC queries

**Revised 2026-09-17:** Unreal owns movement pathfinding and locomotion; no actor per graph
node or edge; the universal nearest-component gate is withdrawn. (That revision also kept the
graph's topology at run time for the goal searches; 2026-09-20 below supersedes it.)

**Revised 2026-09-19:** story 2's scope is closed at five baked families — hints, places,
conversation places, makers and placed NPCs — with its bake pins; the AI logic entities no
spec owned gained a story; the brush world answers each retail mask (movement, sight,
pedestrian volume) by its own contents word, with one NavMesh agent per shipped hull (rat
included) and the mesh baked into the level.

**Revised 2026-09-20, the engine / behaviour cut:** Unreal decides HOW an NPC gets somewhere;
retail decides WHERE it wants to go and what a failure means. Source's movement layer (graph
routing, link tests, node binding, move probes, the simplifier, stale links) is not ported;
AIN is translated at bake time into Unreal's own navigation data and a set of places. Stories
are renumbered in execution order — a story's number is its place in the order. Old → new:
2b → 3; 3 → 4 (places), 5 (navigator and seam), 6 (geometry), 7 (traversals), 9 (selectors),
12 (flying); 4 → 8; 5 → 10; 6 → 11; 7 → 13; 8 → 14; 9 → 15; 10 → 16; 11 → 17; 14 → 18;
12 → 19; 13 → 20.


## Witness
Two maps, baked into their levels and standing in the editor before any NPC program runs on
them. `sp_tutorial_1`: the thug's alley — `pt1..pt3`, his `hint_groups`, the patch graph's 25 human and
36 rat jump links, the footstep and door sounds he hears. `sm_hub_1`: 38 placed
NPCs and 48 maker requests, the densest ambient population in the corpus — pedestrians walking
places and crosswalk hints, cops, makers cycling. Authored places, hints, patrol points and makers
are selectable baked actors; special traversal uses native navigation links. Compact navigation
records are inspectable in a cooked asset. Their queries are exercised independently of NPC
programs, with native movement tests for the pathfinding and traversal boundary.
Graph counts are those of the patch install's own graphs, the ones retail loads (story 4).

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
  the migration still to finish, owned by the map lane's own later spec, not here (story 3
  takes only the slice navigation needs: the contents signatures and the level actor). The
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
  needed by the world → Unreal service. Keep only the AIN-derived data behaviour reads: node
  identities, positions at hull height and hint associations — the place set. The goal
  selectors keep retail's tests over those places; Unreal finds and follows the walking route
  and answers reachability. No Source movement pathfinder, sector partition
  or hull-probe implementation is ported here. The sound list is
  a rule object and sound propagation is not reproduced.
- **A story lands only against its query surface.** Story 1 lists the calls the closure makes
  into each helper class; a story's tests are those calls against the baked witness maps, not
  a speculative API.

## Navigation boundary

Decided 2026-09-20 by the owner: **do not rebuild Valve's Source engine inside Unreal.** A
difference in how an NPC travels is acceptable; a missing behaviour is not.

Every retail navigation function falls into one of three kinds, by four tests — would it
survive if Valve had shipped a nav mesh; does it speak of enemies, cover, hints, doors and fail
codes, or of node ids, links, heaps and trace fractions; could a designer at Troika have meant
it; and, for a bug, is it above the seam (kept: programs were tuned against it) or below it
(noise).

| Kind | Rule | Members |
|---|---|---|
| **Engine** | Unreal replaces it whole. No port, no tests; the oracle keeps the record. | Graph load and rebuild, zones, A* and its costs, nearest-node binding and its cache, the local-route builder, the move probe, `Triangulate`, the jump arc integration, the path simplifier, the motor, stale links, trace plumbing |
| **Behaviour** | Ported verbatim, retail constants and order. | Schedules, tasks, conditions, fail codes; goal types and tolerance rules; the route-failure retry window; what makes a place valid cover, a shoot node, a retreat; hint claim / release / lock timings; the door policy and its 5 s / 20 s timers; the crosswalk wait and its queue rule; the 0.1 s think and the 0.8 s gate |
| **Source-shaped data** | Its MEANING is translated at bake time; Source's structure is never interpreted at run time. | Brush contents → collision signatures; hulls → NavMesh agents and capsules; AIN jump edges → nav links; "a link crosses this door" → a door NPCs may use; crosswalk node pairs → nav links; `0x2000` volumes → a priced nav area; nodes → a set of places |

AIN plays two roles and they separate cleanly. Where retail uses the graph to ENUMERATE places
— a patrol point, a hint's stand position, cover candidates — the places and retail's tests
are kept. Where retail uses it to CONNECT places — routing, reachability, zones — Unreal
answers. So the run-time asset holds places and no links, zones, masks or adjacency.

| Retail concept | Unreal-native form | Story |
|---|---|---|
| Node-graph routing, per hull | One baked `RecastNavMesh` per agent; `AAIController` path following; crowd avoidance (already the body's controller) | 3, 5 |
| Clip, sight-only and pedestrian brushes | Collision bodies by contents signature; a sight channel; a nav area | 3 |
| Nodes, hint and patrol positions | The place set, a cooked asset of points | 4 |
| Goal, tolerance, fail codes, retry window | A navigator object on the NPC, above the movement seam | 5 |
| Traces, hull fit, stand test, reachability | Unreal sweeps, NavMesh raycast and path tests behind the seam | 6 |
| Jump edges | `AElysiumNavJumpLink` smart links (landed), per agent | 7 |
| Doors the graph runs through | A smart link through the doorway, enabled while an NPC can open the door; the rest cut the mesh | 7 |
| Crosswalk pairs and `Walk` / `DontWalk` | A smart link between the two curb places; the wait runs at the curb | 7 |
| Cover, flank, back-away, hunt, cower | Functions over the place set with retail's predicates; reachability from Unreal | 9 |
| Flying NPCs (no fly link ships) | A flying gait that steers straight at its goal | 12 |

**Named modernizations** (each a decision, so none is a defect): the route's shape is
Unreal's; NPCs can reach floor retail's sparse graph never covered (story 4 reports every
authored place that gains or loses reach, to judge case by case); a selector's pick may differ
from retail's among equally valid places — the rules a pick must satisfy are retail's, the
iteration order is not; exact tie order and RNG stream parity are not reproduced (retail's own
cover and shoot-node order turns on a process-global written by every NPC), while the draws a
ported function makes stay where retail makes them; a stale link becomes the door's own
failure timer; a door is handled when the NPC reaches it, not from the simplifier's
look-ahead.

**NPCs use only the doors retail's graph runs through** (decided 2026-09-20): 8 of the
tutorial's 36 door brushes, the smoke-shop pair on the hub. Designers chose them; opening more
changes which encounters can reach the player.

Evidence: `docs/vtmb/navigation-jump-links.md` — the 2026-09-19 sections record every engine
body named above, and § "What the shipped graphs and maps actually use" the censuses these
decisions rest on (no link flies or climbs; link-off is unreachable by content; the rat hull
is not a subset of the human one; zones end at walls and link-less doors).

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

Static place records are cooked once. Hint disabled/owner/next-use state, place cooldown,
interesting-place occupancy/visitors and patrol iteration are runtime state, keyed by the
appropriate stable identity. Registries must
read that state, not stale copies of the actor's initial values. Preserve each query's predicate:
cover's hint-owner test differs from the shoot node's disabled/cooldown/owner test. A global "enabled
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

**The witness graphs, as retail loads them (the patch's own loose AINs).** `sp_tutorial_1`: 203
nodes, 429 links; the human hull uses 388 (363 ground, 25 jump) in twelve components
(63/27/23/22/18/16/10/7/6/3/3/3), the rat hull 428 (392 / 36) in nine. `sm_hub_1`: 578 nodes,
1,862 links; human 1,763 (1,646 / 117) in 509 / 63 / 2, rat 1,862 (1,759 / 103) in 510 / 64 / 2.
Both maps: every node a ground node, `UsedHullBits 0x80001`. `pt1` (`group_id 2`, `enabled 1`, `min_time 30`, `max_time 60`) at the thug's spawn,
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
bodies. The graph figures of this paragraph are the packed exports'; story 4 re-exports from the
graphs retail loads (97 non-empty: 11,558 nodes, 29,523 links).

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
Stories run in the order listed. A story is done when its object is baked on both witness maps, its actor or
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
  Plain `info_node` rows and ordinary AIN nodes/edges get no actor; they are 4's cooked data.
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
     unresolved (`-1`) until 4, and every `HintWords()` caller is audited so no arm answers
     before its search (8) exists. The conversation place is registered with its bindings and
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
  (19) selects and the editor shows; the live hint entity to 8; the placed-NPC actor.
  Oracle: `shape.md` § "The hint node's own words" (the `CAI_Hint` / `CNodeEnt` datamap
  tables), `navigation-jump-links.md` (the `ParseMapData` stash), `population.md`
  § "Classnames outside the census families".
  Size: L. Effort: Opus / high (generator, pipeline stage, editor bake, runtime bind).

- [ ] **3. The contents-driven world, the baked NavMesh and its agents.** Navigation cannot be
  built or measured on a mesh that is cut from the wrong solids, at the wrong size, only at
  run time. Nothing here is deferred to a later pass: every hull the
  shipped graphs use gets its agent, and a brush answers each retail query by its own contents
  word — movement, sight and the pedestrian volume alike — not by a hand-kept list of classes.
  Retail: three masks move an NPC, and all three carry `MONSTERCLIP 0x20000` and none carries
  `PLAYERCLIP 0x10000` — `0x2000b` builds the graph, `0x2400b` probes a local route and fits a
  node, `0x202400b` moves (`navigation-jump-links.md` § "Doors and NPC-clip"). One mask sees:
  `0x2804091`, shared by `FVisible` and the cover and shoot-node traces — `SOLID`, `OPAQUE
  0x80`, `MOVEABLE`, `MONSTER` and Troika's `0x800000` (`%compileNpcOpaque`) and `0x10`; it
  carries neither `WINDOW` nor `GRATE`, so an NPC sees through glass and grating, and it is
  stopped by brushes that are not solid at all — 1,764 of them on 25 maps, 17 on
  `sp_tutorial_1` (`0x8000080`), 894 on `la_museum_1`. A brush whose
  contents carry `0x2000` marks a pedestrian volume (§ "The node searches and the pedestrian
  cost"). Read as answers to those masks — blocks the player (`0x1400b`), blocks an NPC
  (`0x2400b`), blocks sight (`0x804091`), is a pedestrian volume — the game's 146,162
  answering brushes fall into SEVEN signatures, four on the tutorial and five on the hub, and
  no shipped brush blocks the player alone. The hull table has 22 rows; 14 carry links in a shipped graph, and a graph's
  `UsedHullBits` names the hulls its map needs — `0x80001`, human and rat, on both witnesses
  (§ "What the shipped graphs and maps actually use", with the rows). The rat hull is not a
  subset of the human one: 41 of the tutorial's 428 rat links and 99 of the hub's 1,862 carry no
  human motion, and 5 / 9 of them join node sets the human links keep apart. The graph runs
  through a door that has a link and treats a door without one as a wall; step height is 18.
  Port today: `UE_map_sidecars.py:69` keeps `SOLID|WINDOW|GRATE|MOVEABLE|PLAYERCLIP` and merges
  it into one anonymous convex set, so the NPC-only clip brushes (5 on the tutorial, 31 on the
  hub), the sight-only brushes and the `0x2000` volumes (133 brushes on the hub) never leave the
  BSP. `AElysiumMapActor::QueryLineOfSight` traces `ELYSIUM_USE_CHANNEL`, which the hull
  collider is set to ignore, so an NPC's sight is answered by whatever else blocks that channel
  and never by brush contents: nothing makes glass transparent to it or an opaque tool brush
  solid to it. `UElysiumMapCollisionPayload` cooks that set as one `BlockAll`
  body; `UElysiumMapCollision` adopts it into a transient component at map load;
  `ElysiumMapActorLifecycle.cpp` spawns the nav bounds and runs the one Recast build per load
  (`RuntimeGeneration=Dynamic`) at the engine's default agent. The `.umap` carries nothing of
  navigation, so the mesh cannot be inspected or verified offline and the baked jump links
  point at a mesh that does not exist yet.
  Job, in order:
  1. Contents through the seam. The map unit's collision rows keep each brush's `contents`
     word, world and brush-entity alike; the staged hull rows carry it, and the exporter's
     `BLOCK_MASK` filter goes: every brush answering any mask below is staged. A brush's
     SIGNATURE is its four answers — player `& 0x1400b`, NPC `& 0x2400b`, sight `& 0x804091`,
     pedestrian `& 0x2000` — computed from the word by one generated table, the masks named by
     their retail addresses; nothing downstream tests a contents bit again. The `.hulls`
     sidecar gains the same word so the legacy transport partitions identically; its maps keep
     the run-time Recast build until their own migration, with the right solids and agents.
  2. The payload. One cooked body per signature present on the map, its collision profile
     generated from the signature: the player pawn's channel, the NPC pawn's channel and a
     dedicated sight channel each blocked or ignored as the signature says; a pedestrian-only
     signature is convex rows with no collision. Brush-entity bodies keep their per-entity
     table and take the signature of their own brushes, so a door blocks sight and both pawns
     through `MOVEABLE` and a glass `func_brush` does not block sight.
  3. The level actor. `bake map` places one world-collision actor in the `.umap` whose static
     components reference the payload's bodies; a body affects navigation iff its signature
     blocks an NPC. `UElysiumMapCollision` adopts that actor on the payload transport
     and spawns nothing; the sidecar arm is unchanged but for job 1's partition.
  4. Agents from the hull table. One supported agent per hull with links in any shipped
     graph — 14, inside the engine's 16 — generated from the recovered rows: radius half the
     width, height `maxs.z − mins.z`, inches to centimetres once, step height from retail's
     18 units. A map bakes one Recast mesh per bit of its graph's `UsedHullBits`; an NPC's nav
     agent follows its hull, so a rat paths on the rat mesh. The body's capsule is cut from the same row: a
     human stands 26 × 72 units (radius 33 cm, 183 cm tall), not today's 34 cm by 176 cm. Cell size per agent; the rat
     mesh's cost on `sm_hub_1` is measured and accepted, not avoided.
  5. The mesh in the level. Baked nav bounds from the collision union, the meshes built in the
     editor during `bake map` and saved with the level, run-time generation reduced to dynamic
     modifiers. Every door brush entity cuts every agent's mesh; which doors get a link across
     the cut, and what happens on reaching one, is 7's.
  6. Pedestrian volumes as nav modifier volumes under one project area class at unit cost; the
     pedestrian query filter that prices it is 5's.
  7. Sight on the sight channel. `QueryLineOfSight` and the cover and shoot-node traces 6 and
     9 add trace the sight channel against these bodies and the entities `MONSTER` and
     `MOVEABLE` stand for, not the use channel against whatever answers it. This changes what
     the tutorial's thug can see — 17 sight-only brushes start blocking him, 276 window and
     grate brushes stop — so the `sp_tutorial_1` stealth witness runs before and after, and a
     lesson that turns is read against retail before it is accepted. That these brushes block
     retail sight is inferred from the mask and the contents, not yet witnessed in a running
     retail game; one capture at a tutorial brush closes it.
  Acceptance, as `bake map --verify` on both witnesses, per baked agent: every link whose
  motion for that hull has the ground bit paths on that agent's mesh within a stated length
  factor, outliers listed; every NPC-only brush cuts the mesh; the rat-only links path on the
  rat mesh, and the 5 / 9 that bridge separate human node sets do NOT path on the human mesh;
  jump-link endpoints, hints, places and patrol points project onto the mesh of every agent
  that uses them; a report of AIN zones against mesh connectivity, pinned. Both levels save,
  reload and cook with no external export access. Per signature, against the baked level: an
  NPC-only brush stops an NPC and not the player; a sight-only brush stops a sight trace and
  neither pawn; a window stops both pawns and no sight trace; the body count equals the map's
  signature count.
  Pins (player, NPC, sight, pedestrian): tutorial `PNS-` 2,725, `PN--` 309 (33 of them the
  both-clip `0x8030000`), `--S-` 17, `-N--` 5; hub `PNS-` 4,020, `PN--` 151, `PN-p` 93,
  `-N-p` 31, `---p` 9. Those two maps between them show all seven signatures the game has;
  no `P---` brush ships.
  Provides: the solids, the agents and the per-agent meshes to 4–12; the verification harness
  their route tests run on. Consumes: the map unit's collision rows; 2's bake stage.
  Oracle: `navigation-jump-links.md` § "Doors and NPC-clip, the retail contract", § "What the
  shipped graphs and maps actually use".
  Recovered 2026-09-20, the reads this story owed (`docs/vtmb/data/hull_table.json`,
  `research/tooling/probes/{hull_table,contents_signatures,census_links_hulls}.py`, all three
  reproducing the pins from the repository): all 22 hull rows, the 8 link-less ones included —
  of which `WIDE_HUMAN_HULL` is asymmetric `(-15,-15,0)..(20,15,72)` and so could never be an
  agent, though it carries no link. **There is no walkable slope limit to recover**: retail's
  ground move (`0x102e4f50`) clamps only on step height and its stand test (`0x102e7270`) reads
  no normal, so the agents take retail's own standable normal `0.7` (`0x104492d0`, already ported
  as `ElysiumMove::StandableZ`) and the application is the named modernization.
  `TINY_CENTERED_HULL` is the two security cameras' (`shape.md` § "Slot 337"), and its 8,986
  links are on **18** maps, not the 15 stated here — 15 is the `0x81` histogram row alone.
  `GARGOYLE_HULL` is declared by 2 maps and carries links on 1, so "one mesh per bit" can build a
  mesh nothing paths on; the bake reports it. The hub's 9 bridging rat links are enumerated at
  last (`155/156/158/304-…`, `526/567/568-…`: two places, nodes 304 and 568).
  Unrecovered, each a read before its job lands: how `CNPC_VRat`, `CNPC_VCamera`,
  `CNPC_VGargoyle`, `CNPC_VManBat`, `CNPC_VSheriffMan` and `CNPC_VWerewolf` acquire `m_eHull`
  (`+0x1568`) — slot 337 declares a PRECACHE set, not the stand hull, and only six classes carry
  a witnessed store; species overrides of the step height; the open `146–184` pair on the
  tutorial's zone 11 / 12 boundary. (Why links run through standing doors is
  closed: `MOVEABLE 0x4000` is the bit that hits doors, and the graph-build mask `0x2000b` is
  the only one without it — which is 7's door rule.)
  Size: L. Effort: Opus / high (exporter, payload, editor bake, nav config, verify).

- [ ] **4. The place set.**
  Retail: a node is where a patrol, a hint, a cover search and a hunt point. `CAI_Hint::
  GetPosition 0x102d1180` answers the hint's NETWORK NODE at hull height when `m_nNodeID != -1`
  and the hint's own origin otherwise; the patrol lookup answers the node (`+0x5e4`), not the
  hint. `CNodeEnt::Spawn 0x102d78d0` pairs BSP node rows with graph nodes POSITIONALLY, in
  spawn order, through the running counter `DAT_10926a3c`; an out-of-range counter keeps its
  id and attaches nothing. The install loads the patch's own loose graphs (mod directory, then
  base directory, then packs): `sp_tutorial_1` 203 nodes / 429 links, `sm_hub_1` 578 / 1,862.
  What behaviour reads of a node: position, yaw, type, the per-hull Z offset
  (`node+0x14+4*hull`), its hint (`+0xa0`) and the run-time cooldown (`+0x9c`). Links, zones,
  the neighbour bitvector, link info and the 22 motion words are the engine's road network —
  evidence for the bake, never run-time data.
  Port today: no node data at run time. `FElysiumHint::NodeId` is a seam; `FindPatrolPoint`
  answers the hint entity and the patrol walks to its origin; `NavNearestNodeTo`,
  `NavAllHintNodes` and `GatherHintNodes` answer nothing. The spec's old pins (116 / 234) were
  the packed graph, which pairs with the unpatched BSP only.
  Job: the exporter resolves the graph retail would load and names the row fields (`tail` =
  type, flags, neighbour bitvector; `lead` = zone, link count). The bake writes one cooked
  `UDataAsset` per map, referenced by the baked map content: a place row per node — network
  index, type, position, yaw, the Z offset of each agent the map bakes, the hint association
  by positional pairing with its unresolved and out-of-range outcomes kept, the authored
  `nodeid` as provenance — and the crosswalk pairs (two type-11000 hint nodes a link joins).
  No links, zones or masks. At run time a registry keyed by network index carries the
  cooldown; hints get their `NodeId`; a hint's stand position and a patrol point resolve to
  the node at hull height; `TASK_GET_PATH_TO_RANDOM_NODE` draws from the set. The bake reports
  every hint, patrol point and interesting place that sits off an agent's mesh, and every one
  no node covers, so reach that changes against retail is seen, not discovered.
  Acceptance: both witness levels load the asset with no external export access; the pairing
  reproduces retail's on the patch graphs (standalone hints, duplicate authored ids, the
  out-of-range arm); the thug's `pt1..pt3` resolve to node positions; the seams above answer
  from the registry. Pins, patch graphs: tutorial 203 places, all ground; hub 578, all ground.
  Provides: places to 7–12. Consumes: 2, 3.
  Oracle: `navigation-jump-links.md` § "The loader, walked", § "Which graph the patched install
  runs on", § "What the shipped graphs and maps actually use". Unrecovered, a read before it
  lands: `TASK_GET_PATH_TO_RANDOM_NODE`'s body.
  Size: M. Effort: Opus / high.

- [ ] **5. The navigator and the movement seam.**
  Retail, the behaviour kept: `SetGoal 0x102ecd20` — goal types 1 target entity, 2 the enemy's
  last known position, 3 the goal-entity chain, 4 a position, 6 a cover position, 7, 8 an
  interesting place (the pedestrian walk), 9; the tolerance rules (`-2.0` the hull's width, 26
  for a human; `-1.0` that width only while the tolerance is still 0; types 1 / 2 / 7 half the
  sum of the two hulls; any other value as given); a blocked move accepted when what remains is
  within tolerance and `|dz| < 2.0`; the retry window `0x102f1dc0` (a failed find with
  `nav+0x40` set waits until a deadline before raising the failure); the codes — `0x0c` no
  route, `0x1d` no patrol node, `0x0e` a door; the pedestrian byte, set by goal type 8 and
  cleared with the goal on every schedule change unless `PRESERVE_PATH` stands or the NPC is
  mid-jump; `CAI_BaseNPC::NPCThink` silent until the network manager's first think, 0.8 s into
  the map. How the route is found and followed is Unreal's.
  Port today: the goal is three loose fields on `FElysiumNpc` (`MoveGoal`, `bMoveIssued`, the
  schedule's tolerance); one travel call, `IElysiumNpcMotor::MoveTo`, polled; `0x0c` is the
  only route failure; `OnMoveRequestFinished` is recorded and never acted on; `NavGoalType`,
  `NavPathSample` and `FUN_102eee40` answer nothing.
  Job: a navigator object on the NPC holding the goal (type, target, tolerance, gait, flags),
  the pedestrian byte, the retry window and the outcome, which every `GET_PATH_TO_*`,
  `RUN_PATH` / `WALK_PATH` and `WAIT_FOR_MOVEMENT` arm goes through and the kernel's goal
  readers read. The seam, in the NPC's words: request a move; outcomes arrived, failed with a
  code, blocked by a door, blocked by an NPC; a route sample (has a path, distance left, next
  corner). Under it, on the body: a path-following component that raises those events; the
  default and the pedestrian query filters, the pedestrian one pricing 3's area at ONE
  `RandomInt(5, 10)` drawn per request; the acceptance radius equal to retail's tolerance
  with nothing added for the capsule; partial paths off; an off-mesh goal projected within
  the tolerance and refused beyond it. The 0.8 s think gate.
  Acceptance, on the baked witnesses: each goal type issued and its tolerance observed at
  arrival; a refused route raises `0x0c` at once without the retry word and at the deadline
  with it; a schedule change drops the goal and the pedestrian byte, `PRESERVE_PATH` keeps
  them; a pedestrian request prices the hub's roadway and a plain one does not.
  Provides: movement and its outcomes to 0002 and 0003. Consumes: 3, 4.
  Oracle: `navigation-jump-links.md` § "The route gates, walked" (its `SetGoal`,
  `0x102f1dc0` and `DoFindPath` paragraphs; the builders below them are engine record).
  Size: L. Effort: Opus / high.

- [ ] **6. Geometry services.**
  Retail: the questions behaviour asks of the world. Sight, mask `0x2804091` (3's sight
  channel). The hull trace and the hull table's extents. The stand test `0x102a0ed0`: start
  0.1 above the point, drop by slot 523 (36.0 on the Troika line), a foot box of the hull's
  footprint shrunk a quarter each side, pass only on ground that slot 166 accepts. The cover
  validator slot 548: reject only a hull that starts embedded, and a hint of another group.
  "Can I walk straight there" — retail's local probe; "can I get there at all" — retail's
  node route.
  Port today: `QueryLineOfSight` traces the use channel; `MoveProbeCheckStandPosition`,
  `KernelHullTrace`, `RetailHullExtents` and `PositionClearForTeleport` answer false;
  `IsJumpLegal` is real.
  Job: the services behind the seam, each an Unreal query — a sight trace on the sight
  channel; a hull sweep and a hull overlap at the extents generated from the hull table; the
  stand test as a downward sweep with retail's constants; straight-walkable as a NavMesh
  raycast with the `|dz| < 2.0` rule; reachable and path length as path tests on the asking
  NPC's agent and filter. A per-think budget for path tests, measured on `sm_hub_1`.
  Acceptance: each service against fixtures on the baked tutorial — a sight-only brush, a
  window, an NPC-only clip, a ledge, a closed room.
  Provides: the questions 7–12 ask. Consumes: 3.
  Oracle: `navigation-jump-links.md` § "The cover search, walked" (the validator),
  § "The back-away and shoot-node searches, walked" (the stand test); `senses.md` (the sight
  mask and weapon line of sight).
  Size: M. Effort: Opus / high.

- [ ] **7. Traversals: jumps, doors, crosswalks.**
  Retail. JUMPS: a link whose usable motion is exactly jump passes `IsJumpLegal` — 80 up, 250
  down, 160 across, apex within 80 × 1.25 — tested per direction. DOORS: `MOVEABLE 0x4000` is
  absent only from the graph-build mask, so links run through standing doors and every
  run-time probe finds the door solid; a door with no link is a wall. Reaching one, slot 531
  `OnObstructingDoor` opens it (`AcceptInput("Open")` after the lock test `0x100eec70`) or
  declines; a refusal raises `0x0e` and the door-blocked notice `0x1027de00`, and the door
  keeps its own NPC-failure word and timer (`+0x644`, `+0x640`) for 5 or 20 s. CROSSWALKS:
  `Walk` / `DontWalk` on a crosswalk hint set the state of the link joining two type-11000
  nodes (`0x102f97c0`); a PEDESTRIAN goal (type 8) arriving at the first of the pair while it
  is red sets `AT_CROSSWALK` and waits (`conditions-and-states.md`); an NPC blocked by a
  waiter swallows its move while its own waypoint is red and otherwise advances within 32
  units (`0x10298340`). The cycle is the map's `logic_timer` — 40 s on `sm_hub_1` and
  `hw_hub_1`; `sm_hub_2` and `sp_theatre` have none, so nobody waits there.
  Port today: jump links bake and fly (`AElysiumNavJumpLink`), one hull, direction unchecked.
  A door is a movable `BlockAll` brush nothing in navigation knows about; slot 531 is ported
  with its waypoint-through-door arm a seam. No crosswalk exists.
  Job. Jumps: a link per baked agent whose motion word has the jump bit, each direction kept
  only if legal. Doors: the bake marks a door traversable for an agent iff a link with that
  agent's motion crosses its closed box; a traversable door gets a smart link through the
  doorway over a cut strip, enabled while the door is open or an NPC may open it and its
  failure timer is not running; reaching the link runs slot 531, waits for the door, resumes;
  a refusal is 5's `0x0e`. Every other door cuts the mesh. Crosswalks: a smart link between
  the two curb places carrying the pair's `Walk` / `DontWalk` state; the wait and the queue
  rule on 5's events.
  Acceptance: the existing jump-flight tests, per agent; an NPC opens `frontgate` and passes,
  a locked door refuses with `0x0e` and is avoided for its timer; a pedestrian waits at the
  hub's curb through a red phase and crosses on green, a second one queues behind it, a
  non-pedestrian does not wait. Pins, patch graphs: jump links tutorial 25 human / 36 rat,
  hub 117 / 103; door brushes crossed tutorial 8 of 36, hub the smoke-shop pair; crosswalk
  nodes hub 6.
  Provides: special traversal to 5. Consumes: 3, 4, 5, 6.
  Oracle: `navigation-jump-links.md` § "Doors and NPC-clip, the retail contract";
  `conditions-and-states.md` (the crosswalk wait); `schedule-kernel.md` (slot 531).
  Size: L. Effort: Opus / high.

- [ ] **8. Hint nodes.**
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
  The goal selectors (9) read this registry: cover's owner-only hint test and the shoot
  node's full unusable test remain distinct. Python/I/O writes share
  this registry's state, including `EnableHint` / `DisableHint` and hide/unhide; hint `Kill`
  follows retail's hide override rather than removing its graph identity.
  The species halves of slot 566 stay with their classes in 0002.
  Provides: the searches to 0002's cover, kick, interest and alert families. Consumes: 2, 4, 6.
  Oracle: `shape.md` (the four hint sections), `schedule-kernel.md` § "The three hint
  validators". Size: M. Effort: Opus / high.

- [ ] **9. The goal selectors.**
  Retail, each walked in `navigation-jump-links.md`: the cover search `0x10301720` and its
  directional twin; the lateral pre-check `0x102784a0` (ten side points 48 units apart, left
  before right); the shoot node `0x10302e50` behind flank and the `_LOS` tasks; back-away
  `0x10300b50`, its A* sibling `0x10301010` and the three follower variants; the hunt target
  `0x10306f60`; the cower arm. 134 of the 691 shipped schedules name one of them. Each is a
  set of TESTS a place must pass — cover: no live hint owner, `min² <= d² < max²` with `max`
  784 when unset and `min` capped at half of it, three sight lines from the threat's eye all
  blocked, the validator, not a climb node, not cooling down, not more than 1.5× as far (in
  squares) from the NPC as from the threat; shoot node: facing, cooldown, both range bounds
  strict, line of sight, the hint fully usable; back-away: within the height limit, further
  from the threat than where I stand, nearer me than the threat, far enough, standable — and
  a WRITE on success (cooldown `+1.0`, the hint claim) and a fail code.
  Port today: every selector is a seam over an empty hint list; the hunt words are only ever
  reset.
  Job: each selector as a function over 4's places — candidates in the band, nearest to the
  NPC first as retail's frontier pops them, retail's tests verbatim and in order, reachability
  from 6 in place of link expansion, the writes and fail codes as retail's. Back-away answers
  the nearest place that passes (retail's is the first in link order — a named difference).
  The hunt target is a reachable place within 256 units of travel best aligned with the
  heading, with retail's jitter draws. Retail's bugs above the seam stay: task `0x0e` drops
  the cover position it found; the A* back-away answers place 0 when it finds nothing.
  `TASK_CREATE_HUNT_PATROL_LIST` has no issuer and is not built.
  Acceptance, on the baked tutorial with staged threats: each selector's pick satisfies every
  retail test; a claimed or cooling place is skipped; the fail codes; `SHOT_BY_UNKNOWN`,
  `MELEE_RETREAT`, the flank programs and the hunt setup run to a goal.
  Provides: the searches to 0002's cover, flank, retreat and hunt families. Consumes: 4, 6, 8.
  Oracle: `navigation-jump-links.md` § "The cover search, walked", § "The back-away and
  shoot-node searches, walked", § "The hunt selectors, walked", § "The hint-path, snap and
  cower arms, walked".
  Size: L. Effort: Opus / high.

- [ ] **10. Interesting places.**
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
  Provides: the trio to 0002's 11 and 27. Consumes: 2, 5, 8.
  Oracle: `schedule-kernel.md` § "Interesting places: the selector, the programs, the wait",
  § "Interesting-place eligibility", `shape.md` § "The interesting-place wait and its loop".
  Size: M. Effort: Opus / high.

- [ ] **11. Patrol paths and the patrol-point interest record.**
  Retail: `CAI_PatrolPath` and `info_node_patrol_point`; the interest record at `node->+0xa0`
  (`+0x468` the name of a `CAI_InterestingPlace`, `+0x46c` a 0–99 chance) resolved by
  `0x1029f730` and `0x1029f780`.
  Gap: nothing — no path record, no node record.
  Job: the path records and the point's interest record; the queries the patrol program asks
  (next point, hunt path, the record). The roll, the two interest tasks and the programs stay
  in 0002's 10g and 27.
  Consumes: 2, 4, 5, 10. Oracle: `programs.md` § "Patrol paths, walked" and
  `navigation-jump-links.md` § "Route selection and node identity". Story 1 identifies
  `target_name` / `ip_percent`; the hint's network association and hull-adjusted destination
  come from 4. The path is the program's ordered goals; native navigation finds each walking
  route. The hunt target is 9's. `GET_FULL_PATROL_PATH` reads
  only the current node, while `TASK_PATROL_PATH` itself reads no graph/path object (2026-09-17
  task audit). Recovered 2026-09-19 (`programs.md` § "Patrol paths, walked"): `TASK_PATROL_PATH`
  has NO upstream route — `SCHED_TROIKA_IDLE_PATROL` sets an activity and stands, no shipped
  patrol names it, and the walking programs are `0x65` / `0x67` / `0x69`, one type-4 goal per
  point; the path object, its loop / ping-pong type table and the installer are pinned there.
  Size: S–M. Effort: Fable / medium; corpus pass on the node keys first.

- [ ] **12. The flying mover.**
  Retail: no link in any shipped graph flies (0 of 29,523), so every flight is a straight
  move at its goal. `CNPC_Crow` flies to hints (`TASK_CROW_FLY_TO_HINT`; search type 700,
  flags 3, radius 5000); `CNPC_VManBat` runs six `TASK_MANBAT_FLY_TO_HINT` schedules (hint
  type 20000). The navigator's type is fly while it lasts.
  Port today: the body has no flying mode; `EElysiumNpcNavType` is only ever ground or jump.
  Job: a flying gait on the body — CharacterMovement's flying mode steering straight at the
  goal with a swept look-ahead on 3's NPC-solid bodies — driven through 5's navigator, so
  tolerance, arrival and `0x0c` are the same contract as on the ground; the nav type reported
  to the schedule host.
  Acceptance: a crow reaches a hint across open air and refuses one behind a wall.
  Consumes: 5, 6, 8. Unrecovered, reads before it lands: the species' flight speeds; what the
  fly arm of `MoveLimit 0x102e6d70` does when blocked; landing.
  Size: M. Effort: Opus / medium.

- [ ] **13. The AI sound list and its volume table.**
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
  Recovered 2026-09-19 (`senses.md` § "The shared list itself"): 64 records, a full list DROPS
  the new sound (no eviction), freed at `expire + 4.0 <= curtime` on a 0.3 s think.
  Size: M. Effort: Opus / high.

- [ ] **14. Squads.** The object half of 0002's story 17, moved here; 17 keeps the consumers.
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
  Consumes: 0002/5. Oracle: `social.md` § "Squads, decoded". The six wrappers were recovered
  2026-09-19: member fan-outs to NPC slots 54–58, of which only 54 and 56 do anything.
  Size: M. Effort: Opus / high.

- [ ] **15. The attack coordinator and the standoff goal.**
  Retail: Troika slots 600 / 601 acquire and release a melee slot on the coordinator for a
  `CBaseEntity*`, 608 binds it by name (`"Normal"`); `CAI_StandoffGoal`'s two inputs
  (`0x102c87a0`, `0x102c8830`), `UpdateOnRemove 0x102cdc50`,
  `CAI_StandoffBehavior::TranslateActivity 0x102c79e0`.
  Gap: "this substrate has no coordinator object" — the melee bodies are ported against a null.
  Job: the coordinator as a world object with named instances and melee slots; the standoff
  goal entity as a record with its inputs. The bodies that call them stay in 0002.
  Consumes: 2. Oracle: `shape.md` § "The tables" (600–616), `schedule-kernel.md` § "Story 29d,
  family Motor10" (the standoff sections). Recovered 2026-09-19 (`social.md` § "The attack coordinator
  object"): three instances ("Normal", "Player", "Boss"), cap 2 each, rebuilt per map; a full
  coordinator evicts its furthest member for a nearer candidate.
  Size: S–M. Effort: Opus / medium.

- [ ] **16. Makers and templates.**
  Retail: `CNPCMaker`, `CNPCMaker_Fleshpile`, `CNPCMaker_Zombie`; `npctemplate*.txt` (150
  declarations in 36 files); the child inherits `NPCTargetname`, `NPCSquadname`, the forwarded
  outputs; count, frequency and `Flag_*` lifecycle.
  Gap: `ElysiumNpcMaker` exists and spawns; its keyfields are hand-typed.
  Job: the maker as a baked actor the runtime adopts, keyfields from the datamap seam, the
  inheritance and lifecycle rules verified against the hub's 48 requests and the tutorial's 14.
  Consumes: 2, 0019 story 2. Oracle: `population.md` § "How a map defines an NPC",
  § "Templates and inheritance". Size: S–M. Effort: Sonnet / medium.

- [ ] **17. Relationship defaults and the player-law bus.**
  Retail: `AddClassRelationship`, `Rules.txt`; `SetRelationship` (native `D_*` table),
  `SetDisposition` (stance) and `reaction.txt` (RPG score) kept separate; the player's law
  levels, offenders and the closest-NPC cache on `CBasePlayer` (`0x101828b0`, `0x10182a90`).
  Gap: `ElysiumRelationships`, `ElysiumLaw` and the law event bus exist.
  Job: the class-relationship table loaded once from vdata; the bus as the world object NPCs
  read (levels, offender, location, timers). The acts stay in 0005 / 0006, the witnessing in
  0002.
  Oracle: `social.md`, `population.md` § "Player-law observation transaction". Size: S.
  Effort: Sonnet / medium.

- [ ] **18. The AI logic entities.** Added 2026-09-19: found by the classname scan beside
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
  point logic entities on the entity table, not baked actors. `info_node_link` is registered and inert:
  no content can address the six that ship (all on `sp_giovanni_4`, unnamed, no I/O), and the
  link-off bit it would set belongs to the routing layer Unreal replaces;
  `logic_squad_condition` reads 14's squad — any member, and nothing while member 0 is
  disconnected; both conditions resolve their id at Activate and test the raw `m_Conditions`.
  The conversation place is a `CPointEntity` sibling of the place, not a subclass: it writes
  `m_Mode` on 10's one-slot places, needs two occupied, and its too-close arm runs the place's
  `Disable` visitor walk; keep the pick's `RandomInt` draw count. `logic_npc_condition` and
  `ai_changetarget` wait on no story here. Tests use the authored rows above as fixtures.
  Consumes: 2, 10, 14. Oracle: `population.md` § "Classnames outside the census families";
  each body's recovery lands in `entity_io.md`. Size: S. Effort: Sonnet / medium.

- [ ] **19. The infrastructure debugger view.** Visual-only modernization, as 0002's 23.
  Job: the overlay draws hints (type, group, locked), places (enabled, claimant, visitors,
  wait), patrol paths, the sound list with expiry, squads and slots, coordinator slots, the places, the agent
  meshes and nav links, and the last refusal. Select authored objects through their baked
  actors and inspect places through the cooked asset.
  Consumes: 2–18. Size: S. Effort: Sonnet / medium.

- [ ] **20. The hub at idle: the second witness.**
  Job: `sm_hub_1` and `sp_tutorial_1` on the V2 lane (0002's 22, moved here) with every
  object above baked; a scene test per map that opens the baked level and walks each record's
  queries against it — places per group, hints per type, node associations and route outcomes, audibility at a set of
  origins — and a played witness once 0002's idle programs land: pedestrians visit places, cops
  patrol, makers cycle, and each goal keeps its success and failure contract while
  Unreal performs the movement.
  Consumes: everything above. Size: M. Effort: Sonnet / medium, then played.

## Seams
- Provides: the query surface to 0002 — hint searches, place selection and the trio, the next
  patrol point and its record, audible sounds, squad members and the shared memory, the melee
  slot, node-goal data and route outcomes; the baked actors and navigation asset to the debugger;
  the second witness. NavMesh/path following/CharacterMovement supply movement.
- Consumes: 0019's generated datamap bindings (every infrastructure keyfield) and tunables;
  0002/5's enemy record store; 0005 / 0006's law acts; the V2 export units through the map
  bake, never at runtime.
- 0018 runs beside 0019 and consumes 0002 only through the query surface: no story here adds a
  member to the NPC.
