# Material GLB seam

This document defines one binary glTF 2.0 unit for one resolved VtMB VMT material. Format facts
remain owned by `docs/vtmb/texture_format.md`, `docs/vtmb/reflections.md`, and the material sections
of the owning VtMB topic documents.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> materials/<material>.vmt
  -> vtmb:material:<material>
  -> $ELYSIUM_EXPORT_V2_ROOT/materials/<material>.glb
```

The VMT member resolves UP-first. One normalized install-relative VMT path produces one material
GLB, independent of how many characters, props, maps, effects, or UI resources reference it.

## GLB structure

A material GLB is a scene-less glTF asset with one core material and its VTMB semantics:

```text
material.glb
|- JSON chunk
|  |- materials[0]
|  `- ELYSIUM_vtmb_material
`- BIN chunk
   `- numeric payloads used by material extensions
```

```json
{
  "extensionsUsed": [
    "ELYSIUM_vtmb_material"
  ],
  "materials": [
    {
      "name": "upperteeth"
    }
  ],
  "extensions": {
    "ELYSIUM_vtmb_material": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "shader": "VertexLitGeneric",
      "parameters": {},
      "shaderResolution": {},
      "textureBindings": [],
      "dependencies": [],
      "coverage": {}
    }
  }
}
```

## Mapping

| Material datum | GLB mapping |
|---|---|
| Shader and ordered parameters | `ELYSIUM_vtmb_material.shader` and `parameters` |
| Selected shipped program | `shaderResolution.programs` |
| Core PBR approximation | `materials[0]` and applicable Khronos material extensions |
| Texture parameters | stable texture asset IDs in `textureBindings` |
| Patch/include relationship | material dependency asset ID |
| Surface property | surface-property dependency asset ID |
| Environment cubemap symbol | environment dependency identity |
| Proxies and animated parameters | ordered extension records |

## Dependencies

```json
{
  "dependencies": [
    {
      "role": "texture",
      "parameter": "$basetexture",
      "asset": "vtmb:texture:models/character/teeth/upperteeth"
    },
    {
      "role": "surface-property",
      "asset": "vtmb:surface-property:flesh"
    }
  ]
}
```

## Shader resolution

The VMT names a shader *family*; the engine draws with one of that family's shipped combos, chosen
from the material's parameter and flag state. `shaderResolution` records which:

```json
{
  "shaderResolution": {
    "family": "eyes",
    "resolved": true,
    "programs": [
      {"pixelShader": "Eyes_Vampire_Overbright2", "vertexShader": "Eyes",
       "condition": "overbright==2"},
      {"pixelShader": "Eyes_Vampire", "vertexShader": "Eyes", "condition": ""}
    ],
    "inputs": ["$vampire", "overbright==2"],
    "reason": ""
  }
}
```

`programs` holds every pair the material's own state admits. More than one row means the remaining
choice is render configuration rather than anything the VMT says, and exactly one row carries the
empty default `condition`. A pixel program need not belong to its family's name: `Teeth` ships a
vertex program only and draws with `VertexLitTexture`.

The rules are transcribed from `stdshader_dx8.dll` and owned, with their evidence, by
`docs/vtmb/shader_combos.md`. A family whose selector is not transcribed publishes
`"resolved": false` with a `reason` and no programs, because only 11 of 52 family names match a
shipped program stem, so a name-to-file guess is wrong more often than right. That is a join to
another seam's data, not a gap in this material's decode, so it warns rather than entering
`coverage.unresolved`.

## LaCroix examples

```text
<VTMB>/Vampire/pack001.vpk
  -> materials/models/character/teeth/upperteeth.vmt
  -> vtmb:material:models/character/teeth/upperteeth

<VTMB>/Unofficial_Patch
  -> materials/models/character/npc/unique/downtown/lacroix/princebodyfinal.vmt
  -> vtmb:material:models/character/npc/unique/downtown/lacroix/princebodyfinal
```

## Coverage

A complete material GLB has zero `unresolved` and zero `unsupported` parameters. Validation compares
the selected VMT's shader, ordered parameter values, proxies, includes, texture roles, surface
property, and dependency identities with the semantic model reconstructed from the GLB.
