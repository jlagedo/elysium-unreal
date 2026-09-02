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

**Detail props are not this lane's.** The `placements` scene's other node family (`dprp`, 143,412
records over 41 models corpus-wide) is R7.3's: they want instancing and a lighting join, not one
actor each.

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
(`NANITE_BLEND_MODES = {Opaque, Masked}`), so the chunk naming, the section order and the slot
names are byte-for-byte the R5.1 lane's — only the bound asset changes.

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
Because the Nanite split now follows the root instance's blend, `sm_hub_1` chunks 194 → 175
(158 Nanite + 17 `T_`): the two water materials joined the Nanite buckets and the seven
decal/glass-flagged ones left them; `sp_tutorial_1` 110 and `sm_pawnshop_1` 38 are unchanged.

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
