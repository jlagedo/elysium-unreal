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

## Import

`uv run elysium import materials` turns every published material unit into one Unreal material
instance below `/ElysiumBaked/Materials`, parented to a small set of generated masters under
`/Game/ElysiumGenerated/Materials/V2`. It is the fourth slice of the seam migration
(`docs/project/seam_migration.md` → "Plan — surfaces track", phases 3 and 4) and it follows the
texture lane's shape exactly: an offline stage phase, a headless editor import phase, one
`manifest.json` between them, recipe stamps, a prune scope, and provenance carried on the asset.

Every count in this section is measured over the **11,624 install material units** under
`$ELYSIUM_EXPORT_V2_ROOT/materials/**` by `import/design/scan_materials_v2.py` (2026-08-31):
229 distinct parameter keys, 52 shader families, 54 resolved pixel+vertex program pairs, 25 proxy
kinds across 217 materials and 423 proxy instances, and zero units with an incomplete `coverage`.

SF-1.4's **7,501 patched map materials** landed under `materials/maps/**` the same day, taking the
corpus to 19,125 units. They add nothing to the three tables: one new key (`include`), no new proxy
kind, no new program pair. Each is a *delta* — `shader` is `patch`, `shaderResolution.family` is
`patch` with `resolved: false`, and the parameters are one `include` plus a `replace` or `insert`
block — so it takes its master from its base, not from a selector of its own (see "Patched map
materials" below). The three tables' counts stay stated over the install corpus, because that is
where the shader families, the proxies and the parameter vocabulary live.

**No silent drop.** Every key, every proxy kind and every program pair below has a named
destination. A unit carrying a key that is not in the parameter table, a proxy kind that is not in
the proxy table, or a resolved program pair that is not in the master inventory is a **stage
failure** with the unit named and the reason printed — never an instance written with the unknown
part quietly missing. That is the rule the three tables exist to make checkable.

### Identity and naming

```text
vtmb:material:<dir>/<stem>              ->  /ElysiumBaked/Materials/<dir>/MI_<safe stem>
vtmb:material:maps/<map>/<dir>/<stem>   ->  /ElysiumBaked/Materials/maps/<map>/<dir>/MI_<safe stem>
                                            (patched map materials, SF-1.4)
                                        ->  /Game/ElysiumGenerated/Materials/V2/M_V2_<Family>
                                            (the masters, tracked, hand-built by SF-4.3)
```

`<dir>` is the unit's directory below `materials/`, kept as package folders, exactly as the texture
lane keeps them; `<safe stem>` is `asset_names.safe_name` over the file stem, the same function.
The unit's exact original path lives in the provenance, so the fold is never a loss. Two units that
fold to one asset path fail both by name rather than letting either win, and both paths go into
`keep` so a previously good asset is not pruned. The legacy `/ElysiumBaked/Shared/Materials` and
the per-map material packages are untouched by this lane; SF-6.1 and SF-7.1 retire them.

The masters live under `Content/ElysiumGenerated/Materials/V2/` (package root
`/Game/ElysiumGenerated/Materials/V2`) — tracked content, generated by `make_v2_materials.py`,
beside and not over the legacy `make_*_materials.py` set.

**Class index and physical material** come from one resolution, run once per unit (owner rule,
`seam_migration.md` 2026-08-31):

1. `$surfaceprop`, case-folded — **4,605 units**. `defualt` (10) folds to `default`; a name the
   63-entry table does not define (`cloth` 30, `bone` 9, `asphalt` 2, `leather` 1, …) falls back to
   `default`, per `docs/vtmb/surface_properties.md`.
2. otherwise the VMT's **top directory** when it names a surface class — **558 units**:
   `plaster` 124, `wood` 93, `stone` 70, `blends` 55, `brick` 36, `ground` 36, `metal` 35,
   `tile` 29, `drapery` 28, `carpet` 21, `cable` 12, `glass` 10, `grates` 5, `asphalt` 2,
   `water` 2.
3. otherwise the **shader family's default row** — the remaining **6,461 units**.

The resolved name drives two things: `PhysMaterial` on the instance, pointing at the Phase-2 asset
`/ElysiumBaked/SurfaceProperties/PM_<name>`; and the scalar `SurfaceClassIndex`, the row the
master reads out of the class lookup texture that `UElysiumSurfaceCalibration` regenerates
(SF-4.1). One key, two consumers, so a surface's sound and its shine can never disagree.

#### Patched map materials

A patched unit's parent is **its base instance, not a master**. `patch.asset` resolves to
`vtmb:material:<dir>/<stem>`, whose `MI_` already carries the base's master, switches and
parameters; the patched `MI_` is a second-level instance that overrides only what its
`replace`/`insert` blocks name. That is exactly VtMB's own `include` semantics, expressed in the
one Unreal mechanism that has the same shape, and it means the 7,501 patched instances cost 7,501
override sets rather than 7,501 full parameter sets. All 7,499 base references in the corpus
resolve; the 2 units with no operation at all patch nothing and are pure aliases.

What the patches actually touch, over 7,501 units: **7,448 rows set `$envmap`** to the map's probe
(6,253 `replace`, 1,195 `insert`; 4,948 positioned `c<x>_<y>_<z>` probes and 2,500 the map's
`cubemapdefault`), 49 insert `$waterdepth`, 17 replace `$crackmaterial`, 2 replace
`$bottommaterial`. Because a probe's pixels are never sampled, a unit whose only patched parameter
is `$envmap` overrides **nothing** on the material — it exists so `vtmb:material:maps/<map>/…`
resolves to an asset for SF-6.1, and it records `PatchOf`, `EnvMapAssetId` and `EnvMapProbePath` in
provenance. The 68 rows that are not `$envmap` are the only ones that change a parameter.

### Master inventory (SF-3.1)

Nine masters. The split is by Unreal *material-only* property — shading model, translucency
lighting mode, refraction, the decal blend — because those cannot be overridden on an instance.
Blend mode, two-sidedness and the opacity-mask clip value **are** per-instance
(`FMaterialInstanceBasePropertyOverrides`), so they do not multiply masters; that is why there is
no separate "Additive" master, and why `M_V2_Lit` covers opaque and masked alike.

| Master | Shading model | Default blend | Two-sided | Covers | Materials |
|---|---|---|---|---|---|
| `M_V2_Lit` | Default Lit | Opaque | off | `lightmappedgeneric`, `vertexlitgeneric`, `teeth`, `cable` — opaque and masked | 7,925 |
| `M_V2_LitTranslucent` | Default Lit, `TLM_SurfacePerPixelLighting` | Translucent | off | the same four families plus `shatteredglass` — translucent and additive | 1,113 |
| `M_V2_Unlit` | Unlit | Opaque | off | `unlitgeneric`, `cloud` | 1,870 |
| `M_V2_Eyes` | Default Lit | Opaque | off | `eyes` | 406 |
| `M_V2_Water` | Default Lit | Translucent | off | `water` | 24 |
| `M_V2_Sprite` | Unlit, `bDisableDepthTest` | Translucent | on | `sprite` | 62 |
| `M_V2_Refract` | Default Lit, refraction enabled | Translucent | off | `refract`, `heatglow` | 11 |
| `M_V2_Decal` | Unlit | Modulate | off | `decalmodulate` | 38 |
| `M_V2_TwoTexture` | Default Lit | Opaque | off | `unlittwotexture`, `worldvertextransition`, `worldtwotextureblend` | 95 |

11,544 of 11,624 units take a master. The remaining **80** are debug and tool families listed at
the end of this section as "no master, provenance only".

Per-instance overrides, from the VMT, in this order (first match wins):

| VMT state | `BlendMode` | Units |
|---|---|---|
| `$additive` non-zero | `BLEND_Additive` | 250 |
| `$translucent` non-zero | `BLEND_Translucent` | 2,241 |
| `$alphatest` or `$alphatested` non-zero | `BLEND_Masked`, `OpacityMaskClipValue` 0.5 *(provisional owner call)* | 184 |
| family `decalmodulate` | `BLEND_Modulate` | 38 |
| otherwise | `BLEND_Opaque` | 8,911 |

The counts are exclusive and sum to 11,624; the keys themselves overlap. `$translucent` is authored
non-zero on 2,393 units, 151 of which also set `$additive`; `$alphatest`/`$alphatested` on 231, 47
of which also set a mode above them.

`$nocull` non-zero sets `TwoSided` (588 units). `$translucent` carries three authored values —
`1` (2,390), `0` (369) and `2` (2); any non-zero is translucent and the `2` is recorded as an
anomaly rather than read as a mode.

#### Program pairs to masters

One row per resolved pixel+vertex pair, all 54. `Mats` is distinct materials that admit the pair —
a material with both an `overbright==2` row and a default row appears in two rows, and the
bump-mapping pass-1 rows are a second draw of a material that also appears in a pass-0 row. The
cited unit is `vtmb:shader-source:<stem>` under `shader-programs/source/`; `compiled-only` means the
install ships no readable `.psh` and the reference is `vtmb:shader-program:psh/<stem>` or
`fxc/<stem>`. Every vertex program is compiled-only (`vsh/`), so the column names the pixel side.

| Pixel + vertex program | Mats | Master | Switches set | Source unit |
|---|---|---|---|---|
| `VertexLitGeneric` + `VertexLitTexture` | 3852 | Lit | (none) | `vertexlitgeneric` |
| `LightmappedGeneric` + `LightmappedGeneric` | 2306 | Lit | (none) | `lightmappedgeneric` |
| `UnlitGeneric` + `UnlitGeneric_VertexColor` | 1411 | Unlit | `UseVertexColor` | `unlitgeneric` |
| `VertexLitGeneric_MaskedEnvmapV2` + `VertexLitEnvMappedTexture` | 1132 | Lit | `UseEnvMap`, `UseEnvMapMask` | `vertexlitgeneric_maskedenvmapv2` |
| `LightmappedGeneric_MaskedEnvMapV2` + `LightmappedGeneric_EnvMap` | 692 | Lit | `UseEnvMap`, `UseEnvMapMask` | `lightmappedgeneric_maskedenvmapv2` |
| `Eyes_Overbright2` + `Eyes` | 394 | Eyes | (none) | `eyes_overbright2` |
| `Eyes` + `Eyes` | 394 | Eyes | (none) | `eyes` |
| `UnlitGeneric` + `UnlitGeneric` | 375 | Unlit | (none) | `unlitgeneric` |
| `LightmappedGeneric_SelfIlluminatedMaskedEnvMapV2` + `LightmappedGeneric_EnvMap` | 352 | Lit | `UseSelfIllum`, `UseEnvMap`, `UseEnvMapMask` | `lightmappedgeneric_selfilluminatedmaskedenvmapv2` |
| `VertexLitGeneric_SelfIlluminated` + `VertexLitTexture` | 302 | Lit | `UseSelfIllum` | `vertexlitgeneric_selfilluminated` |
| `VertexLitGeneric_SelfIlluminatedMaskedEnvmapV2` + `VertexLitEnvMappedTexture` | 152 | Lit | `UseSelfIllum`, `UseEnvMap`, `UseEnvMapMask` | `vertexlitgeneric_selfilluminatedmaskedenvmapv2` |
| `VertexLitGeneric_EnvmapV2` + `VertexLitEnvMappedTexture` | 118 | Lit | `UseEnvMap` | `vertexlitgeneric_envmapv2` |
| `LightmappedGeneric_SelfIlluminated` + `LightmappedGeneric` | 108 | Lit | `UseSelfIllum` | `lightmappedgeneric_selfilluminated` |
| `VertexLitGeneric_EnvmappedBumpmapV2_MultByAlpha` + `VertexLitGeneric_EnvmappedBumpmap_NoLighting` | 95 | Lit (pass 1) | `UseEnvMap`, `UseNormalMap`, `UseNormalMapAlphaEnvMapMask` | `vertexlitgeneric_envmappedbumpmapv2_multbyalpha` |
| `VertexLitGeneric_EnvmappedBumpmapV2_MultByAlpha_ps14` + `VertexLitGeneric_EnvmappedBumpmap_NoLighting_ps14` | 95 | Lit (pass 1) | same | compiled-only (`fxc/`) |
| `SpriteRenderNormal` + `unlitgeneric` | 60 | Sprite | (none) | compiled-only |
| `LightmappedGeneric_EnvMapV2` + `LightmappedGeneric_EnvMap` | 52 | Lit | `UseEnvMap` | `lightmappedgeneric_envmapv2` |
| `UnlitGeneric_NoTexture` + `UnlitGeneric_VertexColor` | 24 | Unlit | `UseBaseTexture` off, `UseVertexColor` | `unlitgeneric_notexture` |
| `WaterRefract_old` + `WaterWarp_old` | 23 | Water | (none) | compiled-only |
| `WaterRefract_ps20_old` + `Water_vs20_old` | 23 | Water | (none) | compiled-only |
| `UnlitGeneric_EnvMapMask` + `UnlitGeneric_EnvMap` | 23 | Unlit | `UseEnvMap`, `UseEnvMapMask` | `unlitgeneric_envmapmask` |
| `UnlitGeneric_NoTexture` + `UnlitGeneric` | 21 | Unlit | `UseBaseTexture` off | `unlitgeneric_notexture` |
| `WaterReflect_old` + `WaterWarp_old` | 19 | Water (pass 1) | (none) | compiled-only |
| `WaterReflect_ps20_old` + `Water_vs20_old` | 19 | Water (pass 1) | (none) | compiled-only |
| `VertexLitGeneric_SelfIlluminatedEnvmapV2` + `VertexLitEnvMappedTexture` | 17 | Lit | `UseSelfIllum`, `UseEnvMap` | `vertexlitgeneric_selfilluminatedenvmapv2` |
| `Eyes_Vampire_Overbright2` + `Eyes` | 12 | Eyes | `VampireEyes` | compiled-only |
| `Eyes_Vampire` + `Eyes` | 12 | Eyes | `VampireEyes` | compiled-only |
| `LightmappedGeneric_SelfIlluminatedEnvMapV2` + `LightmappedGeneric_EnvMap` | 10 | Lit | `UseSelfIllum`, `UseEnvMap` | `lightmappedgeneric_selfilluminatedenvmapv2` |
| `VertexLitTexture_Overbright2` + `Teeth` | 8 | Lit | (none) | `vertexlittexture_overbright2` |
| `VertexLitTexture` + `Teeth` | 8 | Lit | (none) | `vertexlittexture` |
| `LightmappedGeneric_BaseAlphaMaskedEnvMapV2` + `LightmappedGeneric_EnvMap` | 7 | Lit | `UseEnvMap`, `UseBaseAlphaEnvMapMask` | `lightmappedgeneric_basealphamaskedenvmapv2` |
| `UnlitTwoTexture` + `UnlitTwoTexture` | 5 | TwoTexture | (none) | compiled-only |
| `UnlitGeneric_EnvMapNoTexture` + `UnlitGeneric_EnvMap` | 3 | Unlit | `UseBaseTexture` off, `UseEnvMap` | `unlitgeneric_envmapnotexture` |
| `LightmappedGeneric` + `LightmappedGeneric_VertexColor` | 3 | Lit | `UseVertexColor` | `lightmappedgeneric` |
| `UnlitGeneric_EnvMapNoTexture` + `UnlitGeneric_EnvMapSphere` | 3 | Unlit | `UseBaseTexture` off, `UseEnvMap` | `unlitgeneric_envmapnotexture` |
| `VertexLitGeneric_EnvmappedBumpmapV2` + `VertexLitGeneric_EnvmappedBumpmap_NoLighting` | 3 | Lit (pass 1) | `UseEnvMap`, `UseNormalMap` | `vertexlitgeneric_envmappedbumpmapv2` |
| `VertexLitGeneric_EnvmappedBumpmapV2_ps14` + `VertexLitGeneric_EnvmappedBumpmap_NoLighting_ps14` | 3 | Lit (pass 1) | same | compiled-only (`fxc/`) |
| `LightmappedGeneric_SelfIlluminatedMaskedEnvMapV2` + `LightmappedGeneric_EnvMapVertexColor` | 3 | Lit | `UseSelfIllum`, `UseEnvMap`, `UseEnvMapMask`, `UseVertexColor` | `lightmappedgeneric_selfilluminatedmaskedenvmapv2` |
| `UnlitGeneric_EnvMapMask` + `UnlitGeneric_EnvMapVertexColor` | 3 | Unlit | `UseEnvMap`, `UseEnvMapMask`, `UseVertexColor` | `unlitgeneric_envmapmask` |
| `SpriteRenderTransColor` + `unlitgeneric_vertexcolor` | 2 | Sprite | `UseVertexColor` | compiled-only |
| `LightmappedGeneric_MaskedEnvMapV2` + `LightmappedGeneric_EnvMapSphere` | 2 | Lit | `UseEnvMap`, `UseEnvMapMask` | `lightmappedgeneric_maskedenvmapv2` |
| `VertexLitGeneric_NoTexture` + `VertexLitTexture` | 2 | Lit | `UseBaseTexture` off | `vertexlitgeneric_notexture` |
| `LightmappedGeneric_BaseAlphaMaskedEnvMapV2` + `LightmappedGeneric_EnvMapSphere` | 2 | Lit | `UseEnvMap`, `UseBaseAlphaEnvMapMask` | `lightmappedgeneric_basealphamaskedenvmapv2` |
| `UnlitGeneric_BaseAlphaMaskedEnvMap` + `UnlitGeneric_EnvMap` | 2 | Unlit | `UseEnvMap`, `UseBaseAlphaEnvMapMask` | `unlitgeneric_basealphamaskedenvmap` |
| `WaterCheap_ps11` + `WaterCheap_vs11` | 1 | Water | `CheapWater` | compiled-only |
| `WaterCheap_ps20_old` + `WaterCheap_vs20_old` | 1 | Water | `CheapWater` | compiled-only |
| `UnlitGeneric_EnvMap` + `UnlitGeneric_EnvMap` | 1 | Unlit | `UseEnvMap` | `unlitgeneric_envmap` |
| `UnlitGeneric_EnvMapNoTexture` + `UnlitGeneric_EnvMapCameraSpace` | 1 | Unlit | `UseBaseTexture` off, `UseEnvMap` | `unlitgeneric_envmapnotexture` |
| `LightmappedGeneric_NoTexture` + `LightmappedGeneric` | 1 | Lit | `UseBaseTexture` off | `lightmappedgeneric_notexture` |
| `LightmappedGeneric_EnvmapNoTexture` + `LightmappedGeneric_EnvMap` | 1 | Lit | `UseBaseTexture` off, `UseEnvMap` | `lightmappedgeneric_envmapnotexture` |
| `LightmappedGeneric_MaskedEnvmapNoTexture` + `LightmappedGeneric_EnvMap` | 1 | Lit | `UseBaseTexture` off, `UseEnvMap`, `UseEnvMapMask` | `lightmappedgeneric_maskedenvmapnotexture` |
| `LightmappedGeneric_MaskedEnvMapV2` + `LightmappedGeneric_EnvMapSphereVertexColor` | 1 | Lit | `UseEnvMap`, `UseEnvMapMask`, `UseVertexColor` | `lightmappedgeneric_maskedenvmapv2` |
| `LightmappedGeneric_EnvMapV2` + `LightmappedGeneric_EnvMapSphere` | 1 | Lit | `UseEnvMap` | `lightmappedgeneric_envmapv2` |
| `UnlitGeneric_BaseAlphaMaskedEnvMap` + `UnlitGeneric_EnvMapSphere` | 1 | Unlit | `UseEnvMap`, `UseBaseAlphaEnvMapMask` | `unlitgeneric_basealphamaskedenvmap` |

"Lit" is `M_V2_Lit` or `M_V2_LitTranslucent` by the blend table above. `Lit (pass 1)` and
`Water (pass 1)` are second draws of a material whose pass 0 has its own row; see "Two passes" and
"Water" below for what happens to the second pass.

#### The eight real unresolved families

These have no transcribed selector (`docs/vtmb/shader_combos.md` → "Families whose selector could
not be transcribed" and "DecalModulate is not in the shipped DX8 module") but they are real content
and they do get a master. Where a readable `.psh` of the family's own name ships, it is cited even
though the selector that would pick it was not recovered.

| Family | Mats | Master | Rule | Source unit |
|---|---|---|---|---|
| `worldvertextransition` | 89 | TwoTexture | lightmap alpha lerps `$basetexture`↔`$basetexture2`; `InitShaderParams` deletes `$envmap` without `$bumpmap` and deletes it outright under `$envmapsphere`, so `UseEnvMap` is forced off on this master | `worldvertextransition` |
| `decalmodulate` | 38 | Decal | the string is absent from `stdshader_dx8.dll`; the engine falls back to `FindShader("wireframe")`, so these 38 never drew what they name. Imported as `BLEND_Modulate` on the base texture — a **named deliberate divergence** from retail, which drew them as wireframe *(provisional owner call)* | none |
| `refract` | 9 | Refract | `$refracttexture` is `_rt_WaterRefraction`, a render target with no Unreal twin; the master uses Unreal `Refraction` driven by `$dudvmap`/`$normalmap` and `RefractAmount` | none (`waterrefract` is the shape) |
| `cable` | 9 | Lit | the program is a hemispheric `dp3` of a normal map against one directional light — under Lumen that is an ordinary normal-mapped lit surface, so it takes `M_V2_Lit` with `UseNormalMap` and no bespoke master | `cable` |
| `shatteredglass` | 2 | LitTranslucent | `$crackmaterial` names a second material; recorded as a provenance material reference, not sampled | none |
| `cloud` | 2 | Unlit | `$cloudalphatexture` drives opacity, `$cloudscale` the UV scale | none |
| `heatglow` | 2 | Refract | distortion only; no base colour | none |
| `worldtwotextureblend` | 1 | TwoTexture | same slot layout as `worldvertextransition` | none |

#### No master, provenance only

80 units in 36 families. They are imported as `MI_` instances parented to `M_V2_Unlit` with
`UseBaseTexture` set from `$basetexture` when one is bound, and every parameter recorded in
provenance; nothing else about them is reproduced, and none is reachable from shipped gameplay
content that the map, character or wield lanes consume.

`wireframe` 18, `desaturatespotlight` 11, `screenfeedback` 6, `debugluxels` 4, `shadow` 3,
`vertexlitgeneric_dx6` 3, `modulate` 2, `depthonly` 2, `volumetricfog` 2,
`basetimeslightmaptimesdetail` 2, `worlddiffusebumpmap` 2, and 25 single-material families:
`basetimeslightmapwet`, `basetimesmod2xenvmap`, `bumpmappedenvmap`, `burnpeel`, `camo`,
`debugfbtexture`, `debuglightingonly`, `debugmodifyvertex`, `epicoverlay`, `eyeball`,
`gooinglass`, `internalframesync`, `lightmappedenvmappedbumpmappedtexture`,
`lightmappedmaskedenvmappedbumpmappedtranslucenttexture`, `particlesphere`, `redvision`,
`refractparticle`, `shadowbuild`, `shadowmodel`, `skyfog`, `translucentlightmap`,
`unlitgeneric_dx6`, `vertexdiffuse`, `vertexnormals`, `watersurfacebottom`.

#### Exposed parameters, by master

The binding contract. Stage (SF-4.4) writes exactly these names; the masters (SF-4.3) expose
exactly these names; a name on one side and not the other is a build error, not a silent default.
`T` texture, `S` scalar, `V` vector, `#` static switch. Every master additionally exposes
`SurfaceClassIndex` (S) and reads `MPC_ElysiumSurfaces` for the knobs in "Knob contract".

| Master | Textures | Scalars | Vectors | Static switches |
|---|---|---|---|---|
| `M_V2_Lit`, `M_V2_LitTranslucent` | `BaseTexture`, `Detail`, `NormalMap`, `EnvMapMask`, `EnvMap` | `Alpha`, `SelfIllumAmount`, `DetailScale`, `EnvMapMaskScale`, `BumpScale`, `MinLight`, `MaxLight`, `FrameRate`, `ScrollRateU`, `ScrollRateV`, `SineMin`, `SineMax`, `SinePeriod`, `SineTimeOffset`, `WetnessScale`, `DecalDepthOffset` | `Color`, `SelfIllumTint`, `EnvMapTint`, `TexScaleOffset` | `UseBaseTexture`, `UseDetail`, `UseNormalMap`, `UseSelfIllum`, `UseVertexColor`, `UseVertexAlpha`, `UseEnvMap`, `UseEnvMapMask`, `UseBaseAlphaEnvMapMask`, `UseNormalMapAlphaEnvMapMask`, `UseFixedCube`, `MetallicTint`, `UseAnimatedFrames`, `UseScroll`, `IsDecalSurface` |
| `M_V2_Unlit` | `BaseTexture`, `Detail`, `EnvMap`, `EnvMapMask`, `CloudAlphaTexture` | `Alpha`, `DetailScale`, `EnvMapMaskScale`, `FrameRate`, `ScrollRateU`, `ScrollRateV`, `SineMin`, `SineMax`, `SinePeriod`, `SineTimeOffset`, `CloudScale` | `Color`, `EnvMapTint`, `TexScaleOffset` | `UseBaseTexture`, `UseDetail`, `UseVertexColor`, `UseVertexAlpha`, `UseEnvMap`, `UseEnvMapMask`, `UseBaseAlphaEnvMapMask`, `UseFixedCube`, `MetallicTint`, `UseAnimatedFrames`, `UseScroll`, `UseCloudAlpha` |
| `M_V2_Eyes` | `BaseTexture`, `Iris`, `Glint` | `Alpha`, `IrisFrame` | `Color` | `VampireEyes`, `UseGlint` |
| `M_V2_Water` | `DuDvMap`, `NormalMap`, `BottomMaterial`, `EnvMap` | `RefractAmount`, `ReflectAmount`, `WaterDepth`, `WaterMurkiness`, `WaterBaseFactor`, `WaterBaseMovementDist`, `WaterBaseMovementFreq`, `WaterSpecularMin`, `WaterSpecularMax`, `WaterTimeFreq1`, `WaterTimeFreq2`, `WaterWaveHeight`, `WaterWaveLength`, `CheapWaterStartDistance`, `CheapWaterEndDistance`, `FogStart`, `FogEnd` | `WaterColor`, `RefractTint`, `ReflectTint`, `FogColor`, `EnvMapTint` | `CheapWater`, `UseFogEnable`, `UseEnvMap`, `UseFixedCube` |
| `M_V2_Sprite` | `BaseTexture` | `Alpha`, `FrameRate`, `SpriteRenderMode` | `Color`, `SpriteOrigin` | `UseVertexColor`, `UseAnimatedFrames` |
| `M_V2_Refract` | `DuDvMap`, `NormalMap`, `BaseTexture` | `RefractAmount`, `Alpha` | `RefractTint`, `Color` | `UseBaseTexture`, `ForceRefract` |
| `M_V2_Decal` | `BaseTexture` | `Alpha`, `DecalDepthOffset` | `Color` | `UseVertexColor` |
| `M_V2_TwoTexture` | `BaseTexture`, `BaseTexture2`, `NormalMap` | `Alpha`, `Texture2Scale`, `ScrollRateU`, `ScrollRateV` | `Color`, `TexScaleOffset`, `Texture2Offset` | `UseBaseTexture2`, `UseNormalMap`, `UseVertexColor`, `UseVertexAlpha`, `UseScroll` |

Two texture-slot rules, because the same VMT key means different things per family:

- On `M_V2_Lit`/`M_V2_Unlit`/`M_V2_TwoTexture`, `$bumpmap` **is** the tangent normal map and binds
  `NormalMap` (294 units).
- On `M_V2_Water` and `M_V2_Refract`, `$bumpmap`'s own registration comment is *"dudv bump map"*,
  so it binds `DuDvMap`, and `$normalmap` binds `NormalMap` (`docs/vtmb/shader_combos.md` → Water).

Every texture parameter is bound to the slice-2 asset for its unit, choosing the **`_linear`
twin** when this material's binding is a data read and the texture also has a colour binding
elsewhere — the twin exists exactly for that case (`seam_map_texture.md` → "Role, sRGB and
compression"). Data-class slots here are `NormalMap`, `DuDvMap`, `EnvMapMask`; colour-class slots
are `BaseTexture`, `BaseTexture2`, `Detail`, `Iris`, `EnvMap`, `CloudAlphaTexture`.

### Post-lighting math per master

What each master transcribes, and what it does not. In every VtMB program `v0` is the interpolated
vertex lighting (models) and `t1` is the lightmap (world); **both are Lumen's** and neither is
transcribed. `c0` is `overbrightFactor/2` and reaches the master only as the `Overbright` knob.
`c1` is the self-illum tint, `c2` the `$envmaptint`, `c3` the per-draw modulation colour
(`$color` × `$alpha`).

**`M_V2_Lit` / `M_V2_LitTranslucent`** — `lightmappedgeneric.psh`, plus the envmap and self-illum
spellings. The plain world program is the whole base case:

```text
tex t0
tex t1
mul r0, t0, v0          ; base times vertex color (with alpha)
mul r0.rgb, t1, r0      ; fold in lightmap (color only)
mul_x2 r0.rgb, c0, r0   ; * 2 * (overbrightFactor/2)
```

`t0` → `BaseTexture` into Base Color. `v0` on the world path is the vertex colour, gated by
`UseVertexColor`; on the model path (`vertexlitgeneric_maskedenvmapv2.psh`, `mul r0.rgb, v0, r0
; apply vertex lighting`) the same register is the *lighting* and is dropped. `t1` and `mul_x2 c0`
are dropped: Unreal's lighting replaces them and the overbright factor becomes a single global
`Overbright` knob applied to Base Color, not a per-material constant.

Self-illum, from `lightmappedgeneric_selfilluminated.psh`, is transcribed exactly:

```text
mul r1, c1, t0              ; Self illum * tint
lrp r0.rgb, t0.a, r1, r0    ; Blend between self-illum + base * lightmap
```

`t0.a` is the emission mask, `c1` is `SelfIllumTint`; the `lrp` becomes Emissive =
`BaseTexture.rgb * SelfIllumTint * BaseTexture.a * SelfIllumAmount`, and the same `t0.a` is
removed from the opacity path so a self-illum material is not accidentally masked. In the masked
V2 spelling (`lightmappedgeneric_selfilluminatedmaskedenvmapv2.psh`) the tint is folded into the
lighting term instead (`mad r1, t0, r1, t1`); the two agree once the lightmap leaves, which is
why one Emissive expression covers both.

The envmap term is the reflection contract below; the `mad r0.rgb, r1, c2, r0` that adds it is
**not** transcribed as an additive colour except for the fixed-cube case.

**Two passes.** `vertexlitgeneric_envmappedbumpmapv2.psh` is the whole of pass 1:

```text
tex t0
texm3x3pad  t1, t0_bx2
texm3x3pad  t2, t0_bx2
texm3x3vspec t3, t0_bx2
mul r0.rgb, t3, c0
mov r0.a, c0.a
```

and `_multbyalpha` adds `mul r0.rgb, t0.a, r0`. This is "sample the cube through the tangent
normal at the reflected eye vector, times a constant, optionally times the normal map's alpha".
Under Lumen that is *exactly* what a normal-mapped reflective surface already does, so pass 1 is
**replaced, not transcribed**: it collapses into `UseNormalMap` on the pass-0 instance, and
`_MultByAlpha` sets `UseNormalMapAlphaEnvMapMask` so the normal map's alpha feeds the mask term.
No second material, no second draw. 98 materials.

**`M_V2_Unlit`** — `unlitgeneric.psh` is `tex t0 / mul r0, t0, v0`: Base Color goes straight to
Emissive, `v0` is the vertex colour under `UseVertexColor`. `unlitgeneric_envmapmask.psh` shows the
unlit envmap add explicitly, `mul r0.rgb, t1, t2 / mul r0.rgb, c2, r0 / mad r0.rgb, t0, v0, r0` —
an unlit surface has no lighting for Lumen to replace, so on this master the cube term **is**
transcribed as the literal additive Emissive of the reflection contract whenever a cube exists.

**`M_V2_Eyes`** — `eyes.psh`:

```text
tex t0
tex t1
tex t2
lrp r0, t1.a, t1, t0      ; Blend in the iris with the background
mad r0.rgb, r0, v0, t2 +  ; Modulate by the illumination, add in the glint
mov r0.a, t0.a
```

The `lrp` is transcribed (`BaseTexture` ← eyeball, `Iris` ← `$iris`, alpha of the iris is the
blend); `v0` is lighting and is dropped; `t2` (`$glint`, bound at stage 2) is additive Emissive.
`$glint` is set by **no** shipped material, so `UseGlint` is off on all 406 and the slot exists for
completeness. `eyes_overbright2.psh` differs only by `mul_x2 r0, v0, r0` — an overbright variant of
a term Lumen owns — so the two rows collapse to one instance, and the `Eyes_Vampire*` pair (12
materials, compiled-only) sets `VampireEyes`.

**`M_V2_Water`** — the shipped pair is compiled-only, but `waterrefract.psh` and `waterreflect.psh`
carry the shape. Refract is `texm3x2pad`/`texm3x2tex` through a DUDV map into a screen render
target times `c1`; reflect adds the quintic Fresnel:

```text
dp3_sat r1.rgba, v0_bx2, t3_bx2
mul r0.a, 1-r1.a, 1-r1.a   ; squared
mul r0.a, r0.a, r0.a       ; quartic
mul r0.a, r0.a, 1-r1.a     ; quintic
mad r0.a, r0.a, 1-c3.a, c3.a
mul r0, r0.a, t2
```

Both render-target passes are **replaced**: `_rt_WaterRefraction` and `_rt_WaterReflection` become
Unreal `Refraction` and Lumen reflection on one translucent surface, with the authored constants
(`$waterdepth`, `$watermurkiness`, `$waterwaveheight`, …) carried as scalars driving the same
shapes. The Fresnel is the one place VtMB *has* one, so it is transcribed as Unreal's default
Fresnel rather than flattened. `$forcecheap` sets `CheapWater`, which drops the refraction pass.

**`M_V2_Sprite`** — the `SpriteRender*` programs are compiled-only. The family's whole variation is
blend state chosen by `$spriterendermode`, and only 2 of the 62 materials declare it (mode 8);
60 default to mode 0. Base Color → Emissive, `Color` × vertex colour, blend from the table above.

**`M_V2_Refract`** — no shipped source and no transcribed selector; the master is Unreal
`Refraction` from `$dudvmap`/`$normalmap` scaled by `RefractAmount`, tinted by `RefractTint`.
Stated as a reconstruction, not a transcription.

**`M_V2_Decal`** — no program at all (the 38 materials fell back to `wireframe` in retail).
`BLEND_Modulate` of `BaseTexture` over the receiver, `DecalDepthOffset` from the knob.

**`M_V2_TwoTexture`** — `worldvertextransition.psh` is transcribed except for its lighting:

```text
tex t0 ; basetexture
tex t1 ; basetexture2
tex t2 ; lightmap
texkill t3
mov r0.a, 1-t2.a
lrp r0, r0.a, t1, t0
mul r0, r0, t2
mul_x2 r0.rgb, c0, r0
```

The `lrp` keyed on `1 - lightmapAlpha` is the blend and is kept, sourced from the mesh's vertex
colour alpha (`UseVertexAlpha`) since Unreal has no lightmap alpha; `mul r0, r0, t2` and
`mul_x2 c0` are Lumen's and the `Overbright` knob. `texkill t3` is the clip plane and is dropped.

### Reflection contract

From the Settled entries of 2026-08-31 — *probes are not reflection content*, *the matte-world
premise is repudiated*, *calibration happens on knobs*. Everything below is a knob or a switch;
no numbers are chosen here.

**`$envmap` presence means the surface is reflective** — nothing more. 2,610 of 11,624 units carry
it. It does not mean "add this image"; under Lumen the renderer supplies the image and VtMB's data
says only *how shiny* and *where*.

| `$envmap` value | Units | What the instance does |
|---|---|---|
| `env_cubemap` (the symbol) | 2,217 | no texture asset at all. The surface is reflective; the image comes from the `SphereReflectionCapture` actors SF-6.2 places at the map's own `cubemaps[]` origins. `UseFixedCube` off |
| `envmap/<name>` (authored fixed cube) | 342 | `UseFixedCube` on; `EnvMap` binds `/ElysiumBaked/Textures/envmap/TC_<name>`. An art choice of image, not a room |
| `shadertest/*`, `dev/*` | 49 | same as authored, but these units are all in the debug/tool set or shader-test content |
| `lib/redenv_skyref` | 2 | authored, treated as `envmap/` |
| `maps/<map>/…` (patched, SF-1.4) | 7,448 rows | the concrete `TC_maps/<map>/c…` asset id is **recorded on the instance's provenance** (`EnvMapAssetId`, plus a soft object path) and **never bound to a texture parameter**. Probes are renders of the 2004 lightmapped world; sampling them would put 2004 lighting into Lumen reflections. `UseFixedCube` off, and the patched instance inherits its base's reflection state unchanged |

**Masks decide roughness and specular**, per texel, and never opacity:

| Source | Units | Mask term |
|---|---|---|
| `$envmapmask` texture | 2,368 | the `_linear` twin of that texture, luma |
| `$normalmapalphaenvmapmask` | 136 | `NormalMap.a` — this is the pass-1 `_MultByAlpha` term |
| `$basealphaenvmapmask` | 14 | `1 - BaseTexture.a` — **inverted**, as `lightmappedgeneric_basealphamaskedenvmapv2.psh` writes it (`mul r1, t2, 1-t3.a`) |
| none of the three | 92 | constant 1 (uniform reflectivity) |

```text
mask      = <the row above> * EnvMapMaskScale
Roughness = lerp(MaskRoughnessMax, MaskRoughnessMin, mask)
Specular  = ClassSpecular * (1 + MaskSpecularScale * mask)
```

`MaskRoughnessMin`, `MaskRoughnessMax` and `MaskSpecularScale` are settings knobs
(`UElysiumSurfaceSettings` → `MPC_ElysiumSurfaces`); `ClassSpecular` is the class LUT row.
A bound `$envmapmask` overrides `$basealphaenvmapmask` and self-illum absorbs it, exactly as the
shipped selector tables do (`docs/vtmb/shader_combos.md`), so the stage sets at most one of the
three switches and records the loser as an anomaly rather than blending both.

**`$envmaptint` splits grey from chromatic.** 466 units author it, 2,146 `$envmap` units leave it
implicit white. The split is measured on `max(rgb) - min(rgb) <= 0.02` after normalising the two
authored forms (`[r g b]` 0–1, `{r g b}` 0–255): **362 grey, 104 chromatic**, with a clear gap
between the populations.

```text
grey       -> Specular *= luma(EnvMapTint) * EnvTintScale        ; MetallicTint off
chromatic  -> Metallic  = mask ; BaseColor *= EnvMapTint          ; MetallicTint on
```

The 104 chromatic tints are VtMB naming its own metals — `[0.65 0.5 0.0]` and `[1.0 0.7 0.0]`
brass and gold, `[0.74 0.57 0.31]` and `[0.52 0.36 0.25]` copper — plus a blue/teal set
(`[.5 .6 .9]`, `[0.3 0.6 1.0]`, `[.4 .8 .8]`) that is tinted glass rather than metal. This is a
hand-authored metal mask and it is read, never inferred. `EnvTintScale` is a settings knob.

**The authored fixed cube is the one literal sample.** For the 342 (+49 debug) `envmap/<name>`
units, and only for them, the master adds

```text
Emissive += TextureSampleParameterCube(EnvMap, reflect(V, N))
          * mask * EnvMapTint * FixedCubeStrength
```

behind a `Ray Tracing Quality Switch` that returns black for ray-traced and Lumen-card passes, so
a camera-dependent term cannot enter global illumination or secondary reflections. The Emissive
placement is `docs/vtmb/reflections.md`'s translation carried forward — it preserves the visible
additive term on a dark street, at the cost of not being VtMB's pre-lighting composite point
*(provisional owner call)*. `FixedCubeStrength` is a settings knob; SF-5.3 tunes it. The cube's
face order and handedness are the slice-2 `TC_` asset's, already proven headlessly.

**The sphere variant needs nothing.** `$envmapsphere` is set on 11 units and `$envmapcameraspace`
on 1; of those 12, ten are `shadertest/` or `dev/` and the remaining two are
`glass/breaksurf/break_glass_1` (which has no `$envmap` at all) and `break_glass_2`. So the
sphere-map projection has **two** shipped-content users, one of which is inert. Those two take the
ordinary reflection-vector path and the flag is recorded in provenance; no switch, no master, no
variant *(provisional owner call)*. `$envmapmode` (127 units: 84 `1`, 43 `0`) is read by **no**
transcribed selector and never co-occurs with `$envmapsphere` — it is a Source key this renderer
ignores, and it is provenance-only.

**Non-`$envmap` surfaces still reflect.** The other 9,014 units are not matte. They take
`DefaultSpecular`, `DefaultRoughness` and `DefaultMetallic` from the settings, modulated by their
class LUT row (`SurfaceClassIndex`). That is the whole of the repudiated three-zeroes rule's
replacement, and its values are the owner's to tune in SF-5.1/5.2.

### Parameter table (SF-3.2)

All 229 keys the corpus authors, grouped by destination. A key not in this table fails staging.
Counts are distinct materials.

**Texture parameter (24).** Each binds one slice-2 asset; the count is materials.
`$basetexture` 11492, `$envmapmask` 2392, `$iris` 407, `$envmap` 393 *(the concrete paths only —
the 2,217 `env_cubemap` symbols are not bindings)*, `$bumpmap` 294, `$basetexture2` 88,
`%tooltexture` 49, `$normalmap` 28, `$detail` 28, `$bottommaterial` 24, `$reflecttexture` 19,
`$refracttexture` 17, `$spotlightmask` 11, `$dudvmap` 9, `$texture2` 5, `$crackmaterial` 4,
`$masktexture` 2, `$cloudalphatexture` 2, `$antitexture` 1, `$burntexture` 1, `$detail2` 1,
`$dudvtexture` 1, `$fuzztexture` 1, `$glassenvmap` 1.
Two are **render targets**, not textures: `$reflecttexture` is always `_rt_WaterReflection` and
`$refracttexture` always `_rt_WaterRefraction`; both are replaced by Unreal's own passes and are
recorded, not bound. `$bottommaterial` (24) names a *material*, not a texture — all 24 are
unresolved bindings in the corpus and it becomes a provenance material reference.
`$crackmaterial` (4) and `$modelmaterial` (11) are the same shape.

**Scalar parameter (46).** `$alpha` 301, `$selfillum` 1240, `$bumpframe` 30, `$envmapmaskscale` 14,
`$detailscale` 8, `$detailscale2` 1, `$bumpscale` 3, `$minlight` 11,
`$maxlight` 11, `$refractamount` 26, `$reflectamount` 19, `$texture2scale` 1, `$texscale` 4,
`$tex2scale` 4, `$cloudscale` 2, `$curve` 3, `$contrast` 2, `$fogstart` 22, `$fogend` 22,
`$waterbasefactor` 5, `$waterbasemovementdist` 5, `$waterbasemovementfreq` 5, `$waterdepth` 5,
`$watermurkiness` 5, `$waterspecularmin` 5, `$waterspecularmax` 5, `$watertimefreq1` 5,
`$watertimefreq2` 5, `$waterwaveheight` 5, `$waterwavelength` 5, `$cheapwaterstartdistance` 2,
`$cheapwaterenddistance` 2, `$wave` 1, `$wetbrightnessfactor` 1, `$maxbrightlevel` 1,
`$spriterendermode` 2, `$subdivsize` 14, `$leakamount` 1, `$leakforce` 1, `$fuzzoffset` 1,
`$fuzzedgeopacity` 1, `$fuzzfaceopacity` 1, `$alpha_bias` 4, `$j_basescale` 4, `$halfwidth` 1,
`$mean` 1.

**Vector parameter (17).** `$envmaptint` 466, `$color` 349, `$refracttint` 22, `$fogcolor` 22,
`$reflecttint` 19, `$selfillumtint` 15, `$bumpoffset` 23, `$scale` 25, `$spriteorigin` 54,
`$watercolor` 5, `$texoffset` 4, `$tex2offset` 4, `$maskscale` 2, `$clampcolor` 1, `$maxcolor` 1,
`$leakcolor` 1, `$glassenvmaptint` 1.

**Static switch (13).** `$selfillum` 1240 *(also a scalar: the value is `1`, `0` or `0.2`, so it is
both the switch and `SelfIllumAmount`)*, `$vertexcolor` 1475, `$vertexalpha` 731, `$decal` 559
(`IsDecalSurface`), `$normalmapalphaenvmapmask` 146, `$basealphaenvmapmask` 14, `$vampire` 12,
`$envmapsphere` 11 *(recorded only, see the reflection contract)*, `$envmapcameraspace` 1 (same),
`$forcecheap` 2, `$bumpbasetexture2withbumpmap` 4, `$fogenable` 23, `$normalalphaenvmapmask` 1
*(a one-off misspelling of `$normalmapalphaenvmapmask`; recorded as an anomaly and treated as the
correct key)*.

**Master choice / instance property (10).** `$translucent` 2761, `$additive` 614, `$alphatest` 228,
`$nocull` 591, `$alphatested` 3 *(a misspelling of `$alphatest`, same treatment)*,
`$transparent` 3, `$translucency` 160 *(authored `0` on all 160; recorded, no effect)*,
`$ignorez` 1497, `$model` 198 *(world-vs-model draw, already folded into the resolved program)*,
`$flat` 9.
`$ignorez` sets no property in Phase 4: 1,473 of its 1,497 units are `hud/`, `fonts/`,
`interface/` and `vgui/` UI materials that SF-6.5 draws through UMG, where depth is not tested at
all; the 24 world uses are a listed divergence *(provisional owner call)*.

**Master choice, patched units only (1).** `include` — the 230th key, authored by every one of the
7,501 patched map units and by none of the 11,624 install units. It names the base material, which
is the patched instance's parent; it never becomes a material parameter.

**Physical material and class index (1).** `$surfaceprop` 4605, plus the derived top-directory and
family-default fallbacks described under "Identity and naming".

**Runtime bind (5).** Values a running game supplies, wired by `FElysiumMaterialFactory` in
SF-6.4: `$envmap` when its value is the literal `env_cubemap` (2,217), `$reflecttexture` (19),
`$refracttexture` (17), `$clientshader` 2 (`mouthshader` — the facial path owns it), and every
`resultvar` target of a runtime proxy (see the proxy table).

**Proxy-owned (25).** Keys that only ever appear inside a proxy block and are read by the proxy,
never by the shader: `resultvar` 116, `sinemin` 87, `sinemax` 87, `sineperiod` 87,
`animatedtexturevar` 71, `animatedtextureframenumvar` 71, `animatedtextureframerate` 71,
`texturescrollvar` 48, `texturescrollrate` 48, `texturescrollangle` 48, `scale` 32, `timeoffset` 25,
`srcvar1` 19, `srcvar2` 19, `translatevar` 10, `alpha` 8, `halfwidth` 5, `rate` 5, `mean` 4,
`greatervar` 4, `lessequalvar` 4, `rotatevar` 2, `maxval` 2, `minval` 2, `animationnowrap` 1,
plus the six that belong to a single proxy instance: `camoboundingboxmax`, `camoboundingboxmin`,
`camopatterntexture`, `surfaceprop` (inside `camo`), `direction` (inside `particlesphereproxy`),
`dummy` (inside `waterlod`), and `halfwidthvar`/`meanvar` (1 each, inside a malformed
`gaussiannoise` block).

**Proxy scratch registers (17).** Declared at top level so the proxy chain has somewhere to write,
never read by any program. They exist as VMT parameters and are recorded, but bind nothing:
`$one` 14, `$zero` 4, `$temp` 22, `$temp1` 4, `$temp2` 5, `$tempvec` 1, `$abstmp` 1,
`$a_b_noise` 4, `$a_s_noise` 4, `$j_b_noise` 4, `$j_s_noise` 4, `$xo_b_noise` 4, `$xo_s_noise` 4,
`$a_threshold` 4, `$j_threshold` 4, `$xo_threshold` 4, `$noisechoice` 1 — together with the
halfwidth constants they read (`$a_b_halfwidth`, `$a_s_halfwidth`, `$a_t_halfwidth`,
`$j_b_halfwidth`, `$j_s_halfwidth`, `$j_t_halfwidth`, `$xo_b_halfwidth`, `$xo_s_halfwidth`,
`$xo_t_halfwidth`, 4 each), which are the Nosferatu/Malkavian obfuscate noise set.

**Transform (5).** `$basetexturetransform` 17 is a Source transform *string*
(`center 0 0 scale 2 2 rotate 0 translate 0 0`); the stage parses it into `TexScaleOffset` and
records the string. `$translate` 1, `$noscale` 2, `$mod2x` 1 and `$keepcolor` 4 ride with it.

**Provenance only.** Recorded on the instance, consumed by nobody in the material lane.

- Map-compiler hints, owned by the map seam: `%compilepassbullets` 73, `%compilewater` 27,
  `%compilenodraw` 8, `%compiletrigger` 6, `%compilenonsolid` 4, and one each of `%compileclip`,
  `%compiledetail`, `%compilefog`, `%compilehint`, `%compileladder`, `%compilelightpass`,
  `%compilenovis`, `%compilenpcclip`, `%compilenpcopaque`, `%compileorigin`,
  `%compileplayercontrolclip`, `%compileshadowonly`, `%compileskip`, `%compilesky`,
  `%compilewanderclip`, `%compilewet`, plus the misspelled `$compilepassbullets` 1.
- Editor and tool keys: `%keywords` 27, `%detailtype` 37, `%notooltexture` 2, `%tooltexture` 49
  *(bound as a texture as well, since Hammer's preview image is a real texture unit)*.
- Renderer keys VtMB never implemented: `$envmapcontrast` 19 *(17× `1`, one `0.85`, one authored
  as a vector — no term for it appears in any shipped `.psh`)*, `$envmapmode` 127,
  `$desaturate` 2, `$modintensity` 3, `$blur` 4, `$soft` 1, `$multipass` 17, `$nooverbright` 3,
  `$no_fullbright` 431, `$nofog` 77, `$polyoffset` 2, `$trilinear` 2, `$nomip` 5 and the bare
  `nomip` 5, `$noclip` 1, `$noztest` 1, `$comparez` 1, `$writez` 1, `$decalscale` 561
  *(a decal-geometry scale the BSP exporter consumes, not a material value)*.
- Material references: `$bottommaterial` 24, `$crackmaterial` 4, `$modelmaterial` 11,
  `$leaknoise` 1.
- Keys authored at top level that belong inside a proxy block: `$animatedtexturevar` 1,
  `$animatedtextureframenumvar` 1, `$animatedtextureframerate` 1 — one VMT wrote the
  `animatedtexture` proxy's keys as `$`-parameters. Recorded as anomalies and read as the proxy
  keys, since that is plainly the intent.
- `$spriteorientation` 55 (`vp_parallel`, `parallel_upright`, `oriented`) — a billboard rule that
  belongs to the sprite *component*, not to a material; the sprite placement lane owns it.
- Unprefixed duplicates of a `$` key, authored at top level by mistake: `additive` 2,
  `translucent` 1, `selfillum` 1. Recorded as anomalies and **not** honoured, because VtMB's own
  parser would not have honoured them either.
- One malformed key, `"// added by psycho-a\n}"` 1 — an unofficial-patch comment that a broken
  quote turned into a key. It is one of the six `valueless-key` anomalies and is carried verbatim.

Seven keys appear in two groups because they genuinely have two destinations: `$envmap`,
`$reflecttexture` and `$refracttexture` are texture bindings *and* runtime binds; `%tooltexture`,
`$bottommaterial` and `$crackmaterial` are bindings *and* provenance; `$selfillum` is a scalar
*and* a static switch. Every other key of the 229 has exactly one destination.

**Value-type rules.** Values are always strings in the unit; `valueType` names the parse.

| Key shape | Rule |
|---|---|
| `$alpha`, `$selfillum`, `$detailscale`, `$maxlight`, `$minlight`, `$refractamount`, `$cheapwater*`, `$waterbasefactor`, `$watermurkiness`, `$waterspecularmax`, `$waterwaveheight` | int **or** float in the same key; parse as float. `$alpha` authors `1`, `0`, `0.5`, `0.50`, `0.0` |
| `[r g b]` | float triple, already 0–1 |
| `{r g b}` | integer triple, 0–255; divide by 255. Only `$fogcolor` uses it in this corpus (`{22 20 10}`), alongside `[.2 .4 .2]` in the same key |
| `[u v]` | float pair (`$scale`, `$bumpoffset`, `$texoffset`, `$spriteorigin`) |
| `$scale` | vector on 24 units, bare number on 1; a bare number splats to both components |
| `$selfillumtint` | vector on most, bare `1.0` on one, a string on one; a bare number splats to grey |
| `$envmapcontrast` | int on 17, float on 1, vector on 1 — provenance only, so the mixed type never reaches a parameter |
| `$decalscale` | float on 560, a string on 1; the string is an anomaly |
| flag keys | the value is the string `"0"` or `"1"`; **not** a JSON boolean. 87 keys in the corpus have only `0`/`1` values |

Six units carry a `valueless-key` anomaly, four an `unclosed-block-at-end-of-file`, two an
`anonymous-block`, one `content-after-shader-block` and one `unterminated-quoted-string`. All 14
still stage; the anomaly rides in provenance.

### Proxy policy (SF-3.3)

25 proxy kinds over 217 materials and 423 instances. `resultvar` names the parameter a proxy
writes, `srcvar1`/`srcvar2` name what it reads, and a chain is expressible in a material graph only
when every link's inputs are literals or other shader-time proxies **in the same material** and the
final `resultvar` is a parameter the master exposes.

| Proxy | Mats | Inst | Destination | Keys it reads | Notes |
|---|---|---|---|---|---|
| `sine` | 87 | 114 | shader-time: `Time` → `Sine` | `sinemin`, `sinemax`, `sineperiod`, `timeoffset`, `resultvar` | 37 distinct `resultvar` targets, led by `$alpha` 28, `$selfillumtint` 21, `$color[i]` 22, `$envmaptint[i]`. A `[i]` component target writes one channel of the vector parameter. A target of `$temp1`/`$temp2` is a chain link, not an output |
| `animatedtexture` | 72 | 72 | shader-time: frame index | `animatedtexturevar`, `animatedtextureframenumvar`, `animatedtextureframerate`, `animationnowrap` | `animatedtexturevar` is `$basetexture` / `$bumpmap` / `$normalmap` and `animatedtextureframenumvar` is `$frame` or `$bumpframe` — `$frame` is never declared as a VMT key, so the stage creates the scalar. The slot binds the slice-2 `TA_` array asset and `FrameRate` drives the slice index; `animationnowrap` (1) clamps instead of wrapping |
| `texturescroll` | 48 | 55 | shader-time: `Panner` | `texturescrollvar`, `texturescrollrate`, `texturescrollangle` | the stage precomputes `ScrollRateU = rate·cos θ`, `ScrollRateV = rate·sin θ`, so the master needs no trig |
| `texturetransform` | 12 | 16 | shader-time **when its inputs are** | `resultvar`, `translatevar`, `rotatevar` | writes `$basetexturetransform` (12) or `$texture2transform` (4) from another proxy's output. Expressible only when `translatevar`/`rotatevar` resolve to a shader-time source; otherwise runtime |
| `linearramp` | 5 | 5 | shader-time: `Time × rate` | `rate`, `resultvar` | targets `$tex2offset[1]` / `$texture2offset[1]` — a scrolling second layer |
| `add` | 7 | 7 | shader-time arithmetic | `srcvar1`, `srcvar2`, `resultvar` | |
| `subtract` | 14 | 18 | shader-time arithmetic | `srcvar1`, `srcvar2`, `resultvar` | |
| `multiply` | 2 | 3 | shader-time arithmetic | `srcvar1`, `srcvar2`, `resultvar` | |
| `abs` | 5 | 13 | shader-time arithmetic | `srcvar1`, `resultvar` | |
| `exponential` | 1 | 1 | shader-time arithmetic | `srcvar1`, `resultvar` | |
| `lessorequal` | 4 | 8 | **not expressible** | `srcvar1`, `srcvar2`, `greatervar`, `lessequalvar`, `resultvar` | a branch that selects between two *parameters* by name. A material `If` node compares values, not names, so this needs the runtime |
| `gaussiannoise` | 5 | 25 | **not expressible** | `mean`, `halfwidth`, `resultvar` | per-frame random. No material node gives a frame-stable random; runtime C++ or frozen to `mean` |
| `uniformnoise` | 2 | 3 | **not expressible** | `minval`, `maxval`, `resultvar` | same |
| `globalwetness` | 19 | 57 | runtime C++ | `resultvar`, `scale` | always three instances writing `$envmaptint[0..2]`; becomes one `EnvMapTint` write of `wetness × scale`, with `WetnessScale` the per-instance value (`0.56`, `1.0`, …) |
| `playerproximity` | 3 | 3 | runtime C++ | `resultvar`, `scale` | writes `$temp`, consumed by a `subtract` chain |
| `playerposition` | 4 | 4 | runtime C++ | `resultvar`, `scale` | writes `$temp`, feeds `texturetransform` |
| `playerspeed` | 6 | 6 | runtime C++ | `resultvar`, `scale` | writes `$temp`, feeds `sine`/`subtract` |
| `textconsole` | 4 | 4 | runtime C++ | — | the terminal screen; a runtime render target the terminal actor owns |
| `shadow` | 2 | 2 | runtime C++ | — | |
| `breakablesurface` | 2 | 2 | runtime C++ | — | pairs with `$crackmaterial` |
| `camo` | 1 | 1 | provenance only | `camopatterntexture`, `camoboundingboxmin`, `camoboundingboxmax`, `surfaceprop` | the one `camo`-family material |
| `waterlod` | 1 | 1 | provenance only | `dummy` | |
| `lampbeam` | 1 | 1 | provenance only | — | |
| `lamphalo` | 1 | 1 | provenance only | — | |
| `particlesphereproxy` | 1 | 1 | provenance only | `direction` | |

**Chains, honestly.** The largest chain in the corpus is the obfuscate noise set on four Nosferatu
armour materials: `abs + add + gaussiannoise + lessorequal + linearramp + sine + subtract +
texturetransform` in one material, threading `$a_b_noise`, `$a_s_noise`, `$a_threshold`, `$temp`
and `$abstmp` between eight proxies before writing a texture transform. It contains
`gaussiannoise` and `lessorequal`, so **it is not expressible as nodes** and it goes to the runtime
list whole; the stage records the ordered chain in provenance and marks the material
`proxyChainRuntime`. The other multi-proxy materials are ordinary: `animatedtexture +
texturescroll` (28 materials) and `playerposition + texturetransform` (4) are both fully
shader-time; `playerspeed + sine + subtract` (4) is runtime because of its source.

The rule the stage applies: walk the proxy list in order, resolve each `srcvar`; if every proxy in
the closure is shader-time and the final `resultvar` is an exposed parameter, emit nodes —
otherwise emit **nothing in the material** and register the whole material for the runtime factory.
Never half a chain.

### Provenance

Every instance carries one `UElysiumMaterialProvenance` (`UAssetUserData`, runtime module, so a
packaged game reads it), attached the way `UElysiumTextureProvenance` is (SF-4.2):

| Field | From |
|---|---|
| `AssetId`, `MaterialPath`, `UnitSchemaVersion`, `UnitSha256`, `SourceSha256` | `identity`, the GLB file, `sourceResolution.members[].sha256` |
| `Shader`, `SourceShader` | `shader`, `sourceShader` |
| `Parameters[]` (`Index`, `Block`, `Key`, `SourceKey`, `Value`, `ValueType`, `Offset`) | `parameters`, **in source order**, every row, including the ones this lane does not consume |
| `Blocks[]` (`Name`, `SourceName`, `Path`, `Parent`) | `blocks` |
| `Proxies[]` (`Name`, `SourceName`, `ParameterIndices`, `Destination`) | `proxies` plus the proxy table's destination |
| `ResolvedFamily`, `ResolvedPrograms[]` (`PixelShader`, `VertexShader`, `Condition`, `DrawPass`), `ResolutionInputs[]`, `ResolutionReason` | `shaderResolution` |
| `Master`, `BlendMode`, `TwoSided`, `SurfaceClass`, `SurfaceClassIndex` | this lane's decisions |
| `EnvMapSymbol`, `EnvMapAssetId`, `EnvMapProbePath` | `$envmap` and the reflection contract; the probe path is a soft path, never a bound parameter |
| `TextureBindings[]` (`Parameter`, `Value`, `Kind`, `Asset`, `Resolved`, `UsedLinearTwin`) | `textureBindings` plus the twin choice |
| `Dependencies[]` (`Role`, `Parameter`, `Asset`, `Resolved`) | `dependencies` |
| `SurfacePropertyAsset` | the surface-property dependency |
| `PatchOf`, `PatchKind` | `patch` (`vtmb:material:` of the base, and `replace`/`insert`) |
| `Anomalies[]`, `Omissions[]`, `Coverage` | as published |
| `Comments[]` | `comments` |

`ApplyJson` tolerates a missing key and refuses only a body that is not a JSON object, exactly as
the texture provenance does. Three fields are published as asset-registry tags in
`MetaDataTagsForAssetRegistry` beside `ElysiumRecipe`, so the Content Browser filters without
loading: **`ElysiumAssetId`**, **`ElysiumShaderProgram`** (the default-condition pixel program, or
the family name when unresolved) and **`ElysiumMaster`**.

### Knob contract

Every value that a human might want to change, and where it lives. Nothing that needs taste is a
Python or C++ literal (`seam_migration.md`, "Calibration happens on knobs inside the editor").

| Knob | Kind | Home | Read by |
|---|---|---|---|
| `DefaultSpecular`, `DefaultRoughness`, `DefaultMetallic` | settings scalar | `UElysiumSurfaceSettings` → `MPC_ElysiumSurfaces` | every master, for non-`$envmap` surfaces |
| `MaskRoughnessMin`, `MaskRoughnessMax`, `MaskSpecularScale` | settings scalar | same | the mask term of the reflection contract |
| `EnvTintScale` | settings scalar | same | the grey-tint specular scale |
| `FixedCubeStrength` | settings scalar | same | the 342 authored-cube instances |
| `Overbright` | settings scalar | same | `mul_x2 c0` in every lit program |
| `DecalDepthOffset` | settings scalar | same | `$decal` surfaces and `M_V2_Decal` |
| `LightSpecularScale` | settings scalar | same | the light rig (SF-6.2), one global, no per-map override |
| `CaptureRadius` | settings scalar | same | the reflection-capture placement (SF-6.2) |
| roughness / specular / metallic per surface class | class-table row | `UElysiumSurfaceCalibration` (`UDataAsset`) → a 64×1 lookup texture | every master, indexed by `SurfaceClassIndex` |
| `BaseTexture`, `Detail`, `NormalMap`, `EnvMap`, `EnvMapMask`, … | per-instance texture | the VMT, via the stage | the master's texture slots |
| `Color`, `SelfIllumTint`, `EnvMapTint`, `RefractTint`, `ReflectTint`, `WaterColor`, `FogColor`, `TexScaleOffset` | per-instance vector | the VMT | as named |
| `Alpha`, `SelfIllumAmount`, `DetailScale`, `EnvMapMaskScale`, `BumpScale`, `MinLight`, `MaxLight`, `RefractAmount`, `ReflectAmount`, `Water*`, `FrameRate`, `ScrollRateU/V`, `Sine*`, `WetnessScale`, `CloudScale`, `SpriteRenderMode`, `Texture2Scale` | per-instance scalar | the VMT | as named |
| `BlendMode`, `TwoSided`, `OpacityMaskClipValue` | per-instance base-property override | the VMT flags | the instance |
| every `Use*` / `MetallicTint` / `VampireEyes` / `CheapWater` switch | per-instance static switch | the VMT and the resolved program | the master |

**There is no per-material tuning layer** (owner answer, 2026-08-31). An edit to an imported `MI_`
is a throwaway experiment the next import overwrites; the settings page and the class table are the
whole authoring surface. The `UElysiumSurfaceSettings` object is the single writer of the surface
scalars in the collection — the Cog Environment window becomes a view onto it, not a second writer.

### Idempotency, pruning and failure

Identical to the texture lane. `manifest.json` carries `packageRoot`
(`/ElysiumBaked/Materials`), `select`, `pruneScope`, `keep`, `stageFailures` and one entry per
instance with its `recipe`; the stamp is
`bake_lib.recipe_fingerprint("materials", assetPath, recipe)` over the unit hash, the settings
version, the master, and the resolved parameter set, so a unit whose GLB changes or a
`settingsVersion` bump re-imports and nothing else does. The editor phase compiles each instance
and fails the unit — not the run — when a parameter name it sets does not exist on the master.
The first run is the full corpus; failures are isolated, named with their reason, counted in
`import_report.json`, and the command exits non-zero when any unit failed.
