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
`m_hBlockedDoor` / `SelectDoorObstructionSchedule 0x102b7370`). Spec 0018 story 7 owns the
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
- Which rows make a hint (0018 story 2, 2026-09-19). `CNodeEnt::Spawn` first calls
  `FUN_102d7d30`, which forces `m_eHintType` from the classname — `info_node` 0,
  `info_node_cover_med` 100, `_cover_low` 101, `_cover_corner` 10200, `_crosswalk` 11000,
  `_tzimisce_claw_left/right` 14000/14001, `_kick_over` 10300, `_kick_at` 10301, `_shoot_at` 10400,
  `info_node_werewolf` 0, `_werewolf_hint` kept only in 15000..15018, `_sabbat_*`
  16000..16005, `_bach_*` 17000..17005, `_chang_*` 18000..18003, `_manbat_fly_to_point` 20000;
  other classnames keep the authored value — and warns on a type-10000 row with an empty
  `Group` and on a duplicate exact-case `Group`. It rewrites `info_node_tzimisce` to
  `info_node`. The standalone set above makes a hint iff the (short) type is non-zero; every
  other `CNodeEnt` row iff the type is non-zero OR a `Group` is authored. The forced value only
  decides; the hint itself parses the row's raw block, whose `hinttype` is the authored one.
- `CNodeEnt::ParseMapData 0x102d7890` stashes the row's whole raw keyvalue block in
  `DAT_10926a38` before the base parse, and `FUN_102d2f30` creates the hint as classname
  **`ai_hint`** (`s_ai_hint_1060a388`), feeds it that block (vtable `+0x1ac`), writes the network
  id at `+0x5e4`, and runs `Spawn`/`PostSpawn`. So `group_id`, `ip_percent`, `target_name`,
  `StartHintDisabled`, `UserData` and the outputs — none of them on `CNodeEnt`'s seven-row
  datamap (`0x1060aaf8`) — reach the hint. The live entity is always `ai_hint`; the authored
  `info_node_*` entity never survives its own spawn. Over the 108 exported maps this rule and the
  census's "carries `hinttype`" rule select the same 3,156 rows, and no row's authored type
  differs from its class-forced type.
- The port applies the rule at the def level (`Substrate/ElysiumNodeEntity`): a hint row's
  classname becomes `ai_hint` before construction, the authored one kept on
  `FElysiumEntityDef::SourceClassname`. Rows that make no hint keep today's record-only path;
  their removal, and the network half of the loader above, are 0018 story 4's.
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

**Still unresolved** (closed 2026-09-19 in the sections below: the install's graph — § "Which
graph the patched install runs on"; endpoint binding and the link predicate; the route gates;
the alternate route's cost — § "The node searches and the pedestrian cost"): which packed/cached/rebuilt network the selected patched install uses
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

### The loader, walked: load or rebuild (2026-09-19, 0018 story 4)

_Two independent opencode walks and an adjudication pass; the decision function `0x102f67a0`, the
think `0x102f6a50` and the `NPCThink` gate re-read from the listing here._

**When.** `CWorld::Precache` (`0x1023c020`) calls `0x102f6690`, which creates the entity
`ai_network` (named `BigNet`), publishes it at `DAT_10934088` and its node vector
(`mgr+0x458`: count, then the node-pointer array) at `DAT_1093407c`, decides, loads if told to,
THEN zeroes the running node counter `DAT_10926a3c`, and arms the manager's think `0x102f6a50` at
`curtime + 0.8` (`0x104491a8`, a double). All of this precedes every other BSP entity's spawn, so
`CNodeEnt::Spawn` already knows which branch it is on.

**The decision `0x102f67a0(mapname)`.** It builds `"maps/<map>.bsp"` and
`"maps/graphs/<map>.ain"` and hands both to the ENGINE's file-time compare
(`VEngineServer` `+0x170`, slot 92 → engine `0x2003da20` → comparator `0x200fa010`), which answers
`out = -1 / 0 / +1` for the BSP older than / same age as / newer than the AIN.
`102f689f TEST EAX,EAX / JLE` : **`out <= 0` loads** — an equal stamp loads. `out > 0` loads only
when `ai_norebuildgraph` (`0x10934090`, default `"0"`, flags 0) is non-zero, printing
`.AIN File will *NOT* be updated. User Override.`; otherwise `DevMsg(2, ".AIN File will be
updated")` and rebuild. There is **no checksum, CRC or map-revision test anywhere**: the file's
age is the whole freshness rule. One arm the walk above leaves out (review, 2026-09-19): when the
engine's compare call itself answers 0 — it could not stamp one of the two files, a missing AIN
among them — the function falls straight to "rebuild" with no message (`102f6897 TEST EAX,EAX /
JZ`).

**The load `0x102f5bd0`.** Skipped outright in map-edit mode (engine slot 12). It opens
`"maps/graphs/<map>.ain"` through `VFileSystem005` `Open(path, "r", pathID NULL)`; whether that
resolves to a loose file or a packed one is the engine filesystem's search order, not a choice
made in `vampire.dll`. The file is TEXT: `Version 30`, `NumHulls: 22` (either mismatch aborts
with a DevWarning), `UsedHullBits:` (OR-ed into `DAT_10610be8`), `ZoneCount:` and one integer per
zone — the number of nodes IN that zone (`mgr+0x644`; ids 0–3 reserved, isolated nodes are zone
1, flood-filled zones start at 4) — `NumNodes:`, `Nodes:`, `TotalNumLinks`, `WCLookup:`. Per node:
position, yaw, 22 hull offsets, type (`+0x70`), flags (`+0x74`), the node's NEIGHBOUR BITVECTOR —
`(NumNodes + 31) >> 5` words, into `+0x90` — the zone id (`+0x94`; 0 aborts the load with
`Invalid node zone`) and the link count, read and discarded. **That bitvector is the
"variable-width node tail" of the exports: 116 nodes give 4 words + 2 = 6 integers, 578 give
19 + 2 = 21.** Per link: source, destination, info (`+0x64`), 22 hull-motion words (`+0xc…`);
each link is added to BOTH endpoints' adjacency in file order, and the saver emits links
node-major in each node's own adjacency order, so file order IS install order. An endpoint out of
range bumps `DAT_106c994c` and adds to a null node. Node cap 1500. Any abort frees the nodes and
leaves `DAT_1093408c` 0, which means "rebuild". Success sets `DAT_1093408c = 1`.

**Rebuild.** With `DAT_1093408c` clear, `CNodeEnt::Spawn` takes its live branch and creates a
node per authoring entity. The first think prints **`Node Graph out of Date. Rebuilding...`** — a
plain `DevMsg`, visible at `developer 1` — and re-arms at `curtime + 1.0`; the second runs
`0x102f6610`: every node's links cleared, `InitLinks 0x102fb4e0` (ONLY here — a loaded graph is
never re-linked), zones recomputed (`0x102f49c0`), the graph SAVED to
`"maps/graphs/<map>.ain"` (`"w+"`, pathID NULL, link info masked `& 0x20f0`), and
`DAT_1093408c = 1`. Either way the think then sends `ai_node_graph_built`, applies the
`info_node_link` states (`0x102cc900`), frees the WC-id table, and sets `mgr+0x658`.
**`CAI_BaseNPC::NPCThink` (`0x1026ca80`) does nothing until `mgr+0x658` is set**: no NPC thinks
for the first 0.8 s of a map on a loaded graph, 1.8 s on a rebuilt one.

**The loaded branch of `CNodeEnt::Spawn`**, for the hint association: the hint is created with
`m_nNodeID` = the running counter; it is attached (`node+0xa0 = hint`) iff `0 <= counter <
NumNodes`; otherwise `DAT_106c994c++` and the hint keeps the out-of-range id. The counter
advances once per node-typed authoring entity in BSP spawn order either way. So the pairing of
BSP node rows to AIN nodes is positional, and it is only right when the AIN was built from that
BSP.

### Endpoint binding and the link predicate, walked (2026-09-19; engine record, 0018 story 5)

_Two independent opencode walks, diffed; every disputed compare below was then decoded here from
the listing or the DLL bytes (`0x102ff960`, `0x102fce80`, `0x102f32f0`, its two comparators, the
fraction test in `0x102f39a0`). Both walks misread x87 compares in places; the rule used: after
`FCOM`, `TEST AH,5 / JNP` fires only on strictly-less, `TEST AH,0x44 / JP` falls through only on
equal, `AND EAX,0x4100 / JZ` fires only on strictly-greater._

**`NearestNodeToNPCAtPoint 0x102f3c10(npc, pos)`** on the network (`+0` node count, `+4`
node-pointer array); answers a node id or `-1`. No RNG anywhere.

1. Empty network → `-1`.
2. Cache `0x102f4520`: 20 entries of `{pos, time, node, tag}` at `network+8`, ring cursor
   `+0x1e8`. An entry hits when `time + 5.0 >= curtime`, `tag == 0x17` and the 3-D distance is
   STRICTLY under 24.0. But this function stores its own results with `tag = hull` and only on a
   MISS (`-1`), while the tag-`0x17` entries come from the point search `0x102f41b0`; so for an
   NPC lookup the cache can only replay a point-search result, which is then re-validated (type
   3 needs `caps & 4`, type 2 needs `caps & 1`, not rejected, `CanFitAtNode`) with NO line trace.
3. Otherwise the box search. Half extents `(800, 800, 200)` for a ground mover, `2048` cubed when
   `CapabilitiesGet() & 4` (fly). `ListNodesInBox 0x102f32f0` scans ids `0..count-1`, admits a
   node whose filter passes (the same type/capability/rejected tests) and whose origin is inside
   the box INCLUSIVE on all six faces, keys it by squared 3-D distance to its hull position, and
   keeps the **ten nearest**: its working heap orders by `0x102f37a0` (root = farthest), and once
   full a newcomer is admitted only when STRICTLY nearer than the farthest, which it evicts. The
   result is drained into the caller's heap (`0x102f3770`, root = nearest) and consumed nearest
   first.
4. Per candidate, in order: `CanFitAtNode 0x102f1900(nav, id, 0x2400b)` — the stand test
   `0x102e7270` for ground nodes, then a hull-fit trace at the node; not rejected
   (`0x1027db30`); then the connection trace `0x102f3900` → `0x102f39a0`: a RAY from `pos` to the
   node's hull position plus the NPC's view offset, mask `0x202400b`, a filter that lets NPCs
   through but flags that it crossed one; success is `fraction == 1.0` EXACTLY. A candidate that
   passes without crossing an NPC is returned at once; the first that passes WITH one is kept as
   the fallback and returned if nothing better follows.
5. Nothing passes → the miss is cached and `-1` returned.

`GetPosition 0x102fb0d0`: type 2 adds the hull's Z offset `node+0x14+4*hull`; type 4 (climb)
offsets along the node yaw by `hullSpan * 0.5 + 8.0` (`0x10449270`, `0x1049a148`, both doubles);
else the origin.

**There is no failed-node list.** "Rejected" is slot 527 (`+0x83c`) through `0x1027db30`: the
base body answers 0; the Troika body `0x10293e80` rejects a node whose hint (`node+0xa0`) this
NPC may not take — `0x102d1540`: owned by another live entity, or `curtime < m_flNextUseTime`.
So a claimed or cooling-down hint removes its NODE from endpoint binding and from every link walk.

**`IsConnected 0x102f48b0(a, b)`**: an id `> count` (signed — `== count` slips through) DevMsgs
and answers 0; equal ids 1; either zone (`node+0x94`) `== 1` → 0; either `== 3` → 1; else
`zoneA == zoneB`. Hull is not consulted. Zone 3 is the rebuild's marker for nodes flagged
`node+0x74 & 0x20000000` and their surroundings.

**The link predicate `0x102ff960(link, fromNode)`**, on the pathfinder `{+4 npc, +8 hull, +0x14
last stale-probe time}`; link `{+0 EHANDLE, +4/+8 ids, +0xc+4*hull motion, +0x64 info, +0x68
stale expiry}`. In order:

1. `info & 0x1000` (link off; written only by `info_node_link`, `0x102ccce0`) → refuse.
2. `move = CapabilitiesGet() & link[0xc + 4*hull]`; zero → refuse.
3. The FAR node rejected (`0x1027db30`) → refuse.
4. `move == 2` EXACTLY (`102ff9c8 CMP [ESP+0x10],2`) → `IsJumpLegal` (slot 521 → `0x10280790`
   with 80.0 up, 250.0 down, 160.0 across, each `+ 0.1`; apex rise `<= 80 × 1.25`) from this
   node's hull position to the far node's; illegal → refuse. A link the NPC may ALSO walk
   (`move == 3`) skips the jump test entirely.
5. `info & 1` (stale) clear → accept. Set → `0x102fce80`: `curtime > link+0x68` clears the bit
   and accepts; else, if this pathfinder already probed at this exact `curtime`, refuse without
   probing; else stamp the time and re-probe the segment (`0x10304a40`, mask `0x202400b`, arms by
   motion bit in the order 1, 4, 2, 8): a clear probe clears the bit and accepts. A refusal with a
   live `link+0` door handle calls the door-blocked notice `0x1027de00(npc, door)`.

Stale links are written by `0x102f1fa0(nav, seconds, ent)` — `info |= 1`, `+0x68 = curtime +
seconds`, `+0` = the blocker — from the door-blocked notice (5.0 or 20.0 s, `senses.md`) and from
`CAI_Navigator::Move 0x102eff40` (4.0 s, no entity); it needs `nav+0x50`
(`m_fRememberStaleNodes`) and a live path.

Callers. `0x102ff960`: `0x102fd240`, `0x102fe150`, `0x102fe9f0`, `0x102fef30`, `0x102ff3e0`,
`0x102ffca0`, `0x10300140`, `0x10300b50`, `0x10301010`, `0x10306700`, `0x10306f60`.
`0x102f3c10`: `0x10275760`, `0x102ecd20`, `0x102ed430`, `0x102ee970`, `0x102fdcc0`,
`0x102ffca0`, `0x10300140`, `0x103008f0`, `0x10301010`, `0x10301720`, `0x10302320`,
`0x10302e50`, `0x10304e00`, `0x10306700`, `0x10306f60`, `0x103d0ad0`, `0x103dfd80`.

**Closed since (2026-09-19):** the stand test's constants (§ "The back-away and shoot-node
searches": up 0.1, down slot 523, the 0.75 / 0.25 foot box) and the hull table (§ "What the
shipped graphs and maps actually use": hull 0 is 26 wide and 72 tall).

**The tie order in the nearest-node queues (2026-09-19; two opencode walks diffed, the sift
tests re-read from the DLL, the orders replayed by `heap_tie_sim.py`).** Both queues are the
SDK's `CUtlPriorityQueue` over 8-byte `{float key, int id}` rows. The key is `(dz² + dx²) +
dy²` in extended precision (`102f4159`…`102f4178`), stored as a float32. **Insert** appends at
the tail and sifts up (guard `102f34ce TEST EDI,EDI` / `102f34db JE` for index 0; parent
`(i+1)/2 − 1`; `cmp(child, parent)` and `102f34fb TEST AL,AL / JNE` STOPS) — and both
comparators are strict (`0x102f37a0` answers 1 only on `child < parent`, `0x102f3770` only on
`child > parent`; `AND EAX,0x4100 / JNE`), so **an EQUAL key swaps up past its parent**.
**Remove-head** (`0x102f9000`) moves the last row to the root and sifts down while `i <
count/2`: left child first, then right against the running best, each replacing it only on
a strict answer (`102f3eb4`…`102f3edd`) — **an equal child never displaces its parent**. The
drain pops the working heap (farthest first) into the caller's heap one row at a time.
The order in which `0x102f3c10` then TESTS `n` candidates of exactly equal key (letters = scan
order, i.e. ascending node id) is therefore fixed but not monotonic: 2 → A B; 3 → A B C; 4 → C
D A B; 5 → D A E C B; 10 → I D A J E C G H B F. With more than ten, the eleventh and later
equal keys are refused by the strict admission, so the ten LOWEST ids survive and come out in
the 10-row order. One walk derived C A B D for four; replaying its own stated mechanics gives
C D A B.

**Unrecovered:** nothing in this section. (`0x10304a40` and `0x103059d0` are walked in § "The
route gates" below.)

### The route gates, walked — `SetGoal` to the two builders (2026-09-19, 0018 story 5)

_Two independent opencode walks, diffed, and an adjudication pass; the gate in `0x102f2060` and
the near-goal and partial-accept compares in `0x10303850` re-read from the DLL here (the
adjudicator decoded both of the latter backwards)._

**`SetGoal 0x102ecd20(goal*, byte flags)`.** No dead, no-navigator or same-goal pre-check, and no
RNG anywhere in the chain. Argument bits are an else-if: `& 1` resets the navigator
(`0x102f28a0`); otherwise `& 2` clears the path's target and target offset; `& 4` resets on a
failed find. `goal+0x14 != -1` sets the path activity. Tolerance `goal+0x20`: `-2.0` takes the
hull default (`0x102d61b0`, the hull's lateral extent — 26 for `HUMAN_HULL`, whose row
`staticinit_102d4440` fills as `(-13,-13,0)..(13,13,72)`); `-1.0` takes it ONLY when the path's
tolerance is still 0.0, and otherwise leaves the old value; for goal types 1 / 2 / 7 with a
target NPC it becomes half the sum of the two hulls; any other value is copied. The waypoint
tolerance is always half the hull. "The goal carries a position" means any component of
`goal+4..+0xc` differs from the sentinel `(FLT_MAX ×3)` (`staticinit_102ec960`); else the node id
at `goal+0x10` supplies it.

Goal-flags `goal+0x24`: **`& 2` is the node arm** — both ends bound with `0x102f3c10` /
`0x102f41b0`, the primary search `0x102fd240` run directly, any miss answers false; it never
tries a local route and never reaches `0x102f1dc0`. `& 8` is computed and handed down but
`DoFindPath` never reads it. `& 1` primes the motor on success.

**`0x102f1dc0`** wraps the find in a retry window: on a failed `DoFindPath`, `nav+0x40 == 0.0`
fails at once through `nav` slot 10 `(0xc, 1)` — the `0x0c` route failure — otherwise memory bit
`0x20` is set with a deadline (`+0x48`) and a next-try time (`+0x4c`); later calls inside the
window answer false without searching, a success clears the bit, and passing the deadline raises
the same `0x0c`.

**`DoFindPath 0x102f2330`** switches on the goal type (`path+0x5c`): 1 the target entity's
origin; 2 the enemy's last known position from the enemy memory; **3 the goal-entity chain**
(`m_pGoalEnt +0x5de8`, up to `0x80` waypoints laid directly, the last flagged `|= 8` only when
the chain ended under that cap) — it returns WITHOUT building a route; 4, 5, 6, 9 nothing; 7 the
`[npc+0x98]` object's slot `0x928`; **8 sets `path+1 = 1`** and nothing else; any other type
fails. All but 3 then call `0x102f2060`.

**`0x102f2060`, the two builders** (`102f20be`…`102f218d`):

1. `m_navType (nav+0x18) == 3` (climbing) → node route only.
2. Move mask from `CapabilitiesGet()`: `& 4` or `& 0x10` → `0x32`; else `& 1` → `0x131`, or
   `0x135` with `& 2`; else 0.
3. **The local attempt runs iff `path+1 == 0` AND `path+8` (`m_flExtrapolationTime`) `== 0.0`**:
   `BuildLocalRoute 0x10304130(start, goal, target, 8, node -1, mask, tolerance)`. A route
   answers and is installed; the graph is never consulted.
4. Otherwise, or when that answers NULL: `BuildNodeRoute 0x10304e00(start, goal, tolerance,
   ped = path+1, penalty = path+4, &path+0x14, extrap = path+8)`. NULL → false.
5. On success the route is installed and, when its second waypoint shares the navigator's
   type, the head is trimmed if it is further than 0.1 yet within half a hull.

**`BuildLocalRoute`** dispatches on the mask: `& 1` ground (`0x10303850`, mode 0); `& 2` mode 2;
and only with a node argument, `& 4 && caps & 2` jump (node type 2) and `& 8 && caps & 8` climb
(node type 4 — `info_node_climb`; `info_node_air` is 3). A zero mask answers NULL untraced.
The ground worker `0x10303850`:

- length `< 0.0625` (`0x10451f78`, a double; `103038dc JP` probes on `>=`) → one waypoint at the
  goal with NO probe;
- else the move probe (`CAI_MoveProbe` `[npc+0x5d40]`, `0x102e6d70`; the pushed 100.0 is
  `MoveLimit`'s PERCENT argument — the whole segment — not a distance; corrected 2026-09-20)
  with contents mask `0x2400b`, plus MONSTER when move-mask bit 6 is CLEAR (`10303958 SHL
  ESI,0x13` on `~mask & 0x40` = `0x2000000`) — so `0x202400b`, NPCs included, for the `0x32` /
  `0x131` masks of step 2; the adjudicator's `0x800000` was a shift miscounted;
- a clear probe → one waypoint at the goal;
- a blocked probe is still accepted when mask `& 0x100`, the flag word `& 8`, the probe's
  remaining distance `<=` the tolerance, and `|goal.z − end.z| < 2.0` (`0x10449400`, a double):
  the waypoint is laid at the probe's END position (`103039dc`…`10303a02`);
- mask `& 0x20` → the two-waypoint gap route `0x10304020`;
- the blocked-by-NPC arm: probe code `-3` with mask `& 0x10` re-probes with `0x2400b` and
  accepts a waypoint at the goal when `0x10303fd0` passes — the BLOCKING NPC's `IRelationType`
  (slot 404, `+0x650`) toward the mover is 3, `D_LI`: a friendly NPC in the way does not refuse
  the route;
- else NULL. The probe's failure classifier `0x102e2d70` answers `-3` when the blocker carries an
  NPC at `entity+0x94` — the SDK's `AIMR_BLOCKED_NPC`, not a door — `-1` for another entity, else
  `-2` (world).

**`BuildNodeRoute`**: `extrap > 0.0` → `0x103055b0` (a predicted goal, then itself with no
pedestrian byte); empty network → NULL; both ends bound (`-1` → NULL); `IsConnected` false →
NULL; entry leg `BuildLocalRoute(start, nodePos, …, 0xc, node, 0x60 | bits)` — or a bare waypoint
when already exactly on the node — and exit leg `(nodePos, goal, …, 8, -1, 0x160 | bits)`, the
bits from `0x10300700` (`0x11` for `caps & 1`, `0x15` with `& 2`, `| 8` with `& 8`, `0x12` for
`& 4`); same node at both ends → the two legs joined; else the primary or alternate search
(below) between them; any NULL frees what was built.

**What can succeed with no graph at all:** type 3 always; types 1, 2, 4, 5, 6, 7, 9 through the
local attempt — not climbing, a non-zero mask, `path+1 == 0`, no extrapolation, a passing
probe. **What cannot:** type 8 (and every goal after it until a navigator reset, below), the
`goal+0x24 & 2` node arm, and any goal with extrapolation. So the patrol point's type 4 walks a
clear straight line with no node coverage, while the interesting-place walk always needs the
graph — which is what makes `0x0c` on an uncovered place a retail outcome, not a port defect.

**The route helpers (2026-09-19; two opencode walks diffed; the arm table, the status codes,
the step record and the disputed exits re-read from the DLL).**

- **`MoveLimit 0x102e6d70`** (`RET 0x28`) switches on the nav type through the table
  `0x102e6f98`: **0 ground → `0x102e5d80`, 1 jump → `0x102e6290`, 2 fly → `0x102e6090`, 3 climb
  → `0x102e6be0`** (which ignores the mask argument and hardcodes `0x202400b`); anything else
  writes status `-4`. There is no crawl arm. Status word `trace[0]`: `0` clear, `-1` blocked
  by an entity, `-2` by the world (also a floor or final-z failure), `-3` by an NPC (the
  blocker's `+0x94` set), `-4` illegal (`102e636c MOV [EAX],0xfffffffc` — the jump arm's
  refusal; one walk's "−NaN" was a decompile artifact). Every arm answers `status >= 0`.
- **The ground test `0x102e4f50`** fills the SDK's step record from the NPC: step height =
  slot 522 (`+0x828`, 18.0), **step-down = slot 523 (`+0x82c`, 36.0 on the Troika line)** —
  which settles that slot's name: it is the step-down height the stand probe also uses, not a
  jump speed — and minimum landing = hull width × 0.3333 (`0x1049d8e0`, a double). Segments
  of **16.0**, stop under **0.001** (both doubles, strict), no step cap; the final z tolerance
  is `max(hull height × 0.5, StepHeight + 0.1)` (`102e565b`…`102e56aa`; 36 on hull 0) and the
  move fails only on `|Δz| >` it, strictly. Flag 4 skips the final z check. Flag 8 runs the stand probe
  `0x102e7270` at the start and at every step, but its answer only picks a debug draw.
- **`0x10304a40` — the stale-link re-probe** (from the predicate, through `0x102fce80`):
  the link's motion bits are tried in the order ground (bit 0), fly (bit 2), jump (bit 1),
  climb (bit 3); ground and fly through `0x103048d0`, jump and climb through a bare
  `MoveLimit` (mask `0x202400b`, 100 %); any arm clear → 1. `0x103048d0`: `MoveLimit`; else
  **`Triangulate 0x103059d0`** with tolerance `|end − start| − trace[9]` (`1030495c FSUB
  [ESP+0x44]`, the record being `[ESP+0x1c]` with one push pending); else, only when the
  blocker is an NPC, `MoveLimit` again with mask **`0x2400b`** — NPCs not solid.
- **`Triangulate`** — arithmetic in § "Triangulate, the ground accounting and the jump arm"
  below: a far-leg clamp at 384.0, then candidates beside a base point near the START of the
  segment, two passes, each accepted when `MoveLimit(start → cand)` and `MoveLimit(cand →
  ref)` are both clear.
- **`0x10304020` — the two-waypoint detour** (`RET 0x28`): `Triangulate` or NULL; then two
  0x38-byte waypoints — the detour point (flags **1**, `bits_WP_TO_DETOUR`, node id `-1`)
  linked in front of the goal waypoint. Callers: the local route builder under goal flag
  `0x20` after a blocked probe, and `PrependLocalAvoidance 0x102ede30`.
- **`0x103055b0` — the extrapolated route** (taken when `extrap > 0.0`): `0x10300140` walks
  the graph from the goal's nearest node along the goal's velocity for `speed × time`, always
  taking the link with the greatest projection (one walk: strict `>`, so the first link wins
  a tie); speed under 0.01 or a dead
  end answers the point reached; **no nodes or no nearest node answers FALSE with `out =
  goal`** (`1030018b XOR AL,AL`) and the branch returns no route. Then `BuildNodeRoute` to
  the predicted point with `extrap` 0.

**Triangulate, the ground accounting and the jump arm (2026-09-19; three more opencode A/B
pairs diffed, every disputed instruction re-read from the DLL).**

- **`Triangulate 0x103059d0(navType, start*, end*, tolerance, target, out*, mask, gate)`**
  (`RET 0x20`); `gate != 0` answers 0 at once. `û = normalize(end − start)`, `len` kept.
  **Clamp:** not flying and `len > 384.0` STRICTLY (`10305a51`…`10305a62`, `AND 0x4100 / JNE`
  skips): `ref = û × 384` — **`start` is never added** (`10305a68`…`10305a80`; the SDK's line,
  bug included), `len := 384`, and `MoveLimit(navType, ref → end, 0x202400b` literal`, target,
  100 %)` must be clear or the answer is 0; otherwise `ref = end`. **Perpendicular:** `1 −
  |û.z| <= 0.001` (doubles, INCLUSIVE, `10305b15`…`10305b2e`) → `perp = (1,0,0)` and the
  vertical axis is Y; else `perp = (û.y, −û.x, 0)` (`û × ẑ`, NOT normalised) and the vertical
  axis is Z. `halfW` / `halfH` = half the hull's full width / height (hull table). **Base
  point:** `t = min(halfW + tolerance, len)`, `base = start + t·û` — beside the mover, not at
  the segment's middle. **Candidates:** `cand0 = base − perp·halfW`, `cand1 = base +
  perp·halfW` (all three components × `halfW`, `10305c02`…`10305c37`; one walk read the third
  as `× t`), and when flying `cand2/3 = base ∓ vertical·(3·halfH)`. **Order:** two passes,
  each from the HIGHEST index down (`10305d7f`…`10305eab`): flying up-side, flying down-side,
  then `+perp` — the RIGHT of travel — then `−perp`. After a failed candidate its point
  steps `2·halfW` further out (the vertical pair `3·halfH`), so the horizontal probes are at
  `±1` then `±3` half widths. **The pull-back** (`10305dde`…`10305e56`, only when leg 1 was
  blocked, `trace[0] < 0`): `dot = (blockedPos − start)·û`; `new = min(dot, tArr[i])` — `dot`
  replaces the candidate's along-track distance only when STRICTLY less (`TEST AH,5 / JP`
  keeps the old) — and the candidate is moved `(new − old)·û`, i.e. BACK toward the start to
  where leg 1 stopped. One walk read this as a `max` that pushes forward; it is a `min`.
  Success writes `*out = cand` and answers 1; two failed passes answer 0. No RNG.
- **The ground test's distance accounting.** `seg = min(total2D − walked, 16.0)` (16 only when
  the remainder is strictly greater); `seg < 0.001` ends the loop. `walked` is updated AFTER
  the step test: a clean step adds the REQUESTED `seg` (`102e53da`), a blocked step adds the
  ACHIEVED 2-D distance `|cur − prev|` (`102e53eb`…`102e541e`). A blocked move then writes
  `trace+0x24 = total2D − walked` (distance left), `+0x04` = the position reached, `+0x1c` =
  the blocker, `+0x10` = the hit normal, and `+0x00 = 0x102e2d70(blocker)`: `-3` when the
  blocker's `+0x94` is set, else `-1` / `-2` as the engine call `+0x8c` on its `+0x2e0` answers
  non-zero / zero. A missing floor arrives as a blocked step with zero achieved distance and
  the world as blocker; the final-z failure writes `-2` and `+0x24` = the 2-D distance from
  the end point to the destination. **The percent argument never truncates the walk** — under
  0.001 or flag 1 it is 0, over 99.999 it is 100; its one use clears a step-context byte once
  `walked − pct·total > 0.001` — and the final-z check runs at every percent.
- **The prediction walk's interpolation — `0x103000a0(a*, b*, num, den, out*)`** (`RET
  0x14`): `out = a + (b − a) × (num / den)`, floats, no clamp (one walk had `b` as the out
  argument). `0x10300140`: `budget = time × |velocity|`; `proj = normalize(node − goal) ·
  velocity` (`0x10300010` normalises; one walk missed it). `proj >= 0`: `d = |goal → node|`;
  `d >= budget` (inclusive) → `out = lerp(goal, node, budget / d)` and done; else `budget −=
  d`. **`proj < 0`: `budget += 2·d`** (`10300280 FADD ST0,ST0`). Then node to node: best
  neighbour by greatest projection (`best = −1`, `bestProj = 0.0`, strict — a neighbour that
  does not lie forward is never taken); link length `>= budget` → `lerp(cur, best, budget /
  length)`; else subtract and continue.
- **The jump arm `0x102e6290`** (`RET 0x18`): both ends dropped to the floor (`0x102e7ba0`,
  StepHeight up, 720 down); `IsJumpLegal(start, end, end)`; the stand probe at the landing;
  equal floors or a zero 2-D distance → status `-4`, `trace+0x24 = |end − start|`. Gravity =
  the ConVar at `0x109ef2d4` (float `+0x28`; `sv_gravity` by use) `× m_flGravity (+0x3ec)`,
  `1e-7` when zero. **Slot 524 (`+0x830`, 350.0) is the max
  horizontal jump SPEED** — it is the sixth argument of `0x102e7060` and divides the distance
  there — which settles that slot's name by use.
  **`0x102e7060` is `CalcJumpLaunchVelocity`, pure arithmetic, not a stepper** (`RET 0x1c`,
  returns its out pointer; its only callers are the three sites in the jump arm, thunk
  `0x100020a4`): `minH = ½·g·(½·dist2D / speed)²`; `*h = max(*h, minH)` (`102e70d6`…`102e70e1`;
  one walk read a `min`); `*h = max(*h, Δz)`; `tUp = √(2h/g)`, `T = tUp + √(2|h − Δz|/g)`, `v
  = (dir2D · dist2D / T, √(2gh))`, apex point = `start + v.xy·tUp + (0,0,h)`.
  **The arc walk is inline in `0x102e6290`**: `IsJumpLegal(start, apex, end)` again, then from
  the START floor point (`102e6568`…`102e6583`; one walk had the landing's z) `dt = 0.1·T`,
  `next = cur + (v.x, v.y, v.z − ½·g·dt)·dt`, `v.z −= g·dt`, while `t < T − 0.01` (strict) —
  up to ten hull traces (the NPC's collision bounds, the caller's mask, `CTraceFilterNav`). A
  segment is blocked on the solid byte `+0x37` or `fraction < 0.99` strictly (`0x10462968`);
  a hit on ONE exempt entity counts as clear — the SDK's player-vehicle rule: player 1
  (`0x101cd9e0(1)`), its cached `CBasePlayer` pointer (`+0xa8`), the byte `m_bInVehicle`
  (`+0x20f4`) set, and the blocker equal to `(+0x20fc)->vfunc 1()`, the vehicle's entity
  (`102e5ea4`…`102e5eda` in the ground arm `0x102e5d80`, which has the same exemption).
  **The apex bisection** (`102e67b0`…`102e680e`, `102e691f`…`102e694c`): `H` starts at the
  minimum apex `Hmin`, `step` at 1024, `best` at the 1024 sentinel. Clear arc → `best = H`,
  `step /= 2`, `H −= step` — it looks for a LOWER clear apex (one walk had a clear arc go
  straight to the verdict); an apex `IsJumpLegal` refuses lowers `H` the same way without
  touching `best`. Blocked, hit normal `z < 0` (a ceiling) → `step /= 2`, `H −=
  step`; any other hit → `step /= 2`, `H += step`. Stop when `H <= Hmin`, `H > 1024`, or
  `step < 16`. Verdict: `best != 1024` → launch velocity recomputed for `best` into
  `trace+0x28`, status left clear; else the last blocker, status `0x102e2d70(blocker)`,
  `+0x24 = |landing − cur|`.

**Unrecovered:** nothing in this section.

### The node searches and the pedestrian cost — `0x102fd240`, `0x102fe9f0` (2026-09-19, 0018 story 5)

_Two independent opencode walks, diffed (their x87 reads agree here); the draw, the multiplier and
`InitLinks`' writer re-read from the DLL, and the link bit censused over the installed graphs
(`$ELYSIUM_WORK_ROOT/opencode/re18/ain_linkinfo_census.py`)._

**The primary search `0x102fd240(start, goal)`** is A* over four per-node arrays (`g` from
`FLT_MAX`, `parent` from `-1`, `h`, `f`) and two bit vectors. Seed `h = dist(start, goal) × 0.1`
(`0x104493d0`, a double) — but every later `h` is the plain distance. The pop is a LINEAR scan
for the smallest `f`, strict `<`, so the lowest node id wins a tie; popping the goal ends the
search before expansion. Expansion walks the node's links in adjacency order through the
predicate `0x102ff960` (above); the edge cost `0x102f1860` is the distance between the two
hull-adjusted positions, DOUBLED when the usable motion is exactly jump (2) or climb (8); a
neighbour is relaxed only on a strictly smaller `g`, and may be re-opened. No iteration cap, no
RNG. The route is one waypoint per node, start to goal, flags 4, at `GetPosition(node, hull)`,
typed by the link motion (1→0 ground, 2→1 jump, 4→2, 8→3); only the entry and exit legs trace
geometry, and there is no shortcutting pass over the node list.

**The alternate search `0x102fe9f0(start, goal, ped, brightPenalty)`** has ONE caller,
`BuildNodeRoute`, which takes it whenever its fourth argument — the path's pedestrian byte
`path+1` — is non-zero, passing `path+4` (`m_iBrightRoutePenalty`) as the penalty. It is the same
search with three differences (`102feb59`…`102feb6f`, `102fecb6`…`102fece1`):

1. with `ped` set, **one** `RandomInt(5, 10)` is drawn per call, before the first pop;
2. the edge COST of a link whose `info & 0x2000` is set is multiplied by that integer
   (`FIMUL`), before `g` is added — not `g`, `h` or `f`;
3. a positive `brightPenalty` is ADDED to every relaxed edge; zero or negative does nothing.

Its route builder `0x102fcd00` flags a waypoint `0x24`, and the NEXT waypoint on the route `|=
0x20`, when both sit on nodes whose hint is type **11000**, `info_node_crosswalk`. (Corrected
2026-09-20: the chain is walked goal → start and each new waypoint is PREPENDED — `102fcdf5 MOV
[EAX+0x30],EDI` makes the one built before it its `pNext` — so the `102fcd98 OR AL,0x20` on
that earlier-built waypoint lands on the one that FOLLOWS the new one in route order; an
earlier pass said "the previous one".) (`4` is
the SDK's `bits_WP_TO_NODE`, which both builders put on every node waypoint — `102fcc41 PUSH 4`
in the primary; the crosswalk pair's own mark is `0x20`, `bits_WP_DONT_SIMPLIFY`.) The only
reach is goal type 8 — task `0xa5` `TASK_GET_PATH_TO_INTERESTING_PLACE` (Troika `StartTask` case
`0x31`, `TaskFail(0x22)` with no place, `TaskFail(0xc)` on a refused goal) — whose struct carries
penalty 0, so in shipped data the alternate search is "the primary with the `0x2000` multiplier".

**What `0x2000` is.** Not a door mark and not route admission. In `InitLinks`' ground arm, for
hull 0 only, a second walk test runs between the two node positions with contents mask `0x2000`
alone (`102fbbaa PUSH 0x2000` / `CALL 0x100041ba`); a non-zero answer sets `link+0x64 |= 0x2000`
(`102fbf27 OR AH,0x20`). `0x2000` is a VtMB brush content bit. Witness, from the installed
files: `sp_tutorial_1` has NO brush carrying it and **0 of 429** links flagged; `sm_hub_1` has
133 such brushes (93 `PLAYERCLIP|MONSTERCLIP|0x2000`, 31 `MONSTERCLIP|0x2000`, 9 `0x2000` alone)
and **461 of 1,862** links flagged (450 of 1,856 in the unpatched graph). So the mappers paint
volumes, and every pedestrian route pays 5–10× to walk a link that crosses one, each walk with
its own draw. The volumes that flag links are the 9 clip-free ones, the hub's roadway slabs
(placed against the graph below, § "The `0x2000`-only brushes"). The saver keeps the bit (`info & 0x20f0`), so it is DATA in the loaded graph. The port
takes the BRUSHES, not the link flag (0018 § Navigation boundary, 2026-09-20): the volumes
become a priced nav area and the link records are not kept at run time.

**The pedestrian byte lasts as long as the schedule, not longer** (corrected 2026-09-19, review
against the listing; the earlier text had it sticking across goals). `path+1` is written 1 only
by `DoFindPath` case 8 and cleared by the navigator reset `0x102f28a0` — but that reset has a
third caller besides `SetGoal`'s two arms: `0x102ee270` (the SDK's `ClearGoal`: the reset, then
navigator slot 7), reached from 16 functions, among them `SetSchedule 0x10280e50` and the Troika
`OnScheduleChange 0x102a0940`, which clears the goal on every schedule change unless
`PRESERVE_PATH` stands or the navigator is mid-jump or mid-climb (`npc-ai/programs.md`
§ "Patrol paths, walked"). So a later goal INSIDE the interesting-place walk's schedule is
routed as a pedestrian, and the next schedule starts clean — consistent with "the only reach
is goal type 8" above.

**The Hammer-side name (2026-09-19): `tools/toolspedclip`, `%compilewanderclip 1`**
(`materials/tools/toolspedclip.vmt`, `pack001.vpk`) — Troika's "ped clip". A census of every
`%compile*` key in the shipped materials finds exactly four that the SDK does not have:
`wanderclip`, `npcopaque` (contents `0x800000`, witnessed by texture, below in the cover
section), `shadowonly` and `playercontrolclip`. The tie of `wanderclip` to bit `0x2000` is an
INFERENCE by elimination and by name: like every clip brush (`0x10000` 3,730 of 3,775,
`0x20000` 4,099 of 4,145) the `0x2000` brushes reach the BSP with their sides' texinfo stripped
(2,191 of 2,192), so no texture survives to witness it (`contents_texture_census.py`).

**The sibling searches (2026-09-19; two opencode walks diffed, the disputed points and every
exit re-read from the DLL).** All four are the same A* — open and seen bitsets, `f = g + h`,
the pop `0x102f3270` a linear scan of ids 0 … n−1 with a strict `<` (so the LOWEST node id wins
a tie), neighbours in plain adjacency order through the full predicate `0x102ff960`, relax only
on a strictly smaller `g`, no iteration cap, no RNG, no cooldown or hint write.

- **`0x102fe150` — "is there a route", a bool.** Entry `0x102ee380` → `0x102fdcc0` (both
  points bound with `0x102f3c10`; an unbound one answers false). `h` and the edge cost are the
  cheap length `max + 0.25 · others` (`0x1044bef8`), the seed `h` × 0.1 (`0x104493d0`, a
  double); no jump doubling, no cost hook. Goal test at the pop. Two callers, neither a task:
  `CNPC_VGargoyle::SelectSchedule` (`103789b3`) and `CNPC_VWerewolf::HasPath` (`103d0e29`).
- **`0x102fef30` — dead code.** The primary search's body with an `int* out` for the goal
  node. Neither its address nor its one thunk `0x1000a006` is referenced anywhere in the
  image (byte scan of the whole file).
- **`0x10301010` — `FindBackAwayNodeAStar`** (entry `0x102edbb0`, thunk `0x10002fea`), the
  search behind **`0x88 TASK_FIND_FOLLOWER_BACKAWAY_ASTAR`** (Troika `StartTask` `102a3115`;
  the schedule `SCHED_TROIKA_FOLLOWER_BACKAWAY_ASTAR` lists it): threat = the follower boss,
  `minDist = +0x6488 − 10.0`, third argument 50000.0 (`0x47435000`) and never read. `h =
  max(0, minDist − |threat − node|)`, edge cost the exact length. **Success is tested at the
  pop: `g[best] >= minDist`** (`1030136a FCOMP` / `AND 0x100` / `JE`) — the first popped node
  whose PATH LENGTH from the NPC reaches `minDist`, not its distance from the threat. Per
  neighbour, after the predicate, the edge is SKIPPED when the box spanned by its two node
  positions overlaps the threat box `threat − 20 … threat + (20, 20, 80)` (`0x10300e60`, an
  AABB overlap, twelve floats): the route may not pass through the boss. Pre-checks answer
  `-1` (no nodes, no graph, no nearest node — the last two with a DevWarning) and the wrapper
  maps `-1` to false. **Retail bug:** exhaustion answers **0**, not `-1` (`10301320 XOR
  EAX,EAX`), so the wrapper (`102edc0a CMP EAX,-1`) reports SUCCESS and hands back node 0's
  position.
- **The primary search's second pass — `102fd3e5 TEST [npc+0x224],0x800000`.** Only when pass
  1 exhausts AND that `m_debugOverlays` bit is set: a full re-search (arrays, bitsets and seed
  rebuilt) that draws as it goes (`0x102fcfe0`, a nine-argument cdecl that writes only its own
  stack) and, at each pop, asks NPC slot 527 (`+0x83c`) and drops a refused node without
  expanding it. **It cannot find a route pass 1 missed** (corrected 2026-09-20; an earlier
  pass claimed it could): same seed, pop, cost, relax test and link predicate, and the
  predicate already applies slot 527 to every link's far node (`0x1027db30`, `JMP
  [EAX+0x83c]`), so pass 2's extra test at the pop only REMOVES nodes — its pops are a subset
  of pass 1's, and a stale link pass 1 probed at this `curtime` is refused unprobed. The
  loop-head bit clear `102fd395`…`102fd3c8` is `CBitVec`'s unused-tail mask (table
  `0x1055b298`, entry 0 = 0: a no-op when `n ≡ 0 mod 32`), not a clobber. Pass 2 only draws.
  With the bit clear (every shipped run) exhaustion is final.

**The `0x2000`-only brushes are the ones that flag links (2026-09-19; `pedclip_census.py`,
`pedclip_links.py`).** Game-wide 2,192 brushes carry `0x2000`: 1,881 with both clip bits, 264
with `MONSTERCLIP` alone, and **47 with no clip bit** — 9 `sm_hub_1`, 13 `hw_hub_1`, 16
`la_hub_1`, 6 `ch_hub_1`, and one each in `hw_asphole_1`, `la_parkinggarage_1`, `sp_theatre`.
A brush that also carries `MONSTERCLIP` stops `InitLinks`' own walk test (mask `0x2000b`), so
no ground link is built across it and its `0x2000` never reaches a link. On `sm_hub_1`, with
each brush's box grown by hull 0: **455 of the 461 flagged links cross one of the 9
clip-free boxes** (3 more graze a `MONSTERCLIP|0x2000` box, 3 cross none — the boxes are
AABBs of the brush planes), and **0 of the 1,185 unflagged hull-0 ground links crosses one**.
The 9 are slabs at street level (z −118 … −16 against nodes at z ≈ −111; brush 25 is
2,440 × 244 and carries 198 of the links) and none of the map's 6 `info_node_crosswalk`
origins lies inside one. So: the clip-free `0x2000` brush is the roadway volume — open to the
player and to every NPC, priced only for a pedestrian route — and the clip-carrying ones are
ordinary clip brushes on which the bit is inert for navigation.

**Who calls `CNPC_VWerewolf::HasPath 0x103d0db0` (2026-09-19; two opencode walks diffed, the
reference scan and the disputed sites re-run).** Nothing dispatches it: no vtable, datamap or
table holds its address or its one thunk `0x1000c26b` (dword scan of the file: none). It is a
plain member — `(Vector start, Vector end)` by value, `RET 0x18`, forwarding to the
navigator's `0x102ee380` and returning its bool untouched — with **24 direct call sites in
13 werewolf functions**: `UpdateConditionShouldBreakHint` (origin → break-hint ground point,
true sets COND `0x7b`), `UpdateConditionCanSpecialMove` (move-hint TARGET ground point →
cached enemy point, `103cc718`…`103cc76b`; true sets `0x78`, false clears the move hint),
`CheckAllRandomMoveHints`, `CheckAllMoveHints`, `FindBreakHint`, `FindEgressHint`,
`FindRandomMoveHint`, `IsImperativeMoveHint`, `IsImperativeRandomMoveHint`, `FindMoveHint`,
`FindTeleportHint`, `InitPathableHints` and `IsEnemyUnreachable` (origin → cached enemy
point; a route clears the unreachable byte `+0x66a1`). They are reached from
`GatherConditions`, `SelectSchedule`, `StartTask` `0x158`, `RunTask` `0x14b`–`0x14d`, and
**`NPCThink 0x103cb590`, which with an enemy time-slices on `engine(+0x1e0)() % 5`** (table
`0x103cb754`): 0 `UpdateConditionCanTeleport`, 1 `…EnemyUnreachable`, 2 `…DeathTriggered`, 3
`…CanSpecialMove`, 4 `CheckStuck(1)`. One walk had NPCThink reach `HasPath` only through its
debug walker; the other had case 0 empty. Names are the functions' own scope-trace strings.

**What pass 2 draws — `0x102fcfe0` (2026-09-19; a second opencode A/B pair, which agree on
every colour and shape; the four call sites' pushes re-read from the DLL).** `cdecl(nodes,
parent[], hops[], i, j, hull, R, G, B)`, thunk `0x1000f1be`, called only from the primary
search. Per call: a BOX of the hull's full extents at node `i`'s hull position in `(R, G, B)`;
a LINE from `parent[i]`'s position to `i` at half brightness, when `i` has a parent; and a
10-unit LINE from `i` toward `j` (`normalize(pos j − pos i) × 10.0`, `0x1044e664`). All three
last 10.0 s (`0x41200000`). Each channel is first darkened by `4 × (hops[i] & 0x3f)`, floored
at 0 — but pass 2 zero-fills its `hops` array and never writes it, so nothing darkens. `j` is
always the goal node. The four sites, in search order: the seed, `(goal, goal)`, **green**
`(0x40, 0xff, 0x40)` (`102fd539`…`102fd565`); a popped node that slot 527 REFUSES, **red**
`(0xff, 0x40, 0x40)`, not expanded (`102fd81a`…`102fd83c`); a popped node that is not the
goal, **white** (`102fd855`…`102fd86d`), before its links are walked; a link the predicate
`0x102ff960(link, popped)` refuses, **red** at the link's far node (`102fd89f`…`102fd8c8`);
the goal popped, **green** (`102fda2f`…`102fda44`), then the route is built. An accepted or
relaxed neighbour and exhaustion draw nothing. The helper writes nothing but its own stack;
the draws go out through the engine's debug-overlay message path and are culled beyond
~9,487 units (`d² > 90,000,000`, `0x1046bac4`) of player 1. One of the second pair called slot
`+0x83c` "`OverrideMove`, slot 525"; `0x83c / 4` is 527, the node-refusal slot recovered above.

**Unrecovered:** nothing in this section.

### Which graph the patched install runs on (2026-09-19)

Evidence, from the install read directly (`$ELYSIUM_WORK_ROOT/opencode/re18/ain_mtime_census.py`):

- `Unofficial_Patch/` ships **108 BSPs and 108 AINs**, its own complete set; `Vampire/` has 101
  loose of each. Applying the rule above to the patch set: on **107 of 108 maps the AIN is not
  older than the BSP, so it LOADS.** The patch AINs were written 60–70 s after their BSPs
  (`sp_tutorial_1` 07:29:34 → 07:30:36; `sm_hub_1` 07:58:50 → 08:00:02): the signature of retail's
  own rebuild-and-save. Only `sp_genesisdevice_1` is stale (BSP 2025-12-29, AIN 2025-07-19): it
  rebuilds on first entry and writes the result back.
- The patch tutorial AIN's zone header, `0 2 0 0 33 23 16 3 7 24 10 64 22`, is the base file's
  seven zones plus two new ones of 64 and 22 nodes and ONE node in zone 1 (the header's second
  cell reads 2, but one node carries zone 1) — 86 + 1 = the 87 nodes the patch BSP adds
  (203 − 116).
- A live witness: the project's own RE37 capture of `sm_hub_1` on this install
  (`Unofficial_Patch/logs/console.log`, `developer 1`) contains no `Node Graph out of Date`
  line. The hub graph was loaded. That also excludes the 2015 loose `Vampire/` hub AIN, which is
  older than the patch BSP and would have forced the rebuild message.

So the patched install's graphs are the patch's own loose AINs — **203 nodes / 429 links on the
tutorial, 578 / 1,862 on the hub** — built by retail from the patch BSPs, which is also what
makes the positional hint pairing above valid for them. The packed 116-node tutorial graph pairs
with the unpatched BSP only.

**The search order, read from the binaries (2026-09-19).** `FileSystem_Stdio.dll` and
`launcher.dll` are not in the corpus but are on disk (`bin/`); disassembled directly
(`pedis.py`, image base `0x10000000` for both).

- **Who adds the paths — `launcher.dll` `FileSystem_SetGameDirectory 0x10002540(modDir)`**, with
  `modDir` = the `-game` value (`0x100027b0` reads it): `RemoveAllSearchPaths` (filesystem slot
  `+0x28`); then, when `modDir` is given, `AddSearchPath("<base>\<modDir>", "GAME")` (slot
  `+0x2c`, `100026b6`) FIRST; then `AddSearchPath("<base>\<default>", "GAME")` (`10002771`)
  for the default game directory (`0x10003180`; `Vampire` on this install — the DLL also
  carries a `-defaultgamedir` switch), skipped only when the two names compare equal.
- **What one `AddSearchPath` does — `FileSystem_Stdio.dll` `0x100025c0`**: a path containing
  `.bsp` goes to the map-pack arm (below); a path already present returns; otherwise the
  DIRECTORY entry is appended at the tail of the search-path vector (`this+0x30`, 0x4c-byte
  rows) and THEN its packs are appended behind it by `0x10001ec0` → `0x10001ee0(path, 0)` and
  `(path, 100)`: each call `stat`s `pack%.3d.vpk` upward from its start index until one is
  missing, then appends from the HIGHEST found down to the start (`10001f54 DEC ESI` …
  `10001f5b JL`). So a directory's loose files outrank all of its packs, a higher pack number
  outranks a lower one inside a range, and the `000` range outranks the `100` range.
- **How a file is found — the open loop `10003087`…`100030c7`**: index 0 upward, an optional
  path-ID filter (`row+4`), first search path that opens the file wins (`0x10002a50`).
- **The map's own pack** — the engine adds the `.bsp` as a `"GAME"` path at level load
  (`engine 0x200f55f0`); the `.bsp` arm `0x100022d0` inserts it at index **0** (`1000241c PUSH
  0` / `CALL 0x10009cc0`), ahead of everything. No patch BSP's pak lump holds an `.ain` (all
  108 read), so it never decides a graph.

On this install `Unofficial_Patch/` holds no VPK, so with `-game Unofficial_Patch` the order
is: the map's embedded pack, `Unofficial_Patch/` loose, `Vampire/` loose, `Vampire/pack010 …
pack000`, `Vampire/pack103 … pack100`. `maps/graphs/<map>.ain` therefore resolves to the
patch's loose AIN on every map, which is what the census above assumed. **Unrecovered:**
nothing here; a `developer 2` capture would only re-witness it.

### What the shipped graphs and maps actually use (2026-09-19, 0018 stories 3 and 4)

_Censused over the patched install's 108 BSPs and 108 text AINs, read only
(`$ELYSIUM_WORK_ROOT/research/nav-measure/`: `census_links_hulls.py`, `zone_separators.py`,
`schedule_nav_demand.py`, and the inline rat-link and crosswalk passes); the hull rows and the
waypoint bit read from the corpus._

**Link-off is unreachable.** The whole game carries six `info_node_link` entities, all on
`sp_giovanni_4`, none with a `targetname`; no map output and no exported Python script can address
them. Link info `0x1000` (§ "The link predicate", step 1) is therefore never toggled by content.

**No link flies or climbs.** 97 graphs parse (11 maps declare `NumNodes: 0`): 11,558 nodes,
29,523 links. Across all 22 hull slots every non-zero motion word is 1 (ground) or 2 (jump);
motion 4 and 8 occur nowhere. Node types: 11,517 ground, 37 of type 1, 4 climb, 0 air.

**The hull table** — 22 rows `{bit, name, mins, maxs, smallMins, smallMaxs}`, one staticinit each
(`0x102d4440` … `0x102d5fd0`); a row's index is its bit, not its address. Rows with links in any
shipped graph (Source units; `UsedHullBits` is the OR of these bits):

| hull | bit | name | mins..maxs | links |
|---:|---|---|---|---:|
| 0 | `0x1` | `HUMAN_HULL` | (-13,-13,0)..(13,13,72) | 27,956 |
| 7 | `0x80` | `TINY_CENTERED_HULL` | (-8,-8,-4)..(8,8,4) | 8,986 |
| 10 | `0x400` | `TZIMISCE1_HULL` | (-35,-35,0)..(35,35,100) | 403 |
| 11 | `0x800` | `TZIMISCE2_HULL` | (-25,-25,0)..(25,25,100) | 640 |
| 12 | `0x1000` | `WEREWOLF_HULL` | (-40,-40,0)..(40,40,110) | 596 |
| 13 | `0x2000` | `TZIMISCERUNNER_HULL` | (-12,-12,0)..(12,12,24) | 1,577 |
| 14 | `0x4000` | `GARGOYLE_HULL` | (-15,-15,0)..(15,15,100) | 64 |
| 15 | `0x8000` | `MING_XIAO_HULL` | (-34,-34,0)..(34,34,120) | 48 |
| 16 | `0x10000` | `MING_XIAO_PATHING_HULL` | (-80,-80,0)..(80,80,120) | 33 |
| 17 | `0x20000` | `MING_XIAO_TENTACLE_HULL` | (-13,-13,0)..(13,13,30) | 59 |
| 18 | `0x40000` | `HENGEYOKAI_HULL` | (-30,-30,0)..(30,30,100) | 294 |
| 19 | `0x80000` | `RAT_HULL` | (-6,-6,0)..(6,6,10) | 6,848 |
| 20 | `0x100000` | `MANBAT_HULL` | (-40,-40,0)..(40,40,160) | 256 |
| 21 | `0x200000` | `SHERIFF_HULL` | (-20,-20,0)..(20,20,100) | 363 |

Hulls 1–6, 8 and 9 carry no link anywhere. Their extents were recovered 2026-09-20 with the rest
(all 22 rows are now committed as `docs/vtmb/data/hull_table.json`, replayed by
`research/tooling/probes/hull_table.py --check`), and they are listed here because a hull with no
link still sizes a body:

| hull | bit | name | mins..maxs |
|---:|---|---|---|
| 1 | `0x2` | `HUMAN_PATHING_HULL` | (-8,-8,0)..(8,8,72) |
| 2 | `0x4` | `SMALL_CENTERED_HULL` | (-20,-20,-20)..(20,20,20) |
| 3 | `0x8` | `WIDE_HUMAN_HULL` | (-15,-15,0)..(20,15,72) |
| 4 | `0x10` | `TINY_HULL` | (-12,-12,0)..(12,12,24) |
| 5 | `0x20` | `WIDE_SHORT_HULL` | (-35,-35,0)..(35,35,32) |
| 6 | `0x40` | `WIDE_TALL_HULL` | (-25,-25,0)..(25,25,100) |
| 8 | `0x100` | `LARGE_HULL` | (-40,-40,0)..(40,40,100) |
| 9 | `0x200` | `LARGE_CENTERED_HULL` | (-38,-38,-38)..(38,38,38) |

`WIDE_HUMAN_HULL` is the one asymmetric row in the table: its maxs reach 20 in x against mins of
−15, so it has no single radius and could not be an agent as it stands. It carries no link, so
nothing asks it to be. `UsedHullBits` by map count:
`0x1` 66, `0x81` 15, `0x80001` 10, `0x2001` 4, `0x4001` 2, `0x82c01` 2, and one each of
`0x40081`, `0x38001`, `0x2081`, `0x82001`, `0x80c01`, `0xc01`, `0x801`, `0x300081`, `0x1001`.
Both witness maps are `0x80001`: human and rat.

**Declaring a hull and carrying a link for it are two different counts** (2026-09-20,
`research/tooling/probes/census_links_hulls.py`, which re-derives every figure in this section
from the patch's loose graphs). Per hull, maps declaring it in `UsedHullBits` against maps
carrying at least one link for it: hull 0 **108 / 97** (the 11 `NumNodes: 0` graphs still declare
human), 7 **18 / 18**, 10 4/4, 11 5/5, 12 1/1, 13 8/8, **14 `GARGOYLE_HULL` 2 / 1**, 15–18 1/1,
19 `RAT_HULL` 14/14, 20 1/1, 21 1/1.

Two corrections fall out. `TINY_CENTERED_HULL` carries its 8,986 links on **18** maps, not the 15
stated earlier and carried into 0018 story 3 — 15 is the `0x81` row of the histogram alone, and it
misses `0x40081`, `0x2081` and `0x300081`. And `GARGOYLE_HULL` is the table's one hull a map
DECLARES without shipping a single link for it, so "one mesh per bit of `UsedHullBits`" builds one
mesh nothing can path on; the bake reports such a mesh rather than assuming the declaration is
load-bearing.

**The SMALL extents of the same rows** (record `+0x20` / `+0x2c`, read by `0x102d6140` /
`0x102d6160`; each record is filled by its own static initialiser in `0x102d4440`…`0x102d6100`,
values pass through stack slots — `hull_table.py` replays them). Identical to the full extents
except: 0 HUMAN `(-8,-8,0)..(8,8,72)`; 2 SMALL_CENTERED `±12`; 3 WIDE_HUMAN
`(-10,-10,0)..(10,10,72)` (its FULL maxs are asymmetric, `(20,15,72)`); 5 WIDE_SHORT
`(-20,-20,0)..(20,20,32)`; 9 LARGE_CENTERED `±30`; 10 TZIMISCE1 `(-45,-45,0)..(45,45,100)` and
11 TZIMISCE2 `(-35,-35,0)..(35,35,100)` — both LARGER than their full hulls; 12 WEREWOLF
`(-20,-20,0)..(20,20,60)`; 13 TZIMISCERUNNER `(-3,-3,0)..(3,3,24)`; 14 GARGOYLE
`(-10,-10,0)..(10,10,70)`; 15 MING_XIAO and 16 MING_XIAO_PATHING keep x / y and drop to
height 100; 18 HENGEYOKAI `(-10,-10,0)..(10,10,70)`.

**There is no walkable slope limit to recover** (2026-09-20, 0018 story 3). The spec listed one as
an unrecovered read; it does not exist, because an authored node graph never asks the question.
Two bodies decide whether an NPC may stand or walk somewhere, and neither reads a surface normal:
`CAI_MoveProbe_TestGroundMove 0x102e4f50` clamps only on step height — `max(hull height × 0.5,
StepHeight + 0.1)` at `102e565b`, 36.0 on hull 0 — and `CAI_MoveProbe_CheckStandPosition
0x102e7270` accepts a downward hull trace iff it hit at all (`fraction != 1.0`) and the hit
ENTITY's slot 164 allows standing, reached through slot 166, whose body `0x10026f80` is the base
for every class but the two Ming Xiao overrides. Passability is the step height and the hull, end
to end.

A rasterised NavMesh must answer what retail never asked, so the port supplies the term. It takes
retail's OWN standable normal from the player movement layer, where the same floor is stood on:
`0.7` at `0x104492d0`, already ported as `ElysiumMove::StandableZ`
(`Source/ElysiumUE/Public/ElysiumMoveSolve.h`), giving a 45.57° agent slope. The constant is
retail's; applying it to NPC navigation is the named modernization.

**Step height: three species override it, and the graph was built with none of their values**
(2026-09-20). Slot 522 is `StepHeight`; 77 classes fill it and exactly **three** species replace
the base, which closes `shape.md`'s open "which three override it for real":

| class | body | constant | value |
|---|---|---|---|
| `CAI_BaseNPC` (every other NPC) | `0x101a6b40` | `0x10453b94` | **18** |
| `CNPC_VMingXiao` | `0x10391030` | `0x104492a8` | 30 |
| `CNPC_VMingXiaoTentacle` | `0x1039b050` | `0x1044c3a4` | 9 |
| `CNPC_VTzimisce` | `0x103b6dd0` | `0x104cc4fc` | 26 |
| `CAI_TestHull` | `0x102d72b0` | `0x10462950` | **40** |

The last row is not a species and is the one that matters for a baked mesh. `CAI_TestHull` is the
probe `InitLinks` drives to lay down the graph, and it steps **40 units (101.6 cm)** — more than
twice the 18 any NPC walks with. So **the shipped links assert reachability at a step height no
NPC has**, and a NavMesh cut at 18 units can honestly fail to path a link the graph carries.
Story 3's acceptance ("every ground link paths on its agent's mesh") must therefore treat a
step-height shortfall as an EXPECTED class of outlier, reported with the rise measured, rather
than as a mesh defect — and the alternative, cutting every agent at 40, would let NPCs walk up
ledges retail's own motor refuses. The agents take 18; the harness names what that costs.

(The two constants `TestGroundMove` clamps with are confirmed at the same time: `0x104454d0` is
`0.5` and `0x104493d0` is the double `0.1`, so the clamp really is
`max(hull height × 0.5, StepHeight + 0.1)` — 36.0 on hull 0.)

**The rat hull is not a subset of the human one.** Of 6,848 rat links on 14 maps, 546 carry no
human motion. `sp_tutorial_1`: 41 of 428, reaching 1 node no human link touches, 5 of them
joining node sets the human links keep apart (`0–69`, `1–69`, `44–69`, `77–113`, `146–190`);
`sm_hub_1`: 99 of 1,862, 2 such nodes, 9 such links. A rat passes where a human cannot.

The hub's 9 were enumerated 2026-09-20 (they had been counted but never listed, and 0018 story 3
pins them): `155–304`, `156–304`, `158–304`, `304–154`, `304–577`, `526–568`, `567–568`,
`568–527`, `568–528`. They are two places, not nine: every one touches node 304 or node 568. The
acceptance these support is one-sided — a rat-only link must path on the rat mesh, and these must
NOT path on the human mesh — so a Recast floor that is simply denser than the sparse graph will
show up here as a named exception rather than a silent pass.

Per-hull link and component counts on the two witnesses, from the same probe: `sp_tutorial_1`
human 388 (363 ground, 25 jump) in twelve components `63/27/23/22/18/16/10/7/6/3/3/3`, rat 428
(392 / 36) in nine `64/33/24/23/22/16/10/7/3`; `sm_hub_1` human 1,763 (1,646 / 117) in
`509/63/2`, rat 1,862 (1,759 / 103) in `510/64/2`.

**Brush contents, read as answers to the retail masks.** Taking
each brush's four answers — blocks the player (`& 0x1400b`), blocks an NPC (`& 0x2400b`), blocks
sight (`& 0x804091`, the brush bits of the sight mask `0x2804091`), is a pedestrian volume
(`& 0x2000`) — the 146,162 answering brushes of the 108 maps fall into seven signatures.
Re-derived in the repository 2026-09-20 and reproducing every count below:
`uv run elysium research contents_signatures`, reading the four masks and their retail sites from
`research/tooling/data/contents_masks.json`.

| player | NPC | sight | ped | brushes | typical contents |
|:-:|:-:|:-:|:-:|---:|---|
| ✓ | ✓ | ✓ | | 129,476 | `0x1`, `0x8000001` — solid |
| ✓ | ✓ | | | 12,629 | `0x10000002` window, `0x10000008` grate, `0x8030000` both clips |
| ✓ | ✓ | | ✓ | 1,881 | `0x8032000` both clips and pedestrian |
| | | ✓ | | 1,759 | `0x8000080` — OPAQUE, not solid |
| | ✓ | | ✓ | 264 | `0x8022000` NPC clip and pedestrian |
| | ✓ | | | 106 | `0x8020000` NPC clip |
| | | | ✓ | 47 | `0x8002000` pedestrian alone |

**The port answers these four questions separately as of 2026-09-20** (0018 story 3, jobs 1–5
and 7), and its navigation mesh is cut from the answers: each map carries a baked mesh per agent
its graph names, built from the bodies that block an NPC and no others. A brush's signature is computed once at the seam, `.hulls` carries the contents word, and
each signature gets a collision body wearing a generated profile: the player and NPCs are on two
different object channels, sight has its own trace channel, and Unreal's own rule — a body is
navigation-relevant exactly when it blocks `ECC_Pawn` — makes "blocks an NPC" and "cuts the mesh"
the same fact. The visible consequence on `sp_tutorial_1`: 17 sight-only brushes begin stopping
the thug's sight, 276 window and grate brushes stop doing so, and the 5 NPC-only clips exist for
the first time.

No brush in the game blocks the player alone: `PLAYERCLIP` never ships without `MONSTERCLIP`.
Non-solid brushes carrying `OPAQUE 0x80` or `0x800000` number 1,764 on 25 maps
(`la_museum_1` 894, `la_skyline_1` 265, `hw_warrens_2b` 102 … `sp_tutorial_1` 17); window and
grate brushes, which the sight mask does not name, number 276 on `sp_tutorial_1` and 151 on
`sm_hub_1`. `sp_tutorial_1` shows four of the signatures (2,725 / 309 / 17 sight-only /
5 NPC-only) and `sm_hub_1` five (4,020 / 151 / 93 / 31 / 9). That a sight-only brush stops a
retail sight trace is read from the mask and the contents, not yet witnessed in a running game.

**Zones end where the geometry does.** For every pair of zones with nodes within 320 units on
one floor, the six nearest node pairs were clipped against the BSP brush planes (mask
`0x2400b`) and the door models: on `sp_tutorial_1` zones 6/9 and 6/10 are walled apart, 9/10 meet
at `anotherfuckingtutorialdoor`, 11/12 at `soc_int_locked_door` (one pair, `146–184`, reads open
at chest height and is unexplained); on `sm_hub_1` the 64-node zone 6 has no node within 320
units of the 510-node zone 4, and the only open adjacencies are the isolated zone-1 nodes (`40`,
`564`). A door with no link through it is the usual zone boundary.

**Which schedules need the graph's goal searches.** Of the 691 embedded texts, 134 name a
graph-family task: `TASK_FIND_COVER_FROM_*` in 20 — 24 with the two
`TASK_FIND_FAST_COVER_FROM_ENEMY` and the two `TASK_FIND_LATERAL_COVER_FROM_ENEMY` users
(`sched_text_census.py`, recounted 2026-09-20; an earlier pass said 22) — (among them `SCHED_TROIKA_CHASE_ENEMY_FAILED`
and `SCHED_TROIKA_SHOT_BY_UNKNOWN`), `TASK_GET_PATH_TO_HINTNODE` 17, `TASK_SNAP_TO_HINT` 11,
`TASK_FIND_BACKAWAY_FROM_SAVEPOSITION` 8 and the three follower back-aways,
`TASK_FIND_HUNT_PATROL_TARGET` 6, `TASK_FIND_FLANK_NODE_TO_ENEMY` 4, the cower pair 4,
`TASK_GET_PATH_TO_INTERESTING_PLACE` 5. No text names `TASK_CREATE_HUNT_PATROL_LIST`.

**Waypoint bit `0x20` is "do not simplify", and the navigator simplifies.** The flags word is
waypoint `+0x28` (row `0x38`: position, yaw `+0xc`, node `+0x10`, flags `+0x28`, nav type `+0x2c`,
next `+0x30`, prev `+0x34`). Its readers are the path simplifier's entry `0x102f13d0` (called from
`SetGoal` and the pre-move `0x102efd50`; `102f1425 TEST [EAX+0x28],0x2a` — any of `0x20`, `8`, `2`
on the current waypoint and nothing is simplified), its forward scan `0x102f0ab0`
(`102f0b0b`, same mask) and the fly simplifier `0x102f1690` (`& 0x22`). The arc builder
`0x10304670` sets the same bit on every arc waypoint, so it is the engine's generic bit, which
the pedestrian route builder reuses for crosswalk pairs. So a retail NPC does cut corners between
graph waypoints, and a pedestrian does not across a crosswalk pair.

**The crosswalk gate is a separate, script-driven rule.** `CAI_Hint::InputWalk` /
`InputDontWalk` (`0x102d0a50` / `0x102d0a80`) call `0x102f97c0`, which clears or sets bits `0xf0`
of `link+0x64` on links between two type-11000 nodes; `0x102a0bc0` (from the path advance
`0x102f0400` and the NPC body `0x10298340`) tests waypoint flag 4, goal type 8 and the link's
`0x10 << n`, and on a match `0x102a0b90` sets `NPC+0x14b8 |= 4` and stores the link at
`NPC+0x630c`. Content reaches it: `sm_hub_1` and `hw_hub_1` each fire `Walk` twice and
`DontWalk` twice; 22 `info_node_crosswalk` stand in the game (6 on each of `sm_hub_1`,
`sm_hub_2`, `hw_hub_1`; 4 on `sp_theatre`).

**The wait itself was already recovered** — `npc-ai/conditions-and-states.md` § "The pedestrian
crosswalk" and `npc-ai/programs.md` § "Interesting places": `UpdatePedestrianInfo 0x102a0d20`
(from `RunAI`, path type 8 only) is the reader of `+0x14b8 & 4` and `+0x630c`; `n =
((int)curtime >> 4) & 3`; bit set → `CROSSWALK_DONTWALK 0x13` → the selector's `0x102
SCHED_TROIKA_WAIT_AT_CROSSWALK` (`PAUSE_MOVING; FACE_NEXT_NODE; WAIT_INDEFINITE`, interrupted
by `CROSSWALK_WALK 0x12`); bit clear → `0x12` and `AT_CROSSWALK` dropped.

**The four-phase clock is inert in shipped content (2026-09-19).** `0x102f97c0` is the only
writer of the phase bits in the image (corpus scan for `link+0x64 |=`), it writes all four at
once (`|= 0xf0` / `&= ~0xf0`), and no shipped AIN carries any of them — link info is `0` or
`0x2000` on all four crosswalk maps (`ain_linkinfo_census.py`). So a crossing is WALK from map
load until its first `DontWalk`, and whichever phase the clock is in answers the same. The
cycle is authored in the map: `sm_hub_1` `logic_timer streetlight_timer` (`RefireTime 40`)
fires `crosswalk_south Walk` at +0, `DontWalk` +12, `crosswalk_east_west Walk` +20, `DontWalk`
+32; `hw_hub_1` `streetlight_timer_2` (40 s) fires `xwalk_4 Walk` +0 / `DontWalk` +20 and
`xwalk_3 Walk` +20 / `DontWalk` +40. `sm_hub_2` and `sp_theatre` have crosswalk nodes and no
timer: their pedestrians never wait.

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

### The hint-path, snap and cower arms, walked (2026-09-19, 0018 story 9)

_Read here from the DLL listing (`$ELYSIUM_WORK_ROOT/opencode/re18/vdis.py`); the opencode pair
for this item was cut off by the plan's usage limit and has not reported._ Task ids from the
registry `0x10316ff0`: `GET_PATH_TO_HINTNODE 0x16`, `FACE_HINTNODE 0x2f`, `FIND_HINTNODE 0x40`,
`FIND_LOCK_HINTNODE 0x41`, `CLEAR_HINTNODE 0x42`, `LOCK_HINTNODE 0x43`,
`GET_PATH_TO_COWER_NODE 0x84`, `SNAP_TO_HINT 0x10e`. Troika `StartTask 0x102a1910` dispatches
`id − 5` through the byte table `0x102a7ab8` into `0x102a77f8`; `0x40`–`0x43` take its default
arm and are the base class's (§ "The hint list and its four searches", `shape.md`).

- **`GET_PATH_TO_HINTNODE` (`0x102a371d`).** No `m_pHintNode` → `TaskFail(4)`. Else the stand
  point from `0x102b6120(npc, &pos, 0)` — `CAI_Hint::GetPosition 0x102d1180`: the hint's
  NETWORK NODE at hull height when `m_nNodeID != -1`, else the hint entity's origin; a
  cover-corner hint (`0x27d8`) is pushed sideways along its facing ± 45° (`0x1049949c`) by the
  lean side, scaled 1.4 / 1.2 (`0x1049ae8c` / `0x1049ae90`). Then a goal of **type 4**, no node
  (`-1`), activity `0x13`, tolerance `-1.0`, goal-flags 0, `SetGoal(nav, &goal, 0)`. The arm
  neither completes nor fails the task itself: a refused route is raised by the navigator
  (`0x0c`, § "The route gates"). Type 4 with flags 0 tries the LOCAL route first, so a hint in
  plain sight is walked to with no graph, and a standalone hint (`m_nNodeID == -1`) is an
  ordinary position goal.
- **`SNAP_TO_HINT` (`0x102a5f16`).** No hint → `TaskFail(4)`. Else the same stand point, then
  `SetOrigin` (slot 62) and `TaskComplete`. No trace, no distance tolerance, no angle write:
  the NPC is placed on the hint's stand point outright.
- **`FACE_HINTNODE` (`0x102a382a`)** reads `m_pHintNode` with NO null test. For `0x27d8` the
  ideal yaw is the hint yaw ± 45° by `m_bLeaningLeft`, flipped 180° (`0x1044c3a8`) when the
  motor's `+0x28` byte stands.
- **`GET_PATH_TO_COWER_NODE` (`0x102a2882`, shared with `0x83 TASK_GET_PATH_TO_FLEE_NODE`)**,
  threat = `GetEnemy()` or, with none, the NPC itself. `r` = slot 418 (`+0x688`) of the task operand; `max = r + 8192.0` (`0x1049ae70`).
  1. a held hint is released with a 1.0 s lock (`ClearHintNode`);
  2. **hints first**: `0x102d1af0(npc, type 0x2774, flags 2 = nearest, radius max)`; a found
     hint is claimed (a refused claim drops it) and walked to — goal type 4, activity `0x13`,
     tolerance `-1.0`; on `SetGoal` success the body extents are saved (`+0x65d0`), set to
     `(40, 40, 80)` and the task COMPLETES; on failure the hint is released with a 5.0 s lock;
  3. with no hint in hand (task `0x84` also sets `m_bfAINPCFlags1 |= 0x200`): the graph COVER
     search `0x102edc80` → `0x10301720`, from the threat's position and eye, first over the far
     half `[r + 4096, r + 8192]`, then over `[r, r + 8192]`;
  4. a found point → goal **type 6**, activity `0x13`, tolerance `-2.0` (`0x1049a1b0`, the hull
     default), `SetGoal`; neither → **`TaskFail(0x18)`**.
  `_SAVE_POS 0x85` (`0x102a2bd8`) is the same search anchored on `m_vSavePosition + view
  offset` instead of a live threat.
- **Slot 418** (`+0x688`) resolves SENTINEL task operands to distances; anything else passes
  through. The values are one enum, `-1000000` downward, split across the bodies: base
  `0x102702d0` — `-1000000` → `m_flSpecialDistanceAccum` (`+0x5bac`), `-1000002` → 160.0
  (`0x1047a3ac`), `-1000003` → the melee-range singleton (`DAT_10924a1c`: 0.0 or its `+0x28`);
  Troika `0x102bf6e0` — `-1000005` → `+0x6484`, `-1000006` → `+0x6488`, `-1000007` → `+0x648c`
  (the three `Npc_Follower_Info` radii), `-1000008` → 10.0; `CNPC_VTzimisce 0x103b9120` —
  `-1000001` → 150.0; `CNPC_VMingXiao 0x10392a10` — `-1000004` → `m_flIdealRange`.

So neither family has a query of its own: the hint path is `GetPosition` plus an ordinary
type-4 goal, and cowering is story 4's nearest-hint search followed by the cover search below.
Both opencode walks reported after this was written and agree arm for arm (one misnumbered
the Troika sentinels by 8; the compares above are from the listing). They add: the BASE class's
`GET_PATH_TO_HINTNODE` (`0x10285a9e`) is the same goal without the lean offset, and the base
has no `SNAP_TO_HINT` / cower arms at all (`"No StartTask entry for %s"`); neither class has a
`RunTask` arm for `0x16` or `0x84`, so after `SetGoal` those tasks are finished by the
navigator's own completion or failure, never by the arm; `FACE_HINTNODE`'s run arm completes
on `FacingIdeal` — a yaw error within 0.006 (`0x10499568`, a double).

### The hunt selectors, walked — `0x10306700`, `0x10306f60` (2026-09-19, 0018 story 9)

_Two independent opencode walks, diffed; the task names, the path-index writer, the floor
counter and the second normalise re-read from the DLL here._

Both are `__thiscall` on the pathfinder, `(npc, unused pos*, heading*, maxDist[, out Vector*])`,
and every caller passes `maxDist = 256.0`. With no built graph (`DAT_1093408c == 0`) or no
nearest node (`0x102f3c10` from the NPC's origin → DevWarning) they answer false.

**Direction.** A non-NULL `heading` gives `dir = normalize(heading.xy − origin.xy)`; NULL draws
`y` then `x` from `RandomFloat(-1, 1)` and forces `y = 1.0` when `x == 0.0` exactly. **The
jitter belongs to the HEADING arm only** (`103067ab JE 0x10306836` / `10307040 JE 0x1030709f`
send a NULL heading past it; the jitter blocks end `10306834 JMP` / `1030709d JMP` at the
shared re-normalise): `0x10306700` adds `RandomFloat(-0.1, 0.1)` to an axis only when `|axis|
> 0.1` (`0x104493d0`, a double), x then y; `0x10306f60` adds `RandomFloat(-0.5, 0.5)` to x
then y always. Both normalise again. **Those are the only draws: `0x10306700` 0 to 2 with a
heading and exactly 2 without; `0x10306f60` exactly 2 either way; never 4** (corrected
2026-09-20 — an earlier pass ran the jitter on both arms). The walk itself is deterministic,
and the success exit's patrol-path install (`0x1029f460`, schedule argument 0, path cleared
first by `0x10307aa0`) does not reach the interest roll `0x1029f650`.

**The walk** is a single chain, not a search (the "heap" holds one zero-keyed element). From the
start node, repeatedly: add the 3-D step from the last position; stop when the distance
STRICTLY exceeds `maxDist` (that node is not appended); append the node (cap 64); among the
node's links in adjacency order, take those passing the link predicate `0x102ff960` whose far
node is unvisited — marking every one of them visited, losers included — and score each by
`normalize(pos(far) − ORIGIN AT ENTRY) · dir`; the best STRICTLY greater score wins, so the
earlier link takes a tie. The floor a score must beat is `-1.0` while fewer than 10 nodes are
on the list (`10306b16 CMP ECX,0xa` on the post-increment count: the first nine steps) and
`0.0` after, so a long chain may only keep going forward. No link, or none above the floor,
ends it.

**Output.** `0x10306700` installs the whole list as `m_sppPatrolPathHunt` (`+0x6594`) through
`0x1029f460(…, list, 1)` — even an EMPTY list, answering true. `0x10306f60` answers false on an
empty list; else it installs the one-node path `{terminal}` and writes the terminal node's hull
position to `*out`. The installer resets the path (type 0: start index 0, step +1, schedule 0),
so its interest-roll block does not run here; `0x10307b60` writes the CURRENT INDEX `path+0x10 =
min(count − 1, typeTable[type])`, not the schedule word.

**Tasks** (names from the registry `0x10316ff0`): `0xae TASK_CREATE_HUNT_PATROL_LIST`
(`0x102a3bc7`) and `0xaf TASK_FIND_HUNT_PATROL_TARGET` (`0x102a3c5d`, out =
`m_vecHuntPatrolTarget +0x645c`); heading = the origin of slot 168's entity (the enemy, else a
valid last enemy) or NULL; false → `TaskFail(0x20)`, true → complete. `TASK_WALK_PATH_HUNT` is
`0x104`, a different task. `0x7b` walks the installed hunt path exactly as `0x7a` walks a patrol
(`0x102aa640`: no path or id `-1` → `0x1d`; refused goal → `0x0c`). `CNPC_VFrenzyShadow`
overrides `0xae` (a 256-unit entity fallback for the heading) and `0xaf` (copies the target's
origin with NO graph walk). One walk's dump of the embedded schedule texts finds every shipped
hunt setup using `0xaf`, and no issuer of `0xae`.

**`0xae` is dead in shipped data (2026-09-19).** The name `TASK_CREATE_HUNT_PATROL_LIST` occurs
once in `vampire.dll`, its registry row; no embedded schedule text lists it, and a task reaches
`StartTask` only from the running schedule's task list. `0xaf` is listed by six:
`SCHED_TROIKA_HUNT_SETUP`, `…_HUNT_SETUP_NO_ENEMY`, `SCHED_VFRENZYSHADOW_HUNT_SETUP`,
`SCHED_VTZIMISCE_HUNT_SETUP`, `…_HUNT_SETUP_NO_ENEMY` and `SCHED_VTZIMISCE_HUNT_FINISH`.

**Note, not an unknown:** `0x10306700` never refreshes the pathfinder's cached hull (`+8`),
which `0x10306f60` does — the predicate then reads whatever hull the last caller left; with
`0xae` unreachable that arm never runs.

### The cover search, walked — `0x102edc80` → `0x10301720`, twin `0x102edd50` → `0x10302320` (2026-09-19, 0018 story 9)

_Two independent opencode walks, diffed; the `max == 0` substitute, the min clamp, the side-probe
quadrants and the call-site census re-read from the DLL here._

`FindCoverPos(nav; threatPos*, threatEye*, min, max, out*, ignoreEnt)` answers a bool and, on
success, the chosen node's hull position. No graph (`DAT_1093408c == 0`) or no nearest node to
the NPC's origin → false, `m_pHintNode` untouched. `max == 0.0` becomes **784.0**
(`0x44440000`); then `min` is CAPPED at `max × 0.5` (`10301805 FCOMP ST1` / `AND 0x4100` /
`JNE`: only `min > max/2` stores).

**The frontier** is a best-first expansion from the NPC's node, keyed by squared 3-D distance
from the NPC's origin (a real min-heap here, sized to the node count; no depth limit, no RNG).
Pop order is the candidate order and **the first popped node that passes wins**. The start
node is itself a candidate and skips the two enqueue gates below.

**Per candidate, in order:**

1. the node's hint (`node+0xa0`), if any, must have NO live owner — `0x102d1450(hint, 0)`: an
   invalid, stale or NULL-resolving `m_hHintOwner` passes. **Only `+0x5e0` is read**: a disabled
   or cooling-down hint does not disqualify its node here (flank differs, below);
2. `min² <= d² < max²` from the NPC's origin (low bound inclusive, high exclusive);
3. three LINE traces from `threatEye`, contents mask `0x2804091`, ignoring the NPC and
   `ignoreEnt`, every one of which must be BLOCKED (`fraction != 1.0`): to the node position
   plus the NPC's cover eye offset (slot 533 `+0x854`: `(0,0,24)` for a crouch-capable NPC
   whose cover activity from slot 569 `+0x8e4` is the low one — hint type 101 forces low, 100
   medium — else the default eye offset), and to two points at that height displaced by the
   NPC's collision extents × 1.4 (`0x1049ae8c`), the corner picked by which side of the threat
   the eye falls: `x > tx, y > ty` → `(maxs.x, mins.y)`; `x > tx, y <= ty` → `(maxs.x, maxs.y)`;
   `x <= tx, y > ty` → `(mins.x, mins.y)`; `x <= tx, y <= ty` → `(mins.x, maxs.y)`;
4. the validator, slot 548 (`+0x890`, `0x1028af20`): the NPC's hull (`+0x1568`) traced from
   the position to the same point raised by `0.01 − hullMins.z` (`0x1044e658`, a double) —
   in effect a hull-fits-here test, mask `0x202400b`, filter = the NPC, collision group 0 —
   and the ONLY field read is `startsolid` (`1028b00c MOV AL,[ESP+0x5f]` = record `+0x37`):
   set rejects. The fraction is not read, so a blocked-but-not-embedded hull passes. Then an
   NPC with a hint group (`+0x5db0`) accepts only a node whose hint carries the same `Group`
   (`hint+0x5f0`); a hintless node fails that arm.

**On success** the node's cooldown is stamped `node+0x9c = curtime + 1.0` — hint or not — and a
hint is taken: `m_pHintNode = hint`, claimed through `0x102d1350`. The winning node id is kept
in a global (`DAT_10934140`; the twin uses `DAT_10934144`) that ROTATES the next search's
neighbour order. Exhaustion clears `m_pHintNode` and answers false. The search writes no goal.

**Expansion** happens only from a REJECTED node: its links from index `(i + DAT_10934140) %
count`, skipping link-off (`0x1000`) and links whose hull motion misses the NPC's capabilities
— NOT the full predicate `0x102ff960`: no rejected-node, jump-legality or stale test here. A far
node is enqueued unless it is a climb node (type 4), its cooldown is still running
(`node+0x9c > curtime`), or it fails `d²(npc, node) < 1.5 × d²(threatPos, node)`
(`0x104528c8`, a double) — a node more than 1.22× as far from the NPC as from the threat is
never explored. Every far node that passed the link gates is marked visited, enqueued or not. The
directional twin adds one enqueue gate: `dot(dir, normalize(origin − node)) > 0`.

**Callers** (17 sites of the thunk `0x1001334f`, one of the twin's): base `StartTask` tasks
`0x57` (from the best sound), `0x58`, `0x5b`, `0x5c`, `0x5d`, `0x5e`, `0x0c`, `0x0e` — `min 0,
max 1024` unless the operand supplies one through slot 418 — failing `TaskFail(8)`; Troika
`0xa0` (max 150), `0xa2` (from `m_vSavePosition`, min 32) and the twin's only caller `0xa1`
(`dir = +0x6290`, max 256), also `8`; the flee / cower pair above (`0x18`); `0x115
TASK_FIND_COVER_FROM_LASTENEMY_LKP` (`102a6733`, after its lateral pre-check) and `0x147
TASK_FIND_COVER_FROM_UNKNOWN_ATTACKER` (`102a7666`, min 32; no attacker is `TaskFail(0x21)`,
`102a76f7`), both failing **`TaskFail(8)`** (`102a67d6`, `102a76ca` `PUSH 8`); and the
standoff behaviour (`102c7c6c`). _(Corrected 2026-09-20. An earlier pass gave `102a6733` to
`0x8b TASK_MELEE_DODGE` and the fail code as `0x13`: `0x8b`'s case is the bare
`TaskComplete` at `102a66d7` that `0x115`'s block jumps over — `case_of.py` took the nearest
lower entry; `case_reach.py` follows the flow and every other task attribution in this
document was re-run through it — and `0x13` is the goal's activity argument (`102a675c`,
`102a7682`), not a fail code.)_ After a success
the caller, not the search, submits the goal (type 6).

**The trace record, settled (2026-09-19, re-read from the DLL).** `0x10301720` is EBP-framed,
so no PUSH arithmetic applies: the record is `[EBP-0x118]` (`10301b0b LEA ECX,[EBP-0x118]`) and
each of the three traces is followed by `FLD [EBP-0xec]` / `FCOMP qword [0x10449280]` (1.0, a
double) / `TEST AH,0x44` / `JNP reject` (`10301b33`, `10301c0e`, `10301c48`). `0x118 − 0xec` =
**`+0x2c`**: the fraction, at the SDK `trace_t` offset (startpos 0, endpos `0xc`, plane `0x18`,
fraction `0x2c`, contents `0x30`, dispFlags `0x34`, allsolid `0x36`, startsolid `0x37`). The walk
that said `+0x30` was wrong; nothing else of the record is read here.

**The mask `0x2804091` is the game's one sight mask**, not a cover-only value: `FVisible` (slot
201, `0x102b4630`), the lateral test below and the cover traces all push it. Bits: `0x1` SOLID,
`0x80` OPAQUE, `0x4000` MOVEABLE, `0x2000000` MONSTER — the SDK's `MASK_OPAQUE_AND_NPCS` — plus
two more. **`0x800000` is Troika's NPC-opaque bit**: `materials/tools/toolsnpcopaque.vmt`
(`pack002.vpk`) carries `%compileNpcOpaque 1` with `%compilepassbullets` and `$translucent`, and
the only two brushes in the game with the bit (`ch_temple_2` brushes 1444 / 1445, contents
`0x10800008`) are textured with it — a see-through, shoot-through brush that blocks NPC sight
only. **`0x10` is the SDK's SLIME**, by the company it keeps: the water test everywhere in the
image is `contents & 0x4030` (`CBaseEntity::PhysicsCheckWater 0x1003e880`, `SetWaterLevel`,
`PhysicsCheckWaterTransition`, the grenade and game-trace bodies) — the SDK's `MASK_WATER` =
WATER `0x20` | MOVEABLE `0x4000` | SLIME `0x10`, bit for bit. It is on no brush of any shipped
map (census of lump 18 over all 108 patch BSPs; `contents_texture_census.py`), so in shipped
data it blocks no sight. What the
census does show is that OPAQUE matters: 1,755 brushes are `0x8000080` (OPAQUE | DETAIL, no
SOLID), textured `tools/tools_shadow` (1,731) or `toolsblocklight` (18), across 24
maps — light-blocker brushes that are not solid to movement but DO stop every sight and cover
trace. INFERENCE from mask ∩ contents; not witnessed in a running retail game. (The other
`tools_shadow` population, 742 brushes of `0x18000120`, shares no bit with the mask.) In all, 1,764
brushes in 25 maps (`sp_tutorial_1` among them) meet the mask without SOLID or MOVEABLE.

**The lateral pre-check — `0x102784a0` (thunk `0x10003431`), the SDK's `FindLateralCover`,
and its test `0x10278220` (`TestLateralCover`).** `(threatEye*, ignoreEnt)`. First the NPC's
own origin is tested; failing that, `AngleVectors` of the NPC's angles (slot `+0x374`) gives
`right`, the step is `right.xy × 48.0` (`0x10447ee8`; z untouched), and for `i = 0..4` the
LEFT point (`origin − (i+1)·step`) is tested before the RIGHT one — ten points at most, first
pass wins. The test, per point: one line from `threatEye` to the point plus the NPC's view
offset (`+0x184`), mask `0x2804091`, must be BLOCKED (`fraction != 1.0`, the same double); then
slot 548 with a NULL hint (so an NPC with a hint group can never pass it); then
`MoveLimit 0x102e6d70` (nav type 0, origin → point, mask `0x202400b`, 100 %) must answer status 0;
then the test ITSELF submits the goal — type 4, the point, activity `0x13`, tolerance `-1.0`
(`0x104994a0`) — through `SetGoal 0x102ecd20`, and answers its result. Six call sites, all of
the same shape — threat = `GetEnemy()` **or the NPC itself when it has none**, eye from the
threat's slot `+0x304`; success → `m_flMoveWaitFinished (+0x5cf0) = curtime + operand`,
`TaskComplete`, and the node search never runs; failure → the node search above: base `0x58`
`TASK_FIND_COVER_FROM_ENEMY` (`10283580`), `0x59 TASK_FIND_LATERAL_COVER_FROM_ENEMY`
(`1028376b`), `0x0e TASK_GET_PATH_TO_GOAL` operand 2 (`10284b9b`, threat = the goal entity
`+0x5df4`); Troika `0xa0 TASK_FIND_FAST_COVER_FROM_ENEMY` (`102a2111`), `0xa1
TASK_FIND_FORWARD_COVER_FROM_ENEMY` (`102a2363`), `0x115 TASK_FIND_COVER_FROM_LASTENEMY_LKP`
(`102a66bc`). Case ownership read from the two `StartTask` jump tables (`case_of.py`).

**`0x0e` operand 2 drops the cover position.** Task `0x58` copies the search's `out` into its
goal record (`102835fa`…, type 6, activity `0x13`); the `0x0e` arm does not — after a
successful `FindCoverPos` (`out` = `[ESP+0x7c]`) it submits the record built at the head of the
case (`10284ae8`…): type = `+0x5e04`, target entity = `+0x5df4`, destination still the default
vector `DAT_1093404c` = `(FLT_MAX, FLT_MAX, FLT_MAX)` (static init `0x102ec960`, `0x7f7fffff` ×3
— the SDK's "no destination"; its neighbours `DAT_10934060..68` are the zero arrival direction,
`0x102ec9b0`, and `DAT_10923a30` = `0x450` the default arrival activity, `0x10280bf0`). The hint IS claimed and the node cooldown stamped
by the search, but the route goes wherever that record's type sends it, not to the cover node.

It is dead weight in shipped data: the string `TASK_GET_PATH_TO_GOAL` occurs once in
`vampire.dll`, its registry row — no schedule text lists the task.

**Unrecovered:** nothing in this section.

### The back-away and shoot-node searches, walked — `0x10300b50`, `0x10302e50` (2026-09-19, 0018 story 9)

_Two independent opencode walks, diffed (their x87 reads agree throughout); slot 549, the call
sites and their task ids re-read from the DLL here._

**Back-away — `FindBackAwayNode`: `0x102edae0(nav; threat*, minDist, maxDz, out*)` →
`0x103008f0` → the recursive `0x10300b50`.** No graph → DevWarning, false. The NPC's origin and
the threat are both bound with `0x102f3c10` (an unbound threat falls back to the start node and
is never read again). It is a depth-first walk with NO depth limit, guarded only by a visited
bitset marked on entry, over each node's links in plain adjacency order, through the FULL link
predicate `0x102ff960`. For a far node at hull position `c`, with `d0 = |cur − threat|²`:

1. `|c.z − npc.z| <= maxDz`;
2. `|c − threat|² > d0` — strictly further from the threat than the node we stand on;
3. `|c − npc|² < |c − threat|²` — strictly nearer the NPC than the threat;
4. if `|c − threat|² < minDist²` → not far enough yet: RECURSE into it, no stand test;
5. else, **when the NPC has no cached Troika self-pointer (`npc+0x98 == NULL`, `10300d29`…
   `10300d31 JE 0x10300d9c`) the node is the answer with NO stand test** (added 2026-09-20);
6. else the stand test `0x102a0ed0` (`CAI_BaseNPCTroika::CanStandAt`: `m_bForceNPCCheck = 1`
   around `CAI_MoveProbe::CheckStandPosition 0x102e7270`, mask `0x202400b`): standable → **this
   node is the answer**; not → recurse. The probe is one hull trace from `z + 0.1`
   (`0x104493d0`) down to `z −` slot 523 (`+0x82c`: **36.0** on the Troika line `0x101aa670`,
   18.0 base, 40.0 test hull), with a foot box of `0.75·mins + 0.25·maxs … 0.25·mins +
   0.75·maxs` in x / y (`0x10462958`, `0x10449260`, doubles) and zero height at `mins.z`;
   standable iff `fraction != 1.0` AND the entity's slot 166 (`+0x298`, the SDK's
   `CanStandOn`) agrees. Slot 523 is the STEP-DOWN height — the ground test `0x102e4f50` loads
   it into the step record beside slot 522's step height (§ "The route helpers") — not the
   `GetMaxJumpSpeed` an earlier pass of `shape.md` called it.

The first admitted node in DFS pre-order wins; no score, no RNG, and it WRITES NOTHING — no
cooldown stamp, no hint claim. Callers: Troika `0x5a TASK_FIND_BACKAWAY_FROM_SAVEPOSITION`
(`102a1c98`: threat = `m_vSavePosition`, `minDist` = slot 418 of the operand, `maxDz` 64.0;
search failure `TaskFail(7)`, route failure `0x0c`); the base arm (`10282f00`: min 0, `maxDz`
30000; no enemy `TaskFail(6)`); `0x87 TASK_FIND_FOLLOWER_BACKAWAY_NODE` (`102a3013`: threat =
the follower boss, `minDist = +0x6488 − 10`, `maxDz` 50000, `TaskFail(7)`); and the
MingXiao-tentacle and `0x103acba0` species arms.

**Shoot node — `FindShootNode`: `0x102ed9c0(nav; p1, p2, minDist, maxDist, cooldown, dir*,
losOnly, out*)` → `0x10302e50`** (`0x102edaa0` is the same with a zero `dir`). It is the
search behind flanking AND the `GET_PATH_TO_*_LOS` tasks. No graph → false, silently; no
nearest node → DevWarning. The same best-first frontier as the cover search (keyed by squared
distance from the NPC, the link order rotated by its own global `DAT_10934148`, expansion
through link-off and capability ONLY, no enqueue gates), but the start node is never a
candidate, and the tests are:

1. with a non-zero `dir`: `dot(p1 − node, dir) >= 0`;
2. `node+0x9c <= curtime` — the cooldown, tested at the POP here (cover tests it at enqueue);
3. not a climb node (type 4);
4. `minDist < |node − p1| < maxDist`, 3-D, both strict;
5. line of sight to `p2`. `losOnly == 0`: slot 549 (`+0x894`, `0x1028b0b0` on all 64 Troika
   classes) — the hint-GROUP gate: an NPC with a hint group admits only a node whose hint
   carries the same `Group` — then the weapon-aware LOS slot 562 (`0x1026fbe0`,
   `WeaponLOSCondition`, walked in `npc-ai/senses.md`: the active weapon's slot `+0x5b0`, else
   the innate arm slot 573 under capability `0x20000`, else false; then capability
   `0x10000000` vetoes when the player stands in the 0.92 spread cone).
   `losOnly != 0`: a plain eye-height trace (`0x1026ff00`, mask `0x46004003`);
6. the node's hint, if any, must be fully USABLE — `0x102d14c0`: not disabled, not cooling
   down, no live owner. **This is the distinction from cover, which tests the owner alone.**

Success stamps `node+0x9c = curtime + cooldown`, takes the hint (`m_pHintNode`, claim
`0x102d1350`, return ignored) and stores the node id in `DAT_10934148`. No RNG.

Callers: `0xa3 TASK_FIND_FLANK_NODE_TO_ENEMY` (`0x102a24b1`) — `p1` = the enemy's LAST KNOWN
POSITION from the enemy memory (`0x102dfed0`), `p2 = p1 +` the enemy's view offset, range from
the active weapon (`min(+0x8b8, +0x8bc)` … `max(+0x8c0, +0x8c4)`, else 0 … 2000) capped at
`m_flDistTooFar`, cooldown 1.0; when the enemy was seen under 2.0 s ago (`0x10452dc4`) the
DIRECTED form runs with `dir` = the enemy's facing, else the plain one; no enemy `TaskFail(6)`,
no node **`TaskFail(0xb)`**; on success a type-4 goal whose `SetGoal` result is ignored.
`0x81` (`102a415a`) fails silently. `0x11f TASK_GET_PATH_TO_SAVEPOSITION_LOS_NOATTACK`
(`102a706f`) is the one `losOnly = 1` caller (0 … 4096 from `m_vSavePosition`). The base
class's four sites, by `StartTask` jump-table case (`case_of.py`), all through the zero-`dir`
form, `losOnly = 0`, cooldown 1.0, the weapon range of the flank task (`min(+0x8b8, +0x8bc)`
… `max(+0x8c0, +0x8c4)`, else 0 … 2000, capped at `m_flDistTooFar +0x5de4`), no node →
`TaskFail(0xb)`:

| site | task | `p1` | `p2` |
|---|---|---|---|
| `10284678` | `0x11 TASK_GET_PATH_TO_ENEMY_LKP_LOS` | the enemy's last known position | `p1 +` the enemy's view offset |
| `10284db9` | `0x0e TASK_GET_PATH_TO_GOAL`, operand 1 | the goal position `+0x5df8` | the goal entity's eye (slot `+0x304`), else `p1` |
| `102855b3` | `0x14 TASK_GET_PATH_TO_ENEMY_LOS` | the enemy's slot `+0x364` position | the enemy's eye |
| `1028584b` | `0x1e TASK_GET_PATH_TO_SAVEPOSITION_LOS` | `m_vSavePosition +0x5dd0` | `p1 +` the NPC's OWN view offset |

`0x0e` switches on its operand as an int: 0 walks straight to `+0x5df8`, 1 is the shoot node,
2 the lateral-then-node cover above; any other value `TaskFail(0xc)`. Reach, by the task-name
strings in the DLL's schedule texts: `0x11` and `0x14` are each listed by one schedule; `0x0e`
and `0x1e` by none.

**The species arms (2026-09-19; two opencode walks diffed, disputes re-read).** All pass
`maxDz` 30000 and none stamps or claims anything.

- **`CNPC_VScurrying` — `0x14a TASK_VSCURRYING_FIND_EVADE_PATH`** (`StartTask 0x103ac740`,
  site `103ac8a1`; schedule `SCHED_VSCURRYING_EVADE`) through the helper
  `0x103acba0(threat*, dist, out*)`. Threat = the frightening entity's origin (`+0x667c`) with
  `dist = 2 × m_flDetectionDistance (+0x6690)`, or, once that handle is dead, the remembered
  point `+0x6680` with `dist = m_flFrightDistance (+0x6698)`; a dead handle past its time
  `+0x668c` is `TaskFail(6)`. **Node found:** up to FIVE jittered candidates around it, first
  one that `IsAreaClear` (mask `0x202400b`) wins. The jitter is two `RandomFloat` draws per
  try, the y offset drawn FIRST and the x offset second: with `d = threat − node`, the axis
  of the larger `|d|` (y on a tie) gets `(0, 60)` when `d > 0` on it and `(-60, 0)` otherwise
  — toward the threat's side of the node, as both walks read it — and the other axis gets
  `(-80, 80)`; `z = node.z + 0.5 × StepHeight`. Five failures
  return the bare node position; this arm never fails. **No node:** a straight retreat — hull
  trace (`m_eHull`, mask `0x202400b`) from the origin raised `0.5 × StepHeight` along
  `normalize(origin − threat).xy × dist`; blocked (`fraction < 1`, or either solid byte) →
  `dir += 0.5 × hitNormal`, renormalise, `dist ×= 0.5`, again while `dist > 1.0`; clear →
  the end point; else false → `TaskFail(7)`. The task then submits a type-4 goal (activity
  `0x13`, tolerance `-1.0`); a refused goal is `TaskFail(0xc)`.
- **`CNPC_VMingXiaoTentacle`**, `minDist` 512 everywhere: `0x14f …FIND_EVADE_PATH`
  (`1039c975`: from the enemy, `TaskFail(6)` without one; an INLINE copy of the jitter above
  with its own StepHeight 9.0; success arms `m_flUpdateEvadeTimer = curtime +
  RandomFloat(1, 2)`); `0x152 …FIND_SCATTER_PATH` (`1039d042` / `1039d067`: with an enemy
  nearer than 512 (`d² < 262144`) the threat is the blend **`(3 × enemyOrigin +
  m_vecScatterCenter) × 0.25`** — the raw enemy origin, re-read at `1039cf65`…`1039d023`; one
  walk had the enemy-relative vector there — else the scatter centre; the bare node, no
  jitter; success sets the timer `curtime + 600`); and a re-plan inside `RunAI` (`1039e551`:
  timer elapsed, a type-4 goal live, enemy within 256). Failures: `7` no node, `0xc` refused
  goal.

**Unrecovered:** nothing in this section. (The weapon's own LOS slot `+0x5b0` is one body for
all 169 weapon vtables — `senses.md` § "`WeaponLOSCondition`".)

### The two hull words — `m_eHull` `+0x1568` and the pathing hull `+0x156c` (2026-09-20, 0018 story 3)

_Four independent opencode walks over `vampire.dll`: one from the setter out, one from the hull
table down, one per-class from each constructor forward, and a byte-level tie-break. The six
species values are unanimous across all four; the Tzimisce value and the `GetPosition` call-site
split were settled by the tie-break, which quoted the bytes._

**A constructor writes the hull, which is why the field ledger sees no writer.** The ledger does
not record constructor accesses, so ~24 classes that store these fields were invisible to it and
the earlier note "only six classes carry a witnessed store" was wrong. The stores were found by
scanning `.text` for the displacements `68 15 00 00` / `6c 15 00 00` (183 hits, 46 stores) —
a parallel scan for `lea/add/sub reg, 0x1568/0x156c` finds none, so the set is exhaustive.

`CBaseCombatCharacter::CBaseCombatCharacter 0x10326de0` stores **23** into both
(`0x10327295 b8 17 00 00 00` → `0x103272ce` / `0x103272d4`) — one past the 22-row table, an
"unassigned" value and not a sentinel row. It never survives: `CAI_BaseNPC`'s constructor
`0x1027c300` zeroes both at `0x1027c574`/`0x1027c57a` before any derived constructor runs. It
would fault if it ever reached a reader: the accessors index the pointer table raw —
`FUN_102d6100(i) = (&PTR_DAT_1060a750)[i] + 8`, mins `+8`, maxs `+0x14`, small `+0x20`/`+0x2c`,
bit `+0`, name `+4` — with **no bounds check anywhere**. The 22-row bound comes from code, not
from the table: `CAI_Node::InitLinks` loops `cmp esi,0x16; jl` (`0x102fbee0`) and the test-hull
cursor wraps at `0x16` (`0x102f7a90`).

| Class | ctor | `+0x1568` stands | `+0x156c` paths |
|---|---|---|---|
| `CNPC_VCamera` | `0x10368060` @`0x1036807e` | 7 TINY_CENTERED | 7 |
| `CNPC_VWerewolf` | `0x103ca4b0` @`0x103ca5cc` | 12 WEREWOLF | 12 |
| `CNPC_VGargoyle` | `0x10377a60` @`0x10377a93` | 14 GARGOYLE | 14 |
| `CNPC_VScurrying` (and `CNPC_VRat`, which has no ctor of its own) | `0x103abb00` @`0x103abb22` | 19 RAT | 19 |
| `CNPC_VManBat` | `0x10389cc0` @`0x10389d56` | 20 MANBAT | 20 |
| `CNPC_VSheriffMan` | `0x103ae3e0` @`0x103ae463` | **21 SHERIFF** | **0 HUMAN** (never written) |
| `CNPC_VHengeyokai` | `0x1037e680` @`0x1037e786` | **0 HUMAN** | **18 HENGEYOKAI** |
| `CNPC_VMingXiao` | `0x10390ee0` @`0x10390f78` | **15 MING_XIAO** | **16 MING_XIAO_PATHING** |
| `CNPC_VTzimisce` | `0x103b6c60` @`0x103b6d32` | 10 TZIMISCE1 | 10 |
| `CNPC_VTzimisceHeadClaw` | `0x103c1220` @`0x103c127d` | 11 | 11 |
| `CNPC_VTzimisceRunner` | `0x103c2fa0` @`0x103c2ff3` | 13 | 13 |
| `CNPC_VMingXiaoTentacle` | `0x1039afe0` @`0x1039b008` | 17 | 17 |
| `CNPC_Crow` (in `Spawn`, not the ctor) | `0x10357440` @`0x10357493` | 4 TINY | 4 |
| everything else under `CAI_BaseNPC` / `CAI_BaseNPCTroika` / `CNPC_VHuman` / `CBasePlayer` | — | 0 HUMAN | 0 |

`CNPC_VTzimisce` is decidable from the bytes — `0x103b6d27 b8 0a 00 00 00` (`mov eax,0xa`) feeding
both stores; a reading of `0xb` is HeadClaw's value at `0x103c1278`.

**`+0x156c` decides the route; `+0x1568` decides the box.** `CAI_Navigator::SetGoal 0x102ecd20`
reads `[npc+0x156c]` at `0x102ecd2c` (`8b 88 6c 15 00 00`) into the navigator's `+8` and the route
object's, refreshed every frame by `CAI_Navigator::Move 0x102eff40` at `0x102effe1`; the whole A*
family (`0x102fd240`, `CAI_Pathfinder::HasPathInner 0x102fe150`, `0x102fe9f0`, `0x102fef30`,
`0x10301010`, `0x103005f0`/`7e0`/`8f0`/`b50`, `0x10301720`, `0x10302320`, `0x102fcbd0`,
`0x102fcd00`, `0x102f1900`, `0x102fce80`, `0x102fcfe0`) then hands that cache to
`CAI_Node::GetPosition 0x102fb0d0` as its hull argument. 114 call sites reach `GetPosition`
through the single thunk `0x1000d99a`.

**Seven of those sites pass `m_eHull` instead**, and none of them is node-graph routing: patrol-path
goal anchoring in `CAI_BaseNPCTroika::StartTask`/`RunTask` (`0x102a3a86`, `0x102aa6b6`,
`0x102aa8df`), `CNPC_VZombie::StartTask 0x103dff20`, the worker of
`CAI_Pathfinder::BuildExtrapolatedRoute` (`0x10300211`), debug route drawing (`0x10275a04`) and the
network node collector `FUN_10310d70`. So a Sheriff **routes on the human mesh while standing on a
20 × 100 box, and has his patrol goals anchored at Sheriff-hull node offsets**. Why
`BuildExtrapolatedRoute` reads the standing hull while its callers cache the pathing one is
unrecovered — the bytes state the split, not the intent.

**`0x102d7730` is not the NPC hull setter.** It writes both fields from its one argument and
tail-jumps `SetHullSizeNormal(force=1)`, but it has zero direct callers; its only thunk is reached
from `CAI_Node::InitLinks 0x102fb4e0` and the hull-bumper `0x102f7a90`, both driving the
`CAI_TestHull` singleton with a loop counter. No NPC path touches it.

**Nothing outside code sets a hull.** `m_eHull` carries `SAVE` only — no KEY flag, no external
name — and `CNPCMaker` re-parses keyvalue text per child with no field copy, so a map cannot carry
one. `+0x156c` is in no datamap at all, so a restored NPC's pathing hull is always its class
default even when the standing hull was task-modified at save time.

**When the box is applied**: `CAI_BaseNPCTroika::SetModel 0x10298ce0` calls
`SetHullSizeNormal(force=1)` right after the model loads, inside `CAI_BaseNPCTroika::Spawn
0x10298d30`. `SetHullSizeNormal 0x10273070` first resolves the hull's bit and `DevMsg`s
`"%s is using hull %s which has not been precached."` when it is outside the class's slot-337 set.

**Slot 337 `GetUsedHullBits` is a precache declaration, not the stand hull**, and the two must not
be conflated — the values coincide for most species, which is exactly the trap. Only a witnessed
write counts.

**Unrecovered:** the source-level name of `+0x156c` (no datamap record, no string; "pathing hull"
is synthesised from its readers and the `*_PATHING_HULL` rows); the enum spelling of 23; which
shipped schedule issues `CNPC_VVampireBoss::StartTask`'s task `0x14e`, which sets both fields to 0
after a monster-model swap and is reachable from `CNPC_VSheriffMan::StartTask 0x103aec70`'s default
branch; the intent behind the `BuildExtrapolatedRoute` split.

### Doors and NPC-clip, the retail contract (2026-09-19, 0018 stories 3 and 7)

_Read here from the DLL (mask immediates, `CTraceFilterNav`, the two Troika collide vetoes) and
from the installed files (`brush_contents_census.py`, `door_link_census.py` under
`$ELYSIUM_WORK_ROOT/opencode/re18/`), then cross-checked against the two opencode walks._

**The masks are the SDK's, bit for bit.** Every `PUSH` of them in the AI code:

| mask | SDK name | contents | where |
|---|---|---|---|
| `0x2000b` | `MASK_NPCWORLDSTATIC` | SOLID, WINDOW, GRATE, **MONSTERCLIP** | all 16 pushes in the image: 14 at graph build — the neighbour pass `0x102fa630` (4) and `InitLinks` (10, `102fb706`…`102fbe4b`) — and 2 at RUN time, both engine ray traces: `CAI_BaseNPCTroika::GatherConditions` (`102b2f49`) and `0x102c36d0` (`102c392c`). Corrected 2026-09-20: an earlier pass said "graph rebuild only" |
| `0x2400b` | `MASK_NPCSOLID_BRUSHONLY` | + MOVEABLE | the local-route ground worker's RETRY literal (`10303b30`) — its first probe is COMPUTED, `0x2400b | ((~moveMask & 0x40) << 19)` (`1030393a`…`10303961`), i.e. `0x202400b` whenever move-mask bit 6 is clear (§ "The route gates"; corrected 2026-09-20, this row used to list it as the literal) — `CanFitAtNode` (`102f3ccb`, `102f3df1`), the four hint line traces, the stale re-probe's retry |
| `0x202400b` | `MASK_NPCSOLID` | + MONSTER | the nearest-node connection ray, the jump worker, the stale-link probe, the motor's own moves |
| `0x2600b` | — (VtMB) | `0x2400b` + the pedestrian volume bit `0x2000` | the path simplifier's shortcut test `0x102f06e0`, ONLY while the path's pedestrian byte `path+1` is set (`102f06f4`). Its two callers are the simplifier's forward scan `0x102f0ab0` and its skip-the-next-waypoint arm `0x102f0e80` (both through thunk `0x100135c5`), so a pedestrian never SHORTCUTS across a painted volume; the motor's own moves stay on `0x202400b`, and nothing read so far stops a pedestrian whose route crosses one (corrected 2026-09-19: an earlier reading had the volume physically stopping the walker) |
| `0x2000` | — (VtMB) | the pedestrian volume bit alone | `InitLinks`' second walk test (above) |

All three movement masks carry `0x20000`, and nothing in the image strips it: **an NPC-clip
brush blocks graph links when the graph is built, and the local route, the node fit and the
motor at run time.** (The maker's `Flag_NPCClip` `+0x66c1` is unrelated — a `CanMakeNPC` gate,
`0x1034b580`.) Witness, brushes whose contents hold MONSTERCLIP and not SOLID: `sp_tutorial_1`
38 (33 also PLAYERCLIP, **5 NPC-only**); `sm_hub_1` 124 (93 and **31 NPC-only**).

**What the probe ignores.** `CTraceFilterNav::ShouldHitEntity` (`0x102e3110`; the ground twin
`0x102e32d0`) drops, in order: an entity in the nav-ignore set (`0x102eb170`, an RB-tree at
`0x10934014`); one failing the standard pass rules against the prober (`0x101d2fc0`); one whose
own `ShouldCollide` (slot 91) or the game rules' group test refuses; one the PROBER vetoes — slot
69 `NavIgnoreCollision 0x1029b180` (slot 68 `0x1029afc0` for the ground twin); then
`CTraceFilterSimple`. The Troika veto ignores: every NPC and the player when
`m_bfAINPCFlags & 0x40`, otherwise only an NPC whose flags1 carry `0x20000`; the prop it is
about to kick; any `CBaseCombatWeapon` (`entity+0xa0`, the weapon's self-downcast — dropped
guns never block a route); an entity whose solid flags carry `0x12` (`0x16` with
`m_bNavIgnorePhysicsProps`); and the `IsIgnoreCollisionEntity` list. **A door is none of these**
— `CBaseDoor` caches itself at `+0xa4`, which no filter reads — so a closed door BLOCKS the
probe as an ordinary entity (`-1`), and the `-3` arm of the ground worker is about NPCs:
`0x10303fd0` re-probes without them only when the blocking NPC's `IRelationType` (slot 404,
`+0x650`) toward the mover is 3, `D_LI`.

**Yet the graph runs through doors.** Against the patch graphs: on `sp_tutorial_1`, 9 links
cross 8 of the 36 door brushes, 6 of them with hull-0 GROUND motion (`49–56`, `15–48`, `91–83`,
`96–97`, `143–191`, `66–15` through `frontgate`); on `sm_hub_1` one link (`220–354`) crosses the
two smoke-shop doors and the other 27 doors have none. Retail's own rebuild made those links
1.8 s into the map with the doors standing, so its `0x2000b` sweeps did not stop on them — and
the mask says why: **`0x4000` MOVEABLE is the bit that makes a trace hit `MOVETYPE_PUSH`
entities (doors, platforms), and the graph-build mask is the only one without it.** Doors are
invisible to `InitLinks` and solid to every run-time probe. The door classes in the image are
`func_door` (`CBaseDoor`), `func_door_rotating` (`CRotDoor`) and `momentary_door`; there is no
`prop_door_rotating`.

Put together, the route contract at a closed door is: the LOCAL attempt fails on it; the node
route is admitted because the link exists; walking it, the motor's probe reports the door to
slot 531 `OnObstructingDoor` (`0x102984a0`, `npc-ai/schedule-kernel.md`), which opens it or
declines; a refusal reaches the door-blocked notice (`0x1027de00`, `npc-ai/senses.md`), which
marks the link stale for 5 or 20 s with the door's handle, and the link predicate then routes
around it until the expiry or a clear re-probe. A door with NO link through it (27 of the hub's
29) is simply a wall to an NPC. At move time the door is identified as `hit+0xa4`, `CBaseDoor`'s
self-pointer (`0x102f06e0`), and opened by `door->AcceptInput("Open", npc, npc)` from the
alternate-AI door transaction `0x10298840`, after the lock test `0x100eec70` (already open, or
spawnflag `0x200` with an NPC user, or the key check, or `m_bLocked +0x55c`); the door keeps
the NPC-failure word `m_bfNpcFailedFlags +0x644` and timer `m_flNpcFailedTimer +0x640` the
notice and the obstruction selector read. (Both opencode walks, reporting after the hand walk,
agree on the masks, the MOVEABLE reading, the `-3` arm and the door chain.)

**Two dispatchers of slot 531, not one** (added 2026-09-19, review against the listing). The
move-time one is the movement sink's base body `0x1027dc10`, which reads the move trace's
obstruction `+0x60`, then `+0xa4`, and calls `[+0x84c]` (`1027dc3f`) — it is what the Troika
override `0x10298340` tail-calls. `0x102f06e0`, cited above as "at move time", is the path
SIMPLIFIER's shortcut test (the `0x2600b` row of the mask table): its hull trace toward a later
waypoint can land on a door the NPC has not reached, and then `102f0820` reads `hit+0xa4`,
`102f0834` runs the lock test (true → the shortcut is refused silently), `102f08c3` calls slot
531; a true answer sets the caller's flag byte (`102f08da MOV byte [ECX],1`), and when the
result word slot 531 filled is also non-zero it raises the navigator failure **`(0x0e, 1)`**
through slot 10 (`102f08e1 PUSH 1 / 102f08e3 PUSH 0xe / CALL [EDX+0x28]`) and sends the
door-blocked notice (`102f08ee`). The shortcut test answers false in every door arm. So a door can be opened, and a route failed with `0x0e`, from
the look-ahead of a shortcut the NPC never takes.

**The port today** (`Map/ElysiumMapCollision.cpp`): the world collider is the `.hulls` sidecar,
pre-filtered to `SOLID|WINDOW|GRATE|MOVEABLE|PLAYERCLIP` — **MONSTERCLIP is dropped**, so the 5 /
31 NPC-only brushes do not exist for the nav mesh, and the mesh is cut at the engine's default
agent rather than hull 0 (26 × 72 units). The pedestrian volumes are not exported either (0018 story 3 stages them as a nav area).

**Nobody fills the nav-ignore set (2026-09-19).** It is the SDK's `CNavPropertyDatabase` (vtable
`0x1049d950`, an auto game system plus an entity listener). Its insert `0x102eaaf0` (thunk
`0x1000c554`) and its explicit remove `0x102eacf0` (thunk `0x10004656`) have no call, jump or
data reference anywhere in the image (byte scan of `.text` for `E8` / `E9` targets and of the
whole file for the four addresses; `xcallers.py`). Only the lookup `0x102eb170` — the two
filter sites `102e3167` and `102e3327` — the entity-deleted listener `0x102ea600` and the
level-shutdown clear `0x102ea4d0` are reachable. The set is always empty and the filter's
first arm never drops anything.

The native witness the spec asks for (story 3, job 5) is
still to run: a closed door with a link through it must not cut the Recast mesh, and an NPC-only
clip brush must.

### The engine's per-brush admission test, and what a non-solid OPAQUE brush does to sight (2026-09-20, 0018 story 3)

_Two independent opencode walks over `bin/engine.dll` (image base `0x20000000`, read with
`pedis.py`; the DLL is not in the corpus MCP) — one down from the BSP brush loader, one in from the
server trace interface. Both reach the same instruction and both answer STOPS. The map half is a
repo probe over the leaf-brush lumps._

This closes what story 3 carried as an inference: 1,764 brushes on 25 maps are `OPAQUE 0x80` yet
NOT `SOLID` (17 on `sp_tutorial_1` as `0x08000080`, 894 on `la_museum_1`), and the port's `--S-`
signature makes them block NPC sight. The mask overlap alone was never proof.

**The mask reaches the brush test unchanged.** `CEngineTrace::TraceRay` (`0x2006a5c0`, slot 4 of
the `EngineTraceServer003` vtable `0x20174864`; `vampire.dll` calls it with `0x02804091` from 40
sites) passes its mask to the world trace `0x200312d0`, which stores it once into the global
`0x209b2d20` at `0x20031460`. That global has **five references in the whole image**: the store and
the four leaf-loop reads. Nothing ANDs, ORs, replaces or splits it into world and entity masks.

**The test is exactly `contents & mask`.** In `CM_TraceThroughLeaf 0x20030aa0`:

```
20030ba1  lea eax, [esi + eax*8 + 0x42d168]   ; brush = &brushes[brushnum]
20030ba8  cmp dword ptr [eax + ecx*4 + 0xc], ebp  ; already clipped this trace?
20030bb2  8b 0d 20 2d 9b 20                   ; mov ecx, [0x209b2d20]   the mask
20030bb8  85 08                               ; test [eax], ecx         contents & mask
20030bba  74 5b                               ; je  skip
```

`0x08000080 & 0x02804091 = 0x80`, so the brush is admitted. **`SOLID` is not required and `DETAIL`
is not rejected** — detail is simply another bit in the word. The box/hull twin
`CM_TestInLeaf 0x20030c70` carries the identical test at `0x20030cc1`.

**The loader copies the map's word verbatim.** `CMod_LoadBrushes 0x200334a0` (named by its own
string `"CMod_LoadBrushes: funny lump size"` at `0x2019c370`) reads lump 18's 12-byte records into
24-byte entries at `bsp+0x42d168`, `contents` at `+0`, `numsides` `+4`, `firstside` `+8`, per-hull
check stamps `+0xc`. `CollisionBSPData_LoadLeafs 0x200331a0` keeps only `firstleafbrush` `+0xc` and
`numleafbrushes` `+0xe` of each 32-byte disk leaf.

**There is no leaf-contents pre-test.** This matters because the leaves listing these brushes have
contents `0`: neither trace arm ever loads a leaf's contents dword, and the leaf gatherer
`0x2002fc50` appends every geometrically reached leaf with no contents filter. The only skips in
the loop are the same-trace check stamp, `numsides == 0`, and a per-side `side+8 != 0` test in the
ray arm that is dead for BSP data — the loader writes `0` there for every side
(`0x20033610 c7 46 08 00 00 00 00`) and nothing else stores to it. `CM_ClipBoxToBrush 0x200305e0`
is a pure plane clip that reads no contents and, on a hit, copies the brush's own word into
`trace_t.contents`.

**The map half** (`hull_table.py`-style probe over lumps 10/17/18 of the patched BSPs): all 17
tutorial brushes and all 894 on `la_museum_1` are listed in the leaf-brush list of at least one
open leaf, so a trace reaches them. None is listed only by solid leaves.

**Verdict: STOPS.** A brush that is `OPAQUE` without `SOLID` blocks a `0x02804091` sight trace in
retail. The port's `--S-` answer is correct as built, and the once-planned live capture at a
tutorial brush is retired.

**Unrecovered:** the two walks name the second per-leaf list differently — a displacement list
(`leaf+0x10`, node contents at `+0x3c`) versus a linked brush list — though both show it gated by
the same mask global; which of the two lists holds a given world brush at runtime was not traced
back to brush creation, and the admission test is identical either way. The static-prop stage
receives the same mask through `ClipRayToCollideable` (slot 3, `0x20069730`); its hitbox arm was
read only at its call site.

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
erase those distinctions. First-match hint-list order and entity-list order are separate contracts
and stay. Per-node neighbour order is retail's too, but the port does not keep it (0018
§ Navigation boundary, 2026-09-20): cover and the shoot node pop candidates by distance from
the NPC, which needs no link order, and back-away's first-in-link-order pick is a named
difference.

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

**Open after this audit** (closed 2026-09-19 in the sections above and below, the native
door / NPC-clip witness excepted): selected graph loading/rebuild, complete nearest-node cache/fly and
local-route gates, alternate movement-route costs/waypoints, `GET_PATH_TO_HINTNODE` /
`SNAP_TO_HINT`, the cower search through slot `0x688`, and the upstream route used by
`TASK_PATROL_PATH`. The precise geometric constants and some caller-wrapper variants still
need their full walks. Base node/near/far cover registrations and `GET_FULL_PATROL_PATH` have
no shipped schedule witness in this audit; registration alone does not establish required
behavior. Adjacency reads in the witnessed hunt/cover/retreat/flank families are established.
