# Map GLB seam

This document defines the root unit of one VtMB BSP map. A BSP is one member cut into four
units — the map root and three lump-family sub-units — so that an entity query does not load
world geometry and a lighting inspection does not load the entity lump:

| Unit | Identity | Specification |
|---|---|---|
| Map root | `vtmb:map:<map>` | this document |
| Entities | `vtmb:map-entities:<map>` | `seam_map_map_entities.md` |
| Lighting | `vtmb:map-lighting:<map>` | `seam_map_map_lighting.md` |
| Visibility | `vtmb:map-visibility:<map>` | `seam_map_map_visibility.md` |

The four units together account for every byte of the BSP. The root owns the header, the lump
directory and every lump the sub-units do not claim; it proves the partition before any of the
four is published. Shared rules are owned by `seam_map_unit_contract.md`; format facts by
`docs/vtmb/bsp_format.md`, `docs/vtmb/lighting.md`, `docs/vtmb/reflections.md`,
`docs/vtmb/water.md` and `docs/vtmb/phy_vphysics.md`.

## Unit identity

```text
<VTMB>/Unofficial_Patch -> maps/<map>.bsp
  -> vtmb:map:<map>
  -> $ELYSIUM_EXPORT_V2_ROOT/maps/<map>.glb
```

The key is the file stem below `maps/`. The member resolves UP-first; 108 maps resolve in the
merged install, all `VBSP` version 17, and the patch's geometry differs from retail's on many of
them, so a retail-only decode is an explicit research comparison.

```text
uv run elysium export_v2 map-glb <map>
uv run elysium export_v2 maps-glb
```

The `map-glb` command publishes all four units of the map, because the partition proof is one
computation; each sub-unit is also its own command.

## Lump partition

The lump directory has 64 rows. The census over all 108 maps populates these; a lump no map
populates is a `reserved-zero` directory row and nothing else.

| Lump | Name | Maps | Owner unit | Destination |
|---:|---|---:|---|---|
| 0 | ENTITIES | 108 | entities | quoted-text entity table |
| 1 | PLANES | 108 | root | `planes[]` |
| 2 | TEXDATA | 108 | root | `textures[]` with material references |
| 3 | VERTEXES | 108 | root | `POSITION` accessors |
| 4 | VISIBILITY | 108 | visibility | PVS/PAS rows |
| 5 | NODES | 108 | root | `bsp.nodes[]` |
| 6 | TEXINFO | 108 | root | `texinfos[]` |
| 7 | FACES | 108 | root | primitives; `faces[]` (104-byte `dface_t`) |
| 8 | LIGHTING | 108 | lighting | luxel samples |
| 9 | OCCLUSION | 1 | root | `occluders[]` |
| 10 | LEAFS | 108 | root | `bsp.leafs[]` (32-byte `dleaf_t`) |
| 12 | EDGES | 108 | root | `edges[]` |
| 13 | SURFEDGES | 108 | root | `surfEdges[]` |
| 14 | MODELS | 108 | root | one scene node per brush model |
| 15 | WORLDLIGHTS | 108 | lighting | light records |
| 16 | LEAFFACES | 108 | root | `bsp.leafFaces[]` |
| 17 | LEAFBRUSHES | 108 | root | `bsp.leafBrushes[]` |
| 18 | BRUSHES | 108 | root | `collision.brushes[]` |
| 19 | BRUSHSIDES | 108 | root | `collision.brushSides[]` |
| 20 | AREAS | 108 | root | `bsp.areas[]` |
| 21 | AREAPORTALS | 108 | root | `bsp.areaPortals[]` |
| 22–25 | PORTALS, CLUSTERS, PORTALVERTS, CLUSTERPORTALS | 5 | visibility | portal graph |
| 26 | DISPINFO | 48 | root | `displacements[]` |
| 27 | ORIGINALFACES | 108 | root | `originalFaces[]` |
| 29 | PHYSCOLLIDE | 108 | root | `physics.models[]` hulls in BIN |
| 30 | VERTNORMALS | 108 | root | `NORMAL` accessors |
| 31 | VERTNORMALINDICES | 108 | root | the face-to-normal map |
| 32 | DISP_LIGHTMAP_ALPHAS | 48 | lighting | displacement alpha |
| 33 | DISP_VERTS | 48 | root | displacement `POSITION` |
| 34 | DISP_LIGHTMAP_SAMPLE_POSITIONS | 48 | lighting | displacement sample positions |
| 35 | GAME_LUMP | 108 | root (`sprp`, `dprp`), lighting (`dplt`) | placements; detail lighting |
| 36 | LEAFWATERDATA | 25 | root | `water.leafData[]` |
| 37–39 | PRIMITIVES, PRIMVERTS, PRIMINDICES | 11 | root | t-junction primitives |
| 40 | PAKFILE | 108 | root (directory only) | members routed to material and texture units |
| 41 | CLIPPORTALVERTS | 53 | root | area-portal clip windings |
| 42 | CUBEMAPS | 105 | root | cubemap sample nodes |
| 43, 44 | TEXDATA_STRING_DATA/TABLE | 108 | root | material names |
| 46 | LEAFMINDISTTOWATER | 103 | root | `water.leafMinDist[]` |
| 47 | FACE_MACRO_TEXTURE_INFO | 27 | root | `faces[].macroTexture` |
| 48 | DISP_TRIS | 48 | root | displacement triangle tags |

Every populated lump has directory `version` 0 and a zero `fourCC` on every map, which the root
records once as a census fact and verifies per unit. Bytes between lumps that no directory row
claims are `padding-zero` when zero and `omitted-proven inter-lump-fill` otherwise; an overlap
between two lump ranges fails the map, because a byte with two owners has no ledger.

A sub-unit's `sourceResolution` member carries a `span` per lump it owns, and its ledger is gapless
over those spans. The root's ledger claims the header, the directory and every root lump. The
union of the four ledgers is the whole file; the root publishes `partition` — every lump's owner
unit, offset and length — so a reader can check that claim without the other three units.

## PAKFILE routing

Lump 40 is a ZIP whose members are, on every map, only `materials/maps/<map>/**.{vmt,tth,ttz}`:
cubemap-patched material copies and the baked reflection probes. Each member is exported as an
ordinary material or texture unit with a `bsp-pakfile` origin:

```json
{"kind": "bsp-pakfile", "map": "sp_tutorial_1",
 "member": "materials/maps/sp_tutorial_1/c-1024_512_64.tth",
 "origin": {"kind": "loose", "root": "Unofficial_Patch"}}
```

and its ordinary identity — `vtmb:material:maps/sp_tutorial_1/<mat>_<x>_<y>_<z>`,
`vtmb:texture:maps/sp_tutorial_1/c<x>_<y>_<z>`. The root owns the ZIP container: its local and
central directory records are `mapped` under `pakfile.entries[i].localHeader` and
`.centralHeader`, each member's compressed span is `derived` under `pakfile.entries[i].data`
(its decoded bytes are the referenced unit's source), and `pakfile.entries[]` lists name, method,
CRC-32, sizes and the unit ID the member became. A member that is neither VMT nor TTH/TTZ is
`unsupported` and fails the map, because no seam claims it.

A patched material `maps/<map>/<mat>_<x>_<y>_<z>` and its base `<mat>` are distinct units; the
root's `textures[]` row names both, so the substitution the engine performs is a stated join.

## GLB structure

```text
map.glb
|- JSON chunk
|  |- scenes: world, brushModels, displacements, placements
|  |- nodes, meshes, materials
|  `- extensions.ELYSIUM_vtmb_map
`- BIN chunk
   |- world and brush-model POSITION / NORMAL / TEXCOORD_0 / TEXCOORD_1 / indices
   |- displacement POSITION / NORMAL / alpha / indices
   `- PHYSCOLLIDE hull position / index accessors
```

```json
{
  "extensionsUsed": ["ELYSIUM_material_reference", "ELYSIUM_model_reference", "ELYSIUM_texture_reference", "ELYSIUM_vtmb_map"],
  "extensionsRequired": ["ELYSIUM_vtmb_map"],
  "extensions": {
    "ELYSIUM_vtmb_map": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "coordinateTransform": {},
      "header": {},
      "partition": [],
      "subUnits": [],
      "planes": [],
      "textures": [],
      "texinfos": [],
      "faces": [],
      "originalFaces": [],
      "edges": [],
      "surfEdges": [],
      "models": [],
      "bsp": {},
      "collision": {},
      "displacements": [],
      "primitives": {},
      "physics": {},
      "water": {},
      "cubemaps": [],
      "occluders": [],
      "staticProps": {},
      "detailProps": {},
      "pakfile": {},
      "dependencies": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

`subUnits` lists the three sibling identities with their file paths and SHA-256, so the root is
the entry point to the map.

## Core content

| Map datum | glTF core | Extension |
|---|---|---|
| World faces (model 0) | scene `world`; one mesh, one primitive per material | `faces[]` with `dface_t` fields, `primitive`/`firstIndex` back-links |
| Brush models (models 1..n) | scene `brushModels`; one node per model with its own mesh | `models[]` with origin, bounds, head node, first/num faces |
| Displacements | scene `displacements`; one mesh per `dispinfo` | `displacements[]` with power, start position, neighbours, vertex `dist`/`vector`/`alpha`, triangle tags |
| Vertex normals | `NORMAL` from lumps 30/31 | `faces[].normalIndices` |
| Texture coordinates | `TEXCOORD_0` from `texinfo` texture vectors, `TEXCOORD_1` lightmap luxel coordinates | `texinfos[]` raw vectors and flags |
| Materials | one core material per distinct material name, `ELYSIUM_material_reference` per primitive | `textures[]` with the TEXDATA reflectivity, size, and the base/patched pair |
| Static props | scene `placements`; one node per `sprp` record with `ELYSIUM_model_reference` | `staticProps.dictionary[]`, `.leaves[]`, `.props[]` with solid, flags, skin, fade, lighting origin |
| Detail props | one node per `dprp` record | `detailProps` dictionary, sprites and records |
| Cubemap samples | one node per lump 42 record with `ELYSIUM_texture_reference` | `cubemaps[]` origin, size, resolved texture unit |
| World and model collision | — | `collision` brushes, sides, contents, per-side texinfo and bevel flag |
| VPhysics collision | hull accessors | `physics.models[]` per `dphysmodel_t`: solid count, key-value text, per-solid ledges |
| BSP tree | — | `bsp` nodes, leafs, leaf faces, leaf brushes, areas, area portals, clip portal verts |
| T-junction primitives | mesh primitives when present | `primitives` |
| Water | — | `water.leafData[]`, `water.leafMinDist[]` |
| Occluders | — | `occluders[]` |

A `TOOLS/*` face is exported like any other and marked `tool: true`; skipping it is a consumer's
choice, not the unit's. A face flagged sky is likewise marked; the 3D-sky area split is the
visibility unit's join with the `sky_camera` entity. Each face carries its `area` from the leaf
it belongs to so a consumer can make that split from the root and the entities unit alone.

Winding follows the source: faces are polygons over `surfEdges`, triangulated as fans in the
authored order, and `faces[].firstIndex`/`indexCount` locate each face's triangles in its
primitive, so the primitive's index buffer and the face table describe one geometry from two
sides.

### Static props

The `sprp` payload is version 4 on every map (`DStaticPropV4`, 56 bytes). A node's transform is
the record's origin and angles in glTF space; `ELYSIUM_model_reference.asset` is
`vtmb:model:<dictionary entry>` and the `dependencies` row names it under role `model`. A
dictionary entry the install lacks is a `missing-model` sentinel with `resolved: false`. The
`dprp` game lump is version 2 and `dplt` version 0; `dplt` is the lighting unit's.

### Coordinate transform

The unit contract's transform applies to every positional lump. Plane normals and distances are
transformed as directions plus a scaled distance. `PHYSCOLLIDE` is stated in
`IVP metres, axis-only` like a model's PHY. Lightmap luxel coordinates stay in luxels.

## Extension reference

| Key | Contents |
|---|---|
| `header` | ident, version, `mapRevision`, the 64-row directory (`offset`, `length`, `version`, `fourCC`) |
| `partition` | every lump's owner unit, offset and length; inter-lump ranges and their state |
| `subUnits` | the entities, lighting and visibility identities, paths and digests |
| `planes`, `texinfos`, `faces`, `originalFaces`, `edges`, `surfEdges`, `models` | the lump records, one row each, with `sourceOffset` |
| `textures` | TEXDATA rows joined to their string-table name and material unit ID |
| `bsp` | `nodes[]`, `leafs[]`, `leafFaces[]`, `leafBrushes[]`, `areas[]`, `areaPortals[]`, `clipPortalVerts[]` |
| `collision` | `brushes[]`, `brushSides[]` |
| `displacements` | `dispinfo` rows with their vertex and triangle spans and mesh index |
| `primitives` | `primitives[]`, `verts[]`, `indices[]` |
| `physics` | `models[]` with the VPhysics decode |
| `water` | leaf water data and min-distance rows |
| `cubemaps` | sample rows with the resolved texture unit ID |
| `occluders` | lump 9 rows |
| `staticProps`, `detailProps` | the game-lump decodes |
| `pakfile` | the ZIP directory and per-member unit IDs |

## Dependencies

| Role | Produced by |
|---|---|
| `material` | every TEXDATA name; patched names resolve to their pakfile unit |
| `model` | `sprp` dictionary and `dprp` model entries |
| `texture` | cubemap samples and `cubemapdefault` |
| `map-entities`, `map-lighting`, `map-visibility` | the sibling units |
| `surface-property` | PHYSCOLLIDE key-value `surfaceprop` |

## Anomalies and omissions

| Row | Meaning |
|---|---|
| `anomalies[] directory-version-nonzero` | a lump row whose version or fourCC is not zero |
| `anomalies[] face-outside-model` | a face index no model range covers |
| `anomalies[] degenerate-face` | fewer than three edges or a zero-area winding |
| `anomalies[] stale-texdata-size` | TEXDATA size disagreeing with the resolved texture unit |
| `omissions[] inter-lump-fill` | non-zero bytes between lumps, with digest |
| `omissions[] unused-lump-bytes` | a lump longer than its record count explains |

## Coverage and validation

A complete map root has zero `unresolved` and zero `unsupported` rows, and the four units'
ledgers together claim every byte of the BSP exactly once. Validation re-reads every root lump
independently of the writer and compares record counts and values, checks every face's triangles
against its primitive span, every displacement's vertex grid against its accessor, every
PHYSCOLLIDE hull against its accessors, every `sprp` node against its record, the PAKFILE
directory against the ZIP, and every material name against a `dependencies` row.

## Producer join: the entities+root join behind `.ents`

The legacy `.ents` sidecar is a **join, not a projection**. The entities unit owns lump 0 and
nothing else, so `hulls`, `contents`, `blocks_player`, `brush_mesh` and the 3D-skybox `sky` flag —
five of the fields the running game reads — are root-lump facts that no entities row carries. This
section states that join, the hull solver it runs and the field list it emits, so the sidecar
producer of `docs/project/seam_migration.md` → "Roadmap — one pipeline" (R3.2) is written from a
specification rather than from a reading of `exporters/UE_bsp_to_scene.py::write_entities`.

Every number below is measured over the three-map working corpus — `sp_tutorial_1`,
`sm_pawnshop_1`, `sm_hub_1`; 4,933 entities, 377 of them brush entities — against the published
V2 units and the legacy sidecars on disk, 2026-08-31.

### Which table owns which fact

The producer walks the entities unit in `entities[].index` order and, for every row whose
`model.kind` is `brush`, resolves the brush index `N = model.index` against the root unit:

| `.ents` field | Root unit path | Entities unit part |
|---|---|---|
| `hulls` | `models[N].headNode` → `bsp.nodes[].children` → `bsp.leafs[].firstLeafBrush`/`numLeafBrushes` → `bsp.leafBrushes.values[]` → `collision.brushes[]` → `collision.brushSides[].plane`/`.bevel` → `planes[]` | `model.index` |
| `contents` | `collision.brushes[].contents`, OR-ed over the model's **hull-producing** brushes only | — |
| `blocks_player` | `contents & 0x1400B` — `SOLID\|WINDOW\|GRATE\|MOVEABLE\|PLAYERCLIP`; water and pure `MONSTERCLIP` stay passable | — |
| `brush_mesh` | `models[N].firstFace`/`numFaces` → `faces[].numEdges`/`.texInfo` → `texinfos[].texData` → `textures[].asset`; the model is meshed when **any** face survives | the `func_areaportalwindow` → `target` → `targetname` → `model` join that suppresses render-only visibility backings |
| `sky` | `bsp.nodes[]` + `planes[]` point-leaf walk of model 0, `bsp.leafs[].area`, `models[N].mins`/`maxs` for the brush-entity classification point | the first `sky_camera` block's `origin` and `scale` |

`models[N].origin` is **not** part of the join: it is `(0, 0, 0)` on all 368 models of the three
maps and the exporter never reads it. The entity's `origin` keyvalue is the only translation.

Root positional tables are stated in glTF metres, Y-up (`seam_map_unit_contract.md` →
"Coordinate transform"), so the producer inverts the transform before solving: source normal
`(nx, ny, nz) = (x, -z, y)`, source distance `d_src = d_gltf / 0.0254`, source position
`(sx, sy, sz) = (x, -z, y) / 0.0254`. **The recovered plane rows are held as binary32**, the width
the BSP stores and the width the legacy solver runs at. That is load-bearing, not a detail: over
the three maps' 35,594 planes, 4,772 distances do not return bit-exactly in binary64 (max
|Δ| 9.09e-13 Source inches, an artefact of the `× 0.0254` / `÷ 0.0254` pair), while **zero** fail
to return bit-exactly in binary32. Solving in binary64 changes hull vertex sets; solving in
binary32 does not.

### The hull solver

Ported verbatim from `_model_brushes` and `_brush_hull`. Model brushes first:

1. Depth-first from `models[N].headNode` over `bsp.nodes[].children`. A child `c < 0` is leaf
   `-c - 1`; take its `firstLeafBrush`/`numLeafBrushes` window of `bsp.leafBrushes.values[]`
   (stride 2, `uint16`) into a **set**. `headNode` 0 is the world model — world plus `func_detail`,
   and never a separate brush entity's brushes.
2. Emit hulls in **ascending brush index** over that set.

Then, per brush, the hull is the convex intersection of its sides' halfspaces `n·x ≤ d`:

1. Collect the brush's `numSides` sides from `firstSide`; **skip every side with `bevel` set**
   (bevels are redundant AABB planes). Fewer than four survivors ⇒ no hull for this brush.
2. For every ordered triple `a < b < c` of surviving planes, build `A = [n_a; n_b; n_c]`. Skip the
   triple when `|det A| < 1e-6`.
3. Solve `A x = (d_a, d_b, d_c)`; keep `x` when `n_i · x - d_i ≤ 0.05` for **every** surviving
   plane `i` of the brush, including the three that generated it.
4. Fewer than four accepted points ⇒ no hull for this brush, and the brush's `contents` is **not**
   OR-ed into the entity's `contents`.
5. Dedupe the accepted points in Source units on the key `(round(x, 1), round(y, 1), round(z, 1))`.
   The map keeps first-appearance **order** and last-seen **value**, which is what a Python dict
   comprehension over the point list does, and what a port must reproduce to stay byte-comparable.
6. Transform each surviving point with `(x, y, z)_unreal = (sx, -sy, sz) × 2.54`, round to 4
   decimals, and flatten: one hull is one flat array of `3 × vertexCount` Unreal centimetres.

The three tolerances — `1e-6` determinant, `0.05` halfspace, `0.1` dedupe (Source units) — are the
contract. They are not derived from anything; they are the numbers the shipped collision was built
with, and changing one changes hull vertex counts on real maps.

**One pre-declared divergence, from a latent defect in the legacy reader.** `_brush_hull` unpacks
`dbrushside_t` as `"<hhhh"`, so `planenum` is read **signed**. Exactly one of the 108 maps overflows
it: `la_hub_1` carries 33,294 planes, and 216 of its 63,096 brushsides name a plane index ≥ 32,768,
which the legacy reader turns into a negative index that wraps to the tail of the plane array and
silently produces a wrong hull. The root unit stores `brushSides[].plane` unsigned, so a faithful
port **diverges from the legacy exporter on `la_hub_1` and only there**. That divergence is a fix;
the differ must expect it by name rather than report it as a regression.

### The hull frame: world-space hulls, origin-relative attachment

The solver reads world planes and applies no per-entity transform, so a hull comes out in whatever
frame vbsp compiled the brush model into, and the sidecar's rule is uniform and unconditional:
**world = entity `origin` + hull vertex.** `source_to_unreal` is linear, so converting the origin
and the vertices separately and adding is the same as converting the sum, which is why the runtime
may seat an entity-local body at `Ent.Origin` and read the hull verbatim.

Verified on a real rotating door, 2026-08-31, from `sm_pawnshop_1`'s legacy `.ents` and its V2
root unit, no game run: `havenrm`, `func_door_rotating`, `model "*18"`, `origin` key
`-2008.5 -2559 199` → Unreal `[-5101.59, 6499.86, 505.46]`. Its single hull is 8 vertices spanning
Unreal `[-3.81, -2.54, -139.7] … [3.81, 134.62, 139.7]` — a 3 × 54 × 110 Source-unit slab that is
exactly `models[18]`'s own `mins`/`maxs` (`(-1.5, -53, -55) … (1.5, 1, 55)` in Source units), and
`models[18].origin` is zero. The hull is therefore **not** in world space for this entity: vbsp
re-centred the model on the origin brush, `origin` is simultaneously the door's hinge (the 3-unit-
thick panel's near edge sits one Source unit past it and the leaf runs 53 units the other way), and
only `origin + hull` places the door inside the map. The decisive evidence is instancing, not
magnitude: 8 of `sm_pawnshop_1`'s 10
`func_door_rotating` entities share just three brush models (`*18` twice, `*31` three times, `*33`
three times) at eight distinct origins, which is impossible if a hull carried a world position.
The converse case is authored in the same corpus — `sp_tutorial_1`'s `trigger_changelevel` family
mixes both, `trig_leave_tutorial`'s `*74` centred on its origin and `trig_theater_to_tutorial`'s
`*142` left thousands of units away from it — so the producer must **never** classify: it applies
the one rule and lets vbsp's choice of frame ride through untouched.

### The field list `.ents` must reproduce

The document is `{"map": "<stem>", "entities": [...]}` written with `json.dump(...,
separators=(",", ":"))` and default `ensure_ascii`. `entities[]` is one row **per lump block, in
lump order, with no drops and no reorders** — `ElysiumEntityWorldPersistence.cpp` applies saved
entity state by index, so the ordinal is a save key.

Blocks come from `re.findall(r"\{([^{}]*)\}", …, re.S)` over the lump decoded `ascii`/`replace`;
pairs from `re.findall(r'"([^"]*)"\s+"([^"]*)"', block)`. Keys are **not** folded, and a plain
keyvalue is **last-wins**: a repeated scalar key keeps its final occurrence and its authored
spelling.

| Field | Present | Value |
|---|---|---|
| `classname` | always | the last `classname`, removed from `keys` |
| `targetname` | always | the last `targetname`, removed from `keys`, `""` when absent |
| `origin` | always | `[3]`, Unreal cm, 5 decimals; C `atof` of each token of the last `origin` value when it splits into exactly three whitespace tokens, else `[0,0,0]`. The keyvalue **stays** in `keys` |
| `sky` | when true | the BSP-area test above |
| `hinge_axis` | when `hingeaxis` and `origin` both split into three tokens | `[3]`, 6 decimals; `source_dir_to_unreal(hingeaxis − origin)` normalized; a norm ≤ 1e-6 gives `[0,0,1]` |
| `model` | brush entities | `N` from `*N`, only when `48·(N+1) ≤ len(lump 14)`; an out-of-range index emits **no** `model`/`hulls`/`contents`/`blocks_player` at all |
| `hulls` | with `model` | the solver's output, ascending brush index |
| `contents` | with `model` | OR over hull-producing brushes |
| `blocks_player` | with `model` | `contents & 0x1400B ≠ 0` |
| `brush_mesh` | with `model`, when meshed | `"brush_<N>"` |
| `cull_max_cm` | with `brush_mesh`, `classname.lower() == "func_lod"`, `DisappearDist > 0` | `atof(DisappearDist) × 2.54`, 4 decimals (R6.4, below) |
| `elevator_floors` | `classname.lower() == "func_elevator"` | `[8]`, Unreal cm Z, 5 decimals; `floor1`…`floor8` (exact-case keys), `atof`, default `"0"` |
| `start_hidden` | always | `keys.get("StartHidden", "0") == "1"`, exact-case key |
| `outputs` | when non-empty | below |
| `keys` | always | every remaining keyvalue, authored spelling, last-wins |
| `model_mesh` | studio-model entities | the shared-corpus stem; skipped when `classname` starts with `npc_` (the skeletal lane owns those) or the `model` value (backslashes folded to slashes, lowercased) does not end `.mdl` |
| `model_quat` | with `model_mesh` | `[4]`, 6 decimals, `source_angles_to_unreal_quat` of the `angles` triple, identity when absent or short |

Key order in the emitted object is the order of that table; a byte-comparing differ depends on it.

An output row is emitted for a keyvalue whose key matches `^(On|Out)` case-insensitively **and**
whose value holds at least four commas, and carries `target`, `input`, `param`, `delay`, `times`,
`python`, `name` in that order: `target`/`input`/`python` stripped, `param` **not** stripped,
`delay` a plain `float()` with `0.0` on failure, `times` `int(float())` with `-1` on failure, field
6 (`extra`) dropped. Every such keyvalue is removed from `keys`; an `On*`/`Out*` key with fewer
than four commas stays a plain keyvalue.

Six of those rules disagree with the entities unit's own reading — datamap output typing versus the
`^(On|Out)` prefix test (the unit's `outputLike` demotions), key folding, `param` stripping, `delay`
read with `atof` rather than `float()`, the dropped `extra` field, and `times` normalization of an
authored `0` to unlimited. Each is a **named divergence with its own commit**, owned by
`seam_migration.md` → R3.4, not something the producer decides while porting.

### R3.4 — the six divergences

Each landed as an opt-in flag on `UE_map_sidecars.EntityDivergences`, defaulting to the legacy
behaviour above so `write_sidecars` stays byte-comparable unless a caller asks for the corrected
reading; the R3.3 differ was re-run with each flag on (`producer_root` pointed at a scratch
`_sidecars/` tree) to measure the delta it actually produces on the three-map corpus.

- **Datamap output typing** (`datamap_output_typing`). `True` swaps the `^(On|Out)` shape test for
  the class's datamap (`entity_model.OUTPUT_KEY`/`NOT_OUTPUT_KEYS`/`OUTPUT_KEYS_BY_CLASS`/
  `DISABLED_KEY_SUFFIX`, the same tables `map_entities_glb.decode._is_output` reads), matching the
  entities unit's own `outputLike` demotions and promotions (`seam_map_map_entities.md` →
  "Outputs"). **Measured delta: zero** on `sp_tutorial_1`/`sm_pawnshop_1`/`sm_hub_1` —
  `entityDiffCount` stayed 0 on all three with the flag on, because both shipped examples
  (`game_ui`'s promoted button events, `trigger_player_activity_level`'s demoted `OnTrigger`) are
  authored on `la_hub_1` and `sm_diner_1`, outside the working corpus. The runtime side of this
  divergence — `ElysiumEntityWorldPersistence.cpp`'s `OutputTimesRemaining` restore, which silently
  dropped a cardinality mismatch — now logs a warning instead
  (`Elysium.Substrate.SaveOutputCardinality`); no `FElysiumSaveVersion` bump, per the roadmap's
  2026-09-01 "no save-file compatibility at build time" ruling, which supersedes the bump this
  section's history once called for.
- **Key folding** (`fold_keys`). `True` treats two spellings of one key (`"Origin"`/`"origin"`) as
  the same `keys` slot instead of two independent ones, matching the entities unit's own identity
  rule (`decode.py`'s `occurrences` map, keyed by the already-folded key). The slot's *value* and
  its *printed spelling* both become the last occurrence's, in that occurrence's own casing — the
  flag changes which occurrences collide, never the "authored spelling" rule this same section's
  field-list table states for `keys`. **Measured delta: zero** — none of the 4,933 entities across
  the three maps repeats a key under two spellings.
- **`param` stripping** (`strip_param`). `True` strips the output row's `param` field (index 2)
  the way `target`/`input`/`python` are already stripped, instead of carrying it verbatim
  (`seam_map_map.md`'s field list above: "`param` **not** stripped"). **Measured delta: zero** —
  no output's `parameter` on the three-map corpus carries leading or trailing whitespace.
- **`delay` via `atof`** (`delay_atof`). `True` reads the output row's `delay` field (index 3)
  with this module's own `atof()` — the longest numeric prefix, `0.0` when there is none — instead
  of a plain `float()`, matching every other number `.ents` carries (`origin`, `hingeaxis`,
  `floor1..8`). **Measured delta: zero** — every authored `delay` on the three-map corpus is
  already a plain-`float()`-parseable token; the flag is pinned with a synthetic trailing-junk
  case instead.
- **`extra` field** (`keep_extra`). `True` adds field 6 (`extra`, everything past `python`,
  verbatim including any further commas) to the output row, present only when the value's split
  actually reaches it — matching the entities unit's own `Output.extra`
  (`map_entities_glb.decode._output_row`'s `",".join(fields[residue:])`). **Not** a zero-effect
  flag: retail writes seven comma-separated fields on almost every output even when the 7th is
  empty, so `keep_extra` is measured to add `extra: ""` to **1,027 of 1,028** outputs on
  `sp_tutorial_1`, **121 of 121** on `sm_pawnshop_1`, and **800 of 802** on `sm_hub_1` — moving
  338/34/228 `.ents` entity rows off `byte_equal` (`entityDiffCount`; every difference is exactly
  the added `outputs[].extra` key, nothing else).
- **`times` normalization** — no flag; already landed with exactly one owner. Legacy `.ents`
  carries an authored `0` as literal `0`, not `-1`, so this divergence cannot be a producer flag
  the way the other five are: the exporter must keep shipping the literal value, because
  `ElysiumEntityDefs.cpp`'s def loader already applies the `0` → `-1` rewrite once, at read time
  (`if (OutDef.Times == 0) { OutDef.Times = -1; }`, "normalised here rather than in the exporter,
  so an already-exported corpus behaves"). A producer-side option here would give the normalization
  two owners and risk a double-application; the field list's existing `times` rule
  (`int(float())` with `-1` on failure) is therefore final, and this bullet exists only to record
  that R3.4 checked it and found nothing left to land.

### R3.4 — the two the port surfaced

Two more divergences came up during R3.2's port, not from the field-list comparison above; R3.4
is where the roadmap assigns their decisions.

- **`.dispcol` precision — decided: accept the named divergence.** `displacement_triangles`
  documents the open choice: publish `DISP_VERTS`/`VERTEXES` numerically in the map root unit so
  `.dispcol` becomes byte-reproducible, or accept the measured 4th-decimal drift (max 0.0019 cm /
  0.0006 cm on the three-map corpus, both under `map_sidecar_diff.DISPCOL_TOLERANCE_CM`'s 0.01 cm
  margin) as named. Publishing the raw lumps numerically is a schema change to a *different* GLB
  unit (`map_glb`, owned by R2's work, not R3.4's), for a value nothing downstream reads at
  sub-millimetre precision — `.dispcol` only ever feeds collision. Per "wire first, tune later" and
  R3.4's own scope (a producer-option task, not a root-unit schema task), the divergence stays
  named rather than chased: `classify_dispcol`'s tolerance-gated classification (already landed in
  R3.3) is the final answer, not an interim one.
- **`sm_hub_1`'s embedded-quote entity block — decided: no producer option, already correct by
  construction.** `entity_lump_text`/`_requote` reproduce the legacy corruption verbatim (R3.2,
  pinned by `test_entity_lump_text_reproduces_the_embedded_quote_the_legacy_regex_trips_on`)
  because `write_sidecars` stays byte-comparable against a text-and-regex legacy reader. That
  corruption is purely an artifact of *reconstructing lump text and re-running the legacy regexes*
  — the entities unit's own `entities[].keyValues[]` never loses `logic_auto`'s `origin` in the
  first place, because it reads the lump structurally and never re-derives it from reconstructed
  text. R4.1's `UElysiumMapEntities` deserializes the entities unit directly, not through this
  producer's regex path, so it inherits the fix by construction and needs no flag here — unlike the
  six divergences above, there is no "byte-comparable default vs. corrected opt-in" axis to add:
  the correct reading is simply *not running this producer's text reconstruction at all*.

### Verification

The join above was executed against the published V2 units alone — root plus entities, no BSP read
— and diffed against the legacy sidecars on disk, 2026-08-31:

| Map | Brush entities | Hulls | Hull vertices | `hulls` / `contents` / `blocks_player` diffs | Meshed models (`brush_mesh` rows) | `sky` rows |
|---|---:|---:|---:|---:|---:|---:|
| `sp_tutorial_1` | 185 | 466 | 3,794 | 0 / 0 / 0 | 73 (73) | 59 |
| `sm_pawnshop_1` | 45 | 139 | 1,054 | 0 / 0 / 0 | 28 (33) | 79 |
| `sm_hub_1` | 147 | 345 | 2,694 | 0 / 0 / 0 | 55 (60) | 123 |

950 hulls and 7,542 hull vertices reproduced exactly, every `contents` word and `blocks_player`
bit equal, the meshed-model sets equal (with 5 and 8 `func_areaportalwindow` backing models
correctly suppressed on `sp_tutorial_1` and `sm_hub_1`), and all 261 `sky` rows equal. The five
joined facts are therefore recoverable from the V2 units, and the remaining risk in R3.2 is the
field list above, not the join.

## Import

R4.2 of `docs/project/seam_migration.md` → "Roadmap — one pipeline" moves the map's **collision**
off the loose `<map>.hulls` / `<map>.dispcol` documents and off the per-entity runtime cook, into
cooked content: one `UElysiumMapCollisionPayload` per map. Like R4.1's entity table this changes
transport and not geometry — the convex sets and the triangle soup are the same numbers the
sidecars carry — but unlike R4.1 it also changes *when the cook happens*, and that is the point of
the task.

**What the cook costs today.** `UElysiumMapCollision::LoadHulls` parses a text point cloud and
hands `SetCollisionConvexMeshes` 2,371 / 1,376 / 3,842 convex sets on the three working maps;
`FElysiumEntityWorld::BuildBrushBody` builds and *synchronously* cooks one `UBodySetup` per brush
entity, 185 / 45 / 147 of them. Every one of those cooks runs on every map load, and every one
mints `FGuid::NewGuid()`, so nothing is cacheable even in principle. The payload's body setups are
authored once, offline, with a stable `BodySetupGuid` saved in the package: the editor derives them
into the DDC on the first load after an import and never again, and a cooked build carries the
cooked buffers in the package. That is the "no runtime cook" the roadmap line asks for.

### Identity and naming

```text
$ELYSIUM_EXPORT_ROOT/<map>/<map>.hulls + .dispcol + .ents   (the sidecars this lane replaces)
  -> /ElysiumBaked/<map>/DA_<map>_Collision                 (UElysiumMapCollisionPayload)
```

One asset per map, in the map's own baked package folder beside its `.umap` and beside
`DA_<map>_Entities` — collision is a property of one map. `FElysiumContentPaths::BakedMapCollision`
is the one C++ accessor; its Python twin is
`elysium_pipeline.importers.map_collision.asset_path(map)`, and the two must agree exactly.

### What the payload carries

Three payloads, because the running game builds three colliders and they are not interchangeable:

| Member | Source | Shape |
|---|---|---|
| `WorldHulls` | `<map>.hulls` | one `UBodySetup`, one `FKConvexElem` per solid world brush |
| `Displacement` + `DisplacementVertices`/`DisplacementIndices` | `<map>.dispcol` | one `UBodySetup` whose trimesh is cooked from the asset's own triangle soup |
| `BrushBodies[]` | the `.ents` join's `hulls` | one `UBodySetup` per **brush entity**, keyed by lump ordinal |

The convex vertices live in the body setups' `AggGeom` and nowhere else — the payload carries no
loose vertex table beside them, so a brush entity's hulls are not duplicated between this asset and
`DA_<map>_Entities`: that asset states the entity's *definition* (which the substrate reads for
picks, gizmos and mover geometry), this one states its *cooked collision*.

**The asset is the collision data provider.** A `UBodySetup` reads trimesh source from
`Cast<IInterface_CollisionDataProvider>(GetOuter())`, so the displacement setup is outered to the
payload and `UElysiumMapCollisionPayload` implements that interface — the `UProceduralMeshComponent`
pattern with the asset, not a component, as the vessel. Convex elements need no provider; they cook
from `AggGeom` directly.

**Body-setup settings are stated, not inherited.** Each authored setup reproduces the recipe the
runtime builds by hand today, so the two paths are the same physics:

| | `WorldHulls` | `Displacement` | `BrushBodies[]` |
|---|---|---|---|
| `CollisionTraceFlag` | `CTF_UseDefault` | `CTF_UseComplexAsSimple` | `CTF_UseSimpleAsComplex` |
| `bDoubleSidedGeometry` | `true` | `true` | `false` |
| `bGenerateMirroredCollision` | `false` | `false` | `false` |

`CTF_UseDefault` and `bDoubleSidedGeometry = true` on the world hulls are not a choice made here:
they are what `UProceduralMeshComponent::CreateBodySetupHelper` gives the component today with
`bUseComplexAsSimpleCollision = false`, and the brush component's `CTF_UseSimpleAsComplex` is what
`InitBrush` sets. **One deliberate divergence:** the displacement trimesh is cooked with
`bFastCook = false` and `bDeformableMesh = false`, where the procedural-mesh component sets both
`true`. Those two flags exist because a procedural mesh cooks while the game runs; this payload
cooks offline, so it takes the full-quality cook. Nothing else in the trimesh contract changes —
`bFlipNormals` stays `true`, matching the component, and a Chaos trimesh is two-sided either way.

### Frames, and the one transform the stage applies

World hulls are already in the frame the world collider is registered at (component at the actor
origin, vertices verbatim), exactly as `.hulls` is read today. Brush-entity hulls are
**entity-local** — `world = entity origin + hull vertex`, the rule "The hull frame" above states —
and the payload stores them local, because the body component is seated at the live origin and that
origin is not a load-time constant (a mover moves).

The exception is the 3D skybox. `UElysiumMapEntities::Deserialize` scales a `sky` brush entity's
hulls by the `<map>.sky` scale (`world(v) = scale · (v − skyOrigin)`, hulls taking the scale and not
the translation), so the def the runtime holds is *not* what the `.ents` document stores. A cooked
convex cannot be rescaled after the fact, so **the stage applies that scale when it authors a `sky`
brush entity's body**, from the map's own `.sky`, and the load-time parity test compares the
payload's convex vertices against the deserialized def's hulls — the one place the two rules can
disagree, asserted rather than assumed. On the working corpus this is 4 brush entities of 377 (2 on
`sm_pawnshop_1`, 2 on `sm_hub_1`, 0 on `sp_tutorial_1`), all at scale 16. World hulls need no such
rule: `write_hulls` drops the miniature's own brushes outright.

### Producer and stage

`uv run elysium import map-collision --maps <map>…` stages one manifest under
`$ELYSIUM_WORK_ROOT/import/map_collision/` and then authors the assets in a headless editor
(`pipeline/unreal/import_map_collision.py`) — the same two-phase shape `import models` and
`import map-entities` have, and it refuses to run unscoped. The rows come from the loose sidecars
this lane replaces rather than from a second port of the hull solver: `<map>.hulls`,
`<map>.dispcol` and the `.ents` join's `hulls`, which since R3.5 are all written by the R3.2
producer from the published GLB units. Re-deriving them here would be a second implementation of
`brush_hull` with its own tolerances, which is exactly the divergence "Producer join" exists to
prevent.

**Parity is asserted at both ends, and the two assertions are not the same assertion.**

1. *At stage time*: the staged convex sets are compared against the sidecar rows they were read
   from, count for count and vertex for vertex, and a mismatch refuses the manifest. This proves
   the manifest carries the sidecar unchanged.
2. *At load time*: `Elysium.Content.MapCollision.*` loads the real baked asset and the real
   sidecars and compares what each **C++** path produces — convex count and per-hull vertex counts
   for the world set, triangle count for the displacement set, and per-entity convex geometry
   against `ElysiumEntityDefSource::Load`'s own defs (which is where the sky scale is proved). That
   is the parity that matters: it is the only check that the cooked asset and the loose files
   describe the same solid world.

### Consumption and cutover

`UElysiumMapCollision::Build` loads the payload first and falls back to the sidecar readers,
logging which source answered (`EElysiumCollisionSource`). On the payload path the two colliders
are still the collision-only `UProceduralMeshComponent`s the map has always used — same profiles,
same `ELYSIUM_USE`/`ELYSIUM_PICK` ignores, same registration order — with the payload's setup
assigned into `ProcMeshBodySetup` instead of one cooked from parsed text.

Both barriers keep their sources, as the roadmap line requires:

- **The collision-ready barrier** still reads the components' own body setups
  (`bCreatedPhysicsMeshes` / `bFailedToCreatePhysicsMeshes`), not a payload flag. On the payload
  path `Build` calls `CreatePhysicsMeshes()` before registering, so the state moves Cooking → Ready
  on the first poll instead of after an async cook; a payload whose meshes fail to create is
  `Failed`, exactly as a failed cook is today.
- **The runtime nav bounds** are still the union of the live components' bounds
  (`GetWorldBounds`). A collision-only procedural mesh has no render section to bound it, which is
  why `UElysiumHullCollisionComponent` already carries explicit local bounds; the payload path
  states those bounds from the geometry it just adopted (the convex `ElemBox` union, the trimesh
  vertex AABB) and the displacement component gains the same explicit bounds, which it needs on
  this path for the same reason — an unbounded component is invisible to the navigation octree.

`FElysiumEntityWorld::BuildBrushBody` asks the payload for the body of the entity it is building, by
lump ordinal, and cooks from `Def.Hulls` when there is no payload or no row for that ordinal. A
runtime-created entity (`CreateRuntimeEntityNoSpawn`, whose index runs past the map's def array)
therefore always cooks, which is correct: it has no authored collision to have baked.

**The asset's presence was the cutover flag through R4.2** — a map with a payload loaded cooked
collision, a map without one kept the sidecars, and no map needed an entry anywhere saying which.
R4.6 replaces that implicit rule with an explicit one (below); the outcome for an already-converted
map is unchanged, but the decision now lives in a tracked, reviewable place instead of in whichever
producer happened to run. The `.hulls`/`.dispcol` readers stay: they are the fallback for every
unlisted map, and R8.1 owns their deletion once all 108 maps are converted and listed.

### The explicit per-map cutover flag (R4.6)

Four resolvers each independently grew an "asset wins when present" rule (R4.1's
`ElysiumEntityDefSource::Load`, R4.2's `UElysiumMapCollision::AdoptPayload` above, R4.3's
`UElysiumLightRig::Adopt`, R4.4's `ElysiumMapEnvironmentSource::Load`) — correct for landing each
transport in isolation, but it means "is this map on the new transport" has no single answer: it is
whatever a producer happened to leave on disk, map by map, feature by feature. R4.6 adds one
tracked, explicit switch that answers that question for the three whole-swap transports (entities,
collision, environment) and states, by omission, that lighting calibration is not one of them.

`UElysiumMapTransportSettings` (`Config = Elysium, DefaultConfig`, Project Settings -> Elysium ->
Map Transport) carries one property, `MapsOnNewTransport` (`TArray<FName>`, map stems,
case-insensitive) — a tracked config list rather than a per-map asset, so adding a map to it is a
one-line, reviewable `Config/DefaultElysium.ini` edit and needs no recompile.
`ElysiumMapTransport::IsMapOnNewTransport(MapName)` (`ElysiumMapTransportSettings.h`) is the one
entry point; a second, pure overload takes an explicit `UElysiumMapTransportSettings` reference so
the resolution logic is testable without touching Project Settings or `GConfig`, matching
`UElysiumLightRig::ApplySettings`'s own synthetic-settings test shape (R4.3).

Each whole-swap resolver now checks the flag **before** its own `LoadObject` — an unlisted map
never even attempts to load its asset and falls straight to the sidecar path, exactly as it would
if the asset did not exist, regardless of whether one has in fact been baked for it:

- `ElysiumEntityDefSource::Load` (`ElysiumMapEntities.cpp`)
- `UElysiumMapCollision::AdoptPayload` (this file's `Build`, above) — `FElysiumEntityWorld::
  BuildBrushBody` needs no separate gate, since it only ever sees a payload `AdoptPayload` chose to
  set
- `ElysiumMapEnvironmentSource::Load` (`ElysiumMapEnvironment.cpp`)

A listed map whose asset is missing or unreadable still falls back to its sidecars — the flag names
intent, not a hard requirement that the asset exist, so an owner can list a map ahead of its bake
without bricking it. This means "unlisted maps boot the legacy path unchanged" is exact for the
common case (no asset yet) and merely conservative for the corner case (an asset exists early); the
line's actual guarantee is that an unlisted map's *behavior* never changes, not that its bytes are
inert.

**Deliberately not gated: R4.3's `UElysiumLightCalibration` merge-row apply.** That path is
additive — it applies calibration rows *on top of* the sidecar-derived baseline, never *instead of*
it (`seam_map_map_lighting.md` -> "Import" -> "Cutover") — so there is no legacy behavior for an
unlisted map to fall back to; the calibration asset's own presence already answers "does this map
have hand-tunes" for itself, and gating it on this list would only hide a hand-tune from a map an
owner has not yet flagged for the *other* three transports. R5.6's bake is what eventually retires
the `.lights` derivation this asset augments, and that is the task that gives lighting a real
legacy-vs-new split to gate.

`Elysium.Substrate.MapTransport.FlagResolution` exercises the pure resolver against synthetic
`NewObject`-built settings: an empty list resolves every map to the legacy path, a listed stem
resolves case-insensitively, and an unlisted stem stays on the legacy path even with others listed.
(A `NewObject<UElysiumMapTransportSettings>()` inherits the CDO's config-loaded array rather than
starting genuinely empty once `Config/DefaultElysium.ini` lists real maps, so the "empty list"
case clears `MapsOnNewTransport` explicitly instead of assuming a fresh `NewObject` is empty.)
`Elysium.Substrate.MapTransport.IniRoundTrip` proves the list round-trips through
`TryUpdateDefaultConfigFile`/`GConfig` on a scratch ini, the same shape every other
`Config = Elysium, DefaultConfig` page uses.

### Shot-diff against the R2.1 baseline (2026-09-01)

`sm_pawnshop_1`, `sp_tutorial_1` and `sm_hub_1` were headlessly booted (`uv run elysium debug
shots <map>`) with all three listed on `MapsOnNewTransport`, then compared against the R2.1
baseline (`8077e5b5f902`). All 14 vantages across the three maps fail `shots_diff.py`'s default
tolerance, at magnitudes matching the `sp_tutorial_1`-only failure R3.5 already found and filed as
a stale/anomalous baseline rather than a regression:

| Map | Vantages | Changed-pixels range |
|---|---|---|
| `sm_pawnshop_1` | 4 | 1.9%–98.2% |
| `sp_tutorial_1` | 6 | 79.6%–97.3% |
| `sm_hub_1` | 4 | 43.8%–77.7% |

Unlike R3.5, this run has all three maps failing, including `sm_pawnshop_1` and `sm_hub_1`, which
R3.5 recorded as pixel-identical (0.00% changed) against the same baseline. That gap is R4.1–R4.5
landing in between: those tasks changed the light rig (R4.3, additive calibration on top of the
existing derivation), the environment resolver (R4.4) and several rendering-adjacent settings
(R4.5), any of which can legitimately move rendered pixels even though none of them is this task's
own change. A control run proves the point directly: with `MapsOnNewTransport` emptied (every map
forced onto the 100%-legacy path, the same condition the R2.1 baseline was captured under),
re-shooting `sm_pawnshop_1` reproduces the same magnitudes (`p1` 1.56%, `p2` 95.24%, `p3` 92.32%,
`spawn` 99.34%) as the run with the flag set. The explicit cutover flag this task adds is
therefore not the source of the divergence — an already-converted map's resolved transport is
identical with the flag on or the flag entirely absent, exactly as "the outcome for an
already-converted map is unchanged" (above) states. This is a regression **witness**, not a fix:
per "wire first, tune later" and the standing house rule against reading screenshots to judge
looks, no look-tuning was attempted. Re-saving the baseline is an owner call, as R3.5 already
filed for `sp_tutorial_1`; this task extends that same finding to all three maps and leaves the
`_diff/<map>_*.png` artifacts under `$ELYSIUM_WORK_ROOT/exports/_shots/` for that review.

### Measured (2026-09-01, the three working maps)

| Map | World hulls | Hull vertices | Displacement triangles | Brush bodies (of them `sky`) | Asset |
|---|---:|---:|---:|---:|---:|
| `sp_tutorial_1` | 2,371 | 18,931 | 3,584 | 185 (0) | 3.09 MB |
| `sm_pawnshop_1` | 1,376 | 10,826 | 0 | 45 (2) | 1.53 MB |
| `sm_hub_1` | 3,842 | 28,751 | 288 | 147 (2) | 2.98 MB |

Every count is the sidecar's own, asserted by `Elysium.Content.MapCollision.WorldParity` (convex
count, per-hull vertex values, displacement triangle count) and `…BrushParity` (377 brush bodies
compared convex for convex against the defs `ElysiumEntityDefSource::Load` produces, 0 differing —
the four `sky` bodies included, which is the sky-scale proof).

**The load-time cost, honestly.** A headless boot of each map (`-nullrhi -unattended
-testexit="Activating after"`) reaches the collision-ready barrier on both transports; the
per-transport A/B, same machine, same build, warm DDC:

| Map | `Build` from payload | `Build` from sidecar |
|---|---:|---:|
| `sp_tutorial_1` | 143.1 ms | 21.1 ms |
| `sm_pawnshop_1` | 93.2 ms | 7.6 ms |
| `sm_hub_1` | 185.9 ms | 20.5 ms |

Those two numbers do not measure the same work, and the payload is not "slower" in the sense the
table reads. The sidecar's `Build` only *parses* and *schedules*: `bUseAsyncCooking` sends the cook
to worker threads, so its cook cost is spent after `Build` returns and never appears here. The
payload's `Build` loads a multi-megabyte package and creates the Chaos structures for 48–187 body
setups synchronously before it returns, and in an **editor** build each of those is a DDC lookup —
a cooked build reads the buffers inline from the package. Both transports report the barrier
`Ready` by the time construction completes, and the total activation times are within noise of each
other (1.3–3.6 s on these maps, dominated by navigation, audio and animation preload) because the
sidecar's asynchronous cook finishes inside that window anyway. The claim R4.2 lands is
"the cook happens once, offline", not "the map loads faster"; making the adopt asynchronous is a
tuning question and deliberately not this task's.

## Import — environment

R4.4 of `docs/project/seam_migration.md` → "Roadmap — one pipeline" moves the map's **environment**
— `<map>.env`'s 2D-sky flag and its two fog sets, `<map>.sky`'s 3D-skybox miniature placement
transform, and `<map>.spawn`'s initial player spawn — off the three loose sidecars and onto one
`UElysiumMapEnvironment` per map. Like R4.1/R4.2 this is a transport change and nothing else: every
value the asset carries is the value the sidecar already states, copied straight into the plain
`FElysiumEnvDef` / `FElysiumSkyDef` / `FElysiumSpawnDef` structs the runtime has always consumed.
There is no cook and nothing derived — the whole asset is a dozen scalars, a couple of colours and
two vectors.

**What is deliberately NOT here.** None of these three sidecars carries a taste value in the sense
R4.3's light calibration or R4.5's orphan knobs do. The fog numbers are `ApplySceneFog`'s inputs
unchanged — a per-primitive custom-primitive-data stamp, not a look decision — and the sky/spawn
transforms are placement, not appearance. The one genuinely tunable neighbour, the baked
`AExponentialHeightFog` actor's own component properties (density, height falloff, and so on), is
**not** part of this asset: it is placed by the bake and now has no Cog surface at all (R4.3 retired
the "Sky & fog" tab's height-fog sliders along with the rest of the rig-tuning tabs), so the editor
level *is* the tuning surface for it — an owner edits the actor's `Fog` component directly, the same
direct-actor-edit lane the roadmap line names. This asset does not reach it and never will.

### Identity and naming

```text
$ELYSIUM_EXPORT_ROOT/<map>/<map>.env + .sky + .spawn   (the sidecars this lane replaces)
  -> /ElysiumBaked/<map>/DA_<map>_Environment           (UElysiumMapEnvironment)
```

One asset per map, in the map's own baked package folder beside its `.umap` and beside
`DA_<map>_Entities`/`DA_<map>_Collision` — the environment is a property of one map.
`FElysiumContentPaths::BakedMapEnvironment` is the one C++ accessor; its Python twin is
`elysium_pipeline.importers.map_environment.asset_path`.

### What the asset carries

Three independent value sets, because the three sidecars answer three independent questions and a
map can have any combination present:

| Member | Source | Absent state |
|---|---|---|
| `bSky`/`SkyName`/`SkyConvention`, `bFog`/`FogColor`/`FogStartCm`/`FogEndCm`, `bSkyFog`/`SkyFogColor`/`SkyFogStartCm`/`SkyFogEndCm` | `<map>.env` | all-zero/false row (`FElysiumEnvDef`'s own default) |
| `bHasSkyMiniature`, `SkyOriginCm`, `SkyScale` | `<map>.sky` | `bHasSkyMiniature = false`, the identity scale 1 |
| `bHasSpawn`, `SpawnOriginCm`, `SpawnYawDeg` | `<map>.spawn` | `bHasSpawn = false` |

`.env` is written for every map in the corpus (a map with no `sky_camera` still states its world
fog), so `bSky`/`bFog`/`bSkyFog` false is itself the authored reading on the maps that carry it, not
an absence. `.sky` and `.spawn` are each written only when their entity exists (`sky_camera`,
`info_player_start`), so their `bHasSkyMiniature`/`bHasSpawn` flags distinguish "authored, present"
from "this map has none" — exactly what `FElysiumSkyDef::Parse`/`FElysiumSpawnDef::Parse` returning
`false` already meant, now carried as data instead of read off a missing file.

`UElysiumMapEnvironment::ToEnvDef`/`ToSkyDef`/`ToSpawnDef` are the three conversions back to the
plain structs — a field-for-field copy, asserted by `Elysium.Substrate.MapEnvironment.*`.

### Producer and stage

`uv run elysium import map-environment --maps <map>…` stages one manifest under
`$ELYSIUM_WORK_ROOT/import/map_environment/` and then authors the assets in a headless editor
(`pipeline/unreal/import_map_environment.py`) — the same two-phase shape `import map-entities` and
`import map-collision` have, and it refuses to run unscoped. The rows come from the sidecars
themselves: since R3.5 `<map>.env`/`.sky`/`.spawn` are written by the R3.2 producer straight off the
BSP entity lump (`UE_map_sidecars.write_environment`/`write_sky`/`write_spawn`), so this stage is a
pure carry rather than a second implementation of anything.

**Parity is asserted at both ends, and the two assertions are not the same assertion**, exactly as
R4.1/R4.2 state it:

1. *At stage time*: the staged `env`/`sky`/`spawn` values are compared field for field against the
   sidecar rows they were read from, and a mismatch refuses the manifest.
2. *At load time*: `Elysium.Content.MapEnvironment.*` loads the real baked asset and the real
   sidecars and compares what each **C++** reader produces — `UElysiumMapEnvironment::ToEnvDef`/
   `ToSkyDef`/`ToSpawnDef` against `FElysiumEnvDef::Parse`/`FElysiumSkyDef::Parse`/
   `FElysiumSpawnDef::Parse`. That is the parity that matters: the stage's own comparison proves
   nothing about whether the C++ side the running game actually uses agrees.

### Consumption and cutover

`ElysiumMapEnvironmentSource::Load` is the one entry point, called once per map load
(`AElysiumMapActor::LoadMap`, before `AdoptBakedLevel` — the same "read `.sky` first" ordering R4.1
documented, because the light rig, the `.ents` join and the miniature body all need `SkyDef`). The
baked asset wins whole when it exists; the three sidecars answer independently otherwise, each
exactly as `FElysiumEnvDef`/`FElysiumSkyDef`/`FElysiumSpawnDef::Parse` always has. Downstream:

- `UElysiumMapVisuals::ApplyEnvironment` now takes the resolved `FElysiumEnvDef` as a parameter
  instead of parsing `<map>.env` itself — `ApplySceneFog`'s stamping and the sky-cube assembly are
  otherwise untouched.
- `SkyDef` (the member `AElysiumMapActor` already carried) is filled from the resolver instead of a
  direct `FElysiumSkyDef::Parse` call; every consumer of it (the light rig's `Adopt`, `.ents`'s
  sky-scope transform via `ElysiumEntityDefSource::Load`, the miniature body's mesh scale) is
  unaware anything changed, because the type crossing the boundary did not.
- `AElysiumMapActor::ReadSpawn` is retired; the resolver's spawn output feeds
  `PendingSpawnLoc`/`PendingSpawnYaw` directly, still Source feet with the capsule-centre lift left
  to the readiness poll, exactly as before.

**The explicit per-map cutover flag gates this resolver too**, as of R4.6 (this file's "## Import"
-> "The explicit per-map cutover flag (R4.6)", above): `ElysiumMapEnvironmentSource::Load` checks
`ElysiumMapTransport::IsMapOnNewTransport(MapName)` before its `LoadObject`, so an unlisted map
reads the three sidecars unconditionally, the same as before R4.4 existed. The `.env`/`.sky`/
`.spawn` readers stay: they are the fallback for every unlisted map, and R8.1 owns their deletion
once all 108 maps are converted and listed.

### Measured (2026-09-01, the three working maps)

| Map | Sky name | World fog | 3D-skybox fog | Sky miniature scale | Spawn |
|---|---|---|---:|---:|---|
| `sp_tutorial_1` | `la` | off | off | 16 | `(-35.56, -19032.22, -416.56)`, yaw -270° |
| `sm_pawnshop_1` | `pier` | on, 1270→12700cm | on, 20320→203200cm | 16 | `(-5032.04, 6568.26, 388.62)`, yaw 0° |
| `sm_hub_1` | `pier` | on, 1270→12700cm | on, 20320→203200cm | 16 | `(-4321.25, 7938.54, -261.62)`, yaw 0° |

All three maps carry all three sidecars, so all three assets have `bHasSkyMiniature = true` and
`bHasSpawn = true`; the identity/absent paths above are exercised by
`Elysium.Substrate.MapEnvironment.*`'s synthetic assets, not by this corpus slice.

## Import — geometry and placements

R5.1 of `docs/project/seam_migration.md` → "Roadmap — one pipeline" moves a map's **geometry** and
its **static-prop placements** off the legacy `<map>.obj`, `<map>_sky.obj`, `brushes/*.obj` and
`<map>.props` sidecars and onto the published map root unit — the `world`, `brushModels`,
`displacements` and `placements` scenes this document defines above. It is the first task that
authors a map from the corpus rather than from the legacy exporter's own intermediate files.

**Geometry only, and props onto the R1 corpus.** Materials are R5.4's, the sky dome is R5.2's,
reflection captures are R5.5's and lights are R5.6's; this lane changes where the triangles and the
placements come from and nothing else. A V2 surface therefore binds the *same*
`MaterialInstanceConstant` the legacy surface bound, resolved through the same `<map>.mtl` table, so
a shot-diff of this task sees geometry, not shading.

### Identity and naming

```text
$ELYSIUM_EXPORT_V2_ROOT/maps/<map>.glb        (vtmb:map:<map>, the root unit)
  -> /ElysiumBaked/<map>/Meshes/SM_World_<cx>_<cy>_<cz>      world chunks (T_ prefix: non-Nanite)
  -> /ElysiumBaked/<map>/Meshes/SM_Sky_<cx>_<cy>_<cz>        3D-skybox miniature chunks
  -> /ElysiumBaked/<map>/Brushes/SM_brush_<n>                one local-pivot mesh per BSP submodel
  -> /ElysiumBaked/<map>/<map>.umap                          placements, on /ElysiumBaked/Meshes/SM_*
```

Every asset path, chunk key, slot name and brush stem is the legacy lane's, unchanged: this is a
change of **producer**, not of layout, so a map can be moved between the two lanes without renaming
a single asset and the runtime's `brush_mesh` join keeps resolving. The offline half is
`elysium_pipeline.importers.map_geometry` (no editor, so pytest walks a real map); the editor half is
`pipeline/unreal/bake_map_v2.py`, a `bake_map.Bake` subclass that replaces two inputs and one stage.

**The split is forced, not stylistic.** Reading the unit needs `numpy` — the sky-area BSP walk and
the accessor decode — and Unreal's embedded CPython does not carry it. So the read runs offline, in
the `uv` interpreter, and writes one `manifest.json` plus one packed little-endian vertex file per
map under `$ELYSIUM_WORK_ROOT/import/map_geometry/<map>/`; the editor half reads that pair with
`json`, `struct` and `array` alone. The stage runs from `unreal.bake_maps`, immediately before the
commandlet launches and only for maps on the flag, so the bake's inputs and the published unit can
never be a version apart — it costs about 1.5 s per map. This is the offline-stage / editor-import
shape the model, material and texture lanes already have.

**One classifier, not two.** The world / 3D-sky / brush-model face split, the sub-three-edge and
missing-texinfo drops, the `tools/` namespace drop (`tools/black` and `tools/toolsblack` excepted),
the `func_areaportalwindow` backing-model drop, and the sky-area membership test are the R3.2
producer's own (`exporters.UE_map_sidecars.prepare_join`), imported rather than re-derived. Two
implementations of "which faces are the miniature" is exactly the divergence R3.3 exists to catch,
and the map bake and the `.hulls`/`.ents` sidecars have to agree by construction.

### The frame

The unit publishes glTF metres, Y-up, right-handed; the bake wants Unreal centimetres, Z-up,
left-handed. `(x, y, z)_gltf -> (x, z, y)_unreal x 100` is a reflection, so every triangle's winding
is reversed on the way out — the same reversal `UE_bsp_to_scene` applied at OBJ-write time and for
the same reason (`formats.bsp.source_to_unreal`). A rotation composes the unit's own
`(x, y, z, w)_gltf = (x, z, -y, w)_source` with `source_quat_to_unreal`'s `(-x, y, -z, w)`, giving
`(-x, -z, -y, w)` — the legacy `.props` quaternion up to the overall sign a quaternion is free in.

**UVs.** `TEXCOORD_0` is the unit's own planar projection, normalized by the TEXDATA size, which is
the legacy `emit()` formula unchanged. A **displacement** primitive carries `POSITION`, `NORMAL` and
`_ALPHA` and no `TEXCOORD_0` — the unit states the sculpted geometry, not its albedo frame — so this
lane recomputes the grid's UV from the map face's own `texinfo` vectors, the same projection applied
to the same points. `_ALPHA` is the `WorldVertexTransition` blend weight the legacy `.blend` sidecar
carried and becomes vertex `COLOR.r` exactly as before.

**Vertex sharing is the source's.** Each face owns its own vertex run in the unit's primitive
(`firstVertex`/`vertexCount`), and a displacement owns its grid, so corners are shared within one
face's fan and within one displacement and never across a face boundary — which is what makes
recomputed normals come out flat across a BSP face boundary and smooth inside a displacement, the
same shading the legacy OBJ produced.

### Placements: solid, skin and fade

The `staticProps[]` record is the authority, not the model — `seam_map_model.md` → "Import" →
"Collision" states the per-model asset carries what each mode needs precisely so this lane can
choose without going back to the source. The three per-placement facts are baked in, because a
`GAME_LUMP` prop is not an entity and never changes any of them at run time:

| Record field | Baked as |
|---|---|
| `solid` (`SolidType_t`) | `SOLID_NONE` (0) → the `ElysiumPickOnly` profile (drawn, touched by nothing but the debug pick); every other value → `ElysiumPropSolid`, blocking on whatever simple collision the model asset carries |
| `skin` | the family's slot overrides from `/ElysiumBaked/Meshes/DA_ElysiumPropSkins`, applied as component material overrides |
| `flags & 0x1` (`FADES`) with `fadeMaxDist > 0` | `LDMaxDrawDistance = fadeMaxDist x 2.54` cm |

**Ruling — a VPHYSICS placement of a model that ships no `.phy` still blocks.** The model lane left
this open ("stood inert (faithful) or given the box (playable) is the placement lane's ruling"), and
this lane takes the box. Three reasons, in order. (1) The warning path that ruling cites,
`CPhysicsProp::CreateVPhysics` → `ERROR!: Can't create physics object`
(`docs/vtmb/phy_vphysics.md` → "Missing collision"), is the *entity* path — a `prop_physics` that
must simulate. A `GAME_LUMP` static prop never simulates; it only needs a shape to block against.
(2) A placement authored `solid 6` is authored to block, and the missing `.phy` is a gap in the
**model**, not a statement about the placement. (3) The legacy lane already blocked on these props
(via complex-as-simple on the render mesh), so standing them inert would be a gameplay regression
introduced by a transport change, which is the one thing a cutover task must not do. The rule is
therefore uniform and needs no census branch: **`solid != 0` blocks, `solid == 0` does not**, and
the model asset's own simple collision — the `.phy` ledges when it has them, the studio-header box
when it does not — is what it blocks with.

**Ruling — the miniature is never solid.** A 3D-skybox placement takes `ElysiumPickOnly` whatever
its `solid` byte says, and is scaled, shadow-less and out of the ray-tracing scene, exactly as the
legacy lane placed it: it is scenery the player can never reach, and at 16x it would wall the map
off.

**Detail props are the same lane's second placement family, instanced.** The `placements` scene's
other node family (`dprp`, 143,412 records over 41 models corpus-wide) is placed by "Detail props
(R6.3)" below: one instanced component per model per map, not one actor each.

### Brush fade distances (R6.4)

R6.4 of `docs/project/seam_migration.md` → "Roadmap — one pipeline" gives the two distance-culled
brush classes the FADES treatment above. **The bake has no brush actor to write onto**: a brush
entity's mesh is `SM_brush_<N>` under `/ElysiumBaked/<map>/Brushes`, never placed in the level —
the runtime attaches it to the convex entity body that owns movement, collision, hiding and
teardown (`FElysiumEntityWorld::BuildBrushBody` → `IElysiumEmbodiment::BuildBrushVisual`). So the
bake's product for a brush entity is its **entity-table row**, and that is where the distance is
written, exactly as `elevator_floors` and `blocks_player` already are: derived once by the
producer, applied by the runtime, never re-read from a keyvalue at load.

| Row | Rule |
|---|---|
| `func_lod` with a `brush_mesh` and `DisappearDist > 0` | `cull_max_cm = DisappearDist × 2.54`; the world sets `SetCullDistance(cull_max_cm × body scale)` on the brush visual the moment it is attached |
| any other row | no `cull_max_cm`; the visual keeps Unreal's default (never culled by distance) |

The join is the row's **own** `model` (`brush_mesh` is `"brush_<model>"`): a `func_lod` owns the
brush it hides. VtMB's client side (`C_Func_LOD::ShouldDraw`, client.dll `100bb710`) is a hard
draw/no-draw on the view distance to the entity origin with a hysteresis band beyond
`DisappearDist`, not an alpha fade, so `LDMaxDrawDistance` is the faithful equivalent — the same
mapping R5.1 gives a `FADES` prop (`fadeMaxDist × 2.54`). The three working maps carry 8 / 12 / 13
`func_lod` rows, every one meshed, at 2,200–4,000 units.

**`func_areaportalwindow` writes nothing here, and that is an owner call filed to R7.** All 13 rows
on the three maps (and 246 corpus-wide by the R6 census) are point rows: none carries a `model`.
The distances `FadeStartDist`/`FadeDist` govern the **`target`** brush — the black backing whose
mesh the exporter omits by the ruling in `docs/vtmb/entity_io.md` → "`func_areaportalwindow`:
Source visibility backing" — and `BackgroundBModel` names the foreground glass VtMB keeps fully
drawn at every distance, so culling it at `FadeDist` would invert the behaviour (the glass would
vanish, the interior would show). The one faithful reading — re-mesh the backing and give it
`MinDrawDistance = FadeDist × 2.54` (transparent near, black far, VtMB's two end states) —
reverses that earlier ruling, so it is not taken here. The class exists (`ElysiumBrushFadeClasses.cpp`)
and carries the parsed distances, target and background names for the debug view; the choice is
stated in the R7 list.

**Transport.** `UE_map_sidecars.brush_cull_max_cm` is the pure rule, `build_entities` writes the
field, `FElysiumEntityDefs::Parse` reads `cull_max_cm`, `FElysiumMapEntityRow::CullMaxCm` carries
it through the R4.1 asset (`seam_map_map_entities.md` → "The row shape"; recipe version 2 so every
listed map's asset re-authors), and both parity checks compare it. `bake_verify.verify_brush_cull`
asserts, per map, every `func_lod` row's `cull_max_cm` against its own `DisappearDist` in the
`.ents` and in the baked asset.

### Detail props (R6.3)

R6.3 of `docs/project/seam_migration.md` → "Roadmap — one pipeline" places the `dprp` game lump.
The corpus: **143,412 records over 41 models on 52 of 108 maps**, every one a `dprp` version 2
record (`formats/map_glb/gamelump.py`: origin, angles, `detailModel`, `leaf`, `lighting`,
`lightStyles`, `lightStyleCount`, `swayAmount`, `shapeAngle`, `shapeSize`; no sprite dictionary —
every corpus detail is a model), 35,521 of them with a non-zero `swayAmount`. On the three working
maps: `sp_tutorial_1` 6,031 over 6 models (`grassa`/`grassb`, `rocks/smalla`/`smallb`/`smallc`,
`junk4`; every `swayAmount` 0), `sm_pawnshop_1` 0, `sm_hub_1` 528 over 3 (`weedb`/`weedc`/`weedd`,
231 swaying). Each record is a node of the `placements` scene already (`detailProp[<i>]`, with
`ELYSIUM_model_reference`), and its model is a `dependencies[]` row under role `model`, so the R1
map-scoped selection (`select_for_maps`) already stages every detail model beside the static
props — all nine on the working corpus were in `/ElysiumBaked/Meshes` before this task.

**Ruling — one `AElysiumDetailPropActor` per model per map, one instance per record.** The offline
reader (`map_geometry._detail_placements`) resolves every record in lump order to its R1 stem
(`shared_corpus.static_stem`, the same key a static prop uses), takes the node's transform through
the same `gltf_position_to_unreal`/`gltf_quat_to_unreal` a static prop takes, and stages them as
`details.models[]` + `details.records[]` (manifest **v5**). The editor half (`bake_map_v2.
_place_details`) groups records by `(model, sky)` and spawns one `AElysiumDetailPropActor` per group
— a plain actor whose root is a `UInstancedStaticMeshComponent` (`Instances`) on the corpus mesh
`/ElysiumBaked/Meshes/SM_<stem>` — and adds the group's records as instances **in lump order**, so
instance index `k` of a model's component is that model's `k`-th record and `bake_verify` can count
them back against the staged table. A model with no `SM_` asset is the same loud failure a static
prop's is (`run: uv run elysium import models --maps <map>`), never an empty field. Labelled
`Detail_<stem>` / `Detail_<stem>_sky`, folder `Details` / `Sky/Details`, tags
`elysium.detail` + `elysium.model=<stem>` (`ElysiumBakedTags::Detail` / `DetailModel`).

| Record / rule | Baked as |
|---|---|
| origin, angles | one instance transform (world space), the placements-scene node's, in the R5.1 frame |
| a record inside the 3D-skybox area | the miniature transform, exactly as a static prop: position `scale × (p − origin)`, uniform scale `scale`, its own `_sky` component carrying the `elysium.sky` scope marker beside its class tags (R6.7), `visible_in_ray_tracing = false`. None on the three working maps |
| collision | **none** (`NoCollision`). `dprp` is read by `client.dll` alone (`CDetailObjectSystem`, `CDetailModel` — `100e0d90`, `100e0250`…); the server never sees a detail object and nothing in VtMB ever collides with one |
| shadows | `CastShadow = false`. VRAD never lights the world by a detail prop, and the engine drew them without shadows; the record's `lighting` (`ColorRGBExp32`) and `lightStyles` are VRAD's baked *answer* for the 2004 renderer (`CDetailModel::vfunc13` `100e0300` multiplies them into the draw colour) — on the V2 lane the R5.6 light actors light the instances, so the bytes are not applied |
| draw distance | `Instances->SetCullDistances(start, end)` with `end = DetailDrawDistanceCm`, `start = end − DetailFadeRangeCm`, both off `UElysiumModelSettings` (Project Settings → Elysium → Models → Detail Props). **Defaults are VtMB's own**: `cl_detaildist` **600** and `cl_detailfade` **300** inches → **1524 cm** and **762 cm** (`CDetailObjectSystem::vfunc10` `100e0d90` registers both ConVars; their default strings sit at `102b9fa0` = `"600"` and `102b9f8c` = `"300"` in `client.dll`, read off the shipped binary). VtMB's fade over the band is an alpha ramp, `1 − (d² − (dist − fade)²) / (dist² − (dist − fade)²)`, which that function computes as the per-frame factor; the ISM culls hard at `end` and exposes the band as `PerInstanceFadeAmount`, which no master reads — the shipped rule is the cutoff at `cl_detaildist`, the ramp is named in the entry's follow-ups |
| `swayAmount` (0..255) | per-instance custom data float **0** = `swayAmount / 255`, read by the master's `UseDetailSway` term (`seam_map_material.md` → "Detail sway on the model masters (R6.3)"). A record with `swayAmount = 0` gets an exact 0 and never moves |
| scene fog | `set_fog(Instances, world_fog | sky_fog)` — the same Custom Primitive Data stamp every prop takes (`ElysiumFog.h`); one stamp per component |
| `leaf`, `shapeAngle`, `shapeSize`, `lightStyleCount` | not carried: the leaf is the BSP's own culling key (Unreal culls by bounds), the shape fields describe sprite details (0 sprites on the corpus), and the lightstyle table is the baked lighting above |

**The sway material is a child instance, never a rewrite of the shared `MI_`.** A detail model's
slots bind the material lane's own imported `MI_` (the model asset already carries them), and
that instance is shared by every static prop and every other map that draws the same VMT. The
sway term must therefore be switched **on** only where an instanced draw feeds it, or every Nanite
chunk and every prop on the master would pay a World Position Offset it never uses
(`HLSLMaterialTranslator::IsMaterialPropertyUsed`: a constant-zero WPO is *not* "used", a
switched-off branch compiles to exactly that). So the bake authors, once per detail material,
`/ElysiumBaked/Meshes/Detail/MI_DetailSway_<material path>` — a `MaterialInstanceConstant`
parented to the imported `MI_` with the one static switch `UseDetailSway = true`, recipe-stamped on
the parent's path and the master's graph version — and binds it as the component's slot override.
Map-independent, like the mesh it dresses. A detail material whose master has no `UseDetailSway`
switch is a named bake failure, not a silently still weed. The instances are not pruned by a map
bake (the package is shared, like `/ElysiumBaked/Meshes`; a map's prune scope never reaches it).

**Runtime.** `UElysiumMapVisuals::AdoptBakedLevel` buckets `elysium.detail` actors into
`DetailActors`, sums their instance counts into `DetailInstanceCount` / `DetailModelCount` (the
Cog Maps/Status rows and the boot log line `baked '<map>': … %d detail instances over %d models`),
and `elysium.props` hides them with the static props. Nothing else reads a detail actor: it is
scenery with no entity, no collision and no input.

#### Verification (R6.3)

`pipeline/tests/test_map_geometry.py` pins the record → placement mapping (model index → stem,
lump order preserved, the frame, `swayAmount` carried raw, the miniature flag) and, corpus-gated,
that every record on the three maps resolves to a staged model and the per-model counts match the
root unit's own records. `pipeline/tests/test_bake_map_details.py` pins the editor half's pure
grouping (`detail_instance_rows`: by `(stem, sky)`, lump order inside a group, `swayAmount / 255`,
the sky transform) and the manifest version the two halves share. `bake_verify.verify_details`
loads the level and counts every `elysium.detail` actor's instances against the staged
`details.records[]` per model — one component per `(model, sky)`, the same count, no model missing
and none extra — and checks the cull range against the settings page and that every component
carries exactly one custom-data float. `Elysium.Substrate.DetailProps` pins the settings defaults
(`600 × 2.54`, `300 × 2.54`, `5 × 2.54`), the `DetailSwayAmplitude` binding, the tag helpers and
the actor's own shape (ISM root, `NoCollision`, no shadow). Measured numbers are in the R6.3
Settled entry of `docs/project/seam_migration.md`.

### Sprites (R6.1)

R6.1 of `docs/project/seam_migration.md` → "Roadmap — one pipeline" draws every `env_sprite`.
The corpus: **6,449** rows over 108 maps, 1,286 named, 86 `start_hidden`; by `rendermode` **3
(Glow) 4,347 + 9 (WorldGlow) 77** — Source's coronas (`glowa`/`glowb`) — **5 (Additive) 1,552**
and **1 (Color) 472** — the plain billboards (`volumelight*` shafts, `candle`, `coplights`,
`lightning*`). The owner's ruling (2026-09-02): **all of them are drawn, coronas included**, one
billboard actor per entity in the baked level, on the imported `MI_` of the sprite VMT, and the
entity's own inputs drive its visibility. VtMB's facts below are read off the shipped binaries;
nothing here is a look judgement.

**What VtMB does (`client.dll`, `vampire.dll`).** `CSprite::Spawn` (vampire.dll `1042e550`)
clamps `scale` to `0..8` (`DAT_10516cec`, with a `LEVEL DESIGN ERROR` DevMsg) and starts the
sprite **on when it is unnamed or carries spawnflag 1 (Start On), off otherwise** (`m_fEffects |=
EF_NODRAW`, `1042ef40`); `HideSprite`/`TurnOff` clear the draw, `ShowSprite`/`TurnOn` restore it,
`ToggleSprite` flips it (`1042f080`…`1042f130`). The client draw (`C_Sprite::DrawModel`
`10136890` → `DrawSprite` `100c3480`) writes the entity's `rendermode` into the material's
`$spriteRenderMode` var (`CEngineSprite::Init` `10136da0` caches it) — so **the entity's mode,
not the VMT's, selects the blend** — and for modes **3 and 9 only** multiplies the blend by
`GlowBlend` (`100c24a0`), Source's glow rule, whose constants are `.rdata` doubles:

| Term | VtMB | Where |
|---|---|---|
| apparent size | `scale ← (scale or 1) × dist × 0.005` (dist in Source units; `1/200`) — **mode 3 only** (`100c3197` skips WorldGlow) | `102261e0` |
| brightness | `clamp(19000 / dist², 0.05, 1.0)` | `102324b0`, `101e55b0`, `101e34e0` |
| `renderfx` 14 (NoDissipation) | the whole block is skipped (`100c30e9`): no distance brightness, no screen-constant size, the visibility fade alone. 207 of sm_hub_1's 309 rows and 55 of sp_tutorial_1's 96 carry it | `100c24a0` |
| visibility | pixels of a small quad at the origin that pass the depth test ÷ pixels drawn without it, clamped 0..1, **smoothed**: rises at `1 / r_glowfadein` (**0.2 s**) and falls at `1 / r_glowfadeout` (**0.1 s**) per second | `104a4378`, `104a4258`; defaults `102b1fd8`, `102b639c` |
| query quad | half-size `dist × 3/128` for mode 3 (screen-constant), 3 units for mode 9 | `102324d0`, `10225158` |
| final blend | `renderamt/255 × brightness × smoothed visibility` | `100c3480` |

Every other mode draws the sprite at `scale × texture` world units, `rendercolor` as the colour
modulation and `renderamt/255` as the blend, depth-tested by the material's own state.

**Ruling — one `AElysiumSpriteActor` per row, the values written once, the glow rule live.**

| Fact | Baked as |
|---|---|
| identity | `sprites[]` row `index` = the entity's lump ordinal = `FElysiumEntityHandle::Index`; tags `elysium.sprite` + `elysium.ent=<index>` (`ElysiumBakedTags::Sprite` / `EntityIndex`); label `Sprite_<index>_<material stem>`, folder `Sprites` / `Sky/Sprites` |
| material | the imported `MI_` of `model` (`shared_corpus.material_key`, resolved through the material lane's staged provenance sidecar exactly as a face is), re-parented once per `(MI_, blend)` through `/ElysiumBaked/Sprites/MI_Sprite_<material>_<blend>` — a child whose own values are the blend override and `UseVertexColor = UseVertexAlpha = true`, so the tint and the per-frame glow blend ride the quad's vertex colour and no material is touched at runtime. Shared and map-independent like the R6.3 sway children |
| blend | the entity's `rendermode` through the `$spriterendermode` row table (`seam_map_material.md` → `M_V2_Sprite`): 0 → Opaque; 1, 2, 3, 4, 9 → Translucent; 5, 7, 8 → Additive; 6 → a named bake failure |
| size | `SizeInches = (clamp(scale, 0, 8) or 1) × (texture width, height)` in Source units, the texture's dimensions off the texture lane's sidecar. Mode 3 without `renderfx` 14: `SizeInches × dist_cm × GlowSizePerDistance` — screen-constant, per view. Every other row (mode 9, NoDissipation, the plain modes): the world quad is `SizeInches × 2.54` cm (× the actor scale, so a miniature sprite scales with the miniature) |
| colour | `rendercolor` (default 255 255 255) and `renderamt` (default 255) as the component's `Color`; `renderfx` carried for the NoDissipation rule |
| orientation | `parallel_upright` in the VMT's `$spriteorientation` row → `bUpright` (a yaw-only billboard about world Z); `vp_parallel`, `oriented` (3 units, treated as full billboards — a named divergence) and absent → full camera-facing |
| initial state | `bHiddenAtSpawn = start_hidden or (named and not spawnflag 1)` — `CSprite::Spawn`'s own rule, written by the bake so the level reads right in the editor and published again by the leaf at spawn so the two can never disagree |
| 3D skybox | a row inside the sky area takes the miniature transform a prop takes: position `scale × (p − origin)`, actor scale `scale`, its own `_sky` label and folder, and the `elysium.sky` scope marker beside its two tags (R6.7) |
| collision, shadow, fog | none, none, none: a sprite is a client-side card in VtMB; `M_V2_Sprite` carries no scene-fog term |

**The runtime half — `UElysiumSpriteComponent` and its proxy, the task's one piece of rendering
code.** `FElysiumSpriteSceneProxy` builds one camera-facing quad per view in
`GetDynamicMeshElements` (a yaw-only quad for `bUpright`), sized by the rule above, with vertex
colour `(rendercolor, renderamt/255 × brightness × visibility)`. For every sprite it declares
**sub-primitive occlusion queries** (`HasSubprimitiveOcclusionQueries`/`GetOcclusionQueries`): a
`SpriteQueryGrid × SpriteQueryGrid` grid of small boxes tiling a square of half-size `dist ×
SpriteQueryFootprintPerDistance` at the origin, facing the view — Source's query quad, sampled as
a grid because Unreal's sub-query answers are per-box booleans, not pixel counts. The renderer
tests them against the scene depth on the GPU (hardware queries, HZB or occlusion feedback,
whichever the platform runs) and hands the booleans back one frame later
(`AcceptOcclusionResults`); the **visible fraction is the visible boxes over the grid**, and the
proxy smooths it at `1/GlowFadeInSeconds` up and `1/GlowFadeOutSeconds` down on the render
thread's clock, exactly VtMB's two cvars. `CanBeOccluded()` is forced true: the master is
depth-test-off, and the engine's default would have skipped the queries. The fraction gates every
sprite, not only the coronas: `M_V2_Sprite` disables the depth test on the master (the material
lane's ruling, `$ignorez`), so a plain additive card behind a wall would otherwise draw through
it; a card half behind a table fades as a whole instead of clipping — named below as the R7 call.
No occlusion result yet (the first frame, or a renderer with queries off) reads as fully visible.

Everything tunable is a field on **Project Settings → Elysium → Sprites** (`UElysiumSpriteSettings`),
shipped with VtMB's own values and read by the proxy at creation: `GlowFalloff` **19000**,
`GlowMinBrightness` **0.05**, `GlowSizePerDistance` **0.005**, `GlowFadeInSeconds` **0.2**,
`GlowFadeOutSeconds` **0.1**, `SpriteQueryFootprintPerDistance` **3/128**, `SpriteQueryGrid`
**4** (16 boxes; the one number with no VtMB twin — Source counted pixels). Cost accepted with
eyes open: `grid²` sub-queries per sprite per view (sm_hub_1: 309 × 16), one frame of latency.

**Visibility is the entity's.** `FElysiumEnvSprite` (`ElysiumEnvSprite.cpp`) restates `CSprite`:
`Spawn` sets `bOn` by the unnamed-or-Start-On rule; `HideSprite`/`TurnOff` clear it,
`ShowSprite`/`TurnOn` set it, `ToggleSprite` flips it; `ScriptHide`/`Kill` hide through the base
chain and `ScriptUnhide` restores the last `bOn`; every change publishes `bOn && !IsInert()` through
`IElysiumEmbodiment::SetBakedSpriteVisible(EntityIndex, bVisible)` → `AElysiumMapActor` →
`UElysiumMapVisuals::SetSpriteVisible`, which finds the actor by its `elysium.ent` tag (bucketed
once in `AdoptBakedLevel`) and sets it hidden in game. `bOn` is the leaf's one save field. A map
off `MapsOnV2Models` has no sprite actors: the write finds nothing and the input is still accepted.

**`<map>.sprites` retires.** `UE_bsp_to_scene.write_sprites` and its call are gone; the ledger
row moves to *retired*.

#### Verification (R6.1)

`pipeline/tests/test_bake_map_sprites.py` pins the row → actor mapping on a synthetic join and
fake sidecars (size from `scale × texture` with the 0..8 clamp and the zero-scale default, the
colour and alpha, the blend from the mode table with mode 6 failing, `parallel_upright`, the
spawn-off rule against `start_hidden`/name/spawnflag, the entity index, the sky flag), the
editor half's pure placement (`sprite_actor_values`: the sky transform, the child name), the
shared manifest version, and — corpus-gated — that every `env_sprite` block of the three maps
is a row with the hidden rule re-derived from its own keys (sm_hub_1 places **309** rows, 113 of
them mode 3). `bake_verify.verify_sprites` loads the level and matches every `elysium.sprite`
actor to its staged row by entity index (size, colour, mode, upright, hidden, the child's parent
and blend), no row missing and no actor extra. `Elysium.Substrate.EnvSprite` pins the leaf's
spawn rule and every input on the recording double, and `Elysium.Substrate.SpriteGlow` pins the
settings defaults, the glow size/brightness/smoothing formulas (`ElysiumSpriteGlow.h`, the pure
functions the proxy calls), the tag helpers and the actor's shape. Measured numbers are in the
R6.1 Settled entry of `docs/project/seam_migration.md`.

### 3D-skybox composition (R6.7)

The miniature is one render in VtMB — `Draw3dSkyboxworld` walks one BSP area's leaves through the
same client leaf system the main scene uses, under one view transform and one fog set
(`docs/vtmb/sky-ambience.md` → "The pass, step by step") — so it is one rule here, applied to
every content class the corpus places in that area, never a per-lane re-derivation:

| Fact | The one answer, for every lane |
|---|---|
| membership | `SkyScope.is_sky(p)`: `area(point_leaf(p)) == area(point_leaf(sky_camera.origin))`, computed once by the producer join and carried on every staged row as its `sky` flag (`placements[].sky`, `details.records[][10]`, `sprites[].sky`, `lights[].sky`, `cubemaps[].sky`) |
| transform | `world(v) = scale × (v − origin)`, uniform scale `scale`, no rotation — the inverse of the pass's view (`origin /= scale; origin += sky_origin`) |
| where the transform comes from | the staged manifest's own `sky` block (`scale`, `origin` in Unreal cm, `ok`), written by `map_geometry.stage_map` off the unit's join. The V2 lane reads **nothing else** for it: `MapBakeV2._read_sky` answers from `_StagedGeometry` (`miniature_transform`), and the legacy `<map>.sky` sidecar is never opened on a converted map. Same numbers by construction (both are `source_to_unreal(sky_camera.origin)` and the camera's `scale`), but a unit-authored map must not depend on a sidecar it no longer needs, and a map with a `sky_camera` but no sky *faces* has no `.sky` at all (`write_sky` gates on geometry) while it may still place a sky-flagged prop or sprite |
| fog | the `sky_camera`'s set (`skyfog*` of `.env`, distances already `× scale`) as the primitive's fog CPD (`ElysiumFog.h`), stamped by the bake on every miniature primitive that carries the term — sky chunks, sky props, sky detail components — and re-stamped by `UElysiumMapVisuals::ApplySceneFog` from `EnvDef`, so a `.env` re-export or the `elysium.Fog` A/B reaches them without a re-bake. A sky sprite carries no stamp: `M_V2_Sprite` has no fog term (the R6.1 follow-up stands; Source's sprite shader fogs a card — to the fog colour, or to black when additive — so the miniature's sprites are the one class drawn unfogged) |
| scope marker | `elysium.sky` is the miniature's tag. A sky chunk and a sky prop carry it *instead of* their class tag (R5.1, `AStaticMeshActor`s both, bucketed together as `SkyActors`); a detail component actor and a sprite actor carry it *beside* their class tag (`elysium.detail` + `elysium.model=` + `elysium.sky`; `elysium.sprite` + `elysium.ent=` + `elysium.sky`), because their class is what the runtime buckets by and the scope is a second fact about the same actor. `AdoptBakedLevel` therefore tests the class tags first and `elysium.sky` last, so a tagged detail or sprite actor never lands in the static-mesh sky bucket |
| what the runtime does with the marker | `ApplySceneFog` stamps a detail component with the sky set when its actor carries `elysium.sky`, the world set otherwise (before R6.7 the runtime stamped no detail component at all and the bake's default slot was the only writer); `ToggleSkybox` (`elysium.togglesky`) hides the miniature's details and sprites with its chunks and props, since the miniature reads as one thing; the boot line and the Cog Maps/Status rows count them (`n in the 3D skybox`) |
| the other classes | unchanged and already composed: a sky prop (R5.1 — never solid, no shadow, not in ray tracing, `Sky/Props`), a sky light (R5.6 — position and reach through the same transform, `Sky/Lights`), a sky capture (R5.5), a sky-scope brush entity (runtime, `RegisterRuntimeBrush(Comp, bSky)` off the `.ents` join's `sky` field, `RuntimeSkyBrushes` re-stamped with the sky set) |

The bake writes every miniature transform once; the runtime moves nothing. A sky sprite's actor
scale is the miniature's (R6.1), so a fixed-size card scales with the miniature and a mode-3 glow
stays screen-constant either way — the pass's own behaviour, since the glow's size is a function
of view distance and the miniature view is the player's view at `1/scale`.

#### Verification (R6.7)

`pipeline/tests/test_bake_map_sky_scope.py` pins, per lane, that a sky-flagged record takes the
miniature transform and the marker: a `sprites[]` row (`sprite_actor_values`: position `scale ×
(p − origin)`, actor scale `scale`, tags `elysium.sprite` / `elysium.ent=` / `elysium.sky`), a
`details.records[]` row (`detail_instance_rows` puts it in its own `(stem, sky)` component at the
transform; `detail_actor_tags` adds `elysium.sky`), and that the lane's transform is the manifest's
`sky` block (`miniature_transform`). `bake_verify.verify_sky_scope` loads the level of a
`MapsOnV2Models` map whose manifest says `sky.ok` and counts every class back against the staged
rows: sky props (`elysium.sky` static-mesh actors labelled `Prop_`, position and scale per row),
sky detail components (`elysium.detail` + `elysium.sky`, one per staged sky model group, instance
scale `scale`), sky sprites (`elysium.sprite` + `elysium.sky`, position and scale per row) — and
that every sky prop and sky detail component carries the same fog slots the sky chunks carry.
`Elysium.Substrate.SkyScope` pins the runtime half content-free: a level of one sky chunk, one
prop, a world and a sky detail actor and a sky sprite buckets by class first, `ApplySceneFog`
stamps the sky detail with the sky set and the world detail with the world set, and
`ToggleSkybox` hides the miniature's detail and sprite with its chunk. Measured numbers are in the
R6.7 Settled entry of `docs/project/seam_migration.md`.

### The per-map cutover flag

A second list on the R4.6 settings page, `UElysiumMapTransportSettings::MapsOnV2Models`, tracked in
`Config/DefaultElysium.ini` beside `MapsOnNewTransport`. A listed map is authored from its root unit
by `bake_map_v2` and resolves its prop meshes and skin table under `/ElysiumBaked/Meshes` (R1); an
unlisted map keeps the legacy `.obj`/`.props` bake and `/ElysiumBaked/Shared/Meshes`, byte for byte.

**It is deliberately not the same list as `MapsOnNewTransport`.** The two answer different questions
over different map sets: `sp_theatre` is on the entity/collision/environment transport because those
assets exist for it, but the R1 model import is map-scoped and has staged only the three-map working
corpus, so pointing `sp_theatre`'s prop resolver at the V2 root would resolve nothing. A map joins
`MapsOnV2Models` when its models have been imported **and** its level re-baked on this lane — a
different event from its entity assets landing.

**The flip is one accessor.** `FElysiumContentPaths::BakedMeshesFor(Map)` returns `BakedMeshes()`
(`/ElysiumBaked/Meshes`) for a listed map and `BakedSharedMeshes()` otherwise, and `BakedPropMesh`,
`BakedItemMesh` and `BakedPropSkins` all compose from it. Each now **requires** a map argument, so no
call site can silently land on the legacy root for a map that has been cut over. The four substrate
sites that recompute the stem live (`ElysiumItemContainer`, `ElysiumItemClasses`, `ElysiumLockable`,
`ElysiumTerminal`) are untouched: they produce a stem and hand it to `UElysiumEntityBodies`, which
knows the map.

**Travel's gate follows the same flag.** `UElysiumMapSubsystem::HasTravelableExport` still accepts
the R2.4 `<map>.ready` marker for every map, but accepts the legacy `<map>.obj` **only** for a map
not on `MapsOnV2Models`: once a map is cut over nothing reads its `.obj`, so a stale one left on disk
must not vouch for the sidecars beside it. The `.obj` branch is not retired outright — 105 maps are
still on the legacy lane and only the three converted maps carry a `.ready` marker today — and R8.1
owns its deletion, as R2.4 said it would.

### Verification

The reader is checked against the legacy exporter's own output on the three working maps, which is
the strongest available witness that "producer changed" and "geometry changed" are separable:
`pipeline/tests/test_map_geometry.py` pins the transform algebra and the placement mapping, and the
corpus comparison below was run against `$ELYSIUM_EXPORT_ROOT/<map>/`.

| Map | World verts / tris / groups | Sky verts / tris | Brush models | Placements | Max abs dpos | Max abs duv |
|---|---|---|---:|---:|---:|---:|
| `sp_tutorial_1` | 38,971 / 24,799 / 276 | 5,997 / 3,573 | 73 | 809 | 0.0008 cm | 1.2e-5 |
| `sm_pawnshop_1` | 15,507 / 9,177 / 132 | 930 / 488 | 28 | 194 | 0.0004 cm | 7.2e-5 |
| `sm_hub_1` | 41,901 / 24,434 / 278 | 1,037 / 539 | 55 | 1,043 | 0.0008 cm | 3.0e-5 |

Every count is **identical** to the legacy `.obj`/`.props` on all three maps — vertex count, group
count, per-group triangle count, brush-model set and every placement's stem, position, rotation,
`solid`, `skin` and 3D-skybox flag. The residual positional and UV deltas are the unit's binary32
`POSITION` accessor against the legacy OBJ's four printed decimals: 8 microns at worst, which is the
same seam-precision limit R3.2 measured on `.dispcol` and not a difference in the geometry.

## Import — materials (R5.4)

R5.4 of `docs/project/seam_migration.md` → "Roadmap — one pipeline" moves a converted map's
**surface materials** off the legacy `<map>.mtl` table and the per-map material packages the legacy
bake authored from it, and onto the `MI_` instances the material lane already imported for every
`vtmb:material:*` unit (`seam_map_material.md` → "## Import"). R5.1 left the geometry lane binding
*"the same `MaterialInstanceConstant` the legacy surface bound, resolved through the same `<map>.mtl`
table, so a shot-diff sees geometry, not shading"*; this task is the shading half. Lights are still
R5.6's; reflection captures R5.5's.

### Identity and resolution

```text
face -> textures[texinfo.texData].asset          vtmb:material:<key>   (the root unit, unchanged)
     -> maps/<map>/<dir>/<stem>[_x_y_z]           a PAKFILE-patched face keeps its map-scoped id
  -> $ELYSIUM_WORK_ROOT/import/materials/<key>.provenance.json     the material lane's own sidecar
  -> /ElysiumBaked/Materials/<dir>/MI_<safe stem>                   importers.materials.asset_path_for
  -> /ElysiumBaked/Materials/maps/<map>/<dir>/MI_<safe stem>        (patched: parented to its base MI_)
```

The map root unit already names every face's material unit — `textures[]` joins each TEXDATA row to
its `vtmb:material:*` id, and a VBSP-patched face names the PAKFILE unit (`seam_map_map.md` →
"Dependencies": *"patched names resolve to their pakfile unit"*). The R5.1 reader keeps its group
key (`<material>` or `<material>@<cubemap>`, the legacy `.mtl`'s own split) so every mesh slot name
and every chunk is unchanged, and now records, per group, the unit that group's faces resolved
(`Scene.units`, `MapGeometry.material_units`). The offline stage resolves each unit through the
material lane's **provenance sidecar** — not its `manifest.json`, which describes that lane's *last*
run and shrinks to one directory under `--select`, while every staged unit's sidecar stays on disk
until its own scope prunes it — to the asset path (`asset_path_for`, the same pure function that
named the asset at import), the root master (the sidecar's `master`, already walked to the base for
a patched unit) and the root blend mode (the base's `blendMode`, reached through `patchBase`). That
tuple is the `materials` table in the staged manifest (`MANIFEST_VERSION` 2), one row per face
group; the editor half (`bake_map_v2.MapBakeV2`) loads exactly those assets and `material_for`
answers from that table alone.

**The Nanite question moves with the binding.** The legacy chunker split a cell into a Nanite mesh
(opaque + masked) and a non-Nanite sibling (`T_` prefix) by `MatDef.opaque`, read off the corpus
flags. The V2 lane answers the same question from the root instance's blend mode
(`NANITE_BLEND_MODES = {Opaque, Masked}`) **gated on the bound V2 master's own Nanite capability**
(`NANITE_CAPABLE_MASTERS = {M_V2_Lit, M_V2_LitTranslucent, M_V2_Unlit, M_V2_TwoTexture}`, review
fix): `make_v2_materials.py` deliberately does not set `used_with_nanite` on `M_V2_Water`,
`M_V2_Refract`, `M_V2_Sprite`, `M_V2_Decal` or `M_V2_Eyes`, so an instance's `blendMode` override
alone (`water/sewer_water` and `maps/sm_hub_1/dev/dev_waterbeneath2` are lane-forced `Opaque` on
`M_V2_Water`) cannot make that master's chunk drawable — Unreal falls back silently to the default
material at render time (`LogMaterial: Warning: ... missing usage flag Nanite!`) rather than
failing the bake, which is why the material-slot audit (a load-time check) never saw it. Bar that
one gate, the chunk naming, the section order and the slot names are byte-for-byte the R5.1 lane's
— only the bound asset changes.

**A unit the material lane has not staged fails the map**, naming every missing key and the command
that stages it (`uv run elysium import materials`); a patched unit whose `patchBase` chain leaves
the staging tree fails the same way. An instance the stage resolved but the editor cannot load is a
second loud failure (`resolve_materials`), because the material lane imports map-scoped and the map
it did not import is exactly the map this would silently unbind. Nothing on this lane binds the
master's placeholder quietly.

### What stops, and what does not

- **The per-map world material package stops.** `_material_sets` on the V2 lane returns an empty
  world set, so `stage_materials` prunes `/ElysiumBaked/<map>/Materials` instead of authoring it,
  and the legacy per-map wet-cubemap import (`sm_hub_1`'s `TC_cubemapdefault` stamped into
  per-map wet instances as `SourceCube`) is pruned too: wetness rides `MPC_ElysiumEnvironment` and
  the shared instance's own `WetnessScale`/`WetnessDriven` (R5.3), never a per-map copy. The
  cubemap-patched materials — the ~7,487 corpus-wide that R2's sweep found falling back to generic
  reflection — bind their own map-scoped `MI_` (a parented instance of the base, overriding nothing
  today; the probe join is R5.5's).
- **The decal package does not stop here.** `_place_decals` still binds the legacy per-map
  `M_Decal` MIC from `/ElysiumBaked/<map>/Materials/Decals`, and the V2 lane still reads the
  `.mtl` for exactly those rows. The reason is a domain fact, recorded in `seam_map_material.md` →
  "Scene fog on the world masters (R5.4)": a `UDecalComponent` renders only an
  `MD_DeferredDecal`-domain material, every V2 master is `MD_Surface`, and the `.decals` materials
  are `$decal` surfaces on `M_V2_LitTranslucent`/`M_V2_Unlit`, not `decalmodulate` units. R7.6
  owns the decal rebind and the legacy `M_Decal` retirement.
- **Props were already there.** An R1 prop mesh under `/ElysiumBaked/Meshes` binds its V2 `MI_`
  from its own import; this task changes nothing about props except that their fog now works
  (below).

### Scene fog is now a V2 term

The rebind would have shipped every world surface unfogged: no V2 master carried the per-primitive
distance-fog term `ElysiumFog.h` names as the contract between the material graph, the bake and
`UElysiumMapVisuals::ApplySceneFog`. R5.4 wires `mat_fog.fog_from_primitive` — the legacy graph's
own term — into the five masters a map surface or prop slot binds, reading the exact CPD slots
`ElysiumFog::Pack` writes and the bake's `set_fog` stamps (colour 0..3, start 4, 1/range 5), with
one instance-side gate (`FogInscatter`, 0 on an `Additive` blend because Source fogs additive
surfaces to black). The bake-side stamping (`stage_level`'s `set_fog` on every world/sky/prop
component) and the runtime's re-stamp are unchanged; the ruling and the parameter rows are in
`seam_map_material.md` → "Scene fog on the world masters (R5.4)".

### The provenance report

Beside the staged pair, `materials_report.json` classifies every material the map binds — as data,
never as a look judgement: its V2 master, blend mode and appearance class (`opaque`, `masked`,
`translucent`, `additive`, `refract`, `water`, `decal`); the legacy master the `.mtl` lane would
have selected for the same base material (`Bake._master_for`'s own rule order, restated over the
corpus record) and *its* class; whether the class changed on the rebind; the unit's proxies and
which of them run live on the V2 instance (`sine`, `texturescroll`, and `animatedtexture` unless
the provenance's `animatedFramesArrayUnavailable` omission says its frames array never staged);
whether it is wetness-driven (`wetnessScale` authored on a Lit-family unit); and whether it is a
`$decal` surface. `uv run elysium export map <map>` prints the counts as it stages.

### Measured (2026-09-02, the three working maps)

Staged from the material lane's sidecars in 2.2 / 0.7 / 2.0 s per map, and every unit resolved:

| Map | Face groups bound | PAKFILE-patched | By master | Animated now | Class changed | Wetness-driven |
|---|---:|---:|---|---:|---:|---:|
| `sp_tutorial_1` | 419 | 241 | Lit 384, LitTranslucent 29, TwoTexture 5, Unlit 1 | 0 | 0 | 0 |
| `sm_pawnshop_1` | 160 | 66 | Lit 137, LitTranslucent 16, Unlit 6, TwoTexture 1 | 2 | 3 | 9 |
| `sm_hub_1` | 323 | 134 | Lit 302, LitTranslucent 14, Unlit 4, Water 2, TwoTexture 1 | 2 | 6 | 14 |

**Animated now** (proxies the legacy `.mtl` lane flattened, live on the V2 instance):
`sm_pawnshop_1` — `dev/dev_tvmonitor1a` (`sine`, TwoTexture), `signs/newsticker` (`texturescroll`,
Unlit); `sm_hub_1` — `dev/dev_waterbeneath2@cubemapdefault` and `water/sewer_water`
(`animatedtexture` + `texturescroll`, both on `M_V2_Water`). Four of the corpus's ~217, because the
three maps bind 902 face groups of the 19,121 units; the count scales with the flag list.

**Appearance class changed** — every one a `$decal` world face (`decals/signs/number{0,5,8}` on
`sm_pawnshop_1`; `decals/stains/bloodbg{a,b}`, `decals/stains/blooddrip{c,d}` on `sm_hub_1`) that
the legacy lane bound to the deferred-decal-domain `M_Decal` **as a static-mesh slot** (which a
surface mesh cannot draw) and that is now an ordinary `M_V2_LitTranslucent` translucent surface,
plus the two `sm_hub_1` water surfaces, legacy `M_World_Translucent` → `M_V2_Water` with the
material lane's own `Opaque` blend override (`water` VMTs author no `$translucent`; R7.2's).
Because the Nanite split now follows the root instance's blend, the seven decal/glass-flagged
surfaces left the Nanite buckets; `sp_tutorial_1` 110 and `sm_pawnshop_1` 38 are unchanged.

**Review fix, re-measured (2026-09-02).** The first landing answered the Nanite question from
`blendMode` alone, so the lane's `Opaque` override on `water/sewer_water` and
`dev/dev_waterbeneath2` (both `M_V2_Water`, a master built without `used_with_nanite`) put them in
a Nanite chunk too — `sm_hub_1` chunks 194 → 175 (158 Nanite + 17 `T_`) and a post-boot log entry
per surface: `LogMaterial: Warning: Material .../MI_sewer_water missing usage flag Nanite! Default
Material will be used in game.` — UE's fallback default material in place of the water surface,
not a bake failure, so nothing upstream of a boot log caught it. `MaterialBinding.opaque` now
gates on `NANITE_CAPABLE_MASTERS` as well as blend; rebaking `sm_hub_1` (scoped, no `--force`)
restores `world: 194 chunk meshes` (158 Nanite + 36 `T_`, the extra 19 `T_` chunks over the
pre-R5.4 legacy split from the seven now-legitimately-reclassified decal/glass surfaces sharing
cells with the two water ones), and the rebuilt boot log carries zero `missing usage flag Nanite`
lines.

**The instances were behind the masters.** The staged sidecars and the imported `MI_` predated
R5.3 (Aug 31 09:02): none of `sm_hub_1`'s 14 `globalwetness` units carried `WetnessScale`/
`WetnessDriven`. Re-staged and re-imported by directory, never the corpus — `asphalt` 1 imported /
15 reused, `concrete` 4 / 157, `grass` 1 / 2, `ground` 7 / 91, `tile` 1 / 136, `dev` 1 / 89 (+ the
2 pre-existing `dev/ocean*` stage failures the material doc already names), `signs` 1 / 65 — 16
instances rewritten, exactly the 14 wet units plus the 2 `Additive` ones (`FogInscatter = 0`).

**Bake** (`uv run elysium export map sp_tutorial_1 sm_pawnshop_1 sm_hub_1`, no `--force`; the
recipes changed): 185 / 70 / 234 assets saved, 0 failed, all three levels; `/ElysiumBaked/<map>/
Materials` holds only `Decals/` (27 / 14 / 61 legacy `M_Decal` MICs) and `Textures/Cubes` is empty on
all three. A byte scan of every baked world chunk and brush mesh finds **1,399 / 361 / 1,439**
references into `/ElysiumBaked/Materials/…` (base and `maps/<map>/…` alike) and **0** into
`/ElysiumBaked/<map>/Materials/` or `/ElysiumBaked/Shared/Materials/`.

**Headless boot, 6 + 4 + 4 = 14/14 vantages**, material audit `0 with unbound or default-bound
slots` on 464 / 179 / 483 mesh assets, `ApplySceneFog` stamping 885+32 / 211+25 / 1,130+53
primitives (world + miniature; `sp_tutorial_1` authors no fog, the other two `1270->12700cm`).
Pixel numbers, as data only: against the pre-R5.4 captures of the same build, `sm_pawnshop_1`
mean |Δ| 47–71 / p99 77–191 / 96.5–99.9 % of pixels, `sm_hub_1` 42–44 / 119–156 / 97.7–98.1 %;
frame mean RGB rose from (95–197, 107–175, 87–146) to (141–237, 147–218, 127–195) on
`sm_pawnshop_1` and from (46, 31, 14) to (85, 62, 33) on `sm_hub_1` — every surface changed
shading model, so the harness's 94 % noise floor (R5.1) is not the ceiling here and no regression
verdict is drawn from it; the boot, the audit and the byte scan are the witnesses. One harness
observation for the follow-up list: `sm_hub_1`'s four vantages report identical frame means
before and after, which they did before this task too.

## Import — reflection captures (R5.5)

R5.5 of `docs/project/seam_migration.md` → "Roadmap — one pipeline" [MP-4.4, SF-6.2] gives a
converted map its **reflection captures**: one `ASphereReflectionCapture` per `cubemaps[]` row of
the map root unit, built inside the bake, and the light rig's specular response flipped off the
legacy zero. Nothing here binds a texture to a material: the reflection contract
(`seam_map_material.md` → "Import", "`env_cubemap` is not a runtime bind") stands — the 2,217
`env_cubemap` symbols and the 441 map-scoped patched `MI_` on the three working maps get their image
from these captures and from Lumen, never from the VtMB probe pixels, which stay provenance.

**A Lumen fallback lane, not the reflection.** Under Lumen the captures are what a surface reads
when Lumen reflections have nothing better — the rough end of the roughness range, ray misses, the
Lumen-off scalability tier — and a capture of the Lumen-lit baked scene is the intended image
(`seam_migration.md` → the maps-plan Settled entry: "reflection captures build headlessly under
`-AllowCommandletRendering` and remain a Lumen *fallback* lane"). Where VtMB placed an
`env_cubemap` is exactly where its artists wanted a local reflection sampled from, which is the one
fact the sample rows carry that a runtime cannot invent; the size field is `0` on all 57 samples of
the working corpus (Source's default resolution) and is not read.

### Identity and placement

| Fact | Source | Rule |
|---|---|---|
| Count and order | `cubemaps[]` (lump 42), one row each, lump order | one `ASphereReflectionCapture` per row; label `Capture_<index>`, tags `elysium.capture` + `elysium.src=<index>`, Outliner folder `Captures` |
| Position | the row's node `translation` (glTF metres) through `gltf_position_to_unreal`, the same frame every placement takes (`### The frame` above); the row's integer `origin` is the Source-inch triple the probe file name is built from and is carried for provenance only | `(x, z, y)_gltf × 100` |
| Radius | `UElysiumSurfaceSettings::CaptureRadius` (Project Settings → Elysium → Surfaces, `Config/DefaultElysium.ini`, 1,500 cm) | `InfluenceRadius`, one global knob, no per-sample override — the sample row carries no radius to transcribe |
| 3D-skybox membership | `SkyScope.is_sky(source_position(translation))`, the area rule every content class shares | a miniature sample takes the sky transform like a miniature light: position `scale × (p − sky_origin)`, radius `× scale`, folder `Sky/Captures`; 0 of 57 on the working corpus, kept as a rule so a map that authors one is not silently mis-scaled |
| Mobility / source | Unreal defaults | `Static`, `CapturedScene`; `Brightness` 1 |

The offline stage (`importers.map_geometry.stage_map`) writes the rows into the staged manifest as
`cubemaps[]` (`index`, `origin`, `position` in Unreal cm, `sky`), manifest **version 3**, and
`bake_map_v2.MapBakeV2._place_captures` spawns from that table — same split as geometry and
placements: the numpy-bound read offline, the editor spawn from the staged pair. The legacy lane
places no capture (`Bake._place_captures` returns 0): it is not on the corpus and R5 does not
extend it.

### Build

The capture contents are rendered by the bake itself, not left to the editor's Build menu:
`stage_level` populates the level, then calls
`UElysiumMapBakeLibrary::BuildReflectionCaptures(World)` — a thin `UFUNCTION` face over
`UEditorEngine::BuildReflectionCaptures`, the same call the editor's *Build → Reflection Captures*
makes (waits for every pending shader and asset compile so no capture sees the default material,
refreshes the sky captures, then `UReflectionCaptureComponent::UpdateReflectionCaptureContents`) —
and only then saves. The commandlet already runs under `-AllowCommandletRendering` (`unreal.bake_maps`),
which is the precondition; the sky light the R5.2 lane places (`TC_Sky_<name>`, `SLS_SpecifiedCubemap`)
is refreshed first by that same engine call so the sky is in the capture.

Where the data lands: `ULevel::MapBuildData`, the `UMapBuildDataRegistry` in the sibling package
`/ElysiumBaked/<map>/<map>_BuiltData`, one `FReflectionCaptureMapBuildData` per component
`MapBuildDataId` (`CubemapSize`, `AverageBrightness`, the full-HDR cube bytes). `UWorld::Rename`
carries the registry through `save_map`'s untitled-to-final rename and `FEditorFileUtils::SaveWorld`
saves the `_BuiltData` package beside the level, so one save writes both. A packaged or `-game` boot
loads the registry with the level (a hard `UPROPERTY` reference) and uploads the cubes at
registration; nothing at runtime re-captures.

**The bake asserts the build.** `BuildReflectionCaptures` returns the number of capture components
whose `MapBuildDataId` resolves to a registry entry with `CubemapSize > 0` and captured bytes;
`MapBakeV2._build_captures` fails the map when that number is short of the placed count, because a
capture that placed but never rendered is a black probe the running game would read as "no
reflection here" with nobody saying why. `bake_verify.py` re-counts on the loaded level through the
same library call (`CountBuiltReflectionCaptures`), and the Content tier's
`Elysium.Content.MapBake.ReflectionCapturesBuilt` loads each `MapsOnV2Models` level package and
checks every placed capture has its registry entry.

**Recipe.** The level recipe names the capture table (index, position, radius) and the radius knob,
so a `CaptureRadius` edit or a re-export that moves a sample re-authors the level rather than
reusing it.

### `LightSpecularScale` rides along

The legacy bake wrote `specular_scale = 0` onto every light and `UElysiumLightRig` re-applied a
`SpecularScale = 0` at adopt ("VtMB world is pure Lambert") — the third of the three zeroes the
owner repudiated (`seam_migration.md` → "The matte-world premise is repudiated"; the surface
two, Specular 0 / Roughness 1, already flipped with the V2 masters). R5.5 flips the light one:

- **One knob.** `UElysiumSurfaceSettings::LightSpecularScale` (default **1.0**, the ini) is the
  light rig's specular scale — "one global knob, no per-map override" (the owner's knob-set answer,
  2026-08-31; `seam_map_material.md` → "Knob contract"). `UElysiumLightingSettings::SpecularScale`,
  the R4.3 carry-over of the same number at the old zero, is **deleted** rather than left as a
  second writer; `UElysiumLightRig::ApplySettings(const UElysiumLightingSettings&, const
  UElysiumSurfaceSettings&)` copies it into the rig's `SpecularScale` mirror and `ApplyToSource`
  writes it on every non-overridden light as before.
- **Live.** A terminal edit of `LightSpecularScale` on the Surfaces page pushes through
  `UElysiumLightingSettings::PushToWorlds` to every live rig (the interactive drag does not, for
  the same 400-light reason the Lighting page never did).
- **The bake writes the same value.** `Bake._place_lights` stamps `specular_scale` from the
  settings CDO rather than a `SPECULAR_SCALE` literal, so the level in the editor and the adopted
  level in the game agree; the rig re-derives at load regardless.
- **No look judgement.** 1.0 is Unreal's own neutral, the value at which the surface knobs
  (`DefaultSpecular`, the mask term, the class table) are what decide a highlight; the owner's
  tuning session moves it on the page, never here. The Cog Environment window's local-light
  specular slider stays what it was — a per-session view onto the live rig.

## Import — effects (R7.3)

R7.3 of `docs/project/seam_migration.md` → "Roadmap — one pipeline" places a converted map's
**effects entities** as actors in the baked level: every `env_particle` / `func_particle` on the
slotted generic floor `NS_ElysiumParticle` (or its family override), every `func_dustmotes`,
`env_steam` and `env_beam` on its own authored family system. The owner's rulings (2026-09-02,
`effects-architecture.md` §5): **A1** — stage off the V2 particle unit and retire the strict
compiler for converted maps; **B3** — a generic floor with family overrides; **C1** — one actor per
row, R6.1's sprite pattern; **P1** and the explosion bundle follow the ambient set. The corpus:
`env_particle` 1,304 rows over 155 placed roots, `func_particle` 98, `func_dustmotes` 82,
`env_steam` 11, `env_beam` 47 (108 maps). Nothing here is a look judgement; the projection rules
are `seam_map_particle.md` → "Semantics".

### Identity and naming

```text
vtmb:map-entities:<map>  entities[i]  (env_particle | func_particle | func_dustmotes | env_steam | env_beam)
  + vtmb:particle:<root> and its closure            (the particle lane, seam_map_particle.md)
  + vtmb:image:particles/<sprite>.tga               -> /ElysiumBaked/Textures/particles/T_<safe stem>   (the texture lane)
  + vtmb:material:sprites/beama                     -> /ElysiumBaked/Materials/sprites/MI_beama        (the material lane)
  + models[N]                                       (the root unit; brush bounds for func_particle / func_dustmotes)
  -> $ELYSIUM_WORK_ROOT/import/map_geometry/<map>/manifest.json   effects[], particleTrees{}, dustmotes[], steam[], beams[], effectStats{}
  -> /ElysiumBaked/<map>/<map>.umap                                one actor per row
```

| Row | Actor | Label | Folder | Tags |
|---|---|---|---|---|
| `effects[]` | `AElysiumEffectActor` | `Effect_<index>_<root stem>` | `Effects` / `Sky/Effects` | `elysium.effect` + `elysium.ent=<index>` (+ `elysium.sky`) |
| `dustmotes[]` | `AElysiumDustActor` | `Dust_<index>` | `Effects` | `elysium.effect` + `elysium.ent=<index>` |
| `steam[]` | `AElysiumSteamActor` | `Steam_<index>` | `Effects` | `elysium.effect` + `elysium.ent=<index>` |
| `beams[]` | `AElysiumBeamActor` | `Beam_<index>` | `Effects` | `elysium.effect` + `elysium.ent=<index>` |

`index` is the entity's lump ordinal = `FElysiumEntityHandle::Index`, the same number the sprite
actors carry (`ElysiumBakedTags::EntityIndex`); the runtime buckets `elysium.effect` actors by
C++ class in `AdoptBakedLevel` and finds one by its entity index exactly as `SetSpriteVisible`
does. The particle sprites come off the texture lane: `particles/*.tga` (318) are admitted as
texture units (`seam_map_texture.md` → "Texture unit"; `<dir>` = `particles`), so a leaf's sprite
is `/ElysiumBaked/Textures/particles/T_<safe stem>` and the legacy PNG derivative retires with the
legacy lane.

### What the stage publishes

The offline stage (`importers.map_geometry.stage_map`, manifest **version 7**) joins each effects
entity row of the entities unit to its references and publishes five tables. Every field has a
name, a unit and a source; a consumer reads these tables and never the install.

#### `effects[]` — one row per `env_particle` / `func_particle`

| Field | Unit | Source |
|---|---|---|
| `index` | int | `entities[i].index` (lump ordinal) |
| `classname` | `env_particle` \| `func_particle` | `entities[i].classname` |
| `targetname` | string \| null | key `targetname` |
| `origin_cm` | `[x, y, z]` Unreal cm | `entities[i].origin.gltf` → `gltf_position_to_unreal` (the placements' frame); a `func_particle` without `origin` (46 of 98) is `[0, 0, 0]`, the brush frame's own origin |
| `rotation` | `[qx, qy, qz, qw]` Unreal | `entities[i].angles.gltf` → `gltf_quat_to_unreal`; **the emitter basis** (particle X → actor forward, Y → up, Z → right) |
| `angles_deg` | `[pitch, yaw, roll]` Source degrees | key `angles` as authored, provenance |
| `attach_type` | int | key `attach_type` (`m_nAttachType`), default 0; `-1` carried as authored (the runtime's switch default → origin); **`func_particle` is always 15** (`CFuncParticle::Activate` forces it, whatever the key says) |
| `parentname` | string \| null | key `parentname` |
| `bone` | string \| null | key `bone` (`m_sAttachName`) |
| `attach_point` | int | key `attach_point` (`m_nAttachPoint`, `+0x460`), default 0; authored on 0 rows |
| `active` | bool | key `active`, default 1 (the constructor's); 274 rows author 0 |
| `start_hidden` | bool | key `StartHidden`, the base-class hidden state |
| `spawnbounds_cm` | float cm | key **`spawnbounds`** (`m_fSpawnBounds` `+0x49c`), default 512 in = **1300.48 cm**; authored on 228 rows, all 512. The FGD's `bounds` (1,110 rows: 512 ×1,108, 256, 128) is **not a keyfield on `CEnvParticle`** and is carried nowhere — `weather.md`'s "512 and 256" on `sm_hub_1` both run at the default |
| `ramp_scale` | float | key `ramp_scale` (`m_fRateScaleTarget` `+0x48c`), default 1 |
| `ramp_time` | float s | key `ramp_time` (`+0x490`), default 0 |
| `bounds_cm` | `{min: [..], max: [..]}` Unreal cm \| null | `func_particle` only: the world AABB of `models[N]` (`entities[i].model.index`, the root unit's `models[]` bounds) through the placements' frame |
| `volume_scale` | float | `func_particle` only: `clamp(|dx|·|dy|·|dz| × 2⁻²¹, 0.01, 100)` over the Source-unit extents (a 128-unit cube = 1) — `CFuncParticle::Activate`'s `sizeScalar`, the one the client multiplies into the rate float |
| `particle` | `vtmb:particle:<key>` \| null | the `references[]` row of `particle_definition` (role `particle`, folded key, spaces kept); null with `unresolved: true` when the install lacks it — VtMB's `Activate` removes the entity, the bake places no actor, `effectStats.unresolvedRoots[]` names it |
| `sky` | bool | `SkyScope.is_sky(origin)`, the R6.7 rule; a sky row takes the miniature transform a sprite takes |
| `spawnflags` | int | key `spawnflags`, provenance only — the class reads no spawnflag |

The `m_fRed/Green/Blue/MaskScale` and `m_fSizeScale` fields (`+0x464`…`+0x474`) have **no
keyfield name** in the datamap: a map cannot author them, they default to 1, and code producers
(the impact spawn's constant 0.8) write them. They are actor pins (`effects-architecture.md` §5),
not staged fields.

#### `particleTrees{}` — one entry per root id referenced by `effects[]`

Keyed by the root's `vtmb:particle:<key>`. **The closure is kept as a tree**, never flattened:
node 0 is the root, every other node is one definition reached through one block of its parent,
and a definition reached through two blocks is two nodes (the block's keys differ). Ramps are
keyframe lists `[[t, lo, hi], …]` in normalized age (`seam_map_particle.md` → "Ramps"); a scalar
is `[[0, v, v]]`.

| Field | Unit | Source |
|---|---|---|
| `root` | `vtmb:particle:<key>` | the entry's key; `name` as the entity spelled it |
| `nodes[].index` | int | tree order, root first, parents before children |
| `nodes[].id`, `.name` | `vtmb:particle:<key>`, string | the unit's identity |
| `nodes[].kind` | `root` \| `spawn` \| `leaf` \| `both` | node 0 is `root`; otherwise the unit's `role`: `emitter` → `spawn`, `drawing` → `leaf`, `both` → `both` |
| `nodes[].draws`, `.spawns` | bool, bool | the unit's `role` (a root that draws has `draws: true`) |
| `nodes[].parent` | int \| null | the parent node |
| `nodes[].via` | `spawn` \| `collide` | reached through the parent's `spawn {}` (spawns at the parent particle / the emitter) or its `collide { spawn {} }` (spawns at the impact) |
| `nodes[].blockIndex` | int | the parent unit's `blocks[]` index the node was reached through |
| `nodes[].depth` | int | 0 for the root |
| `nodes[].resolved` | bool | false when the install lacks the definition (three references in the placed corpus: `d_animalism_pestilence_cast_emitter`, `smoke3`, a `{` typo) |
| `nodes[].fps` | float | key `fps`, default 30 |
| `nodes[].lifetime_s`, `.lifetime_min_s`, `.lifetime_max_s` | s | `frames / fps`, `min_frames / fps`, `max_frames / fps` (defaults `frames` = `fps`, min/max = `frames`); `timescale` on the reaching spawn block already divided in |
| `nodes[].loop` | bool | key `loop` |
| `nodes[].size_cm` | ramp, cm | key `size` × 2.54 |
| `nodes[].width`, `.height` | ramp | keys `width`, `height` (dimensionless) |
| `nodes[].rotation_deg` | ramp, deg | key `rotation` (shortest arc) |
| `nodes[].red`, `.green`, `.blue`, `.color`, `.mask` | ramp, 0..1 | the five colour keys `/255` |
| `nodes[].refract` | ramp | key `refract` |
| `nodes[].radius_speed_cm_s` | ramp, cm/s | key `radius_speed` × 2.54 |
| `nodes[].theta_speed_deg_s`, `.phi_speed_deg_s` | ramp, deg/s | keys `theta_speed`, `phi_speed` |
| `nodes[].x_speed_cm_s`, `.y_speed_cm_s`, `.z_speed_cm_s` | ramp, cm/s in the emitter basis | keys `x_speed`, `y_speed`, `z_speed` × 2.54 |
| `nodes[].elevation_speed_cm_s` | ramp, cm/s world up | key `elevation_speed` × 2.54 |
| `nodes[].parent_speed` | ramp | key `parent_speed`, default 1 |
| `nodes[].spawn` | object \| null | the reaching spawn block's keys (null on the root): `rate` (ramp, /s), `burst` (ramp, count; first keyframe may sit at `t ≠ 0`), `distance` (bool), `radius_cm`, `theta_deg`, `phi_deg`, `x_cm`, `y_cm`, `z_cm`, `elevation_cm`, `rotation_deg`, `width`, `height`, `size`, `red`, `green`, `blue`, `color`, `mask`, `refract` (ramps, the units above), `timescale` (scalar, default 1) |
| `nodes[].movealign`, `.flat`, `.sortfront`, `.no_z_test`, `.lighting`, `.precipitation` | bool ×6 | the flags |
| `nodes[].depth_offset_cm` | float cm | key `depth_offset` × 2.54 |
| `nodes[].surface_color_optout` | bool | `surface_color 0` \| `use_surface_color 0` \| `ignore_surface_color 1` |
| `nodes[].sprite` | `{id, texture, size_px, aspect}` \| null | `vtmb:image:particles/<sprite>.tga`; `texture` the texture lane's `T_` path; `size_px` `[w, h]` off the texture lane's sidecar; `aspect` `[0.5·w/max, 0.5·h/max]` |
| `nodes[].normal` | same shape \| null | key `normal`, the DUDV sprite |
| `nodes[].collide` | object \| null | `{bounce, friction, gravity, drag, self, nested, spawn: [node index…], decals: [{id, texture, angle_spread}], vdecal: {first, last, angle_spread} \| null}` — defaults 1, 1, 0, 1, false; `nested` true when the four scalars were read off a nested `spawn {}` (`raindrops2`) |
| `stats.leafCount`, `.depth`, `.maxKeyframes` | int | the tree's drawing nodes, its depth, the longest ramp (the corpus maximum is 5 — the floor's slot count is checked against this histogram) |

#### `dustmotes[]` — one row per `func_dustmotes` (Valve `C_Func_Dust`)

| Field | Unit | Source |
|---|---|---|
| `index`, `targetname`, `sky` | | as `effects[]` |
| `model` | int | `entities[i].model.index` (`*N`); the brush solid's convex set is the entity row's own `hulls` (`DA_<map>_Entities`, entity-local cm) — the actor samples inside them by entity index, so the row carries no vertices |
| `bounds_cm` | `{min, max}` Unreal cm | `models[N]` world AABB |
| `spawn_rate` | motes/s | key `SpawnRate` (10 ×48, 20 ×21, 30 ×7, 40 ×5, 60 ×1) |
| `color` | `[r, g, b]` 0..1 | key `Color` `/255` (`205 201 182` ×64, `203 202 217` ×8, …) |
| `alpha` | 0..1 | key `Alpha` `/255` (100 ×63, 90 ×8, …) |
| `speed_max_cm_s` | cm/s | key `SpeedMax` × 2.54 (2 ×64, 4, 8, 13) |
| `size_min_cm`, `size_max_cm` | cm | keys `SizeMin`, `SizeMax` × 2.54 (7–12 ×64, 5–15 ×8, …) |
| `lifetime_min_s`, `lifetime_max_s` | s | keys `LifetimeMin`, `LifetimeMax` (3 / 5 on all 82) |
| `dist_max_cm` | cm | key `DistMax` × 2.54 (1024 ×77, 512 ×5) |
| `frozen` | bool | key `Frozen` (0 on all 82) |
| `start_disabled` | bool | key `StartDisabled` |
| `sprite` | `vtmb:material:particle/sparkles` | key `SpriteName` (`particle/sparkles` and the `materials/…vmt` spelling, one key) — provenance; the family system owns its look |

#### `steam[]` — one row per `env_steam` (Valve `CSteamJet`)

| Field | Unit | Source |
|---|---|---|
| `index`, `targetname`, `origin_cm`, `rotation`, `angles_deg`, `sky` | | as `effects[]`; the jet fires along the actor's forward |
| `type` | 0 normal \| 1 heatwave | key `type` (0 on all 11) |
| `initial_state` | bool | key `InitialState` (1 ×10, 0 ×1) |
| `spread_speed_cm_s` | cm/s | key `SpreadSpeed` × 2.54 (4, 15, 12) |
| `speed_cm_s` | cm/s | key `Speed` × 2.54 (30, 120, 160) |
| `start_size_cm`, `end_size_cm` | cm | keys `StartSize`, `EndSize` × 2.54 (10→10, 5→10, 8→20) |
| `rate` | particles/s | key `Rate` (26, 35, 24) |
| `jet_length_cm` | cm | key `JetLength` × 2.54 (128, 80, 120) |
| `lifetime_s` | s | derived: `JetLength / Speed` (4.27, 0.67, 0.75) |
| `color` | `[r, g, b]` 0..1 | key `rendercolor` `/255` |
| `alpha` | 0..1 | key `renderamt` `/255` |

#### `beams[]` — one row per `env_beam` (`CEnvBeam`)

| Field | Unit | Source |
|---|---|---|
| `index`, `targetname`, `origin_cm`, `sky` | | as `effects[]` |
| `start`, `end` | string | keys `LightningStart`, `LightningEnd` — targetnames resolved at load (random pick among duplicates; `RandomArea` / `RandomPoint` within `radius_cm` when one is missing) |
| `width_cm` | cm | key `BoltWidth` × 2.54 (6 ×26, 10 ×15, 1 ×6) |
| `end_width_cm` | cm | derived: `width_cm × 0.1` — every VtMB beam tapers |
| `noise_amplitude_cm` | cm | key `NoiseAmplitude` × 2.54 (0 ×21, 15 ×21, 200 ×5); the runtime scales it by `length / 100` over 128 divisions |
| `texture` | `vtmb:material:sprites/beama` | key `texture` (`materials/sprites/beama.vmt` on all 47) → the material lane's `MI_` |
| `texture_scroll` | units/s | key `TextureScroll` (35 on all 47) |
| `radius_cm` | cm | key `Radius` × 2.54 (256 on all 47) |
| `life_s` | s | key `life` (0 ×42 continuous, `.1` ×5 strikers) |
| `strike_time_s` | s | key `StrikeTime` (0, 2, 3, 4, 5) |
| `damage` | per trace | key `damage` (0 ×27, 1 ×8, 600 ×7, 100 ×5) — one trace along the straight axis |
| `color`, `alpha` | 0..1 | keys `rendercolor`, `renderamt` `/255` |
| `spawnflags` | int | key `spawnflags` (1 ×35, 0 ×7, 384 ×5 — Source's beam bits: 1 start on, 4 random strike, 128/256 shade start/end) |
| `start_hidden` | bool | key `StartHidden` |
| `impact_particle`, `faces_player`, `framerate`, `framestart`, `renderfx` | provenance | authored on 19 / 47 / 47 / 47 / 47 rows; `impact_particle` is precached and never spawned, `faces_player` latches a send-prop no client code reads — **inert in VtMB, carried for provenance, inert here** |

#### `effectStats{}`

`rows` per table, `roots` (distinct root ids), `unresolvedRoots[]` (`{index, particle_definition}`),
`unresolvedChildren[]` (`{root, node, name}`), `keyframeHistogram` (`{count: rows}` over every
ramp of every tree — the number the floor's slot shape is checked against), `maxDepth`,
`maxLeaves`.

### Producer and stage

The stage is a new `importers.effects` module called from `stage_map`, reading three published
units and nothing else: the entities unit (rows and `references[]`), the particle units of the
closure (`keys[]`, `blocks[]`, `projection`, `dependencies[]` — walked from the root through
`spawn.particle` and `collide.spawn.particle` / `collide.decal.particle` with a visited set per
path), and the root unit's `models[]` for the brush bounds; sprite dimensions come off the texture
lane's staged sidecar, like a sprite's (`sprites[]`), and a sprite the texture lane has not
imported is the same loud failure a missing `SM_` is (`run: uv run elysium import textures`).
The projection's `meaning` decides which table a key lands in; a key with `meaning: null` is a
stage warning naming the definition, offset and key, never a silent drop. The differ gets an
`effects` row (rows, roots, unresolved counts) beside `sprites`.

`<map>.particles.json`, `formats/particles.py::compile_definition`, `make_particle_systems.py` and
`UElysiumParticleAssetBuilder` are **not read by this lane**. They keep serving the legacy bake
for every unlisted map (below) and retire with it in R9; nothing in R7.3 edits them.

### Consumption and cutover

`bake_map_v2._place_effects` spawns one actor per row of the four placed tables, writes the row
onto the actor (`AElysiumEffectActor`'s fields are the row's fields with the same names, the tree
as a `FElysiumParticleTree` of `FElysiumParticleNode`s), binds the leaf sprites and the family
material children by path, and stamps the fog slots (`ElysiumFog.h`, world or sky set by the
`sky` flag) — the same split as sprites: the numpy-bound read offline, the editor spawn from the
staged table. A row whose root is unresolved places no actor (VtMB's own behaviour: `Activate`
removes the entity). The level recipe names the effects tables so a re-export that changes a
tree or a row re-authors the level.

**The explicit per-map cutover.** A map on `UElysiumMapTransportSettings::MapsOnV2Models`
(`Config/DefaultElysium.ini`, "The per-map cutover flag" above) gets its effects placed by this
bake, and its `AElysiumMapActor::ApplyEmitter` drives the placed actor by entity index
(`effects-architecture.md` §5). A map **not** on the list keeps the legacy path **unchanged**:
`<map>.particles.json`, the per-map `NS_<root>` Fountain flatten under
`/ElysiumBaked/<map>/Particles`, and `ApplyEmitter`'s lazily created `UNiagaraComponent` with
`User.RateScale` — byte for byte, until R9 retires that lane once every map is listed. The
substrate leaf `FElysiumEnvParticle` is the same object on both paths; only the embodiment's
answer to `ApplyEmitter` differs, and it logs which path answered.

**Deferred with a road, on this product** (`effects-architecture.md` §5, the fidelity ledger):
the `precipitation` leaf gate (weather's, needs the visibility unit's leaf sky bit — the flag is
staged), the runtime decals of `collide { decal }` (R7.2's runtime-stain seam — the collision
event is wired, the decal spawn calls R7.2's name), the rain look of the `func_particle` boxes
(weather's; the class body is this lane's).

C3 — one Niagara Data Channel world system per map with actors writing islands entries — is
recorded as the fallback if C1's instance count ever hurts (≈ 12 rows per map on average;
`sm_hub_1` is the test, exploration check 10); not the first cut.

#### Verification (R7.3)

At the seam, in the count the note authorizes: one stage row in `pipeline/tests/test_effects.py`
— `fire2_emitter` stages to a tree whose root runs the one-second default clock at `fps` 30 and
whose two live leaves carry their own lifetimes (`Flames2` `frames 10` / `max_frames 15` →
1/3 s, max 1/2 s; `FlameGlow2` `frames 60` → 2 s), and `barrelfireemitter` resolves with every
child present (the top placed roots the legacy compiler failed);
`bake_verify.verify_effects` loads the level and matches every `elysium.effect` actor to its
staged row by entity index (class, root, attach data, leaf count), no row missing and none extra,
and counts the dust / steam / beam actors the same way. The runtime rows (`Elysium.Policy`:
`SetRateScale` ramps linearly into the actor's one rate float; `point_explosion` orders light →
damage → particle → shake and honours the three damage bools; one Substrate pin on the shake
pattern's `frac²` envelope) are `effects-architecture.md` §5's.

### Measured (2026-09-02, the three working maps, the offline stage only)

`importers.map_geometry.stage_map` (manifest **version 7**) on the three maps, after
`import textures --select particles` had staged the 318 sprite sidecars; no bake ran:

| Map | `effects[]` | roots | unresolved roots | max depth / leaves | keyframes per ramp (count of ramps) | dust / steam / beam |
|---|---|---|---|---|---|---|
| `sp_tutorial_1` | 29 | 16 | 1 | 4 / 8 | 1 ×2011, 2 ×74, 3 ×21, 4 ×29, 5 ×13, 6 ×5, 7 ×5, 8 ×2, 9 ×4, 10 ×2, 11 ×2, 15 ×4, 16 ×2, **83 ×1** | 0 / 0 / 0 |
| `sm_pawnshop_1` | 1 | 1 | 0 | 2 / 4 | 1 ×163, 3 ×2, **42 ×1** | 0 / 0 / 0 |
| `sm_hub_1` | 55 | 8 | 0 | 4 / 8 | 1 ×1148, 2 ×31, 3 ×10, 4 ×19, 5 ×3, 7 ×1, 9 ×2, 16 ×2, **42 ×1** | 0 / 0 / 0 |

**The keyframe histogram corrects the note.** "The corpus maximum is five keyframes" was the
*spawn-block* maximum (`rate "15,5~50,20,5~50,15"`); the *particle-body* ramps of the placed trees
go far past it: `Airplaine` `mask` 42 keyframes (`airplane_emitter`, 22 maps), `FlameGlow1` /
`FlameGlow2` `size` and `color` 15 (`fire1_emitter`, `fire2_emitter`), `Moth_Path` `theta_speed`
16 (`moth_emitter`), `FlameEmbers1` `x_speed` / `y_speed` 9 (`barrelfireemitter`,
`fire1_emitter`), `d_animalism_pestilence_fx4` `width` 83. The stage carries every keyframe
(`effectStats.keyframeHistogram` is the count); a floor that holds five per ramp
(`effects-architecture.md` §5.3, `User.Leaf<ii>.Ramps` at 37 × 5) must resample or widen — an
open item for the floor's author, recorded here, not a stage decision.

None of the three maps authors a `func_dustmotes`, `env_steam` or `env_beam`; the three tables
were exercised on `ch_lotus_1` (39 dustmotes), `hw_hub_1` (2 steam) and `ch_fulab_1` (12 beams,
`sprites/beama` staged) without staging them. Stage warnings the corpus produces
(`effectStats.unreadKeys`): `ch_fulab_1`'s `pilot_flame_emitter` authors `fps` inside a spawn
block and `flametrail*` a `rate` on the particle body (no row in the runtime's tables), and
`impactfx_sparks_blue` a `maxframes` typo — all carried nowhere, as VtMB carries them nowhere.
The `sparks_warrens_computers_fx*` colour ramps spell `255!,0!,…`; the stage reads the numeric
prefix as the engine's `atof` does.
