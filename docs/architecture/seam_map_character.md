# Character GLB Exporter seam

This document defines the isolated Character GLB Exporter's complete binary glTF 2.0 (`.glb`)
unit for one VtMB character body. This offline product does not replace the ESKM character bake or
participate in runtime loading.

The source-format facts referenced by this contract remain owned by
`docs/vtmb/mdl_v2531.md`, `docs/vtmb/animation_and_movers.md`,
`docs/vtmb/facial_animation.md`, `docs/vtmb/secondary_motion.md`, and
`docs/vtmb/phy_vphysics.md`.

Referenced GLB units are defined by `docs/architecture/seam_map_animation_bank.md`,
`docs/architecture/seam_map_material.md`, `docs/architecture/seam_map_texture.md`, and
`docs/architecture/seam_map_surface_property.md`.

## Flow and source notation

An arrow names a member inside a source container:

```text
<VTMB>/Vampire/pack001.vpk -> models/character/npc/unique/downtown/lacroix/lacroix.mdl
```

Loose files use the same install-relative member name:

```text
<VTMB>/Unofficial_Patch -> models/character/npc/unique/downtown/lacroix/lacroix.mdl
```

Source selection is **UP-first** for every member independently:

```text
Unofficial_Patch loose -> retail loose -> retail VPK
```

The winning MDL, VTX, PHY, facial-resource, and resolved material members may therefore come from
different source containers.

## Character unit

One UP-first character MDL path produces one character-body GLB:

```text
models/character/<model>.mdl
  -> vtmb:character-body:<model>
  -> $ELYSIUM_EXPORT_V2_ROOT/characters/<model>.glb
```

The unit key is the normalized path below `models/character/`, without `.mdl`. One MDL produces
one GLB; each distinct body or armor MDL produces its own GLB.

The public command is:

```text
uv run elysium export_v2 character-glb models/character/<model>.mdl
uv run elysium export_v2 characters-glb
```

The feature's code is isolated from the ESKM writer: format aggregation lives below
`elysium_pipeline.formats.character_glb`, the product writer is
`elysium_pipeline.exporters.character_glb`, and its independent structural reader is
`elysium_pipeline.validation.character_glb`.

It owns:

- the character's geometry, LODs, material sections, skeleton, bind pose, and skin weights;
- material-slot and skin-family bindings to stable material asset IDs;
- animation clips authored locally by the character MDL;
- sequence selection metadata attached to those local clips;
- morph targets, flex-controller graph, mouth, eyes, and eyelids;
- attachments, procedural bones, secondary motion, and renderer cloth;
- character collision and ragdoll physics when a PHY companion exists;
- authored include-model dependency references;
- a complete mapping-coverage report over every admitted source field.

## Source closure covered by the specification

| Source member | Character ownership | GLB destination |
|---|---|---|
| `<VTMB>/Vampire/pack*.vpk -> models/character/<model>.mdl` | body-authoritative | core skeleton, vertex attributes, local animations and morphs; `ELYSIUM_vtmb_character.mdl` for VTMB semantics |
| `<VTMB>/Vampire/pack*.vpk -> models/character/<model>.dx80.vtx` | body-authoritative primary topology | core mesh primitives, indices, sections and LODs; source-to-core topology mapping in `vtx` |
| `<VTMB>/Vampire/pack*.vpk -> models/character/<model>.dx7_2bone.vtx` | legacy topology validation source | semantic-equivalence check against DX80; `omitted-proven` coverage on agreement |
| `<VTMB>/Vampire/pack*.vpk -> models/character/<model>.phy` | optional character physics | `physics` solids, hulls, mass and ragdoll constraints |
| `<VTMB>/Vampire/pack*.vpk -> materials/<search-path>/<material>.vmt` | material dependency | stable material asset ID in `materialBindings` and `dependencies` |
| `<VTMB>/Vampire/pack*.vpk -> expressions/<stem>_expressions.{txt,vfe}` | model-selected facial table | normalized expression-controller table in `facial` |
| `<VTMB>/Vampire/pack*.vpk -> expressions/<stem>_phonemes.{txt,vfe}` | model-selected phoneme table | normalized phoneme-controller table in `facial` |
| `<VTMB>/Vampire/pack*.vpk -> models/character/<bank>.mdl` | animation-bank dependency | stable bank asset ID in `dependencies` |

Every row resolves through the UP-first policy. TXT/VFE and DX7/DX80 pairs are decoded into their
semantic forms and compared; a disagreement enters coverage as `unresolved`.

### LaCroix source closure

The first exercise resolves these direct members:

```text
<VTMB>/Unofficial_Patch
  -> models/character/npc/unique/downtown/lacroix/lacroix.mdl

<VTMB>/Vampire/pack001.vpk
  -> models/character/npc/unique/downtown/lacroix/lacroix.dx80.vtx
  -> models/character/npc/unique/downtown/lacroix/lacroix.dx7_2bone.vtx

<VTMB>/Vampire/pack008.vpk
  -> models/character/npc/unique/downtown/lacroix/lacroix.phy
```

The MDL include maps to a dependency identity:

```text
<VTMB>/Vampire/pack001.vpk
  -> models/character/shared/male/npc_allsequences.mdl
  => vtmb:animation-bank:shared/male/npc_allsequences
```

LaCroix's material search paths resolve the body, head, eyes, teeth, and tongue slots to stable
material asset IDs. The model-selected `lacroix_expressions` and `lacroix_phonemes` tables enter
`facial`.

## Material references

The canonical material identity is the normalized install-relative VMT path:

```text
vtmb:material:<path-below-materials-without-.vmt>
```

For example:

```text
vtmb:material:models/character/teeth/upperteeth
```

The character decoder resolves each MDL material name through its search paths and writes the
resulting material asset ID. Multiple character, prop, and world assets reuse that identity.

```json
{
  "materialBindings": {
    "slots": [
      {
        "slot": 3,
        "sourceName": "upperteeth",
        "material": "vtmb:material:models/character/teeth/upperteeth"
      }
    ],
    "skinFamilies": []
  }
}
```

Each mesh primitive carries the resolved material ID:

```json
{
  "extensions": {
    "ELYSIUM_material_reference": {
      "material": "vtmb:material:models/character/teeth/upperteeth"
    }
  }
}
```

Each material asset carries its own texture references, so the character dependency graph stops at
the material asset ID.

A studio texture name that resolves to no VMT through any of the model's search paths keeps a
sentinel identity instead, and its slot records the candidate paths that were tried:

```json
{
  "slot": 2,
  "sourceName": "HEAD",
  "sourcePath": null,
  "material": "vtmb:missing-material:2:head",
  "resolved": false,
  "candidates": ["materials/models/character/gibs/HEAD.vmt"]
}
```

A total miss is the engine's own routine outcome, not an export defect: VtMB composes exactly
`materials/<search path><name>.vmt` over the model's header search paths in header order and
carries no flat, `models/`-prefixed or otherwise global last resort, so a name whose VMT sits
outside those paths is unreachable to retail too and the material system substitutes its `___error`
checkerboard for the slot (research case `material-resolution`; the offline resolver states the
same rule in `formats/mdl.py`, which answers `NO_MATERIAL`).

A sentinel identity therefore names no material asset and produces no `dependencies` row. It enters
coverage as `omitted-proven` with the reason `studio-texture-name-has-no-vmt`, because the miss is
proven behaviour rather than unknown meaning. A primitive bound to a sentinel keeps it in
`ELYSIUM_material_reference`, and a consumer binds the error material for that slot, which is what
the character bake does for the same slots.

## Binary glTF layout

The character unit is an ordinary GLB 2.0 container with exactly two chunks in the required order:

```text
character.glb
|- header      magic 'glTF', version 2, total length
|- chunk 0     JSON  (0x4E4F534A), UTF-8, padded to 4 bytes with 0x20
|  |- glTF core object graph
|  `- extensions.ELYSIUM_vtmb_character
`- chunk 1     BIN   (0x004E4942), padded to 4 bytes with 0x00
   |- geometry, tangent and index accessors
   |- inverse-bind matrices
   |- animation input/output accessors
   |- morph-target position/normal accessors
   `- physics hull position/index accessors
```

There is one `buffers` entry whose `byteLength` is the BIN chunk payload, and every `bufferView`
uses buffer 0. Each accessor owns one buffer view, and view offsets are 4-byte aligned. The JSON is
serialized with compact separators and rejects `NaN` and infinity, so one source closure yields one
byte-identical product.

The JSON chunk declares two custom namespaces, both used and both required:

```json
{
  "extensionsUsed": [
    "ELYSIUM_material_reference",
    "ELYSIUM_vtmb_character"
  ],
  "extensionsRequired": [
    "ELYSIUM_material_reference",
    "ELYSIUM_vtmb_character"
  ],
  "extensions": {
    "ELYSIUM_vtmb_character": {
      "schemaVersion": "1.2.0",
      "identity": {},
      "sourceResolution": {},
      "coordinateTransform": {},
      "mdl": {},
      "vtx": {},
      "physics": {},
      "materialBindings": {},
      "facial": {},
      "procedural": {},
      "secondaryMotion": [],
      "cloth": {},
      "dependencies": [],
      "coverage": {}
    }
  }
}
```

Object-local extension payloads carry stable indexes into the root extension.

## Core glTF content

Standard glTF carries everything a general consumer can draw or play; the extension carries the
VTMB-only information beside it.

| Character datum | glTF core representation | VTMB-only information in the extension |
|---|---|---|
| Bone hierarchy and bind locals | joint `nodes` | MDL bone index, flags, controllers, scales, pose-to-bone, procedural and physics fields |
| Skin | `skins`, `inverseBindMatrices`, `JOINTS_0`, `WEIGHTS_0` | influence selector, decoded-weight rule, source record identity |
| Vertex geometry | `POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD_0` | bodypart/model/mesh/vertex identity and the source-to-core vertex map |
| VTX topology | primitive `indices` and material sections | VTX variant, LOD, strip groups and original-vertex mapping |
| Multiple skeleton roots | identity non-joint common root | the original parentless-root set |
| Material slots | `ELYSIUM_material_reference` per primitive and per core material | source material name, search path and stable material asset ID |
| Skin families | — | `materialBindings.skinFamilies`, family and skinref indexes |
| Local animation tracks | `animations`, samplers and channels | sequence/animation identity, ownership mask, activity, flags, fades and additive base |
| Blend grids and layers | the referenced animation objects | `sequences[].grid` axes and cells, autolayer order and composition semantics |
| Timeline events and movement | — | `sequences[]` event, root-motion, reach, swing, envelope and combo records |
| Facial deformation | morph targets and `extras.targetNames` | flex descriptors, controllers, rules, ramps, mouths and morph-piece mapping |
| Eyes and eyelids | eye nodes and morph targets | eyeball bases, eyelid targets, iris rules and eye-mesh association |
| Attachments | attachment nodes parented to bones | original attachment index, flags and bone binding |
| Procedural bones | — | `procedural.axisInterpolation[]` control bone, driven bone, axis and table |
| Secondary motion and cloth | numeric accessors | recipes, particles, constraints, collisions and render maps |
| PHY collision and ragdoll | hull position/index accessors | solids, ledges, mass, damping, constraints and key values |

### Coordinate transform

Core glTF uses its standard right-handed, Y-up, metre coordinate system. The extension states the
transformation applied to each coordinate-bearing domain in `coordinateTransform`:

| Field | Value |
|---|---|
| `source` | `Source inches, Z-up, right-handed` |
| `destination` | `glTF metres, Y-up, right-handed` |
| `scale` | `0.0254` |
| `position` | `(x, y, z)_gltf = (x, z, -y)_source * 0.0254` |
| `direction` | `(x, y, z)_gltf = (x, z, -y)_source` |
| `quaternion` | `(x, y, z, w)_gltf = (x, z, -y, w)_source` |
| `domains` | the per-domain rule for mesh, skeleton, animation, morph, attachments, eyes, physics and cloth |

The mapping is a rotation rather than a reflection, so triangle winding carries through unchanged.
Quaternions are normalized on conversion, and a non-finite or degenerate quaternion fails the
export. Physics is stated in `IVP metres, axis-only`, and cloth records keep source inches inside
the extension.

### Nodes and skin

`nodes` is built in a fixed order, and that order is the contract every object-local index relies
on:

```text
[0 .. boneCount-1]      one joint node per MDL bone, in MDL bone order
[boneCount ..]          one node per attachment, then one node per eyeball
[optional]              identity common root, only when the MDL has several parentless bones
[last]                  the skinned mesh node
```

A joint node's index is its MDL bone index, so `skins[0].joints` is `[0 .. boneCount-1]` and
`ELYSIUM_vtmb_character.mdl.bones[i]` describes node `i`. Each joint node carries `name`,
`translation` and `rotation` from the MDL bind pose, and `children` from the bone parent table.
`skins[0].inverseBindMatrices` is a `MAT4` float accessor holding the inverse of each bone's
composed global bind matrix, column-major. `skins[0].skeleton` is present only when the model has
exactly one parentless bone; a model with several keeps them all as scene roots below one identity
node, so no bone transform is invented.

An attachment node is parented to its bone and carries:

```json
{
  "extensions": {
    "ELYSIUM_vtmb_character": { "attachmentIndex": 0, "flags": 0, "bone": 12 }
  }
}
```

An eyeball node is parented to its bone and carries `eyeballIndex`, `bone`, `radius` and
`irisScale`; its full record stays in `mdl.header.bodyParts[].models[].eyeballs[]`.

### Meshes and primitives

One `meshes` entry per VTX LOD, named `<asset>:lod<n>`, and one primitive per material section.
The scene instances LOD 0 through the single skinned mesh node; higher LODs remain addressable
`meshes` entries with no node of their own, and `vtx.lods[]` maps each LOD index to its mesh index,
switch points and primitive count.

Every primitive carries this attribute set:

| Attribute | Type | Component |
|---|---|---|
| `POSITION` | `VEC3` | `FLOAT`, with `min`/`max` bounds |
| `NORMAL` | `VEC3` | `FLOAT` |
| `TEXCOORD_0` | `VEC2` | `FLOAT` |
| `JOINTS_0` | `VEC4` | `UNSIGNED_SHORT` |
| `WEIGHTS_0` | `VEC4` | `FLOAT` |
| `TANGENT` | `VEC4` | `FLOAT`, only when the source model stores tangents |
| `indices` | `SCALAR` | `UNSIGNED_INT`, triangles |

`NORMAL` is the authored MDL normal wherever the source stores a usable one; a degenerate authored
normal is replaced by the area-weighted geometric normal of the primitive's own triangles, so the
attribute is always unit length.

Each primitive names its source identity and its material:

```json
{
  "material": 3,
  "extensions": {
    "ELYSIUM_material_reference": { "material": "vtmb:material:models/character/teeth/upperteeth" },
    "ELYSIUM_vtmb_character": {
      "bodyPart": 0,
      "model": 0,
      "mesh": 3,
      "skinReference": 3,
      "sourceVertices": [],
      "stripGroups": []
    }
  }
}
```

`sourceVertices` is the source-to-core vertex map, and `stripGroups` records the VTX strip-group
decomposition the primitive was flattened from.

`materials` holds one core material per distinct material asset ID, in first-use order. Its PBR
values are a neutral placeholder: the surface itself belongs to the material GLB the ID names, and
the binding rather than the appearance is what this product carries.

### Morph targets

Facial deformation is written as glTF morph targets on the LOD 0 primitives. Each target supplies
`POSITION` and `NORMAL` deltas. `meshes[0].extras.targetNames` lists the target names in order and
`meshes[0].weights` is the matching all-zero rest weight vector. `facial.morphTargets[]` maps each
target back to its flex description and its source flex pieces.

### Animations

Each MDL-local animation becomes one `animations` entry named `<index>:<name>`. All samplers in an
entry share one `SCALAR` input accessor holding `frame / fps` seconds. A bone whose per-animation
weight is zero contributes no channel; every other bone contributes two `LINEAR` samplers and two
channels, `translation` (`VEC3`) and `rotation` (`VEC4`), targeting that bone's joint node. The
poses are decoded parent-relative locals for every bone except those listed in
`mdl.splitRotationBones`, whose rotation channel states the bone's model-space orientation while its
translation stays attached to the parent; a consumer applies the rule in
`docs/vtmb/animation_and_movers.md` §A.4a to those bones. `mdl.localAnimations[]` carries the
VtMB-side record for each clip and its `animation` index into the core array.

### Physics

`physics` mirrors the PHY records, with each convex hull's numeric payload moved into the BIN
chunk: `hulls[].positions` is a `VEC3` float accessor and `hulls[].indices` a `SCALAR`
`UNSIGNED_INT` accessor. Solid properties, ledge nodes, edit parameters, constraints, breaks and
the trailing key-value text stay as extension records.

## Extension reference

| Root key | Kind | Contents |
|---|---|---|
| `schemaVersion` | string | `1.2.0` |
| `identity` | object | `asset` (stable body ID), `modelPath`, `sourcePolicy` |
| `sourceResolution` | object | `policy`, and `members[]` of `role`, `path`, `origin`, `byteLength`, `sha256` |
| `coordinateTransform` | object | the source-to-glTF rule and its per-domain table |
| `mdl` | object | `header`, `bones[]`, `splitRotationBones[]`, `localAnimations[]`, `sequences[]`, `poseParameters[]`, `attachments[]`, `hitboxSets[]`, `ikChains[]` |
| `vtx` | object | `variants[]`, `comparison`, `lods[]` |
| `physics` | object or null | `header`, `coordinateSystem`, `solids[]`, `editParams[]`, `constraints[]`, `breaks[]`, `keyValues[]` |
| `materialBindings` | object | `slots[]` and `skinFamilies[]` |
| `facial` | object | `flexDescriptions[]`, `controllers[]`, `rules[]`, `mouths[]`, `phonemeFilter`, `selectedTables`, `morphTargets[]` |
| `procedural` | object | `axisInterpolation[]` |
| `secondaryMotion` | array | one record per authored dynamics recipe |
| `cloth` | object | `garments[]` and `sourceModels[]` |
| `dependencies` | array | `role`, `asset`, `sourcePath`, `byteLength`, `sha256` |
| `coverage` | object | `mapped[]`, `typedUnidentified[]`, `omittedProven[]`, `byteLedger[]`, `unresolved[]`, `unsupported[]` |

`identity.asset` and each member's `origin` come from the same resolution the byte ledger is
written against. A member `origin` is either `{"kind": "loose", "root": <install subdirectory>}` or
`{"kind": "vpk", "container": <pack file>, "offset": …, "size": …}`.

`mdl.header` restates every studio header field, including the table directory (`header.tables`,
one `count`/`offset` row per indexed table) and the decoded `bodyParts`, `textures`,
`sequenceGroups`, `boneControllers`, `transitionGraph` and `includeModels` payloads. Every record
that came from a file offset keeps that offset, so a ledger range and the record it pays for name
the same place. A sequence keeps both its raw fixed `animationTable` and the resolved `grid` its
cells describe.

`vtx.comparison` states which variant is primary, both decoded headers and material-replacement
tables, whether the two variants are semantically `equivalent`, and which LOD indexes overlap or
belong to only one variant.

## Source identity and dependencies

`identity` carries the stable character asset ID, `up-first` source policy, and each selected
member's role, install-relative path, origin, and SHA-256. `dependencies` is one array of stable
asset references. Each row carries a role, asset ID, source path, and content hash.

Character dependency roles are:

- `material`, from resolved MDL material slots and skin families;
- `animation-bank`, from the MDL include-model table;
- `surface-property`, from character physics or resolved materials;
- `sound` and `effect`, from character-local animation events.

LaCroix carries `material` and `animation-bank` dependency rows.

## Semantic coverage

Coverage is stated in two vocabularies. The semantic states below grade a source *field or record*;
the byte-ledger states further down grade a *byte range*. A field graded `equivalent` and the bytes
it was read from graded `mapped` describe the same decode from the two directions.

Every source field or record receives one mapping state:

| State | Meaning |
|---|---|
| `mapped` | represented directly by core glTF or the character extension |
| `equivalent` | transformed into a representation with the same understood meaning |
| `derived` | recoverable completely from other represented data |
| `omitted-proven` | confirmed padding, dead storage, or redundant mechanism with recorded evidence |
| `unresolved` | meaning is unknown or candidate interpretations disagree |
| `unsupported` | meaning is understood but the GLB schema does not yet represent it |

A complete character GLB has zero `unresolved` and zero `unsupported` rows. Every source field
family, record identity, and reference edge is present in the ledger. Unknown typed values retain
their source-offset identity.

Schema `1.2.0` additionally requires `coverage.byteLedger`, one row for every direct source member
listed by `sourceResolution.members`:

| Field | Meaning |
|---|---|
| `sourcePath` | the member's install-relative path, matching its `sourceResolution` row |
| `sourceSha256` | SHA-256 of the member's bytes, matching its `sourceResolution` row |
| `byteLength` | the member's byte length |
| `accountedBytes` | bytes claimed by the range table; equal to `byteLength` |
| `coveragePercent` | `100.0` |
| `stateBytes` | claimed bytes per state, key-sorted |
| `rangesSha256` | digest of the range table |
| `ranges` | the gapless ordered range table |

A range row is `{"offset", "length", "state", "owner"}`. Rows are ordered by `offset`, each row
begins where the previous one ends, the first begins at 0, and the last ends at `byteLength`.
`owner` is the walker's path to the record that paid for the range, so a range and the extension
record it belongs to name the same place. `rangesSha256` is the SHA-256 of the UTF-8 encoding of

```text
json.dumps({"path": sourcePath, "byteLength": byteLength, "ranges": ranges},
           sort_keys=True, separators=(",", ":"))
```

The allowed states are:

| State | Meaning |
|---|---|
| `mapped` | binary record or payload decoded into the extension or a core accessor |
| `mapped-string` | a null-terminated ASCII string decoded into a named field |
| `mapped-text` | a text region decoded into a structured table |
| `derived` | recoverable completely from other represented data |
| `omitted-proven` | evidence-backed omission, carrying its reason in `coverage.omittedProven` |
| `reserved-zero` | a declared field the source stores as zero |
| `padding-zero` | alignment or unreferenced storage the source stores as zero |

`reserved-zero` and `padding-zero` are verified: claiming either over a non-zero source byte aborts
publication. Publication also fails when ranges overlap, leave even one byte unclaimed, disagree
with the source hash or length, or when a member has no ledger row.

This is **100% byte accountability without an opaque source mirror**. It covers the direct MDL,
both admitted VTX variants, optional PHY, and selected TXT/VFE twins. Referenced VMTs and included
animation-bank MDLs remain separately hashed dependencies: their payload belongs to their own
export product rather than being duplicated into the character body.

The export-time validator receives the actual source members and checks the ledger against their
bytes before the destination is written. A later standalone GLB validation can still verify range
continuity, state totals, source identities and the range-table digest, but cannot re-read a user's
install that is no longer present.

The decoder and byte walker independently traverse the same declared format surface, while the
validator checks the emitted core/extension structure:

- every source vertex maps to core vertices and every VTX triangle maps to core indices;
- all LOD, section, material-slot, skin-family, UV, authored-normal, joint, and weight values agree;
- every local sequence, frame/bone pose, ownership mask, grid, layer, event, and movement row agrees;
- every morph, facial rule, eye, attachment, procedural, dynamics, cloth, and PHY record agrees;
- alternate VTX and TXT/VFE representations are equivalent or explicitly represented as distinct;
- every required reference resolves or produces one named coverage failure.

The character seam's losslessness claim is complete semantic representation plus gapless byte
accountability. It is not bit-identical source reconstruction: verified padding and
renderer/compiler mechanisms are classified rather than copied as opaque data.
