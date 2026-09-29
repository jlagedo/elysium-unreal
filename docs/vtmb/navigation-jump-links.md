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
NumNodes`; otherwise `DAT_106c994c++` and the hint keeps the out-of-range id. So the pairing of
BSP node rows to AIN nodes is positional, and it is only right when the AIN was built from that
BSP. **Which rows move the counter is closed (2026-09-29, 0018 story 4; § "The place set,
landed" below):** not every node-typed row — the four standalone classes (`info_hint`,
`info_node_kick_over`, `info_node_kick_at`, `info_node_shoot_at`) never advance it and take id
-1; every other node row advances it exactly once, whether or not it makes a hint, and
`DAT_106c994c` counts only a hint that was made.

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

**Corrected 2026-09-29 (0018 story 5, the sections below):** (1) `nav+0x44` (`m_timePathRebuildDelay`), the retry
interval `0x102f1dc0` adds to the next-try time, is NEVER written non-zero: its only store is the zero in the reset
`0x102f28a0`, and the constructor `0x102eca50` skips `+0x40..+0x4c`, so a failed find retries on every later think until the
`nav+0x40` deadline raises `0x0c`. (2) `0x102ee2e0` reads the byte `path+0x10` (`CAI_Path::m_bPaused`), not "goal active"
(`IsGoalActive` is `0x102ee6a0`, `path+0x24 != 0`; `IsGoalSet` is `0x102ee680`, goal type non-zero). (3) The blocked-move rule's
`& 8` in `0x10303850` is a route-control literal, `BuildLocalRoute`'s fourth argument stamped on each waypoint at `+0x28` ("this
leg ends at the goal"), NOT goal word `+0x24`; story-5 correction *(c)*'s "goal flag `0x8`" was a misread, and the port needs no
goal-flag input for it.

**Unrecovered:** nothing in this section.

### The goal types, their issuers and the goal record (2026-09-21, 0018 story 5)

_A Codex worker's walk (`$ELYSIUM_WORK_ROOT/codex/re2/wp03-goal-types`); `DoFindPath`'s switch and
the type-9 issuer re-read by the lead._ `DoFindPath 0x102f2330` switches on the path's goal type
(`path+0x5c`, read through `0x100113d8` — which is therefore the goal-TYPE getter, zero meaning
"no goal"), filled by `SetGoal 0x102ecd20` from goal word `+0x00`. Names are the SDK's where the
behaviour matches and contextual otherwise; no retail enum strings exist.

| Type | Name (source) | What `DoFindPath` prepares | Tolerance in `SetGoal` | Issued by |
|---:|---|---|---|---|
| 0 | none | nothing; the default arm fails | — | only `GET_PATH_TO_GOAL 0x0e` replaying an unset stored type (`+0x5e04`) |
| 1 | target entity (SDK) | the target's origin (slot 217) | half the sum of the two hulls | base `StartTask 0x102827f0` call site `0x10282fec`: `WALK_TO_TARGET 0x08`, `RUN_TO_TARGET 0x09`, `SCRIPT_CUSTOM_MOVE_TO_TARGET 0x0a`; `0x0e` replaying a stored 1 |
| 2 | enemy (SDK) | the enemy's last known position (`0x102dfed0`) | half the sum of the two hulls | `GET_PATH_TO_ENEMY 0x0f` — base arm and Troika's own (`0x102a352c`); `0x0e` replaying a stored 2 |
| 3 | path corner / goal-entity chain (SDK) | walks `m_pGoalEnt (+0x5de8)` through slot `+0x2b0` (next target), up to `0x80` entities, one waypoint each at slot 220's point; **the only arm that never calls the route builder `0x102f2060`** | as given | `ScheduledFollowPath 0x102801e0` (type hard-coded at `0x10280205`; the caller's argument is the ACTIVITY) from the scripted-schedule modes 4 / 5 (`0x101a98c0`); Troika task `0x120 GET_PATH_TO_PATHCORNER` |
| 4 | location (SDK) | none — the vector at `+0x04`, or node `+0x10` when the vector is the no-destination sentinel | as given | nearly everything: 27 call sites in the base `StartTask`, 19 in Troika's, the patrol pair `0x102aa640` / `0x102aa860`, `ScheduledMoveToGoalEntity 0x102800c0` (type hard-coded at `0x102800ec`), the vector / wander / random wrappers `0x102ed610` / `0x102ed540` / `0x102ed940` / `0x102ed820`, and the Ming Xiao, tentacle, scurrying and zombie species arms |
| 5 | (SDK candidate: location-nearest-node) | none; falls to the builder with 4, 6, 9 | as given | **no issuer**: none of the 15 functions that reach `SetGoal` (through its thunk `0x1000ce64`) builds a record with type 5, and no schedule text names a task that would. The arm is dead by content; port it as 4's twin or leave it a seam — either is unobservable |
| 6 | cover (behaviour) | none | as given | the base and Troika cover / flee / cower / flank arms; `CAI_StandoffBehavior 0x102c7bd0` (`0x102c7d09`) |
| 7 | best unknown (task string `0x105d4920`) | the object at NPC `+0x98` answers slot 586 — `m_hBestSeeUnknown`; that entity's origin becomes the destination, the NPC's translation hook (slot `+0x8cc`) adjusts it, and **path byte `+0x00` is set to 1** | half the sum of the two hulls | Troika task `0x79 GET_PATH_TO_BESTUNKNOWN` (`0x102a36ba`), named by six shipped texts: `SCHED_TROIKA_HUNT_INVESTIGATE_UNKNOWN`, `…_INVESTIGATE_UNKNOWN_OTHER`, `…_OTHER_RUN`, `SCHED_VFRENZYSHADOW_HUNT_INVESTIGATE_UNKNOWN`, `SCHED_VHENGEYOKAI_INVESTIGATE_UNKNOWN_ATTACK`, `SCHED_VTZIMISCE_HUNT_INVESTIGATE_UNKNOWN` |
| 8 | interesting place, pedestrian | none but **path byte `+0x01` = 1, the pedestrian byte** | as given | Troika task `0xa5 GET_PATH_TO_INTERESTING_PLACE` (`0x102a76a4`); no held place is `TaskFail(0x22)` |
| 9 | interesting place, animal | none — and NO pedestrian byte, so an animal walks to its place without the roadway pricing | as given (the arm passes `-1.0`, `_DAT_104a8730`) | `CNPC_VAnimal::StartTask 0x1035f650`, the same task id `0xa5` overridden: `m_vecInterestingPlace` with activity `-1`; no place `TaskFail(0x22)`, a refused route `TaskFail(0x0c)`, a granted one `TaskComplete` at once. Its texts are the animal, dog, scurrying and zombie families' (`0x1062beb0`, `0x10637080`, `0x1064fd80`, `0x10664d18`) |

So 7 and 9, which the story listed without a meaning, are both reached by shipped schedules; 5 is
reached by none.

**The goal record `SetGoal` reads** — sixteen words: `+0x00` type; `+0x04..+0x0c` destination;
`+0x10` destination node id; `+0x14` movement ACTIVITY (copied to `path+0x2c`); `+0x18` arrival
activity; `+0x1c` arrival sequence; `+0x20` tolerance; `+0x24` goal FLAGS (copied to
`path+0x60`); `+0x28` target entity; `+0x2c..+0x34` arrival direction; `+0x38` extrapolation
time; `+0x3c` the bright-route penalty. **The `0x13` many ledger rows call a "flag" is the activity
word `+0x14`, not a flag** (`checklist-19-29.md` rows `0x10278220`, `0x102c7bd0`, `0x1039c4c0`,
`0x103ac740`, and `0x102800c0` / `0x102801e0`, whose "caller-supplied goal type" is the activity).
Flag bits with a proven reader: `0x1` — `SetGoal` (`0x102ed161`) turns the motor toward the
destination at once; `0x2` — `SetGoal` (`0x102ecf27`) takes the explicit node route (nearest node,
destination node, `0x102fd240`), bypassing the local / retry find; `0x4` — `UpdateTargetPos
0x10271b10` re-paths a type-1 goal when its target moves (and the comfort sweep's test,
`conditions-and-states.md`); `0x8` — shifted into `0x102f1dc0`'s second argument
(`0x102ed121`), where nothing recovered branches on it.

**Corrected 2026-09-29 (0018 story 5, the sections below):** goal flag `0x2` has exactly ONE issuer, Troika task `0xc7
`GET_PATH_TO_ENEMY_CLOSEST` (`0x102a4812`; `SetGoal` at `0x102a4921`), reached by shipped data through
`SCHED_VTZIMISCE_ATTACK_CLOSEST`; flags `0x4` and `0x8` have no issuer (`0x8` is also inert end to end: `DoFindPath` never
reads it). The goal-state word `0x102ee620` is the goal TYPE (`path+0x5c`), and `ValidateNavGoal`'s `== 6` is a cover goal, not a
"goal state"; `nav+0x18` (`0x1027d990`) is the nav TYPE, a different word. See "Route-control flags, their issuers and the
blocked-move arms" and "The navigator's words and getters" below.

**Failure codes by type.** A refused route is `0x0c` for every type, through `0x102f1dc0` →
`OnNavFailed 0x102eeae0` (`npc-ai/schedule-kernel.md` § "`SetGoal` DOES complete the task").
Type-specific codes are raised by the issuing ARM before `SetGoal`: `0x1d` no patrol node
(`0x102aa640`; its reissue sibling `0x102aa860` ignores the route result); `0x22` no held
interesting place (8, 9); `0x06` / `0x07` / `0x08` from the cover searches and `0x1b` from
`ValidateNavGoal 0x10280360` when the cover-to-enemy line is clear (6). `0x0e` is not a goal
failure at all — it is the door transaction `0x10290570`.

**Unrecovered:** type 5's name; a writer of goal flag `0x4` on a type-1 record (the 15-caller
census shows the target arm writing flags `0`; the reader is proven); what, if anything, consumes
the `0x8` that `0x102f1dc0` receives; what path byte `+0x00` (set by type 7) is read by. (Type 3's
chain end, closed 2026-09-21 with the `aiscripted_schedule` walk: a null `GetNextTarget` ends the
chain SUCCESSFULLY at the last waypoint, a null initial `m_pGoalEnt` builds nothing and fails, and
a chain longer than `0x80` is truncated, not refused.) The worker also could not tie the `0x8` in the
blocked-move rule `0x10303850` (0018 story 5 correction *(c)*) to goal word `+0x24` — its callers
`0x10304130` / `0x10303d10` pass a literal `8` as a route-control argument; not re-read by the lead.

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

### The navigator's words and getters — CAI_Navigator, walked (2026-09-29, 0018 story 5)

_Read from `vampire.dll` through the corpus (decompile, callers, datamap fields). Navigator words are
`nav+N`; path words are `path+N` where `path = nav+0x30` (`m_pPath`); the NPC's navigator pointer is
`npc+0x5d34` (`m_pNavigator`). Datamap names: `datamap_CAI_Navigator` (`0x102eca20`) and the `CAI_Path`
datamap._

**1. `nav+0x40` and `nav+0x44` — who writes them.**
- `nav+0x40` (datamap `m_timePathRebuildMax`, float) has ONE non-zero writer: `SetRouteSearchTime
  0x102886f0` (`MOV [ECX+0x40],EAX`), whose only caller is `CAI_BaseNPC::StartTask 0x102827f0` (the
  `TASK_SET_ROUTE_SEARCH_TIME 0x50` arm, task string at `0x105d4dc4`, registered by `0x10316ff0`).
- **`nav+0x44` (datamap `m_timePathRebuildDelay`, float) is NEVER written non-zero by any code.** Its
  only store is the zero in the reset `0x102f28a0` (`+0x40`, `+0x44`, `+0x48`, `+0x4c` all `= 0`, then
  clears memory bit `0x20` at `npc+0x5d8c`, then path reset `0x1030bb30`). No task, no ConVar, no
  constructor default: the constructor `0x102eca50` stores `+0x04`, `+0x0c`, `+0x10`, `+0x18`, `+0x1c`,
  `+0x20..+0x30`, `+0x34`, `+0x38`, `+0x50`, `+0x54..+0x64` and does NOT touch `+0x40..+0x4c`; the block is
  `operator_new(0x68)` (`0x1027cf90`) / `(0x6c)` (humanoid, `0x10262430`) from the CRT heap
  (`0x104312d5` -> `__nh_malloc`), which does not zero. So `+0x40..+0x4c` hold whatever the heap gave until
  the first reset. The only other way a value reaches them is a save restore through the datamap fields
  (the fields exist in `datamap_CAI_Navigator`, float-typed), which can only carry back what was written.
  `+0x44` is therefore effectively the constant 0: the retry interval is a real WORD in the layout and a
  real read in `0x102f1dc0`, but retail never gives it a value. (`vtmb_readers` resolves no typed access
  for either offset; the reads found are all in `0x102f1dc0`.)
- The reset `0x102f28a0` is reached from exactly two places: `SetGoal 0x102ecd20` when its flag argument
  has bit `1` (`thunk 0x1000988b`), and `ClearGoal 0x102ee270` (which then calls slot 7 `0x102eea70`).
  **A `SetGoal` with bit 1 zeroes the route search time `TASK_SET_ROUTE_SEARCH_TIME` wrote**, and every
  `ClearGoal` does too. `SetGoal` itself never writes `nav+0x40` — the `+0x40` store in `0x102ecd20`
  (`FLD hull; FMUL half`) is `path+0x40`, the waypoint tolerance.
- Consequence for `0x102f1dc0`: on a failed find with `nav+0x40 != 0`, memory bit `0x20` is set,
  `+0x48 = curtime + nav+0x40`, `+0x4c = curtime + nav+0x44`. With `+0x44 == 0` the next-try time is the
  failing frame's own curtime, so the very next call (a later frame, the compare is `+0x4c < curtime`,
  strict) retries; the retry runs every think until the find succeeds or `+0x48 < curtime` raises
  `OnNavFailed(0xc, 1)`. A retry that fails again re-arms `+0x4c = curtime + nav+0x44` (i.e. curtime
  again). A retry that succeeds clears bit `0x20` and, if the NPC's slot `+0x844` says not-`0x6e`-state, calls slot 2.
  `path+0x44` is a different word: `-1` written on every find (`0x102f1dc0`, `0x102f2060`, `0x102f2330`)
  and by the path constructor `0x1030bec0`.

**2. The goal-state word `0x102ee620`.**
- `0x102ee620` = `FUN_100113d8(nav+0x30)` = `*(path+0x5c)` = `CAI_Path::m_goalType` (datamap; guard byte
  `m_bGoalTypeSet` at `path+0x58`). It is the goal TYPE, the SDK's `GetGoalType()`. Values: `0` none
  (path constructor `0x1030bec0` and reset `0x1030bb30` both store 0); `1` target entity; `2` enemy; `3` path
  corner / goal-entity chain; `4` location; `5` no issuer; `6` cover; `7` best-unknown; `8` interesting
  place (pedestrian); `9` interesting place (animal) — the table in `navigation-jump-links.md` §"The goal
  types" (`DoFindPath 0x102f2330` switch). `SetGoal` writes it through `0x1030ba50(path, goal+0)`.
- `ValidateNavGoal 0x10280360` requires `== 6`: the goal is a COVER goal. With a non-null enemy (slot
  `+0x29c`) it takes the goal position (`0x102ee140`), lifts it by the NPC's slot `+0x8e4` / `+0x854`
  offsets, traces to the enemy (`+0x304` of the enemy, mask `0x2804091`), and if the trace fraction equals
  `DAT_10449280` (a clear line — the "cover" does not cover) either sets condition `0x39` (when the
  current schedule is interrupted by it) or `TaskFail(0x1b)` (`m_failLine 0xbc`, text
  `AI_BaseNPC.cpp`). For any other type the body returns true untouched.
- Readers of `0x102ee620` (the getter is called through `thunk 0x100037e2`; direct callers found by
  `vtmb_grep`), with the value tested where the decompile shows it: `CAI_BaseNPC::SelectSchedule 0x1028a380`,
  `CNPC_VAnimal::SelectSchedule 0x1035fb50`, `CNPC_VCombatman::SelectSchedule 0x10370230`,
  `CNPC_VMoleman::SelectSchedule 0x1039fd20`, `CAI_BaseNPCTroika::SelectSchedule 0x102af660` (values not
  read here); `CAI_BaseNPC::StartTask 0x102827f0`, `CAI_BaseNPCTroika::StartTask 0x102a1910`,
  `CNPC_VWerewolf::StartTask 0x103ccda0` (values not read here); `CAI_BaseNPC::RunTask 0x10288780` (`!= 0`,
  twice, plus a value passed on), `CAI_BaseNPCTroika::RunTask 0x102aacf0` (`== 0` test),
  `CNPC_VWerewolf::RunTask 0x103cdfb0` (`== 0`), `CNPC_VZombie::RunTask 0x103e01d0` (`== 0`),
  `CNPC_VMingXiaoTentacle::RunAI 0x1039e3d0` (`== 4` behind `0x102ee6a0`); `ValidateNavGoal 0x10280360`
  (`== 6`); `CAI_BaseNPC::UpdateTargetPos 0x10271b10` (`== 1`); `CAI_BaseNPC::UpdateEnemyPos 0x10271900`
  (`== 2`, and it also reads `3` / `1` from `nav+0x18`); `FUN_1028e480` (`== 7`); `FUN_1028e980` (`!= 0`,
  behind a `nav+0x18` not-3-not-1 gate); `FUN_102729d0` `GetNavTargetEntity` (`2`, `1`, `7`, else
  NULL); `FUN_102ecc40` (`2`, `1`, `7` — `pMoveTarget`); `CAI_BaseNPCTroika::UpdatePedestrianInfo
  0x102a0d20` (`== 8`); `FUN_102a0bc0` (`== 8`); `FUN_102a0eb0` (bare read, result unused);
  `CAI_BaseNPC::FUN_10280300` slot 153 -> `IsGoalSet` (`0x102ee680`); `CAI_BaseHumanoid::vfunc585
  0x1025f1c0` and `FUN_102f02a0` (through `0x102ee680`). Also `CAI_Navigator::Move 0x102eff40` and
  `DoFindPath 0x102f2330` read the word through `0x100113d8` directly.

**3. `0x102ee140`, `0x102ee680`, and the two neighbours the port has crossed.**
- `0x102ee140` (nav) -> `0x1000f89e(path)`: returns a pointer to a STATIC 3-float scratch
  (`DAT_10936af0`, shared by every caller — copy it) holding `path+0x4c..+0x54` (`m_goalPos`) minus
  `path+0x34..+0x3c` (`m_vecTargetOffset`), i.e. the SDK's `ActualGoalPosition`. There is no goal test: with
  no goal it answers whatever the two triples hold. After the reset `0x1030bb30` both are `vec3_origin`
  (`DAT_1070d1b0..b8` = 0,0,0 by `staticinit_101370b0`), so the answer is `(0,0,0)` — never "none". The path
  constructor `0x1030bec0` sets goalPos to origin but does NOT write `+0x34..+0x3c` (heap garbage until
  the first reset). `0x1030ba30` is the raw `&path.m_goalPos` (`path+0x4c`, no subtraction), read by `SetGoal`,
  the route builder `0x102f2060` and the sink slot `0x102ef760`.
- `0x102ee680` = `0x100113d8(path) != 0` (goal type non-zero): it is the SDK's `IsGoalSet`, NOT
  `IsGoalActive`. The word `IsGoalActive` names is `0x102ee6a0`: `nav+0x30 != 0 && path+0x24 != 0` — a
  CURRENT WAYPOINT exists (`path+0x24` = head of the waypoint list). `0x102ee2e0` is neither: it reads the
  BYTE `path+0x10` (`CAI_Path::m_bPaused`), set to 1 by `0x102ee2a0` (`0x1030be80`) and cleared by `0x102ee2c0`
  (`0x1030bea0`).
- Other path words the NPC reads through the navigator: `0x102ee1a0` `path+0x28` goal tolerance (setter
  `0x102ee1c0`); `0x102ee3f0` `path+0x2c` movement ACTIVITY (setter `0x102ee250`; path constructor and reset
  store 1); `0x102ee640` `path+0x60` goal flags; `0x102ee160` `path+0x30` target handle resolved through the
  entity table (NULL when `-1` or stale); `0x102ee5e0` -> `0x10012805` current waypoint position; `0x1027d990`
  is `npc+0x5d34 -> nav+0x18`, the NAV TYPE.

**4. `0x102eee40` — navigator vtable slot 17, the per-waypoint move-info block.**
- It is the SDK's `MoveCalcBase(AILocalMoveGoal_t*)`: it fills a sixteen-word block `B` (the caller
  zero-fills it first). Fields, in order:
  `B[0..2]` target = current waypoint position, `0x10012805(path)` (`path+0x24` head waypoint's
  `+0x00..+0x08`, or the static origin waypoint `DAT_10936afc` when `path+0x24 == 0`);
  `B[3..5]` dir = target minus the NPC's origin (`npc` slot 220, vtable `+0x370`), then normalised IN PLACE:
  `nav+0x18 == 0` (ground): `dir.z = 0` and `0x102e5d00` (2D normalise) ; otherwise the 3D normalise
  (`PTR 0x1057966c`); `B[6..8]` facing = a copy of the normalised dir; `B[9]` speed = `0x102e12c0(nav+0x20)`
  (motor -> NPC vtable `+0x3e0`, the ground speed); `B[10]` maxDist = the length the normalise returned;
  `B[11]` curExpectedDist = `motor+0x30` (the move interval) times speed, clamped to `B[10]`;
  `B[12]` navType = `nav+0x18`; `B[13]` pMoveTarget = `0x102ecc40(nav)` (goal type 2 / 1 / 7 -> the NPC's
  `GetNavTargetEntity 0x102729d0`; any other type -> the `path+0x30` handle resolved); `B[14]` flags,
  OR-ed: bit `1` (`TARGET_IS_GOAL`) when the head waypoint is the goal (`0x1030bd50`: `(wp+0x28 >> 3) & 1`,
  0 for no head); else bit `4` (`TARGET_IS_TRANSITION`) when the head has a next waypoint (`wp+0x30`) whose
  activity (`+0x2c`) differs from the head's; `B[15]` pPath = `nav+0x30`. It does not null-check
  `path+0x24` on the flags arm (reads `*(0+0x30)` if the head is null and the goal bit is clear) — callers
  reach it only behind the active-route gate `0x102efd50`.
- Callers: `CAI_Navigator::MoveNormal 0x102efaa0` (`+0x44` dispatch after zeroing `0x1f` words and after slot 16
  `0x102ef510` answered false) and `CAI_HumanoidNavigator::vfunc12 0x10264470` (its slot-12 override of
  `MoveNormal`, `+0x44` dispatch with an extra argument). Slot 17 is `0x102eee40` in both the base and the
  humanoid tables (`0x1049d9c4`, `0x10499444`). `MoveJump 0x102eece0` and `0x102eebc0` read the waypoint
  through `0x10012805` directly.
- **The SDK-style route sample the kernel asks for.** Waypoint COUNT and DISTANCE REMAINING have no getter
  in the NPC-side navigator: nothing found reads `nav+0x30` to count the list or sum it; `0x102ee6d0
  GetPointAlongPath(out, dist)` (walks `path+0x24` by `+0x30`, 2D length when `nav+0x18 == 0`, else 3D)
  is read by `CAI_BaseHumanoid::vfunc585 0x1025f1c0` at 144.0; `0x102ef510` (slot 16) measures distance
  to the head waypoint (3D unless ground) against `DAT_10451f78` / `DAT_10449260`; the debug overlay
  `0x102f28e0` draws the list. Current waypoint position = `0x102ee5e0` (readers: `MaintainEyeDirection
  0x1026b810`, `StartTask 0x102827f0`, `0x102ede30`, `0x102f1690`, `0x102f1500`); path type = goal type
  `0x102ee620`; nav type = `nav+0x18` (`0x1027d990`, written by `0x102eeba0` / NPC-level `0x1027d9b0`:
  `0` ground, `1` jump — `MoveJump`, `3` climb — `0x102eebc0`, `2` fly — `0x1038c170`, `0x103580d0`).

**5. The port's nine kernel readers.** (`NavGoalState()` = family Motor's seam, answers -1;
`NavGoalPosition` answers "none"; port names quoted from the comments.)

| Port site | Retail word actually read | Shape it needs | Port stands for | Verdict |
|---|---|---|---|---|
| `ElysiumNpcBaseHelpers.cpp:155` `GetNavTargetEntity` (`0x102729d0`) | `0x102ee620` = `path+0x5c` goal type | int in 0..9; arms 2 (`m_hEnemy +0x5ce0`), 1 (`m_hTargetEnt +0x5ce4`), 7 (`(npc+0x98)` slot `+0x928` handle); else NULL | goal type | right getter; -1 -> NULL is retail's no-goal answer (0) |
| `ElysiumNpcBaseHelpers.cpp:230` `CalcIdealYaw` (`0x10274b30`) | **`0x102ee3f0` = `path+0x2c` MOVEMENT ACTIVITY**, compared to `0x37` and `0x38` | int activity id, default 1 | port calls it `NavGoalState()` | **wrong getter**: not the goal type; the arm choice follows the path's activity |
| `ElysiumNpcBaseMotor.cpp:322` `ValidateNavGoal` gate (`0x10280360`) | `0x102ee620` | int; `== 6` (cover) | goal type | right getter; 0 (none) also skips the body |
| `ElysiumNpcBaseMotor.cpp:334` `NavGoalPosition` (`0x102ee140`) | `path+0x4c..+0x54` minus `path+0x34..+0x3c` | 3 floats, never "none"; `(0,0,0)` after a reset | actual goal position | only reached behind the type-6 gate, so "none" is unobservable here |
| `ElysiumNpcBaseWerewolf.cpp:58` `UpdateTargetPos` (`0x10271b10`) | `nav+0x18` (`!= 3 && != 1`), then `0x102ee620 == 1` | int; body also reads `0x102ee160` (`path+0x30`), `0x102ee640` (`path+0x60 & 4`), `0x102ee140` | goal type (nav-type gate not in the port) | goal-type half right; the `nav+0x18` gate is a missing seam |
| `ElysiumNpcRunAiSpecies.cpp:441` Tentacle RunAI (`0x1039e3d0`) | `0x102ee6a0` (`path+0x24 != 0`) then `0x102ee620 == 4` | bool, int | `NavigatorGoalIsActive()` = active route; `NavGoalState() == 4` | both right; `NavGoalState` -1 never opens the arm |
| `ElysiumNpcStartTask.cpp:947` `TASK_WAIT_FOR_MOVEMENT` (`0x102a1dcc`) | `0x102ee2e0` = **`path+0x10` paused byte**; `0x102ee620 == 0`; `0x102ee6a0` | bool; int; bool | port `NavIsGoalSet()` (comment: "goal type `+0x10`") | **`0x102ee2e0` is mislabelled**: it answers `m_bPaused`, not goal-set; the port answers the goal latch. `NavIsGoalActive()` here stands for `0x102ee6a0` while the `.inl` says `0x102ee680` |
| `ElysiumNpcStartTask.cpp:982` teleport rescue | `0x102ee140` | 3 floats | goal position | right getter |
| `ElysiumNpcStartTask_2.cpp:1108` `TASK_ATTEMPT_DIVE` | `0x102ee140` | 3 floats; zero-initialised = the reset answer | goal position | right getter; `(0,0,0)` is retail's cleared-goal answer |
| `ElysiumNpcSchedule.cpp:641` `NavigatorGoalType` (`0x1027d990`; caller `ElysiumNpcManBat.cpp:253`) | **`nav+0x18` = `m_navType`**, not the goal type | int `Navigation_t`: 0 ground, 1 jump, 2 fly, 3 climb; constructor default 0 | port: "goal type, anything but 2 is no active goal" | **wrong meaning**: `CNPC_VManBat::SelectSchedule 0x1038e340` tests `navType != 2` (not flying); the flyer's nav type is set to 2 by `0x1038c170` / `0x103580d0` through `0x1027d9b0` |

Getter table (address · SDK name if it matches · words read · answer with no goal):

| Address | SDK name | Words read | No-goal answer |
|---|---|---|---|
| `0x102ee620` (`0x100113d8`) | `GetGoalType` | `path+0x5c` | `0` |
| `0x102ee680` | `IsGoalSet` (not `IsGoalActive`) | `path+0x5c != 0` | false |
| `0x102ee6a0` | `IsGoalActive` | `nav+0x30`, `path+0x24 != 0` | false |
| `0x102ee140` (`0x1000f89e`) | `ActualGoalPosition` | `path+0x4c..0x54` minus `path+0x34..0x3c`, static buffer `DAT_10936af0` | `(0,0,0)` after reset; else stale |
| `0x1030ba30` | `&GoalPos` (raw) | `path+0x4c` | `(0,0,0)` |
| `0x102ee5e0` (`0x10012805`) | `GetCurWaypointPos` | `path+0x24 -> wp+0x00..0x08` | origin waypoint `DAT_10936afc` = `(0,0,0)` |
| `0x102ee6d0` | `GetPointAlongPath` | `path+0x24` list by `+0x30`, `nav+0x18`, NPC origin | false (no head) |
| `0x102ee1a0` | `GetGoalTolerance` | `path+0x28` | 0.0 |
| `0x102ee3f0` | `GetMovementActivity` | `path+0x2c` | `1` |
| `0x102ee640` | `GetGoalFlags` | `path+0x60` | 0 |
| `0x102ee160` | `GetTarget` | `path+0x30` handle | NULL |
| `0x102ee2e0` | (`m_bPaused` getter) | `path+0x10` | 0 |
| `0x102ee660` | `CurWaypointIsGoal` | `head+0x28 >> 3 & 1` (`0x1030bd50`) | 0 |
| `0x1027d990` | `GetNavType` (NPC-level) | `nav+0x18` | `0` |
| `0x102eee40` | `MoveCalcBase` (slot 17) | see 4 | — |

**Unrecovered:** the value each `SelectSchedule` / `StartTask` / `RunTask` reader of `0x102ee620` compares
against (only the arms listed were read); the activity names behind `0x37` / `0x38` in `0x10274b30`; who,
if anyone, allocates the navigator block from zeroed memory (so whether `nav+0x40..+0x4c` can be non-zero
before the first reset); a caller-by-caller census of slot 17 beyond `MoveNormal` and the humanoid override
(the corpus lists 105 possible call sites at slot 17 of unrelated classes); a waypoint-count or
distance-remaining getter (none found); what `0x102ef510`'s `DAT_10934070` and `0x10451f78` /
`0x10449260` tunables are.

### Route-control flags, their issuers and the blocked-move arms (2026-09-29, 0018 story 5)

Read on `vampire.dll` (decompile plus listing). Names: **BLR** = `BuildLocalRoute 0x10304130`, **worker** =
the ground/fly route worker `0x10303850`. Argument order of BLR, from the pushes at every call site
(`__thiscall`, `this` = the pathfinder, `this+4` = the NPC): `(start, goal, target, FLAGS, node, MASK,
tolerance, notrace-byte)`. The worker's is `(mode, start, goal, target, FLAGS, node, MASK, nodeType,
tolerance, notrace-byte)`; `0x10304130` forwards `FLAGS` and `MASK` unchanged (`10304130`: `param_4` →
worker `param_5`, `param_6` → `param_7`).

**Callers of the worker.** Only two: `0x10303d10` (mode 0, a `CAI_Pathfinder_BuildGroundRoute` profiler
wrapper, a sibling with no logic of its own) and `0x10303f80` (mode 2, the fly/swim twin). Both are
reached only from BLR (`if (mask & 1)` → `0x10303d10`; `if (mask & 2)` → `0x10303f80`). BLR's own jump /
climb arms (`0x10303c90`, `0x10303cd0`) need a node argument and never reach the worker.

#### 1. What "the flag word `& 8`" is: a route-control literal, NOT the goal flags

The worker's fifth argument is BLR's fourth, a literal each caller pushes. It never touches goal word
`+0x24`. Proof in the worker itself: it is passed as the fifth argument of the waypoint constructor
`0x10319df0` at all four places the worker builds a route (`10303850`; the ctor stores that argument at
waypoint `+0x28` — `10319df0`: `*(this+0x28) = param_4`) and to the detour `0x10304020`. So it is the
**waypoint flags word** the route builder stamps on every waypoint it lays (the same `+0x28` the
simplifier `0x102f13d0` / `0x102f0fe0` / `0x102f0ab0` tests with `0x2a`, `0x4`; `DoFindPath 0x102f2330`
stamps `2` on type-3 chain waypoints and ORs `8` onto the last one, `102f24d5`; the arc builder ORs `8`
and `0x20`). The bit meanings observed: `1` the simplifier's detour, `2` pathcorner, `4` node, `8` the leg
ends at the goal, `0x10`/`0x20` door / no-simplify (`0x30` at the door approach). These match the SDK's
`bits_WP_*` (`TO_DETOUR 1, TO_PATHCORNER 2, TO_NODE 4, TO_GOAL 8, TO_DOOR 0x10, DONT_SIMPLIFY 0x20`) —
corroboration only. So the blocked-move rule "flag word `& 8`" = "this leg ends at the goal"; the 0018
story 5 correction *(c)* wording "goal flag `0x8`" is a misread. The port needs no goal-flag input for it.

**Caller table** (`BLR` has exactly six caller functions; the grep on the address and the thunk
`0x100132b9` finds no other):

| Caller | Call site | Situation | FLAGS literal | MASK | `0x100` | `0x10` | `0x20` | `0x40` |
|---|---|---|---|---|---|---|---|---|
| `0x102f2060` (route build from `DoFindPath 0x102f2330`, itself from `SetGoal`'s `0x102f1dc0` and `0x102f1f80`) | `0x102f2154` | the LOCAL attempt; only if nav type != 3, `path+1 == 0`, `path+8 == 0.0` | `8` (`102f214b PUSH 8`) | caps `&4` or `&0x10` → `0x32`; else caps `&1` → `0x131`, `0x135` with `&2`; else 0 (`102f2119 MOV EBX,0x32`, `102f20fe MOV EBX,0x31`, `102f210f 0x35`, `102f2114 OR BH,1`) | walkers yes; fliers/swimmers NO | yes | yes | no (NPCs traced) |
| `0x103005f0` (node-route ENTRY leg, start → node; called by `BuildNodeRoute 0x10304e00` and `HasPathOuter 0x102fdcc0`, both push `0x60`) | `0x103006a2` | node route, first leg | `0xc` (`10300696 PUSH 0xc`) | `0x60 \| bits` | no | yes when bits carry it | yes | **yes (NPCs not traced)** |
| `0x103007e0` (node-route EXIT leg, node → goal; same two callers, both push `0x160`) | `0x10300893` | node route, last leg | `8` (`10300887 PUSH 8`) | `0x160 \| bits` | **yes** | yes | yes | **yes** |
| `0x102f0fe0` (simplifier corner cut; from `0x102f13d0`) | `0x102f11e6`, `0x102f1241` | two segments npc → projected point → next waypoint | `1` (`102f11ce`, `102f122f PUSH 1`) | `0/1/5`: `1` if caps `&1`, `\|4` if caps `&2` | no | no | no | no |
| `0x102984a0` (Troika `OnObstructingDoor`, slot 531) | `0x1029865e` | route to the door's approach point | `0x30` (`1029864a PUSH 0x30`) | `1` | no | no | no | no |
| `0x10304670` (arc builder, from `0x102ed390`) | `0x103047d7` | each arc segment | `0` (`103047a7 PUSH 0`; the two literal zeros are target and flags) | `1`, or `2` when its byte arg is set (`103047a1 INC EDX`) | no | no | no | no |

`bits` for the two node legs come from `0x10300700` (returns `8` when the leg endpoint is the NPC's own
position and `0x1027d990() == 3`; else caps `&4` → `0x12`; else caps `&1` → `0x11`, `0x15` with caps `&2`,
`|8` with caps `&8`; else 0). Note it tests caps `&4` and `&1` only — no `&0x10` — so a swimmer-only NPC
gets `bits = 0` and BLR answers NULL untraced for its node legs. `HasPathOuter 0x102fdcc0` passes tolerance
`0.0` and notrace `0` to both legs. The notrace byte (last argument, worker `param_10` → probe `param_9`,
`0x102e4f50` returns "clear, end = goal" without tracing when non-zero) is `0` at every site read here
except the arc builder's and `BuildNodeRoute`'s pass-through of `DoFindPath`, which is the literal `0`
(`102f2661 PUSH 0` — `DoFindPath` ignores its own stack argument entirely).

**Where each arm applies (shipped-data situations).**

- **Blocked-move acceptance** — needs `MASK & 0x100` AND `FLAGS & 8` AND remaining distance `<=`
  tolerance AND `|goal.z − end.z| < 2.0`; the waypoint goes at the probe's END position (not at the
  goal). Both bits are present together in exactly two places: (a) the local attempt of `0x102f2060`
  for every ground walker (caps `&1`, no `&4`/`&0x10`) whose goal is a normal route, tolerance = the
  path's `+0x28` (goal tolerance as resolved in `SetGoal`); (b) the node route's EXIT leg
  (`0x160 | bits`, `FLAGS 8`), tolerance = the same `path+0x28` when reached through `BuildNodeRoute`,
  `0.0` through `HasPathOuter` (so it can never accept there: a blocked probe has a positive remainder).
  It never applies to: fliers / swimmers' local attempt (`0x32` has no `0x100`), the ENTRY leg
  (`FLAGS 0xc` has the 8 but `0x60` has no `0x100`), the simplifier, the arcs, the door approach.
  Note it also holds in worker mode 2 for the `0x172`-style exit legs (mask carries `2` and `0x100`).
- **NPC-blocked D_LI re-probe** — needs probe code `-3` AND `MASK & 0x10`. The `0x10` bit is set in
  `0x131`/`0x135`/`0x32` (local attempt, walkers AND fliers/swimmers) and inside `bits` of both node
  legs. It can only fire where the FIRST probe traces NPCs (`0x40` clear → contents `0x202400b`): the
  local attempt. In the node legs `0x40` is set → contents `0x2400b` without `MONSTER`, so an NPC is not
  expected to stop the first probe and the `0x10` bit there is inert (inference from the mask; the
  engine's non-brush-entity rule that needs `CONTENTS_MONSTER` was not read in this corpus).
- **Two-waypoint detour `0x10304020`** (→ `0x103059d0`) — needs `MASK & 0x20`, tried only after the
  blocked-move acceptance fails or is not eligible: the local attempt (`0x131`/`0x135`/`0x32`) and both
  node legs (`0x60`/`0x160` carry `0x20`). Never for the simplifier, arcs or door approach.

Order inside the worker for a blocked probe (`10303850`): accept-at-end → detour → `-3` D-LI arm → NULL.

#### 2. Goal-flag `0x2` (explicit node route, `SetGoal 0x102ecd20`; test at `0x102ecf27`, taken at `0x102ecf2e`)

Census method: every function that calls `SetGoal` (through `0x1000ce64`) — 15 real functions plus twin
thunks — and for each call the store to goal word 9 (`+0x24`), read from the listings (base = the `LEA`
pushed as the record; word 9 = base + 0x24). Result for all 27 sites of `CAI_BaseNPC::StartTask 0x102827f0`,
all 19 of `CAI_BaseNPCTroika::StartTask 0x102a1910`, and every other caller: word 9 is `0` except:

- **`0x2` — ONE issuer: `CAI_BaseNPCTroika::StartTask 0x102a1910`, task `0xc7 TASK_GET_PATH_TO_ENEMY_CLOSEST`
  (`0x102a4812`)**: store `MOV [ESP+0xd0],0x2` at `0x102a48f2`, `SetGoal` at `0x102a4921`. Record: type 4
  at the enemy's origin (slot `+0x370` of `GetEnemy()`), activity `-1`, tolerance `DAT_1049a1ac`, target
  `DAT_10923dd8`, call flags 0; a refusal is `DevWarning(2,"GetPathToEnemy failed!!\n")`, line `0x335a`,
  `TaskFail(0xc)`. **Shipped data reaches it:** `Content/ElysiumCorpus/ai/schedules/cnpc_vtzimisce/
  sched_vtzimisce_attack_closest.sch` (`SCHED_VTZIMISCE_ATTACK_CLOSEST`: `TASK_SET_TOLERANCE_DISTANCE 24`,
  `TASK_GET_PATH_TO_ENEMY_CLOSEST`, `TASK_RUN_PATH` …); the only `.sch` naming the task (the string also
  sits in `FUN_10316ff0`'s task table and in the inline schedule text loaded by `FUN_103b7120`). Species
  shadow rows (`0xc7 → 0xc8` on `CNPC_VMingXiaoTentacle` / `TzimisceHeadClaw` / `TzimisceRunner`, the
  frenzied `0xc7 → 0xc9`, `schedule-kernel.md`) translate the id before Troika sees it; whether the
  Tzimisce boss is ever in a state that selects that schedule was not traced. So the arm is NOT dead:
  one retail schedule can reach `0x102ecf2e`. (The base and Troika `GET_PATH_TO_ENEMY 0x0f` arms use
  type 2 with flags 0; the doc's "goal flags 2" for `0xc7` is right.)
- Flag `0x1` (`SetGoal` turns the motor toward the destination, `0x102ed161`) — three issuers:
  `CAI_BaseNPC::StartTask` task `0x120 PATHCORNER` (`0x10285ff8`; store `MOV [ESP+0x5c],1` at `0x102860d4`,
  call `0x102860f0`, type 3), `ScheduledMoveToGoalEntity 0x102800c0` (`uStack_1c = 1`) and
  `ScheduledFollowPath 0x102801e0` (same).

Everything else builds `AI_NavGoal_t` with flags `0`: the constructors `0x102a9c80` (type given) and
`0x102a9d20` (type 4) — used only inside Troika `StartTask` — are called with flags argument `0` at every
site (`102a61ad`, `102a65a6`, `102a7098`, `102a768c`); the wrapper `0x102ed610`, the patrol pair
`0x102aa640` / `0x102aa860`, `CAI_StandoffBehavior::vfunc15 0x102c7bd0`, `0x10278220`, and the species
arms (`CNPC_VZombie 0x103dfd80`, `CNPC_VScurrying 0x103ac740`, `CNPC_VMingXiao 0x10392d80`,
`CNPC_VMingXiaoTentacle 0x1039c4c0` / `RunTask 0x1039d750`, `CNPC_VAnimal 0x1035f650`). No function
writes the path's `+0x60` other than `SetGoal` (`*(path+0x60) = goal[9]`, the only match for that store).

#### 3. Goal flags `0x8` and `0x4`

- **`0x8`: no issuer** (no `SetGoal` site stores it; every word-9 value above is `0`, `1` or `2`) **and no
  consumer**. `SetGoal` shifts it out (`0x102ed121`, per the existing doc: `(goal[9] >> 3) & 1`) into the stack argument of
  `0x102f1dc0`, which forwards it to `DoFindPath` (`102f1e59`, `102f1eab`). `DoFindPath 0x102f2330`
  (`RET 4`) never reads that argument (no `[ESP+0x34]` access anywhere in its listing) and calls
  `0x102f2060` with a literal `PUSH 0` (`102f2661`). The `0x8` is inert end to end. Together with §1 this
  closes the old "unrecovered `0x8`".
- **`0x4`: no issuer.** Reader: `UpdateTargetPos 0x10271b10`, through the accessor `0x102ee640` (which
  has that one caller): for a type-1 goal whose target moved it re-paths when `(path+0x60 & 4)` is set
  and the target is farther than `_DAT_104454c8` from the stored point. No record with bit 4 is built
  anywhere, so in retail data the re-path-on-move arm is dead by content (the port keeps it as a seam).

#### 4. `0x10303fd0` walked (`this` = the pathfinder; args `start`, `goal` unused; third arg = the blocker)

1. `npc = blocker[+0x94]` (the blocker's cached NPC pointer, `MyNPCPointer`); null → `false`.
2. `npc[+0x2e0]` (`m_pEdict`) must be non-zero; zero → `false`.
3. `rel = npc->vtable[+0x650 = slot 404 IRelationType](mover = pathfinder+4)`; answer `rel == 3`.

Nothing else: no class test, no alive test, no `+0x94` of the mover. The BLOCKER, not the mover, is
asked how it feels about the mover, and it is asked ONLY at the moment the first probe returned `-3`. The
blocker pointer is the FIRST probe's (`probe+0x1c`, worker local `iStack_54`), not the re-probe's; the
re-probe (`0x2400b`, no NPCs) only has to come back clear (`status >= 0`), then `0x10303fd0` decides, then
a waypoint is laid at the goal (`10319df0(..., param_3, ...)`). Values of slot 404: `0` D_ER, `1` D_HT,
`2` D_FR, `3` D_LI, `4` D_NU; only `3` passes. For the shipped classes slot 404 is
`CAI_BaseNPCTroika::IRelationType 0x10299da0` (most), `CBaseCombatCharacter 0x10333340`, and species
overrides (`CNPC_VCop 0x10372b70`, `CNPC_VHunter 0x10388bb0`, `CNPC_VNewscaster 0x103a01b0`,
`CNPC_VPedestrian 0x103a2930`, `CNPC_VPlayerController 0x103a48b0` (also VFrenzyShadow, VWolfMorph),
`CNPC_VYukie 0x103dd880`). The Troika body returns `3` outright when the blocker's owner slot
(`this+0x647c`, an entity whose `+0x9c` combat pointer) equals the entity asked about (
`if (pCVar3 == param_1) return 3`); otherwise `0` for self/null, `1` when the closest player or the
owner hates the asker, else the `0x10333340` class table (not walked).

#### 5. `0x102e2d70` and doors

`0x102e2d70(blocker)`: `-3` iff `blocker[+0x94] != 0` (an NPC); else `engine slot 0x8c (35 = IndexOfEdict)`
on `blocker[+0x2e0]`: non-zero → `-1` (an ordinary entity), zero → `-2` (the world). Confirmed. Callers:
the four probe cores `0x102e4f50` (ground), `0x102e6090`, `0x102e6290`, `0x102e6be0`. `-2` is also written
by `0x102e5d80` for a missing floor and by `0x102e4f50` for the final-z failure. `0x102e5d80` additionally
turns a blocked result into CLEAR (`*param_7 = 0`) when the blocker is the goal's `target` argument
(`param_4 != 0 && param_4 == probe[7]`) or the entity held at player `+0xa8 → +0x20f4/+0x20fc`.

**No, the classifier does not distinguish a door.** A door has no `+0x94` so it is `-1` (or `-2`); it is
found only afterwards through `CBaseDoor`'s self-pointer at entity `+0xa4` and NPC vtable slot 531
(`OnObstructingDoor(goal, door, distClear, &result)`, `+0x84c`). There is no `prop_door_rotating` string in
the image; the door classes are `func_door` (`CBaseDoor`), `func_door_rotating` (`CRotDoor`) and
`momentary_door`. **Callers of slot 531 (both dispatch sites of `[obj+0x84c]`):**

- `0x1027dc10` (the movement sink's base body, `1027dc3f CALL [+0x84c]` on `this-0x19b0`): reads the move
  trace's obstruction (`goal+0x60`), then that entity's `+0xa4`; if non-null calls slot 531 with
  `(goal, door, distClear, &result)` and answers its byte. Per `navigation-jump-links.md` (1682–1690) it is
  the move-time dispatcher, which the Troika override tail-calls.
- `0x102f06e0` (the path simplifier's shortcut test, called by `0x102f0ab0` and `0x102f0e80`): a hull trace
  (mask `0x2400b`, `0x2600b` when `path+1` is set) toward a later waypoint; when it lands on an entity with
  `+0xa4` and the lock test `0x100eec70` says the NPC could open it, it builds a zeroed goal with
  `+0x28 = distance + _DAT_1044e664` and calls slot 531 (`102f08c3`); a true answer sets the caller's byte
  and raises navigator failure `(0x0e, 1)` (`102f08e1`).

Implementers: `CAI_BaseNPC 0x1027dc80` (declines when `goal+0x28 < distClear`; needs door toggle state 1 or
3; `distClear < _DAT_104493d0` → result `-1`, else it stores `distClear` at `goal+0x28`, result `0`) and
`CAI_BaseNPCTroika 0x102984a0` (77 classes fill slot 531; the Troika body owns the open/wait/approach
route: its BLR call `0x1029865e` uses mask `1`, flags `0x30`).

**Unrecovered:** which classes report `CapabilitiesGet` bits `4` / `0x10` (one body, `0x1026db30`, not
walked — decides who takes `0x32` vs `0x131`); the `0x10333340` class-disposition table behind slot 404
(so which mover/blocker pairs really answer `D_LI` beyond the Troika owner arm); whether the engine's
standard filter really drops NPCs when `MONSTER` is absent from `0x2400b` (assumed, makes the node legs'
`0x10` inert); whether the Tzimisce boss ever selects `SCHED_VTZIMISCE_ATTACK_CLOSEST` (the only shipped
flag-`0x2` route); the caller of the movement sink's slot-1 body `0x1027dc10` beyond the existing doc; the
source of the arc builder's notrace byte (callers of `0x102ed390` not censused).

### CAI_Navigator::Move 0x102eff40, walked (2026-09-29, 0018 story 5; two readers diffed, disagreements re-read from the DLL)

Sources: R3a.md (reader A) and R3b.md (reader B), merged. Every disagreement was re-read from the `vampire.dll` listing
(`vtmb_asm`); constants marked (file) were read from the image bytes of `Vampire/dlls/vampire.dll`. Vocabulary: `nav` =
`CAI_Navigator`; `path` = `nav+0x30` (`CAI_Path`: `+0x10 m_bPaused`, `+0x24` head waypoint, `+0x28 m_goalTolerance`, `+0x2c
m_activity`, `+0x30 m_target`, `+0x40 m_waypointTolerance`, `+0x44` last node passed, `+0x5c m_goalType`); waypoint `+0x0`
position, `+0x10` node id, `+0x20`/`+0x24` entity/door handle, `+0x28` flags (`0x2` InPass, `0x4` node, `0x8` goal, `0x10`
door), `+0x2c` move type, `+0x30` next; `motor` = `nav+0x20`, `probe` = `nav+0x24`, `localnav` = `nav+0x28`; `sink` = the
`IAI_MovementSink` sub-object at `nav+0x10` (`vftable_CAI_Navigator_at16`; its methods are called S1..S7 below by slot); the
NPC-side sink at `npc+0x19b0` is consulted first by every S* (base table: slots 2..7 `return false`). Goal record (31 dwords,
`AILocalMoveGoal`): `+0x0c` dir, `+0x24` speed, `+0x28` maxDist, `+0x2c` expected step, `+0x30` navType, `+0x34` target
entity, `+0x38` flags (1 TARGET_IS_GOAL, 2 CONSUME_INTERVAL, 4 TRANSITION), `+0x44` MoveLimit trace (`+0x44` status, `+0x60`
obstruction entity). `AIMoveResult`: 1 CHANGE_TYPE, 0 OK, -1 entity, -2 world, -3 NPC, -4 illegal. Signature `Move(this,
float interval, arg2)`, `RET 8`.

Correction to the brief (both readers): `0x102ee2e0` is `path.m_bPaused` (`MOV AL,[path+0x10]`), not "no active goal".

#### Entry gate and order of tests

All return-and-do-nothing unless stated (`0x102eff40` listing).

1. `102eff57..102effa1`: `if (interval > 1.0) interval = 1.0f` (double `0x10449280` = 1.0, file).
2. `102effab`: `0x102ee2e0` = `path.m_bPaused` non-zero -> return. No stop, no fail, `nav+0x1c` untouched.
3. `102effc2`: NPC slot 525 `OverrideMove(interval)` (`+0x834`) true -> return. Base `0x1027da90` false; overrides
   `CNPC_Crow 0x10357ba0`, `CNPC_VManBat 0x1038b120`, `CNPC_VVampireBoss 0x103c5fe0` (+ its subclasses), not walked.
4. `102effd3`: `npc+0x1a40 m_bShouldMove == 0` (`102f0198`): `nav+0x18 == 3` (climb) -> motor slot 5 (`0x102e1110`),
   `SetNavType(0)` (`0x102eeba0`); else `nav+0x18 != -1` -> motor slot 10 (`0x102e1440`, velocity 0). No failure raised.
5. `102effe1..102f0069`: hull/frame stamps on five components (`nav`, `npc+0x5d3c` pathfinder, `localnav`, `probe`,
   `npc+0x5d44`): `comp+8 = owner+0x156c`, `comp+0xc = gpGlobals+4`. `0x1000f240(path)` called, result dropped.
6. `102f007b`: `motor+0x30 = interval` (the time budget the loop and the motor spend).
7. `102f0081`: `m_goalType == 0` -> `Warning("AIError: Move requested with no route!\n")`, `OnNavFailed(table[1] = 0x0d, 1)`.
   Table `0x1060fcc4` = `{0, 0x0d, 0x0c, 0x0e, 0x0f}` (file).
8. `102f00bc`: head waypoint `== 0` -> `OnNavFailed(table[2] = 0x0c, 1)`, no warning, no stale mark.
9. `102f00cd`: `0x10280a20(npc)` = `curtime < npc+0x5cf0 m_flMoveWaitFinished` -> return (silent wait).
10. Loop (`102f00e9..102f0165`): result seeded `1`, `nav+0x1c = 0`, pass counter `EBP = 0`.
    - top: `nav+0x1c` set -> exit if result `>= 0`, else failure tail; `motor+0x30 <= 0.0f` -> exit (`TEST AH,0x41; JNP`).
    - dispatch on `0x1000f240(path)` = head waypoint `+0x2c` (-1 without head), jump table `0x102f01d4`: 0 and 2 -> slot 12
      `MoveNormal 0x102efaa0`; 1 -> slot 14 `MoveJump 0x102eece0`; 3 -> slot 13 `0x102eebc0` (climb); other -> `DevMsg("Bogus
      route move type!")`, `-4`.
    - after: `INC EBP; CMP EBP,0x10; JG` -> the 17th dispatch prints `"ERROR: AI navigation not terminating. Possibly bad
      cyclical solving?"` and jumps to `102f016e` (stale mark, then `0x0c`) whatever the result, even `>= 0` or `-3`;
      otherwise result `>= 0` loops, `< 0` goes to the failure tail.
    - failure tail `102f0169`: `CMP EAX,-3; JZ 102f017c` skips the stale mark; else `0x102f1fa0(nav, 4.0f, NULL)`
      (`PUSH 0; PUSH 0x40800000`); then `OnNavFailed(0x0c, 1)` (`102f0180`, nav slot 10).

No frozen test and no in-flight jump/climb test in `Move`; jump/climb state is `nav+0x18` (-1 none, 0 ground, 1 jump, 2 fly,
3 climb) consulted inside the arms. `OnNavFailed 0x102eeae0`: `0x102eeb70` reset, `npc+0x1b44/+0x1b48` = source file / line
`0x406`, NPC slot 448 `TaskFail(code)` (`+0x700`), stopped activity (`0x100097d2(npc, 0x1000b285(npc))`), `nav+0x1c = 1`;
its second argument is not read. `0x102eeb70`: `nav+0x54 = -1`, `nav+0x58 = nav+0x60 = -1.0f`, then localnav reset
`0x1000b550`.

`MoveNormal` gate `0x102efd50`: route type 0 with `nav+0x18 != 0` -> `DevMsg("Warning: NPC appears to have wrong nav
type...")`, jump (1) -> motor slot 8, climb (3) -> motor slot 5, `SetNavType(0)`; route type 2 with `nav+0x18 != 2` ->
answers false and `MoveNormal` returns **-4** (no simplify); otherwise `SimplifyPath(nav, 0)` (`0x100053b7`, result
ignored), `nav+0x51 = 0`, true. `MoveNormal` does NOT re-test `nav+0x1c` after the gate (see the door path).

#### The arrival test and waypoint advance (`AdvancePath 0x102f0400`, `OnNavComplete`)

Both readers agree; re-read constants from the file. Navigator slot 16 `0x102ef510`, called from `MoveNormal` at `102efb2f`
before any step is built (result seeded `-4`; true -> `MoveNormal` returns the word):

- NPC origin = NPC slot 220 `GetOrigin` (`+0x370`) against the head waypoint position. `nav+0x18 == 0` -> **2-D**
  `sqrt(dx*dx+dy*dy)`; any other nav type -> **3-D**.
- Tolerance is a constant, not `path+0x28`, not `path+0x40`, not a hull: double `0.0625` (`0x10451f78`, file), or `0.25`
  (`0x10449260`, file) when ConVar `npc_vphysics` (`DAT_106bbaa4`, `+0x2c` int value) is non-zero; shipped default `"0"`.
  Not reached iff `tol < dist` (NaN: not reached).
- Dead second refusal: `DAT_10934070` (BSS, no writer) set, next waypoint of a different type, `dist >= 0.001` -> not
  reached.
- Reached, goal waypoint (`0x102ee660` -> `0x1030bd50`: flag `0x08`): `OnNavComplete 0x102eea90` (nav slot 8: `0x102eeb70`
  reset, owner `TaskMovementComplete` via `0x102eccc0`, `nav+0x1c = 1`), `*result = 0`, true. `Move` exits at the next loop
  top.
- Reached, not the goal: `AdvancePath 0x102f0400`, `*result = 1`, true; the loop re-enters with the remaining budget.
- Not reached: false; `MoveNormal` reads ideal speed, sets `path.m_activity` (slot 310), returns 0 when speed `<= 0` and
  `m_Activity == 2`, else builds the step (slot 17 `0x102eee40`) and calls `MoveEnact`.

Why 0.0625 suffices: slot 17 clamps the step to `min(motor+0x30 * speed, dist)` (`goal+0x2c`) and the motor moves exactly
that, so the NPC lands on the waypoint and the leftover budget re-enters the loop.

Other `OnNavComplete` callers under `Move`: `MoveJump` landing on the goal waypoint; S2's `distClear < 0.125` arm; S7 on
motor code 4; `0x102ef760` (below).

`AdvancePath 0x102f0400`, in order: (a) flag `0x02`: entity at `wp+0x20`, vtable `+0x1d8` `AcceptInput("InPass", npc)`;
(b) not the goal and `npc+0x98` non-null: `0x102a0bc0(sub, wp)`; flag `0x10` (door): entity from `wp+0x24`, `+0xa4` door;
`0x1027f550(npc, door)` and `door+0x4f8 == 1` -> `0x10298800` (`m_bShouldMove = 0`, `0x102ee2a0(nav)`, `npc+0x644c = 1`,
door-transaction start, no fail code); null entity -> `DevMsg("%s trying to open a door that has been removed")`; (c) flag
`0x02` with a next waypoint: `npc+0x5de8 = slot 172 (+0x2b0)`, then `DoFindPath 0x102f2330` (the only route re-find in
this chain); else pop `0x1030ba90` (flag `0x04` -> `path+0x44 = wp+0x10`; no next -> `"ERROR: Force end of route without
goal"` and flag `0x08` set on the last).

#### The motor status table

Motor execute `0x102e23a0` (`CAI_Motor_MoveNormalExecute`): `goal+0x30 == 0` -> motor slot 19 (`0x102e14a0` ->
`0x102e1560` ground; `CAI_HumanoidMotor` fills slot 19 with `0x10264680`), else slot 20 (`0x102e1760`, fly/3-D). The
executor code comes from `0x102e0bd0` (ground step; `MoveLimit` over mask `0x202400b`, 100.0 %):

| motor code | condition in `0x102e0bd0` (asm) | table `0x1060e654` (file `{-4,0,-3,-2,-2}`) | what follows |
|---|---|---|---|
| 0 | trace status `< 0`, 4th arg (partial-move bool) false, not the target | -4 | never from the navigator: both callers (`0x102e1560`, `0x10264680`) pass 1; only `AutoMovement` can |
| 1 | trace status `>= 0` | 0 | loop continues |
| 2 | blocked, status `== -3` | -3 | S7 -> NPC-blocker hold (below) may rewrite to 0 |
| 3 | blocked, status -1 / -2 / -4 | -2 | S7 leaves it; `Move` stale-marks and fails `0x0c` |
| 4 | trace entity == goal target (`goal+0x34`, checked first) | -2 | S7 calls `OnNavComplete`, `*result = 0` |

Any non-zero table answer calls the motor sink slot 7 (`motor+0x10`, `+0x1c` = S7 `0x102ef6d0`, args `(goal, trace, code,
&result)`) and zeroes `motor+0x30` (`102e2419`), ending the budget. S7: NPC sink slot 7 first; code 4 -> `OnNavComplete`,
0; code 2 -> `0x102ef3e0` true -> 0; returns true either way. Verdict: B's mapping holds on the navigator path (-1/-2/-4 ->
-2; -3 -> -3), plus A's point that code 4 completes rather than fails.

What `Move` does per status reaching the loop:

| status | from | what `Move` does | code |
|---|---|---|---|
| 1 | arrival advance, S2 avoidance splice, `MoveJump` landing | loop while budget remains | none |
| 0 | enact, holds, completions | loop while `motor+0x30 > 0` and `nav+0x1c == 0` | none (arrival: `OnNavComplete`) |
| -1 | `MoveCalc` (base door hook `-1`; trace status propagated by S1/S4), `MoveJump` probe | stale mark 4.0 s, `OnNavFailed` | `0x0c` |
| -2 | motor codes 3/4 (4 is rewritten), Troika door hook in the step | stale mark 4.0 s, `OnNavFailed` | `0x0c` |
| -3 | motor code 2 after the hold, S1/S4 trace-status propagation | **no** stale mark, `OnNavFailed` | `0x0c` |
| -4 | gate refusal, `MoveCalcDirect` speed `<= 0`, `MoveCalcRaw` tail, bogus route type | stale mark 4.0 s, `OnNavFailed` | `0x0c` |
| 17th pass | loop | `DevMsg`, stale mark 4.0 s, `OnNavFailed` | `0x0c` |
| no `m_goalType` / no head | entry 7 / 8 | Warning (7 only), no stale mark | `0x0d` / `0x0c` |

Stale mark `0x102f1fa0(nav, 4.0, NULL)`: only when `nav+0x50 m_fRememberStaleNodes`, a path, a head, `path+0x44 != -1`
and `wp+0x10 != -1`; looks the link up in `nav+0x2c` (out of range bumps `DAT_106c994c`), `link+0x64 |= 1`, `link+0x68 =
curtime + 4.0`, `link+0 = -1`. Reader of the flag: `0x102fce80`. `Move` never calls `0x102f1dc0` (route re-find) or
`PrependLocalAvoidance 0x102ede30` directly.

#### The -3 arm as settled

Verdict: **both readers are half right.** `Move` itself has no wait: `102f0169 CMP EAX,-3; JZ 102f017c` skips only the
stale mark and fails `0x0c` at once (B). But a hold exists below `Move`, in the navigator's sink, and it rewrites the
blocked status to 0 before `Move` ever sees it (A). It is not per-status; it is keyed on the obstruction being an NPC.

`0x102ef3e0(nav, trace)` (thunk `0x1000856c`), instruction by instruction:
- blocker = `trace+0x1c` (the obstruction entity, `goal+0x60`) `->+0x94` (non-null for an NPC); none -> return `nav+0x51`.
- resolve `nav+0x54` (EHANDLE, serial `>> 0xd`, index `& 0x1fff` in the entity list `0x10566458`); not the blocker ->
  **arm**.
- same blocker and `curtime - nav+0x60 > -0.001` (double `0x10497530`, file) -> **arm**.
- same blocker inside the window: `curtime - nav+0x58 <= -0.001` -> `nav+0x51 = 1`, return true (**hold**); else return
  `nav+0x51` unchanged (0, since the `MoveNormal` gate cleared it this pass, unless an earlier hook in this pass set it).
- **arm** (`102ef49a`): `nav+0x51 = 1`, `nav+0x54 = blocker handle` (vtable `+4`), `nav+0x58 = curtime + nav+0x5c`,
  `nav+0x60 = curtime + nav+0x64`, return true.
- `nav+0x5c = 0x3e800000` (0.25 s), `nav+0x64 = 0x40400000` (3.0 s), set by the ctor `0x102eca50`; `nav+0x58 = nav+0x60 =
  -1.0f`, `nav+0x54 = -1` at ctor and by `0x102eeb70`, i.e. on every `OnNavFailed` and `OnNavComplete`.

Its two callers:
- **S3 `0x102ef350`** (sink slot 3), dispatched from `MoveCalcRaw 0x102de7b0` at `102de861`: NPC sink slot 3 first; then
  `0x102ef3e0(nav, goal+0x44)` true -> `*result = 0`, `goal+0x28 = distClear` (walk up to the clearance), `goal+0x38 |= 2`
  (consume the whole interval), true.
- **S7 `0x102ef6d0`** on motor code 2 (above): true -> `*result = 0` (budget already zeroed).

Where S3 sits in `MoveCalcRaw` (asm `102de7fe..102de99a`), for a probe that did not clear: `localnav` slot 5
`MoveCalcDirect 0x102ddc80` (speed `<= 0` -> -4; clear, or goal/transition flag with the clearance past `maxDist` -> 0) ->
S1 `0x102eefb0` (slot 1; tolerance `path+0x28` for the goal leg, `path+0x40` otherwise; a -3 whose blocker's slot 153
`+0x264` is true skips the goal-inside-tolerance completion; else NPC sink slot 1 = door hook) -> `localnav` slot 6
(`0x102de110`, steer) -> S2 `0x102ef1a0` (slot 2: goal-leg close arms; half-hull waypoint arm; then `distClear <
goal.maxDist` -> `PrependLocalAvoidance(distClear, 0)`, success -> `*result = 1`, a local detour spliced at the head) ->
**S3 hold** -> if motor slot 16 (`+0x40`, `0x102e1300`) `> distClear`: S4 `0x102ef0e0` (slot 4: `0x102efde0` true, a moving
NPC going the same way -> 0, `maxDist = distClear`, flag 2; else `distClear < 1.0` -> `*result = trace status` (-3 for an
NPC), `maxDist = 0`), then `localnav` slot 7 (`0x102de4a0`) -> tail: `distClear <= goal+0x2c` -> **-4**, else 0.

So an NPC blocking the step, per blocker: (1) steering and a local-avoidance detour are tried first (S2 -> 1, the NPC walks
around); (2) failing that, a **0.25 s hold** (walk up to the clearance, spend the interval, no fail); (3) once the 0.25 s
has run, the status stands: follow a same-direction mover (S4 -> 0), else `-3` (S4, `distClear < 1.0`; motor code 2) ->
`0x0c` without a stale mark, or the `MoveCalcRaw` tail `-4` -> `0x0c` **with** the 4.0 s stale mark. `0x102ef760` runs
before `Move` sees any of these. The 3.0 s window only matters while the blocker memory survives: `OnNavFailed` resets it,
so a schedule that retries the move against the same NPC gets a fresh 0.25 s hold; a second contact inside 3.0 s without
an intervening fail/complete (e.g. after a successful detour) gets none. No re-path happens anywhere in this arm.

#### `0x102ef760`

Sink slot 5 (`OnMoveBlocked`), confirmed from the asm; B's reading holds, A's S5 note agrees. Reached only from `MoveEnact`
(`102ef923`, `CALL [nav+0x10 vtbl +0x14]` when the result is `< 0`); its single direct thunk is `0x1000e5bb`. Statuses that
reach it: every negative result leaving `MoveEnact`, i.e. from `MoveCalc` (-1..-4) and from the motor table (-4/-3/-2)
after S7. Not reached by: the `MoveNormal` gate -4, the bogus-type -4, `MoveJump`/climb statuses, the 17-pass cap, entry
failures.

Body (`this = nav+0x10`): NPC sink slot 5 (`npc+0x19b0`, `+0x14`) true -> return true, result as that sink left it (base
false; `CNPC_VZombie` override `0x103de330` not read). Else stopped activity (`0x100097d2(npc, 0x1000b285(npc))`),
unconditionally; then distance from `GetOrigin` (slot 220) to the path's goal position (`0x1000f60a` -> `0x1030ba30`):
**2-D when `nav+0x18 == 0`, 3-D otherwise**; tolerance `0x102ee1a0` = `path+0x28 m_goalTolerance` `+ 0.1f` (`0x104491b4`,
file). `dist < tol + 0.1` strictly (`FCOMPP; TEST AH,5; JP`; equal or NaN fails) -> `OnNavComplete` (nav slot 8), `*result =
0`, true. Else false, the status stands. This is the only "close enough to the goal" completion after a blocked step,
on any leg.

#### The door path (`SimplifyPath 0x102f13d0` -> `0x102f06e0` -> slot 531 -> `0x0e`)

`SimplifyPath(nav, bForce)`: `nav+0x3c = bForce`; requires `nav+0x18 in {0, 2}` (`0x1027d990`), a head with a same-type
next and `flags & 0x2a == 0`; if forced or `nav+0x38 <= curtime`: `nav+0x38 = curtime + 0.5f` (`0x1049d988`, file), forward
pass `0x102f0e80` (look-ahead 384.0, `0x1060fcd8`; runs the door probe; true -> return true; door byte set -> return
false), corner cut `0x102f0fe0`; then always the quick pass `0x102f13a0` -> `0x102f0e00` (143.9, `0x1060fce8`). `SetGoal`
calls it with 1 after `0x102f1dc0`; the `MoveNormal` gate with 0 every pass.

`0x102f06e0` (`__fastcall`, callers `0x102f0e80` and `0x102f0ab0`): trace from NPC slot 193 (`+0x304`) with mask `0x2400b`
(`0x2600b` when `path+1`, pedestrian); fraction `== 1.0` -> `MoveLimit` to `path+0x30`, answers `status >= 0`. A hit whose
entity has a door (`+0xa4`) not already passable (`0x100027d4` -> `0x100eec70(door, npc)`): zeroed goal with `+0x28 = dist +
10.0f` (`0x1044e664`, file), then **NPC slot 531 (`+0x84c`)(goal, door, dist, &result)** at `102f08c3`. True -> `*outDoorSeen
= 1`; **`result != 0` (`TEST EAX,EAX; JZ`) -> `OnNavFailed(0x0e, 1)` (`102f08e3..102f08e7`) then `0x1027de00(npc, door)`**
(door notice; writes a stale mark `0x102f1fa0(nav, 5.0 or 20.0, door)` among others, not fully walked). Returns false on
every door path.

The result word, settled from both bodies (`vtmb_slot 531`: base on `CAI_BaseNPC`, `CGenericNPC`, `CCineAI*`, `CNPC_Crow`,
`CScriptedTarget`, `CGeneric_NPC_bathack` etc.; Troika on every `CNPC_V*` and `CGeneric_NPC`):
- base `0x1027dc80`: `goal+0x28 < dist` -> false; `door+0x4f8` not 1 or 3 -> false; `dist < 0.1` (double `0x104493d0`)
  -> **`*result = -1`**, true; else `goal+0x28 = dist`, `*result = 0`, true.
- Troika `0x102984a0`: null door -> `DevMsg`, false; `goal+0x28 < dist` -> false; `m_hOpeningDoor (+0x5d24)` is this door ->
  0; squad-focus door -> **-2**; slot `+0x740 == 4` without the `0xd00` capability -> 0 (and may start alternate-AI 4);
  `0x1027f550` unusable -> **-2**; capability branch: door route `0x10304130` spliced -> 0; no route, door neither 0 nor 2 and
  `0x10298840` false -> **-2**; other arms false. Never -1.

Verdict: the test is non-zero, so base `-1` (B) and Troika `-2` both raise `0x0e`; A's "-1 is never written" is true of
Troika only. In the shipped game's NPC classes the raiser is effectively Troika `-2`.

The second dispatch site, NPC sink slot 1 `0x1027dc10` (Troika wrapper `0x10298340` first tries `0x102a0bc0` and a squared
distance `< 0x1045d650` `AdvancePath`), handles a door in the step's own trace (`goal+0x60 -> +0xa4`); its result becomes
`MoveCalcRaw`'s status, so a refused door there surfaces as `-1`/`-2` -> stale mark 4.0 -> `0x0c`, not `0x0e` (A).

Quirk kept verbatim: `OnNavFailed(0x0e)` from the gate's simplify pass sets `nav+0x1c` but does not clear the path, and
`MoveNormal` does not re-test `nav+0x1c`; the pass goes on to arrival/enact, and a negative result in the same pass reaches
`Move`'s tail and raises `OnNavFailed(0x0c)` as a second `TaskFail`. A result `>= 0` exits at the loop top.
`0x10290570` (NPC slot 532) is a separate `0x0e` raiser (`TaskFail` only, reasons 2/4 with `m_eAlternateAI == 4`), outside
the navigator chain.

#### `MoveEnact` and what it hands the motor

`MoveEnact 0x102ef870` (`"CAI_Navigator_MoveEnact"`, `RET 8`): copies the 31-dword goal (`102ef8a9`); `localnav` slot 3
`MoveCalc 0x102debe0 (goalCopy, 0, &surface)` with `surface = 0` (`102ef8b7`) -> `MoveCalcRaw`. `surface != 0` ->
`0x1000c897` -> `0x10270290`: `npc+0x5b90 = surface` (named `m_pSurfaceData` in the field ledger / `docs/vtmb/footsteps.md`
1.5; no datamap entry), whether or not the move succeeds. Result 0 -> motor execute `0x102e23a0(motor, goalCopy, arg2)`,
`arg2` = `MoveNormal`'s own stack argument = `Move`'s `arg2` (passed through `PerformMovement 0x1026c120` untouched),
landing on `0x102e0bd0`'s trailing bool. Result neither 0 nor 1 -> motor slot 10 (stop). Result `< 0` -> `0x102ef760`
(`&result`). Returns the (possibly rewritten) word.

Goal filled by slot 17 `0x102eee40`: `[0..2]` head position, `[3..5]` direction (z dropped and `0x102e5d00` on ground, 3-D
normalise otherwise), `[6..8]` copy, `[9]` ideal speed (`0x102e12c0` -> NPC slot 248), `[10]` distance, `[0xb]` `min(motor+0x30
* speed, dist)`, `[0xc]` navType, `[0xd]` target (`0x102ecc40`), `[0xe]` flags (1 goal waypoint, else 4 if the next waypoint
differs in type), `[0xf]` path. The motor's ground executor `0x102e1560` spends `motor+0x30`: clamped budget `<= maxDist` or
flag 2 -> `motor+0x30 = 0`, else `motor+0x30 *= 1 - maxDist/projected`. `MoveNormal` restore arm (`102efc11..102efc80`):
result 0, pre-activity speed `< 0.01` and displacement `< 0.01` -> `m_nSequence` and `m_Activity` restored; result 0 and
`nav+0x51 == 0` -> nav slot 6 (`0x102eea50`, empty).

**Port consequence.** In the Unreal port the body walks the route itself, so the substrate has no `MoveLimit`/motor to
produce statuses; it must turn the body's facts into exactly these outcomes, in this order, per think: (1) entry gates
before anything (`m_bPaused` return; `OverrideMove`; `m_bShouldMove` clear -> stop, no fail; no `m_goalType` -> Warning and
`TaskFail(0x0d)`; no head waypoint -> `TaskFail(0x0c)`; `m_flMoveWaitFinished` -> return). (2) **Arrived**: the body within
0.0625 (2-D on ground, 3-D otherwise; 0.25 under `npc_vphysics`) of a waypoint -> `AdvancePath` arms (InPass input, door
flag `0x10` transaction start, pass-waypoint re-find, pop), or, on the goal waypoint, `OnNavComplete` (`TaskMovementComplete`,
done byte, blocker memory reset). (3) **Blocked by an NPC** (the body's obstruction is an NPC): first a local-avoidance
detour spliced at the head (result 1, keep walking); failing that, a 0.25 s hold per blocker (`nav+0x54/0x58/0x5c/0x60/0x64`,
0.25 s / 3.0 s window, `-0.001` edges, reset by every `OnNavFailed`/`OnNavComplete`) during which the NPC stands at the
clearance and nothing fails; after it, follow a same-direction mover, else the blocked result. (4) **Any blocked result**
first passes the goal-tolerance completion: `dist(origin, goal position) < path+0x28 + 0.1` (2-D ground / 3-D else, strict)
-> `OnNavComplete`, not a failure (stopped activity set either way). Hitting the goal's own target entity also completes.
(5) **Failed `0x0c`**: blocked by an NPC -> no stale mark; blocked by world/entity/illegal or the 17-pass cap -> stale-mark
the link 4.0 s (when `m_fRememberStaleNodes`) then `0x0c`. (6) **Door `0x0e`**: only from the simplify pass's straight-line
door probe (every 0.5 s, or forced by `SetGoal`) when slot 531 answers true with a non-zero word (Troika `-2`: squad-focus
door, unusable door, no door route and open refused; base `-1` within 0.1) -> `OnNavFailed(0x0e)` + door notice; a door met
in the step itself fails `0x0c` with the stale mark. Where the body cannot yet report an input (steer slot 6, slot 7, motor
slot 16, `0x102efde0`'s mover test), build the seam and answer "nothing" with a comment naming the retail word.

**Unrecovered:** `localnav` slots 6 and 7 (`0x102de110`, `0x102de4a0`) and `MoveLimit`'s arms (who fills `surface`); motor
slot 16 `0x102e1300` (the S4 gate distance) and `0x102efde0`'s constants; what obstruction `+0x94` is beyond "non-null for an
NPC", and the Troika sink's `+0x98`/`+0x14b8 bit 2` test; `PrependLocalAvoidance 0x102ede30` success conditions; slot 525
overrides `0x10357ba0`, `0x103c5fe0`, `0x1038b120`; NPC sink overrides (`CNPC_VZombie 0x103de330`); the meaning of `Move`'s
`arg2`; `0x102f0fe0`, `0x102f0ab0`, `0x102f0e00` past their outline; `0x1027de00`'s writes and the `0x10298800`/`+0x644c`
state machine beyond 1 -> 3; `TaskFail`'s overwrite rule for the `0x0e`-then-`0x0c` double fail; whether any writer besides
the ctor touches `nav+0x5c`/`nav+0x64`; `DAT_10934070` (no code writer; data patch not excluded); which motor class
(`CAI_Motor` or `CAI_HumanoidMotor 0x10264680`) each NPC uses for slot 19 (both pass the partial-move bool 1).

### The think gate, the path-corner chain and the step rise (2026-09-29, 0018 story 5)

_Read from `vampire.dll` listings and decompiles; constants read from the DLL image (`0x104491a8` = double 0.8, `0x104491b4` = float 0.1, `0x104493d0` = double 0.1, `0x104454c0` = 1.0, `0x10453b94` = 18.0, `0x10462950` = 40.0). Port files read: `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseThink.cpp` 25-120, `ElysiumEntityWorld.cpp` (clock hits), `Public/ElysiumGameClock.h`, `Public/ElysiumEntityWorld.h` 1165-1189._

#### Q1 - the 0.8 s think gate

**(a) The two reads test two different things, and the "built" flag `+0x658` is read by exactly one function.**

- `DAT_1093408c` is a **byte**, not the network pointer (`MOV CL,byte ptr [0x1093408c]` in `0x1026c3d0`; `!= '\0'` everywhere). Meaning: "the node graph is loaded or built". Writers (all three, from the ledger): `0x102f5bd0` (the `.ain` loader, sets 1 after `WCLookup`), `0x102f6610` (the rebuild step: `if flag==0 { 0x102f51d0 build; flag=1 }`), and `0x102f65b0` (sets 0, and `DAT_1093407c = 0`), whose only caller is `CAI_SystemHook::LevelShutdownPostEntity 0x102cc410`.
- `DAT_10934088` is the network **manager entity** pointer (written only by `0x102f6690`: `DAT_10934088 = this`, `DAT_1093407c = &this[1]+8`, the CAI_Network member). `manager+0x658` is the "built" byte; it has ONE writer, the manager's think `0x102f6a50` (`0x102f6a50: *(param_1+0x658) = 1`), and ONE reader, **`CAI_BaseNPC::NPCThink 0x1026ca80`** at `0x1026cb23..0x1026cb36` (`MOV EAX,[0x10934088]; JZ; CMP byte [EAX+0x658],0; JZ`). A whole-DLL grep for `+ 0x658)` finds no other reader.
- Consequence the port's seam names got wrong: `Think19AiNetworkReady` (`manager && manager+0x658`) belongs ONLY to the base-class body `0x1026ca80`. The Troika body `0x10292de0` (the one the VtMB NPC classes run) never touches `+0x658`; it gates only through `0x1026c3d0`, which reads `DAT_1093408c` (and `DAT_1092053c` bit 0 = AI disabled, bit 1 = ai_step). So the 0.8 s think gates the base-class NPCs (per the port's own comment at `ElysiumNpcBaseThink.cpp:101`: `CAI_BaseNPC`, `CAI_BaseHumanoid`, `CAI_ExpressiveNPC`, `CAI_TestHull`, the Cine classes, `CGenericNPC`, `CGenericSabbat_NPC`) and nothing on the Troika line.
- **`DAT_1093408c` is normally already 1 before the 0.8 s think ever fires.** `0x102f6690` itself does `if (0x102f67a0(mapname)) 0x102f5bd0(this)` (`0x102f6690`, before `ThinkSet`): `0x102f67a0` asks the engine (slot `0x170`) whether `maps/graphs/<map>.ain` is present and current versus the map; when it is, the loader runs at once (unless the engine's slot `0x30`, the Worldcraft edit-mode test, is set) and the flag becomes 1 during `CWorld::Precache`. A shipped map with a current `.ain` therefore has its Troika NPCs admitted from their first think. The flag stays 0 only when the file is missing/stale or the loader fails a check (version 0x1e, hull count 0x16, `TotalNumLinks`, `WCLookup`).
- What the 0.8 s think does when the flag is already 1 (`0x102f6a50`, `+0x450 == 0` arm, flag set, not edit-mode, jump to `LAB_102f6ab3`): fire the engine event `ai_node_graph_built`, run `0x102cc900` (initialise every dynamic link `CAI_DynamicLink` on the list `DAT_1092541c`: resolve its two WC node ids through `0x102f6d10`, then `0x102ccce0`; a missing WC lookup prints "Trying initialize links..." and skips), run `0x102f6cd0` (frees the WC lookup at `network+0xc` outside edit-mode), set `+0x658 = 1`, clear its own think, then `0x1028d8d0`: for every entity in the class list at `0x106eb5d8` call vslot `0x920` (slot 584, `CAI_BaseNPCTroika::FUN_1028d910` `0x1028d910`) = `slot 614 (+0x998)` then stamp `m_flLastThink / LastUpdateThink / LastNormalThink / LastMoveThink / LastAIThink = curtime` (the port already has this as `ResetThinkTimers`, `ElysiumEntityWorld.cpp:844`). When the flag is 0 instead: prints "Node Graph out of Date. Rebuilding...", sets `+0x450 = 1` and re-arms at `curtime + 1.0` (`_DAT_104454c0`); the next think runs `0x102f6610` (build, flag = 1) and then the same "built" tail.

**(b) `curtime` is `gpGlobals->curtime`** (`*(DAT_1070b228 + 0xc)`), the engine's per-map server clock. The first think is `m_flNextThink = curtime + (float)0.8` at the END of `0x102f6690`, so the gate is a stamp RELATIVE to the moment `0x102f6690` ran, not an absolute time. The engine side (whether `curtime` restarts at 0 on a map load, and what a restore rebases it to) is engine.dll and was not read; it does not matter to the gate, because the gate never compares against a map-absolute value.

**(c) When it runs.** `0x102f6690` has one caller: `CWorld::Precache 0x1023c020` (via thunk `0x1000bd66`), immediately after the light styles and before `0x1030c560(0x10936b68)`. `Precache` is reached from `CWorld::Spawn 0x1023bba0` (its tail dispatches slot `0x1a0`) and, on a save RESTORE, from the entity restore pass `CEntitySaveRestoreBlockHandler` `0x101a2e40`: its second loop, per restored entity, calls `Restore` (`+0x1fc`) then `ObjectCaps` (`+0x1d4`): bit 4 set -> Spawn (slot `0x19c`), else Precache (slot `0x1a0`). The world entity (`worldspawn`, index 0, special-cased in that function's first loop) takes one of the two, and `CWorld::Spawn` calls Precache itself, so **a restored map re-runs `0x102f6690`**: it creates a fresh `ai_network`, reloads the `.ain` (flag 1 again after `LevelShutdownPostEntity` zeroed it), and arms a new `curtime + 0.8` think. A restored map does wait 0.8 s for the manager's think (only base-class NPCs feel it; Troika NPCs are admitted as soon as the loader ran). The manager entity is recreated, not restored (inference: it would otherwise be duplicated; its `ObjectCaps` was not read).

**(d) A gated NPC.**
- Base body `0x1026ca80`: writes `m_flNextThink = curtime + 0.1` (double `0x104493d0`) at `0x1026cb14..0x1026cb1d`, BEFORE any gate; then manager null or `+0x658 == 0` -> return (`0x1026ccf5`); then `0x1026c3d0` false -> return. So it re-arms at 0.1 s and does nothing else (the port's body at `ElysiumNpcBaseThink.cpp:113` already matches).
- Troika body `0x10292de0`: the gate call is at `0x102933f9` (inside the "Set2" arm, after `ResolveStandingOnHead`, `0x102bfdf0`, `0x102bf310` and the closest-player LOS block). False -> skip `RunAlternateAI`/slot `0x6c0`/`PostRun`/`PerformMovement`/`CalcNextMoveThink`/`CalcNextAIThink` and skip the whole `LAB_1029364a` block (so no `CalcNextUpdateThink`/`CalcNextNormalThink`), and right after the gate call: `if (DAT_1093408c == 0) m_flNextThink = curtime + 0.1f` (`_DAT_104491b4`). When the flag is 1 but the gate is false (AI disabled or ai_step), the body does NOT write `m_flNextThink` on this path (whatever was there stays).
- Inside `0x1026c3d0` a closed graph (flag 0) prints the throttled "A.I. Disabled..." overlay (5 s, `_DAT_10454110`) and calls `SetActivity(ACT_IDLE = 1)` (slot 310, `+0x4d8`) every think; flag 1 with `DAT_1092053c & 1` also does the `SetActivity(1)` without the overlay. The port's `Think19AiConsoleGate` reproduces this.

**The port's clock question.** `FElysiumEntityWorld::NowSeconds()` (`ElysiumEntityWorld.cpp:140`) is `GameState->GameClock().GetNow()`, the session clock (`ElysiumGameClock.h`: "persists across map travel; only the entity world and its queue die with the map actor"; `Reset(StartSeconds)` restores a saved curtime). The world keeps NO map-start stamp: `Activate(double Now)` (`ElysiumEntityWorld.cpp:376`) writes `Now` only into `LastTickNow`, which every tick overwrites (`:1764`, `:1814`); a grep of `Source/ElysiumUE` for `MapStart|LevelStart|LoadedAt|LevelTime|MapTime` finds nothing relevant. **Verdict for story 5: no per-level clock is needed and none should be built.** The gate is "0.8 s after `Precache` ran", so it needs one new per-world field stamped when the world is built (`= NowSeconds()` at the `0x102f6690` equivalent, +0.8), which restore gets for free because the port rebuilds the world on load and restore exactly as retail re-runs `Precache`. Story 5 builds the seam: (1) `Think19NodeGraphBuilt` = "baked graph loaded for this world" (true from world load when the baked graph exists, false after teardown; today it returns constant true); (2) `Think19AiNetworkReady` = "manager exists and its 0.8 s think has fired", read ONLY by the base-class body; (3) the manager think's tail effects (dynamic-link init `0x102cc900`, `0x1028d8d0`'s per-NPC slot-584 reset) at that moment.

#### Q2 - the type-3 path-corner chain

**(a) Yes, arrival fires the corner's `OnPass` and copies its `speed`; `wait` is never read.** Arrival is `CAI_Navigator::FUN_102ef510` (slot 16, `MoveNormal`'s first probe at `0x102efaa0` `vt+0x40`): distance to the current waypoint (`path+0x24`) versus the tolerance; then, if `FUN_102ee660` (= `0x1030bd50`: current waypoint flag byte `+0x28` bit 3, "is the goal") is false, it calls **`AdvancePath 0x102f0400`**; if true, slot 8 `0x102eea90` (which resets the move state and calls `0x102eccc0` -> the NPC's `OnMovementComplete 0x10273ec0`). Other advance triggers, same function: the in-range test `0x102f1500` and the skip-ahead test `0x102f1690` (advance when a straight local probe to the next waypoint is clear), so an intermediate corner can be passed without touching it. `AdvancePath` step 1: if the waypoint flag `+0x28 & 2` (`bits_WP_TO_PATHCORNER`; the DoFindPath type-3 arm builds every waypoint with flags 2 via `0x10319df0(..., 2, -1)` and stores the corner's handle at waypoint `+0x20`) and its handle resolves, it calls `AcceptInput` (vslot `0x1d8`) on the corner with the input **`"InPass"`** (string `0x1057b630`), activator = the walking NPC, caller = the corner. `CPathCorner::InputInPass 0x10147d50` fires `m_OnPass` (`+0x454`, output `OnPass`) with that activator/caller and delay 0. The last corner is covered too: at the goal, slot 8 -> `OnMovementComplete 0x10273ec0` -> (`0x102ee6a0`: navigator still holds a waypoint) -> `AdvancePath` -> `InPass` on the goal corner; note the order in that function: `TaskComplete` for the waiting task (movement state 2) is written BEFORE the `InPass` fires, then the goal branch of `AdvancePath` skips everything else. `speed`: at chain build `0x102f2330` case 3 (`0x102f2393..0x102f23af`) reads the FIRST entity's `+0x164` (`CBaseEntity::m_flSpeed`, the `speed` key); if non-zero it stores it into the WALKER's `+0x164` through `0x102ecd00`; since `AdvancePath` rebuilds the chain from each new `m_pGoalEnt` (b), it re-applies at every corner passed. I found no reader of a walker's `+0x164` (only door/mover/`Dump` readers) so the copy is unconsumed as far as the corpus greps show. `wait` (`CPathCorner::m_flWait +0x450`, key `wait`): its only reader is the getter `0x10147b10` (slot 152), which has **zero callers** (direct, virtual and possible): no path corner pause exists. Also observable: `CPathCorner` has an input `SetNextPathCorner` (`0x10147d10`, rewrites the corner's `m_target`), which changes what `GetNextTarget` returns on later rebuilds.

**(b) `m_pGoalEnt` advances on every non-goal corner, and the chain is rebuilt rather than popped.** `AdvancePath` after `InPass` and after the goal test: if the waypoint has a next (`+0x30`) and flag `& 2`, it does `m_pGoalEnt (+0x5de8) = m_pGoalEnt->GetNextTarget()` (vslot `0x2b0`, slot 172; note it is the CURRENT `m_pGoalEnt`'s next, not the waypoint's entity) and re-enters `DoFindPath 0x102f2330`, which clears the path and lays a fresh chain (up to `0x80`) from the new `m_pGoalEnt`. Otherwise it pops (`0x1030ba90`; a "Force end of route" DevMsg and goal flag `|= 8` when there is no next). At the goal corner `m_pGoalEnt` is NOT advanced (it stays on the last corner). Raw-offset ledger for `+0x5de8` (whole-DLL grep): readers/writers are `DoFindPath` (read), `AdvancePath` (rewrite), `ScheduledMoveToGoalEntity 0x102800c0` and `ScheduledFollowPath 0x102801e0` (write; the latter calls `0x10280de0(schedule)`, sets `+0x5de8`, builds the goal record with type 3 and calls `SetGoal(nav)`), `CAI_ChangeTarget::InputActivate 0x101c99c0` (writes 0), `ReadyNPC 0x10273ad0` (`m_pGoalEnt = FindByName(m_target)`, then `SetState(1)` + `0x10280de0(this,3)`), the base `StartTask 0x102827f0`, and `CNPC_VCamera 0x10369930`. The `aiscripted_schedule` modes 4/5 (`0x101a98c0`) reach it through `ScheduledFollowPath`; Troika task `0x120` reaches type 3 through its own `SetGoal` call (not re-read here). What a script can observe: `OnPass` per corner in order (activator = the NPC), the NPC's `m_pGoalEnt` moving corner to corner, and the schedule's `TASK_WAIT_FOR_MOVEMENT` ending on the last corner.

**(c) Success vs failure.** Success: the goal waypoint (the last laid, `|= 8` set at `0x102f24d1..0x102f24d8` ONLY when the loop count is `< 0x80`, the `CMP EAX,0x80; JGE` at `0x102f24ca`) is reached -> `OnMovementComplete` -> `TaskComplete`. A null `GetNextTarget` ends the chain successfully at the last corner (`piVar7 == 0` breaks the loop); a chain of `0x80` or more is truncated and its last waypoint is NOT flagged goal (so the walker reaches waypoint 128 with `AdvancePath` still wanting a next, whose `+0x30` is 0: it falls to the pop, the "Force end of route" message, and sets the goal bit there). Failure: a null initial `m_pGoalEnt` builds nothing and returns 0 (`LAB_102f2614`) -> `SetGoal` refuses -> `OnNavFailed`, `TaskFail 0x0c`; a blocked corner-to-corner leg fails through `CAI_Navigator::Move` exactly like any leg (Q3). The type-3 arm never calls the route builder `0x102f2060`, so no node graph, link or stale bit is consulted for the chain: the legs between corners are straight local walks.

#### Q3 - the step-height rise

Confirmed, with one refinement. The ground arm walks the leg in 16-unit segments (`0x102e4f50`, segment cap 16.0, stop 0.001), each segment through `CAI_MoveProbe::CheckStep 0x102e4160` (name string `0x1060ec0c`), whose step record holds slot 522 (`StepHeight`, `+0x828`, read by `0x102e7e00`; 18.0 for every non-species class) as the up-step and slot 523 as the down-step; a wall taller than the raised trace is a blocker whose class through `0x102e2d70` is `-2` (world), `-1` (entity) or `-3` (NPC), and the final z is refused (`*param_6 = -2`, blocker set to the world entity) when `|final z - requested z| > max(hull height x 0.5, StepHeight + 0.1)` (36.0 on hull 0). So a discrete ledge of 18 to 40 (the graph builder's `CAI_TestHull` steps 40) is refused at run time by the NPC's own probe with status -2, while a continuous ramp reaching the same height is not limited by step height at all (there is no slope limit; `navigation-jump-links.md:997`). The navigator then treats the refusal as follows: the pathfinder never probes a non-stale link (the predicate `0x102ff960` accepts when `link info & 1` is clear), so the route is planned through the link and walked to the ledge; there `MoveEnact 0x102ef870` returns a negative status (and notifies the movement sink slot 5), `MoveNormal` returns it, `CAI_Navigator::Move 0x102eff40` leaves its loop and, unless the status is -3 (NPC blocker), calls `0x102f1fa0(nav, 4.0, 0)` (the stale-link mark: `link info |= 1`, `link+0x68 = curtime + 4.0`, blocker none; only when `nav+0x50 m_fRememberStaleNodes` is set, a path is live and both node ids are valid), then unconditionally `OnNavFailed 0x102eeae0(0x0c, 1)` -> `TaskFail 0x0c`. There is no re-path inside `Move`; the re-plan happens on the schedule's next request, where `0x102fce80` re-probes the stale link (`0x10304a40`) each new `curtime` until `link+0x68` passes and then clears the bit unprobed (so the ledge link is retried about every 4 s). For the port this is a pin, not code: the 18-unit NavMesh agents already exclude those rises, and the only divergence is timing, not code: retail fails `0x0c` after walking to the ledge and 4 s-marks the link, while the mesh either routes around from the start or refuses the route at request time with the same `0x0c`. Record it as a named modernization; build code only if a map script is found to observe the walk-to-the-ledge failure.

**Unrecovered:** (1) the engine-side rebase of `curtime` at map load and at save restore (engine.dll was not read; the gate is relative so it does not matter here); (2) the `ObjectCaps` of `CAI_NetworkManager` (the "recreated, not restored" claim is inference; the restore-path `Precache`/`Spawn` selection is read at `0x101a2e40`, bit 4 of `ObjectCaps`); (3) slot 614 (`+0x998`) body called by `0x1028d910`, and what `0x101ce440` does at the two "Rebuilding" points; (4) which entities the class list `0x106eb5d8` walked by `0x1028d8d0` selects (flag `0x40` argument); (5) whether a walker's `+0x164` (`m_flSpeed`, written from the corner's `speed`) has any reader; (6) the Troika task `0x120` call site was taken from `navigation-jump-links.md:645`, not re-read; (7) the step-up decision inside `0x102e3450` (the trace helper `CheckStep` calls) and the exact `MoveEnact` negative-status mapping to `-2` were read structurally, not per-instruction; (8) the Troika `NPCThink` arm where the flag is 1 and the gate is false (AI disabled / ai_step) leaves `m_flNextThink` untouched, and what the think dispatcher does with an unchanged value was not read.

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
`RunTask` arm for `0x16` or `0x84`, so after `SetGoal` those tasks are finished by `SetGoal`'s own
find wrapper `0x102f1dc0` — `TaskComplete` at route submission, `OnNavFailed(0x0c)` on a refusal
(`npc-ai/schedule-kernel.md` § "`SetGoal` DOES complete the task", 2026-09-21; the cower task's
hint branch also calls `TaskComplete` itself at `0x102a2a4b`) — never by a run arm; `FACE_HINTNODE`'s run arm completes
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
is synthesised from its readers and the `*_PATHING_HULL` rows); the enum spelling of 23; the intent
behind the `BuildExtrapolatedRoute` split. (Closed 2026-09-20: which shipped schedule issues
`CNPC_VVampireBoss::StartTask`'s task `0x14e`, the arm that sets both fields to 0 after a
monster-model swap. It is `TASK_VVAMPIREBOSS_SET_AS_MONSTER`, carried by three programs — the boss's
`0x159`, the Sheriff's `0x15b` and the SabbatLeader's `0x163` — and reached by six classes, not the
Sheriff alone: `npc-ai/programs.md` § "The boss transformation programs".)

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

### The flying movers, walked — Crow and ManBat (2026-09-21, 0018 story 12)

_The three reads story 12 owed: the species' flight speeds, what the fly arm of `MoveLimit` does
when blocked, and landing._

**Flight is a navigator type, not an entity move type.** `CAI_Navigator` names type `0` Ground,
`1` Jump, `2` Fly, `3` Climb (`0x1027e760`; the literal `Fly` at `0x105cd440`). Every NPC keeps
entity move type `4` — `CAI_BaseNPCTroika::Spawn` sets it through slot 93 (`0x10298d30`,
`10298fe1`-`10298fed`) and `PhysicsSimulate` dispatches `4` to `PhysicsStep` (`0x10040390`) — so a
flying crow is a `PhysicsStep` entity whose NAVIGATOR is in type 2 with flag `0x400` set. Takeoff
sets both (`0x103580d0` crow, `0x1038c170` manbat); landing clears the flag, returns the navigator
to type 0 and reasserts entity move type 4 in the same two bodies.

**Only two species fly.** The slot-525 movement overrides are `CNPC_Crow 0x10357ba0` and
`CNPC_VManBat 0x1038b120`; **`CNPC_VGargoyle` inherits the base override `0x1027da90` and has no
flight task or type-2 transition at all** (`0x10377c70`, `0x103790d0`, `0x103793e0`), despite its
hull 14 in both hull words (`0x10377a60`). Story 12's premise that the gargoyle is a flyer does not
survive the binary.

**Speeds.** The crow is a flat **170** source units/second (`.rdata 0x10454028`), written straight
to `SetAbsVelocity` by `0x10357be0` with no ramp; takeoff uses the same 170 with a random heading
and a vertical factor between 0.1 and 0.5 (`0x10358330`, `10358433`-`10358485`). Its arrival latch
`m_bReachedMoveGoal +0x5f54` trips when the pre-avoidance distance falls under that same 170.
`m_flGroundSpeed +0x654` has no reader on either flyer, so sequence ground speed drives nothing
here. The ManBat accelerates: `0x1038b370` wants **700** when the target's delta-Z is below `-30`
(`.rdata 0x10462868`) and **500** otherwise, clamped toward current velocity by the cvar at
`0x1093b7cc`, with the downward limit scaled by `3.0` (`0x10450010`); its takeoff is **200**
(`0x104492b8`, through `0x1038c250`) and `TASK_MANBAT_FLY_RANDOM` is **500** (`0x10457f5c`,
`0x1038c390`). Four activities (`0x30`, `0xb0`, `0x4b`, `0x1171`) zero velocity before the
calculation.

**The fly arm of `MoveLimit 0x102e6d70`.** The switch caches the PATHING hull `+0x156c` into the
probe and dispatches: case 0 ground `0x102e5d80`, case 1 jump `0x102e6290`, **case 2 fly
`0x102e6090`**, case 3 climb `0x102e6be0`, default status `-4`; the function returns
`status >= 0`. The fly arm is a **3-D hull sweep, not a ray**: it takes the owner's collision
bounds from `+0x270`, calls `CAI_MoveProbe::TraceHull 0x1026e940`, and compares the fraction
against `1.0` (`_DAT_104454c0`). Clear, it writes the requested endpoint and leaves status 0.
Blocked, there are two arms — if the blocker is the caller's EXPECTED blocker (`param_4`) the hit
point is accepted as a partial move with status untouched; otherwise it stores the blocker at
`+0x1c`/`+0x28`, measures the distance reached into `+0x24`, and classifies through `0x102e2d70`:
**`-3` an NPC, `-1` another entity, `-2` world geometry.** A negative status reaching the navigator
is `CAI_Navigator::Move 0x102eff40` -> `OnNavFailed 0x102eeae0` -> `TaskFail` with code **`0x0c`**.
The ground arm differs materially: it is segmented 2-D movement through `0x102e7ba0` / `0x102e4f50`
with step-up, step-down and a final-Z test, and only then the same classifier. **Flight has no step
or floor contract at all** — one sweep, and the generic consumer `0x103048d0` may triangulate
(`0x103059d0`) or retry an NPC blocker under mask `0x2400b`.

**But the two flyers do not consume that generic failure.** The crow steers with its own avoidance
helper `0x10357e50`, so a blocked sweep yields a steer vector, never a code. The ManBat sweeps with
its own mask `0x202400b` in `0x1038bec0` and, on any fraction below 1.0, returns straight up
`(0,0,1)` and arms its flap timer — it does not distinguish an NPC from a wall. The conditions
`COND_FLYING_WALL_HIT 0x36` and `COND_FLYING_NPC_HIT 0x37` are the schedule-visible half.

**Landing.** The ManBat lands properly: `TASK_MANBAT_LAND 0x150` (registered by `0x10389f80`),
run by `SCHED_MANBAT_FLY_END` as `FIND_LANDNODE -> FLY_TO_HINT -> STOP_MOVING -> LAND -> wait 1 s`
(`0x106423b8`). `StartTask 0x1038c390` sets `ACT_LAND 0x30` and zeroes velocity; `RunTask
0x1038d130` waits out the activity, calls `0x1038c170(this, 0)` and settles on `ACT_IDLE 1`. The
stun route is separate — `SCHED_MANBAT_FLY_STUN` (`0x106422d0`) runs `FIND_LANDNODE ->
FALL_TO_GROUND`, waits 10 s and breaks the spotlight; `TASK_MANBAT_FALL_TO_GROUND 0x14c` sets
`ACT_FALLING 0x2f`. **The crow has no land task.** Its flight tasks are
`TASK_CROW_FLY_TO_HINT 0x14d` and `TASK_CROW_FALL_TO_GROUND 0x151` (`0x10359270`); the failure
schedule runs the fall then `SCHED_CROW_HOP_AWAY` (`0x10628ca0`), and because
`CNPC_Crow::SelectSchedule 0x10358ce0` answers `SCHED_CROW_IDLE_FLY` while the navigator is type 2,
a crow that reaches its hint keeps flying rather than perching. **Neither species reads hint yaw on
arrival** — both take only the position through slot `+0x370`. On death the ManBat clears its
flight state (`0x1038e8c0` -> `0x1038c170(this, 2)`); the crow has no death override (it inherits
`CAI_BaseNPC::Event_Killed 0x10265ad0`) and so dies with flag `0x400` still set.

**The two fly-to-hint tasks.** `TASK_CROW_FLY_TO_HINT 0x14d`'s own `StartTask` arm is empty
(`0x10358330`); the search is the PRECEDING `TASK_FIND_HINTNODE 0x40`, type **700**, flags **3**,
radius **5000.0**, storing `m_pHintNode`, and a miss pushes failure code 4 before slot 448
(`103583f5`-`103586a5`). The crow builds no `AI_NavGoal_t` and writes no tolerance — arrival is the
170-unit latch. `TASK_MANBAT_FLY_TO_HINT 0x14b` likewise does not search; `TASK_MANBAT_FIND_LANDNODE
0x14e` searches `(20000, flags 2, r 5000.0)` and `TASK_MANBAT_FIND_FLYNODE 0x14f` searches
`(20000, flags 0, r 5000.0)` with one retry at node mode 1 (`0x1038c390`, misses at `1038c571` /
`1038c5ee`). Its arrival is `m_bReachedMoveGoal +0x6664`, set when the remaining distance drops
under `0.2 x |velocity|` (`0x10449198`).

**`TASK_MANBAT_FLY_TO_HINT` is named by THREE schedules, not six** — `SCHED_MANBAT_MISSILE_ATTACK`,
`SCHED_MANBAT_FLY_END` and `SCHED_MANBAT_FLY_CONTINUE` (`0x10642080`, `0x106423b8`, `0x106424b0`,
all registered through `0x10389f80`), against the task text at `0x10642a4c`. Two independent passes
reached the same three. The fly simplifier `0x102f1690` (the `& 0x22` test) has **zero callers** and
is on neither species' path.

The ManBat acceleration cvar `0x1093b7cc` is `manbat_delta`, default **600.0**, read as a float by
`0x1038b370` (named 2026-09-21, `npc-ai/convars.md`). **Unrecovered**: the
client-side ragdoll decision for a crow that dies with flag `0x400` set; the semantic names of
`MoveLimit`'s fifth and seventh arguments (every flight-like caller passes zero).

### The crosswalk wait, walked — `0x102a0bc0` and the inert four-phase clock (2026-09-21, 0018 story 7)

`0x102a0bc0` (reached from the path advance `0x102f0400` and the NPC body `0x10298340`) is the
whole "must I wait at this curb" test, and it is gated four ways before it looks at anything:

```
waypoint != 0
waypoint+0x10 (node index) >= 0
waypoint+0x30 (link) != 0
waypoint+0x28 & 4            // the crosswalk waypoint flag
goal type == 8               // 0x102ee620 — PEDESTRIAN only
```

so a non-pedestrian never waits, exactly as the story says. It then indexes the node array
(`navigator+0x5d34` -> `+0x2c`) with a bounds check whose failure bumps the counter `DAT_106c994c`
and yields a null node, resolves the link with `0x102f96e0`, and tests

```
link+0x64 & (0x10 << (((int)curtime >> 4) & 3))
```

— a **four-phase clock**: bits `0x10`, `0x20`, `0x40`, `0x80` selected by `(curtime >> 4) & 3`, so
the selected phase advances every 16 seconds and wraps every 64. A set bit means red and the NPC
is sent to the wait `0x102a0b90`.

**That rotation is unobservable in the shipped game.** The only writer of those bits is
`0x102f97c0`, reached from `CAI_Hint::InputWalk 0x102d0a50` and `InputDontWalk 0x102d0a80`, and it
clears or sets the whole nibble `0xf0` at once. So every shipped link is either all-four-set
(always red, whichever phase is current) or all-four-clear (always green), and the map's own
`logic_timer` — 40 s on `sm_hub_1` and `hw_hub_1`, absent on `sm_hub_2` and `sp_theatre` — is the
only thing that changes the state. **A port that models the crosswalk state as one boolean per
link pair is behaviourally identical to retail for all shipped content**; a port that implements
the 16-second phase rotation is implementing an arm no map can reach. The rotation is recorded
here so the choice is a decision rather than an omission.

### The `146-184` pair, closed — a barrel, and a ray where retail sweeps a hull (2026-09-21, 0018 story 3)

§ "Zones end where the geometry does" left one finding open: on `sp_tutorial_1` the zone 11 / 12
boundary at `soc_int_locked_door` has a pair, nodes `146-184`, that "reads open at chest height and
is unexplained". **It is explained, the door is not involved, and the earlier test was the wrong
test.**

**What a zone is.** A zone is a connected component of the node adjacency list, nothing spatial.
`0x102f49c0` runs only AFTER links are built: isolated nodes get zone `1`, flood fills start at
zone `4`, and `0x102f4940` writes `node+0x94` while following every entry of `node+0x7c`, taking
the opposite endpoint from link `+0x04`/`+0x08`. It inspects no hull motion, link info, node type,
flag, distance or geometry. **Zones are computed at REBUILD, not at load**: the load path
`0x102f5bd0` reads the serialized zone word straight out of the file and never calls `InitLinks`
or the zone pass (the rebuild chain is `0x102f6610 -> 0x102f4e00 -> 0x102fb4e0 -> 0x102f49c0`,
chosen by the BSP-vs-AIN timestamp compare at `0x102f688d`). So the question is never "why are
these two zones apart" but "why is there no link", and the answer is in the builder.

**The candidate pass is open; the link test is not.** `0x102fa630` (from `0x102fac00`) proposes
neighbours by tracing hull-2 node positions under mask `0x2000b`, keeping ordinary nodes within
**800** units (`0x10457ac4`) and type-3 nodes within **2048** (`0x1046bacc`). At 109.56 units the
pair is well inside. `CAI_Node::InitLinks 0x102fb4e0` then runs the real per-hull fit, stand, walk,
jump and climb traces (`102fb706`-`102fbe4b`) under the same `0x2000b` — which carries SOLID,
WINDOW, GRATE and MONSTERCLIP but **not `MOVEABLE 0x4000`**, the reason links run through standing
doors. There is no zone-count cap that could split a component; the only limit is 1,500 nodes at
`0x102f47f0`, which aborts a rebuild rather than dividing a zone.

**The shipped data** (`Unofficial_Patch/maps/graphs/sp_tutorial_1.ain`, `Version 30`,
`NumHulls 22`, `UsedHullBits 0x80001`, `ZoneCount 13`, 203 nodes, 429 links; every figure below
re-parsed from the file):

| node | origin | type | flags | zone | links | hull-0 offset |
|---:|---|---:|---:|---:|---:|---:|
| 146 | `-7046, 3167, 6863` | 2 | 0 | 11 | 1 | **`+48.01`** |
| 184 | `-7078, 3255, 6859` | 2 | 0 | 12 | 1 | `-4.87` |

At hull 0 they stand at `(-7046, 3167, 6911.01)` and `(-7078, 3255, 6854.13)` — **109.5597** apart
and **56.9 units apart vertically**. Node 146's only link is 301 (`146 -> 190`); node 184's only
link is 398 (`183 -> 184`). **No link in the file joins 146 to 184, and no link joins zone 11
(64 nodes) to zone 12 (22 nodes) anywhere.** They are each other's nearest cross-zone pair
(next: `190-184` at 139.07, `144-175` at 144.40, `145-165` at 145.29).

**Why.** That `+48.01` hull-0 offset is the whole answer: every other node on this map offsets
`-3.87` or `-8.87`, so **node 146 does not stand on the floor — it stands 48 units up, on top of a
prop.** The prop is static-prop row 679, `models/scenery/structural/society/barrel.mdl` at
`(-7030.38, 3193.49, 6884)`, angles `(-90, 160, 0)`, solid byte 6, whose transformed collision
envelope is about `x -7050.50..-7010.26, y 3173.68..3213.30, z 6857.12..6910.88` — and
`6910.88` is node 146's own stand height to within a hundredth. The model is in the **patch**
BSP's static-prop dictionary and **not in retail's**, which is exactly the patch-first pairing this
document already requires.

A centre ray from 146 to 184 at floor, chest or head height misses that envelope, which is why the
earlier pass reported "open". **Retail does not cast a ray; `InitLinks` sweeps the hull.** The
human hull `(-13,-13,0)..(13,13,72)` enters the barrel immediately on leaving node 146 and stays
inside it for roughly the first half of the segment, so the walk trace fails and no link is built.

`soc_int_locked_door` (entity row 1546, `func_door_rotating`, model `*163`, brush 2986, contents
`0x1`, closed world box `x -7156..-7153, y 3143..3197, z 6854..6961`) is **disjoint in X from the
segment at every height** and has nothing to do with this pair — the section's attribution of the
11/12 boundary to that door is wrong for `146-184` specifically, though the door still separates
the zones elsewhere. Both nodes are plain `info_node` rows (`WCLookup` 532 and 586) carrying no
`hinttype`, so `CNodeEnt::Spawn 0x102d78d0` builds no hint for either and both node hint pointers
stay empty.

**The lesson for the port**, and the reason this was worth chasing: an open-ray check against BSP
brushes is not retail's admission test. It misses static props entirely and it misses hull width.
Any future "these two nodes should be joined" finding has to be reproduced with a hull sweep
against props as well as brushes before it is called a defect.

### `TASK_GET_PATH_TO_RANDOM_NODE` `0x1f`, walked (2026-09-21, 0018 story 4)

_The read story 4 owed. **It is not a draw from a set of places.** It is a random WALK over the
AIN adjacency lists, and it reads links, link info, per-hull motion words and stale-link state at
run time._

**Registration.** `0x10316ff0` calls `0x102ea130(&DAT_1090ff20, "TASK_GET_PATH_TO_RANDOM_NODE"
0x105d52a8, 0x1f, ...)`, so the task id is **`0x1f`**. The base `StartTask` dispatcher indexes
`iTask - 1` through the byte table `0x10287138`; entry `0x1b` at `0x10287156` lands on the arm
**`0x10285d7f`**. Troika delegates (byte `0xaf` at `0x102a7ad2` -> `0x102a77e2` -> the base thunk).

**The arm, entire:**

```
direction = BodyDirection2D()                 // slot 368 -> 0x10331950
distance  = ResolveTaskDistance(flTaskData)   // slot 418; base 0x102702d0, Troika 0x102bf6e0
if (!0x102ed940(navigator, distance, direction, 0))  TaskFail(0x18)
else                                                 TaskComplete(false)   // 0x10273e80
```

The failure code is **`0x18`**, not `0x0c`, `0x1d` or `0x0e`. `RunTask` for `0x1f` is an empty
`break` (`0x10288780`, via byte `0x04` at `0x102897b1`) — **the task completes synchronously in
`StartTask`** and has no run phase.

**The hull is the PATHING word.** `0x102ed940` caches `npc+0x156c` into both `navigator+0x08` and
`pathfinder+0x08` (`102ed946`-`102ed977`) before anything else, and the nearest-node query loads
`+0x156c` at `102f3c7c` and hands it to `CAI_Node::GetPosition 0x102fb0d0` at `102f3e23`. So the
Sheriff / Hengeyokai / Ming Xiao split between `+0x1568` and `+0x156c` is observable here.

**The start node** comes from `0x102f3c10` (via `0x102ed430`), which may reuse the 20-entry cache
`0x102f4520` and otherwise searches a box of half-extents `(800, 800, 200)` — `(2048, 2048, 2048)`
for a mover with capability bit `0x4` — keeping ten candidates. A candidate must pass, in order:
node type 3 requires capability `0x4` and type 2 requires capability `0x1` (`102f3c9e`-`102f3cb3`);
virtual slot 527 must not call it unusable (`0x1027db30`); `CanFitAtNode` under mask `0x2400b`
(`0x102f1900`); and the connection trace `0x102f3900`, which prefers a candidate with no
intervening NPC. No start node means `-1`, and the task fails with `0x18`.

**The walk, `0x102ff3e0`.** The start node must have a nonzero adjacency count at `node+0x78` or
the walk returns null at once. Neighbours come from `node+0x7c` through a **rotating per-node
cursor at `node+0xa4`** (advanced and wrapped by `0x102f9750`, read by `0x102f9780`) — so
neighbour order is MUTABLE RUN-TIME STATE, not serialized link order. A destination already in the
locally allocated visited bitset (`0x102fe9a0` — not the serialized neighbour bitvector at
`+0x90`) or equal to the previous node is rejected.

The **cooldown `node+0x9c` is two-tier, not a filter**: a node whose cooldown has expired joins the
primary candidate list, one still cooling joins a DEFERRED list, and the deferred list is used only
when the primary list is empty (`102ff799`-`102ff7ce`).

Selection: with a zero direction vector it is `RandomInt(0, count - 1)` on `0x1070b244` slot 2 (the
engine stream) over the primary list, falling back to the deferred list when primary is empty —
**one draw per selected step, no fixed draw count, and no `RandomFloat` anywhere in the task**.
With a nonzero direction it dots the normalized step against the current direction and keeps only a
STRICTLY greater score, so ties keep the earlier candidate; the direction is then REPLACED by the
step taken, so the body's heading only really steers the first choice. Accumulated distance is the
sum of 3-D distances between RAW node origins (`102ff6c9`-`102ff70b`). The walk ends when that
reaches the resolved distance or the iteration guard passes `0x14`, and **a type-4 node is never
accepted as the final node**.

**The link predicate `0x102ff960` is where the run-time link reads live**, and it is the reason
this task cannot run on a links-free asset:

| read | offset | effect |
|---|---|---|
| link info | `link+0x64 & 0x1000` | rejects the link outright |
| **per-hull motion word** | `link+0x0c + 4*hull` | AND-ed with the NPC capability word (slot 513, `+0x804`); zero rejects |
| far endpoint | via `0x102dda40` (`link+0x04`/`+0x08`) | the destination node id |
| destination usability | slot 527 -> `0x1027db30` | rejects |
| jump legality | motion word exactly `2` -> slot `+0x824` | three `GetPosition` calls at the cached pathing hull |
| **stale bit** | `link+0x64 & 1` -> `0x102fce80` (expiry `link+0x68`) | a failed re-probe notifies the blocker through `0x1027de00` and rejects |

Only the ONE motion word the pathing hull selects is read — not all 22. The task reads **no zone
`+0x94`**, no serialized neighbour bitvector `+0x90`, and never calls `IsConnected 0x102f48b0`.

**The winner is installed as a PATH, not a goal.** `0x102fcbd0` rebuilds the predecessor chain,
calling `GetPosition` at the cached pathing hull for each node, and `0x102ed430` writes it straight
into the navigator's path object at `+0x30`: `0x1030ba50(path, 4)` writes path type 4,
`0x1030b4d0(path, chain, false)` installs the chain, `0x1030b8e0(path)` finalises, and the squared
distance to the endpoint goes to `navigator+0x14`. **`CAI_Navigator::SetGoal 0x102ecd20` is never
called and no `AI_NavGoal_t` or goal tolerance is built** — the `4` is the path object's own
`+0x5c` type field, not a goal type.

**Who issues it: 15 schedules** (the task text occurs 16 times in `vampire.dll`, once as the
registration literal and 15 times inside schedule blobs; operands verified in file order):

| schedule | operand | | schedule | operand |
|---|---:|---|---|---:|
| `SCHED_TROIKA_FLEE_RANDOM` | 3000 | | `PATROL_RUN` | 200 |
| `SCHED_TROIKA_RUN_TO_SAVED` | 2048 | | `SCHED_VANIMAL_FLEE` | 500 |
| `SCHED_TROIKA_PLAYER_ON_HEAD_RUN` | 256 | | `SCHED_VCOP_WANDER_PATROL_SHORT` | 2048.0 |
| `SCHED_TROIKA_FLEE_AND_COWER_STALL` | 1024 | | `SCHED_VCOP_WANDER_PATROL` | 4096.0 |
| `RUN_RANDOM` | 500 | | `SCHED_VCOP_WANDER_AND_VANISH` | 2048.0 |
| `PATROL_WALK` | 200 | | `SCHED_VCOP_RUN_TO_SAVED` | 2048 |
| `IDLE_WANDER` | 200 | | `SCHED_VMING_XIAO_TENTACLE_SCATTER_RANDOM` | 256 |
| | | | `SCHED_VWEREWOLF_RUN_TO_TELEPORT` | 5000 |

**`IDLE_WANDER`, `PATROL_WALK`, `PATROL_RUN` and `RUN_RANDOM` are base schedules** loaded by
`0x102cb690`, and four more are the COP wander/patrol programs (`0x10370b00`). No exported script
names the task; the run-time issuer is always schedule bytecode.

**Consequence for the port.** The hub-at-idle witness — "pedestrians visit places, cops patrol" —
runs through this task, and this task needs adjacency (`+0x78`/`+0x7c`), the rotating cursor
(`+0xa4`), link endpoints, `link+0x64` link info, the pathing-hull motion word and stale-link
state. A run-time asset of "places and no links" cannot host it.

**Decided 2026-09-21 (0018 story 4): the port does not host the walk.** It picks a retail place at
the order's distance — capped at `20 x` the map's median hop, in place of the guard `0x14` — and
lets Unreal route there. The contract above is kept where a place draw can keep it: fail `0x18`,
synchronous completion, a path and no goal, no type-4 endpoint, the two-tier cooldown, the draw on
the engine stream. The spec's story 4 carries the rule and names what it gives up.

### The place set, landed — the node binding, `GetPosition`, the nearest node and the wander pick (2026-09-29, 0018 story 4)

_The reads story 4 made to land, each re-read from the listing and cited at its line in the port:
`Substrate/ElysiumPlaceSet.{h,cpp}` (the network as the runtime stands it), `ElysiumNodeEntity`,
`ElysiumHint::OnPostRestore`, `ElysiumNpcPositions.cpp` (`NavNearestNodeTo`),
`ElysiumNpcBaseStartTask.cpp` (the pick). The cooked side is `UElysiumMapPlaces`
(`/ElysiumBaked/<map>/DA_<map>_Places`), staged by `importers/map_places.py` and baked by
`pipeline/unreal/bake_places.py`._

**`CNodeEnt::Spawn 0x102d78d0`, the counter rule entire (loaded branch).**

- **The four standalone classes** — `info_hint`, `info_node_kick_over`, `info_node_kick_at`,
  `info_node_shoot_at` — never touch `DAT_10926a3c`. With a non-zero (class-forced, `0x102d7d30`)
  hint type they make a hint with `m_nNodeID` -1 (`0x102d7ba5`); with type 0 they print
  `WARNING: Hint node with no hint type!` (`0x102d7bde`) and make nothing.
- **Every other node row** (`info_node_tzimisce` is `info_node` by then; `info_node_link` is
  `CAI_DynamicLink`, not a `CNodeEnt`) makes a hint iff its hint type is non-zero OR it authored a
  `Group` (`0x102d79b8`), and advances the counter exactly once on the loaded branch whether or not
  it made one (`0x102d7a28 INC EDX`). The hint's `m_nNodeID` (`+0x5e4`) is the counter, written by
  `FUN_102d2f30` at `0x102d2fce` before the hint's own `Spawn` — **even out of range**. It is
  attached at `node+0xa0` iff `0 <= counter < NumNodes` (`0x102d7a04 JL` / `0x102d7a0f JGE`);
  otherwise `DAT_106c994c++` (`0x102d7a3b`), and only when a hint was made (`0x102d79fa TEST
  EBX,EBX`): a row with no hint moves the counter and counts nothing. `DAT_106c994c` is one DLL
  global shared by every node reader (the hint lookup `0x102d3e60`, the patrol readers, `0x1027db30`)
  and nothing in the image zeroes it: `0x102f6690` zeroes only `DAT_10926a3c`, the patrol-pool reset
  `0x10307d00` only its cursor `DAT_109363f8`. The port keeps it as one process static
  (`ElysiumAiNetwork::NodeMissCounter`); nothing reads it for behaviour.
- **Every node authoring entity is removed** on every arm (`0x1000e255` → `0x101cd970`).

The port walks the def array in BSP order inside `FElysiumEntityWorld::Load`, after
`BeginMapSpawn` (`0x102f6690`'s zeroing), ahead of construction: only a node row can move the
counter and no map entity's `Spawn` creates one, so walking the node rows first lands every id
where retail's interleaving does. A row that makes no hint is retired and gets no entity. The
unloaded arm (each row ADDS a node, `0x102f47f0`, for the rebuild) is never taken: every shipped
map carries its AIN, and the bake is the only producer.

**`CAI_Node::GetPosition 0x102fb0d0`, every arm.**

| type | answer |
|---|---|
| 4 (climb), `0x102fb0d9 CMP EAX,4` | `s = width(hull) * 0.5 + 8.0` — `0x102d6180` is the hull row's `maxs.x - mins.x`, the constants two doubles (`0x10449270`, `0x1049a148`), the sum stored as a float; the yaw `+0x6c` scaled by `_DAT_1044eb08`; then, first bit set wins: `4` (`0x102fb0f8`) `origin + fwd*s`; `8` (`0x102fb166`) `origin - right*2s - fwd*s`; `0x10` (`0x102fb20b`) `origin + right*2s - fwd*s`; none `origin - fwd*s` |
| 2 (ground), `0x102fb2f6 CMP EAX,2` | the origin with `zoffset[hull]` (`+0x14 + 4*hull`, `0x102fb303`) added to Z |
| any other | the raw origin (`0x102fb31f`) |

The hull argument can be any of the 22 — the pathing word `+0x156c` from the routing sites, the
standing word `m_eHull` from seven others (§ "The two hull words") — so **a place row carries all
22 offsets**, retail's own row, not one per baked agent. Retail indexes node and hull blind; no
caller passes either out of range, and the port answers "no position" for one.

**Save: retail saves no node.** There is no datamap for `CAI_Node` or the network (the
`ai_network` entity's `CAI_NetworkManager` map is two function-table rows), so a restored map
starts every node at its constructor words (`0x102fc5d0`: `+0x9c` 0, `+0xa0` null) and the hints
relink themselves through their SAVED `m_nNodeID`: `CAI_Hint::OnRestore 0x102d3ec0` (slot 130)
runs the base `0x100aa5a0`, then the node lookup `0x102d3e60` (-1 answers no node and counts
nothing; an id inside the network answers it; any other id bumps `DAT_106c994c`), then teleports
the hint to the node's RAW origin (vtable `+0x2d4`) and sets `node+0xa0`. Ported as
`FElysiumHint::OnPostRestore`; the place set saves nothing.

**The hint's node arms, `0x102f46d0` / `0x102f47b0`.** The network halves of `CAI_Hint::
GetPosition 0x102d1180` and of the hint yaw `0x102d12e0`: `vec3_origin` / `0.0f`
(`_DAT_104454c4`) for a network with no node array or an id outside `-1 < id <= count`, else
`GetPosition(node, npc+0x156c)` / the node's `+0x6c`. The bound is `<=`, so `id == count` reads
the slot past the last node of `m_pAInode` (a `new[MAX_NODES]` whose tail is unwritten).
**Named divergence:** the port answers the origin / `0.0` for `id == count` instead of reading past
the array.

**`0x102f41b0`, the network's nearest node to a point** (no NPC, no hull):

```
if (*network == 0) return -1;
cached = 0x102f4520(network, pos); if (cached != -2) return cached;       // not ported
list = ListNodesInBox(10, pos - 2048, pos + 2048, CNodePosFilter(pos));    // 0x102f32f0, _DAT_1046bacc
for (node : list, nearest first)
    if (0x102f39a0(NULL, pos, node->origin, &flag)) { 0x102f45f0(network, pos, node, 0x17); return node; }
0x102f45f0(network, pos, -1, 0x17); return -1;
```

`CNodePosFilter` (`0x102f44b0` / `0x102f44d0`) admits every node and scores the squared distance
to the RAW origin. The trace `0x102f39a0` is a line (no extents) under mask `0x202400b`, clear
exactly at `fraction == 1.0` (`_DAT_10449280`), and its filter `CTraceFilterNearestNode`
(`0x102f37d0`) admits as a blocker only an entity whose `GetMoveType()` (slot 94) is 0 — the world
and static brushes, never a character. `ListNodesInBox` orders BOTH its queues with `0x102f3770`
(`Less(a, b) = b.dist < a.dist`), so the head is the NEAREST; once the queue is full a candidate
enters only when strictly nearer than the head, which it evicts — the list always holds the
nearest in-box node beside the first-admitted others. The port keeps that quirk verbatim. The
20-entry cache `0x102f4520` / `0x102f45f0` is engine machinery in front of the search and is not
ported: the search runs every call.

**`GatherHintNodes`' selectors read the hint's own origin.** `CNPC_VSheriffMan::SelectTeleportNode
0x103b0630` and `CNPC_VSabbatLeader::SelectTeleportArchway 0x103a9540` walk the live hint list
(`DAT_10925450`, next at `+0x5d8`) and score `GetAbsOrigin()` (vtable `+0x364`), not the node.

**The node cooldown `+0x9c`.** `0x102ff3e0` counts a node's cooldown expired when `+0x9c <=
curtime`, and refuses a distance `<= 0` outright. `CAI_StandoffBehavior::vfunc13 0x102c7600`
writes the NODE's `+0x9c` (through `0x102d3e60`), not the hint's `+0x5ec`. The claim-time write
(`+1.0`) is story 9's; the place set holds the storage and the read.

**`0x102ed430`, the install.** Path type 4 at `0x102ed4a6` (`0x1030ba50(path, 4)`) and the
endpoint distance² at `navigator+0x14` at `0x102ed4e9`; `SetGoal` is never called (§ above).

**`TASK_WANDER 0x76` → `SetWanderGoal 0x102ed540`.** Five tries of `RandomFloat(min, max)` for the
distance and `RandomFloat(0, 359.99)` (`0x43b3feb8`) for the heading, each handed to the radial
probe `0x102ed610`; all five failing, `SetRandomGoal(1.0, vec3_origin)` (`0x102ed5c2`). **No
shipped schedule issues it** — it is only registered in `cai_basenpc`. Retail's walk always takes
one hop (it stops only AFTER a step), so the fallback reaches a neighbouring node.

**The port, as landed.** `StartTaskSetRandomGoal` is the capped point pick decided 2026-09-21
(named modernization; the spec's story 4 carries the rule), with two port constants of no retail
source: a route longer than `kWanderDetourRatio` **2.0** × the resolved distance is a detour, and
at most `kWanderMaxDraws` **5** draws are made. A dropped pick leaves the pool, and tier and band
are taken again from what remains. `TASK_WANDER`'s draws are made and its radial probe is story
5's seam (false), so it always reaches the pick, where an order of 1.0 finds no place and fails
`0x18` — **named divergence**, unreached by content. The route length is
`IElysiumNpcMotor::RouteLengthTo` (a synchronous Unreal path test on the body's agent; story 6
absorbs it) and the install `InstallPathNoGoal` (path type 4, `navigator+0x14`, no goal record,
no tolerance; story 5 absorbs it). One more **named divergence**: the Werewolf's
`GetNearestNodeToPlayer` (`FUN_103d0bf0`) caches a node POINTER keyed on zero; the port caches the
node INDEX, so a nearest node 0 re-queries on the next call.

**The census over the graphs retail loads (2026-09-29, `map_places`).** 11,517 type-2 nodes, 37
type 1, 4 type 4 (climb), never type 3 (air); `flags` 1 on 4 nodes, its writer unrecovered. All
108 maps stage with 0 out-of-range hints and 0 disagreements between a node's `wcId` and its
bound row's authored `nodeid`. The witnesses: `sp_tutorial_1` 203 places / 49 bound hints,
`sm_hub_1` 578 / 274. Wander caps (`20 x` the median link length, Source units): human
tutorial 2,942.141 / hub 3,113.326; rat 3,097.891 / 3,185.672. The thug's patrol points `A1..A3`
(BSP rows 433–435, authored `nodeid` 39/40/41) are network nodes 15/16/17; the hub's six
crosswalk rows (BSP 1613–1618) are nodes 258–263, joined in 8 pairs.

**The reports (2026-09-29; observations in `verify/nav/report.json`, never counted by the gate;
21-9 owns pins).** Places off mesh: 0 on both witnesses. Uncovered points (a hint, patrol point
or interesting place with no node inside `0x102f41b0`'s ±2048 box): 0. Zone pairs joined on the
HUMAN mesh: tutorial 2 of 45 — `4/7`, and `11/12`, which is the `146–184` barrel pair (§ "The
`146-184` pair, closed") — hub 3 of 6 (`1/5` at a 9,978 cm path, `1/6`, `5/6`); on the rat mesh
tutorial 2, hub 1 (rat-mesh noise is 21-10's). Same zone, no mesh path: 0. Per story 3's lesson,
none of the joined pairs is a defect claim until a hull sweep against props reproduces it.

**Still open.** The hint searches, claims and `IsHintAvailableToMe` (0018/8); the cooldown's
claim write (0018/9); the crosswalk link over the baked pairs (0018/7); `SetGoal`'s goal-flag-2
node route and the radial probe `0x102ed610` (0018/5); `CheckStandPosition` and the hull trace
inside `CanFitAtNode 0x102f1900` (0018/6); the `flags` 1 writer.

### How the graph builder picks a node's links — `InitNeighbors` `0x102fac00`, walked (2026-09-21, 0018 story 4)

_The links are DERIVED data: designers placed nodes, the engine laid the links. Read to answer
whether the hop graph can be rebuilt from places plus walkable floor. Engine record — nothing here
runs on a loaded graph (`InitLinks` is reached only from the rebuild, § "The loader, walked")._

**The rebuild, `0x102f4e00`, in order:** per node `0x102fa510` (type checks; a ground node whose
hull offset is under `-200.0` `0x1049df14` prints `ERROR: Node ... too low`); per node
**`InitNeighbors 0x102fac00`**; `0x102cc9f0`; every adjacency count `node+0x78` zeroed; per node
`InitLinks 0x102fb4e0`; `0x102f99b0`; zones `0x102f49c0`.

**Candidates — the visibility pass `0x102fa630`.** Skipped whole for a type-1 node. A node at
EXACTLY another's origin (and not climb, type 4) is retyped 1 — the duplicate is deleted. When
`node+0x74` bit 28 is set, a squared-distance pre-filter runs first: `640000.0` `0x1049da5c`
(800²) to a non-air node, `4194304.0` `0x1049adfc` (2048²) to an air one. Then up to four line
traces under mask `0x2000b`, from `GetPosition(hull 2)` to the other's: as placed; both ends raised
`70.0` `0x104528d4`; this end raised; the other end raised. Any clear trace sets the other's bit in
the neighbour bitvector `node+0x90`.

**The cull, `0x102fac00`.** For each set bit `i`: its own bit is cleared; the offset to `i` is
normalised in place (`0x1057966c`, length kept at `[ESP+0x10]`); **over `800.0` `0x10457ac4` —
`2048.0` `0x1046bacc` when `i` is air — the bit is cleared.** Otherwise `i` is tried against every
other set bit `j`: skipped when exactly one of the two is air, and skipped when `i` is climb; else
the two unit directions are dotted and **at `dot >= 0.9` (`double 0x104493f0`, `FCOMP` at
`102faf51` — a 25.8° cone) the FARTHER of the two loses its
bit; on equal distances `j` loses** (`102faf60`–`102faf99`). The inner loop does not break when
`i` itself is cleared, so a culled candidate goes on culling — the result depends on node order.
This cone is why the shipped graphs average about six links a node.

**`InitLinks 0x102fb4e0` adds no rule of its own.** Per set bit not already connected
(`Nodes already connected` / `Sharing previously establish connection` — the relation is
per-node, so a link exists when EITHER end kept the other): for each of the 22 hulls some entity
uses (`0x102f9950`; else `Skipping hull %s because no entities use it`), fit at both nodes
(`0x102f1900`, `0x2000b`); air–air a hull trace, motion 4; climb–climb the same, motion 8; a
ground end, stand at both (`0x102e7270`) then the ground walk `0x102e4f50` → motion 1, and on hull
0 alone `0x102e7e80(…, 0x2000)` sets link info `0x2000`; walk failed with both ends ground, the
jump probe `0x102e6d70` each way → motion `|= 2`. No hull connects → the neighbour bit is cleared
and `NO LINK` logged. Link length is bounded by the 800 above and by nothing else, which the
shipped graphs bear out: across 27,956 human-hull links the longest is 800
(`research/tooling/probes/census_link_lengths.py`; median 174, p10–p90 72–391).
