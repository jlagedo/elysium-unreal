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

Reachability in retail is the graph, not the geometry: a navigator `SetGoal` (`0x102ecd20`)
with no node path for the hull fails, and every path task answers that with `TaskFail(0x0c)`
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
at all, so in retail Jack's walk to either group-32 place and every hunter's patrol or visit
fails at `SetGoal` and runs the program's failure route (`_FAILED`'s 5.1–10 s retry for a
place; the default fail schedule `0x43` for a patrol). The port's runtime Recast mesh
(`ElysiumMapActorLifecycle.cpp`, a projection of the `.hulls` sidecar — world brushes of
player-blocking contents, monsterclip excluded — at the engine's default agent: radius 34 cm,
height 144 cm, step 35 cm) refuses the same routes on `sp_tutorial_1` as partial paths ending
160–240 m short, which matches retail's answer here by coincidence of geometry, not by
contract: nothing ties the mesh's connectivity to the graph's. Retail hull 0 stands
`(-13,-13,0)..(13,13,72)` (66 × 183 cm) and steps 18 units (45.7 cm). UNRECOVERED: whether
`MONSTERCLIP` cut links at graph build, and whether door brushes cut the port's mesh (retail's
links pass through doors; the NPC opens them, `m_hBlockedDoor` / `SelectDoorObstructionSchedule
0x102b7370`). Spec 0002 story 24 owns the reconciliation.
