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

Negating two axes is a rotation (determinant +1), not a reflection, so **triangle winding carries
through unchanged** — unlike `bsp.source_to_unreal`, which negates one axis and forces the
exporter to reverse winding at OBJ-write time.

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
  mapping for the solid transforms is not yet settled.
- `massbias` is sparse; `surfaceprop` is `flesh` on the humanoid rigs.

Which models carry a rig, the shape distribution, and every behaviour that consumes it are
`docs/vtmb/physics-interaction.md`.

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
