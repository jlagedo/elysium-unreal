# 0018 world-ai-infrastructure — The world's AI infrastructure: the graph, the places, the sound world and the groups every NPC queries, witnessed by the tutorial's alley and the Santa Monica hub

## Witness
Two maps, baked into their levels and standing in the editor before any NPC program runs on
them. `sp_tutorial_1`: the thug's alley — `pt1..pt3`, his `hint_groups`, the nine hull-0 jump
links, the ten graph components, the footstep and door sounds he hears. `sm_hub_1`: 38 placed
NPCs and 48 maker requests, the densest ambient population in the corpus — pedestrians walking
places and crosswalk links, cops, makers cycling. The witness is that every infrastructure object
on both maps is visible as a baked actor, answers the queries the mind makes with retail's
answers, and is exercised by a scene test that needs no NPC to run.

## Scope
Everything an NPC queries but does not own: the nav graph and its reachability on the Unreal
mesh; hint nodes; interesting places and their type table; patrol paths and patrol-point interest
records; the shared AI sound list and its volume table; squads; the attack coordinator and
standoff goals; the relationship defaults; makers; the player-law bus. For each object: its baked
actor or asset in the generated level, the substrate record the runtime builds from it at
activation, its query surface (exactly the calls the kernel's closure makes into it), and its
debugger view.

Owned elsewhere: the mind that consumes these queries — **0002**; the data seams, the class tree
and the verdict pass — **0019**; the player's law acts — **0005**, **0006** (this spec owns the
bus NPCs read, not the transaction that writes it); the scripted entities — **0003**.

Three rules, decided by the owner 2026-09-15:
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
  source.** For now only the AI infrastructure entities bake as actors — hints, interesting
  places, patrol points, makers, and the graph's nodes and links; every other classname in the
  lump keeps its `.ents` read until the map lane's spec. So a placed object in this spec is a
  baked actor, a table is a deployed corpus file read from `CorpusRoot()`, and the substrate
  record is built from the baked or deployed thing at activation. The bake and the
  import regenerate everything, so an edit made in the editor does not persist; an authored
  override layer would be a separate, named story. (`docs/vision.md`
  § "The build shape" still says the runtime builds "from the intermediates on disk"; that
  sentence is superseded by this decision and awaits the owner's edit.)
- **Every interaction reaches Unreal.** Walking, path following, traces, collision and line of
  sight go through Unreal's NavMesh, nav links and collision — which is the second reason the
  objects are real actors. Named by content → record; needed by the world → Unreal service.
  The retail graph survives only as data on the baked node actors (per-hull masks, component
  id) for the reachability gate; the sound list is a rule object and sound propagation is not
  reproduced; no Source pathfinder, sector partition or hull probe is ported here.
- **A story lands only against its query surface.** Story 1 lists the calls the closure makes
  into each helper class; a story's tests are those calls against the baked witness maps, not
  a speculative API.

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

**`sp_tutorial_1`.** The graph is ten components (27/23/18/16/10/7/6/3/3/3 nodes); nine hull-0
jump links `22, 24, 30, 88, 110, 115, 147, 163, 218`; Jack's start has no node within 6000
units. `pt1` (`group_id 2`, `enabled 1`, `min_time 30`, `max_time 60`) at the thug's spawn,
`pt2` / `pt3` (`group_id 2`, `enabled 0`) down the alley. `thug_1`: `hint_groups 1..32`,
`interesting_place_groups 2`, `use_interesting 1`, no squad; his investigate inputs are the
player's footsteps (180/240 units) and doors; the `logic_gunfire` sounds are `sound_event 0`,
audio only. `squad_warehouse` (`thug_2`, `thug_3`) is the map's one squad.

**`sm_hub_1`.** 86 definitions, mostly `npc_VPedestrian` and makers. The per-map counts of
hints, places, patrol points and nodes are **not in the oracle yet**; story 1 produces them.
Until it lands every size below is provisional.

## Stories
In build order. A story is done when its object is baked on both witness maps, its actor or
asset stands in the level, every query on its surface is tested against the baked level, and
its recovery is written in the oracle section it names.

- [ ] **1. The infrastructure census and the query surface.**
  Retail: none — a reading of the exports and the ledger.
  Job: one survey over the 22 entity exports and the 22 nav graphs: per map, the counts of
  `info_node`, `info_node_hint` by hint type, interesting places by group and enable state,
  `info_node_patrol_point`, makers by `NPCType`, squads, and every keyfield each carries; and,
  from `functions.md`, the calls the closure makes into each helper class — one table per
  object, "query, retail address, caller, what it answers". Lands as a section of
  `population.md` and the surface table every story below builds to; sizes are re-read from it.
  Size: S. Effort: Sonnet / medium.

- [ ] **2. The baked infrastructure actors.**
  Retail: none — a build-shape decision. Decided 2026-09-15 by the owner: **baked**, over
  runtime-spawned views, for the two reasons in § Scope (no sidecars; every interaction reaches
  Unreal).
  Job: following the jump-link lane, the map bake stages one record per placed infrastructure
  entity — nodes and links from the nav-graph unit; hints, places, patrol points and makers from
  the entities unit — and the editor bake places one actor per record in the generated level,
  carrying the entity's keyvalues as properties and a debug draw (type, groups, enabled, yaw,
  links, component). At activation the entity world adopts the actors by tag, as
  `AElysiumMapActor` adopts the level's, builds each substrate record from its actor, and skips
  those classnames in the `.ents` read so no entity exists twice; every other classname keeps
  the `.ents` path until the map lane's spec. The actor is the runtime's source, the export is
  the bake's. An export entity of a baked classname with no actor after the bake is a bake
  error. The tables these objects read (`interestingplacetypelist.txt`,
  `sound_volume_table.txt`, `Rules.txt`) are already deployed under
  `Content/ElysiumCorpus/vdata/` by the vdata lane; their loaders
  (`FElysiumInterestingPlaceTable::Load`, the `ElysiumVdata::ReadVdata` callers) read
  `CorpusRoot()` instead of the export root in 5, 7 and 11.
  Keyfields come from 0019's generated datamap bindings — **this story waits for 0019 story 2**
  so no infrastructure keyfield is hand-typed. Nothing baked is committed.
  Consumes: 0019 story 2. Provides: the actor the debugger (12) selects and the editor shows.
  Size: M. Effort: Opus / high (pipeline stage, editor bake, runtime bind).

- [ ] **3. The nav graph and reachability.** Absorbs 0002's stories 22 and 24, moved here.
  Retail: a route exists when the `.ain` graph has a node path for the hull, and for nothing
  else — navigator `SetGoal` (`0x102ecd20`) refuses otherwise and every path task answers
  `TaskFail(0x0c)`. Hull 0 stands `(-13,-13,0)..(13,13,72)` (66 × 183 cm), steps 18 units
  (45.7 cm); links carry per-hull ground/jump masks, `linkInfo & 0x1000` is never set at
  build; doors are not graph cuts: `CAI_Node::InitLinks` (`0x102fb4e0`) probes every link with
  mask `0x2000b` (`SOLID|WINDOW|GRATE|MONSTERCLIP`), excludes `MOVEABLE`, and marks a hull-0
  ground link that a `0x2000`-mask hull trace hits with `linkInfo |= 0x2000` (the NPC opens the
  door: `m_hBlockedDoor`, `SelectDoorObstructionSchedule 0x102b7370`, `IGNORE_DOOR_FAILURE`).
  **`MONSTERCLIP` cuts links at graph build.** On the tutorial, Jack's walk to `ip_by_window` /
  `ip_lean_1` and every hunter's route fails at `SetGoal` and runs the program's failure route.
  Port today: a Recast projection of the `.hulls` sidecar with monsterclip excluded
  (`ElysiumMapCollision.cpp:285`), the engine's default agent (`DefaultEngine.ini:73-75`),
  built at activation (`ElysiumMapActorLifecycle.cpp:840`); the nine jump links as baked
  proxies (`ElysiumNavJumpLink.h:12`); `FindPathSync` called once, for the jump-link check
  (`ElysiumNpcBody.cpp:617`); partial paths refused (`ElysiumNpcBody.cpp:545`). Its connectivity
  matches retail's refusals on the tutorial by coincidence of geometry.
  Job: nodes and links baked as actors carrying their per-hull masks and hull-0 component id
  (traversal links as Unreal nav links, as the jump links already are); the reachability gate
  reading the component ids off the actors; the `.hulls` / `.dispcol` collision the mesh
  projection reads today moved off the export root under the no-sidecars rule (consumed from
  the map lane, not owned here); the reachability gate — a request whose start and goal fall in different hull-0
  components (nearest node per end) is refused before Recast is asked, so `TaskFail 0x0c` fires
  where retail's does and the mesh supplies only the geometry inside a component; the agent
  from hull 0 in the `RecastNavMesh` block; monsterclip brushes added to the NPC-blocking
  geometry (a second projection or a per-agent area, the player's collision untouched); a
  game-side check that every enabled hull-0 ground link is walkable on the built mesh,
  reported like the jump-link staging; door brushes verified not to cut the mesh. No pathfinder
  is ported: Recast walks inside a component.
  Decision for the owner, carried from 0002/24: the gate is the retail contract; naming wider
  reachability a modernization means NPCs retail stands idle (Jack at the tutorial start) walk
  off in the port.
  Provides: the route refusal 0002's path tasks and 0003's walks fail through. Consumes: 2.
  Oracle: `navigation-jump-links.md` § "Tutorial connectivity: the graph's components" (incl.
  "Closed 2026-09-12: `MONSTERCLIP` cuts links at graph build"). Unrecovered: the navigator's
  reader of `linkInfo & 0x2000`; the name of contents bit `0x2000` in this engine's `bspflags`;
  whether door brushes cut the port's mesh (a witness, not a corpus item).
  Size: M–L. Effort: Opus / high.

- [ ] **4. Hint nodes.**
  Retail: `CAI_Hint`'s datamap (`shape.md` § "The hint node's own words"), the three
  validators (`0x10295ed0`, `0x102961a0`, `0x10296c40`), `FValidateHintType` slot 566 with its
  ten species bodies, the two hint searches and the cover forwards (`0x10365780`, `0x103bfa50`,
  `0x10297430`, `0x102974f0`), `hint_groups` as a 1-based index list → mask (empty = all), the
  expiring lock list bosses keep on hints (`0x103662d0`, `0x10366400`, `0x10366490`).
  Gap: the port has the rule bodies (`ElysiumNpcKernelHints*.cpp`) and no registry under
  them — every search answers over nothing.
  Job: the hint registry as records (type, group mask, yaw, node, `match_orientation`, in-use
  owner); the queries on the surface from 1 — nearest by type, group and distance; cover from
  enemy; shoot-at hint; lock and unlock with expiry; the validators rehomed onto the records.
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
  Consumes: 2, 5. Oracle: `schedule-kernel.md` § "Patrol paths, walked". Unrecovered: the
  `info_node_patrol_point` key names that fill `+0x468` / `+0x46c`; which shipped map authors
  one (1 answers this).
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
  components and the last refusal; selection through the baked actor.
  Consumes: 2–9. Size: S. Effort: Sonnet / medium.

- [ ] **13. The hub at idle: the second witness.**
  Job: `sm_hub_1` and `sp_tutorial_1` on the V2 lane (0002's 22, moved here) with every
  object above baked; a scene test per map that opens the baked level and walks each record's
  queries against it — places per group, hints per type, links per component, audibility at a set of
  origins — and a played witness once 0002's idle programs land: pedestrians visit places, cops
  patrol, makers cycle, nobody walks between graph components.
  Consumes: everything above. Size: M. Effort: Sonnet / medium, then played.

## Build order
1 → 2 (after 0019 story 2) → 3 → 4, 5, 6 in parallel → 7 → 8, 9 → 10, 11 → 12 → 13. Story 1
first because every size above is provisional until the census exists. 0018 runs beside 0019
and consumes 0002 only through the query surface: no story here adds a member to the NPC.

## Seams
- Provides: the query surface to 0002 — hint searches, place selection and the trio, the next
  patrol point and its record, audible sounds, squad members and the shared memory, the melee
  slot, the route refusal; the baked actors to the debugger; the second witness.
- Consumes: 0019's generated datamap bindings (every infrastructure keyfield) and tunables;
  0002/5's enemy record store; 0005 / 0006's law acts; the V2 export units through the map
  bake, never at runtime.
