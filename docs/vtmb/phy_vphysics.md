# `.phy` — VtMB's VPhysics collision models

A VtMB `.mdl` ships with a sibling `.phy` holding the collision model the original game
simulates against: a set of **convex hulls** authored alongside the render mesh, not derived
from it. The retail VPKs carry **2,854** of them; the Unofficial Patch adds and replaces more
as loose files, so a `.phy` must be resolved through the install index (patch shadows VPK),
never straight out of a VPK.

`prop_physics` is the consumer: roadmap 8.4 gives a physics prop these hulls rather than an
approximation of its render mesh. The decoder is `pipeline/src/elysium_pipeline/formats/phy.py`; it emits `props/<stem>.phys`,
which the bake turns into the mesh asset's simple collision.

## Container

VtMB is Source 2003, so it uses the **legacy** surface header — there is no `VPHY` magic, and
`IVPS` sits at +0x2C of the solid body instead.

```
phyheader_t                 { int size; int id; int solidCount; int checksum; }   // size == 16
per solid:  int size;                                       // bytes of surface data
  legacysurfaceheader_t     { float mass_center[3];         // +0x00
                              float rotation_inertia[3];    // +0x0C
                              float upper_limit_radius;     // +0x18
                              int   max_deviation:8,        // +0x1C
                                    byte_size:24;           //   == the solid's size field
                              int   offset_ledgetree_root;  // +0x20, relative to this header
                              int   dummy[2];               // +0x24
                              char  magic[4]; }             // +0x2C, 'IVPS'
after every solid:  a plain-text keyvalues block
```

`byte_size` re-encoding the solid size is the integrity check worth asserting: it holds on all
2,854 files with zero exceptions. The `magic` is decorative: it reads `IVPS` on most files and is
**zeroed on 42 of them**, which are otherwise the same layout (all 42 decode, 100 hulls, every one
convex). Those 42 are not a meaningful class — 38 are ordinary scenery (`securitycam`, `plates`,
`curtains`, `trailer_1pc`, …) and only 4 overlap the ragdoll set. The ragdoll marker is the
keyvalue tail, not the magic.

The header `checksum` is compiler provenance, not a runtime admission key. Fourteen character
source closures in the merged install have a valid MDL and PHY whose stored checksums differ,
including VPK/VPK and loose/loose pairs as well as patch/retail mixtures. The retail module corpus
contains no checksum diagnostic or comparison in the collision-loading path. This agrees with the
VtMB SDK's `VCollideLoad(output, solidCount, buffer, size)` seam: the caller passes `solidCount`
and the bytes **after** this 16-byte header, so the collision loader never receives the MDL
checksum. A converter may report the mismatch as provenance, but rejecting the PHY for it is
stricter than retail.

## Ledge tree

```
compactledgenode_t (28 B)   { int offset_right_node;        // 0 => this node is a leaf
                              int offset_compact_ledge;     // leaf only, relative to the node
                              float center[3]; float radius;
                              uchar box_sizes[3]; uchar pad; }
compactledge_t (16 B)       { int c_point_offset;           // relative to the ledge
                              int ledgetree_node_offset;
                              uint flags:8, size_div_16:24;
                              short n_triangles; short pad; }
compacttriangle_t (16 B)    { uint packed; compactedge_t edge[3]; }
compactedge_t (4 B)         { uint start_point_index:16; int opposite_index:15; uint virtual:1; }
point                       float[4]                        // IVP metres, w unused
```

A leaf's `offset_right_node` is 0; otherwise the **left child follows the node inline** and the
right child is `offset_right_node` away. Each leaf ledge is one convex hull. Triangle corners
index a point pool shared across the ledge, so a decoder must renumber densely per hull.

Every offset is relative to the structure that carries it — `offset_ledgetree_root` to the
surface header, `c_point_offset` to its ledge — never to the file.

## Convexity

Each ledge is convex by construction, and that is checkable rather than assumed: a triangulated
convex polyhedron satisfies Euler's identity **`F = 2V − 4`**. Across the whole retail set —
2,854 files, **7,889 hulls** — it holds with **zero** exceptions. `pipeline/src/elysium_pipeline/formats/phy.py` asserts it at
export, so a mis-parse becomes a hard error instead of a plausible but wrong collider.

This is what lets the bake reproduce a hull *exactly*: each ledge goes to Geometry Script's hull
builder alone (`ConvexHulls`, `MaxConvexHullsPerMesh = 1`, simplification off), which returns
the same hull rather than decomposing or approximating anything.

## Coordinates

```
Unreal cm = (ivp.x, -ivp.z, -ivp.y) * 100
```

This is **not** Valve's documented `ConvertPositionToHL` (`x, -z, y`) — the phy frame is
mirrored with respect to it. The mapping was settled empirically rather than from a source
claim: scoring all 48 axis-permutation × sign combinations against the already-verified render
mesh bounds, over every `prop_physics` model, gives

| rank | mapping | centre error |
|---|---|---|
| 1 | `(x, -z, -y)` | **0.85 cm** / model / axis |
| 2 | `(-x, -z, -y)` | 1.48 cm (X-mirror; most props are X-symmetric, so it scores close) |
| 3 | `(x, z, -y)` | 3.93 cm |
| … | worst of 48 | 17.2 cm |

Independently corroborated: the `society_barrel` hull matches its render mesh bounds to **0.2 cm
on all three axes**.

The full raw-IVP mapping swaps Y/Z as well as negating two axes: its determinant is **−1**.
The earlier determinant +1/winding claim (also in `phy.py::_read_ledge`'s comment) was wrong;
negations alone do not account for the swap. The V4d articulated builder uses convex point
clouds, so it does not consume this triangle winding. A triangle consumer must check/reverse
winding for its actual transform. This corrects the frame algebra, not the measured prop mapping.

## Keyvalues tail

After the solids comes plain-text KeyValues. Ordinary rigid bodies carry one `solid { }` block per
solid plus one `editparams { }`:

```
solid {
"index" "0"
"name" "barrel_phy"
"mass" "5.000000"
"surfaceprop" ""
"damping" "0.000000"
"rotdamping" "0.000000"
"inertia" "1.000000"
"volume" "56683.640625"
}
editparams {
"totalmass" "5.000000"
}
```

`mass` / `totalmass` is VtMB's **authored** mass in kilograms — boulder 2000, break_crate 100,
stool 25, wine glass 1.46. Every `prop_physics` in the exported maps carries
`override_mass = -1`, so this is the only mass the original game ever uses for them. The bake
puts it on the mesh's `BodySetup.DefaultInstance` mass override; the entity's own `override_mass`
still outranks it at spawn, which is Source's precedence.

Two composite character props use a different valid tail shape. `garg_gibbs.phy` and
`throwtaxi.phy` contain compact inline blocks such as:

```
break { "model" "character\\...\\gib..." "health" "100" }
```

`break` names a child model and its authored health. Braces and fields may share one line, so a
line-oriented parser that requires the opening and closing braces on separate lines falsely ends
"inside a block". `PhysModelParseSolid` (`vampire.dll` `0x1002e210`) consumes only `solid` blocks;
the exact breakable-composite consumer of `break` remains to be identified.

## The ragdoll rig

A character's `.phy` carries a second payload in the same tail: its solids are **named after bones**
and are followed by one `ragdollconstraint` block per joint. Nothing in the binary half changes —
the solids, ledge trees and hulls decode identically — so a ragdoll file is a collision file whose
keyvalues say how its hulls articulate.

```
solid {
"index" "3"
"name" "Bip01 R Thigh"
"parent" "Bip01 Pelvis"
"origin" "-3.444549 0.294401 38.981178"
"angles" "87.016258 132.105820 -47.932941"
"mass" "10.162495"
"surfaceprop" "flesh"
"damping" "0.010000"
"rotdamping" "1.500000"
"inertia" "5.000000"
"volume" "843.899231"
"massbias" "2.000000"
}
ragdollconstraint {
"parent" "0"          // solid index, not a bone name
"child"  "3"
"xmin" "-25.000000"  "xmax" "20.000000"  "xfriction" "1.000000"
"ymin" "-40.000000"  "ymax" "20.000000"  "yfriction" "1.000000"
"zmin" "-37.000000"  "zmax" "63.000000"  "zfriction" "1.000000"
}
```

- `name` / `parent` are **model bone names**, and the client resolves them by name at load —
  `"CRagdollProp::CreateObjects: Couldn't Lookup Bone %s"` is the failure. A rig is therefore
  addressed by name, never by bone index.
- `parent` / `child` on a constraint are **solid indices** into the same file.
- The per-axis `min`/`max` are degrees and `friction` is dimensionless. A joint with all six limits
  at `0` and zero friction is a fixed weld — the two accessory solids parented to `Bip01 Pelvis` in
  the example rig are authored that way.
- `origin` / `angles` are **Source units and Euler degrees**, not the IVP metres the hulls use. One
  file therefore carries two frames: `Coordinates` above governs the hull vertices only, and the
  mapping for the solid transforms is settled by the V4d measured frame gate below.
- `massbias` is sparse; `surfaceprop` is `flesh` on the humanoid rigs.

Which models carry a rig, the shape distribution, and every behaviour that consumes it are
`docs/vtmb/physics-interaction.md`.

### V4d D1 — diagnostic builder and frame gate (2026-10-05)

`UElysiumClothBuildLibrary::BuildRagdollPhysicsAsset` consumes resident
`UElysiumPhysicsData`, the actual baked skeletal mesh and a long `_RAGDOLL` package name.
It leaves saving, recipe stamping and mesh attachment to `import_characters.py` (D2).
Cloth's `_PHYS` remains a separate asset. Its receipt carries errors/warnings, no-physics skip,
body/convex/constraint counts read from the generated asset, per-body mass overrides in typed
solid order, `BuilderVersion=ragdoll-v1` and the exact frame recipe as `FrameVersion`.

Source functions: `model_glb/physics.py::_ledge`, `physics_data.py::physics_projection`,
`bsp.py::source_to_unreal/source_quat_to_unreal/source_angles_to_unreal_quat`,
`skeletal_stage/payload.py::_conv_pos/_conv_quat`, `bake_lib.py::set_phy_collision`, and
the existing editor `BuildPhysicsAsset/MakePackage/BoneBindTransform`. S14 §2 distinguishes
their verified equations from the still-unmeasured articulated placement and joint basis.
Retail capability is model-interface slot 18 in `BecomeClientRagdoll` (`vampire.dll`
`0x10090180`); this builder never invents a rig for absent source physics.

The source projection publishes **g=(raw.x,−raw.y,−raw.z)** in metres. The label
`IVP metres, axis-only` therefore does not mean binary IVP coordinates. The builder applies
one conversion, `q=100*(g.x,g.z,g.y)`, with matrix

```
P = [ 1  0  0 ]     q = 100 P g; det(P) = -1
    [ 0  0  1 ]
    [ 0  1  0 ]
M = diag(1,-1,1)    Source translation -> 2.54 M t
A = det(M) M       angular components -> diag(-1,+1,-1)
```

For solid placement, `S_u` has translation `2.54 M origin` and rotation
`M Rz(yaw) Ry(pitch) Rx(roll) M`; it does not use Unreal's `FRotator` convention.
`B_u` is this mesh's accumulated reference-bone transform. The three diagnostic recipes
choose alternatives; none stacks the three expressions:

| Explicit FrameRecipe | Cooked bone-space vertex |
|---|---|
| `diagnostic-solid-local-source-bone-v1` | `B_u.inverse(S_u(q))` |
| `diagnostic-model-local-source-bone-v1` | `B_u.inverse(q)` |
| `diagnostic-bone-local-source-bone-v1` | `q` |

Only `vtmb:model:character/npc/common/cop_variant/regular_cop/regular_cop`, with 15 solids
and 14 constraints, is admitted for diagnostics. Unknown recipes are refused; production
`solid-local-source-bone-v1` is admitted by the measured gate below. D1 prepared the
three candidates without claiming a measured frame. The raw Source-inch, row-major 3×4 `PoseToBone`
is retained evidence; it is not applied to published metre vertices.

The provisional common joint frame is **J_u=B_child**: child bind pivot and mirrored Source
bone basis. Before signed rest offsets, endpoint frames are `B_child.inverse*J_u` and
`B_parent.inverse*J_u`, so their bind-space pivots coincide. UE Frame1 is child and Frame2
parent (`ConstraintInstance.cpp::CreateJoint_AssumesLocked`). This is a diagnostic
hypothesis, not recovered placement. Candidate axis assignment is Source x→UE twist,
y→UE Swing2, z→UE Swing1, conditional signs −/+/−. It deliberately does not assume
Source z→Swing2. Each symmetric limit is half the authored span; a zero span is locked.
The signed midpoint is represented by rotating the **child reference basis**, keeping its
pivot fixed, by `Rz(−mid_z) Ry(−mid_y) Rx(−mid_x)` in this candidate joint basis.
The parent reference remains the common frame expressed in the parent bone.

Bounded reads of the staged regular_cop GLB confirmed authored IDs 0..14 and these inputs:

| Witness | Solid IDs | Authored Source range | Candidate mirrored range / half-span / midpoint |
|---|---|---|---|
| right knee / left knee | 1→2 / 3→4 | x=y=0, z=−95..+4° | z=−4..+95°, 49.5°, +45.5° |
| right elbow / left elbow | 7→8 / 9→10 | x=y=0, z=−120..+4° | z=−4..+120°, 62°, +58° |

Right thigh mass is 9.612112 kg, right calf 4.190075 kg; left thigh/calf are
9.612099/4.190072 kg. These are source numbers, not measured solver masses.
The builder uses `DefaultInstance.SetMassOverride(mass,true)` and reads back the override.
All ledges retain their point clouds, with no primitive fitting, decomposition or requested
simplification. Invalid/degenerate hulls, missing masses/limits, source gaps and inconsistent
authored-ID/typed-ordinal joins fail with unit/solid/constraint/field context. It also checks
that cooking produced a convex for every ledge. Maps/bounds, constraint default profiles,
registry and package dirtiness are updated; the caller saves only successful receipts.

**Named presentation tuning:** adjacent constrained bodies have collision disabled; every
other pair retains self-collision. Chaos defaults own solver iterations, damping and sleep.
No mass or limit is tuned. Friction, authored inertia and surface response remain 0014.
Chaos's multi-axis swing cone is its native solver representation; equivalence to the
retail joint's simultaneous multi-axis admissible set is not established by the hinge sweep.
Do not interpret the stored per-axis spans as proof of that equivalence.

**D1 handoff status (superseded by the integrator measurement below):** no builder was run, no asset
was baked, no screenshot was captured. Before the 34-model bake, build 1 and bake regular_cop
with the explicit diagnostic recipe. In Physics Asset Editor/lab, show collision and constraint
gizmos, compare right thigh/calf against the bind mesh and common knee pivot, then sweep the
Source z joint to **−95° and +4°**. Record anatomy/calf-follow and endpoint pivot agreement.
Mirror-check left knee and both elbow **−120°/+4°** stops with the same matrices. Free fall
does not prove axes or stops. Distinguish the three solid candidates without stacking transforms;
if none is consistent, stop the wider bake and identify the failed recipe/transform.

Append the controlled result here: accepted `S_u`, actual accumulated `B_parent/B_child`,
accepted `J_u`, both reference matrices, axis permutation/signs, authored and measured stop
angles, package/recipe fingerprints and screenshot path. Promote a production frame version
only after that evidence; feed any correction to D1/D2 serially and use the reserved corrected
build/re-bake. These measurements cannot be fabricated under the coder's no-run instruction.

## Missing collision

VtMB does **not** remove a `prop_physics` whose model has no collision model, and it does not
substitute a bounding box. `CPhysicsProp::CreateVPhysics` (`vampire.dll`, vtable `0x10474c44`
slot 223, function `0x10191510`):

```
1019152a  PUSH 6                    ; SOLID_VPHYSICS
1019152c  CALL 0x1000e78c           ; VPhysicsInitNormal(...)
10191535  TEST EDI,EDI / JNZ        ; success path exits above
  ; ---- failure ----
1019159e  CALL <SetSolid>           ; arg 0 = SOLID_NONE
101915b5  CALL [EDX+0x174]          ; two 0 args - SetMoveType(MOVETYPE_NONE, 0)
101915d3  PUSH "ERROR!: Can't create physics object for %s\n"
101915d8  CALL <Warning>
101915e3  CALL 0x1001514a           ; -> CBaseEntity::Relink  (NOT UTIL_Remove)
101915e9  MOV AL,0x1 / RET          ; returns TRUE - the entity survives
```

Retail Source calls `UTIL_Remove` at this point; VtMB warns and leaves the prop standing as
visible, non-solid, non-simulating scenery. The runtime reproduces that: a baked mesh with no
simple collision shapes means the model had no `.phy`, and `FElysiumPhysProp::BuildBody` stands
the body inert.

With the Unofficial Patch installed the path is unreachable in practice — all 27 `prop_physics`
models across the exported maps resolve a `.phy` (19 of them supplied by the patch).

## Which entities get a collision model

`solid` is a keyfield on the **embedded** `CCollisionProperty` map (`datamap_t` `0x10560c20`,
records `0x10560cbc`), reached from `CBaseEntity` record `0x10552f38` (`m_Collision`,
`FIELD_EMBEDDED` at `+0x270`), so the absolute field is `CBaseEntity+0x2b0`:

```
[0] 10560cbc INTEGER m_Solid        off=0x0040 flags=0x0006 KEY|SAVE  ext=solid
[1] 10560ce8 SHORT   m_usSolidFlags off=0x0044 flags=0x0002
```

It is also `DT_CollisionProperty`'s `solid` SendProp at **3 bits** (`FUN_100dbf50`), so the maximum
value is 7 and the enum is stock `SolidType_t`: `NONE=0, BSP=1, BBOX=2, OBB=3, OBB_YAW=4, CUSTOM=5,
VPHYSICS=6`.

**A `prop_dynamic` builds static collision from the same `.phy` a `prop_physics` simulates against.**
`CDynamicProp::Spawn` ends in a virtual tail call through vtable `+0x37c` (vftable `0x10474234`) to
`CDynamicProp::CreateVPhysics` (`FUN_101907e0`):

```c
if (GetSolid() != 0)                     // vtable +0x170 -> CBaseEntity::GetSolid (0x10027570)
    CBaseEntity::VPhysicsInitStatic(this);
return 1;
```

and `VPhysicsInitStatic` (`FUN_100a5bb0`) chooses by the same value:

```c
if (GetSolid() == 0) return NULL;
if (GetSolid() == 2) return PhysModelCreateBox(mins, maxs, origin, true);   // SOLID_BBOX
else                 return PhysModelParseSolid(this, model);                // the .phy
```

So `solid 6` and `solid 3` produce a **static** VPhysics object parsed from the model's collision
data, `solid 2` produces a box from the model bounds, and only `solid 0` is genuinely non-solid.
Across the exported maps the `prop_dynamic` family authors `6` on 689 placements, `0` on 200, `3` on
8, `2` on 2, and leaves it unset on 4 — so most dynamic props are world-solid in the original game.

`CBaseProp::Spawn` (`FUN_1018df70`) reads the same value a second time for audio:
`if (0 < solid && (solid < 3 || solid == 6)) SetOccludesSound(true)` (`FUN_100a9470`, writing
`m_bOccludesSound` `+0xfe`). `CDynamicProp::Spawn` additionally sets `m_fFlags |= 0x40000` — bit 18,
the `FL_STATICPROP` position in Source's flag list; its consumer in this build has not been
identified.

## V4d measured frame gate (2026-10-05)

Accepted **`solid-local-source-bone-v1`**, builder `ragdoll-v1`. Chaos is the named
visual modernization; game state/event order and authored mass/limits are unchanged.
This supersedes the pending diagnostic status above. No human/editor screenshot was needed.

Recipe: published `g` becomes `q=100*(g.x,g.z,g.y)` once; Source solid origin/angles
become `S_u=(M Rz(yaw) Ry(pitch) Rx(roll) M, 2.54 M origin)`, `M=diag(1,-1,1)`.
Cook `B_u.inverse(S_u(q))`. Joint `J_u=B_child`; parent frame `B_parent.inverse*J_u`,
child frame `Rz(-mid_z) Ry(-mid_y) Rx(-mid_x)` at zero translation. Source x/y/z map
to UE Twist/Swing2/Swing1, angular signs -/+/-; knee half-span/midpoint 49.5/45.5°,
elbow 62/58°. Only origins coincide at bind: the deliberate midpoint orientation offset
is 45.5°/58°, and both hinge axes coincide. Zero-width axes remain locked.

Reproduce: scoped regular_cop import; `InspectRagdollPhysicsAsset(mesh, asset)` reads
actual saved convex points, accumulated reference bones, LOD0 vertices with any nonzero
weight on each named bone, both stored joint frames and actual limit half-spans. Build
the two alternative diagnostic packages, save, then inspect all three in a fresh
`uv run elysium run editor -run=pythonscript` boot. Numerical evidence/scripts are in
`E:/elysium-work/codex/V4d-int/{measure_frames.py,analyze_frames.py,frames.json,frame-analysis.json}`.
The diagnostic import took 87.8s; alternative bake/inspect 8.5s, fresh saved readback 8.5s.
15 bodies, 15 exact convex ledges, 14 joints, total authored override mass 89.999994kg.
Diagnostic recipe fingerprint `c7576dafe0efa6914a0bd25a871e049de0a118cd9dc2b1140a2f5579c10dc8ca`.

Candidate mean AABB-centre distance to the bone-weighted bind mesh: solid-local 4.609cm,
model-local 98.821cm, bone-local 5.753cm. Model-local puts the head at z=0.116cm versus
the anatomical head hull z=172.775cm. Bone-local shifts forearm centres 6–8cm and
hand centres about 11cm from weighted mesh centres, versus 1.5–2.1cm / 5.2–6.1cm
for solid-local. Authored collision hulls need not equal the weighted render surface: the
Spine1 hull covers the torso including child Spine2; hand hulls include finger volume.
The numbers choose authored solid placement over an untransformed bone-local shortcut.

All hulls below, centimetres in component space. C = vertex centroid, E = AABB half extents;
B = reference bone position; mesh min/max are direct nonzero-weight bounds for that bone.

| Bone | C | E | B | Mesh min | Mesh max | Centre error |
|---|---|---|---|---|---|---|
| Bip01 Pelvis | 0.178,-0.803,98.115 | 18.034,13.914,8.689 | 0.144,-0.748,99.012 | -17.945,-13.125,75.108 | 17.945,15.211,110.336 | 5.803 |
| Bip01 R Thigh | -9.485,-3.022,70.684 | 8.491,11.130,15.911 | -8.749,-0.748,99.012 | -18.016,-13.125,49.294 | 2.219,14.444,109.966 | 9.493 |
| Bip01 R Calf | -9.094,-5.732,30.929 | 7.609,9.542,20.774 | -8.749,-2.498,53.758 | -16.503,-14.651,1.940 | -0.729,5.655,56.252 | 2.107 |
| Bip01 L Thigh | 9.845,-3.022,70.684 | 8.491,11.130,15.911 | 9.037,-0.748,99.012 | -2.219,-13.125,49.294 | 18.016,14.444,109.966 | 9.560 |
| Bip01 L Calf | 9.454,-5.732,30.929 | 7.609,9.542,20.774 | 9.037,-2.498,53.758 | 0.008,-14.994,-0.365 | 16.503,5.655,55.953 | 3.574 |
| Bip01 L Foot | 8.936,0.820,3.181 | 6.524,16.658,4.507 | 9.037,-6.957,9.029 | 1.741,-15.251,-1.086 | 17.947,17.233,16.310 | 4.341 |
| Bip01 Spine1 | 0.110,-0.293,132.115 | 17.397,15.459,24.591 | 0.144,2.553,120.436 | -19.536,-15.720,121.052 | 19.536,16.602,139.785 | 2.532 |
| Bip01 R UpperArm | -27.152,-4.247,142.866 | 11.082,7.437,12.087 | -19.164,-3.682,149.997 | -40.281,-17.109,127.546 | -7.193,5.581,158.138 | 4.583 |
| Bip01 R Forearm | -46.289,-3.000,123.584 | 8.328,6.964,8.349 | -37.880,-5.005,131.787 | -55.158,-9.135,113.428 | -35.370,1.149,134.886 | 1.513 |
| Bip01 L UpperArm | 27.502,-3.972,142.828 | 11.074,7.435,12.119 | 19.452,-3.682,149.997 | 7.193,-17.109,127.074 | 40.281,5.581,158.137 | 4.904 |
| Bip01 L Forearm | 46.640,-2.427,123.504 | 8.340,6.940,8.350 | 38.141,-4.324,131.723 | 35.369,-8.914,113.429 | 55.174,1.149,134.886 | 2.056 |
| Bip01 L Hand | 61.843,1.716,107.794 | 9.436,8.519,9.897 | 54.429,-1.069,115.292 | 51.379,-5.594,106.902 | 65.495,4.313,116.505 | 6.094 |
| Bip01 Head | 0.116,0.746,172.775 | 11.477,14.729,14.626 | 0.144,-0.015,163.482 | -9.374,-12.209,156.699 | 9.413,14.572,186.412 | 2.940 |
| Bip01 R Hand | -61.449,1.333,107.888 | 9.446,8.518,9.920 | -54.134,-1.549,115.363 | -65.370,-5.594,105.597 | -51.379,4.312,116.505 | 5.213 |
| Bip01 R Foot | -8.572,0.820,3.181 | 6.524,16.658,4.507 | -8.749,-6.957,9.029 | -17.947,-15.251,-1.086 | -1.741,17.233,16.345 | 4.418 |

Signed sweep uses the **saved** endpoint matrices: for UE Swing1 angle a, component
rotation `D=R_parentFrame * Rz(a) * inverse(R_childFrame_component)` about the common
pivot. Apply D to the foot/hand offset (and to each cooked hull point) so the calf hull
and its foot move together, keeping both pivot positions fixed. Native physical rotation
is `a+mid=-Source_z`. This is an offline pose of the asset's actual limits, not a free-fall
or sleep inference. Knee free axis (-1,0,0) is perpendicular to thigh and calf shafts
(|dot| < 1e-6). Both knee pivot errors < 5.2e-14cm. Bind foot forward is +Y; Source
-95° moves the right foot by (0,-39.711,+53.069)cm, +4° by (0,+3.131,-0.202)cm.
Left knee has the same Y/Z deltas within 1e-5cm. Elbows' Source -120° folds the hands
inward/up/forward; +4° is slight extension. Left/right elbow axes are mirrored axial
vectors: R=(.6977,-.0941,-.7102), L=(.6977,.0941,.7102), orthogonal to each forearm
(|dot| < 1e-6); their pivot errors < 4.6e-14cm.

Accepted column-vector homogeneous matrices (rounded to 6 decimals). Parent/child bind
are actual mesh reference transforms. Joint J is the unoffset child bind. Endpoint
frames are the saved bone-local frames; midpoint offsets above explain basis difference.

### Bip01 R Calf

```text
parentBind
-0.000001 -0.000001 -1.000000 -8.749154
-0.038641 -0.999253  0.000001 -0.747778
-0.999253  0.038641  0.000001  99.012184
 0.000000  0.000000  0.000000  1.000000
childBind
-0.000001 -0.000001 -1.000000 -8.749210
-0.099194 -0.995068  0.000001 -2.497785
-0.995068  0.099194  0.000001  53.757678
 0.000000  0.000000  0.000000  1.000000
joint
-0.000001 -0.000001 -1.000000 -8.749210
-0.099194 -0.995068  0.000001 -2.497785
-0.995068  0.099194  0.000001  53.757678
 0.000000  0.000000  0.000000  1.000000
parentFrame
 0.998158 -0.060669 -0.000000  45.288330
 0.060669  0.998158  0.000000  0.000000
 0.000000 -0.000000  1.000000 -0.000000
 0.000000  0.000000  0.000000  1.000000
childFrame
 0.700909  0.713250  0.000000  0.000000
-0.713250  0.700909 -0.000000  0.000000
-0.000000  0.000000  1.000000  0.000000
 0.000000  0.000000  0.000000  1.000000
```

| Source stop | UE Swing1 | Foot/hand component position |
|---|---|---|
| +4.0 | -49.5 | -8.749,-3.826,8.827 |
| -95.0 | +49.5 | -8.749,-46.667,62.098 |

### Bip01 L Calf

```text
parentBind
-0.000001 -0.000001 -1.000000  9.036856
-0.038641 -0.999253  0.000001 -0.747802
-0.999253  0.038641  0.000001  99.012151
 0.000000  0.000000  0.000000  1.000000
childBind
-0.000001 -0.000001 -1.000000  9.036800
-0.099194 -0.995068  0.000001 -2.497809
-0.995068  0.099194  0.000001  53.757641
 0.000000  0.000000  0.000000  1.000000
joint
-0.000001 -0.000001 -1.000000  9.036800
-0.099194 -0.995068  0.000001 -2.497809
-0.995068  0.099194  0.000001  53.757641
 0.000000  0.000000  0.000000  1.000000
parentFrame
 0.998158 -0.060669 -0.000000  45.288334
 0.060669  0.998158  0.000000  0.000000
 0.000000 -0.000000  1.000000 -0.000000
 0.000000  0.000000  0.000000  1.000000
childFrame
 0.700909  0.713250  0.000000  0.000000
-0.713250  0.700909 -0.000000  0.000000
-0.000000  0.000000  1.000000  0.000000
 0.000000  0.000000  0.000000  1.000000
```

| Source stop | UE Swing1 | Foot/hand component position |
|---|---|---|
| +4.0 | -49.5 | 9.037,-3.826,8.827 |
| -95.0 | +49.5 | 9.037,-46.668,62.098 |

### Bip01 R Forearm

```text
parentBind
-0.715809  0.029579  0.697669 -19.163976
-0.050608  0.994278 -0.094078 -3.681954
-0.696459 -0.102649 -0.710216  149.997414
 0.000000  0.000000  0.000000  1.000000
childBind
-0.695665  0.171198  0.697669 -37.880053
 0.147933  0.984513 -0.094077 -5.005181
-0.702970  0.037762 -0.710217  131.787289
 0.000000  0.000000  0.000000  1.000000
joint
-0.695665  0.171198  0.697669 -37.880053
 0.147933  0.984513 -0.094077 -5.005181
-0.702970  0.037762 -0.710217  131.787289
 0.000000  0.000000  0.000000  1.000000
parentFrame
 0.980067 -0.198668  0.000000  26.146723
 0.198668  0.980067  0.000000 -0.000001
-0.000000 -0.000000  1.000000 -0.000010
 0.000000  0.000000  0.000000  1.000000
childFrame
 0.529919  0.848048  0.000000  0.000000
-0.848048  0.529919 -0.000000  0.000000
-0.000000  0.000000  1.000000  0.000000
 0.000000  0.000000  0.000000  1.000000
```

| Source stop | UE Swing1 | Foot/hand component position |
|---|---|---|
| +4.0 | -62.0 | -54.373,-3.162,115.342 |
| -120.0 | +62.0 | -26.289,13.187,140.763 |

### Bip01 L Forearm

```text
parentBind
 0.714785 -0.048305  0.697674  19.451855
-0.024565  0.995262  0.094076 -3.681989
-0.698913 -0.084383  0.710211  149.997302
 0.000000  0.000000  0.000000  1.000000
childBind
 0.697127 -0.165118  0.697674  38.141132
 0.139334  0.985767  0.094076 -4.324293
-0.703278  0.031626  0.710211  131.723031
 0.000000  0.000000  0.000000  1.000000
joint
 0.697127 -0.165118  0.697674  38.141132
 0.139334  0.985767  0.094076 -4.324293
-0.703278  0.031626  0.710211  131.723031
 0.000000  0.000000  0.000000  1.000000
parentFrame
 0.986403 -0.164344 -0.000000  26.146713
 0.164344  0.986403  0.000000 -0.000000
 0.000000 -0.000000  1.000000  0.000003
 0.000000  0.000000  0.000000  1.000000
childFrame
 0.529919  0.848048  0.000000  0.000000
-0.848048  0.529919 -0.000000  0.000000
-0.000000  0.000000  1.000000  0.000000
 0.000000  0.000000  0.000000  1.000000
```

| Source stop | UE Swing1 | Foot/hand component position |
|---|---|---|
| +4.0 | -62.0 | 54.658,-2.683,115.280 |
| -120.0 | +62.0 | 26.656,13.994,140.579 |

### Accepted solid transforms

Column-vector S_u matrices for the diagnostic right leg (the source origin/QAngles,
converted once, before inverse actual bone bind).

```text
Bip01 R Thigh
-0.000002 -0.000001 -1.000000 -8.893007
-0.038642 -0.999253  0.000001  0.144948
-0.999253  0.038642  0.000002  99.012182
 0.000000  0.000000  0.000000  1.000000
Bip01 R Calf
-0.000002 -0.000002 -1.000000 -8.893078
-0.099194 -0.995068  0.000002 -1.605069
-0.995068  0.099194  0.000002  53.757678
 0.000000  0.000000  0.000000  1.000000
```

### V4d floor measurement and scope

The 34-key S14 import took 145.2s: 30 real rigs built, zero failed; rat, rat_swimming,
wolf_form and mercuriodamagedstreet have no .phy and were skipped without fitted substitutes.
70 included animation banks have no drawn mesh. Mesh/animation recipes remain characters-v2.
Every rig is 15 bodies / 15 ledges / 14 joints except security_guard: 18/18/17.
Production receipts live in `E:/elysium-work/codex/V4d-int/wide-receipts.json`.
Generated packages are ignored by repository policy and remain on disk.

With actual production _RAGDOLL attachment and sim=1, fixed-60-Hz scalar regular_cop
(death t=3.017) measured height/speed at t=3.2: 89.930/109.821; 4.2: 26.134/151.921;
4.4: 17.855/13.061; 5.0: 18.113/6.601; 5.6: 18.128/0.467; final rest 18.138/0
(cm, cm/s). Paired stealth measured 5.4: 15.124/18.904; 6.4: 17.342/3.779;
7.2 onward: 17.016/0. Both use max_height20, a 1.862cm margin over the higher rest,
unchanged 5cm/s rest threshold. No mass/hull/limit was tuned to fit acceptance.

The scalar record's actual death/OnDeath/corpse all occur at 3.017; its origin remains
at the kill spot while the pelvis falls to (-45.423,1062.906,18.138). Retail unseen
SUB_PVSRemove 0x102696f0 correctly removes at13.017. Terminal release logged the same
15-body component sim=1/awake=0. Its old final15s floor query used a stale visual
retained past removal; with real cleanup it fails for no drawn mesh. Floor acceptance
now reads at8s, before removal, with all death staging and final Dead/alive=false
probes retained. This is a record repair, not a removal-clock modernization.
All four removal/retention controls kept their V4c verdicts on the first named run.

### Rendered and witness-map presentation smoke

`uv run elysium gr --arena`, `elysium.gr_scenario damage_lethal_death`: pass,
15.00s game / 15.01s wall, rendered pelvis rest17.485cm and speed0, sim=1.
Normal `uv run elysium run play sm_hub_1` (the lab deliberately rebuilds its arena
on travel) killed the two visible bum_male pedestrians #1925/#1927 using a numeric
TakeDamage10000. Both admitted actual SK_bum_male_RAGDOLL, 15 bodies, sim=1.
#1925's origin stayed (1857.73822,5803.0364,-302.649991); screenshot
`E:/elysium-work/codex/V4d-int/bum-map-dead.png` shows its collapsed jacket/hat
following the body on the parking-space floor, without a standing garment or splayed
cloth. Hidden haven_bum_male was not a valid witness; no hidden-input change was made.

Save slot v4d-smoke committed at clock118.439, loaded that clock and the same entity
anchors. Each rebuilt visual admitted once; both NPC minds retained Dead, and the
restored body fell/rested with coherent jacket/hat. Screenshot
`E:/elysium-work/codex/V4d-int/bum-map-restored-close.png` confirms presentation,
with no crash or standing body. The solve pose differs: no exact pose persistence
claim. No new damage/OnDeath was emitted on load; restoration calls the guarded
presentation handoff rather than BecomeClientRagdoll, so makes no weighted pick.

V6 remains required: this smoke also observed m_lifeState(+0x200) return from1 to0
and Health return to10 after load despite the retained Dead mind. The corpse's
think/render/life/health save words and precise pose persistence are not proved here.
The physics admission/garment smoke passes; this is explicitly not corpse save parity.
