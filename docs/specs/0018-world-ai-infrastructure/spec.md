# 0018 world-ai-infrastructure — The world's AI infrastructure: Unreal navigation, authored places, the sound world and the groups every NPC queries

**Standing rule for every story:** follow retail behaviour; do not take on work already planned in another story of this spec or in another spec.

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

**Revised 2026-09-20, story 21 audited and split:** the owner's rule for everything the game
opens — **baked; or, when it cannot be baked, deployed into `Content/ElysiumCorpus/`; never read
from outside the project** — extends story 21 from the transport selectors to the legacy BSP
decoder and the export root itself. Audited against the code at `a7f448ed`, its first text had
undercounted the work by half and misplaced three facts, so it is now 21-1 … 21-8, run in order.
Only 21-1 is REQUIRED ahead of 4 (so 4–20 never carry a legacy arm) and 21-6 ahead of 20 (its
cook check); the owner runs the group ahead of 4.

**Revised 2026-09-21, the wander decided:** story 4's open question — `TASK_GET_PATH_TO_RANDOM_NODE`
is a random walk over AIN links, not a draw from places — is closed by the owner as the **capped
point pick**: a retail place at the order's distance, chosen by retail's tests, the route Unreal's,
the walk's iteration guard replaced by one baked number per map and agent. The asset stays places
and no links; the Navigation boundary below is untouched. Story 4 carries the rule, what it gives
up, and the upgrade held in reserve.


## Witness
Two maps, baked into their levels and standing in the editor before any NPC program runs on
them. `sp_tutorial_1`: the thug's alley — `pt1..pt3`, his `hint_groups`, the patch graph's 25 human and
36 rat jump links, the footstep and door sounds he hears. `sm_hub_1`: 38 placed
NPCs and 48 maker requests, the densest ambient population in the corpus — pedestrians walking
places and crosswalk hints, cops, makers cycling. Authored places, hints, patrol points and makers
are selectable baked actors; special traversal uses native navigation links. Compact navigation
records are inspectable in a cooked asset. Their queries are exercised independently of NPC
programs, with native movement tests for the pathfinding and traversal boundary.
Graph counts are those of the patch install's own graphs, the ones retail loads (resolved by 3,
21-2 and 21-8; staged as places by 4).

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
  the migration still to finish. (It had been left to the map lane's own later spec, story 3
  taking only the slice navigation needs — the contents signatures and the level actor; since
  2026-09-20 it is owned here, by 21-3 for the per-map readers and 21-6 for the root.) The
  placement rule, confirmed 2026-09-15: **what can stand on a map is baked onto the map as an
  actor; what cannot goes to the corpus; what is recovered from the binary is generated
  source.** The navigation exception is explicit: AIN nodes and ordinary edges describe a
  compiled network, and do not each require an actor. Hints, interesting places, patrol points
  and makers bake as actors; native nav links represent special traversal. Required network
  data lives in one cooked `UDataAsset` per map, referenced by its baked map content. Other
  entities keep the existing entity-table transport (`UElysiumMapEntities`; its `.ents` arm and
  the per-map migration state that chose it are retired by 21-1). Tables live under
  `CorpusRoot()`. The substrate reads these
  baked or deployed sources at activation. The bake and the
  import regenerate everything, so an edit made in the editor does not persist; an authored
  override layer would be a separate, named story. (`docs/vision.md`
  § "The build shape" still says the runtime builds "from the intermediates on disk"; that
  sentence is superseded by this decision and awaits the owner's edit. **Flagged again
  2026-09-21:** 21-6 made it false in fact rather than in intent -- there is no intermediates
  tree left for the runtime to build from, `Root()` is deleted and the game opens no file outside
  the project. The same bullet's "written to a gitignored, regenerable export tree" is now only
  half true: an export tree still exists offline, but nothing the game opens is in it.)
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
look-ahead; the wander (`TASK_GET_PATH_TO_RANDOM_NODE`) picks a retail place at the order's
distance instead of walking the links to one, its iteration guard a baked distance (story 4,
decided 2026-09-21).

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
bodies. The graph figures of this paragraph are the packed exports'. The graphs retail loads —
the patch's loose AINs, resolved since 3, 21-2 and 21-8 — are 97 non-empty: 11,558 nodes (11,517
ground, 37 type 1, 4 climb, no air; story 4's census, 2026-09-29), 29,523 links.

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

- [x] **3. The contents-driven world, the baked NavMesh and its agents.** Closed 2026-09-20 (closing note below). Navigation cannot be
  built or measured on a mesh that is cut from the wrong solids, at the wrong size, only at
  run time. Nothing here is deferred to a later pass: every hull the
  shipped graphs use gets its agent, and a brush answers each retail query by its own contents
  word — movement, sight and the pedestrian volume alike — not by a hand-kept list of classes.

  **Landed 2026-09-20, jobs 1, 2, 3 and 7 (the contents half).** A brush's signature — its four
  answers — is computed once at the seam and nothing downstream tests a contents bit again.
  `research/tooling/data/contents_masks.json` → `gen_contents_signatures` emits the runtime
  header, the pipeline module and one named collision profile per signature, because only a
  profile name survives a `.umap` save. `.hulls` carries every answering brush led by its
  contents word. (It was written to stay readable by the old format's reader; story 21 retires
  that reader and the divergence record with it, leaving `.hulls` a V2 intermediate.)
  Payload v2 cooks one body per signature and refuses a version it does not know, and a
  brush entity wears the `Dyn` profile of its own brushes. Sight moved off the +use channel,
  which the world collider ignored outright, onto a channel the signature profiles answer:
  17 tutorial brushes begin blocking the thug's sight and 276 window and grate brushes stop.
  The player took its own object channel so the two pawn questions can differ, with
  `ElysiumPlayer` mirroring `Pawn` in every engine profile (the test re-reads the engine's own
  table). NPCs stayed on `ECC_Pawn`, which is what makes "blocks an NPC" and "cuts the NavMesh"
  one fact — asserted through `IsNavigationRelevant` itself, not by reading config. The hull
  table answers `RetailHullExtents` for its 13 callers, and the body is 33.02 × 182.88 stepping
  45.72 rather than 34 × 176 stepping the engine's default.

  **Landed 2026-09-20, job 4's agents.** All 14 are declared, generated from the hull rows, with
  `bAutoCreateNavigationData` off so nothing creates data for agents nothing uses.
  `UElysiumNavBakeLibrary` restricts a level to the agents its graph's `UsedHullBits` names (the
  stage now carries the word), places the bounds volume as a real brush and builds synchronously
  reporting each agent's tiles, bytes and seconds; `RestrictNavigationToUsableAgents` holds the
  run-time path, which has no graph in hand, to the one agent a body stands on. Verified by
  loading both witnesses: `sp_tutorial_1` Active in 34.4 s and `sm_hub_1` in 22.2 s, each having
  built exactly one mesh — `Human`, radius 33.0, 0.46 s on the tutorial.

  **Job 3's level actor landed 2026-09-20.** `AElysiumWorldCollisionActor` stands the world
  collision in the `.umap` — one static `UPrimitiveComponent` per signature, referencing the
  payload's cooked body (a bare primitive, not the procedural mesh the transient colliders use,
  whose body setup is `Instanced` and would duplicate the geometry into the level; and the bodies
  had to become `RF_Public`, since a level may not name another package's private sub-object).
  Placed by the collision import, which is also what authors those bodies, so the two cannot
  disagree; adoption checks map name, payload identity and body count and refuses a level with
  two. Witnessed: the tutorial adopts its four signatures and boots Active in 21.4 s where the
  same collision took 34.4 s built at load, the hub adopts five with the roadway correctly not
  cutting the mesh, and the collider arrives in 0.1–0.2 ms.

  **Job 5 landed 2026-09-20: each map carries its navigation meshes.** Cut from the
  world-collision actor's own bodies — so from the right solids, a body affecting navigation
  exactly when its signature blocks an NPC — for the agents its own graph's `UsedHullBits` names.
  Both witnesses adopt them and build nothing: `sp_tutorial_1` Active in 20.2 s, `sm_hub_1` in
  21.0 s, no tile-limit errors. The rat has a mesh for the first time: tutorial Rat 1,235 tiles
  against Human 915, hub 1,598 against 1,122. `RuntimeGeneration` is `DynamicModifiersOnly`, so a
  door or a priced volume can still change an area without regenerating underneath it, and
  `bForceRebuildOnLoad` is gone — it would have discarded the baked mesh on every load.

  Four things had to be right and three fail silently, which is why the first attempt was reverted
  rather than patched. The editor's loader leaves an `AsyncLoadLock` and `Build` declines while
  any lock is held, saving nothing and reporting success. The agent mask must be handed to
  `AddNavigationSystemToWorld`, not set after: editor-mode creation spawns data for every
  supported agent as it initialises. The bounds must come from the collision bodies — taken from
  every navigation-relevant primitive, one of `sm_hub_1`'s 387 carries world-sized bounds and the
  union was a 10 km cube against a 290 m map, which is what asked Recast for 3,084,588 tiles. And
  cell and tile size are `RecastNavMesh` properties `SupportedAgents` cannot carry, so each mesh
  takes its agent's own from the hull table. A fifth, mine: `IsRuntimeNavigationReady` demanded a
  bounds volume the adopt path never spawns, so an adopting map hung rather than failing.

  **The hull gate is closed (2026-09-20, four walks; `navigation-jump-links.md` § "The two hull
  words").** A CONSTRUCTOR writes the hull, which is why the kernel ledger — which does not record
  constructor accesses — saw a witnessed store for almost nobody; ~24 classes carry one. The rat
  stands and paths on 19 through `CNPC_VScurrying`'s constructor `0x103abb22`. And there are **two**
  hull words, not one: `+0x1568` sizes the collision box and the trace helpers, while `+0x156c` is
  the PATHING hull that `CAI_Navigator::SetGoal 0x102ecd2c` caches and the whole A* family feeds to
  `CAI_Node::GetPosition`. **So the Recast agent follows `+0x156c` and the capsule follows
  `+0x1568`** — the Sheriff stands on hull 21 and routes on the human mesh, needing no agent of his
  own, while Hengeyokai is the inverse (stands 0, paths 18) and Ming Xiao splits 15/16. Seven
  `GetPosition` sites do pass `m_eHull`, none of them routing: patrol-goal anchoring, the zombie's
  patrol arm, extrapolated routes, debug drawing. `0x102d7730` turns out to be `CAI_TestHull`
  machinery with no NPC caller, and nothing outside code can set a hull.

  **The hull words landed 2026-09-20.** `docs/vtmb/data/class_hulls.json` carries every recovered
  constructor store with its address, and `gen_hull_table` emits it as `ElysiumRetailHulls::
  ClassHulls`, most-derived first. The kernel carries BOTH words -- `HullKind` for the standing
  hull and `PathingHullKind` for `+0x156c`, which retires the alternate-hull seam -- filled in
  `BaseNPCInit`, the first body that runs with the retail class known. The body sizes its capsule
  from the standing hull and states its nav agent from the pathing one, never deriving the agent
  from the capsule (a rat's capsule clamps to a sphere, so a derived agent would be the wrong
  shape for its own mesh). A rat now stands and paths on 19, inherited through `CNPC_VScurrying`
  exactly as retail inherits it. Named modernization limit: `UCrowdManager` serves one mesh -- the
  first supporting the default agent -- so a body on any other agent runs plain path following
  rather than being steered against a mesh it does not path on.

  **Job 6 landed 2026-09-20: the roadway is priced and the walls are walls.** Both marks are
  convexes, not boxes -- `FAreaNavModifier` through `INavRelevantInterface`, because
  `ANavModifierVolume` wants brush geometry for a convex set the payload already holds exactly and
  `UNavModifierComponent` marks an AABB, which would price the pavement beside a roadway slab.
  `UElysiumNavArea_Pedestrian` at cost 1 over the `---p` rows (hub 9, tutorial none): not cheaper
  ground, ground story 5's filter prefers. `UElysiumNavArea_DoorCut` over every door no graph link
  runs through, computed in the collision lane from the patch graph -- which reproduces the
  census's own figures, tutorial 8 of 36 and the hub's smoke-shop pair 2 of 29, from the pipeline
  for the first time.
  Recorded limit: a door can be traversable for ONE agent and not another -- on the tutorial 5 of
  the 8 are crossed by both hulls, 1 by the human alone and 2 by the rat alone. A nav area is not
  per-agent, so job 6 cuts only the doors no agent crosses and leaves those 3 open on both meshes.
  That over-permits rather than walling an agent out of a door retail let it use, and 7's
  per-agent smart link is where it closes (`partialByAgent`, pinned 3 and 0).

  **The acceptance harness landed 2026-09-20: `uv run elysium verify nav --maps <map>`.**
  `importers/map_nav_acceptance.py` projects retail's graph into an answer key -- ground and jump
  links per agent, the links only one agent has, and the subset of those that BRIDGE node sets the
  human's links leave apart. `UElysiumNavVerifyLibrary` answers it in batch (a per-link editor
  round trip would dominate the bake), distinguishing "an endpoint is off the mesh" from "both ends
  are on it and cannot be joined" -- a hole and a wall are different findings with different fixes.
  `validation/nav_acceptance.py` holds every verdict and needs no editor, which is what makes the
  rules testable.
  It reproduces the census from the pipeline for the first time: tutorial 203 nodes / 429 links,
  41 rat-only, 5 bridging; hub 578 / 1,862, 99 rat-only, 9 bridging.

  **What it found.** All 2,009 human ground links path, and all 2,151 rat ones but three. Every
  jump endpoint projects on both agents. All 14 bridging links path on the RAT's mesh -- the claim
  a one-mesh port cannot make, now witnessed rather than asserted.
  Three real findings and one correction:
   * **The bridging criterion above was wrong and is changed.** It demanded those links NOT path on
     the human's mesh. Measured, the human mesh paths all 14, 70 cm to 9,155 cm -- which is this
     spec's own named modernization ("NPCs can reach floor retail's sparse graph never covered"),
     not a defect. Retail's graph is a sparse set of designer-placed nodes and a rasterised floor is
     not; demanding Recast reproduce its CONNECTIVITY would be demanding it reproduce its
     sparseness. The base-mesh result is now reported as reach that changed, with its length, which
     is what this story already asks for: reach that changes is "seen, not discovered".
   * **38 step-height outliers** (5 tutorial, 30 hub, 3 shared with the detours below): links whose
     rise is above the NPC's 18 and inside the graph builder's 40. Predicted by the recovery and
     excused by the harness rather than failed -- the mesh is right and retail's own graph is
     asking for a climb its own motor would refuse. Story 5 decides what an NPC does on reaching
     one.
   * **3 rat detours on the hub**, of 1,759 ground links: indices 406 (1,086 cm straight, 4,561 cm
     routed), 721 (152 / 577) and 1,472 (1,120 / 5,229). The first guess -- the rat's finer cells
     resolving an obstacle the human's smooth over -- was tested and is wrong: all three are
     RAT-ONLY links, the human graph has no such link at all. Each is a rat hole, a passage only
     retail's 12 x 12 x 10 hull fits, and the rat's mesh joins its two ends the long way round.
     Leading hypothesis, not proven: Recast's quantisation closes the hole -- radius 15.24 cm on
     5 cm cells erodes ceil(15.24 / 5) = 4 cells = 20 cm a side, so a passage must be 40 cm wide
     where retail's rat needs 30.48, and 11 cells of 2.5 cm ask 27.5 cm of headroom against 25.4.
     They are PINNED by link index in `validation/nav_known_findings.py`: the gate is green with
     them reproducing, fails on any new finding, and fails on a pin that stops reproducing. The
     settling experiment (a rat mesh at 2.54 cm cells, four times the tiles) is handed to 5.

  **Closing note, 2026-09-20.** An independent review of the branch found four High defects after
  the jobs had "landed" -- the rat never actually received its hull (the body was sized in `Spawn`,
  the kernel filled the words in `Activate`), the crowd limit was decided where no controller
  exists, the navigation build lock was never released so no run-time modifier could ever apply,
  and the bake library broke a Game target. All fixed (`eed07f14`), and the lesson kept: the tests
  that missed the first one pinned the TABLE rather than the BODY. The acceptance below is rewritten
  to say what is checked rather than what was hoped, the gate runs by itself -- `import
  map-collision` judges the meshes it built before it may succeed -- and everything not built here
  has a home: hints / places / patrol projection and the zone report in 4; the step-height
  outliers and the rat-hole experiment in 5; characters as sight occluders and the stealth-lane
  sight witness in 6; per-agent doors and toggled `func_brush` blockers in 7; the cook check in
  20; the legacy deletion, the 102 other maps and folding this lane into `bake map` in 21; the
  species transform tasks under Seams for 0002.
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
     sidecar gains the same word. (It gained it to keep the legacy transport partitioning
     identically; story 21 retires that transport, and the sidecar stays as a V2 intermediate.)
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
     graph — 14, inside `FNavAgentSelector`'s 31 — generated from the recovered rows: radius half the
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
     lesson that turns is read against retail before it is accepted. **Closed 2026-09-20, read
     from `engine.dll` rather than captured live** (two walks, `navigation-jump-links.md` § "The
     engine's per-brush admission test"): the mask reaches the leaf loop unchanged through one
     global with five references, the admission is exactly `contents & mask`
     (`0x20030bb8 85 08`), `SOLID` is not required and `DETAIL` is not rejected, no leaf-contents
     pre-test exists — which matters, since these brushes sit in leaves of contents 0 — and a
     repo probe over lumps 10/17/18 confirms all 17 tutorial and all 894 `la_museum_1` brushes
     are listed by an open leaf. A non-solid OPAQUE brush STOPS a `0x2804091` trace.
  Acceptance, as it is checked. **Navigation** -- `uv run elysium verify nav`, which `import
  map-collision` runs itself on the meshes it has just built (a gate nobody runs is not one): every
  link whose motion for a hull has the ground bit paths on that agent's mesh within 3x the
  straight line; every jump endpoint projects on its agent's mesh; every BRIDGING link -- one only
  the rat has, joining node sets the human's links leave apart -- paths on the rat's mesh, the
  claim a one-mesh port cannot make; the level carries a mesh WITH TILES for exactly the agents its
  graph names. One expected class of outlier is excused, recovered 2026-09-20: the graph was laid
  down by `CAI_TestHull`, which steps **40** units (`0x102d72b0`), while every NPC steps 18
  (`CAI_BaseNPC::StepHeight 0x101a6b40`; only Ming Xiao 30, its tentacle 9 and Tzimisce 26
  differ), so a link asserting a rise between the two is reported with its rise measured, not
  counted a mesh defect -- cutting the agents at 40 would let NPCs climb what retail's own motor
  refuses. Findings already judged are pinned by link index and the gate fails only on NEW ones
  and on stale pins. *(Corrected here: this paragraph first demanded the bridging links NOT path on
  the human's mesh. Measured, the human mesh paths all 14, 70 cm to 9,155 cm -- this spec's own
  named modernization, "NPCs can reach floor retail's sparse graph never covered" -- so it is
  reported as reach that changed, with lengths, not failed.)*
  **The world** -- `Elysium.Content.MapCollision.{Tutorial,Hub}`: the SHIPPED payload authored into
  a live physics scene exactly as the bake authors it, and every brush of every signature body
  asked all three questions at its own centroid, of its own component (shipped brushes overlap, so
  the question is put to the body, not the point): an NPC-only brush stops an NPC and not the
  player; a sight-only brush stops sight and neither pawn; a window stops both pawns and no sight
  trace; the roadway stops nobody; the body count equals the map's signature count; each body cuts
  the NavMesh exactly when it blocks an NPC. 2,391 brushes on the tutorial and 3,882 on the hub,
  none answering wrongly.
  **The marks** -- `Elysium.Content.NavArea.{Hub,Tutorial}` over the baked levels and
  `test_map_nav_doors.py` over the derivation: hub 9 roadway convexes and 27 of 29 doors cut (the
  smoke-shop pair left for 7), tutorial 28 of 36.
  Both levels save and reload, adopting their meshes on a clean boot with no build and no
  tile-limit error. NOT checked here, each moved with its reason: hints, places and patrol points
  projecting onto the meshes (4 -- the place set does not exist yet); the AIN-zones-against-
  connectivity report (4); cook with no external export access (20 -- the CLI has no cook lane).
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
  How the six species acquire `m_eHull` is **closed** (2026-09-20; § "The two hull words"): a
  constructor writes it, the ledger never saw those stores, and there are two hull words — the
  agent follows `+0x156c`, the capsule `+0x1568`.
  **The `146–184` pair is closed (2026-09-21, `navigation-jump-links.md` § "The `146-184` pair,
  closed — a barrel, and a ray where retail sweeps a hull"), and this story's last open item with
  it.** Node 146's hull-0 offset is `+48.01` where every other node on the map is `-3.87` or
  `-8.87`: it stands on top of the static prop `models/scenery/structural/society/barrel.mdl`
  (patch BSP row 679; absent from retail's, the patch-first pairing again), whose collision top
  `6910.88` is its stand height `6911.01`. A centre ray misses the barrel; **`CAI_Node::InitLinks
  0x102fb4e0` sweeps the human hull**, which enters it immediately on leaving 146, so no link is
  built. `soc_int_locked_door` is disjoint in X from the segment at every height and is not the
  cause for THIS pair. Zones are a connected-components pass over links (`0x102f49c0` /
  `0x102f4940`, reading no geometry) computed at REBUILD only — the load path `0x102f5bd0` takes
  the serialized zone word — so "why no link" was always the right question.
  **The lesson, which outlives the finding:** an open-ray check against BSP brushes is not
  retail's admission test — it misses static props and hull width entirely. 4's zone report and
  any later "these should be joined" claim must reproduce with a hull sweep against props, not a
  ray against brushes.
  (Why links run through standing doors is
  closed: `MOVEABLE 0x4000` is the bit that hits doors, and the graph-build mask `0x2000b` is
  the only one without it — which is 7's door rule.)
  Size: L. Effort: Opus / high (exporter, payload, editor bake, nav config, verify).

- [x] **4. The place set.** Landed 2026-09-29.
  Retail: a node is where a patrol, a hint, a cover search and a hunt point. `CAI_Hint::
  GetPosition 0x102d1180` answers the hint's NETWORK NODE at hull height when `m_nNodeID != -1`
  and the hint's own origin otherwise; the patrol lookup answers the node (`+0x5e4`), not the
  hint. `CNodeEnt::Spawn 0x102d78d0` pairs BSP node rows with graph nodes POSITIONALLY, in
  spawn order, through the running counter `DAT_10926a3c`; an out-of-range counter keeps its
  id and attaches nothing. The install loads the patch's own loose graphs (mod directory, then
  base directory, then packs; the port's resolve had already landed with 3, 21-2 and 21-8):
  `sp_tutorial_1` 203 nodes / 429 links, `sm_hub_1` 578 / 1,862.
  What behaviour reads of a node: position, yaw, type, the per-hull Z offset
  (`node+0x14+4*hull`), its hint (`+0xa0`) and the run-time cooldown (`+0x9c`). Links, zones,
  the neighbour bitvector, link info and the 22 motion words are the engine's road network —
  evidence for the bake, never run-time data.
  Port today: no node data at run time. `FElysiumHint::NodeId` is a seam; `FindPatrolPoint`
  answers the hint entity and the patrol walks to its origin; `NavNearestNodeTo`,
  `NavAllHintNodes` and `GatherHintNodes` answer nothing. The spec's old pins (116 / 234) were
  the packed graph, which pairs with the unpatched BSP only.
  Job: the exporter names the row fields (`tail` = type, flags, neighbour bitvector; `lead` =
  zone, link count) of the graph retail would load — resolving that graph is not this story's
  job: it landed with 3, 21-2 and 21-8 (corrected 2026-09-29). The bake writes one cooked
  `UDataAsset` per map, referenced by the baked map content: a place row per node — network
  index, type, position, yaw, all 22 hull Z offsets (retail's own row: `GetPosition` may be
  asked for any hull; corrected 2026-09-29 from "the Z offset of each agent the map bakes"), the
  hint association by positional pairing with its unresolved and out-of-range outcomes kept, the authored
  `nodeid` as provenance — and the crosswalk pairs (two type-11000 hint nodes a link joins).
  No links, zones or masks. One number per baked agent rides beside the rows: the WANDER CAP
  (decided below). At run time a registry keyed by network index carries the
  cooldown; hints get their `NodeId`; a hint's stand position and a patrol point resolve to
  the node at hull height; `TASK_GET_PATH_TO_RANDOM_NODE` draws from the set. **"At hull height"
  is two questions, and the row carries every offset** — retail's `CAI_Node::GetPosition
  0x102fb0d0` is reached with the PATHING hull `+0x156c` from every routing site and with the
  STANDING hull `m_eHull` from seven others (patrol anchoring, the zombie's patrol arm,
  extrapolated routes, debug drawing; 3's § "The two hull words"). The two agree for every species
  but the Sheriff, Hengeyokai and Ming Xiao, so the asset keeps all 22 Z offsets and each caller
  names which word it is asking with. The bake reports
  every hint, patrol point and interesting place that sits off an agent's mesh, and every one
  no node covers, so reach that changes against retail is seen, not discovered. **Handed over by 3:**
  the AIN-zones-against-mesh-connectivity report, pinned -- retail's zones are its own partition
  of reachability, and 3's harness already showed the meshes join what the graph keeps apart
  (all 14 bridging links path on the HUMAN mesh too, 70 cm to 9,155 cm); this report is that
  observation made for every zone pair, beside the per-agent projection of every place.
  Acceptance: both witness levels load the asset with no external export access; the pairing
  reproduces retail's on the patch graphs (standalone hints, duplicate authored ids, the
  out-of-range arm); the thug's patrol points `A1..A3` resolve to node positions (corrected
  2026-09-29: `pt1..pt3` are interesting places, not patrol hints, `programs.md:1476`; the
  patrol points reaching them are `A1`/`A2`/`A3`, BSP rows 433–435, network nodes 15/16/17); the
  seams above answer from the registry. The wander draw, called as the task arm calls it: an order of 200 on the hub
  picks a place within 200 units and ahead of the body; an order of 4096 never picks beyond the
  cap; a type-4 place is never picked; a cooling place is picked only when no expired one stands;
  no place in range is `0x18` inside `StartTask`; the installed path carries no goal type and no
  tolerance. Pins, patch graphs: tutorial 203 places, all ground; hub 578, all ground; each
  agent's wander cap equal to `census_link_lengths`' `20xmed` for that map and hull (human:
  hub 3,113, tutorial 2,942).
  Provides: places to 5, 7, 8, 9 and 11 (corrected 2026-09-29 from "7–12": 10 and 12 consume no
  place — their own lines read 2, 5, 8 and 5, 6, 8 — and 6 asks geometry, not places).
  Consumes: 2, 3; and two surfaces of 5 and 6 that do not stand yet, built here as narrow seams
  for their owners to absorb (added 2026-09-29): the path-no-goal install `InstallPathNoGoal` (5)
  and the route length `IElysiumNpcMotor::RouteLengthTo` (6; landed as `QueryRoute`).
  Oracle: `navigation-jump-links.md` § "The loader, walked", § "Which graph the patched install
  runs on", § "What the shipped graphs and maps actually use",
  § "`TASK_GET_PATH_TO_RANDOM_NODE` `0x1f`, walked", § "The place set, landed".
  **Raised 2026-09-21 and DECIDED the same day (below): the read this story owed came back against
  the "no links" decision.** `TASK_GET_PATH_TO_RANDOM_NODE` is task `0x1f` (`0x10316ff0`), and it is
  **not a draw from a set of places**. Its arm `0x10285d7f` runs a random WALK over the AIN
  adjacency lists (`0x102ff3e0`), and the per-link predicate `0x102ff960` reads, at RUN TIME:
  `link+0x64` link info (bit `0x1000` rejects outright), the **per-hull motion word**
  `link+0x0c + 4*hull` AND-ed with the NPC capability word (slot 513), the far endpoint
  (`0x102dda40`), `IsJumpLegal` when the motion word is exactly `2`, and the **stale bit**
  `link+0x64 & 1` with its expiry `link+0x68` (`0x102fce80`, a failed re-probe notifying the
  blocker through `0x1027de00`). It also needs adjacency `node+0x78`/`+0x7c` and the **rotating
  per-node neighbour cursor `node+0xa4`** (`0x102f9750` / `0x102f9780`), which is mutable run-time
  state rather than serialized order. It reads no zone `+0x94`, no neighbour bitvector `+0x90`, and
  never calls `IsConnected`; and it needs only the ONE motion word the pathing hull `+0x156c`
  selects, not all 22.
  This matters because of WHO issues it: **15 schedules**, among them the base wander programs
  `IDLE_WANDER`, `PATROL_WALK`, `PATROL_RUN` and `RUN_RANDOM` (`0x102cb690`) and four cop programs
  `SCHED_VCOP_WANDER_PATROL` / `_SHORT` / `_AND_VANISH` / `SCHED_VCOP_RUN_TO_SAVED`
  (`0x10370b00`). **Story 20's witness — "pedestrians visit places, cops patrol" — runs through
  this task**, so it cannot be deferred as an unreached arm.
  Three ways out were weighed: **(i)** a reduced adjacency in the cooked asset (endpoints,
  `link+0x64`, one motion word per baked agent) and the walk ported verbatim; **(ii)** a named
  modernization over the NavMesh; **(iii)** places only, the 15 schedules not running and the hub
  witness forfeited. Two more stood beside them and were set aside: MassEntity + ZoneGraph lanes
  (experimental in the 5.8 install, a second movement stack under one body, and a lane model the
  area-covering AIN does not fit), and the hop graph REBUILT from the places — held in reserve,
  last paragraph.
  **DECIDED BY THE OWNER, 2026-09-21: (ii), as the CAPPED POINT PICK** — the least build for the
  value. Retail's everyday orders are 200 units against a median hop of 156–174, so an idle wander
  is one or two hops and "a nearby place ahead" IS that walk; the designs differ only on the long
  orders (2048 and up), by some 25–30 % of distance at most. (i) was refused on the boundary's own
  terms: every run-time link read in `0x102ff960` is dead by content or already translated —
  link-off `0x1000` unreachable, motion only ever 1 or 2 (the per-agent meshes and jump links),
  the stale bit the door's failure timer — so it would carry a second connectivity truth beside
  the NavMesh to buy exactness in what the boundary already names noise.
  **The pick, entire.** `direction = BodyDirection2D()`; `distance = min(ResolveTaskDistance(
  flTaskData), the agent's WANDER CAP)`. Candidates are the place rows within `distance` of the
  NPC in a straight line, a type-4 place never among them and slot 527's usability test
  (`0x1027db30`) kept. The two-tier cooldown `node+0x9c` orders them — expired places first, a
  cooling one only when no expired place stands. Within the tier, the preferred band is the places
  AHEAD (positive 2-D dot against `direction`) in the outer half of the range
  (`>= distance / 2`); that empty, every place ahead; that empty, every candidate; a zero
  `direction` skips the ahead test. ONE `RandomInt(0, count - 1)` on the engine stream picks within
  the band. Unreal routes to the pick on the agent the pathing hull `+0x156c` selects, under the
  DEFAULT filter — this task never sets the pedestrian byte, so the roadway is not priced. A pick
  with no route, or whose route runs far past `distance` (the far side of a wall), is dropped and
  the next drawn; the detour ratio and the retry count are port constants, named where they are
  defined. None left is `TaskFail(0x18)`. The route is installed as a PATH through 5's seam — no
  goal, no tolerance — inside `StartTask`.
  **The wander cap** stands in for the iteration guard `0x14`: `20 x` the median 3-D length of the
  links carrying motion for that agent's hull, computed by the bake from the graph it already
  reads and written as ONE NUMBER per baked agent beside the place rows — no link ships, and an
  agent whose hull carries no link takes the human figure.
  `research/tooling/probes/census_link_lengths.py` is the measure (2026-09-21, 97 graphs, 27,956
  human-hull links): median 174, p10–p90 72–391, longest 800 — the builder's own reach — so a cap
  of 3,113 on `sm_hub_1` and 2,942 on `sp_tutorial_1`. By that measure the guard never fires ahead
  of an order of 1024 or less on any map, and ahead of 2048 only on eleven small interiors; the
  cop's 4096 meets it on `sm_hub_1` / `sm_hub_2` and not on the other three hubs. A central
  estimate, named as one: the walk takes the straightest hop, not a median one.
  **Named modernization:** the wander's route and the places it passes are Unreal's; its endpoint
  is a retail place chosen by retail's tests; the guard is a baked distance. Given up, knowingly:
  the dot-to-dot meander, the rotating neighbour cursor `node+0xa4`, the per-step replacement of
  the heading, the visited set, and accumulated path length (a straight line checked against the
  route stands for it).
  **Held in reserve.** If the hub witness (20) shows the pick wanting — cop beats that read wrong,
  pedestrians converging — the upgrade replaces this one function and nothing around it: the hop
  graph rebuilt at bake from the places by retail's OWN neighbour rule
  (`navigation-jump-links.md` § "How the graph builder picks a node's links": 800 units, the `0.9`
  cone, the farther loses) over Unreal's straight-line walk test and the baked jump links, and
  the walk ported over it. That one does amend the boundary's "no adjacency" sentence, so it is
  the owner's call again when it comes.
  What retail's task observably does is recovered, and the pick keeps the part of it a place
  draw can keep (the last two clauses are what the paragraph above gives up): **failure is
  `TaskFail(0x18)`**, not `0x0c`; `RunTask` is an empty break because the task completes
  synchronously in `StartTask`; the result is installed straight into the navigator's PATH object
  (`0x1030ba50(path, 4)`, `0x1030b4d0`, `0x1030b8e0`, endpoint distance² at `navigator+0x14`) and
  **`SetGoal` is never called**, so there is no goal type and no tolerance; the cooldown
  `node+0x9c` defers rather than filters; a type-4 node is never the final node; the walk stops at
  the resolved distance or an iteration guard of `0x14`; and selection is `RandomInt(0, count-1)`
  once per step, with a directed variant that keeps only a STRICTLY greater dot product and then
  replaces the heading with the step taken.
  *(For the avoidance of the doubt this paragraph caused once: "links, zones … never run-time
  data" above is a statement about the PORT's cooked asset, not about retail — retail plainly
  reads them, as `0x102ff960` shows.)*
  **Landed 2026-09-29 (box open until the smoke is recorded).** Pipeline: the nav-graph node
  fields named (`type`, `flags`, `neighbourBits`, `zone`, `linkCount`; 461febc8);
  `importers/map_places.py` stages the `places` block (manifest 16 → 17) with `CNodeEnt::Spawn`'s
  counter rule, the crosswalk pairs and one wander cap per declared hull;
  `pipeline/unreal/bake_places.py` cooks `/ElysiumBaked/<map>/DA_<map>_Places`
  (`UElysiumMapPlaces`), which the travel gate `HasBakedMap` now requires. Runtime:
  `FElysiumPlaceSet`, owned by the entity world — the rows, `GetPositionCm` (`0x102fb0d0`, all
  three arms, the climb arm included), the node cooldown `+0x9c` (storage and read), the attached
  hint `+0xa0` and the counter `DAT_10926a3c` (the out-of-range count `DAT_106c994c` is one
  process-wide word, `ElysiumAiNetwork::NodeMissCounter`, never reset -- `0x102f6690` and the
  patrol-pool reset `0x10307d00` zero only their own words); the counter
  runs in `FElysiumEntityWorld::Load` and writes `FElysiumHint::NodeId`, retiring every node row
  that makes no hint; `CAI_Hint::OnRestore 0x102d3ec0` relinks as `FElysiumHint::OnPostRestore`
  (retail saves no node state). The seams answer from the registry: the patrol (`PatrolNodeIdFor`
  is the hint's `m_nNodeID`, `PatrolNodePosition` the node at the caller's hull word), the hint's
  node arms `0x102f46d0` / `0x102f47b0`, `NavNearestNodeTo 0x102f41b0`, `0x102f3c10`, slot 527,
  and `NavAllHintNodes` on the live hint list. `0x102c7600` writes the NODE's `+0x9c`, settling
  the lifecycle comment that said `+0x9c` and wrote `+0x5ec`. The wander is the capped point pick
  in `StartTaskSetRandomGoal`, with the port constants `kWanderDetourRatio` 2.0 and
  `kWanderMaxDraws` 5; a dropped pick leaves the pool, and tier and band are re-taken. The bake's
  gate writes three observation reports, unpinned: places off mesh, uncovered points, zone pairs.
  Numbers (census over the 108 maps; oracle § "The place set, landed"): 11,517 type-2 nodes, 37
  type 1, 4 type 4, never type 3; `flags` 1 on 4 nodes (writer unrecovered); every map stages with
  0 out-of-range hints and 0 `nodeid` disagreements; tutorial 203 places / 49 bound hints, hub
  578 / 274; caps human 2,942.141 (tutorial) / 3,113.326 (hub), rat 3,097.891 / 3,185.672; `A1..A3`
  are nodes 15/16/17; the hub's crosswalk rows (BSP 1613–1618) are nodes 258–263, 8 pairs. The
  reports: 0 places off mesh on both witnesses; 0 uncovered points (coverage is `0x102f41b0`'s
  ±2048 box); zone pairs joined on the human mesh, tutorial 2 of 45 (`4/7`, and `11/12` = the
  `146–184` barrel pair), hub 3 of 6 (`1/5` at a 9,978 cm path, `1/6`, `5/6`); rat mesh tutorial 2,
  hub 1; same zone unjoined 0. Tests: `Elysium.Substrate.PlaceSet.*`,
  `Elysium.Substrate.PlaceSeams.*`, `Elysium.Content.Places.{Tutorial,Hub,WanderHub}`,
  `test_map_places.py`, `test_bake_places.py`.
  Named modernization: the capped point pick (decided 2026-09-21, above). Named divergences:
  `0x102f46d0` / `0x102f47b0` test `id <= count`, and the port answers the origin / `0.0` for
  `id == count` instead of reading past the node array; `TASK_WANDER 0x76`'s fallback
  `SetRandomGoal(1.0, …)` fails `0x18` under the pick where retail's walk takes one hop (no shipped
  schedule issues `0x76`); the Werewolf's nearest-node cache (`FUN_103d0bf0`) holds an index, not
  a pointer, so a nearest node 0 re-queries; the 20-entry nearest-node cache `0x102f4520` /
  `0x102f45f0` is not ported.
  Stays open: the hint searches, claims and `IsHintAvailableToMe` (8); the cooldown's claim write
  (9); the crosswalk link over the baked pairs (7); `SetGoal`'s goal-flag-2 node route
  (`ElysiumNpcBaseStartTask.cpp` ~:1976) and the radial probe `0x102ed610` (5);
  `CheckStandPosition` and the hull trace inside `CanFitAtNode` (6); pins for the new reports
  (21-9); rat-mesh noise in them (21-10).
  **Smoke (2026-09-29, `sm_hub_1` then `sp_tutorial_1`):**
  - *Hub load.* The hub adopts 578 places (hulls `0x80001`, 2 caps), and hints read their nodes (`s3` → 453).
  - *The wander pick.* `SCHED_VCOP_WANDER_PATROL_SHORT` was forced on both `copcar` cops. The pick answered (no `0x18`) and each walked ~3 m, then ended `Blocked` against the other and failed `0x0c`. That is crowd blocking, which is row 08's. At idle nothing on the hub issues the task: pedestrians run the ambient loop, and the patrol cops run `0x67`.
  - *Tutorial.* `A1..A3` are bound to nodes 15/16/17. `sentry2` loops, and the monk loops up to its wrap leg, both as in run 3 (`5b0a5a40` holds).
  - *The hub patrol cops still stall, as they did in run 3 before this story, so this is not a regression.* Unreal routes `patrol_cop`'s `s2 → s3` over AIN jump link 731 (151 ↔ 160, 247.9 units, motion 2). Retail's link predicate `0x102ff960` refuses that link through `IsJumpLegal` (slot 521, `0x10280880`: 80 up / 250 down / 160 across). The jump service cannot begin it, and the move aborts `InvalidPath` → `0x0c` → `FAIL 0x43`. The bake places jump links without that legality test, both ways (hub: 95 of 117 fail it both ways; tutorial: 17 of 25). The gate's path lengths use the default filter, so a ground link that paths only over a jump link passes. Separately, the jump service refuses LEGAL jumps too (hub link 88, the tutorial's 390). All of this is routed to **7** (the jump links carry retail's legality, per direction) and **5** (the jump seam).
  - *New line.* `monk_upstairs_podium TaskFail 0x1d` fires between `SetupPatrolType` and `FollowPatrolPath` after a cold `map_load`. It is unproven whether this is new; story 4 does not touch the patrol cells.
  Size: M. Effort: Opus / high.

- [x] **5. The navigator and the movement seam.** Landed 2026-09-29.
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
  readers read. One arm installs a PATH and no goal: `GET_PATH_TO_RANDOM_NODE` (4's decided
  block) hands the seam a route to a place with no goal type and no tolerance, the squared
  distance to its endpoint at `navigator+0x14`, and `SetGoal` is never called
  (`0x1030ba50(path, 4)`). The seam, in the NPC's words: request a move; outcomes arrived, failed with a
  code, blocked by a door, blocked by an NPC; a route sample (has a path, distance left, next
  corner). Under it, on the body: a path-following component that raises those events; the
  default and the pedestrian query filters, the pedestrian one pricing 3's area UP by ONE
  `RandomInt(5, 10)` drawn per request; the acceptance radius equal to retail's tolerance
  with nothing added for the capsule; partial paths off; an off-mesh goal projected within
  the tolerance and refused beyond it. The 0.8 s think gate.
  **The pedestrian price is an AVOIDANCE, and this story's own text had the sign backwards**
  (corrected 2026-09-21 by 21-8, before the filter was built). Retail's A* `FUN_102fe9f0` draws
  `local_20 = RandomInt(5, 10)` once per request and applies it as
  `cost = cost * local_20` on a link whose flag is set, then relaxes a node only when the new
  total is SMALLER -- so the multiply is a penalty. The pedestrian keeps to the pavement and pays
  5-10x to enter the roadway; `FUN_103055b0` passes the flag byte as `'\0'` and everyone else
  pays the unpenalised cost, so the road is never a wall. `ElysiumNavAreas.h` said "priced to
  prefer" and now says what it does.

  **And the flag is per-LINK, which gives this story an exact acceptance set it did not have.**
  The brush contents `0x2000` that 3 marked as an area lands in `CAI_Link::m_LinkInfo` at
  `link+0x64` -- the nav-graph unit's `fields[0]` (`seam_map_nav_graph.md` § "Link stream",
  recovered 2026-09-21 from the loader listing). It is the only value that field ever takes:
  **1,331 links across 40 of the 108 maps**, and on `sm_hub_1` exactly **461 flagged against
  1,185 unflagged hull-0 ground links** -- the two numbers `ElysiumNavAreas.h` had already
  derived from the brush side, reached here independently from the link record. So the area mark
  and the link flag are two views of one fact, and this story's filter can be judged link by link
  against the published units rather than by eye: every link the port prices up should be one
  retail flagged, on every map, and the 68 maps with no flagged link should see no pricing at all.

  Three things story 3 hands over rather than settles. **The route's hull is `+0x156c`**, read at
  `SetGoal 0x102ecd2c` and refreshed each frame by `CAI_Navigator::Move 0x102effe1` — this
  navigator is the object that owns that read, and 3 only supplies the word (§ "The two hull
  words"). **The step-height outliers**: retail's graph was laid down by `CAI_TestHull`, which
  steps 40 (`0x102d72b0`), while NPCs step 18 — 3's harness reports every link asserting a rise
  between the two, with its rise measured, and this story decides what an NPC does about one
  (refuse the link, or accept a gait that cannot climb it). Cutting the agents at 40 is not an
  option: it would let NPCs climb what retail's own motor refuses. **The rat holes**: three hub
  links only the rat has (406, 721, 1472) path on the rat's mesh the long way round, pinned in
  `validation/nav_known_findings.py` with the leading hypothesis that Recast's erosion (20 cm a
  side at 5 cm cells, against the rat's 15.24) closes a passage retail's rat fits. The settling
  experiment is a rat mesh at 2.54 cm cells; if it opens them, this story decides whether four
  times the tiles is worth three rat routes.
  Acceptance, on the baked witnesses: each goal type issued and its tolerance observed at
  arrival; a refused route raises `0x0c` at once without the retry word and at the deadline
  with it; a schedule change drops the goal and the pedestrian byte, `PRESERVE_PATH` keeps
  them; a pedestrian request prices the hub's roadway and a plain one does not.
  Provides: movement and its outcomes to 0002 and 0003. Consumes: 3, 4.
  Oracle: `navigation-jump-links.md` § "The route gates, walked" (its `SetGoal`,
  `0x102f1dc0` and `DoFindPath` paragraphs; the builders below them are engine record).
  **Three corrections to the paragraph above, read off `vampire.dll` 2026-09-21.**
  *(a)* **Goal type 5 exists and this story omits it.** `DoFindPath 0x102f2330`'s switch reads
  `case 4: case 5: case 6: case 9: break;` — type 5 takes no preparation and falls into the route
  builder `0x102f2060` exactly as 4, 6 and 9 do. (Type 8's arm is visible in the same switch as the single store
  `*(goal+1) = 1`, the pedestrian byte.) **Closed 2026-09-21 (`navigation-jump-links.md` § "The goal
  types, their issuers and the goal record"): type 5 has NO issuer** — none of the 15 functions that
  reach `SetGoal` builds one and no text names a task that would — so its arm is unobservable; and
  the two types this story lists bare are named by their issuers: **7 is the best-unknown goal**
  (`GET_PATH_TO_BESTUNKNOWN 0x79`, six shipped texts; the two-hull tolerance; sets path byte 0)
  and **9 is the ANIMAL's interesting-place goal** (`CNPC_VAnimal`'s override of task `0xa5`: the
  same walk as 8 WITHOUT the pedestrian byte, so no roadway pricing). The section also lays out
  the sixteen-word goal record (`+0x14` is the activity, `+0x24` the flags) and the flag bits.
  *(b)* **The 0.8 s gate is the LOADED-graph case only.** `0x102f6690` sets the network manager's
  first think at `curtime + 0.8` (`_DAT_104491a8`); if the graph is out of date its think
  `0x102f6a50` prints `Node Graph out of Date. Rebuilding...` and re-arms at `curtime + 1.0`
  (`_DAT_104454c0`), and the network is not marked built (`+0x658 = 1`) until that SECOND think —
  so a rebuilding map starts NPC thinking at ~1.8 s. The port bakes from the graphs retail loads,
  so 0.8 s is the case it reproduces; say so rather than implying 0.8 s is unconditional.
  *(c)* **The blocked-move rule above is necessary but not sufficient** (`0x10303850`, read
  2026-09-21). After `MoveLimit` returns a negative status, the partial move is accepted — a
  waypoint built at the blocked endpoint — only when ALL FOUR hold: route flag `0x100` set, goal
  flag `0x8` set, the remaining distance under the tolerance, and `|dz| < 2.0` (`_DAT_10449400`).
  Fail any one and retail instead tries triangulation `0x10304020` when route flag `0x20` is set,
  and then, only for status `-3` (an NPC) with route flag `0x10`, re-runs `MoveLimit` under the
  plain mask `0x2400b` and admits the move if `0x10303fd0` accepts the blocker. The trace mask is
  itself route-flag-driven: `(~flags & 0x40) << 0x13 | 0x2400b`, so flag `0x40` is what drops
  `MONSTER 0x2000000` from the sweep.
  *(d)* **The goal survives mid-CLIMB too, and the tolerance does not survive either way**
  (`CAI_BaseNPCTroika::OnScheduleChange 0x102a0940`, read 2026-09-21). `PRESERVE_PATH` is bit `8`
  of `m_bfAINPCFlags`; with it clear, the body skips the navigator's goal clear `0x102ee270` when
  the nav type is **3 (Climb) or 1 (Jump)** — this story names only jump. Everything after the
  skip runs regardless: `m_flGoalTolerance`, both interrupt distances and `m_flInterruptTime` are
  zeroed, `m_bShouldMove` cleared, `m_hMoveTargetEnt` released, an `m_hOpeningDoor` sent input 8,
  and the interesting-place cache `+0x6300` and patrol-interest cache `+0x659c` both wiped. So
  "the goal and the pedestrian byte survive a schedule change mid-traversal" is true; "the
  schedule's tolerance survives" is not.
  **Corrected 2026-09-29 (wave 0 reads; oracle `navigation-jump-links.md` § "The navigator's words
  and getters", "Route-control flags, their issuers and the blocked-move arms",
  "`CAI_Navigator::Move 0x102eff40`, walked", "The think gate, the path-corner chain and the step
  rise").** *"Port today" was stale:* the retry window, `SetGoal`'s tolerance rules and
  `OnScheduleChange`'s `PRESERVE_PATH` / Jump / Climb handling were ported before this story.
  *Correction (c)'s "goal flag `0x8`" is a misread:* the `& 8` word is `BuildLocalRoute`'s FOURTH
  ARGUMENT, a route-control literal meaning "this leg ends at the goal" (waypoint `+0x28`), not
  goal word `+0x24`. Blocked-move acceptance (mask `0x100` + that literal) applies only to walkers'
  local attempt and the node-route exit leg; the NPC-blocked `D_LI` re-probe (mask `0x10`) only in
  the local attempt. *Goal flag `0x2` has ONE shipped issuer:* Troika task `0xc7
  GET_PATH_TO_ENEMY_CLOSEST`, schedule `SCHED_VTZIMISCE_ATTACK_CLOSEST`; flags `0x4` and `0x8` have
  none. *`nav+0x44`, the retry interval, is never written non-zero:* the constructor skips
  `+0x40..+0x4c`, so retail retries every think until the deadline and the port's 0 is right.
  *Arrival is NOT `path+0x28`:* a waypoint arrives at a constant `0.0625` (0.25 under the vphysics
  cvar), 2-D for ground nav and 3-D otherwise; `path+0x28` is read only by the completion test
  (`+ 0.1`). *`Move` never re-paths* (the re-path-on-move arm is dead by content), and *door `0x0e`
  is not `Move`'s:* `SimplifyPath 0x102f13d0 -> 0x102f06e0 -> slot 531` raises it. *The census
  above is the wrong graphs:* "1,331 links over 40 maps, hub 450" is the BASE `Vampire/` set
  (1,320 / 40) mixed with the patch hub count; the patch graphs the game runs on carry **1,277
  flagged links over 32 maps, hub 461**, and the gate judges those. *The 0.8 s gate is relative*
  to `CWorld::Precache` (re-run on a restore), not to a map-absolute clock; Troika's `NPCThink`
  gates only on the loader byte `DAT_1093408c` and re-arms at 0.1 s, and `manager+0x658` is read
  only by the base body `0x1026ca80`. Port mislabels found on the way: `0x102ee620` is the goal
  TYPE (`path+0x5c`; `ValidateNavGoal`'s 6 is a cover goal), `0x102ee680` is `IsGoalSet` (type
  `!= 0`) and `IsGoalActive` is `0x102ee6a0` (`path+0x24`, a current waypoint exists),
  `0x102ee2e0` reads `path+0x10` `m_bPaused`, `CalcIdealYaw` reads the movement activity
  `path+0x2c` (`0x102ee3f0`), and `NavigatorGoalType` (`0x1027d990`) is the NAV type `nav+0x18`
  (ManBat's `!= 2` is "not flying"). Type 3 (the goal-entity chain): `AdvancePath 0x102f0400`
  fires each corner's `OnPass` with the NPC as activator and copies the corner's `speed` onto the
  NPC; `wait` is never read. A step rise of 18-40 is refused by the probe (`-2`), stale-marked for
  4 s and failed `0x0c`: a pin, not code.
  **Landed 2026-09-29.** Runtime: `FElysiumNpcNavigator` (`ElysiumNpcNavigator.h`) holds the path
  and goal words by their retail offsets (type `path+0x5c`, flags `+0x24`, paused `+0x10`, the
  activity `+0x2c`, the completion distance `+0x28`; the nav type `nav+0x18`, the retry pair
  `nav+0x40/0x44`), every writer and reader recovered, `ResetPath` clearing retail's four words
  (`0x1030bb30`: flags, target, paused, `+0x20`). The seam is `FElysiumNpcMoveRequest` /
  `FElysiumNpcMoveFacts` on `IElysiumNpcMotor`: the body reports FACTS (a request finished, how,
  what stopped it, the blocker's class, the 2-D and 3-D distance left, the snap offset) and never
  a verdict; the outcomes are the substrate's. `NavigatorMoveStep` is `CAI_Navigator::Move
  0x102eff40` row for row: the gates (paused, `m_bShouldMove`, no goal type -> `0x0d`, no head
  waypoint -> `0x0c`, `m_flMoveWaitFinished`), arrival at the constant `0.0625`, the world block
  (`0x0c` with the 4.0 s stale mark `0x102f1fa0`), the stale-mark seam for the marks the port has
  no source for, the NPC-blocker hold (`NavBlockerHold 0x102ef3e0`) and its head-leg re-issue
  (the port re-issues the leg because Unreal's follower has already ended the request), the
  second-contact window, the within-goal-tolerance acceptance (`0x102ef760`), the `path+0x28 + 0.1`
  completion and the target-hit completion (`NotifyHit` honours `bSelfMoved`; the player pawn is
  named as a blocker). `TaskMovementComplete`'s `AdvancePath` and the path-corner chain (`OnPass`,
  `speed`; modes 4/5 lay the chain through `ScheduledFollowPath`; every corner arrives at
  0.0625) stand, as does the goal-flag-2 node route (`SetGoal`'s arm at
  `ElysiumNpcBaseStartTask.cpp` ~:1976, refusing with no network or no node near the goal). The
  goal projects at the engine's DEFAULT extent (retail never snaps; the snap offset is a fact,
  91 x 91 x 91 cm). The pedestrian filter `ElysiumNavQueryFilter_Pedestrian` prices 3's area by
  the ONE `RandomInt(5, 10)` per request, goal type 8 only (type 9's animal walk and plain
  requests take the default filter); `pipeline/.../validation/pedestrian_link_census.py` judges it
  link by link against the published units: **1,277 flagged links over 32 maps, hub 461 flagged
  against 1,185 unflagged hull-0 ground links**, none flagged on the other 76. The 0.8 s gate:
  one per-world timestamp, `FElysiumEntityWorld::BuildStamp` (`BuildStampSeconds()`), stamped at
  build; `Think19` reads `now - BuildStamp >= 0.8` for the base body only; Troika gates on the
  loader byte, which answers false only while a map's place set is pending
  (`SetPlaceSetPending`, wired in `AdoptMapPlaces`; a bare world thinks, as retail's rebuild arm
  sets the byte too); a restore re-stamps it (`ElysiumEntityWorldPersistence.cpp`). The seams
  the story left to others answer "nothing" and name the retail field. *Found outside the
  list:* `ChangeSchedule 0x10280de0` installed UNTRANSLATED ids; retail's `SetSchedule(int)
  0x102cc1f0` runs slot 440 `TranslateSchedule` first. Fixed in `ElysiumNpcBaseSchedule.cpp`.
  **Acceptance, each line answered.** *Each goal type issued, its tolerance observed at arrival:*
  `Elysium.Substrate.Navigator.SetGoal.WritesTheWords`, `...MoveStep.Arrived.{GoalWaypoint,
  NonGoalHead,LastCornerInPass}`, `...MoveStep.Blocked.WithinGoalTolerance`,
  `...CornerChain.{ThreeCorners,SingleCornerIsGoal,NullNextCornerEndsSilently,
  PathcornerTask_0x120}`, `...GoalFlag2.{NodeRoute,NoNetworkRefuses,NoNodeNearGoalRefuses}`; live,
  the unblocked cops ARRIVE (below). *A refused route raises `0x0c` at once, and at the deadline
  with the retry word:* `...RetryWindow.NoIntervalRetriesEveryThink` (the interval is never
  non-zero in retail, so the "with it" arm has no shipped case), `...MoveStep.Gates.
  {NoGoalType,NoHeadWaypoint}`, `Elysium.Visual.NpcBody.MoveFacts.{RefusedRequest,
  NoController,AlreadyAtGoal,EndsAndNewRequest,BlockerAndPartial}`, `...MoveRequest.
  FollowerRequest`. *A schedule change drops the goal and the pedestrian byte, `PRESERVE_PATH`
  keeps them:* `...Navigator.Reset.Values`, `...Pedestrian.ByteAndMultiplier`, and the
  `ChangeSchedule` translation fix (the `PRESERVE_PATH` / Jump / Climb handling itself pre-dates
  the story). *A pedestrian request prices the hub's roadway and a plain one does not:*
  `Elysium.Content.NavArea.{Hub,Tutorial,PedestrianFilter}`, `test_pedestrian_link_census.py`,
  and the smoke. The rest of the suite: `...MoveStep.Gates.{Paused,ShouldMoveClear,MoveWait}`,
  `...MoveStep.Blocked.{NpcHoldResumes,NpcHoldExhaustedFails,SecondContactWindow,NpcWhileWalking,
  WorldFailsWithStaleMark}`, `...MoveStep.OnNavFailed.ResetsBlockerMemory`,
  `...MoveStep.LegacyMotor.StatusFallback`, `...Navigator.{Getters.NoGoal,PathSample.
  FromMoveFacts}`, `Elysium.Substrate.Think19.Gate.{BaseLine,RestoreRestamps,
  TroikaIgnoresNetworkGate,LoaderByteFollowsPlaceSet,AiStepReadsPathHead}`.
  **Smoke (2026-09-29, `sm_hub_1`, second pass at `998d85d1`; the first pass is superseded).**
  An UNBLOCKED cop arrives: three of three wander walks of 45-50 m (40-52 s) ended `Move: arrived`
  with 1.0, 1.0 and 14.8 cm left, no `TaskFail`. Pedestrian requests (goal type 8) carry x5..x9
  and the pedestrian filter (9 first requests: x5 once, x6 once, x7 twice, x8 three times, x9
  twice); every plain walk (cop wander, `patrol_cop`'s path-corner walk) is x0 with the default
  filter, with no mixed case. 0 `ensure` / assertion / crash over ~153k log lines, 0
  `TaskFail 0x0d`. Pedestrian outcomes seen: `arrived` (1.0 cm), `arrived (blocked inside the
  goal tolerance, 0x102ef760)` (41.4 cm), `0x0c`, and the retail `0x22` arm.
  **Named modernizations:** WHEN the NPC hold starts (after Unreal's follower gives the request
  up, not at first contact); the body's Success end counts as arrival, and a partial-path end is
  not arrival; the 1 cm follower floor (`FollowerArrivalFloorCm`) under retail's 0.0625-unit
  arrival; Unreal's crowd avoidance stands for the local steer and `PrependLocalAvoidance`; the
  flag-2 route is Unreal's between the two bound nodes; the pedestrian price is the filter's
  area cost, drawn once per request as retail draws it; the re-issued leg's `Blocked` end is
  judged as of the re-issue time (Unreal's follower takes ~5 s to give a request up, longer than
  retail's 3.0 s window), with retail's own `distClear < 1.0` (`0x102ef0e0`, `0x10449280`) deciding
  whether the leg walked on.
  **Stays open / handed on.** (1) *The NPC hold's cadence* — found by the second smoke and fixed
  the same day (`414ac129`): a cop held by an NPC re-armed `hold start` every ~14.8 s for 3 min,
  because every think re-read the same `Blocked` end as a new contact. The 14.8 s is RETAIL's
  cadence, kept: `CalcNextNormalThink 0x10290b60` gives `(dist − 2048) · 3 / 4096` s, ×10 out of
  the player's PVS (`+0x6278`, `_DAT_1044e664`) capped at 16 s (`_DAT_10451ad0`), ported in
  `ElysiumNpcThinkCadence.cpp`; the +0.1 s re-arm is only the refused-gate path (`0x10293411`).
  The hold now reads a re-read end only for its hold test (`curtime − nav+0x58 <= −0.001`), a
  hold over re-issues the leg, a lapsed window restarts from that think (`nav+0x60`), and the
  re-issued leg's `Blocked` end by the same NPC with `< 1.0` unit walked is the exhausted hold →
  `−3` → `0x0c`. Tests `…Blocked.NpcHoldRetailCadence` (0.1 s thinks, the −0.001 edge at 0.2485 /
  0.2495) and `…Blocked.NpcHoldSlowCadence` (15 s thinks: Blocked → hold → re-issue → Blocked →
  `0x0c` in three thinks). Third smoke (`smoke3.md`): cop `#2557`, both cops forced at spawn —
  body `Blocked` → `hold start` +1.3 s → `hold over, head leg re-issued` +14.8 s → second
  `Blocked` → `blocked by an NPC (−3), failed 0x0c` +10.2 s; three `hold start` / three `hold over`
  in the whole run, no loop; two pedestrians held and then `arrived (blocked inside the goal
  tolerance, 0x102ef760)`; the cop blocked by the car (not an NPC) failed `0x0c` with no hold, as
  retail's `−1`. One more jump refusal seen (hub links 997 / 797, `cannot begin traversal`) →
  row 13's list. (2) *The `copcar` pair's stall at spawn* is the police
  car, a `prop_dynamic` the NavMesh does not carry: both stop at x~3861 after 2.3 m whether or not
  the other moves, and moved off it they arrive. Nav obstacles for prop collision -> **3**'s
  contents marking and row 13 (7). (3) *The jump seam's refusal of legal jumps* (hub link 88,
  tutorial 390) and retail's legality per direction -> **7** (row 13); *the door policy* behind
  slot 531 and `0x0e` -> **7**. (4) `RouteLengthTo` stays a seam -> **6** (the path-length
  service; landed as `QueryRoute`). (5) *The rat links* 406 / 721 / 1472 -> **21-10**. (6) *`hw_hub_1`'s 124 of 513
  UNFLAGGED ground links cross a `0x2000` box*: reported, not gated, cause uninvestigated ->
  **21-9** (or 3's contents marking). (7) *The step-rise pin* (18-40, refused by the probe, `0x0c`
  after the 4 s mark): story 3's harness report. (8) *Three 0.8 s-think tails are unwired:* the
  `ai_node_graph_built` event and the dynamic-link initialisation `0x102cc900` -> **7**, the
  per-NPC slot-584 pass `0x1028d8d0` -> `0019/6`. (9) `TaskMovementComplete` logs nothing; the
  arrival is visible as `Move: arrived`.
  Size: L. Effort: Opus / high.

- [x] **6. Geometry services.** Landed 2026-09-29.
  Retail: the questions behaviour asks of the world. Sight, mask `0x2804091` (3's sight
  channel). The hull trace and the hull table's extents. The stand test `CheckStandPosition
  0x102e7270` (`0x102a0ed0` is `CanStandAt`, its `m_bForceNPCCheck` wrapper; corrected at landing):
  start 0.1 above the point, drop by slot 523 (36.0 on the Troika line), a foot box of the NPC's
  `m_Collision` box (not a hull-table row) shrunk a quarter each side, pass only on ground that
  slot 166 accepts. The cover
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
  **Handed over by 3:** the sight channel answers brushes, movers and props only. Retail's one
  sight mask `0x2804091` carries `MONSTER 0x2000000`, so in retail a body standing between two
  points breaks the line; in the port no pawn profile blocks the channel. That is a NAMED
  DIVERGENCE of the port today, not retail's arrangement, and this story -- which adds the cover
  and shoot-node traces on the same channel -- is where the character arm is wired. With it
  comes 3's unbuilt witness: the `sp_tutorial_1` stealth lane traced from the thug's eye along
  `pt1..pt3` on BOTH the old +use channel and the sight channel in one run, each flip
  attributed to a signature (17 sight-only brushes start blocking him, 276 window and grate
  brushes stop), so a lesson that turns is read against retail before it is accepted.
  Provides: the questions 7–12 ask. Consumes: 3.
  Oracle: `navigation-jump-links.md` § "The cover search, walked" (the validator),
  § "The back-away and shoot-node searches, walked" (the stand test); `senses.md` (the sight
  mask and weapon line of sight).
  **Landed 2026-09-29** (`a3941ddc` seam, `fbd3f682` bodies, `17b55d99` review V1, `3ecdb573`, `5f48fe01` live
  and V2 fixes; reads R1, R2). **Corrections to the "port today" text (survey, before code).** (1)
  `QueryLineOfSight` has traced the sight channel since story 3, not the use channel; the stale comments are
  rewritten. (2) `RetailHullExtents` was real (the 22-row table); the zero box was `RetailCollisionExtents`,
  so `IsValidCover` and `IsAreaClear` swept a ray. (3) `RouteLengthTo` and `InstallPathNoGoal` were real, but
  `RouteLengthTo` ran the DEFAULT filter while the pedestrian `MoveTo` prices the roadway. (4)
  `PositionClearForTeleport` answered false only in the Asian-vampire body; four species rules were real. (5)
  `0x102a0ed0` is `CanStandAt`; the stand test is `CheckStandPosition 0x102e7270` (R1 §1-2) and boxes the
  NPC's `m_Collision` OBB, not a hull row. Latent, fixed: `ElysiumNpcGeometry .cpp:328/336` passed cm where
  Source units were expected. **The seam.** `TraceRetail` takes retail's mask and returns the characters as a
  list; each kernel caller applies its own retail filter, so no collision profile or generator changed.
  `QueryRoute` replaces `RouteLengthTo`, under the navigator's pedestrian multiplier: `true` with `bReachable
  = false` for "no route", `false` only for "no mesh" (found live, `3ecdb573`). `NavRaycast` is the ground
  probe. The mask recipe maps bits to channels; MONSTER `0x2000000` has one reader, game-side
  `StandardFilterRules 0x101d3080` (R2 §2). **The bodies.** Stand test: start +0.1, drop slot 523 (18 base, 36
  Troika, 40 test hull, Ming Xiao 50, tentacle 30, Tzimisce 56; R1 §5), foot box 0.75/0.25 at z = mins.z, pass
  iff `fraction != 1.0` and slot 166; start-solid is a HIT (R1 §1). `CanStandAt`'s `+0x63da` bracket (R1 §3).
  `IsValidCover 0x1028af20`: end 0.01 ABOVE the start, OBB extents, `0x202400b`, `startsolid` only (R2 §3).
  `IsAreaClear 0x102a0fb0`: start == end, `>= 1 && !allsolid && !startsolid`, bracket (R2 §4). `CanFitAtNode
  0x102f1900`: stand test only for type 2 / type 4 `& 0x1d`, then a +0.01 full-row ray, `startsolid` only (R1
  §4). Floor drop 256; wander probe `0x102ed610` through `MoveLimit` (R1 §6); the yaw sweep through
  `MotorMoveTraceSweep`. `ValidateNavGoal 0x10280360`: floor probe `0x102f99d0` (384 down, `0x2400b` then
  `0x202400b`), `EyeOffset`, line to the enemy's eye on `0x2804091` (R2 §6). `RetailCollisionExtents` answers
  for NPCs and the player's `CGameMovement` hull. `FVisible 0x100a6fa0`, four arguments: probe 0/10 eye, 1 OBB
  centre, 2..9 the corners at 0.9 z, Y mirrored into port axes; every NPC is skipped (`m_bNPCTransparent`),
  the player and solid props block; verdict `== 1.0`, then hit == target (clear), then blocker (R2 §1). Props
  by the MONSTER arm: entity props only under MONSTER, static props always (engine `2006aabe..2006aae5`).
  Contact is not start-solid: hits back off to `DIST_EPSILON`. **Fixtures and the spike.** Five tutorial
  fixtures pass on real geometry: sight-only `--S-` idx 0, window `PN--` 0, NPC clip `-N--` 0, embedded hull
  `PNS-` 0, ledge `PNS-` 5. The NavMesh spike SKIPS (the baked level has no navigation system, 0 active
  tiles): reachability, the nav raycast and the closed room are live-only. **Live check** (smoke.md).
  Sightlane, thug eye to `pt1..pt3`, attributed: pt1 clear on both channels; pt2 / pt3 blocked on both by one
  static prop (`junkedcar02`, sight fraction 0.591 / 0.391), 0 of 3 flipped. The player on the ray:
  `blocked(entity #1868 '!player')`; an NPC on it: `visible`. Stand: floor stands; 3 m up, no ground; inside
  the car stands (faithful: start-solid untested, a static prop is no entity, `CanStandOn(null)` accepts; R1
  §1). Cover: floor valid; inside the car refused. Route: reachable, 901.4 cm against 870.8 straight; behind
  the door cuts refused, 1224.3 cm partial when accepted. `sm_hub_1`, 3 min idle: 0 path tests, 450-1028
  traces per 30 s (177-506 ms), 4790 queries in 2362 ms (~0.5 ms each), 0 ensure / assert. Both patrol cops
  failed `0xc` after link 731: row 13's. Gate: Substrate 504 / 0 and Content 23 / 0. **Named divergences.**
  (a) The target's own hull under a MONSTER ray (`RayReaches`, throw LOS) is EXEMPT, an inference:
  `0x10382020` traces `0x600400b` and reads only `fraction == 1.0` (`103821e9`), so it rides on the player
  model's `$contents` (RE-BACKLOG 39). (b) A NavMesh raycast stands for `MoveLimit`'s ground arm:
  `TestGroundMove`, `CheckStep`'s per-step `CanStandOn` and the final delta-z rule do not run (R1 §6). (c)
  `bStartPenetrating` unions startsolid and allsolid; a start-solid trace reads fraction 0 even when it exits.
  (d) Characters are capsules, not cylinder-flagged bboxes. (e) `0x46004003` runs on `ECC_Pawn`, so NPC clips
  block weapon LOS. (f) Nav filter arms (a) `CNavPropertyDatabase`, (c) slot 91, (d) gamerules groups not
  ported. (g) `StandardFilterRules` arms not carried: render mode, solid `0x20`, `blocks_traces` /
  `npc_transparent` keyfields. (h) A stale pedestrian byte can price the wander route. (i) The `FVisible`
  blocker cell is not delivered. (j) The budget is measured, never enforced. **Handed on.** Row 13 (0018/7):
  the jump refusals stalling both hub patrol cops. Row 19 (0018/13): the weapon LOS mask `0x46004003` has no
  exact channel. Row 11 (0019/6): `MoveLimit`'s jump / fly / climb arms and `TestGroundMove`'s stand chain;
  (f), (g), (i); trace cost (~0.5 ms each, one Chaos particle per signature body; measured, not capped); the
  closed-room fixture; the Werewolf fake-hull check (compares the enemy's local box, not its world bounds);
  the slot-523 accessor still named `GetMaxJumpSpeed` (the name comes from the generated `signatures.tsv`);
  `0x4091` unlisted in the recipe (a one-time warning); the Witness ray's `0x2804091`, chosen, not
  recovered. Unrecovered (RE-BACKLOG 39): the player `$contents`, `CTraceFilterNav`'s first gate
  `0x102eb170`, flag `0x1000000`, ProneDialog's filter (chosen: NPCs block).
  Size: M. Effort: Opus / high.

- [x] **7. Traversals: jumps, doors, crosswalks.** Landed 2026-09-30.
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
  a refusal is 5's `0x0e`. **A door NO link crosses is already cut by story 3** — 3 hands over the
  per-door, per-agent crossing table it computes from the patch graph — so what this story adds is
  the cut plus the smart link for a TRAVERSABLE door, together, in one commit. (Owner decision,
  2026-09-20: cutting a linked door before its link exists would make the 8 tutorial doors NPCs use
  and the hub's smoke-shop pair into walls until this story lands, so 3 leaves them uncut.) Story 3
  also hands over its bake-report list of the other NPC-blocking brush entities — 19 `func_brush`
  on the tutorial, 18 on the hub — for the same judgement, since map logic can toggle them.
  Crosswalks: a smart link between
  the two curb places carrying the pair's `Walk` / `DontWalk` state; the wait and the queue
  rule on 5's events.
  Acceptance: the existing jump-flight tests, per agent; an NPC opens `frontgate` and passes,
  a locked door refuses with `0x0e` and is avoided for its timer; a pedestrian waits at the
  hub's curb through a red phase and crosses on green, a second one queues behind it, a
  non-pedestrian does not wait. Pins, patch graphs: jump links tutorial 25 human / 36 rat,
  hub 117 / 103; door brushes crossed tutorial 8 of 36, hub the smoke-shop pair; crosswalk
  nodes hub 6.
  Provides: special traversal to 5. Consumes: 3, 4, 5, 6.
  Oracle: `navigation-jump-links.md` § "Doors and NPC-clip, the retail contract",
  § "The crosswalk wait, walked — `0x102a0bc0` and the inert four-phase clock";
  `conditions-and-states.md` (the crosswalk wait); `schedule-kernel.md` (slot 531).
  **Recovered 2026-09-21, and it simplifies the crosswalk job.** `0x102a0bc0` gates on waypoint
  flag `4` AND goal type 8 before anything else, so a non-pedestrian provably never waits. The
  red/green test is `link+0x64 & (0x10 << (((int)curtime >> 4) & 3))` — a **four-phase clock**
  rotating every 16 s — but its only writer `0x102f97c0` sets or clears the whole nibble `0xf0`
  at once, so every shipped link is always-red or always-green and the rotation is unreachable by
  content. **Model the pair's state as one boolean; that is behaviourally identical to retail
  here.** Implementing the phase rotation would be building an arm no map can observe — record
  the choice either way.
  **Landed 2026-09-30** (wave 0 reads R-J, R-D, R-C; builds B-J, B-D, B-C; reviews V-J, V-D, V-C;
  the oracle in `navigation-jump-links.md` § "The capability gate", § "Doors, landed", § "The
  crosswalk walk", `conditions-and-states.md` § "The pedestrian crosswalk", `schedule-kernel.md`
  § "The door's NPC-open data and the alternate-AI door modes"). **The owner's decisions.** (1)
  Jumps are faithful: no jump link is traversable. `0x102ff960` step 2 ANDs slot 513 with the
  link's hull word, and no shipped NPC holds bit 2: slot 513 has no override,
  `m_afCapability +0x5cec` is SAVE-only, none of the 63 `CapabilitiesAdd` literals carries it, the
  fly toggles `0x1038c170` / `0x103580d0` pass 1 or 4, weapon slot 360 ORs in 0 / `0x2000` /
  `0x40018000`, and `CapabilitiesClear` has one caller (`CNPC_Crow::Spawn`). So `IsJumpLegal` is
  never reached, and link 731 is refused at the capability step. (2) Crosswalks use a path-build
  splice, not a smart link. A `UNavLinkCustomComponent` would halt the body before the kernel set
  `AT_CROSSWALK` and ran `0x102`, which is an event-order change. (3) One boolean per pair for the
  four-phase clock. (4) `copcar`, `ai_node_graph_built` (client-only), `0x102cc900` and the "NPC
  may open it" pre-gate are recorded, not built. (5) **2026-09-30: close on tests.** The live
  crosswalk wait and the live locked-door refusal go to 0002/11 (TRACKER row 36). Every hub
  pedestrian moves through the port's ambient interesting-place executor ("Body owner: Ambient"),
  never through retail's `0xff` / `0x100` chain that builds the pedestrian goal, so no live NPC
  arms `m_flNextCrosswalkUpdateTime`, holds a pedestrian link or reaches the smoke-shop door link
  until that row retires the executor. **Corrections to this story's text.** JUMPS: "a link whose
  usable motion is exactly jump passes `IsJumpLegal`" is true of the predicate but unreachable by
  content. No word-3 link exists (`InitLinks 0x102fb4e0` tries jump only after ground fails). The
  hub cops' retail route is the ground `452 → 151 → 453`. DOORS: a refusal raises `0x0e` only on
  the look-ahead (`0x102f06e0`, and there the lock refusal drops the shortcut silently). A door
  met on the move step (sink `0x1027dc10`) fails `0x0c` after the 4.0 s stale mark. The lock
  test's first arm is `noopenwanted` AND player `+0x1d10 > 0`. The link predicate is `0x102fce80`
  (not stale, expired, or the first re-probe this tick clear), not "open or may open".
  `DoorHitTop` zeroes `+0x640`. The spec's "an NPC may open it" link pre-gate is not retail and was
  not built. `frontgate` is opened by `logic_scene_1`, never by an NPC. CROSSWALKS: "the wait and
  the queue rule on 5's events" was not retail and was not built: the wait test runs from
  `AdvancePath` and the queue arm is the movement sink's slot 1. The queue's "within 32 units" is
  `d² <= 1024.0` 3-D, inclusive. Arm (ii) also makes the blocked walker a waiter. `OnScheduleChange`
  keeps `AT_CROSSWALK`. **"Port today" corrections** (survey): the jump links flew with only hull
  0's word tested. `IsJumpLegal` had no production caller. `RetailDoorToggleState` answered 0
  though the state existed. Nothing outside tests called slot 531. `NavSimplifyPathDoorRefused`
  was always false, so `0x0e` was unreachable. `0x102f96e0` always answered false.
  `ResolvePedestrianPathNode` read stale field names, and nothing called it. `0x102a0b90` was
  ported twice. The pedestrian ids defaulted to 0, not retail's −1. **Named modernizations.** A
  NavMesh route stands for the AIN route. The splice (`NavLayPedestrianLegs`) stands for the
  pedestrian chain `0x102fcd00`: legs [curb A, curb B, …, goal] for consecutive walkable-pair
  curbs within 48 u flat, start node included, `4 | 0x20` and the node id on the curbs and `8` on
  the goal. A route away from any crosswalk lays the goal alone. `QueryRoute` to `StandPos` stands
  for `BuildLocalRoute 0x10304130` in slot 531 arm 7. A collision-world hull sweep with the NPC
  retry under `0x2400b` stands for `CheckStaleRoute`'s probe `0x10304a40`. A paused path re-issues
  its leg on unpause (the pause arm `0x102effab`). Jump links are disabled records. One boolean
  per pair stands for the four-phase clock. **Built.** Jumps: `map_jump_links.py` rows per human
  jump-only pair with every used hull's word and `capabilityUsable = false`; per-direction
  verdicts from `UElysiumNavBakeLibrary::JumpLinkVerdicts` → `IsJumpLegalGeometry`;
  `AElysiumNavJumpLink` with no agents and the smart link off; the gate row
  `baked-jump-links-disabled`. Doors: `FElysiumDoorBase` `+0x644` / `+0x640` as SAVE rows with
  their three writers and the reader `0x1027f550`; `GetNPCOpenData` (100 / 50 / 54 / 24; a sliding
  door answers −1); `IsUseRefused`'s arm 1; HitTop in `0x100f0860`'s order calling
  `OnDoorFullyOpen 0x1027dd10` (slot 532(1), unpause if paused, `m_bShouldMove`, slot 528);
  `IsCloseBlocked` on the auto-close (linked door first, 0.5 s re-think, notice `0x1027dfb0`);
  `debug_hit_by_mode` = "1" (hit-by-door takes slot 532(4)); slot 531 on the real toggle state and
  open data, with the door waypoint (flags `0x30`, handle) spliced before the head (`0x1031a0e0`);
  `NavAdvancePath`'s door arm → alternate-AI modes 1 / 2 / 3; the look-ahead under `0x102f13d0`'s
  gate (0.5 s stamp `0x1049d988`, always the quick pass); the sink with S1's pre-sink arms; the
  stale mark only on the link the body is held at; `AElysiumNavDoorLink`, one per witness link
  (`SupportedAgents` from `crossedByHulls`, endpoints clipped to the largest crossing hull, the
  `0x102fce80` predicate per query, never disabled); every door cut. Crosswalks: pairs staged
  `[a, b, hull-0 word]`; the place set's `CrosswalkRed`, `FindCrosswalkPair` (`0x102f96e0`),
  `SetCrosswalkWalk` (`0x102f97c0`, every pair of the node); `0x102a0bc0` re-fielded with retail's
  gate order; `0x102a0d20`'s strict stamp; `0x102a0b90` once (`SetAtCrosswalk` /
  `AtCrosswalkNode` deleted); the queue arm `0x10298340` as `MovementSinkObstructed`;
  `PAUSE_MOVING 0x102bf770` and `FACE_NEXT_NODE`, so `0x102` runs; save / restore `0x102993c0` /
  `0x10299700` / `0x102998c0` with the constructor's −1. **Pins as baked.** `sp_tutorial_1`: 25
  jump records disabled (hull 0: 25 jump-only, 8 legal each way, 0 usable; hull 19: 17 / 5 / 0);
  8 door links over 8 traversable doors of 36 (3 partial by agent); 60 convexes, all 36 doors cut;
  0 crosswalk pairs. `sm_hub_1`: 117 jump records disabled (hull 0: 117 / 22 / 0; hull 19: 68 / 13
  / 0); 1 door link over the smoke-shop pair (2566 locked + 2567 inert); 63 convexes, all 29 doors
  cut; 8 crosswalk pairs, 6 walkable. **Tests.** `Elysium.Substrate.NpcDoorLink.*` ×8 (OpenData,
  OpenAndPass through production, ObstructionPreSink, LockedMoveStep → `0x0c`, DoorBlockedWindows
  5 / 20 s, LinkPredicate, LookAhead → `0x0e`, LockTestAndCloseBlocked);
  `Elysium.Substrate.NpcCrosswalk.*` ×5 (Signal, ThinkWaitsAtRed through the real think loop
  `0x100 → 0x102 → 0x100`, OneLeg, QueueArm, SaveRestore); `Elysium.Content.Places.HubCrosswalks`;
  the jump-flight synthetic tests kept, and `Elysium.Content.NavJumpLink.Tutorial` pinned to 25
  disabled. **Gate.** Editor build succeeded; pipeline pytest 1,154 passed, 8 skipped;
  `Elysium.Content` 27 / 0 on the re-baked maps; `Elysium.Substrate` 1,705 / 0 (03:20Z; the last
  failure, `NpcCrosswalk.ThinkWaitsAtRed` after the body hold on pause was reverted, was fixed and
  rerun); `verify nav` clean on both maps. **Live check** (`scratch/0018-7/live.md`).
  `sp_tutorial_1` new game, about 3 min: 0 ensure / assert / jump refusals ("cannot begin
  traversal" is gone). `sm_hub_1` about 20 min: 0 ensure / assert / jump refusals. Both patrol cops
  walk their paths with every move a success (`patrol_cop` node 9/12, `patrol_cop_north` node
  10/18, schedule `0x67`); row 07's link-731 stall is gone. The `streetlight_timer` fires Walk /
  DontWalk on the 40 s cycle into the hint and the pair state. `plus_smoke_door` reads AtBottom,
  `0x0 / 0.00`. The `copcar` cops still stand at the car. **Seams left answering "nothing"**, by
  retail name: the `StartBlocked` dispatcher (door slot 177; body ported); the corner-cut pass
  `0x102f0fe0`; the local-route arm `Triangulate 0x103059d0` in the stale re-probe (counted);
  `SetGoal`'s forced simplify (`SimplifyPath(nav, 1)`); `DoorHitTop`'s SF 1 output, slot `0x3e4`
  and SF `0x2000` think; the squad focus (`0x1027dfb0`'s `SetSquadFocus`, slot 531 arm 2). The
  unrecovered reads are RE-BACKLOG 45–48. **Handed on.** 0002/11 (row 36): the live crosswalk wait
  and the live locked-door refusal as acceptance, and a suspected port defect in
  `ThinkSchedulePolicy` (recorded there). Story 3's agent step height and 21-9's judgement row:
  the mesh walks EVERY jump-only pair (tutorial 25/25 human and 36/36 rat; hub 117/117 and
  103/103; complete paths near the straight line), steps that retail's ground walk refused. The
  gate's new `jump-only-walked-hull-<h>` rows report this and never fail on it. 21-9: bridging
  keeps the graph's ground+jump definition (the hub's rat-only 727 / 1242-1244 would be bridging
  under retail reach, 9 → 13). 0018/19's debugger view: rat-only jump pairs get no record actor
  (tutorial 19, hub 35, counted). 21-10: rat link 721's pin is retired (it routes at 150 cm now),
  so the list shrinks to 406 / 1472. Story 3's contents marking: the `copcar` prop obstacle and
  the three crossed `func_brush` windows (`sheriff_shield` link 138, `knob_buff_brush` link 111,
  hub `taxi_clip` link 97). 0002/26's flight body, if it ever flies: `BeginNavigationJump`'s
  launch direction (`SuggestProjectileVelocity_CustomArc` against `CalcJumpLaunchVelocity
  0x102e7060`).
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
  **Landed 2026-09-30 (tick pending the owner's decision on the live check)** (wave 0 reads R1,
  R2; builds A-E; review G; live check H; the oracle in `shape.md` § "The hint list and its four
  searches" and the new § "The claim primitives, the hint LOS check and the idle gate",
  `schedule-kernel.md` § validators). **Retail has, and the port now runs.** The four searches over
  the world's list, cursor and count (`DAT_10925450` / `54` / `58`): `0x102d1af0` (type), `0x102d24b0`
  (anchor), `0x102d2980` / `0x102d2940` (class mask, `2940` being mask 1), `0x102d1760` (the random
  pick), each with retail's walk start, wrap, cursor write, admission order and tie rule. The class
  word `+0x474` derived in `Spawn` (`0x102d0b60`). Claim `0x102d1350`, release `0x102d1420`, owner
  test `0x102d1450` and available-to-me `0x102d1540`, on the hint's own SAVE words. The attack
  validator `0x10296c40`, the hint LOS check `0x102968f0`, the idle gate `0x102b5de0`. The kick-hide
  walk `0x102d0910`, now on the hint as `NpcKicked` and wired from `TASK_KICK_HINT` / `_AT`. Slot 550
  `CoverRadius` (1024 base, 4096 for `CNPC_VPedestrian` and `CNPC_VTzimisce`). `hint_rating` is
  REPLACED by a row of `NPC_Cover_Distance_Scalar` (8, 3.5, 3, 2.5, 2, 1.5, 1), not clamped: the
  authored 3 becomes 2.5. **Corrected on the way.** This story's "no registry under them" line was
  stale: the entity, list, node binding, inputs and slot 566 were already stood (0018/2, 0018/4,
  0019/8); what was missing was the query and claim surface. The two mask-search callers
  (`0x102b6b50`, `0x102b7110`) passed a TYPE search with `8`, the flags byte, as the type; they now
  call `FindHintByClassMask(8, 0x10, ...)` and `(8, SearchType, CoverRadius())`. The port's comments
  on `0x102968f0` (the ray's direction) and `0x102b5de0` (a 3.0 s elapsed-occlusion test, not a
  distance; arm 1 fails only on a world hit) were backwards; both bodies are ported from the listing.
  The NPC-side `bOwnsHint` / `HintReusableAt` stand-ins are deleted, and `ClearHintNode 0x10295ab0`
  releases only when `0x102d1450` says I own it. `UpdateOnRemove` releases with delay 0 and no owner
  gate, and the base `TASK_LOCK_HINTNODE` arm calls `0x102d1350` directly. The duplicate
  `FUN_10296c40` / `AttackHintRejectReason` (four defects) is deleted. The cover validators
  `0x10295ed0` / `0x102961a0` take their facing from `0x102d12e0` (a node-bound hint's NODE yaw) in
  Source axes, normalise in 3-D and dot X/Y, keep each compare's NaN direction and call the real
  `HintLosCheck`; `0x102961a0` casts its own ray at mask `0x46804099` (it traced at mask 0, always
  clear). `0x102d1fe0` has no caller (dead). **Named divergences and crash guards.** A null anchor in
  `0x102d24b0` (retail faults) answers none with the cursor untouched. A non-hint index in the claim
  primitives (retail dereferences NULL) is guarded. `DefaultEyeOffsetCm()` stands for
  `m_vecViewOffset`. `ClassMask` is re-derived on restore (not saved; whether retail's restore
  re-runs `Spawn` is unread, RE-BACKLOG 49; if it does not, retail's mask search finds nothing after
  a load). The idle gate's monsters-only mask falls back to `ECC_Pawn`, so its arm 1 can meet an
  NPC-solid brush retail cannot. The LOS filter's `ShouldCollide` and game-rules group arms are not
  asked (every combat character is refused, as retail). The weapon `+0x8c0` max range is still the
  1024 stand-in. The kernel draws the random pick on `EElysiumRngStream::NpcSchedule`. Save schema
  `NpcHintClaimOnHint = 40`, `MinSupported` moved to it. **Gate.** Editor build green;
  `Elysium.Substrate` 1,722 / 0 (baseline 1,705: +13 `HintSearch`, +3 `NpcKernelHints`, +1
  `NpcKernelBaseHelpers.CoverNodeFacing`); `Elysium.Content` 33 / 0 (baseline 27; +6
  `Elysium.Content.Hints.*`, including `TutorialTacticalSearch` and `TutorialClaimAndCooldown` over
  the baked tutorial's 49 hints). The ledger is regenerated and `uv run elysium research kernel
  --check` is 7 / 7 (not re-run for this record): override census 1,048 to 1,050 (`CoverRadius` on
  Pedestrian and Tzimisce), `kernel_shape --unported` 371 to 369, `unported.tsv` 393 to 393,
  `seam-list.md` 390 to 390 (the searches and claim primitives had no ledger row). Census
  (`story8/census.md`): tutorial 49 hints (12 mask-1 cover, 37 patrol), hub 274 (234 mask-1, 34
  patrol, 6 crosswalk); all 108 maps 3,156 hints; 216 `EnableHint` / `DisableHint` outputs and one
  Python site (`temple.py`). Only class mask 1 has candidates on either witness, so the cower, kick
  and shoot-at searches must miss there. **Live check (`story8/live-check.md`, three attempts).**
  OBSERVED: hint row 465 reads `hint_rating` 2.5 (the rulebook replacement), owner none, next use 0,
  class mask 1; `DisableHint` sets `m_iDisabled` and hides, a hidden hint swallows `EnableHint`,
  `ScriptUnhide` alone unhides and clears the disabled word (slot 78 `0x102d0890`), `StartHidden 1`
  alone does not disable; Python `FindEntityByName(...).DisableHint()` works; the cower, melee and
  shoot-at searches found nothing, as the census predicts; 0 ensure / assert over `sm_hub_1` idle
  (about 3 min) and the whole session (136.6k log lines). **NOT OBSERVED, which is why the box stays
  open:** an NPC's combat-time tactical search claiming a cover hint and releasing it. Each of three
  attempts was stopped by something outside this story. (1) `thug_3` (the census witness,
  `StartHidden 1`) sits in `FALL_TO_GROUND 0x3e` after `ScriptUnhide` with `on_ground false` and no
  `Body owner`, sees and hates the player but never selects combat (a hidden-NPC lifecycle / motor
  defect). (2) The hub's `patrol_cop_north` is a baton cop: it takes the melee branch (mask 8) and
  never runs the mask-1 search, and the player fell through the world when placed off-mesh. (3)
  `sentry3` / `Hunter1` (tutorial, ranged) read "SEEN, in cone" at 6-7 m with `D_HT 5` yet
  `Conditions (none)` and no combat in 85 s (an authored-friendly NPC flipped by `SetRelationship`
  never enters the sighted list; `Hunter1`'s `hint_groups` 6 admits no group-1 row). The claim and
  release chain is pinned instead by the 13 `HintSearch` unit tests and the two tutorial content
  tests (row 465 first from the head, 464 after the cursor moves, the 5.0 s cooldown's strict `<`).
  **Handed on.** Seams NOT taken: the weapon `+0x8c0` max range (family Motor's), and the player's
  `BodyTarget` stub (the idle gate traces to the world origin when the enemy is the player).
  `0x102d0910`'s output activator and caller order, `m_iszUserData`'s reader, `_DAT_1044e674`. Story
  9: the cooldown `+1.0` claim write and the selectors over these searches. 0002/12b (row 32): the
  kick chooser over `NpcKicked` (no kick hint exists on either witness). RE-BACKLOG 49: whether the
  engine's restore path re-runs `CAI_Hint::Spawn`. Also found, outside this story: the hidden-NPC
  lifecycle (attempt 1), the senses `Sighted()` gate for a `SetRelationship`-flipped NPC (attempt
  3), an open dialogue blinding perception (`Closest player unseen` until `EndDialog`), a `map_load`
  of the same map keeping the previous session's entity state, and `TakeDamage 1` raising `health`
  without writing a last-damage record.

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
  Oracle: `programs.md` § "Interesting places: the selector, the programs, the wait",
  § "Interesting-place eligibility" (both corrected 2026-09-21 — they are in `programs.md`, not
  `schedule-kernel.md`); the `0x102daac0` visitor walk is `lifecycle.md`;
  `shape.md` § "The interesting-place wait and its loop".
  Size: M. Effort: Opus / high.

- [ ] **11. Patrol paths and the patrol-point interest record.**
  Retail: `CAI_PatrolPath` and `info_node_patrol_point`; the interest record at `node->+0xa0`
  (`+0x468` the name of a `CAI_InterestingPlace`, `+0x46c` a 0–99 chance) resolved by
  `0x1029f730` and `0x1029f780`.
  Gap: nothing — no path record, no node record.
  A patrol point's position is taken at the **standing** hull, not the pathing one: the three
  patrol arms (`0x102a3a86`, `0x102aa6b6`, `0x102aa8df`) are among the seven `GetPosition` sites
  that pass `m_eHull` while routing passes `+0x156c` (3's § "The two hull words"). For every
  species but the Sheriff, Hengeyokai and Ming Xiao the two words agree, so this matters only for
  those three; the place set (4) carries a point's per-hull offsets so either can be asked for.
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
  **The corpus pass is done (2026-09-21, `programs.md` § "The patrol-point interest record and
  the path object"), so this story starts from an answer, not a read.** The roll is one
  `RandomInt(0, 99)` at `0x1029f650` compared **strictly `<`** against `+0x46c`, made at path
  install / `NEXT_PATROL_POINT` / schedule selection, not per read. `0x1029f730` answers nothing
  while `+0x65a0` is zero; `0x1029f780` caches at `+0x6300`, falls back to the empty-string global
  `DAT_106b8540` on a null name, and applies **no classname test**. `CAI_PatrolPath` is a pooled
  record (32 slots of `0x114` at `DAT_10934158`) with a SAVE-only, **externally nameless** datamap
  `0x106117c0`, and it **is** reached by shipped content (`sp_tutorial_1` row 1627 wires
  `SetupPatrolType` + `FollowPatrolPath` on `sentry2`), so it is built, not skipped. The loop /
  ping-pong table is `0x1049df20` and belongs to the path object alone — a type-10000 hint carries
  no path type. Hint-list order is **head insertion** (`0x102d2e30`), so the later hint wins a
  duplicate `Group`.
  Three data facts change the tests. `ip_percent` is authored on all 582 rows and is **`100` on
  556 of them**, which under the strict `<` always takes the interest — the fixture must not
  assume a coin flip. `target_angle_range`, `target_dist_min`, `target_dist_max` and `hint_rating`
  are **inert on a type-10000 hint** (`0x102d0b60` has no branch reading them), so the record need
  not carry them as behaviour. And **7 of the 60 `target_name` rows resolve to nothing**, five of
  them on `sp_tutorial_1` (`sentry1_ip_cigarette`, `sentry2_ip_whistle`, `monk1_ip_pray`,
  `monk1_ip_idle`, `cellar_ip_whistle`) — the witness map's own dangles are the unresolved-name
  fixture. The absent-key default of `+0x46c` is **0** (closed 2026-09-21, `programs.md`: the
  entity allocator is a `calloc` and no constructor writes the word), so an unauthored point never
  takes its interest; no shipped row exercises it.
  Size: S–M. Effort: Fable / medium.

- [ ] **12. The flying mover.**
  Retail: no link in any shipped graph flies (0 of 29,523), so every flight is a straight
  move at its goal. `CNPC_Crow` flies to hints (`TASK_CROW_FLY_TO_HINT`; search type 700,
  flags 3, radius 5000); `CNPC_VManBat` runs six `TASK_MANBAT_FLY_TO_HINT` schedules (hint
  type 20000). The navigator's type is fly while it lasts.
  Port today: the body has no flying mode; `EElysiumNpcNavType` is only ever ground or jump.
  The two flyers' hulls are recovered and sit in 3's generated class table unused until this story
  spawns them — `CNPC_VManBat` 20 `MANBAT_HULL` (ctor `0x10389d56`), `CNPC_VGargoyle` 14
  `GARGOYLE_HULL` (ctor `0x10377a93`), each the same in both hull words. `GARGOYLE_HULL` carries
  links on one map only, so its agent can build a mesh nothing paths on; a straight flight does not
  need one.
  Job: a flying gait on the body — CharacterMovement's flying mode steering straight at the
  goal with a swept look-ahead on 3's NPC-solid bodies — driven through 5's navigator, so
  tolerance, arrival and `0x0c` are the same contract as on the ground; the nav type reported
  to the schedule host.
  Acceptance: a crow reaches a hint across open air and refuses one behind a wall.
  Consumes: 5, 6, 8.
  **The three reads are done (2026-09-21, `navigation-jump-links.md` § "The flying movers,
  walked"), and two of this story's premises did not survive.** *(a)* **`CNPC_VGargoyle` does not
  fly** — it inherits the base movement override `0x1027da90` and has no flight task or type-2
  transition (`0x10377c70`, `0x103790d0`, `0x103793e0`); only Crow (`0x10357ba0`) and ManBat
  (`0x1038b120`) do. Its hull 14 stays unused, and the "a mesh nothing paths on" worry is moot.
  *(b)* **`TASK_MANBAT_FLY_TO_HINT` is named by three schedules, not six** —
  `SCHED_MANBAT_MISSILE_ATTACK`, `SCHED_MANBAT_FLY_END`, `SCHED_MANBAT_FLY_CONTINUE`
  (`0x10642080`, `0x106423b8`, `0x106424b0`); two independent passes agree.
  Speeds: the crow is a flat **170** u/s (`0x10454028`), no ramp, and its arrival latch
  `+0x5f54` uses that same 170 as its radius; the ManBat ramps to **700** below delta-Z `-30`
  (`0x10462868`) and **500** above, takeoff **200**, `FLY_RANDOM` **500**, arrival at
  `0.2 x |velocity|`. Blocked: the fly arm `0x102e6090` is a **3-D hull sweep** with no step or
  floor contract, classifying `-3` NPC / `-1` entity / `-2` world (`0x102e2d70`) into
  `OnNavFailed 0x102eeae0` -> `0x0c` — **but neither flyer consumes that**: the crow steers
  (`0x10357e50`) and the ManBat returns `(0,0,1)` and flaps (`0x1038bec0`), so the port's flying
  gait answers `COND_FLYING_WALL_HIT 0x36` / `COND_FLYING_NPC_HIT 0x37`, not a route failure.
  Landing: `TASK_MANBAT_LAND 0x150` under `SCHED_MANBAT_FLY_END`; **the crow has no land task**
  and keeps flying at its hint (`SelectSchedule 0x10358ce0` answers `SCHED_CROW_IDLE_FLY` while
  navigator type is 2). Neither reads hint yaw on arrival. Flight is navigator type 2 + flag
  `0x400` over entity move type 4, never an entity-movetype change.
  **This story now has a concrete work list, and the gate hands it over** (2026-09-21, 21-8's
  first bake of `la_ventruetower_3` -- the corpus's only hull-20 map and its only four-agent map).
  Every one of that map's 70 navigation findings was hull 20 while hulls 0, 7 and 21 were clean on
  the same geometry: **7 ground links, 12 jump starts, 19 jump ends and 32 bridging links**. They
  are not mesh defects. The nodes behind them are reached by nothing that walks -- nodes 46 and 48
  carry sixteen hull-20 links and ZERO links for every other hull, node 47 carries twelve against
  a single hull-0 and hull-7 link each whose move type is `2`, a jump -- and the Manbat's own mesh
  answers at no height within 600 cm of any of the three. `MANBAT_HULL`'s 160-unit height is the
  flight envelope, not a body walking under a ceiling.
  So `validation/nav_acceptance.FLYING_HULLS` routes hull 20's ground-link, jump-projection and
  bridging verdicts through `flight_row`: every finding is reported under `flightClaims` and none
  is failed, because they are flight this port has not built rather than floor it failed to
  rasterise. The map reads `clean [41 step-height outlier(s) excused, 70 flight claim(s)
  reported]`. **That list is this story's acceptance input**: when the flying gait lands, each
  claim is either answered by flight or re-judged, and `FLYING_HULLS` is the one place to revisit.
  The mesh is still cut for hull 20 -- `TASK_MANBAT_FIND_LANDNODE` and `TASK_MANBAT_FALL_TO_GROUND`
  say the bat touches ground and needs somewhere to land.

  **The open question 21-8 could not close.** This story's own first line says no link in any
  shipped graph flies, and that survives a full sweep: `fields[1+h]` is only ever 0, 1 or 2 across
  all 29,382 links, never the fly bit. Yet hull 20's links on this map are move type `1`, GROUND.
  How a ground link squares with a flyer that never traverses links -- whether `FIND_FLYNODE` and
  `FIND_LANDNODE` read the node set directly and ignore link move types, or whether the ManBat
  does ground-walk between landings -- is unread. The node record's own type word is still
  typed-unidentified (`seam_map_nav_graph.md` § Node stream), so nothing has been read as
  `NODE_AIR`; what is measured is only that the single hull reaching those nodes is one that
  flies. Settle it here before building the gait.
  Recovery: `seam_map_nav_graph.md` § "Flying hulls".

  Oracle: `navigation-jump-links.md` § "The flying movers, walked".
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
  **Corrected and walked 2026-09-21 (`senses.md` § "`CommitBestSound 0x102b4090` — the priority
  ladder and the ninth record"):** the nine records are **seven raw plus two derived**, not nine
  copies. `0x102b39e0` writes one raw snapshot per hearing condition; `CommitBestSound 0x102b4090`
  is a **strict first-match ladder** — `HEAR_COMBAT 0x6d`, `HEAR_BULLET_IMPACT 0x70`,
  `HEAR_FLINCH 0x72`, `HEAR_PLAYER 0x6f`, `HEAR_DANGER 0x6a`, `HEAR_PHYSICS_DANGER 0x71`,
  `HEAR_WORLD 0x6e` — whose winner becomes `BestSound +0x60b0`, then `InvestigateSound +0x60dc`
  is copied from it in the same call. **`HEAR_DANGER` is fifth, not first**, and `HEAR_THUMPER` /
  `HEAR_BUGBAIT` have no slot at all. With no condition set neither derived record is rewritten,
  so a stale best sound outlives the raw slot that made it — a retail behaviour the port must
  keep, not a bug to fix. This ladder, not "nine copies", is what 10a's sweep re-reads from the
  shared list.
  Oracle: `senses.md` § "Hearing, walked" and § "The shared list itself" (the story's
  parenthetical "the sound world" is not a heading in that file).
  **Read 2026-09-21 (`conditions-and-states.md` § "The port's NPC-core guesses, settled", item
  6):** the `HEAR_*` condition is chosen by the sound's TYPE BIT, which the caller of `InsertSound`
  supplies (`0x1` combat, `0x2` world, `0x4` player, `0x8` danger, `0x10` bullet impact, `0x400`
  physics danger, `0x800` flinch); a `sound_volume_table.txt` row contributes only the radius
  level and the occlusion flag. So a producer writing into the list must carry a type, and the
  port's match-the-category-by-name rule goes.
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
  **0019/1 (2026-09-21) judged the standoff half `dead`:** `ai_goal_standoff` is authored by no
  map and created by no script (`population.md`), and slot 455 answers 0 on every NPC class, so
  no NPC ever hosts `CAI_StandoffBehavior` — the goal's two inputs, `UpdateOnRemove` and
  `TranslateActivity` are on `docs/vtmb/npc-kernel/delete-list.md`. What remains here is the
  coordinator; slot 608's bind-by-name stays `rule`.
  Size: S–M. Effort: Opus / medium.

- [ ] **16. Makers and templates.**
  Retail: `CNPCMaker`, `CNPCMaker_Fleshpile`, `CNPCMaker_Zombie`; `npctemplate*.txt` (150
  declarations in 36 files); the child inherits `NPCTargetname`, `NPCSquadname`, the forwarded
  outputs; count, frequency and `Flag_*` lifecycle.
  Gap: `ElysiumNpcMaker` exists and spawns; its keyfields are hand-typed.
  Job: the maker as a baked actor the runtime adopts, keyfields from the datamap seam, the
  inheritance and lifecycle rules verified against the hub's 48 requests and the tutorial's 14.
  Consumes: 2, 0019 story 2. Oracle: `population.md` § "How a map defines an NPC",
  § "Templates and inheritance".
  **Measured 2026-09-21, and the "150 in 36 files" pin needs its source named**: the patch's LOOSE
  `Unofficial_Patch/vdata/system/` holds **29 `npctemplate*.txt` files with 130 `TemplateName`
  declarations, all distinct**. `Vampire/vdata/` does not exist as a directory — retail's copies
  are inside a VPK — so 36/150 can only be the patch-first RESOLVED set (loose patch files
  shadowing the VPK, plus the VPK files the patch does not shadow). The lane must state which set
  it loads and reproduce its own count (**settled 2026-09-21, `population.md` § "Templates and
  inheritance"**: 36 / 150 IS the patch-first resolved set — 29 loose patch files with 130
  declarations plus the 7 `pack101.vpk` files the patch does not shadow with 20; the loader is a
  wildcard find over the MOUNTED view, first search path wins per file, first declaration wins
  per name; the deployed corpus already holds exactly that set, and no shipped maker names one of
  the 19 VPK-only templates); a bake that reads only the loose tree is 20 templates
  short. Size: S–M. Effort: Sonnet / medium.

- [ ] **17. Relationship defaults and the player-law bus.**
  Retail: `AddClassRelationship`; `SetRelationship` (native `D_*` table),
  `SetDisposition` (stance) and `reaction.txt` (RPG score) kept separate; the player's law
  levels, offenders and the closest-NPC cache on `CBasePlayer` (`0x101828b0`, `0x10182a90`).
  **Corrected 2026-09-21: there is no relationship table in vdata, and `Rules.txt` is not its
  source.** The shipped `vdata/system/rules.txt` has no relationship, disposition or `D_*` block
  at all — its blocks are frenzy, damage, heal, occult, discipline, ladder, knockback, jumping,
  animal friendship, physics hand, zombie grapple, npc combat, Ming Xiao, npc follower and melee
  reactions. Every one of the 19 `AddClassRelationship` sites in `vampire.dll` is a **hard-coded
  C++ constant**: `CNPC_Crow::Spawn 0x10357440` (`0xe, 4, 0`), `CNPC_VVampire::Spawn 0x103c4ef0`
  (`1, 1, 0`), `CNPC_VPlayerController::Spawn 0x103a4510` (`1, 3, 0`),
  `CNPC_VGhoulCroucher::NPCInit 0x1037b290`, `CNPC_VManBat::Spawn 0x1038b030`,
  `CNPC_VWerewolf::NPCInit 0x103caef0`, `CNPC_VZombie::NPCInit 0x103defc0` and the
  transformation/trigger arms (`CNPC_VSabbatLeader 0x103aa3b0`, `CNPC_VHengeyokai 0x10383170`,
  `CNPC_VMingXiao 0x1039a700`, `CNPC_VSheriffMan 0x103b1470`, `CNPC_VAndreiBlood 0x1035dd00` /
  `0x1035e980`), all `(1, 1, 10)` unless noted. The only data-driven path is
  `InputSetRelationship 0x10273790`, which is the map/script surface, not a default table.
  Gap: `ElysiumRelationships`, `ElysiumLaw` and the law event bus exist.
  Job: the class-relationship defaults as a **generated table from those 16 code sites**, one row
  per class with its address, applied where retail applies it (`Spawn`, `NPCInit`, or the named
  transformation/input arm — they are not all spawn-time, so the table cannot simply be a
  constructor sweep); `InputSetRelationship` on top of it. The bus as the world object NPCs
  read (levels, offender, location, timers). The acts stay in 0005 / 0006, the witnessing in
  0002.
  Oracle: `social.md`, `population.md` § "Player-law observation transaction". Size: S.
  **The bus, read 2026-09-21 (`population.md` § "The law transaction's guesses, settled").** It is
  THREE stores, and the port's two invented lifetimes go: the player's act channels (level,
  expiry, COUNT — the count is what an NPC's processed counter `+0x636c` / `+0x6370` is compared
  with; duration = the explicit one, else `max(retained level, pl_min_act_timer)` with
  `pl_min_act_timer` **5**); the 64-record global `CAI_CsActList` (published by NPC death and the
  supernatural action task, NOT by the player's acts; swept by expiry only, **no cursor** — a live
  record is re-observed every gather); and the player's scare queue (`scarednpc_time` **180 s**,
  over `scarednpc_count` **3** the OLDEST is evicted). The five `pl_*` thresholds are normalised
  once, at `Spawn`: anything `< 1` — unauthored, `-1`, or `0` — becomes 6. `nosferatu_tolerrant`
  has no reader in retail.
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
  **Handed over by 3:** both witness levels COOK with no external export access. 3 verified
  save, reload and a clean boot that adopts the baked meshes; the CLI has no cook lane, and a
  Game-target compile was the nearest proxy available then. That compile (2026-09-20) proved
  3's own editor guard and found the Game target ALREADY broken by four files 3 never touched,
  each using editor-only API unguarded: `Tests/ElysiumContentTests.cpp` (15 errors),
  `Tests/ElysiumSurfaceKnobTests.cpp` (11), `Tests/ElysiumTerminalProjectionTests.cpp` (1) and
  `Visual/ElysiumBipedAnimInstance.cpp` (1). A cook cannot pass until they are guarded; the log
  is `$ELYSIUM_WORK_ROOT/logs/game-target-compile.log`. "No external export access" was 21-6's
  to provide and it landed 2026-09-21: the game takes no content argument and opens no file
  outside the project.
  Consumes: everything above, and 21-6. Size: M. Effort: Sonnet / medium, then played.

- [ ] **21. Retiring the legacy map transport.** Added 2026-09-20 by owner decision: V2 is the
  pipeline, and a map that cannot load on it is a failure rather than a compatibility case. This
  entry is the group's frame; the work is 21-1 … 21-8 below, run in order, each landing green.

  **Owner decisions, 2026-09-20.** (1) Everything the game opens is baked; what cannot be baked
  is deployed into `Content/ElysiumCorpus/`; nothing is read from outside the project. (2) The
  legacy BSP decoder, its two-pass export, `CorpusBake` and `/ElysiumBaked/Shared` go with the
  transport. (3) Entities, collision and environment all fold into `bake map` — one command
  yields a loadable level (3's handover: the owner chose "inside bake map" and 3 built
  `import map-collision` instead). (4) The map set is six: `sp_tutorial_1`, `sm_hub_1`,
  `sp_soc_3`, `sm_pawnshop_1`, `sp_theatre`, and `sp_genesisdevice_1` — the blank
  character-creation room, never BAKED from the legacy lane (it had been legacy-EXPORTED at some
  point; 21-4 found the directory and deleted it), so the CLEAN-ROOM witness that the lane stands
  alone. It proves plumbing only; decals, weather, water and ropes are proven on the other five,
  re-baked with their legacy directories deleted. `sm_pier_1` leaves the set for 21-8. (5) The
  producer's byte-compatibility debt is paid here (21-7). This group supersedes the contracts'
  R8.1 / R9.2 precondition "once all 108 maps are listed" (`seam_map_map.md:714,935`).

  **What the audit corrected in this story's first text** (evidence is the audit at `a7f448ed`;
  a line number below is where to start reading, not a pin). The selectors alone are 160
  occurrences in 40 files, not "~80 across 36", and gate arms the text never named —
  environment, the run-time sky assembly, the light rig, the rain emitters, the travel gate.
  `Bake` cannot be deleted "beside `MapBakeV2`": `MapBakeV2(Bake)` (`bake_map_v2.py:321`) and
  `CorpusBake(Bake)` (`bake_map.py:2633`) subclass it; what goes is its legacy source path. The
  sidecar-diff lane is run by no `elysium verify` command — only by hand — and already
  short-circuits to `self_comparison` on every map, so the whole module is legacy-only. The
  "legacy lump reader" that refuses three maps is `UE_map_sidecars.entity_lump_text` (`:431-468`),
  INSIDE the V2 producer, and the maps are `la_ventruetower_2`, `la_ventruetower_3` and
  `sp_giovanni_2b` (`test_map_ai_infra.py:144`): deleting the transport does not unblock them,
  21-7 does. `.hulls` / `.dispcol` / `.props` have no C++ reader already, while
  `FElysiumEntityDefs::Parse` stays reached by tests. And only the two witnesses were re-baked
  after 3: the other listed maps carry version-1 payloads (2026-09-01 … 09-07) and cannot load.
  Confirmed as 3 left them: `LoadHulls`, `RestrictNavigationToUsableAgents`,
  `LEGACY_BRUSH_SIGNATURE` and `PAYLOAD_CONTENTS_MASK` are gone; `EnsureRuntimeNavigation` is
  adopt-only; an unlisted map hard-fails (`ElysiumMapCollision.cpp:231`).

  **Where each thing lands.**

  | Today | Reader | Lands | Story |
  |---|---|---|---|
  | `<map>.ents` | fallback arm, `elysium.ents`, two test fixtures | `DA_<map>_Entities` (exists) | 21-1, 21-3 |
  | `.env` `.sky` `.spawn` | fallback arm | `DA_<map>_Environment` (exists) | 21-1 |
  | `.hulls` `.dispcol` | none at run time | `DA_<map>_Collision`, the level actor, the meshes (exist) | 21-1 |
  | `.lights` | `UElysiumLightRig::Adopt` | baked light actors, `AdoptBaked` (exist) | 21-1 |
  | `.ropes` | `ElysiumMapVisuals.cpp:589`, ungated | baked rope actors in the `.umap` (new) | 21-3 |
  | `.ready`, `.obj`, the per-map directory | travel gate, `ExportedMaps()` | nothing: the baked level and its three assets are the proof | 21-1, 21-3 |
  | `.decals` | `bake_map_v2.py:380` | staged `decals[]` rows → the baked `ADecalActor`s (exist) | 21-4 |
  | `.weather.json`, `weather/rain_height.png` | `bake_map_v2.py:381` | staged rows → `/ElysiumBaked/<map>/Weather` (exists) | 21-4 |
  | `.props` | weather cover, `bake_verify`, the level recipe | staged `placements` (landed) | 21-4 |
  | `<map>.materials.json` | `load_corpus`, `bake_verify`, the material report | nothing: one map in the tree ever had one, and a PAKFILE-only material is a unit in its own right (landed) | 21-4 |
  | `<map>.mtl` (the verify lane's half) | the hub's wetness check | the staged `materials` table joined to the corpus on `provenance` (landed) | 21-4 |
  | the producer's eight sidecars | the three asset stages, the level recipe | offline scratch `exports_v2/_sidecars/<map>/`, never opened by the game | 21-4 |
  | `.obj` `.mtl` `.blend` `_sky.obj` `brushes/*` `tex/cube/*` `.water` `.particles.json` | the legacy bake only | deleted | 21-5 |
  | `/ElysiumBaked/Shared/{Textures,Materials,Meshes}`, `export bundle corpus` | `CorpusBake`, the legacy maps | deleted; V2's homes are `/ElysiumBaked/{Textures,Materials,Models/_Corpus}` | 21-5 |
  | `/ElysiumBaked/Shared/Error/M_ElysiumError` | both bakes (`bake_lib.py:557`) | `/Game/ElysiumGenerated/Materials` | 21-5 |
  | `scripts/` `cfg/` `signs/` `ui/strings.json` | the Python VM, ScriptFS, the command bus, signs, UI strings | `Content/ElysiumCorpus/{scripts,cfg,vdata/signs,ui}` from the `exports_v2` units | 21-6 |
  | `_cast` `_compose` `_greenroom` `_move` `_profile` `_shots` `_lights` , the wire dump | debug WRITES under `Root()` | `Saved/Elysium/<kind>/` | 21-6 |

- [x] **21-1. The selectors and the runtime's legacy arms.**
  Retail: none for the transport. For job 5: retail loads a map whose graph is empty, and a
  route asked for there fails.
  Port today: `UElysiumMapTransportSettings` (`MapsOnNewTransport`, `MapsOnV2Models`,
  `Config/DefaultElysium.ini:57-69`) and its Python twin `map_transport.py` choose an arm at
  seven C++ sites — `ElysiumMapCollision.cpp:231`, `ElysiumMapEntities.cpp:105`,
  `ElysiumMapEnvironment.cpp:69`, `ElysiumMapVisuals.cpp:439,686`, `ElysiumContentPaths.h:459`,
  `ElysiumMapActorWeather.cpp:157`, `ElysiumMapSubsystem.cpp:319,355` — and in the pipeline at
  `bake_map.py:2780`, `export_manager.py:1084-1093`, `unreal.py:847` and thirty lines of
  `bake_verify.py`. Entities and environment load their asset `LOAD_NoWarn | LOAD_Quiet` and
  fall through to the sidecar without a word (`ElysiumMapEntities.cpp:109`,
  `ElysiumMapEnvironment.cpp:71`).
  Job, in order:
  1. The settings class, its ini section, `ElysiumMapTransport::IsMapOn*` and `map_transport.py`
     go; `bake map` always builds `MapBakeV2`. (Their comments are already wrong: both say
     `sp_theatre` is off `MapsOnV2Models`, and the ini lists it.)
  2. Each site keeps its V2 arm alone. A missing or unreadable `DA_<map>_Entities` or
     `DA_<map>_Environment` FAILS the load with a named error, as collision already does;
     `EElysiumMapEnvironmentSource::Sidecar` goes. The light rig adopts baked actors only, and
     `UElysiumLightRig::Adopt` goes with its synthetic test (`ElysiumWorldEffectsTests.cpp:505-522`;
     R5.6 re-homed its assertions, `seam_map_map_lighting.md:682`). The run-time sky assembly
     (`ElysiumMapVisuals.cpp:699-~790`) and the per-map Niagara rain path with
     `BakedParticleSystem` (`ElysiumMapActorWeather.cpp:183-217`) go. The travel gate loses its
     `.obj` arm; `.ready` stands until 21-3.
  3. Dead code: `BakedMeshesFor` / `BakedPropMesh` / `BakedItemMesh` / `BakedPropSkins` and
     `BakedShared*` (no non-test caller); the accessors nothing calls — `MapHulls`, `MapDispCol`,
     `MapProps`, `MapDecals`, `MapSkyObj`, `MapTexDir`, the `Shared*` five; and what 3 left
     unreachable — `UElysiumNavBakeLibrary::SetMapNavAgents`, `bake_navmesh.py` (its only caller,
     imported by nothing), `UElysiumMapCollisionPayload::LegacyWorldSignature`,
     `UElysiumMapCollision::GetWorldBounds` / `RefreshNavigationData`.
  4. The verify lane: the eight "not on V2, skip" guards of `bake_verify.py` (`:403, 501, 603,
     706, 851, 890, 1180, 1672`) become unconditional; the legacy material block `:1866-2132`
     and `_material_slot` (`:105-128`) go; `verify_brush_cull`'s asset check (`:1446`) holds for
     every map. `validation/map_sidecar_diff.py` goes whole, with `test_map_sidecar_diff.py` and
     the six `classify_hulls` cases of `test_map_hulls_contents.py:48-104` (`:109-176` pin the
     live `.hulls` row and stay).
  5. ~~**The no-graph rule.**~~ **Dropped, 2026-09-20, owner decision, after reading the install.**
     The rule's premise was wrong. Every one of the 108 patch `.ain` files names at least one hull.
     ELEVEN declare `NumNodes: 0`, and all eleven still carry hull bits: `sp_genesisdevice_1` and
     `sm_smoke_1` are `UsedHullBits: 1` (human), `hw_chateau_1` is `16385`, and the other eight are
     `ch_cloud_1`, `la_malkavian_5`, `sm_oceanhouse_1`, `sm_shreknet_1`, `sm_tattoo`, `sp_epilogue`,
     `sp_masquerade_1`, `sp_ninesintro`. **No map in the corpus has a zero agent word**, so the
     EMPTY word, the `UPROPERTY` on the world-collision actor and the tightened runtime gate all
     had no case to answer — and `ElysiumMapActorLifecycle.cpp:805-809` assigns that word's journey
     to the runtime to story 4 anyway. `sp_genesisdevice_1`'s only real blocker is that its
     nav-graph unit was never exported; `nav_graph_glb.py` already writes a scene-less unit for an
     empty graph and `map_nav_acceptance.project` runs clean on zero nodes. Exporting it is one
     command, and it belongs to 21-2 with the rest of that map's first bake. What this story kept
     is the one-line correctness fix behind it: `import_map_collision.used_hull_bits` tested
     `if bits else None`, conflating a staged 0 with "the lane never ran" and reporting the wrong
     remedy; it is a presence test now.
  6. Tests: `ElysiumMapTransportSettingsTests.cpp` and `ElysiumModelCorpusRootTests.cpp` go;
     `ElysiumMapExportGateTests.cpp` is rewritten to the one arm left; the pipeline pins of the
     selector go or take the V2 branch (`test_map_geometry.py:273`,
     `test_bake_orchestration.py:522`, `test_bake_verify_water.py:239-255`,
     `test_bake_map_sprites.py:240`, `test_bake_verify_lanes.py:127`). Comments still calling
     `.hulls` the live collider are corrected (`ElysiumMapActor.h:191`,
     `ElysiumMapActorLifecycle.cpp:347`, `ElysiumMapSubsystem.cpp:346`), as are the seven
     "`MapsOnV2Models` maps only" notes of `ElysiumBakedTags.h`.
  Acceptance: `uv run elysium build`, `uv run pytest` and `uv run elysium test Elysium.Content`
  green; both witnesses boot Active adopting their meshes as 3 left them; no `MapsOn`,
  `IsMapOn` or `is_map_on` outside `docs/`. The other listed maps fail to load with the named
  error — they are stale, and 21-2 re-bakes them.
  The eight maps with no nav-graph unit, read from the install 2026-09-20 — 108 `.ain` files,
  100 units. Three ship a header-only AIN (142–146 bytes) with no nodes but a real hull word:
  `sp_genesisdevice_1`, `sm_smoke_1`, `hw_chateau_1`. Five have a real graph that exists ONLY as
  the patch's file, with no base copy (`Unofficial_Patch/maps/graphs/`: `la_malkavian_3b` 8,117
  bytes, `la_bradbury_1` 4,990, `hw_warrens_2b` 4,910, `la_library_1` 3,154, `sm_coffee_1`
  1,840). All eight are missing because the 2026-08-30 export read the base set, before 3's
  patch-first rule; each exports under that rule and takes its own agent word.
  `sp_genesisdevice_1` is 21-2's, with the rest of that map; the other seven are 21-8's.
  Consumes: 3. Size: M. Effort: Sonnet / high.

- [x] **21-2. One command: the imports fold into `bake map`, and five maps stand.**
  **Landed 2026-09-21: all five maps stand and pass the gate.**
  `uv run elysium bake map --maps <map>` alone yields a loadable level: one editor session authors
  the geometry, the level's actors, `DA_<map>_Entities`, `DA_<map>_Environment`, the cooked
  `DA_<map>_Collision`, the world-collision actor, the nav-area marks and the Recast meshes, and
  one `save_map` carries all of it; then `verify nav` judges the meshes that run just built. The
  three `import map-*` commands, their launchers and `level_collision_is_current` are gone, the
  four runtime refusals name the one command, and `--from <stage>` is a scoped `--force` over
  `textures materials world sky particles entities environment collision level` (it cannot be a
  scoped SKIP: `stage_level` authors from tables the earlier stages fill on their reuse paths and
  stamps the level against a recipe computed from them). `MAP_BAKE_BATCH` is 1 — the cook and the
  Recast build now live in the bake process.

  **What the fold's own new check found, and what it cost.** The bake asserts after its save that
  the level on disk carries a mesh with tiles for every agent its graph names — the only thing in
  the project that had ever asked whether baked navigation reached disk, and
  `level_collision_is_current` had asked it on the NEXT run. Standing it up settled two things and
  found one defect:
   * Recast builds into the unsaved `/Temp/Untitled_N` world and the data survives `save_map`'s
     rename intact. No second save is needed.
   * The check itself cannot be a `LoadPackage` read, which is how it was written first: it
     reported "no meshes" on a level that had just built 2,150 tiles, because `TActorIterator`
     walks `UWorld::Levels` (empty on a package-loaded world) and `GetNumActiveTiles` reads a
     generator that only exists once a navigation system has registered the data. It re-opens the
     level through the editor's loader instead. (`CountBuiltReflectionCapturesInPackage` escapes
     both by reading `ULevel::Actors` and the level's own registry; there is no such shortcut for
     a tile count.)
   * **The displacement terrain was never in the level.** `AuthorFromPayload` placed the signature
     bodies only, so story 3's level actor left the payload's displacement trimesh out; the
     runtime still built its own transient component, so collision was right in game and the
     BAKE saw no terrain — every map whose floor is displacement got a mesh with a hole where its
     ground is. It stands in the level now, one component wearing `PNS-` (what its transient twin
     answered: `BlockAll` less the +use and pick channels), its body `RF_Public` like the others
     because a level may not name a private sub-object, and `NavigationBoundsOf` counts it. The
     tutorial's 3,584 triangles and the hub's 288 now cut their meshes and both stay clean —
     and standing it is what exposed `.dispcol`'s reversed winding, below.

  | map | bodies (signature: hulls) | disp tris | marks (roadway / doors cut) | agents | tiles | gate |
  |---|---|---|---|---|---|---|
  | `sp_tutorial_1` | `PNS-` 2,128, `PN--` 243, `--S-` 15, `-N--` 5 | 3,584 | 44 (0 / 28 of 36) | Human, Rat | 945 / 1,303 | clean, 5 excused |
  | `sm_hub_1` | `PNS-` 3,662, `PN--` 87, `PN-p` 93, `-N-p` 31, `---p` 9 | 288 | 61 (9 / 27 of 29) | Human, Rat | 1,123 / 1,608 | clean, 30 excused, 3 pinned |
  | `sp_soc_3` | `PNS-` 197, `PN-p` 36, `PN--` 32, `-N-p` 10 | 20,448 | 19 (0 / 4 of 5) | Human | 383 | clean, 13 excused |
  | `sm_pawnshop_1` | `PNS-` 1,327, `PN--` 41, `PN-p` 8 | 0 | 10 (0 / 10 of 10) | Human | 255 | clean |
  | `sp_theatre` | `PNS-` 1,045, `PN--` 71, `PN-p` 5, `-N-p` 3, `---p` 1 | 0 | 14 (1 / 9 of 9) | Human | 496 | clean, 2 excused, 5 pinned |

  All five boot Active adopting the level's baked mesh with NO build, in one run of the game
  (2026-09-21): `sp_tutorial_1` 20.4 s, `sm_hub_1` 19.7 s, `sm_pawnshop_1` 18.5 s, `sp_theatre`
  38.6 s (its first load, cold DDC), `sp_soc_3` 12.6 s. The three displacement maps were re-baked
  after `.dispcol`'s winding was corrected and booted again: `sp_soc_3` 18.4 s, `sp_tutorial_1`
  23.3 s, `sm_hub_1` 20.6 s, each Active with no build. Each reports its displacement triangle
  count from the level it adopted rather than from a component it built.
  `Elysium.Content` is 11 of 11, `MapCollision.{Tutorial,Hub}` and `NavArea.{Tutorial,Hub}`
  included, so the fold and the displacement both left the witnesses' levels as story 3 measured
  them.

  The witnesses' figures are story 3's own, re-measured through the fold; their hull counts are the
  staged-hull numbers `Elysium.Content.MapCollision` pins, not the census pins of § "Job 3's level
  actor". The three new maps are all `UsedHullBits 0x1` — human only, no rat — so none of them
  builds a rat mesh and no door can be partial by agent. Their graphs, re-exported under 3's
  patch-first rule: `sp_soc_3` 116 nodes / 335 links, `sp_theatre` 104 / 311, `sm_pawnshop_1` 5 / 6.
  The exporter flagged a `node-count-mismatch` anomaly on the last, and examining it found the
  flag wrong rather than the graph: the decoder's check assumed 32 tokens per node, a number read
  off one 116-node graph, and so fired on 79 of 101 published units. The law is
  `NumHulls + 6 + ceil(NumNodes / 32)` — exact on all 97 non-empty patch graphs — because a node's
  record ends in a bitset of one bit per node, which selects among that node's own link neighbours
  (never a non-neighbour, over 11,558 nodes) by a rule the link rows do not carry. The check uses
  the law now and flags none of them; the bitset's meaning stays typed-unidentified until the
  retail loader is walked (`seam_map_nav_graph.md` § Node stream).
  `sp_genesisdevice_1`'s unit is exported too, and says what 21-1 predicted: 0 nodes, 0 links,
  `UsedHullBits 0x1`. Its bake is 21-4's.

  **`sp_theatre`'s five, judged and pinned.** Five links, one destination: node 26 stands 132 cm
  below every node that links to it (0, 1, 2, 20, 21, all at z −162). Both ends project and the
  mesh will not join them, because 132 cm is a DROP — past retail's own step height of 18 units
  (45.7 cm) and past even its graph builder's 40 (101.6). Retail's NPC gets down by falling;
  nothing in the port falls yet, the AIN marks no jump link here, and the harness's step-outlier
  excuse does not reach a rise that large. Pinned by index in `nav_known_findings.py`; story 5
  owns what an NPC does on reaching a link its mesh does not offer.

  **`sp_soc_3` was the map that kept this open, and what it took is worth the space.** Its graph
  asserted 19 ground links whose endpoints project nowhere and one whose ends project and cannot be
  joined, in two rooms (x 3,000…4,550 / y −1,285…−538 and x −10,400…−9,200 / y 10…584). Both
  rooms' ground is displacement, not brush: 20,448 triangles spanning z −2,111…+3,057, sculpted
  rock with floor, walls and cavern ceiling in one soup and no per-face contents word to partition
  by.

  **What the mesh actually did, measured** (`research/tooling/probes/probe_soc3_displacement_mesh.py`,
  which sweeps z at each failing node's XY and reports every height the agent's mesh answers at).
  The mesh is not missing over those rooms — it is at the WRONG HEIGHT. At six of the sixteen
  nodes (0, 7, 9, 73, 74, 75) the only answer is 5.75–6.5 m ABOVE the node, on the storey above;
  the floor the node stands on is not on the mesh at all. At the other ten the floor answers and,
  on six of those, the upper surface answers too. Every answer is `NavArea_Default`, so no mark is
  covering anything.

  **It was the winding, and the first measurement of it was wrong.** The soup's floor triangles
  carry component-cross normal z ≈ +0.99 and its ceilings ≈ −0.99 (2,463 up, 4,426 down, 13,559
  steep over the whole soup), and that was first read as "correctly up-facing, not the winding".
  It is the opposite. What decides walkability is not the arithmetic normal but the engine's own
  chain, every link of it in source: the cook swaps v0/v1 when `bFlipNormals` is set
  (`ChaosCooking.cpp:41`, and the payload sets it as `UStaticMesh` does); the navigation export
  feeds each triangle's indices as `{2,1,0}` (`RecastNavMeshGenerator.cpp:417`);
  `Unreal2RecastPoint` is the reflection (−x, z, −y) (`RecastHelpers.cpp:7`); and Recast walks a
  triangle iff the normal of what it was FED has +y (`Recast.cpp:379`). Run a component-cross-up
  triangle through that and it arrives with normal.y = −1 — a ceiling — and a component-cross-down
  one arrives walkable. Engine meshes are wound the other way, which is why the chain works for
  them. So Recast read every displacement floor as a ceiling and every ceiling as a floor, which
  is precisely what the probe saw: mesh on the cavern's roof, none on the ground.

  The defect is in the seam, not the bake. `pipeline/CLAUDE.md`'s coordinate contract pairs the
  Source-to-Unreal Y reflection with a winding reversal, and `displacement_triangles`
  (`UE_map_sidecars.py`) reproduced the legacy fan `v00,v10,v11` / `v00,v11,v01` verbatim, for row
  parity with the decoder. Nothing could tell: no one draws `.dispcol`, and a Chaos trimesh
  collides from both sides. It is `v00,v11,v10` / `v00,v01,v11` now — same rows, same order, the
  corners inside a row reversed — and on disk the counts exchange exactly, 4,426 up / 2,463 down.
  **`sp_soc_3` is clean: 20 findings to 0, 13 step-height outliers excused, nothing pinned, 383
  tiles.**

  Three more suspects were measured on the way and are recorded so nobody re-runs them:
  *clearance* (floor at node z − 2…6 cm, next surface 4.8–6.2 m above, agent 1.83 m); *the
  bounds* (standing the terrain moved `NavigationBoundsOf` by 38 cm on one axis; the volume was
  already 216 × 90 × 74 m); and *both faces of the sheet* (`bDoubleSidedGeometry` one-sided
  rebuilds the identical mesh — the navigation export does not read the flag — so it is back
  where it was). Recast logged no layer, clamp or tile-limit warning on any run. The lesson is the
  one 21-1's review already wrote down in another form: a normal computed by hand answers a
  question about arithmetic, and the question was about what four pieces of engine code do to a
  triangle in sequence.

  Retail: none.
  Port today: `import map-collision` authors the payload, places the world-collision actor, the
  nav-area marks and the meshes, and runs `verify nav` (`import_map_collision.py:123-519`,
  `cli.py:3219-3315`); `import map-entities` and `import map-environment` author data assets
  only and touch no level. After 21-1 all three are load-bearing, so the procedure is four
  commands in a fixed order.
  Job:
  1. `bake map` runs, per map and in one editor session: the geometry and the level, the
     entities asset, the environment asset, the collision payload COOK, the world-collision
     actor, the nav-area marks, the meshes, the prune, the save, then `verify nav`. The order is
     real: the actor names the cooked bodies, the marks precede the build
     (`import_map_collision.py:349`), the meshes are cut from the actor. The gate stays
     automatic and `--skip-nav-verify` stays for lane work. `level_collision_is_current`
     (`:286-332`), which exists to notice a level `bake map` has just wiped, goes.
  2. The three `import map-*` commands go. If lane work needs a partial run it is
     `bake map --from <stage>`, which runs that stage and every one after it, so a level is
     never left between stages.
  3. The runtime's refusals name the one command (`ElysiumMapCollision.cpp:93,151,208`,
     `ElysiumMapActorLifecycle.cpp:912`).
  4. The five maps. The witnesses re-bake as the regression. `sp_soc_3`, `sm_pawnshop_1` and
     `sp_theatre` re-export (`export map --intermediate-only`: the decoder still stands, and
     this is its last use — their `.hulls` predate the contents word), re-export their nav graph
     from the patch's loose AIN (3's rule; the units on disk for the last two are dated
     2026-08-30, before it), and bake. The nav gate has never judged them: every finding is
     judged — a mesh defect, a step-height outlier, a rat hole, reach that changed — and pinned
     with its reason in `validation/nav_known_findings.py`, and their signature counts and door
     cuts are recorded here as 3 recorded the witnesses'. An unjudged finding keeps this open.
  5. `pipeline/CLAUDE.md` and `pipeline/AGENTS.md` state the procedure as it now is.
  Acceptance: for each of the five, `uv run elysium bake map --maps <map>` alone yields a level
  that boots Active; `verify nav` green with its pins; `Elysium.Content.MapCollision` and
  `Elysium.Content.NavArea` green on the witnesses.
  Consumes: 21-1. Size: L. Effort: Opus / medium (editor ordering; three maps judged).

- [x] **21-3. The level carries everything a map needs.**
  **Landed 2026-09-21: the running game opens no file under `$ELYSIUM_EXPORT_ROOT/<map>/`.**
  Witnessed with the five maps' export directories renamed away — `elysium_maps_list` returned the
  six maps the mount carries whole, each of the five travelled and reached Playing, and each strung
  its cables from the level: `sp_tutorial_1` 70, `sm_hub_1` 76, `sm_pawnshop_1` 21, `sp_theatre` 12,
  `sp_soc_3` 4, one `UCableComponent` per `AElysiumRopeActor`, counted live. Those are the
  producer's own segment counts, and `bake map --verify` reports the same five against the staged
  rows, every material bound, 0 problems. `MapDir`, `MapExportReady`, `MapEnts` and `MapRopes` are
  deleted; `.ready` is gone from the producer too, since nothing ever read it.

  **Two corrections to this story's own text, both found by the audit and both material.**
   * **`FElysiumRopeDef` carries EIGHT facts, not seven.** The list below omits the flag word, and
     it is load-bearing: bit 0 is `Dangling`, `CRopeKeyframe::KeyValue` clearing
     `ROPE_LOCK_END_POINT`, which `BuildRopes` turns into `UCableComponent::bAttachEnd`
     (`ElysiumMapVisuals.cpp:607`). A cable baked without it hangs pinned at an end retail leaves
     swinging. The actor carries all eight and `bake_verify.rope_errors` compares all eight.
   * **Both "fixtures" of job 3 were dead code.** `FElysiumMapSlice` is reachable only through
     `Tests/ElysiumTerminalGym.h`, which no translation unit includes, plus an unused include in
     `ElysiumCameraCinematicTests.cpp`; `SurveyMap`/`FEntsSurvey` have no call sites at all, left
     behind by `44ac84f6`'s retirement of the corpus test tier. Owner decision: re-point both as
     written rather than delete, so the slice harness stays ready for the terminal gym's own TU.

  **What the gate's move cost, and what it bought.** `ElysiumMapExportGateTests.cpp` could not
  survive it: both its tests drove `HasTravelableExport` over a scratch export root
  (`FElysiumScratchContentRoot`), and a scratch root cannot fabricate a `/ElysiumBaked` package. It
  is replaced by `Elysium.Content.MapList` / `MapListMatchesGate`, which ask the mount itself —
  every map the gate accepts carries all four packages, every bare `.umap` is refused (the mount
  holds ~100 of those, levels baked before the three assets existed, so that half is not
  hypothetical), and `BakedMaps()` is exactly the accepted set. Filtering the registry on
  `UElysiumMapEntities` rather than on `World` is what keeps those ~100 out of the list without a
  second pass. One sharp edge, found by the test's own path-traversal case: `BakedLevel` composes on
  top of an empty `BakedMapDir`, so a name `BakedUnit` refuses still yields a path-shaped string —
  `HasBakedMap` asks `BakedMapDir` directly rather than testing `BakedLevel` for emptiness.

  Recovery: `seam_map_map.md` § "The level carries everything a map needs (0018 story 21-3)";
  `seam_map_material.md` § R6.5 and `entity_visuals.md` §5 (the sidecar is now an offline
  intermediate); `seam_map.md`'s Ropes row.
  Retail: none.
  Port today: three per-map reads of the export root survive 21-1, none gated:
  `BuildRopes` parses `<map>.ropes` (`ElysiumMapVisuals.cpp:589`); `HasTravelableExport` wants
  `<map>.ready` and `Travel` refuses when no export root is configured
  (`ElysiumMapSubsystem.cpp:315,330`); `ExportedMaps()` lists the root's DIRECTORIES (`:678`),
  so with no per-map directory the map cycle, `elysium_maps_list` and `NextMap` see nothing.
  And `.ents` is still opened by the `elysium.ents` console command
  (`ElysiumEntityDefs.cpp:287`) and two fixtures (`Tests/ElysiumMapSlice.h:38`,
  `Tests/ElysiumContentTests.cpp:184`).
  Job:
  1. Ropes bake. The stage reads the producer's rope rows (`UE_map_sidecars.py:1379`) and places
     one baked rope actor per row carrying `FElysiumRopeDef`'s seven facts — material id, both
     endpoints, width, rest length, nodes, texture scale — under its own baked tag; `BuildRopes`
     builds its cables from the adopted actors, and `bake map --verify` counts them.
  2. The travel gate and the map list ask the project: a map is travelable when its baked level
     and its three `DA_<map>_*` assets exist, and the list comes from the asset registry.
     `.ready` and the `IsConfigured()` refusal go.
  3. The console command and the two fixtures read `DA_<map>_Entities` through
     `ElysiumEntityDefSource::Load`. `FElysiumEntityDefs::Parse` stays: `ElysiumEntityIOTests`
     writes and parses a synthetic `.ents` of its own.
  4. `MapDir` and every `Map*` accessor under it go.
  Acceptance: the five maps boot, travel and list with `$ELYSIUM_EXPORT_ROOT/<map>/` renamed
  away; rope counts equal the producer's rows; the entity-reading suites green.
  As it was checked: `uv run pytest` green (`test_map_ropes.py` 16, `test_bake_map_ropes.py` 7;
  the two `kernel_ledger`/`kernel_shape` `--check` failures reproduce on a clean tree and are not
  this story's); `Elysium.Substrate.Ropes` and `Elysium.Content` 13 of 13; all five re-baked and
  `bake map --verify` clean; then the five directories renamed away and the five maps travelled in
  one run of the game.
  Consumes: 21-2. Size: M. Effort: Sonnet / medium.

- [x] **21-4. The bake needs no legacy directory.**
  **Landed 2026-09-21: a map goes from its published units to a level in one command, and nothing
  in the bake opens a file under `$ELYSIUM_EXPORT_ROOT/<map>/`.** Witnessed with all six export
  directories renamed away: `sp_genesisdevice_1` — which HAD one, contrary to this story's first
  text, so the clean-room witness meant deleting it — baked from its four `exports_v2` units
  alone, 7 world hulls, 3 brush bodies, 0 nodes, and `verify nav` clean; the other five re-baked
  with `--force --verify`. `bake map` runs `UE_map_sidecars.write_sidecars` itself inside
  `_stage_map_inputs`, into the producer's own `exports_v2/_sidecars/<map>/`, and names that root
  to both commandlets as `-BakeMapSidecars=`. The `.env` gate and `export map --intermediate-only`
  leave the procedure.

  **What the diff found, and what it cost.** The two ported producers were compared with the
  decoder while it still exists (`research/tooling/probes/decal_weather_parity.py`), and the
  result is better than the story assumed and worse in exactly one named place.
   * **Decals reproduce byte for byte on 84 of the 92 maps with a legacy `.decals`**, 5,037 lines
     compared. Three maps were the entity-lump refusals 21-7 has since removed, so they stage now
     and were never compared (the probe went with the decoder). **Five differ, by 22 rows in
     total, and every one is the same cause**: a displacement face states its geometry through
     its own mesh and its FLAT winding — which is what a decal projects onto — is published
     nowhere, so a decal the decoder bound to sculpted terrain has no face to bind here. Each of
     the 22 is an `unbound` row (no face), never an `unresolved` one (no material size), so no
     published decal material fails to size anywhere in the corpus; 21 sit inside a
     displacement's own bounds and the 22nd was walked to its face (`la_malkavian_5`'s
     `decals/damage/malkfire3` → face 2610, `dispInfo 103`, flat plane z = −64, which is the
     legacy line's own position). **All six maps here are byte-identical**, so the acceptance is
     unaffected; `la_library_1` 11, `sm_oceanhouse_2` 6, `sp_soc_1` 2, `sm_warehouse_1` 1,
     `la_malkavian_5` 1 are 21-8's to judge. `decal_rows` reports `dispFacesSkipped` per map.
   * **The rain cover's inputs reproduce exactly** — 245,571 triangles (24,214 world, 221,357
     prop), the same 278 world groups keeping the same 24,214 under the filter, world vertices
     within 7.9e-4 cm and prop vertices within 4.0e-4 cm, bounds within 6e-4 cm so the pinned
     footprint 28971.24 × 19639.28 holds — and so does the raster's COVERAGE: 964,071 covered
     texels, zero sentinel disagreements. The encoded heights differ on 0.75% of samples (31,614
     of 4,194,304; ±1–4 LSB for the bulk, 156 LSB at the tail) because **27.65% of the cover
     triangles are near edge-on seen from above**, where the top-down barycentric denominator is
     ill-conditioned and a sub-millimetre vertex shift moves the interpolated height by
     decimetres. Re-rasterising with the decoder's own bounds moves the count by 460, so it is
     the vertices and not the quantisation. Named, not chased.
   * **`<map>.materials.json` is owed no replacement.** Exactly one map in the whole export tree
     has ever had one — `sm_pier_1`, which this group delisted — and the V2 material lane already
     represents a PAKFILE-only material as a first-class unit keyed `maps/<map>/…` with its own
     `patchBase` chain. All three readers are deleted, and `MapBakeV2` stops calling
     `load_corpus` at all: no field it set had a reader on this lane.
   * **The entity lane's parity check was a self-comparison and is gone**, with
     `bake_map_entities`' `parity` required key. Since R3.5 the sidecar is written from the same
     join the stage calls; since this story the bake writes it moments earlier in the same run.

  **A check the story did not ask for, added because nothing had it.** There was no
  `verify_decals`: the decal count was a line in the bake log and nothing compared it with
  anything, so a level that had lost every projector passed every lane. `bake map --verify` now
  counts the `ADecalActor`s against the staged rows — material instance, location, and the
  `(half depth, half height, half width)` triple a deferred decal's `DecalSize` carries — keyed
  on the component's own `SortOrder`, since `get_all_level_actors` returns the editor's order and
  not the bake's. The key is free: the sort order IS the staged index, because it is also how two
  decals on one wall layer.

  **And one the story did not foresee.** The hub's wetness check read `<map>.mtl` off the export
  root and reported an empty corpus the first time the map was baked without one. It reads the
  staged material table now, joined to the corpus on each row's `provenance` and deduped on the
  group key with R7.4's suffixes stripped, which reproduces the expected 14 scalars exactly.
  `_world_materials` is gone with it, and nothing in the verify lane opens
  `$ELYSIUM_EXPORT_ROOT/<map>/` any more.

  As it was checked: `uv run elysium build` green; `uv run pytest` green apart from the two
  `kernel_ledger`/`kernel_shape` `--check` failures that reproduce on a clean tree and are not
  this story's. All six directories moved away; the six baked; `verify nav` 0 findings over all
  six; `verify maps` green. Then all six travelled in one run of the game — the tutorial's
  scripted opening playing, `elysium.weather.rain_on` bringing up the hub's two
  `rain_follow_emitter`s. Staging the decal lane over the whole published corpus is clean as well:
  105 of 108 maps with zero validation failures, the three that are not being the lump-reader
  refusals 21-7 removed, taking the corpus to 108 of 108. The
  six legacy directories were restored afterwards: they were the parity probe's comparison
  subject until 21-5 deleted the decoder, and nothing read them. (21-5 moved them away again for
  its own witness and left them there; the probe is gone with the decoder.)

  Recovery: `seam_map_map.md` § "The bake needs no legacy directory (0018 story 21-4)".
  Retail: none — but a producer ported here must reproduce the decoder's output, which is
  itself the port's reading of the BSP; a difference is named, not absorbed.
  Port today: `export map` runs `UE_bsp_to_scene.main` and then
  `rewrite_sidecars_via_producer` overwrites eight sidecars with V2 bytes
  (`export_all.py:156-250`). Four decoder-only products still have a live V2 reader:
  `.decals` (`bake_map_v2.py:380`), `.weather.json` with its height map (`:381`,
  `bake_verify.py:157`), `.props` (`weather.py:61`, `bake_verify.py:61,1856,2113`,
  `bake_map.py:479`) and `<map>.materials.json` (`bake_map.py:710`, `bake_verify.py:85`,
  `map_geometry.py:2250`). `bake map` refuses a map with no `<map>.env` in the legacy directory
  (`export_manager.py:1094`). The producer reads `shared/materials.json` and
  `shared/manifest.json` (`UE_map_sidecars.py:1403-1423`) and uses them once, for the six
  sky-face keys (`:1222`); the materials table is loaded and never read.
  Job:
  1. The decals producer: `infodecal` rows projected onto the nearest visible face, from the
     planes, nodes, leafs, faces, texinfo and texture sizes the map root unit already publishes
     (`seam_map_map.md:233-249`) — the port of `UE_bsp_to_scene.py:1268-1360` — staged as
     `decals[]` in the geometry manifest, not as a file.
  2. The weather producer: the entity half already runs on the producer's `.ents`; the geometry
     half takes cover triangles from the staged world geometry under the decoder's own filter
     (`UE_bsp_to_scene.py:1692-1703`), prop cover from the model units instead of
     `shared/props/*.obj` (`weather.py:56-90`), bounds from the world AABB. `sm_hub_1` alone
     ships it. The `weather_inputs` hand-over (`export_all.py:209-247`) goes.
  3. **Diff before delete.** Each producer is compared with the decoder's output on the five
     maps while the decoder still exists — after 21-5 nothing is left to compare with — and
     every difference is named and recorded in `seam_map_map.md`, as R3.3 named its own.
  4. The re-points: `.props` readers onto the staged `placements`; the `materials.json` reads
     deleted; the sky-face check onto the texture units, `_corpus()` going with it (checked
     2026-09-20: `exports_v2/textures/skybox/<sky><face>.glb` holds all six faces for every sky
     the six maps name — `la`, `pier`, `santamonica`, `hav`); `load_corpus`
     (`bake_map.py:701-714`) off `shared/`.
  5. The intermediates: `bake map` invokes the producer itself, writing to the producer's own
     default `exports_v2/_sidecars/<map>/` (`UE_map_sidecars.py:1433`); the collision,
     environment and level-recipe reads follow it (`map_collision.py:492`,
     `map_environment.py:266`, `bake_map.py:437-498`); the entity stage's parity check against
     `<map>.ents` (`map_entities.py:161-179`) is a self-comparison now and goes; the `.env`
     gate goes; `export map --intermediate-only` leaves the procedure.
  Acceptance: `sp_genesisdevice_1` goes from its `exports_v2` units to a level that boots,
  with no `$ELYSIUM_EXPORT_ROOT/sp_genesisdevice_1/` ever having existed; the other five
  re-bake with their legacy directories DELETED and `bake map --verify` green — decal and rope
  counts, the hub's weather package — against the values recorded before the deletion.
  Consumes: 21-3. Size: L. Effort: Opus / high (two producers ported and diffed).

- [x] **21-5. Deleting the decoder.**
  **Landed 2026-09-21: `UE_bsp_to_scene.py`, `UE_extract_corpus.py`, `CorpusBake` and
  `/ElysiumBaked/Shared` are gone, and nothing imports or names any of them.** With them went the
  two-pass `export map`, the `corpus` bundle, the `export prop` / `export material` /
  `export texture` unit commands, the `--particles` Niagara pass with `make_particle_systems.py`
  and the `<map>.particles.json` writer, and 8,018 lines net. `export map` survives as "import
  this map's model dependencies, then bake it"; `export all` / `export grid` are bundle-only runs.

  **This story is the recovered R9.2**, and R8.1 is the retire track's R9.1 — both defined only in
  `docs/project/seam_migration.md`, which `2f0f604d` deleted. Recovering that text
  (`git show 2f0f604d^:...`) is what let the docs half close rather than defer: R9.2 named
  `/ElysiumBaked/Shared/Textures`, the legacy `bake_map.py` lanes and the `<map>.particles.json`
  writer explicitly, which is this story almost verbatim.

  **Three targets the job list did not name, and two things already dead.** `wield_corpus`
  imported `_parse_ent_blocks` (the producer's `parse_entity_blocks` has a byte-identical body);
  four lighting probes imported the decoder only to reach `formats.bsp` re-exports and
  `base_material`; and **`bake_map.py:3129` still put `/ElysiumBaked/Shared` first in
  `MAP_SCAN_PACKAGES`, so every V2 map bake was registry-scanning the dead mount**. Already dead:
  `<map>.water` has no reader anywhere in the bake, and `export placed-model` called an
  `export_manager.export_placed_models` that does not exist.

  **`Bake` lost every dead member, not just its source path** (owner's decision). With `CorpusBake`
  gone `MapBakeV2` is the only subclass, so its 15 overridden base bodies had no live caller, as
  did the corpus-only `stage_props` / `_author_skin_set` and ten `bake_lib` readers (`MatDef`,
  `read_mtl`, `mat_from_record`, `read_obj`, `read_decals`, `read_skins`, `read_phys`,
  `read_floats`, `make_skin_set`, `glb_material_albedo`). **One correction on the way**: the base
  `_level_recipe` was NOT dead — `MapBakeV2` calls it and overwrites only its `props`/`prop_skins`
  — so it was restored minus those two entries. `shared_corpus.py` is 288 lines, down from 546,
  and owns no document at all.

  **Two verify checks needed a new source rather than deletion, and one lost half of itself.**
  The hub's 14 `GlobalWetness` scalars now come from the material lane's own manifest, which
  writes `WetnessDriven`/`WetnessScale` into each instance's scalar overrides — 19 rows carry one
  install-wide, 1×0.56 / 8×0.60 / 10×1.0, which is exactly where the hub's expected
  `[0.56] + [0.60]*6 + [1.0]*7` comes from. `_map_prop_mtls` became `_map_prop_material_units`,
  reading the model lane's `slots[].materialId` — the same list the `.mtl` carried, already
  resolved. The glass/refract check keeps only its refract half: `M_V2_Refract` is a master and
  states itself, but legacy `glass 1` has no V2 master (`glass/glass01` parents to
  `M_V2_LitTranslucent` like any translucent instance) and the corpus flag that distinguished it
  is deleted.

  **What widening that selector found, and why it was reverted.** Selecting on "binds a
  `NormalMap`" instead was tried, and **failed 4 of 4 maps on 15 units whose `T_<stem>_normal` is
  not `TC_NORMALMAP`** — `brick/sewwllb`, `concrete/sewer_sm_base`, `concrete/sewer_sm_hole`,
  `wood/boardwalka`, `blends/searocka` (twice), `blends/seaflra`, `.../pier/cliffa`, two phone
  cords, two rusty-pipe units, a sewerpipe cap and two vehicle skins. Those are real and they are
  the TEXTURE lane's question; asking it from a deletion story would have made this a corpus-wide
  regression report. Reverted, and the list is written into `seam_map_map.md` for whoever owns
  that lane. The material report also loses its legacy comparison columns (`classChanged`,
  `byLegacyClass`, `legacyRecordMissing`) with the corpus they compared against.

  As it was checked: `uv run elysium build` green; `uv run pytest` green apart from the two
  `kernel_ledger`/`kernel_shape` `--check` failures, confirmed identical on a stashed tree. With
  all six export directories renamed away, `sp_genesisdevice_1` baked from its published units
  alone — 7 world hulls, 3 brush bodies, 0 nav nodes, the numbers 21-4 recorded — and the other
  five re-baked `--force --verify` clean: decals **123 / 219 / 0 / 38 / 29**, each equal to 21-4's
  count. `verify nav` **6 maps, 0 findings**; `verify maps` 6 maps, zero verify errors. Then all
  six travelled in **one run of the game**: `sp_tutorial_1` from `new_game` with its scripted
  opening playing (the sign popup raised, the queue draining 4 -> 0), `sm_hub_1` with 18 queued
  events and `elysium.weather.rain_on` bringing up both `rain_emitter`s and the
  `rain_light_loop` bed, then `sp_soc_3`, `sm_pawnshop_1`, `sp_theatre` and finally the
  clean-room `sp_genesisdevice_1` — which adopted its baked navmesh with no build, ran its
  `logic_auto` and chargen script to the entry popup, and audited **14 mesh assets with 0 unbound
  or default-bound slots**, so the error material's move off the dead mount binds nothing wrongly.
  Its wire report is 49 authored / 3 fired / 3 delivered / **0 unknown target or input**. No
  import of `UE_bsp_to_scene`, `UE_extract_corpus` or `CorpusBake` anywhere; no
  `/ElysiumBaked/Shared` in source, config or tests. The 102 stale baked levels and the 1.8 GB
  `Shared/` package were pruned from the mount.

  **Owed, and stated rather than absorbed.** `test_legacy_map_native_props.py` tested the deleted
  `Bake._place_props`, so `MapBakeV2`'s native-reference embedding has no direct unit coverage
  until someone writes it against the staged placements. The producer's `EntityDivergences`
  defaults are now a written-down contract rather than a diffable one — which is why 21-4 measured
  the two ported producers *before* this story. The decoder's
  `source_visibility_backing_models` returned a warning per malformed link and the producer's twin
  does not, so those three diagnostic strings are no longer pinned. `mdl.write_obj_scene` is dead
  and `formats/mdl.py` was left alone.

  Recovery: `seam_map_map.md` § "Deleting the decoder (0018 story 21-5)"; the R-number scheme and
  the deleted roadmap's provenance are now in `seam_map.md` § "The R-numbers".
  Retail: none.
  Port today: after 21-4 nothing on the map lane calls it. `UE_bsp_to_scene.py` (~1,700 lines)
  is still imported by `UE_extract_corpus.py` (`decode_prop_models`, `DRAWN_TOOL_MATERIALS`)
  and by five tests for its helpers; `CorpusBake` (`bake_map.py:2633-2749`) authors
  `/ElysiumBaked/Shared` from `shared/`, a corpus no live resolver reads; the error material
  both bakes bind lives under that mount (`bake_lib.py:557`, `bake_map_v2.py:502`).
  Job, in order:
  1. `M_ElysiumError` moves to `/Game/ElysiumGenerated/Materials`; `_assert_prunable`
     (`bake_lib.py:710`) stops guarding the `Shared` prefix.
  2. `CorpusBake`, `bake_corpus`, `ensure_corpus_bake` and the `/ElysiumBaked/Shared` mount go,
     then `UE_extract_corpus.py`, the `corpus` bundle and the `export prop` / `export texture`
     commands it serves. `shared_corpus.py` keeps only what a V2 lane still imports.
  3. `Bake` loses its legacy source path — the `.obj` / `.mtl` / `.blend` / `.props` / `.lights`
     / `_sky.obj` / `brushes/*` reads of `load_sources` and the stages only they fed
     (`bake_map.py:716-790, 1578, 1787, 2324-2380`) — and stays as the base `MapBakeV2` builds
     on. `make_particle_systems.py` and `.particles.json` go with the emitter arm 21-1 removed.
  4. `UE_bsp_to_scene.py` and `export_all`'s two-pass `export_maps` /
     `rewrite_sidecars_via_producer` go. `export map`, `export all` and `export grid` lose the
     map decode; what is left of each is listed, and a command left with nothing is deleted.
  5. Tests: `test_map_sidecar_default_path.py` goes; `test_map_producer_join.py`,
     `test_item_models.py`, `test_area_portal_window_translation.py` and `test_map_sidecars.py`
     stop importing the decoder (a helper still needed moves into the producer); the
     corpus-bake cases of `test_bake_orchestration.py:136-184,358-372` go; the `Shared/*` path
     pins are re-pointed (`test_contracts.py:645-655,839`, `test_cook_roots.py:67`,
     `test_models_canonical_paths.py:117`, `test_import_textures_editor.py:580-588,694`).
  6. The stale baked folders are pruned: every `Plugins/ElysiumBaked/Content/<map>/` outside
     the six — about a hundred legacy-baked levels that already cannot load, `sm_pier_1` with
     them. Gitignored; 21-8 re-bakes what it needs.
  7. The contracts are rewritten to the lane as it stands: the ~44 selector and legacy-path
     mentions of `docs/contracts/seam_map_*.md`, the dangling `seam_migration.md` references,
     and R8.1 / R9.2 marked superseded by this group.
  Acceptance: 21-4's acceptance re-run unchanged; build and `uv run pytest` green; no import of
  `UE_bsp_to_scene`, `UE_extract_corpus` or `CorpusBake` anywhere; no `/ElysiumBaked/Shared`
  path in source, config or tests.
  Consumes: 21-4. Size: M. Effort: Sonnet / medium (deletion).

- [x] **21-6. The game needs no export root.**
  **Landed 2026-09-21: the running game opens no file outside its own project directory, and
  `FElysiumContentPaths::Root()` does not exist.**

  **One correction to this story's own text, and it doubled the work.** The three seams the four
  lanes deploy from carried NO source capsule. `corpus_deploy` lifts a unit's capsule -- the exact
  winning bytes in its BIN chunk -- and that is what makes a lane byte-exact; `script`,
  `engine-config` and `ui-resource` were all still schema 1.0.0 with no BIN chunk at all, so
  `source_capsules` refused every one of them outright. Three commits adopted the capsule before a
  single lane could be written, on `exporters/vdata_glb.py`'s one-line pattern, each with its seam
  doc and its round-trip test. Size M became L.

  **Two things the job list named that turned out to be already dead.** `IsConfigured()` had zero
  callers, and so did `SkipIncompleteCorpus` -- the ONLY C++ reader of the `.elysium-incomplete`
  markers, whose abstentions had already been replaced by structural ones. The markers' runtime
  half was a corpse, not a migration. `clean.py`'s offline half is untouched.

  **And one the job list did not foresee: the bytecode the VM writes into its own corpus.**
  CPython 2.1 compiles beside the source it imports and has no `dont_write_bytecode` -- the legacy
  mirror held 17 `.pyc` no exporter ever wrote. Once `sys.path` points at
  `Content/ElysiumCorpus/scripts/` they land there, and without a rule every `import scripts`
  would delete the compiled tree the running game had just built. `Lane.kept_suffixes` is new for
  it. So is `Lane.unit_guard`: the lane deploys the source and never the companion (a `.pyc` given
  a fresh mtime by a deploy can never validate against its sibling, so the interpreter recompiles
  and overwrites it anyway), which would silently deploy NOTHING for a source-less `pyc-only`
  unit -- the guard makes that a named failure instead. No unit in the corpus is one.

  **The `vdata/signs/` mount was deleted rather than repointed**, the `sound/` precedent: signs are
  the `vdata/signs/` subtree of the table corpus now, so the `vdata/` mount already answers that
  prefix, one sandbox path to one real path. It also retires the ordering constraint the mount
  table carried. All 278 panels deployed byte-identical to the flat mirror they replace, name for
  name, neither side holding one the other lacked.

  **What the UI reader flip cost, measured before the extractor was deleted.** `strings.json` was
  derived by one regex; the runtime parses the install's own KeyValues document now. **194 distinct
  tokens on both sides, same keys, same values.** The file carries 195 rows -- `GameUI_Advanced` is
  authored twice, "Advanced..." then "Advanced" -- and last-wins agrees through both readers.
  Reading the `Tokens` block rather than the root is also what keeps `Language` out: the regex
  dropped it by NAME, so a token genuinely called `Language` would have gone with it.

  **`Elysium.Scripting` does not exist** and never did -- this acceptance line was its only
  occurrence in the repository. The suites meant are `Elysium.Substrate.ScriptFS`,
  `Elysium.Substrate.Console`, both `Elysium.Substrate.Sign*` and `Elysium.Content.*`. There was
  no UI-string test at all either; `Elysium.Substrate.UiStrings` and `Elysium.Content.UiStrings`
  are new.

  As it was checked: `uv run elysium build` green; `uv run pytest` 4,173 passing with only the two
  `test_check_mode_matches_committed_tables` failures already red on a clean `main`; `uv run
  elysium test substrate` **1,242 of 1,242, 0 failed**, and `Elysium.Content` 14 of 14. Then the
  witness, with `$ELYSIUM_WORK_ROOT/exports` renamed away and no `-ElysiumContentRoot` passed: the
  game booted to its menu drawn off the deployed table (`UI string table: 194 entries`), the
  console seeded `4 file(s) from .../Content/ElysiumCorpus/cfg -> 121 aliases, 129 cvars; patch
  profile Plus` -- identical to the pre-flip run's numbers off the export root -- and all six maps
  travelled and reached Playing, `sp_tutorial_1` raising its scripted opening's sign panel out of
  `vdata/signs/` and each map importing its level script (`tutorial`, `santamonica`, `demo`) out of
  `Content/ElysiumCorpus/scripts/`. Not one missing-file complaint in the whole run. The VM wrote
  15 `.pyc` across the six travels and a re-run of `import scripts` reported `already current, 0
  pruned` with all 41 sources intact.

  Owed, and stated rather than absorbed: `importers/vdata.py` is still the one corpus lane not on
  `corpus_deploy` -- no recipe stamps, no read-back verification, non-atomic writes -- and signs
  now ride it. Eight of the seventeen `engine-config` units are published and not deployed. And
  the `.pyc` is exempted from the prune rather than suppressed: story 20's cook has to decide
  whether a packaged game's corpus directory is writable at all.

  Recovery: `seam_map_map.md` § "The game needs no export root (0018 story 21-6)"; an `## Import`
  section each in `seam_map_script.md`, `seam_map_engine_config.md` and `seam_map_ui_resource.md`
  (the three seams' capsule adoption is in their `## GLB structure`), and `seam_map_vdata.md`'s
  signs carve-out rewritten to what is now true.
  Retail: none.
  Port today: four loose trees are still read from `ElysiumContentPaths::Root()`, which is why
  the game cannot start without `-ElysiumContentRoot`: `scripts/` (`ElysiumPythonVM.cpp:523,
  945,990`, `ElysiumScriptHost.cpp:65`, ScriptFS `python/`), `cfg/` (`ElysiumCommandBus.cpp:31`,
  `ElysiumPythonVM.cpp:537`, ScriptFS `cfg/`), `signs/` (`ElysiumSignData.cpp:189,214`, ScriptFS
  `vdata/signs/`) and `ui/strings.json` (`ElysiumUIStrings.cpp:27`). None can be baked: the
  Python VM and ScriptFS read them as files. `exports_v2` already publishes the units —
  `scripts/` 41, `engine-config/` 17, 278 sign units under `vdata/`, `ui-resources/` 52 — and
  `dlg`, `lip`, `scenes`, `sound` and `vdata` are the precedent. The debug tools WRITE under the
  same root: `_cast`, `_compose`, `_greenroom`, `_move`, `_profile`, `_shots`,
  `_lights/<map>.probe.json`, the wire dump.
  Job:
  1. Four corpus lanes on `corpus_deploy`'s machinery (recipe stamps, byte-equality, pruning):
     `import scripts` → `Content/ElysiumCorpus/scripts/`, `import engine-config` →
     `…/cfg/`, signs through `import vdata` → `…/vdata/signs/`, UI strings → `…/ui/` (checked
     2026-09-20: `strings.json` is `resource/gameui_english.txt` parsed by
     `UE_extract_ui.parse_strings`, and `ui-resources/resource/gameui_english.txt.glb` carries
     that token table, so the lane deploys the file and the parse moves to the reader or the
     lane). The accessors and the ScriptFS mounts (`ElysiumScriptFS.cpp:56-63`) move to
     `CorpusRoot()`.
  2. Debug output lands under `Saved/Elysium/<kind>/`, beside `ScriptFsRoot`; the pipeline's
     readers of `_shots`, `_lights` and `_profile` follow.
  3. `Root()`, `IsConfigured()`, the `.elysium-incomplete` markers' runtime half and
     `-ElysiumContentRoot` go; `uv run elysium run` stops passing it. The legacy extractors
     these lanes replace (`UE_extract_scripts`, `UE_extract_cfg`, `UE_extract_signs`,
     `UE_extract_ui`) go with whatever `export` command is left empty. `CLAUDE.md`'s
     "Build & run" lines for `-ElysiumContentRoot` and `$ELYSIUM_WORK_ROOT/exports/scripts/` are
     corrected, and `docs/vision.md` § "The build shape" is flagged to the owner again.
  Acceptance: the six maps boot, travel and run their map scripts with NO
  `-ElysiumContentRoot` and `$ELYSIUM_EXPORT_ROOT` renamed away; the tutorial's scripted opening
  plays; `Elysium.Scripting` and the UI-string and sign suites green.
  Provides: to 20, the precondition of its cook check — no external export access.
  Consumes: 21-3 (21-5 for the commands it empties). Size: M. Effort: Sonnet / medium.

- [x] **21-7. The producer's debt: the lump reader and the entity divergences.**
  **Landed 2026-09-21 in nine commits: the entity lump is read rather than rebuilt, the five R3.4
  divergence flags are gone, and there is one output parser in the tree.** The story's own account
  of the damage was too small in both directions — the corruption was worse than a pinned quirk,
  and every flag but one measured zero on the six maps.

  **What the measurement found** (job 1, `research/tooling/probes/entity_reading_delta.py`, all 108
  published units; recorded in `entity_io.md` § "What the corpus actually authors"). The debt was
  two things and not six:

  | rule | rows it moved |
  |---|---:|
  | an empty `input` reads `Use` | 6,856 (2 of them name a target) |
  | the lump is read, not rebuilt | 9 entities on 8 maps, + 3 maps unblocked |
  | the datamap types an output | 18 on 7 maps |
  | retail has no comma gate | 2 |
  | no field is trimmed | 1 |
  | `delay` `_atof`, `times` `_atoi` + 0 → −1, key folding, the 255-byte cap, the key trailing-space trim | 0 |
  | field 6 (`extra`) stays dropped | 0 — and turning it on would have moved 24,114 |

  **The escaped-quote corruption destroyed placements, not a string.** Nine keyvalues in the corpus
  carry an embedded quote; the regex reader consumed the `origin` key of all nine of their
  entities, parking each at world zero, and swallowed most of their outputs as junk keys:
  `ch_hub_1[898]` 1 output → 12, `hw_hub_1[1202]` 4 → 11, `la_hub_1[1097]` 1 → 10,
  `sm_hub_1[1611]` 20 → 20 (the Python alone), `sp_endsequences_a[771]` 4 → 9, `[833]` 0 → 3,
  `sp_endsequences_b[628]` 1 → 6, `sp_epilogue[214]` 2 → 6, `sp_masquerade_1[38]` 13 → 14.
  `sp_endsequences_b[628]` also flipped `sky` false → true, because an entity at the origin
  classifies against the wrong BSP area. `la_hub_1` had lost `posterCheck()` and
  `checkMasquerade()` as well as `setArea("downtown")`. **`lilly_trunk.OnOpen` is not reachable
  here**: it is real in the shipped `sm_hub_1.bsp` (line 26017) and the Unofficial Patch deletes
  that line, so the patch-first corpus never carries it — a correction to `entity_io.md`'s own text.

  **Two findings the story did not have.** (1) Retail's splitter `0x101d16c0` has **no comma gate**:
  the port refused to parse a datamap-typed key with fewer than four commas, and `la_empire_2[412]`
  / `[413]`'s `npc_VHumanCombatant.OnDeath` = `"G.Dead_Russians = G.Dead_Russians + 1,"` is an
  output row in retail (addressing an entity that does not exist, so it drops — reproduced, not
  corrected). (2) The `times` rewrite had **three** owners, none of them the parser:
  `UElysiumMapEntities::Deserialize`, `FElysiumEntityDefs::Parse` and
  `AElysiumInfraActor::BuildDefOutputs` each applied it while `split_output`'s docstring claimed it
  and `int(number(parts[4], -1))` did not. It is the parser's now and the three readers copy
  verbatim.

  **How it landed.** Job 2 was bigger than the story's two callers: the reconstructed text also fed
  `SkyScope`'s `sky_camera` scan, `.env`'s fog and `skyname`, `.spawn`, the brush-model origins and
  `decal_rows`' `infodecal`, plus `map_ropes` and `wield_corpus`. All of them read
  `entity_pair_blocks` / `folded_keys` / `first_of_class` now; `entity_lump_text`, `_requote`,
  `parse_entity_blocks`, `entity_block_texts` and `MapJoin.text_blocks` are deleted, and
  `wield_corpus` — which reads a real BSP lump — uses the unit's lexer, the port of the tokeniser.
  The five scans match a classname as a classname where the text scan matched the literal string
  anywhere in the block; a before/after snapshot of all eight sidecars over the 105 maps that could
  be reconstructed at all shows that narrowing changing **no** `.env`, `.sky`, `.spawn`, `.hulls`,
  `.lights`, `.ropes` or `.dispcol` line (`research/tooling/probes/sidecar_snapshot.py`,
  `sidecar_diff_trees.py`).

  Then, with the five settled, the producer's own output parser agreed with the entities unit's on
  every field of every output in the corpus except the comma gate — so **the duplicate went**:
  `is_output_key`, `_is_datamap_output` and `split_output` are deleted and `entity_outputs` reads
  the unit's published `outputs[]`, whose `decode._output_row` is the port of `0x100ccf90`. The
  `Use` substitution landed in that parser, so all 108 entity units were re-exported
  (`export_v2 map-entities-glb --all`, validation clean on every one).

  Recovery: `seam_map_map.md` R3.4 (rewritten to "settled", one row per divergence with its retail
  address and measured count) and its `sm_hub_1` bullet, whose claim that `UElysiumMapEntities`
  "inherits the fix by construction" was the false statement this story existed to correct;
  `seam_map_map_entities.md` → "Outputs"; `entity_io.md` § "What the corpus actually authors".
  Oracle: `entity_io.md` § "The lump tokeniser and what a keyvalue actually becomes"
  (recovered 2026-09-21: the tokeniser, the 256-byte key/value cap, the `{}()'` delimiter set,
  the key-only trailing-space trim, the datamap type switch and the six-field row parser).
  Consumes: 21-5. Size: M–L. Effort: Opus / high.

  As it was checked: `uv run elysium build` green; `uv run pytest` 4,166 passed with only the two
  `kernel_ledger` / `kernel_shape` `--check` failures that reproduce on a clean tree;
  `Elysium.Substrate` 458 of 458. All six maps re-baked `--force --verify`, `verify nav` clean on
  each. All 108 entity units re-exported with the exporter's validation clean, and all 108 maps
  stage through `map_ai_infra` and `map_decals`. Then all six travelled in one run of the game:
  **`sm_hub_1`'s `logic_auto` is entity 1611 with its `origin` back (`-2586.48 -2091.1 -111`), no
  key spelled `"),"`, 20 outputs, and the FIRST line of the map's I/O history is
  `py: __main__.setArea("santa_monica") => Void`** followed by the `basementdr` close and lock that
  call performs — the map-load script call the port had been losing since R3.2. The tutorial's
  scripted opening plays (`unhidePlus()`, the front doors lock, `masterRefill`, the sentry patrol,
  the move popup) and the `logic_failed_blueblood` chain fires all eight rows in reverse parsed
  order with both Python payloads, unchanged. Every row whose `input` is now `Use` and whose target
  is empty is still routed `<py-only>`, so the substitution adds no dead wire.

- [ ] **21-8. The other 102 maps.** On the owner's approval, not automatically — run when a
  story's witness needs a map outside the six, not before.
  Job: each map from its `exports_v2` units through `bake map`, its nav findings judged as 21-2
  judged three. Owed first: the five patch-only graphs re-exported under 3's patch-first rule
  (21-1 lists them); `sm_pier_1`, delisted by this group; and `la_ventruetower_2`,
  `la_ventruetower_3`, `sp_giovanni_2b`, which 21-7 unblocked.
  Consumes: 21-7. Size: L, by batch.

  **Started 2026-09-21 on the owner's approval, as a whole-corpus pass rather than a map at a
  time.** The legacy export tree went with it: the 102 per-map directories, the six `_21-5-away-*`
  witness directories and `shared/` are deleted (3.5 GB), leaving an export root with no map half
  at all. Five stale readers of it were corrected FIRST, so none of them failed silently
  afterwards -- `_maps_bake_fingerprint` had been hashing two paths whose readers went with 21-4
  and 21-5 and now hashes the four published units per map; `debug compose` gated on a legacy
  directory and now gates on the baked `.umap` through a new `asset_paths.baked_level_path`;
  `unreal.py`'s `_compose`, `_cast` and `_greenroom` readers still looked under the export root
  that 21-6 moved the writers out of; and `UE_extract_scenes.check_referenced` globbed legacy
  `.ents` for its `SceneFile` cross-check and now reads the published entity units -- which makes
  it STRONGER, 113 values over all 108 maps where before it saw only whichever maps happened to
  have been legacy-exported. All seven missing nav-graph units are exported, so the corpus is
  108 of 108, and they match 21-1's reading of the install (`hw_chateau_1` 0 nodes / hull bits
  16385, `sm_smoke_1` 0 / 1, then `hw_warrens_2b` 19, `la_bradbury_1` 25, `la_library_1` 14,
  `la_malkavian_3b` 35, `sm_coffee_1` 8).

  **How a finding is triaged** (owner's rule, 2026-09-21, after the first three):
  1. Ask whether the spec already knows the CLASS and has a story that owns it — the step-height
     outliers and the rat-hole erosion hypothesis (story 5), a link whose mesh answers only the
     long way round (story 5), per-agent links and drops (story 7), flight (story 12).
  2. If it does: make the minimal change that lets the map bake — a pin in
     `nav_known_findings.py` carrying the measurement — and annotate the owning story with what
     this map adds. Do not redesign anything from inside this pass.
  3. If it does not: stop and recover the cause properly, then FILE A STORY for the work. A
     finding whose class is unknown is a question about VtMB, and answering it by pinning would
     bury it.
  The second map judged took route 3 twice over (flight, and the link record), which is why the
  rule is written down rather than assumed.

  **What the pass is finding, and it is not what the story expected.** Every failure so far is
  the navigation gate, none is a bake failure. Two are judged:
   * **`sm_pier_1` link 97** (nodes 37->59, 3.53x) is pinned. Node 37 stands on the beach UNDER
     the pier and node 59 on the deck 461 cm above it, and the deck overhangs the beach from
     ~200 cm east of the straight line, so no climbing surface exists between them; the mesh's
     4,431 cm back to the ramp by node 47 is the only walk the geometry offers. Retail asserts
     the link and prices it: it is the only one of the map's 187 carrying `m_LinkInfo & 0x2000`,
     which retail's own A* multiplies by `RandomInt(5, 10)`. The port's 3.53x is INSIDE retail's
     own penalty for it.
   * **`la_ventruetower_3`'s 70 findings were all hull 20** and none is a mesh defect; they are
     flight, and they are now story 12's work list (see there). The gate learned flying hulls
     rather than pinning them away.
  Both settled a seam on the way: the link record's 25 tokens are now read
  (`seam_map_nav_graph.md` § "Link stream" -- token 2 is `CAI_Link::m_LinkInfo` at `link+0x64`,
  tokens 3..24 the per-hull move types), and that flag turns out to be the same `0x2000` story 3
  marked as the roadway area from the brush side. The two recoveries agree to the link on
  `sm_hub_1` (461 flagged, 1,185 unflagged) -- and reading the A* is what caught story 5's sign
  error, where the roadway was to be PREFERRED and is in fact avoided at 5-10x.

  **The first BAKE defect, and the shape of it is the story's whole point.** `ch_fulab_1` did not
  fail the gate -- it failed to bake at all: `ElysiumBeamActor exposes no property 'material' for
  row[725].material`. The producer has always written two fields for a beam's texture, the
  `vtmb:material:` id (`texture`, which the actor carries) and the material lane's resolved
  package path (`material`, which no actor property matches and nothing reads), and the bake
  writes every family row field it does not explicitly skip. No map in the six owns an `env_beam`
  -- `seam_map_map.md` says the beam table was exercised on this very map by STAGING and never by
  baking -- so the mismatch could not be seen until a map with one was baked. `material` joins
  `FAMILY_ROW_SKIP` as the join result it is, and the contract's `beams[]` table lists it now.
  Inert either way: all 47 beams in the corpus name one texture, and the runtime binds a fixed
  `BeamMaterial` regardless.

  Memory, measured for the first time (nothing in the project had this number): one editor process
  per map, peak working set **12.54 GB** worst case on a 32 GB machine, median 9.09 GB, lowest
  system free 2.62 GB, with no upward trend across maps -- the per-map process exit is releasing
  everything. Median 54 s per map.

  **STOPPED INCOMPLETE 2026-09-21 by owner decision, with the small-hull problem below unsolved.**
  63 of 108 maps attempted in 64 minutes: **37 green, 26 failed, 45 never attempted.** The ledger
  is `$ELYSIUM_WORK_ROOT/scratch/21-8/bake_ledger.jsonl` (one row per attempt: exit, seconds, peak
  RSS, log path) and `status.json` holds the green/failed split; `verify/nav/by-map/<map>.json`
  holds the per-map verdicts for the 41 maps judged after the retention fix. Re-running
  `scratch/21-8/bake_all.py` skips every green map and retries only the rest, so the pass resumes
  where it stopped -- and `ch_fulab_1` will then bake, since its beam fix landed after it ran.
  The 26: `ch_fishmarket_1`, `ch_fulab_1`, `ch_hub_1`, `ch_lotus_1`, `ch_temple_1`, `ch_temple_2`,
  `ch_zhaos_1`, `hw_609_1`, `hw_ash_sewer_1`, `hw_hub_1`, `hw_jewelry_1`, `hw_netcafe_1`,
  `hw_vesuvius_1`, `hw_warrens_1`, `hw_warrens_2`, `hw_warrens_3`, `hw_warrens_4`,
  `la_bradbury_1`, `la_bradbury_2`, `la_bradbury_3`, `la_confession_1`, `la_crackhouse_1`,
  `la_empire_1`, `la_empire_2`, `la_expipe_1`, `sp_giovanni_2b`.

  **What stopped it: the findings are one problem, not twenty-six.** Every failure but
  `ch_fulab_1`'s bake defect is the navigation gate, and over the 41 retained reports the 1,154
  findings sort by AGENT rather than by map:

  | hull | agent | radius cm | cell cm | Recast erosion | findings | maps |
  |---|---|---:|---:|---:|---:|---:|
  | 7 | TinyCentered | 20.32 | 10 | 30.0 | **624** | 5 |
  | 19 | Rat | 15.24 | 5 | 20.0 | **231** | 6 |
  | 0 | Human | 33.02 | 10 | 40.0 | 220 | 17 |
  | 13 | Tzimiscerunner | 30.48 | 10 | 40.0 | 70 | 5 |
  | 11 | Tzimisce2 | 63.50 | 15 | 75.0 | 9 | 2 |

  By kind: 789 ground links, 160 jump starts, 132 jump ends, 73 bridging. **855 of 1,154 -- 74% --
  are the two smallest agents**, whose eroded radius exceeds retail's hull by the widest margin.
  That is story 5's rat-hole hypothesis at corpus scale, and it is 21-10's to settle: pinning
  these one by one would be a tolerance wearing pins as a disguise, on a hypothesis
  `nav_known_findings.py` itself marks NOT proven. **21-8 cannot close until 21-10 does.**
  Nothing was pinned for it.

- [ ] **21-9. The gate's judgement machinery.** Opened 2026-09-21 by 21-8, which ran into its
  limits on the second map it judged.
  Port today: `validation/nav_acceptance.ground_link_errors` takes `known=` and reports a pinned
  finding rather than failing it, with a stale pin failing too. **`bridging_errors` and
  `projection_errors` take no pins at all**, so a judged finding in either has nowhere to live: on
  `la_ventruetower_3` the 12 jump-start, 19 jump-end and 32 bridging findings could only be
  handled because the hull turned out to FLY, which `FLYING_HULLS` covers wholesale. A
  non-flying hull with one judged jump endpoint would leave the gate red with no way to record
  the judgement, and the temptation then is a tolerance -- the one thing
  `nav_known_findings.py`'s own docstring forbids.
  Job: extend the pin mechanism to both checks on the shape `ground_link_errors` already has
  (pin by index, report on reproduce, FAIL a pin that stops reproducing), keyed by point index
  for projections and link index for bridging; `nav_known_findings` grows the two tables beside
  `KNOWN_DETOURS`. Then re-read whether any pin 21-8 wrote as a whole-hull flight exemption
  should be a per-finding pin instead.
  Consumes: 21-8's corpus pass, for the real finding set to shape it against. Size: S.
  Effort: Sonnet / medium.

- [ ] **21-10. The small hulls' cell size.** Opened 2026-09-21 by 21-8, which is blocked on it.
  Retail: `NAI_Hull`'s rat is 12 x 12 x 10 units (radius 15.24 cm) and its TINY_CENTERED is
  16 x 16 x 8 (radius 20.32 cm); retail's own graph asserts links through passages those hulls
  fit, because `CAI_TestHull` swept THAT hull to build them.
  Port today: `ElysiumRetailHulls::AgentCellSize` gives the rat 5 cm cells and TinyCentered 10,
  and `ElysiumNavBakeLibrary` applies them to every resolution (`:365-378`). Recast erodes the
  walkable surface by `ceil(radius / cell)` CELLS, so the rat is eroded 20 cm a side against a
  true 15.24 and TinyCentered 30 against 20.32 -- each agent loses 4.8 / 9.7 cm a side, and a
  passage between those bounds is open in retail and closed on the mesh.
  Evidence, measured rather than argued: the table above. The three `sm_hub_1` rat holes story 5
  already pins carry this as their "leading hypothesis, NOT proven"; 21-8 turned three into 855
  across eleven maps, which is what makes the experiment worth its cost.
  Job:
  1. **Settle the hypothesis before changing anything.** Re-cut ONE map's rat mesh at 2.54 cm
     cells (erosion exactly 15.24, four times the tiles) and re-ask the same failing links.
     `hw_hub_1` is the subject: 145 rat ground-link findings, the largest single set.
     `ARecastNavMesh::NavMeshResolutionParams` is a UPROPERTY, so this is a probe over the baked
     level and needs no C++ change or re-bake. If the findings collapse, erosion is the cause; if
     they do not, erosion is exonerated and the real cause is still open -- say which.
  2. On a confirmed cause, decide the cell size per hull against its cost (tiles go as the
     inverse square: the rat's 5 -> 2.54 is 3.9x the tiles, and `CellsPerTile` 200 means the tile
     edge shrinks with it), then re-bake and re-judge the eleven maps.
  3. Whatever survives at the chosen cell size is judged link by link and pinned with its
     measurement, as 21-2 and 21-8 pinned theirs.
  Consumes: 21-8's finding set (present). Provides: to 21-8, the ability to close; to story 5,
  a measured answer in place of its "three rat routes" framing.
  Size: M. Effort: Opus / high (one experiment, then a cost decision).

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
- **The species hull-transform writers stay with their classes in 0002** (recorded 2026-09-20, so
  the work is tracked rather than lost). 0018 story 3 provides both hull words, the generated
  class table and the setter; each writer below is built when 0002 registers its class, since the
  port spawns none of them today. Each changes `m_eHull` alone — the pathing hull survives the
  shape change — except the last two, which write both: `CNPC_VHengeyokai::StartTask 0x1038096e`
  and its transform `0x103831c0` (→ 18), `CNPC_VMingXiao::StartTask 0x10392f4d` and its transform
  `0x1039a77d` (→ 15), `CNPC_VMingXiaoTentacle::StartTask 0x1039c4c0` task `0x14b`
  (17/17 or 15/15), `CNPC_VSabbatLeader::TransformationStart 0x103ab39d` (0/0), and
  `CNPC_VVampireBoss::StartTask 0x103c5ac0` task `0x14e` `TASK_VVAMPIREBOSS_SET_AS_MONSTER` (0/0
  after a monster-model swap). Recovered 2026-09-20, `npc-ai/programs.md` § "The boss transformation
  programs": three programs issue it (boss `0x159`, Sheriff `0x15b`, SabbatLeader `0x163`), all six
  boss-line classes reach the arm — the SabbatLeader after three writes of its own — and the shipped
  maps fire it for the Sheriff (`la_ventruetower_3`), Andrei (`la_bradbury_3`, plus a code-side
  500-unit proximity trigger) and both Becketts (`sm_warehouse_1`, where the "monster" model is
  Beckett, so 0/0 lands on a human).
