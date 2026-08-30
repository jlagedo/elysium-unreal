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
