# `.phy` — VtMB's VPhysics collision models

A VtMB `.mdl` ships with a sibling `.phy` holding the collision model the original game
simulates against: a set of **convex hulls** authored alongside the render mesh, not derived
from it. The retail VPKs carry **2,854** of them; the Unofficial Patch adds and replaces more
as loose files, so a `.phy` must be resolved through the install index (patch shadows VPK),
never straight out of a VPK.

`prop_physics` is the consumer: roadmap 8.4 gives a physics prop these hulls rather than an
approximation of its render mesh. The decoder is `tools/phy.py`; it emits `props/<stem>.phys`,
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

`byte_size` re-encoding the solid size is the integrity check worth asserting — it caught every
mis-split during development, and it holds on all 2,854 files. The `magic` is decorative: it
reads `IVPS` on scenery and is **zeroed on the 42 ragdoll files**, which are otherwise the same
layout (all 42 decode, 100 hulls, every one convex).

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
2,854 files, **7,889 hulls** — it holds with **zero** exceptions. `tools/phy.py` asserts it at
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

After the solids comes plain text: one `solid { }` block per solid plus one `editparams { }`.

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
