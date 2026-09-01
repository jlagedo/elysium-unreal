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
`docs/vtmb/wielded_weapons.md` and `docs/vtmb/mdl-coverage-and-gaps.md`. `## Import` at the end of
this document is the binding contract for turning these units into Unreal static meshes; everything
before it describes the unit and makes no Unreal claim.

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
`skins[0]` and `animations`. A static unit has one joint and, when the MDL declares
`$staticprop`, `mdl.header.flags.staticProp: true` — the studio header's own flag word, which is
where that fact lives; see "Extension reference" for what does *not* live in a KeyValues region.

**A static unit is not an animation-free unit.** 3,294 of the corpus's 3,300 static-shape units
carry exactly one local animation — a single reference-pose track named `0:only_sequence` (1,662)
or `0:idle` (1,630), with `0:ragdoll` and `0:none` once each — because studiomdl compiles a
`$sequence` even for a prop that never moves. Only 6 carry none. A consumer therefore branches on
`identity.shape`, never on "has no `animations`"; the props import lane (`## Import`) reads
`shape` and ignores the track rather than inferring anything from its presence.

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
| `mdl` | object | `header` (every studio header field and the table directory), `bones[]`, `splitRotationBones[]`, `localAnimations[]`, `sequences[]`, `poseParameters[]`, `attachments[]`, `hitboxSets[]`, `ikChains[]`, `boneControllers[]`, `transitionGraph`, `includeModels[]`, `sequenceGroups[]`, `keyValues` (always `null` — see below) |
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

**v2531 has no header KeyValues region, and `prop_data` does not exist in this seam.**
`mdl.keyValues` is published and is `null` on every one of the 4,445 units; every unit carries the
same `omissions[]` row — `reserved-field`, field `mdl.keyValues`, reason
`v2531-has-no-header-keyvalues-region` — whose evidence is that the count/index pair at header
`+396`/`+400`, modern Source's `keyvalueindex`/`keyvaluesize`, addresses this build's 28-byte
secondary-motion records instead. The key stays in the schema so the difference from modern Source
is *stated* rather than merely absent. What an earlier draft of this document placed there lives
elsewhere, and consumers read it from there:

| Datum | Where it is |
|---|---|
| `$staticprop` | `mdl.header.flags.staticProp` (bit `0x10` of the studio header flag word) |
| model mass | `physics.solids[i].properties.mass`, from the PHY's KeyValues tail |
| surface property | `physics.solids[i].properties.surfaceprop`, and `mdl.header.surfaceProperty` for a model that ships no PHY |
| mover metadata | the placing entity's own keys (`vtmb:map-entities:`) — never the model |
| `prop_data` | nowhere; no VtMB model member carries it |

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

`identity.roles` is also the props import lane's selection set: `## Import` stages the units
something references and leaves the rest published but unimported.

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
| `mdl.includeModels[i]` | MDL | the include region (there is no keyvalue region — see "Extension reference") |
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

## Import

`uv run elysium import models` turns every **referenced** model unit into one `UStaticMesh` below
`/ElysiumBaked/Meshes`, with its slots bound to the landed V2 material instances, its collision
cooked from VtMB's own convex hulls and its skin families written into one corpus-wide table. It is
the props lane of the one-pipeline roadmap (`docs/project/seam_migration.md` → "Roadmap — one
pipeline", R1.3–R1.5), and it follows the material and texture lanes' shape exactly: an offline
stage phase, a headless editor import phase, one `manifest.json` between them, recipe stamps, a
prune scope, and provenance carried on the asset as `UAssetUserData`.

Every count in this section is measured over the **4,445 model units** under
`$ELYSIUM_EXPORT_V2_ROOT/models/**` and the corpus index's inverse reference graph
(`index.glb` → `ELYSIUM_vtmb_corpus_index.inverse`), 2026-08-31 — the same run that produced the
validation record in `seam_migration.md` → "Props/models seam validated". The ranked gap list in
that entry is what this section decides; the decisions there are transcribed, not reopened.

**No silent drop, and no silent guess.** Every input this lane reads — every slot, every family
index, every LOD row, every solid, every surface-property name — has a named destination or a named
recorded consequence below. A unit whose input falls outside those is a **stage failure** with the
unit named and the reason printed, never an asset written with the unknown part quietly missing.
That is the rule the tables in this section exist to make checkable.

### Scope and selection

The unit set is the **referenced** one: a model unit is in scope when some other unit names it.
`identity.roles` is that inverse, and the corpus index publishes it whole.

| Set | Units | Rule |
|---|---|---|
| Published model units | 4,445 | every `models/**.mdl` install member |
| `vtmb:model:` ids some unit references | 3,677 | inverse-graph keys, 10,612 `role=model` edges |
| — of those, ids naming no published unit | 16 (36 edges) | dangling; see below |
| **In scope: published and referenced** | **3,661** | 2,753 static-shape, 908 skeletal-shape |
| — placed by a map or its entities | 3,185 | 2,655 static-shape, 530 skeletal-shape |
| Published and unreferenced | 784 | **provenance-only; never imported** |

The 784 unreferenced units are not an oversight and not a backlog. They are published so the seam
is complete and so a later consumer that starts naming one finds it already exported; nothing
places them, so nothing imports them. They are listed in the run report under `unreferenced` and
are outside the prune scope, so an `--all` run neither writes nor deletes at their paths.

**The 16 dangling ids route to the shipped placeholder.** They are retail data bugs, not seam
gaps — `vtmb:model:missing` (named by `la_library_1`), `vtmb:model:error`,
`character/npc/doppleganger/doppleganger`, the four `weapons/disciplines/*/info/i_*` rows in
`system/stats`, the four `weapons/pistol` and `weapons/throwing_star` view/world/ground rows,
`weapons/fists/view/v_fists`, and the four `andrei`, `bodyblood`, `malkgirl/girl` and
`hands/female/gangrel/v_gangrel_fem_hands` rows in `system/npctemplate*` and `system/clandoc000`.
No such MDL exists in the install, so no `SM_` can
be written for them. The rule is: **a dangling `vtmb:model:` reference resolves to
`/ElysiumBaked/Meshes/SM_elysium_missing_model`**, one shipped placeholder authored by this lane
(the `MI_V2_Missing` material on a unit cube, so a wrong reference is visible rather than invisible),
and the referencing unit records `danglingModelReference` with the id it asked for. The 16 ids and
their 36 referring edges are pinned in the run report; a 17th appearing is a report row, not a
failure, because the consumer lanes must keep booting.

**Map-scoped runs are the working mode; the full corpus is an owner-approved run.** The stage
refuses to run unscoped. `--maps <stems>` stages exactly the models that those maps' root units
(`vtmb:map:<stem>` — `staticProps[]` plus the detail-prop nodes) and their entities units
(`vtmb:map-entities:<stem>` — every `model.asset`) reference, resolved through the same inverse
graph. `--all` stages the 3,661 and is the owner-approved whole-corpus run.

| `--maps` stem | Models referenced |
|---|---|
| `sp_tutorial_1` | 242 |
| `sm_pawnshop_1` | 95 |
| `sm_hub_1` | 178 |
| the three together (the working test corpus) | **414** — 349 static-shape, 65 skeletal-shape |

None of the three names a dangling id. A map-scoped run sets `pruneScope: null` and prunes
nothing — only `--all` prunes, because only `--all` knows the whole `keep` set. That asymmetry is
the point of the flag: a scoped run can never delete a model another map still stands.

### Identity and naming

```text
vtmb:model:<path>   ->  /ElysiumBaked/Meshes/SM_<safe_name(static_stem("models/<path>.mdl"))>
                    ->  /ElysiumBaked/Meshes/DA_ElysiumPropSkins        (the corpus skin table)
                    ->  /ElysiumBaked/Meshes/SM_elysium_missing_model   (the dangling-reference placeholder)
                    ->  /Game/ElysiumGenerated/Materials/V2/MI_V2_Missing  (the sentinel material)
```

The stem is the **whole model path folded**, `models/` included, not the base filename:
`models/items/Rings/Ground/Ring03.mdl` → `models_items_rings_ground_ring03` →
`SM_models_items_rings_ground_ring03`. The fold is `formats.mdl.sanitize` (lower-case, then every
character outside `[a-z0-9._-]` replaced one-for-one, runs **not** collapsed, `.` and `-` kept)
composed with `asset_names.safe_name`; `shared_corpus.static_stem` is the composed function and
`FElysiumContentPaths::PropModelStem` is its C++ twin. The fold is collision-free by measurement:
4,445 units produce 4,445 distinct stems.

**The layout is flat, and this lane writes beside the legacy corpus, never over it.** The legacy
shared bake owns `/ElysiumBaked/Shared/Meshes`; the V2 lane owns `/ElysiumBaked/Meshes`, a sibling
at the mount root exactly as `/ElysiumBaked/Materials` sits beside `/ElysiumBaked/Shared/Materials`
and `/ElysiumBaked/Textures` beside `/ElysiumBaked/Shared/Textures`. Same principle as the V2
masters: **beside, not over.** The two corpora coexist until the flip, so a bisect has both halves
and a broken V2 import never takes the running game down with it.

Because the root is the only thing that differs, **asset names, stems and slot names are preserved
exactly**, and the runtime flip is a one-line change to one accessor. `BakedPropMesh`,
`BakedItemMesh` and `BakedPropSkins` all compose their paths from
`FElysiumContentPaths::BakedSharedMeshes()`; pointing that accessor at `/ElysiumBaked/Meshes` moves
the whole corpus. The four substrate call sites that recompute the stem live
(`ElysiumItemContainer.cpp`, `ElysiumItemClasses.cpp`, `ElysiumLockable.cpp`,
`ElysiumTerminal.cpp`), `UElysiumEntityBodies::ResolvePropMesh`/`BindMapMaterials`, and both skin
appliers are untouched by the flip. That flip is a **named wiring task of its own** (roadmap
R5.1/R6 territory), not part of this lane: R1.4 lands the corpus at the new root, and the flip
happens when the map lane is ready to place from it.

**Slot names are `safe_name(materialBindings.slots[i].sourceName)`** — the studio texture name, not
the VMT path and not the material asset name. `tankwht`, `metalox`, `capwht`. This is load-bearing:
`ApplyPropSkin` and `ApplyAnimatedPropSkin` look their overrides up by
`UStaticMesh::GetMaterialIndex(SlotName)`, and `BindMapMaterials` copies a skeletal body's surfaces
across from its static twin by matching slot names, so a renamed slot silently unbinds a skin swap
rather than failing.

**Duplicate folded slot names get a suffix, and the first occurrence keeps its name.** 10 referenced
units author the same studio texture name in more than one skin-table slot — two static-shape
(`scenery/furniture/fancybed/fancybed`, `fancybed2`, both `null` twice) and eight skeletal, of which
`scenery/furniture/computer/monitor_useableb` is the only one that also carries a second skin family.
Unreal resolves a duplicated `MaterialSlotName` to the first index, so a legacy bake bound every
repaint of the later slots onto the first one. The rule: occurrence 0 keeps `safe_name(sourceName)`
verbatim, so every name a legacy consumer could already resolve resolves identically; occurrence
*n*>0 becomes `<name>_<slot index>` with the MDL skin-table slot index; the skin table is written
with the same disambiguated names, so a family now reaches every slot. Each unit records a
`duplicateSlotName` anomaly listing the collided names.

**The fold is pinned by a Python↔C++ twin test.** A pytest case walks every one of the 4,445 model
paths through `shared_corpus.static_stem` and compares against a table generated from
`FElysiumContentPaths::PropModelStem`, and an `Elysium.Policy.*` automation test runs the C++ side
over the same table. Neither implementation may be changed without the other failing. `safe_name`
and `baked_asset_name` are **not** interchangeable here (`asset_names` says why) — this lane uses
`safe_name`, on both sides.

### Material binding

**The per-primitive material is `materialBindings.skinFamilies[family][skinReference]`.** The
primitive's own `ELYSIUM_material_reference.asset` is the **family-0 answer only** and must never be
read as the model's binding: it is the value the exporter resolved through family 0's row, which is
correct for skin 0 and wrong for every other skin. The lane reads the family table.

- `materialBindings.slots[]` gives the mesh's slots: `slot` (the skin-table column),
  `sourceName` (the studio texture name → the Unreal slot name), `material` (a `vtmb:material:` id
  or a `vtmb:missing-material:` sentinel), `resolved`, `surfaceProperty`.
- `materialBindings.skinFamilies[]` is one row per skin family, each a flat array indexed by the
  primitive's `ELYSIUM_vtmb_model.skinReference`, whose entries are `vtmb:material:` ids. No row in
  the corpus is ragged: every family of a unit has the same length (0 exceptions in 4,445 units).
- **Family 0 supplies the mesh's own default slots.** A `UStaticMesh` carries exactly one material
  per slot; that material is `skinFamilies[0][skinReference]`. Every other family is a
  *substitution set* and lives in the skin table, never on the mesh.
- Family counts over the 3,661 in scope: 1 family on 3,474 units, 2 on 128, 3 on 28, 4 on 9, 5 on 9,
  6 on 6, 7 on 1, 10 on 2, 11 on 1, 12 on 1, 28 on 1, 40 on 1.

**A material id resolves to the landed V2 instance by `importers.materials.asset_path_for`** — the
same function the material lane names its own assets with, so the two lanes cannot drift. There is
no second lookup and no path arithmetic in this lane. A material id that `asset_path_for` maps to a
path with no asset on the mount is a **stage failure** naming the model, the slot and the id: the
material corpus is a prerequisite of this lane, and a mesh with an unbound slot renders Unreal's
grey default while its receipt freezes that state as current.

**The skin-index clamp is engine behaviour, and the import reproduces it.** 59 placements in the
corpus name a `skin` index past the end of their model's `skinFamilies`. VtMB clamps to the last
family rather than falling back to 0 or failing to draw, so the skin table's lookup clamps the same
way: `family = min(requested, len(skinFamilies) - 1)`, and a clamped lookup records
`skinIndexClamped` with the requested and used indices. The clamp lives in the table's resolution
rule, not at each call site, so the runtime appliers keep asking for the index the placement wrote.

**`vtmb:missing-material:` sentinel slots bind one shared, loud placeholder.** A studio texture name
that resolves to no VMT through the model's header search paths keeps the sentinel
`vtmb:missing-material:<slot>:<name>` (see "Meshes and primitives"), and the sentinel is
**invisible to every closure check** — 1,523 sentinel slots over 597 units corpus-wide, 1,328 over
461 referenced units, and 98 slots on 66 placed static-shape props. It is not a resolution failure
to fix: VtMB itself substitutes its `___error` checkerboard for exactly that slot, so the shipped
game draws a checker there too.

The rule: every sentinel slot binds **`/Game/ElysiumGenerated/Materials/V2/MI_V2_Missing`** — one
tracked instance of `M_V2_Unlit` with a generated magenta/black checker on `BaseTexture`
(`T_V2_MissingChecker`, authored beside it by `make_v2_materials.py`, the same generator that
authors `T_LinearWhiteMask` and `T_V2_DefaultFrames`), `BLEND_Opaque`, two-sided off. It is
deliberately loud, it is `___error`'s stand-in rather than a divergence from it, and being opaque it
never vetoes Nanite. Each affected unit records a `missingMaterialSentinel` anomaly per slot with
the slot name, the studio texture name and the candidate paths the export proved empty; the run
report rolls the count up, so the number is visible without walking sidecars. A sentinel is
**never** a stage failure — it is a proven source fact. A slot with no material id *and* no
sentinel is a stage failure, because that combination means the export itself is incomplete.

### Geometry

**LOD 0 is the scene-instanced mesh.** The GLB's scene instances the LOD-0 mesh; `vtx.lods[k].mesh`
names the glTF mesh for every other LOD. Attributes read verbatim: `POSITION`, `NORMAL`,
`TEXCOORD_0`, `TANGENT` when the source stores it, `UNSIGNED_INT` indices. One glTF primitive
becomes one Unreal mesh section, and its slot is the position of its
`ELYSIUM_vtmb_model.skinReference` in the slot list built from LOD 0 in first-use order. No
higher-LOD primitive in the corpus names a slot LOD 0 lacks (0 units), so the slot list is complete
from LOD 0; a unit where that ever fails is a stage failure rather than a silently extended slot
array.

**The one-bone skin is dropped and the positions are read verbatim.** A static-shape unit has one
joint, all weights are 1.0 on joint 0, and joint 0's transform is identity — verified per unit
before the drop. `JOINTS_0`/`WEIGHTS_0` are discarded and `POSITION` is used as-is. A unit whose
joint 0 is not identity is a stage failure naming the transform, never a silent bake of a
pre-skinned pose. For a skeletal-shape unit the same rule cannot hold, and its static twin is built
from the bind-pose positions the GLB already stores — the twin is a material and slot carrier, not
a pose (see "What this lane does not do").

**Coordinate transform: glTF metres Y-up right-handed → Unreal centimetres Z-up left-handed.** One
rule for the whole GLB, because the export already normalized every domain into the glTF frame
(`coordinateTransform.domains`), and the runtime performs no coordinate math (`pipeline/CLAUDE.md`):

```text
position   (X, Y, Z)_ue = (x, z, y)_gltf * 100
direction  (X, Y, Z)_ue = (x, z, y)_gltf          (normal, tangent; re-normalize)
tangent w  w_ue = -w_gltf                          (bitangent sign; the map is a reflection)
winding    reversed once, at section build
```

It composes exactly with the two rules the pipeline already owns and must keep agreeing with both:
a mesh vertex through `Source inches → glTF` (`(x, z, -y) * 0.0254`) and then this rule lands on
`formats.bsp.source_to_unreal` (`(x, -y, z) * 2.54`) to the float; a PHY hull point published in
`IVP metres, axis-only` (`(x, -y, -z)`) lands on `formats.phy`'s `(x, -z, -y) * 100`. The map is a
reflection (determinant −1), which is why the winding reverses **once** and why `TANGENT.w` flips —
`B = w · (N × T)` and a reflection negates the cross product. Reversing winding twice, or forgetting
the `w` flip, is the one class of error this lane cannot see in a screenshot, so both are asserted
in the stage's own unit tests against a hand-computed triangle.

Raw source-unit records restated in `mdl.header` — `hullMin`/`hullMax` in particular — are **not**
in the glTF frame; they take `source_to_unreal` directly, and the Y negation swaps min and max on
that axis.

**Submodel 0 per body part, for static props.** `sprp` carries no bodygroup selector, so a static
placement draws submodel 0 of each body part; the import bakes exactly that. 14 referenced units
declare more than one submodel, 9 of them placed, and only one of those is static-shape
(`scenery/physics/cannister/cannister01`, two body parts of one submodel each). Each records a
`multiSubmodelBakedZero` anomaly listing every submodel it dropped, so the day a bodygroup
vocabulary exists the affected set is already enumerated. Bodygroups are not invented here.

**VTX LODs become Unreal LODs.** LOD *k* of the primary variant becomes Unreal LOD *k*, one section
per primitive, slots by name. LOD counts over the 3,661 in scope: 1 LOD on 3,048 units, 2 on 168,
3 on 128, 4 on 91, 5 on 27, 6 on 66, 7 on 129, and 4 units with none (see "Loud failures"). A
`switchPoints` row of `-1.0` is a shadow LOD and is **dropped** — the LOD is not built and the
remaining LODs renumber; 147 such rows exist in the corpus and every one is on a skeletal-shape
unit, so no static-shape LOD chain is affected today. The rule is stated anyway because a static
twin of a skeletal unit meets it.

**`switchPoints` → `ScreenSize`: the direction is read off the corpus, not off a transcribed
selector.** `docs/vtmb/mdl_v2531.md` records `ModelLODHeader_t.switchPoint` as a field; the engine
comparison that consumes it was never transcribed, so the mapping is derived from what the data
proves. Across all 4,445 units no LOD row's `switchPoints` disagree with one another (0 rows, so a
row has exactly one value); LOD 0's value is `0.0` on every static-shape unit; and the value rises
monotonically with LOD index (LOD 1 clusters at 5–15, LOD 3 at 12–50, LOD 5 at 50–177, over 55
distinct values from 1.0 to 300.0). A coarser LOD is drawn further away, so the Source metric grows
with distance while Unreal's `ScreenSize` shrinks with it: the two orders are **reciprocal**, and a
reciprocal is the only one-constant monotone family that preserves the authored ordering.

```text
ScreenSize[0] = 1.0
ScreenSize[k] = clamp(LodSwitchConstant / switchPoint[k], LodScreenSizeFloor, LodScreenSizeCeiling)
```

`LodSwitchConstant` (default `1.0`), `LodScreenSizeFloor` (`0.001`) and `LodScreenSizeCeiling`
(`0.9`) are **knobs, not literals**: they live on `UElysiumModelSettings` (a `UDeveloperSettings`
page, `Project Settings → Elysium → Models`) and the stage reads them out of
`Config/DefaultElysium.ini` section `[/Script/ElysiumUE.ElysiumModelSettings]`, exactly as the
material stage reads `ChromaThreshold`. They ride in the recipe, so changing one re-imports the
multi-LOD units and nothing else. The values are a wiring default, not a tuning judgement — the
owner's LOD pass happens on this page after the roadmap ("Wire first, tune later").

The result must be strictly decreasing. A row that does not decrease is set to
`min(previous * 0.5, computed)` and records `lodScreenSizeNotMonotone`; a row whose
`switchPoints` are not all equal takes the maximum and records `lodSwitchPointDisagreement`.
Neither fires in today's corpus; both exist so that if the day comes it is a report line rather
than a silently reordered LOD chain.

**No `TEXCOORD_1`, and no generated lightmap UVs.** No model unit in the corpus carries a second UV
set. Lighting is Lumen-only (`seam_migration.md` → "The matte-world premise is repudiated"), so
static lighting is never built, a lightmap UV set would be dead data in 3,657 assets, and the
import sets `generate_lightmap_u_vs = False` explicitly rather than accepting the importer default.
`UStaticMesh::LightMapCoordinateIndex` is left at 0 and `LightMapResolution` untouched.

**Nanite is on iff every material the model can ever wear is opaque or masked.** The legacy rule,
kept verbatim in intent and restated on V2 terms: Nanite is a whole-mesh setting, so a single
translucent, additive, modulated or refractive surface disqualifies the mesh, and the test spans
**every slot of every skin family**, not just family 0 — a swap that brings in a translucent
surface would otherwise leave one on a Nanite mesh. On V2 the predicate is read off the material
manifest rather than guessed: a bound material is Nanite-able when the `blendMode` in its
`basePropertyOverrides` is `BLEND_Opaque` or `BLEND_Masked` **and** its master is neither
`M_V2_Refract` nor `M_V2_Water`. `MI_V2_Missing` is opaque and so never vetoes. The decision and the vetoing slot ride
in provenance, so "why is this mesh not Nanite" is answerable from the asset.

### Collision

Collision geometry is cooked **per model**; which of it a placement uses is **not this lane's
call**. The authority is the placement record: `solid` is a keyfield on `CCollisionProperty` with
the stock `SolidType_t` enum (`NONE=0, BSP=1, BBOX=2, OBB=3, OBB_YAW=4, CUSTOM=5, VPHYSICS=6`, 3
bits — `docs/vtmb/phy_vphysics.md` → "Which entities get a collision model"), and the census proves
the placement overrules the model: 2,530 records demand VPHYSICS on 264 models that ship no `.phy`,
3,428 override a `.phy` that is present, and 6,971 place with no collision at all. The contract this
lane owes is therefore: **the per-model asset carries what each mode needs**, so the map bake
(R5.1) can select a mode without ever going back to the source.

| Model | Simple collision | `CollisionTraceFlag` | Mass |
|---|---|---|---|
| ships a `.phy` (2,349 of the 3,661 in scope) | one convex shape per `physics.solids[i].hulls[j]` ledge, unsimplified | `CTF_UseSimpleAndComplex` | `physics.solids[0].properties.mass`, authored |
| ships none (1,312) | one box from `mdl.header.hullMin`/`hullMax` | `CTF_UseSimpleAndComplex` | none set |

**The hulls are reproduced, not approximated.** Each ledge is already convex (the export asserts
it), so it is handed to the collision builder one at a time with `max_convex_hulls_per_mesh = 1`
and `simplify_hulls = False`, and with `bAutoDetectBoxes`/`bAutoDetectSpheres`/`bAutoDetectCapsules`
all off — GeometryScript's `GetDetectedSimpleShape` runs ahead of the convex path even at one hull
per mesh, so leaving them at the engine default re-fits a box-shaped ledge as an `FKBoxElem` (96 of
the test corpus's 283 `.phy` meshes did exactly that before the flags were pinned) — and the results
are combined; that reproduces the authored hull rather than decomposing or re-fitting it. Solid counts run 1 (2,573 units), 15 (289), 18 (9),
3 (7), 17 (5), 2 (3), 19 (2) and 7 (2); a cooked shape count that disagrees with the ledge count
is a stage failure naming both, exactly as the legacy bake failed it.

**`CTF_UseSimpleAndComplex`, never complex-as-simple, because one asset serves both modes.** A
Chaos rigid body can only simulate against *simple* shapes, while the debug pick and a solid static
placement want a per-poly face index they can turn back into a material slot. Both are cooked, so
the same `SM_` serves a simulating `prop_physics` and a static placement of the same model. The
legacy corpus split these — hull-bearing meshes got `CTF_UseSimpleAndComplex` and everything else
`CTF_UseComplexAsSimple` — and the split is dropped here: with the bbox rule below, every mesh has
a simple shape, so the uniform flag is both simpler and strictly more capable.

**The bbox rule.** A model with no `.phy` gets a single box shape from the studio header's own
movement hull, `mdl.header.hullMin`/`hullMax` through `source_to_unreal`. It exists so that the two
placement modes that do not want the render trimesh have a shape: `solid` 2 (SOLID_BBOX), which
`VPhysicsInitStatic` implements as `PhysModelCreateBox(mins, maxs, origin, true)` and which
`FElysiumProp` currently rebuilds at runtime, and `solid` 6 on the 264 models that ship no `.phy`.
Baking the box moves that construction offline, where the bake writes final values and the runtime
applies state.

Carrying the box is **not** a decision to use it. VtMB does not substitute a bounding box for a
failed VPHYSICS init: `CPhysicsProp::CreateVPhysics` warns `ERROR!: Can't create physics object`
and leaves the prop standing as visible, non-solid, non-simulating scenery
(`docs/vtmb/phy_vphysics.md` → "Missing collision"). Whether a VPHYSICS-without-`.phy` placement is
stood inert (faithful) or given the box (playable) is the **placement lane's** ruling, taken with
the census in hand; this lane guarantees only that both are reachable from the asset. Every such
model records `noPhysicsSolidsBoxFallback` with the hull bounds it used.

### Surface properties

The physical material comes from one resolution, run once per unit, that always terminates and
always records which tier answered, as `surfacePropertySource`:

1. **`physics.solids[0].properties.surfaceprop`, lower-cased**, when non-empty — **1,338 units**.
   Solid 0 speaks for the model: **no unit in the whole 4,445 corpus authors two different
   `surfaceprop` values across its solids** (0 disagreements), so there is nothing to arbitrate. A
   future disagreement records `surfacePropertyPerSolidDisagreement` with the full list and still
   takes solid 0. `surfacePropertySource: "physSolid"`.
2. otherwise **`mdl.header.surfaceProperty`, lower-cased**, when non-empty — **494 units**. This
   tier is real coverage, not a formality: it is the only surface 494 units in scope have, most of
   them among the 1,312 that ship no `.phy` at all. `surfacePropertySource: "mdlHeader"`.
3. otherwise **`default`** — the remaining **1,829 units**. 1,025 PHY solid rows in scope author
   the empty string and 2,055 units name nothing in their header; a unit that misses both lands
   here. `surfacePropertySource: "default"`.

The three counts are exclusive and sum to 3,661.

The resolved name binds `UBodySetup::PhysMaterial` to `/ElysiumBaked/SurfaceProperties/PM_<name>`,
the Phase-2 asset, and the name itself rides in provenance beside it so the asset is readable back
without the table. **Lower-casing is part of the rule, not a convenience**: the corpus ships six
case variants (`Kitchen_Pan`, `Kitchen_Pot`, `Kitchen_Utensils`, `Metal`, `Rock`, `Wood`) that name
surfaces the 63-entry `scripts/surfaceproperties.txt` table defines in lower case, matching the
material lane's tier 1.

**An unknown name falls back to `PM_default` and records the anomaly.** A name with no published
`vtmb:surface-property:` unit gets `PM_default`, keeps its authored spelling in provenance, and
records `surfacePropertyUnknown`. Over the 3,661 units in scope this lane's own input yields
exactly **3 unknown names**, one solid row each: `cloth`, `floorblock`, `metaldetector`. The
larger figure in `seam_migration.md` — 79 edges over 17 unknown names, including `bone` and the
shipped typo `defualt` — is measured over the whole `surface-property` dependency role, which also
covers the material lane's `$surfaceprop` join; the two are consistent, they count different edge
sets. Unknown-name coverage is not this lane's to widen: adding class rows is the material lane's
call and already settled there.

### Skins table

One asset for the whole corpus: `/ElysiumBaked/Meshes/DA_ElysiumPropSkins`, a
`UElysiumPropSkinSet`, regenerated from `materialBindings.skinFamilies` — the successor of the
legacy bake's table of the same name, at the V2 root, with the same asset name and the same row
shape so `ApplyPropSkin`/`ApplyAnimatedPropSkin` need no change at the flip.

```text
(stem, family index) -> [ (slot name, MI_ asset) ... ]
```

- The key is the `static_stem` and the family index, the two things a placement writes.
- Rows are **slot-name keyed**, using the disambiguated slot names above — the same names on the
  `SM_`, so `GetMaterialIndex(SlotName)` resolves directly on a static body, and the same names the
  skeletal twin's `BindMapMaterials` matched across.
- Family 0 gets **no row**. It is the mesh's own material set; the appliers restore it by clearing
  the override array, which is also what makes a swap back to skin 0 — and a swap to a family the
  model does not carry, which Source draws as the authored set — undo itself.
- A family row lists **only the slots that differ from family 0**. A family identical to family 0
  produces no row at all; the resolution then finds nothing and the body draws its own materials,
  which is the same picture by a cheaper route.
- Each stem carries its **`FamilyCount`** so that `UElysiumPropSkinSet::Find` can clamp an
  out-of-range index to the last family without loading the mesh, per the clamp rule above. The
  clamp belongs to `Find`, not to its callers: `ApplyPropSkin`, `ApplyAnimatedPropSkin` and the
  item, lockable and terminal call sites all keep asking for the index the placement wrote, and
  none of them learns about clamping. That one-method change ships with the table (R1.5).

### Provenance and idempotency

Every mesh carries one `UElysiumModelProvenance` (`UAssetUserData`, runtime module, so a packaged
game reads it), attached the way `UElysiumMaterialProvenance` and `UElysiumTextureProvenance` are:

| Field | From |
|---|---|
| `AssetId`, `ModelPath`, `Stem`, `UnitSchemaVersion`, `UnitSha256`, `SourceSha256[]` | `identity`, the GLB file, `sourceResolution.members[].sha256` (MDL, both VTX variants, PHY) |
| `Shape`, `Family`, `Roles[]` | `identity` |
| `Slots[]` (`Index`, `SlotName`, `SourceName`, `SourcePath`, `MaterialAssetId`, `MaterialAsset`, `Resolved`, `IsSentinel`) | `materialBindings.slots` plus this lane's resolution |
| `SkinFamilies[]` (`Family`, `Overrides[]`) | `materialBindings.skinFamilies`, as written into the table |
| `Lods[]` (`Index`, `SwitchPoint`, `ScreenSize`, `Sections`, `Triangles`, `Dropped`) | `vtx.lods` plus the mapping |
| `bNanite`, `NaniteVetoSlot`, `NaniteVetoMaterial` | the opacity rule |
| `CollisionMode`, `HullCount`, `ShapeCount`, `MassKg`, `HullBounds` | the collision rules |
| `SurfaceProperty`, `SurfacePropertySource`, `PhysMaterial` | the surface-property resolution |
| `Anomalies[]` (`Kind`, `Extra`), `Omissions[]` (`Reason`, `Extra`), `Coverage` | the unit's own rows plus this lane's, `Extra` carrying every field besides `Kind`/`Reason` |

Three fields publish as asset-registry tags in `MetaDataTagsForAssetRegistry` beside
`ElysiumRecipe`, so the Content Browser filters without loading: **`ElysiumAssetId`**,
**`ElysiumModelShape`** and **`ElysiumNanite`**.

`manifest.json` carries `packageRoot` (`/ElysiumBaked/Meshes`), `select`, `pruneScope`, `keep`,
`stageFailures` and one entry per mesh with its `recipe`. The stamp is
`bake_lib.recipe_fingerprint("models", assetPath, recipe)` over the unit hash, `SETTINGS_VERSION`
(`elysium-model-import-v2`), the resolved slot→asset list, the full skin-family table, the LOD
screen sizes, the collision decision, the Nanite decision, the physical material and
`recipe.provenanceSha256` — a sha256 of the exact bytes the provenance sidecar is written as. The
recipe states the **whole** asset, not the parts that changed, for the same reason the material
lane's does: a reused asset keeps a setting an earlier recipe applied unless the entry restates it
every run. A corrected anomaly, an added omission row, a `PhysMaterial` no longer wanted or a
knob change all move this hash and re-import that entry; `SETTINGS_VERSION` is the coarse,
whole-corpus lever and the recipe hash is the fine one.

### Loud failures

Isolated per unit, named with their reason, counted in `import_report.json`, and the command exits
non-zero when any unit failed. The report rolls every anomaly and omission up by kind, so
`missingMaterialSentinel=N`, `surfacePropertyUnknown=N`, `skinIndexClamped=N`,
`multiSubmodelBakedZero=N` and the rest are visible without walking a sidecar.

| Condition | Consequence |
|---|---|
| a slot with neither a `vtmb:material:` id nor a `vtmb:missing-material:` sentinel | **fail the unit** — the export is incomplete, not the source |
| a material id whose `asset_path_for` path holds no asset | **fail the unit**, naming model, slot and id |
| no admitted VTX topology — the unit declares body parts and publishes no `meshes` | **skip the unit loudly** and name it. 19 such units corpus-wide, **4 in scope**: `null`, `w_null`, `weapons/w_null` and `character/npc/unique/hollywood/and/and`. Every one is skeletal-shape, no static-shape unit in scope lacks a mesh, and **none is placed by any map** — which is why a skip is safe. (`seam_migration.md` counts 7 by the export's own admitted-topology measure; the two count different things and neither set is placed.) |
| joint 0's transform is not identity on a static-shape unit | **fail the unit**, naming the transform |
| a cooked shape count that disagrees with the ledge count | **fail the unit**, naming both |
| a higher LOD naming a slot LOD 0 lacks | **fail the unit** |
| two units folding to one `SM_` path | **fail both**, and put both paths in `keep` so a good asset is not pruned |
| a dangling `vtmb:model:` reference | **not a failure** — route to `SM_elysium_missing_model`, record `danglingModelReference` |
| a `vtmb:missing-material:` sentinel | **not a failure** — bind `MI_V2_Missing`, record the anomaly |

### What this lane does not do

Wire first, tune later. This lane makes props exist at the right paths with the right surfaces,
collision and skins. It makes no judgement that needs a screenshot.

- **No tuning.** No exposure, brightness, roughness or LOD-distance taste. The three LOD constants
  are wiring defaults on a settings page and are left alone; the owner's pass happens on that page
  after the roadmap.
- **No Nanite quality judgement.** The opacity rule above is the whole decision. No cluster
  budgets, no fallback-percent tuning, no per-model opt-outs.
- **No skeletal or animated props.** `USkeletalMesh`, `USkeleton`, `UAnimSequence`,
  `UPhysicsAsset`, morph targets, and the facial, procedural, secondary-motion and cloth data are
  the character/animated-prop lane's, unchanged by this one. The boundary is exact: this lane
  imports **one `UStaticMesh` per in-scope unit that carries geometry**, whatever its
  `identity.shape` — 3,657 assets, being the 3,661 in scope less the 4 with no admitted topology.
  For a static-shape unit that mesh *is* the prop. For one of the 908 skeletal-shape units in scope
  (530 of them placed) it is the **static twin**: the asset `BindMapMaterials` copies a skeletal
  body's slot surfaces from and `ApplyAnimatedPropSkin` layers a family over, which is why the twin
  must exist and must carry the same slot names. The twin is a material and slot carrier built from
  the bind pose; it is never a substitute for the skeletal asset and nothing places it as one.
- **No map placement.** No actor, no transform, no per-placement `skin`, `solid`, fade distance or
  `override_mass` is read here. Those are placement facts and belong to R5.1; this lane's assets are
  what R5.1 places.
- **No legacy retirement.** `/ElysiumBaked/Shared/Meshes` and `DA_ElysiumPropSkins` under it are
  untouched. The root flip and the legacy corpus's deletion are their own named tasks.
