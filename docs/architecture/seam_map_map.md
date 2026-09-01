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
