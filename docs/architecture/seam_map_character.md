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
  -> $ELYSIUM_EXPORT_ROOT/glb/characters/<model>.glb
```

The unit key is the normalized path below `models/character/`, without `.mdl`. One MDL produces
one GLB; each distinct body or armor MDL produces its own GLB.

The public command is:

```text
uv run elysium export_v2 chacter-glb models/character/<model>.mdl
uv run elysium export_v2 chacters-glb
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

## Binary glTF layout

The character unit is an ordinary GLB 2.0 container:

```text
character.glb
|- JSON chunk
|  |- glTF core object graph
|  `- ELYSIUM_vtmb_character
`- BIN chunk
   |- geometry/index accessors
   |- skin and inverse-bind accessors
   |- animation input/output accessors
   |- morph-target accessors
   `- physics numeric payloads
```

The JSON chunk declares one custom character namespace:

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
      "schemaVersion": "1.1.0",
      "identity": {},
      "sourceResolution": {},
      "mdl": {},
      "vtx": {},
      "physics": {},
      "materialBindings": {},
      "facial": {},
      "procedural": {},
      "secondaryMotion": {},
      "cloth": {},
      "dependencies": {},
      "coverage": {}
    }
  }
}
```

Object-local extension payloads carry stable indexes into the root extension.

## Core glTF mapping

| Character datum | glTF core representation | VTMB-only information retained in the extension |
|---|---|---|
| Bone hierarchy and bind locals | joint `nodes` | original MDL bone index, flags, controllers, procedural and physics fields |
| Skin | `skins`, `inverseBindMatrices`, `JOINTS_0`, `WEIGHTS_0` | influence selector, decoded-weight rule, source record identity and validation |
| Vertex geometry | `POSITION`, authored `NORMAL`, `TEXCOORD_0` | bodypart/model/mesh/vertex identity and source-to-core vertex map |
| VTX topology | primitive `indices` and material sections | VTX variant, LOD, strip-group and original-vertex mapping |
| Multiple skeleton roots | identity non-joint common root | original parentless-root set |
| Material slots | `ELYSIUM_material_reference` on each primitive | source material name and stable material asset ID |
| Skin families | `materialBindings.skinFamilies` | original family/skinref indexes and material asset IDs |
| Local animation tracks | `animations`, samplers and channels | sequence/animation identity, ownership mask, activity, flags, fades and additive base |
| Blend grids and layers | referenced animation objects and numeric accessors | grid axes/cells, masks, autolayer order and composition semantics |
| Timeline events and movement | extension references | event, root-motion, reach, swing, envelope and combo records |
| Facial deformation | morph targets and animation weights | flex descriptors/controllers/rules, ramps, mouths and morph-piece mapping |
| Eyes and eyelids | eye nodes and morph targets | eyeball bases, eyelid targets, iris rules, material asset IDs and eye-mesh association |
| Attachments | attachment nodes parented to bones | original attachment index, flags and bone binding |
| Procedural bones | extension records | control bone, driven bone, axis and interpolation table |
| Secondary motion and cloth | extension records and numeric accessors | recipes, particles, constraints, collisions and render maps |
| PHY collision and ragdoll | extension records and numeric accessors | solids, hulls, mass, damping and constraints |

Core glTF uses its standard right-handed, Y-up, metre coordinate system. The extension records the
Source-to-glTF transformation applied to each coordinate-bearing domain.

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

Schema `1.1.0` additionally requires `coverage.byteLedger`, one row for every direct source member
listed by `sourceResolution.members`. Each row carries the member's path, byte length, SHA-256,
per-state byte totals, a digest of the range table, and a gapless ordered list of byte ranges.
The allowed states are `mapped`, `mapped-string`, `mapped-text`, `derived`, `omitted-proven`,
`reserved-zero`, and `padding-zero`. Publication fails when ranges overlap, leave even one byte
unclaimed, disagree with the source hash/length, or label a non-zero source byte as zero storage.

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
