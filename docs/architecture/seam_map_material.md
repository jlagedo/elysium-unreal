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

### Patched map materials

SF-1.4. Every map's PAKFILE lump carries cubemap-patched copies of the materials it lights, one
per baked probe: a real `Patch` shader whose `include` names the base and whose `replace` block
overrides `$envmap` to the probe. Their identity is `vtmb:material:maps/<map>/<mat>_<x>_<y>_<z>`
(`seam_map_map.md` → "PAKFILE routing"), sourced from the BSP's own `bsp-pakfile` origin the same
way `seam_map_texture.md` sources the probe it binds to. Where the filename encodes a probe origin,
the extension states the join by name, independent of what the VMT's own shader happens to be:

```json
{
  "identity": {"asset": "vtmb:material:maps/sp_tutorial_1/plaster/socwndwd_-7831_3759_6441"},
  "patchOf": {"asset": "vtmb:material:plaster/socwndwd", "cubemapOrigin": [-7831, 3759, 6441]}
}
```

`patchOf` is `null` for a patched copy the compiler names for the map's `cubemapdefault` fallback
rather than a positioned probe (no coordinate suffix to join on); the base edge is still stated,
through `patch` (below) rather than `patchOf`. Every real corpus case is the literal `Patch`
shader, so the base material dependency the `patch` field already states and the one `patchOf`
would add are the same row; the decode adds it once. `$envmap`'s value is a concrete probe path
(`maps/<map>/c<x>_<y>_<z>` or `maps/<map>/cubemapdefault`), never the `env_cubemap` placeholder, so
it resolves the ordinary texture-binding way to the SF-1.3 texture unit:

```json
{
  "textureBindings": [
    {"parameter": "$envmap", "value": "maps/sp_tutorial_1/c-7831_3759_6441", "kind": "texture",
     "asset": "vtmb:texture:maps/sp_tutorial_1/c-7831_3759_6441", "resolved": true}
  ],
  "dependencies": [
    {"role": "material", "asset": "vtmb:material:plaster/socwndwd",
     "sourcePath": "materials/plaster/socwndwd.vmt"},
    {"role": "texture", "parameter": "$envmap",
     "asset": "vtmb:texture:maps/sp_tutorial_1/c-7831_3759_6441",
     "sourcePath": "materials/maps/sp_tutorial_1/c-7831_3759_6441.tth", "resolved": true}
  ]
}
```

Resolving that binding needs a map-aware `texture_exists`: the probe is a PAKFILE member, not an
install member, so the exporter's default (`materials/<path>.tth in index`) never sees it. The
patched material's own `export()` call substitutes one that also answers true for its map's own
PAKFILE `.tth` stems.

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
