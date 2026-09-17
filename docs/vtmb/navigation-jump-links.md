# AIN jump links and the navigator jump state

Recovered 2026-09-08 for `0002-npc-ai` story 19. Addresses below are in retail
`vampire.dll`; the V2 witness is `nav-graphs/sp_tutorial_1.glb`.

## Serialized connection

The version-30 loader `0x102f5bd0` requires 22 hulls. Its link loop allocates `0x6c` bytes and
constructs the link with `0x102dda70`. The disassembly at `0x102f61e8` reads source node into
`link+4`, `0x102f61fb` reads destination into `link+8`, `0x102f620e` reads link-info into
`link+0x64`, and `0x102f6224–0x102f6243` reads 22 integer movement masks into `link+0x0c`.
The GLB's existing `links[].fields` therefore means `[linkInfo, hull0Motion, …, hull21Motion]`.
The loader adds the same link to both endpoints (`0x102f626f`, `0x102f629f`). `DestNodeID`
`0x102dda40` returns the opposite endpoint; the serialized source is not a one-way marker.

`CAI_Node::InitLinks` `0x102fb4e0` tests ground movement first and writes mask 1. If standing
is legal at both ground nodes but walking failed, it probes both jump directions. Either
successful probe retains the shared connection: disassembly `0x102fbdc8` ORs mask 2 into that
hull's entry beside the `Nodes connect for jumping` diagnostic at `0x10610e24`. Flying and
climbing write 4 and 8. `HUMAN_HULL`'s initializer `0x102d4440` sets hull bit 1, identifying
index 0; its standing bounds are Source `(-13,-13,0)..(13,13,72)`.

The actual path eligibility predicate `0x102ff960` rejects `linkInfo & 0x1000`, then intersects
`link[hull+3]` with the NPC's movement capabilities (virtual `+0x804`). If the result is exactly
2 it calls the NPC's directional jump-legality virtual `+0x824`; stale bit 1 invokes the stale
link probe `0x102fce80`. A stale hint is not itself an unconditional rejection. The offline
jump projection carries enabled human links whose motion mask includes 2; Unreal's path and
capsule collision service supplies the runtime geometric test.

**Do not copy the later Source SDK's disabled bit:** its `ai_link.h` assigns `bits_LINK_OFF=2`.
VtMB's predicate above reads **0x1000**. The later SDK is corroboration for names, not the oracle
for this binary's layout or masks:
[Valve ai_link.h](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/ai_link.h).

`CAI_Node::GetPosition` `0x102fb0d0` adds `node+0x14+4*hull` to origin Z for ground-node
type 2. Non-ground nodes return their origin except for the separate ladder-offset branch.
The bake therefore adds the decoded `hullOffsets[0]` in Source space and then invokes the
pipeline's sole `source_to_unreal` conversion. No GLB parsing or coordinate conversion runs
inside the game.

## Tutorial witness

The decoded graph has 116 nodes, 234 links, used hull bits `524289` (0 and 19), and nine
human jump connections. Their link indices and endpoint node ids are:

| Link | Nodes |
|---|---|
| 22 | 8 ↔ 11 |
| 24 | 8 ↔ 15 |
| 30 | 9 ↔ 42 |
| 88 | 32 ↔ 36 |
| 110 | 49 ↔ 53 |
| 115 | 51 ↔ 53 |
| 147 | 70 ↔ 71 |
| 163 | 76 ↔ 74 |
| 218 | 101 ↔ 106 |

Every listed link's link-info is zero. Link 115 is Jump for hull 0 and Ground for hull 19,
which is why merging all hull masks is not a valid human-link filter. The other 225 human
entries are ground or disconnected and do not become jump proxies.

## Runtime state and implementation boundary

`CAI_Navigator::MoveJump` `0x102eece0` probes the jump while grounded before writing nav type
1 through `0x102eeba0` (`navigator+0x18`) and entering the motor's jump-start virtual (`+0x18`).
In flight it calls motor `+0x1c`. Upon grounding it calls motor `+0x20`, sets nav type 0, and
then advances/completes the path. Requirement 13's `STOP_MOVING` reader must still see Jump
after clearing the goal, so the body's ordinary hard Stop is not that operation during flight.

The port bakes one `AElysiumNavJumpLink : ANavLinkProxy` per selected connection. It uses only
the smart link; a parallel simple link would bypass the traversal callback. The motor begins
a CharacterMovement flight after a native capsule arc probe succeeds, preserves the suspended follower, reports Jump, then resumes the
follower after a valid landing or publishes failure with Ground. Goal cancellation preserves
airborne velocity. The substrate can therefore observe airborne Jump with effectively zero
velocity and run its recovered stuck-on-top arm. No extra flight deadline is introduced.
An actual lost/aborted path request or movement service reports failure.

The map staging version and jump payload hash participate in level invalidation. Malformed or
missing decoded graphs fail staging with a concrete source/export diagnostic. The pipeline
tests cover the raw tutorial rows, wrong hulls, disabled/stale masks, duplicate directions,
invalid endpoints, floor offsets, unit conversion and invalidation. Native automation exercises
the smart-link callback, physical flight/landing and stop-during-jump state; a rendered tutorial
playthrough remains a distinct acceptance step.

## Tutorial connectivity: the graph's components (2026-09-12)

For requests that reach the node-route builder, no node path for the hull makes navigator
`SetGoal` (`0x102ecd20`) fail, and the following path tasks answer that with `TaskFail(0x0c)`
"Don't have a route" (`TASK_GET_PATH_TO_INTERESTING_PLACE`, Troika `StartTask 0x102a1910` case
`0x31`; `TASK_GET_PATH_TO_PATROL_POINT`, case `0x13`, after `"%s can't reach patrol point"`).
Union of the 234 links over hull 0 (ground bit 1 or jump bit 2, `linkInfo & 0x1000` never set
here; 17 links carry no hull-0 motion) splits the 116 nodes into **ten components** of 27, 23,
18, 16, 10, 7, 6, 3, 3 and 3 nodes. Witness positions against it (Source units):

| entity | origin | nearest node | distance | component |
|---|---|---|---|---|
| `Jack` (spawn) | 144 7352 −199 | 61 | 6012 | 3 nodes |
| `ip_by_window` | 85 132 112 | 46 | 9 | 3 nodes |
| `ip_lean_1` | −221 −258 −32 | 114 | 16 | 27 nodes |
| `pt1` (`thug_1`) | −507 −84 −40 | 15 | 9 | 27 nodes |
| `ip_melee_guy` ×2 | −1709 468 −200 | 57 / 58 | 6 / 11 | 16 nodes |
| `mercenary_upstairs`, `sentry2`, `monk_upstairs_podium`, `sentry3_ip_arms_crossed` | −7139 3492 7055 … | 105 | 8963–9429 | none |

Jack's start area has no node within 6000 units and the Society hub at the far end has none
nearby in this exported graph. If this graph is the network retail loads, a node-route request
from those areas cannot bind its start. A refused request runs the program's failure route
(`_FAILED`'s 5.1–10 s retry for a
place; the default fail schedule `0x43 FAIL` — `STOP_MOVING; SET_ACTIVITY ACT_IDLE; WAIT 1;
WAIT_PVS` — for a patrol, then the idle selector's patrol step again: one refused route per
second, `npc-ai/schedule-kernel.md` § "The kernel's failure route and the base programs").
The hub's two patrollers are `sentry2` and `monk_upstairs_podium` (`SetupPatrolType` then
`FollowPatrolPath` from the level scripts, § "Patrol paths, walked"). The port's runtime Recast mesh
(`ElysiumMapActorLifecycle.cpp`, a projection of the `.hulls` sidecar — world brushes of
player-blocking contents, monsterclip excluded — at the engine's default agent: radius 34 cm,
height 144 cm, step 35 cm) refuses the same routes on `sp_tutorial_1` as partial paths ending
160–240 m short. This agrees with the static node-route prediction, but the selected retail
network and branch still need confirmation. Retail hull 0 stands
`(-13,-13,0)..(13,13,72)` (66 × 183 cm) and steps 18 units (45.7 cm). UNRECOVERED: whether door
brushes cut the port's mesh (retail's links pass through doors; the NPC opens them,
`m_hBlockedDoor` / `SelectDoorObstructionSchedule 0x102b7370`). Spec 0018 story 3 owns the
reconciliation. The 2026-09-17 correction below limits what this static census proves: it does
not establish which graph the patched game actually loads, or that every request needs one.

**Closed 2026-09-12 (story 24): `MONSTERCLIP` cuts links at graph build.** `CAI_Node::InitLinks`
(`0x102fb4e0`) runs every probe with trace mask **`0x2000b`** = `CONTENTS_SOLID 1 | WINDOW 2 |
GRATE 8 | MONSTERCLIP 0x20000`: the per-hull fit test at both nodes (`0x102f1900`, `"Cannot
fit at node %d"`), the ground stand test (`0x102e7270`, `"Failed to stand at %d"`), the walk
test (`0x102e4f50`, step 2.0, `"Failed to walk between nodes"`), the fly/climb hull traces
(`ITraceFilter` slot 4 with `0x2000b`) and both jump probes (`0x102e6d70`, 100.0, `"Nodes
connect for jumping"`). `MOVEABLE 0x4000` is not in the mask, so brush-entity doors do not
block a link at build; and a hull-0 ground link additionally runs `0x102e7e80(start, end,
0x2000)` (a hull trace with mask `0x2000` alone) and, when it hits, sets **`linkInfo |=
0x2000`** on the new link (`link+0x64`) — the retail door-on-link mark, consumed by the
navigator (`0x102fe9f0`, recovered below; `0x2000` is a contents bit whose name in this engine's
`bspflags` is not in the image). The port's `.hulls` projection excludes monsterclip, so a
monsterclip brush retail authored to keep NPCs off an area is a component cut retail has and
the port's mesh does not. That establishes a collision difference; it does not establish a
universal component gate in front of all movement. A link's `linkInfo & 0x1000` (disabled)
is never set at build; `0x2000` is the only info bit the builder writes.

## Route selection and node identity: correction (2026-09-17)

Read from the pinned `vampire.dll` through `vtmb_code`, with the branch and argument order of
`0x102f2060` and `CNodeEnt::Spawn 0x102d78d0` checked against `vtmb_asm`. The earlier sentence
"reachability is the graph, not the geometry" was too broad.

- `SetGoal 0x102ecd20` has an explicit node-path branch when the goal flags include bit 2;
  otherwise it calls `0x102f1dc0`, which reaches `DoFindPath 0x102f2330` and `0x102f2060`.
  `0x102f2060` can try `BuildLocalRoute 0x10304130` first, with node argument `-1`. A non-null
  result jumps directly to installing the path (`0x102f215d -> 0x102f219b`), before any call to
  `BuildNodeRoute 0x10304e00`. The local builder includes ground and fly branches; geometry
  and movement capability still decide whether either succeeds. Missing AIN coverage alone
  cannot prove that such a request fails.
- Goal type 8, used by the interesting-place task, sets path byte `+1` in `0x102f2330`;
  `0x102f2060` skips the local attempt when that byte is nonzero. Type 4, used by the patrol
  point task, does not set that byte in this switch. The complete lifetime/reset of this
  byte and the other local-route gates must be checked before classifying every goal type.
- `BuildNodeRoute 0x10304e00` has a separate positive-parameter branch (`0x103055b0`). On
  its ordinary branch it requires a nonempty network, binds both ends with
  `NearestNodeToNPCAtPoint 0x102f3c10`, calls `IsConnected 0x102f48b0`, and builds the entry
  and exit segments before selecting a node route. `IsConnected` compares node zone words
  at `+0x94`, including special values 1 and 3; it is not a lookup of an offline per-hull
  connected-component id. `0x102f3c10`'s uncached ground candidate box has half extents
  `(800,800,200)` Source units, keeps up to ten candidates, and applies capability, node,
  fit, failed-node and connection tests. An unrestricted Euclidean nearest-node lookup is
  not equivalent. The cache and fly branches differ.
- `InputFollowPatrolPath 0x1029ed90` resolves names through `0x102d2900 -> 0x102d2840`:
  a hint of type 10000 or 800 with the requested `Group`, then its network id at `+0x5e4`.
  `0x102aa640` rejects a missing/invalid patrol node with `TaskFail(0x1d)`, reads the valid
  node's hull position through `0x102fb0d0`, then asks `SetGoal`; route failure is separately
  `TaskFail(0x0c)`. `0x1029f6c0` reads that network node's hint pointer at `+0xa0` for patrol
  interest. These are observable node identities, even when a different engine moves the NPC.
- `CNodeEnt::Spawn 0x102d78d0` uses the running node counter `DAT_10926a3c` when a loaded
  graph is active (`DAT_1093408c` and the engine-mode check). It attaches the hint to that
  indexed node if the index is valid, advances the counter, and removes the authoring entity.
  The rebuild branch creates a node and writes the authored `m_nWCNodeID` to the lookup table.
  `info_hint`, `info_node_kick_over`, `info_node_kick_at` and `info_node_shoot_at` instead
  create standalone hints with network id `-1`. Thus BSP lump index, authored `nodeid`, and
  network index are different identities; neither matching by `nodeid` nor zipping every
  BSP entity with AIN nodes reproduces this loader.
- The previously unidentified reader of `linkInfo & 0x2000` is present in the alternate
  route builder `0x102fe9f0`. Its decompilation reports damage, so this fact was checked in
  assembly: `0x102feb60..0x102feb6f` draws one integer in 5..10 when its third parameter is
  nonzero; `0x102fecb6..0x102fecc8` multiplies an edge's cost by that integer when the flag
  is set (`TEST AH,0x20` on `link+0x64`). This is a route-cost distinction, not a disabled
  edge. The whole alternate route and its observability have not been audited here.

The current UP-first exported pairs have the following provenance and static counts. Component
sizes below union enabled links with hull-0 movement bits 1 or 2; they are structural evidence,
not an execution of the retail route predicate.

| Map | BSP source / node entities | AIN source / nodes / links | Hull-0 component sizes | Hull-0 jump links |
|---|---|---|---|---|
| `sp_tutorial_1` | Unofficial_Patch loose BSP / 203 | `pack007.vpk` / 116 / 234 | 27, 23, 18, 16, 10, 7, 6, 3, 3, 3 | 9 |
| `sm_hub_1` | Unofficial_Patch loose BSP / 578 | `pack007.vpk` / 578 / 1,856 | 509, 63, 2, 1, 1, 1, 1 | 119 |

Both BSPs contain repeated authored `nodeid` values. Tutorial's entity-lump SHA-256 is
`42e9f9d52e22aa59e670f66eadb1bcd8a3bcc85c31104af611ad933b9c71ce2c`, its AIN SHA-256 is
`c21f5c350da722aee52402cfa741af67346eb659cb2255ba6b092a609a84abf0`. Santa Monica's are
`d8a0f3298010622a0c6a58aaeafd37c4d5554f1651ba7a811dc163fe18140f64` and
`9a61aad428d1171724f7f26f5e6ca33de7b3891a35783b816dadb1e8af53c7ad`. Santa Monica's exported
node tail is 21 integers rather than tutorial's 6; its width diagnostic must not be treated
as proof that the fixed fields or link table are corrupt.

**Still unresolved:** which packed/cached/rebuilt network the selected patched install uses
on entry; the complete endpoint-binding filters and local-route acceptance; the alternate
route's cost/waypoint effects. The task-reader audit below establishes the hunt/cover/retreat/flank
topology reads separately. These findings invalidate a
universal nearest-component rule; they do not establish that all AIN-dependent behavior can
be discarded or that the exported tutorial mismatch itself is a retail defect.

### Installed graph candidates, inspected 2026-09-17

The exported packed graphs above are not the only installed candidates. Header reads of the
loose files give:

| Installed path below the game root | Nodes | Links | ZoneCount |
|---|---|---|---|
| `Unofficial_Patch/maps/graphs/sp_tutorial_1.ain` | 203 | 429 | 13 |
| `Vampire/maps/graphs/sp_tutorial_1.ain` | 116 | 234 | 11 |
| `Unofficial_Patch/maps/graphs/sm_hub_1.ain` | 578 | 1,862 | 7 |
| `Vampire/maps/graphs/sm_hub_1.ain` | 578 | 1,856 | 7 |

All four declare version 30 and 22 hulls. The patch tutorial file's 203 nodes agree in count
with the patch BSP's 203 node entities. This is evidence for a candidate pairing, not proof
of its origin, freshness, exact node correspondence, or the filesystem choice on a live load.
The graph loaded/rebuilt by the selected patched install still needs an instrumented witness;
file presence and count agreement alone do not resolve it. The packed-export jump/component
counts must therefore remain labelled with their source graph.

## Task readers of network data (2026-09-17)

Read through the kernel ledger and the pinned `vampire.dll` listings. The patrol task arms
below were checked in `StartTask 0x102a1910` assembly; graph selection reads were checked in
`0x10306700`, `0x10301720`, `0x10300b50` and `0x10302e50`. This is a task/data audit, not a
claim that every route-builder arm or every geometric predicate has been closed.

| Family and shipped consumer | Retail entry / helper | Network data observed | Observable result |
|---|---|---|---|
| Patrol input; patch `chinatown.py` installs reordered `A1 A2 A3 A4` patrols on `gangster_up_1/2` | `0x1029ed90 -> 0x102d2900 -> 0x102d2840` | Hint type 10000/800, exact-case `Group +0x5f0`, first match in hint-list order, hint network id `+0x5e4` | Name lookup needs no adjacency or enabled/owner/cooldown test. A failed token aborts the input before `0x1029f460` installs a path. |
| Current patrol goal; `FOLLOW_PATROL_PATH*` / `INVESTIGATE_NODE*` | `0x102aa640`, hunt twin call at `0x102a39d9` | Ordered path ids/current index, network bounds, node hull position via `0x102fb0d0` | Missing node fails `0x1d`; valid node submits a type-4 goal; refused route fails `0x0c`. |
| `GET_FULL_PATROL_PATH 0x7c`; registered, no shipped schedule use found | Start arm `0x102a39f4` | Only `nodes[currentIndex]`, its network row and hull position | A single type-4 goal, not a full-list handoff. Null path fails `0x1d`; current id `-1` returns without task completion/failure. See `programs.md` for the exact limits of the recovery. |
| `TASK_PATROL_PATH 0x105`; `SCHED_TROIKA_IDLE_PATROL` | `0x102a594d -> 0x102a5904` | No graph or patrol-object read in this arm | Sets movement activity, clears memory mask 2 and completes. Its upstream route source remains open. |
| Patrol interest; shipped patrol schedules, `target_name` / `ip_percent` in the entity census | `0x1029f6c0`, `0x1029f650`, `0x1029f730`, `0x1029f780` | Current node's hint at `+0xa0`; hint place name `+0x468` and chance `+0x46c`; live named place | Interest roll/cache, place claim, facing/activity/wait and outputs. The association is sufficient for this lookup; adjacency is not read here. |
| Interesting-place pick and walk; tutorial `thug_1`, `pt1..pt3` | `0x102db590`, `0x102dad60`, `0x102da0d0`; path task at `0x102a1910` case `0x31` | Pick reads place eligibility/ratings/groups/capacity/bounds, not AIN. Walking submits the sampled destination as goal type 8, whose route branch consults the network. | No eligible place/spot fails `0x22`; refused route fails `0x0c`, releases the place and enters the schedule's retry path. |
| Hunt list/terminal point; `HUNT_SETUP`, `HUNT_SETUP_NO_ENEMY` and the species hunt setup programs | `0x10306700`, `0x10306f60` | Nearest node, hull positions, adjacency/order, visited nodes, capability/hull and full link predicate `0x102ff960` | Direction-dependent neighbor choice with RNG and stopping rules; one helper returns a list, the other a terminal-node hunt target. Task failure is `0x20`. |
| Cover; `SHOT_BY_UNKNOWN`, `COMBAT_DODGE` and cover-from-enemy/sound programs | `0x102edc80 -> 0x10301720`; directional twin `0x102edd50 -> 0x10302320` | Node positions/type, adjacency/order, node cooldown `+0x9c`, associated hint `+0xa0`, link-off and hull/capability masks; live geometric validation | Chooses a node, updates cooldown and claims an associated hint; candidate failure follows the calling task. Owner-only hint test differs from flank. |
| Retreat/back-away; `MELEE_RETREAT`, `RUN_AWAY_FROM_ENEMY` | `0x102edae0 -> 0x103008f0 -> 0x10300b50` | Nearest nodes for NPC/anchor, hull positions, adjacency, full link predicate, distance/height tests and standability | Recursive neighbor selection chooses a retreat node or fails; a vector pointing away from the enemy is not the same query. |
| Flank/shoot position; `FLANK_ENEMY`, `CHASE_ENEMY_LKP_FLANK`, `COMBAT_DODGE_FLANK` | `0x102ed9c0 -> 0x10302e50` | Nearest node, positions, adjacency/order, type/cooldown, hint availability, directional/range and shoot/LOS checks | Chooses a stable node id, stamps cooldown and claims its hint; no candidate yields the task's flank failure. |

### Static topology and live query state

The relevant `CAI_Node` layout includes origin `+0x08..+0x10`, the hull-offset table starting
at `+0x14`, node type `+0x70`, adjacency count/list `+0x78/+0x7c`, zone `+0x94`, runtime
cooldown `+0x9c`, and associated hint `+0xa0`. Links carry endpoint indices at `+4/+8`, hull
motion words at `+0x0c`, and link-info at `+0x64`. These are distinct from BSP entity index
and authored `nodeid`. The serialized graph establishes topology; cooldown, hint handles and
query cursors are live state, not immutable AIN fields.

Hunt selects among eligible unvisited neighbors by direction and returns ordered node choices.
Its input position, target direction, random draws, distance budget and termination affect the
result. Cover and flank prioritize candidates by squared NPC distance and rotate neighbor
enumeration (`DAT_10934140/144` for cover, `DAT_10934148` for flank). Retreat searches neighbors
subject to increasing distance from its anchor, vertical limits and standability. These consumers
observe more than whether two endpoints share a connected component.

The predicates deliberately differ. Hunt/retreat use the full link predicate `0x102ff960`.
Cover/flank inline link-off and hull/capability checks. Cover accepts an absent hint or one
whose owner test `0x102d1450(hint, NULL)` succeeds; it does not substitute the disabled/next-use
predicate. Flank calls `0x102d14c0`, which checks disabled, next-use and ownership. Success
updates node cooldown and may claim the hint. A shared generic "usable node" predicate would
erase those distinctions. First-match hint-list order, entity-list order and per-node neighbor
order are also separate contracts; none may be replaced with an arbitrary sorted enumeration.

Patrol input failure deserves its own distinction: `0x102d2900` returns `-1` on lookup failure,
but `InputFollowPatrolPath 0x1029ed90` logs and returns before the common path builder. It does
not install a partial list containing that sentinel. This differs from a task later encountering
an invalid current node in an already existing path.

### Mutations reached by I/O and Python

The patch's `python/temple/temple.py:65..68` finds `Bottleneck_Cover` and calls `EnableHint()`.
`python/chinatown/chinatown.py:224..229` finds the gangster entities, calls `SetupPatrolType`,
then `FollowPatrolPath`. The tutorial entity export also carries
`logic_failed_blueblood.OnTrigger -> blueblood_maker.Spawn` with delay 1.25 s. These are direct
content witnesses that infrastructure is addressed outside the NPC task loop.

`CAI_Hint::InputEnableHint 0x102d09f0` calls base `ScriptUnhide` then clears disabled `+0x5e8`;
`InputDisableHint 0x102d0a20` calls base `ScriptHide` then sets it. The hint's own hide/unhide
overrides `0x102d0860/0x102d0890` do the corresponding writes, and `Kill 0x102d08c0` dispatches
its hide slot rather than destroying the hint. `InputSetUserData 0x102d4060` writes the string
at `+0x5d4` for a string variant and clears it otherwise; the consumer remains unrecovered.
Interesting-place `InputDisable 0x102db440` clears enabled then invokes visitor eviction
`0x102daac0`; `InputEnable 0x102db420` sets enabled. Availability, ownership, occupancy and
cooldown therefore remain runtime facts, with family-specific readers as described above.
Python input/field dispatch and event order are documented in `python_bridge.md`; name
resolution and output actions are in `entity_io.md`.

**Open after this audit:** selected graph loading/rebuild, complete nearest-node cache/fly and
local-route gates, alternate movement-route costs/waypoints, `GET_PATH_TO_HINTNODE` /
`SNAP_TO_HINT`, the cower search through slot `0x688`, and the upstream route used by
`TASK_PATROL_PATH`. The precise geometric constants and some caller-wrapper variants still
need their full walks. Base node/near/far cover registrations and `GET_FULL_PATROL_PATH` have
no shipped schedule witness in this audit; registration alone does not establish required
behavior. Adjacency reads in the witnessed hunt/cover/retreat/flank families are established.
