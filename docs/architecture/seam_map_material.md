# Material GLB seam

This document defines one binary glTF 2.0 unit for one resolved VtMB VMT material. Format facts
remain owned by `docs/vtmb/texture_format.md`, `docs/vtmb/reflections.md`, and the material sections
of the owning VtMB topic documents.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> materials/<material>.vmt
  -> vtmb:material:<material>
  -> materials/<material>.glb
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
