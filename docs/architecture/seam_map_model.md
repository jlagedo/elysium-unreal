# Model GLB seam

This document defines one binary glTF 2.0 unit for one VtMB studio model: every `models/**.mdl`
in the install — character bodies, animation banks, scenery, items, wield models, view models
and placed skeletal props — through one decoder, one schema and one identity namespace. It
supersedes the character-body unit of `seam_map_character.md` and the bank unit of
`seam_map_animation_bank.md`; `vtmb:character-body:` and `vtmb:animation-bank:` are retired in
favour of `vtmb:model:`, and `ELYSIUM_vtmb_character` becomes `ELYSIUM_vtmb_model` at schema
`2.0.0`. Shared rules are owned by `seam_map_unit_contract.md`.

Format facts remain owned by `docs/vtmb/mdl_v2531.md`, `docs/vtmb/animation_and_movers.md`,
`docs/vtmb/animation_rig_resolution.md`, `docs/vtmb/facial_animation.md`,
`docs/vtmb/procedural_bones.md`, `docs/vtmb/secondary_motion.md`, `docs/vtmb/phy_vphysics.md`,
`docs/vtmb/wielded_weapons.md` and `docs/vtmb/mdl-coverage-and-gaps.md`.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> models/<model>.mdl
  -> vtmb:model:<model>
  -> $ELYSIUM_EXPORT_V2_ROOT/models/<model>.glb
```

The key is the normalized path below `models/` without `.mdl`. One MDL produces one GLB whatever
its content; a bank with no geometry and a one-bone static prop are the same unit kind with
sections empty.

```text
uv run elysium export_v2 model-glb models/<model>.mdl
uv run elysium export_v2 models-glb
```

`identity` classifies the unit from its bytes, not from its path:

| Field | Values | Rule |
|---|---|---|
| `family` | `character`, `items`, `scenery`, `weapons`, … | first path segment below `models/` |
| `shape` | `bank` | zero body parts |
| | `static` | `$staticprop` flag set, or exactly one bone and no local animation |
| | `skeletal` | otherwise |
| `roles` | `character-body`, `animation-bank`, `wield`, `view-model`, `ground-item`, `placed-prop`, `static-prop`, `include-only` | every role another unit assigns it; filled by the corpus index, empty at export |

`roles` is the one field written after export: it is the inverse of every `model` reference in
the corpus, so an MDL nothing references is visible as such.

### Retired identities

| Retired | Replacement |
|---|---|
| `vtmb:character-body:npc/unique/downtown/lacroix/lacroix` | `vtmb:model:character/npc/unique/downtown/lacroix/lacroix` |
| `vtmb:animation-bank:shared/male/npc_allsequences` | `vtmb:model:character/shared/male/npc_allsequences` |
| `characters/<model>.glb`, `animation-banks/<bank>.glb` | `models/<model>.glb` |

A member under any other prefix — the VPK ships `unpacked 0.74/shovelhead/shovelhead_short.mdl`
beside its VTX pair — is not a model unit, because the engine composes model paths below
`models/` and cannot reach it; the corpus index records it as `unreachable-member`.

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| `models/<model>.mdl` | unit-selecting, body-authoritative | core skeleton, vertices, animations, morphs; `mdl` |
| `models/<model>.dx80.vtx` | primary topology | primitives, indices, sections, LODs; `vtx` |
| `models/<model>.dx7_2bone.vtx` | legacy topology, validation twin | `vtx.comparison`; `omitted-proven` on agreement |
| `models/<model>.phy` | optional physics companion | `physics` |
| `materials/<search-path>/<name>.vmt` | material dependency | `vtmb:material:` IDs in `materialBindings` |
| `models/<include>.mdl` | include-model dependency | `vtmb:model:` IDs in `dependencies` |
| `expressions/<stem>_{expressions,phonemes}.{txt,vfe}` | model-selected facial tables | `vtmb:expression-table:` IDs in `facial.selectedTables` |

Every member resolves UP-first independently; the MDL, its VTX pair and PHY may come from
different containers. A VTX variant the install lacks is `omissions[] missing-vtx-variant`; an
MDL with body parts and no VTX at all is `unresolved`, because topology is the unit's own
structure. A PHY that is absent is not an omission — most models ship none.

The expression tables are **references**, not content: `phonemes.vfe` and `phonemes_male.vfe` are
the client's literal fallbacks for every model without a same-stem table, and inlining them would
make hundreds of units authoritative for one file. `facial.selectedTables` names the
`vtmb:expression-table:` ID the model stem selects for each class and the fallback it would take,
with `resolved` per row; the tables' decoded content is owned by `seam_map_expression_table.md`.

## Binary glTF layout

```text
model.glb
|- JSON chunk
|  |- glTF core object graph
|  `- extensions.ELYSIUM_vtmb_model
`- BIN chunk
   |- geometry, tangent and index accessors
   |- inverse-bind matrices
   |- animation input/output accessors
   |- morph-target position/normal accessors
   `- physics hull position/index accessors
```

```json
{
  "extensionsUsed": ["ELYSIUM_material_reference", "ELYSIUM_vtmb_model"],
  "extensionsRequired": ["ELYSIUM_material_reference", "ELYSIUM_vtmb_model"],
  "extensions": {
    "ELYSIUM_vtmb_model": {
      "schemaVersion": "2.0.0",
      "identity": {},
      "sourceResolution": {},
      "coordinateTransform": {},
      "mdl": {},
      "vtx": {},
      "physics": null,
      "materialBindings": {},
      "facial": {},
      "procedural": {},
      "secondaryMotion": [],
      "cloth": {},
      "dependencies": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

A bank unit has no BIN geometry, no `meshes` and no skinned node, but keeps its joint nodes,
`skins[0]` and `animations`. A static unit has one joint, no `animations` and, when the MDL
declares `$staticprop`, `mdl.header.flags.staticProp: true`.

## Core content

| Model datum | glTF core | VTMB-only information in the extension |
|---|---|---|
| Bone hierarchy and bind locals | joint `nodes` in MDL bone order | bone index, flags, controllers, scales, pose-to-bone, procedural and physics fields |
| Skin | `skins[0]`, `inverseBindMatrices`, `JOINTS_0`, `WEIGHTS_0` | influence selector, decoded-weight rule, record identity |
| Vertex geometry | `POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD_0` | bodypart/model/mesh/vertex identity, source-to-core vertex map |
| VTX topology | primitive `indices`, one primitive per material section | variant, LOD, strip groups, original-vertex mapping |
| Several skeleton roots | identity non-joint common root | the parentless-root set |
| Material slots | `ELYSIUM_material_reference` per primitive and per core material | source name, search path, stable ID, sentinel on a proven miss |
| Skin families | — | `materialBindings.skinFamilies` |
| Local animations | `animations`, samplers, channels | sequence identity, ownership mask, activity, flags, fades, additive base |
| Blend grids and layers | the referenced animation objects | `sequences[].grid`, autolayer order and composition |
| Events and movement | — | `sequences[]` events, root motion, reach, swing, envelope, combo |
| Facial deformation | morph targets, `extras.targetNames` | flex descriptors, controllers, rules, ramps, mouths, morph-piece mapping |
| Eyes and eyelids | eye nodes, morph targets | eyeball bases, eyelid targets, iris rules |
| Attachments | nodes parented to bones | attachment index, flags, bone |
| Procedural bones | — | `procedural.axisInterpolation[]` |
| Secondary motion and cloth | numeric accessors | recipes, particles, constraints, collisions, render maps |
| PHY collision and ragdoll | hull position/index accessors | solids, ledges, mass, damping, constraints, key values |
| Hitboxes and IK chains | — | `mdl.hitboxSets[]`, `mdl.ikChains[]` with the bounds and links |

### Nodes

```text
[0 .. boneCount-1]   one joint per MDL bone, in MDL order
[boneCount ..]       one node per attachment, then one per eyeball
[optional]           identity common root when several bones are parentless
[last]               the skinned mesh node, when the unit has geometry
```

A joint node's index is its MDL bone index; `skins[0].joints` is `[0 .. boneCount-1]` and
`mdl.bones[i]` describes node `i`. `skins[0].skeleton` is present only for exactly one
parentless bone; no bone transform is ever invented.

### Meshes and primitives

One `meshes` entry per VTX LOD named `<key>:lod<n>`, one primitive per material section. The
scene instances LOD 0; higher LODs are addressable `meshes` entries with no node, mapped by
`vtx.lods[]`. Attributes are `POSITION`/`NORMAL`/`TEXCOORD_0` (`FLOAT`), `JOINTS_0`
(`UNSIGNED_SHORT`), `WEIGHTS_0` (`FLOAT`), `TANGENT` only when the source stores tangents, and
`UNSIGNED_INT` triangle `indices`. A degenerate authored normal is replaced by the area-weighted
geometric normal and the vertex is listed in `anomalies[] degenerate-normal`. Each primitive
carries `bodyPart`, `model`, `mesh`, `skinReference`, `sourceVertices` and `stripGroups`.

`materials` holds one neutral core material per distinct material ID in first-use order; the
surface belongs to the material unit and only the binding is carried here.

A material name that resolves to no VMT through the model's header search paths — VtMB composes
exactly `materials/<search path><name>.vmt` in header order and has no global last resort — keeps
the sentinel `vtmb:missing-material:<slot>:<name>`, lists its `candidates`, and enters coverage as
`omitted-proven` with `studio-texture-name-has-no-vmt`, because the engine substitutes its
`___error` checkerboard for exactly that slot.

### Animations

Each MDL-local animation is one `animations` entry `<index>:<name>`, all samplers sharing one
`SCALAR` input of `frame / fps` seconds; a bone with zero per-animation weight contributes no
channel, every other bone two `LINEAR` channels. Poses are parent-relative locals except the
bones in `mdl.splitRotationBones`, whose rotation channel states model-space orientation while the
translation stays with the parent (`docs/vtmb/animation_and_movers.md` §A.4a). An animation-bank
unit's tracks are decoded against its own declared skeleton; the remap onto a body is the
consumer's join through `dependencies`.

### Morph targets, physics, coordinate transform

Facial deformation is written as morph targets on the LOD 0 primitives with `POSITION` and
`NORMAL` deltas; `facial.morphTargets[]` maps each back to its flex description and pieces.
`physics` mirrors the PHY records with each convex hull's positions and indices in the BIN and
the solid, ledge, constraint, break and key-value records in the extension, stated in
`IVP metres, axis-only`. `coordinateTransform` follows the unit contract; cloth keeps source
inches inside the extension.

## Extension reference

| Key | Kind | Contents |
|---|---|---|
| `schemaVersion` | string | `2.0.0` |
| `identity` | object | `asset`, `modelPath`, `family`, `shape`, `roles`, `sourcePolicy` |
| `sourceResolution` | object | the member table |
| `coordinateTransform` | object | the rule and per-domain table |
| `mdl` | object | `header` (every studio header field and the table directory), `bones[]`, `splitRotationBones[]`, `localAnimations[]`, `sequences[]`, `poseParameters[]`, `attachments[]`, `hitboxSets[]`, `ikChains[]`, `boneControllers[]`, `transitionGraph`, `includeModels[]`, `sequenceGroups[]`, `keyValues` |
| `vtx` | object | `variants[]`, `comparison`, `lods[]` |
| `physics` | object or null | `header`, `coordinateSystem`, `solids[]`, `editParams[]`, `constraints[]`, `breaks[]`, `keyValues[]` |
| `materialBindings` | object | `slots[]`, `skinFamilies[]` |
| `facial` | object | `flexDescriptions[]`, `controllers[]`, `rules[]`, `mouths[]`, `phonemeFilter`, `selectedTables[]`, `morphTargets[]` |
| `procedural` | object | `axisInterpolation[]` |
| `secondaryMotion` | array | one record per dynamics recipe |
| `cloth` | object | `garments[]`, `sourceModels[]` |
| `dependencies` | array | the reference table |
| `anomalies` | array | `degenerate-normal`, `degenerate-bind`, `stale-include-path`, `vtx-variant-disagreement`, `phy-solid-count-mismatch`, `header-length-mismatch` |
| `omissions` | array | `missing-vtx-variant`, `legacy-vtx-equivalent`, `unused-sequence-group`, `reserved-field` |
| `coverage` | object | the coverage object |

`mdl.header` restates every field including `checksum`, `flags`, the `char[128]` name and every
count/offset pair, so a ledger range and the record it pays for name the same place. Where a
count is zero the table is an empty array, not an absent key: absence is a schema difference,
emptiness is a source fact.

`mdl.keyValues` carries the header's KeyValues text decoded as a tree, with the raw text beside
it; this is where `$staticprop`, `prop_data` and mover metadata live on props.

## Dependencies

| Role | Produced by |
|---|---|
| `material` | resolved material slots and skin families |
| `model` | the include-model table, in include order (`includeIndex` on the row) |
| `expression-table` | `facial.selectedTables` |
| `surface-property` | PHY solid `surfaceprop` and material `$surfaceprop` joins |
| `sound`, `particle` | local animation events whose options name a file |

An include model the install lacks is `unresolved`, because the unit's own sequence resolution
depends on it. A sound or particle an event names that the install lacks warns.

## Wield, item and placement joins

The unit owns nothing about who uses it. The item-to-model join (`viewmodel`, `playermodel`,
`wieldmodel_m`/`_f`, `infomodel`) is owned by the item's `vtmb:vdata:` unit; static and detail
placement by the `vtmb:map:` unit; entity placement by `vtmb:map-entities:`; the actor-to-bank
join by `vtmb:scene:`. Each of those references `vtmb:model:` and the corpus index writes the
inverse into `identity.roles`. `docs/vtmb/wielded_weapons.md` §3's per-bone name-matched merge is
therefore a consumer computation over two model units, not a field of either.

## Byte ledger owners

| Owner | Member | Range |
|---|---|---|
| `mdl.header` | MDL | the studio header |
| `mdl.header.name` | MDL | the 128-byte name |
| `mdl.bones[i]`, `mdl.bones[i].name` | MDL | one bone record, its name string |
| `mdl.hitboxSets[i].hitboxes[j]` | MDL | one hitbox |
| `mdl.localAnimations[i].frames` | MDL | one animation's frame data |
| `mdl.sequences[i]`, `.events[j]`, `.autolayers[j]` | MDL | one sequence and its tables |
| `mdl.bodyParts[i].models[j].meshes[k]` | MDL | one mesh record |
| `mdl.bodyParts[i].models[j].vertices` | MDL | the vertex block |
| `mdl.bodyParts[i].models[j].meshes[k].flexes[f].vertAnims` | MDL | one flex's vertex animation |
| `mdl.textures[i]`, `mdl.searchPaths[i]`, `mdl.skinTable` | MDL | material tables |
| `mdl.includeModels[i]`, `mdl.keyValues` | MDL | include and keyvalue regions |
| `mdl.attachments[i]`, `mdl.ikChains[i]`, `mdl.procedural[i]`, `mdl.secondaryMotion[i]`, `mdl.cloth` | MDL | the named tables |
| `vtx.<variant>.header`, `.bodyParts[i].models[j].lods[k].meshes[m].stripGroups[s].*` | VTX | one strip group's vertices, indices, strips |
| `vtx.<variant>.materialReplacements` | VTX | the replacement list |
| `phy.header`, `phy.solids[i].*`, `phy.keyValues` | PHY | the VPhysics records |
| `mdl.padding`, `vtx.padding`, `phy.padding` | any | verified zero alignment |

Alignment fill the compiler wrote non-zero is `omitted-proven compiler-fill` with its bytes'
digest, never `padding-zero`.

## Coverage and validation

A complete model unit has zero `unresolved` and zero `unsupported` rows. Validation re-decodes the
MDL, both VTX variants and the PHY independently of the writer and checks that every source vertex
maps to core vertices, every VTX triangle to core indices, every LOD, section, slot, family, UV,
normal, joint and weight agrees, every local sequence, frame/bone pose, mask, grid, layer, event
and movement row agrees, every morph, facial rule, eye, attachment, procedural, dynamics, cloth and
PHY record agrees, both VTX variants are equivalent or explicitly distinct, and every reference
resolves or produces one named coverage row.

The unit's losslessness claim is complete semantic representation plus gapless byte
accountability over MDL, VTX pair and PHY. Referenced VMTs, included models and expression
tables are separately hashed dependencies whose payload belongs to their own units.

## LaCroix reference

```text
<VTMB>/Unofficial_Patch -> models/character/npc/unique/downtown/lacroix/lacroix.mdl
<VTMB>/Vampire/pack001.vpk -> models/character/npc/unique/downtown/lacroix/lacroix.dx80.vtx
<VTMB>/Vampire/pack001.vpk -> models/character/npc/unique/downtown/lacroix/lacroix.dx7_2bone.vtx
<VTMB>/Vampire/pack008.vpk -> models/character/npc/unique/downtown/lacroix/lacroix.phy
  => vtmb:model:character/npc/unique/downtown/lacroix/lacroix
     -> model            vtmb:model:character/shared/male/npc_allsequences
     -> material         vtmb:material:models/character/teeth/upperteeth  (and the body, head, eye slots)
     -> expression-table vtmb:expression-table:lacroix_expressions
     -> expression-table vtmb:expression-table:lacroix_phonemes
     -> surface-property vtmb:surface-property:flesh
```
