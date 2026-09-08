# AIN jump links and the navigator jump state

Recovered 2026-09-08 for `0005-park-stealth` requirement 19. Addresses below are in retail
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
An actual lost/aborted path request or movement service reports failure. Engine flight details and their stated
modernization boundary are recorded in `docs/decisions.md`.

The map staging version and jump payload hash participate in level invalidation. Malformed or
missing decoded graphs fail staging with a concrete source/export diagnostic. The pipeline
tests cover the raw tutorial rows, wrong hulls, disabled/stale masks, duplicate directions,
invalid endpoints, floor offsets, unit conversion and invalidation. Native automation exercises
the smart-link callback, physical flight/landing and stop-during-jump state; a rendered tutorial
playthrough remains a distinct acceptance step.
