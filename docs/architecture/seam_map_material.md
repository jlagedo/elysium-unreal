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
per baked probe: a real `Patch` shader whose `include` names the base and whose `replace` or
`insert` block overrides `$envmap` to the probe. Their identity is
`vtmb:material:maps/<map>/<mat>_<x>_<y>_<z>` (`seam_map_map.md` → "PAKFILE routing"), sourced from
the BSP's own `bsp-pakfile` origin the same way `seam_map_texture.md` sources the probe it binds
to. Of the 7,501 units in the corpus, **7,499 are `patch` and 2 are complete, standalone
`lightmappedgeneric` materials** with no `include`, no `patch` block and no base edge at all
(`maps/sm_tattoo/glass/glass01`, `maps/sm_tattoo/tile/bradfloora`) -- a consumer must not assume
every `maps/**` material unit patches a base. Of the 7,499 patched units, 6,255 carry a `replace`
operation and 1,244 an `insert`. Where the filename encodes a probe origin, the extension states
the join by name, independent of what the VMT's own shader happens to be:

```json
{
  "identity": {"asset": "vtmb:material:maps/sp_tutorial_1/plaster/socwndwd_-7831_3759_6441"},
  "patchOf": {"asset": "vtmb:material:plaster/socwndwd", "cubemapOrigin": [-7831, 3759, 6441]}
}
```

`patchOf` is derived from the unit's own filename (`patchOf` is filename-derived, not shader-derived):
`patch_of` is set whenever the stem ends in a `_<x>_<y>_<z>` coordinate suffix, and `null`
otherwise. Of the 7,501 identities, 4,951 carry that coordinate suffix and 2,550 do not; of the
2,550, 2,471 are the map's `cubemapdefault` fallback (named without coordinates by construction)
and 30 are positioned-probe patches whose own filename simply carries no coordinate (their
`$envmap` still names a positioned `c<x>_<y>_<z>` probe; only the *patched material's own* name
lacks one). The remaining 49 of the 2,550 are `insert` patches under `**/water/**` /
`**/dev_water*` names ending `_depth_<n>` that set `$waterdepth` and carry **no `$envmap` binding
at all** -- they patch a water depth value, not a cubemap. `patchOf` is `null` for all of these
2,550; the base edge is still stated, through `patch` (below) rather than `patchOf`, for every one
of the 7,499 that has a `patch` block at all (the 2 standalone materials above have neither).
Every real corpus case with a coordinate suffix is the literal `Patch` shader, so the base material
dependency the `patch` field already states and the one `patchOf` would add are the same row; the
decode adds it once. Where `$envmap` is set, its value is a concrete probe path
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

SF-1.4's **7,501 map materials** landed under `materials/maps/**` the same day, taking the corpus
to 19,125 units. **7,499 are patches** and add nothing to the three tables: one new key
(`include`), no new proxy kind, no new program pair. Each is a *delta* — `shader` is `patch`,
`shaderResolution.family` is `patch` with `resolved: false`, and the parameters are one `include`
plus a `replace` or `insert` block — so it takes its master from its base, not from a selector of
its own (see "Patched map materials" below). The other **2** — `maps/sm_tattoo/glass/glass01` and
`maps/sm_tattoo/tile/bradfloora` — carry no `patch` block at all: they are standalone
`lightmappedgeneric` materials a mapper wrote into the PAKFILE, and they take a master by the
ordinary selector, exactly like an install unit. The three tables' counts stay stated over the
install corpus, because that is where the shader families, the proxies and the parameter
vocabulary live.

**No silent drop.** Every key, every proxy kind and every program pair below has a named
destination. A unit carrying a key that is not in the parameter table, a proxy kind that is not in
the proxy table, or a resolved program pair that is not in the master inventory is a **stage
failure** with the unit named and the reason printed — never an instance written with the unknown
part quietly missing. That is the rule the three tables exist to make checkable.

**Landed (2026-08-31), revised the same day (review pass — findings 1-9).** `uv run elysium import
materials` lands the corpus with the required-slot rule (below) now enforced. All **19,125** units
stage since R7.1 (2026-09-04); the four that used to fail loudly were each a `textureClassMismatch`
the static frame-0 fallback cannot resolve (none is a multi-frame array), and each is now ruled:
`dev/ocean`/`dev/oceanbeneath` by ruling A's `M_V2_Water` required-slot exemption (the SLW base
texture is coverage, not colour), `envmap/gioint`/`skybox/hav_env` by the cube-`$basetexture` named
divergence (`CUBE_BASE_TEXTURE_DIVERGENCE_UNITS`, below). `uv run elysium import materials --lookdev` places
every tracked review-set entry still named in `lookdev_set.json`. Real numbers:
`docs/project/seam_migration.md` → "Material import landed (2026-08-31)".

### Identity and naming

```text
vtmb:material:<dir>/<stem>              ->  /ElysiumBaked/Materials/<dir>/MI_<safe stem>
                                        ->  /ElysiumBaked/Materials/<dir>/MI_<safe stem>_Decal
                                            (the projector twin, R7.2 — see "Two instances")
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

**Two instances, for one kind of unit (R7.2).** A material can be *projected* as well as worn, and
the two need different Unreal material domains, which is a material-only property: no instance can
override it. So a unit that is ever projected stages a **second shared instance** beside its
surface one, in the same package, named `MI_<safe stem>_Decal` and parented to `M_V2_Decal` — the
projector twin. The rule is the unit's own authored intent, not a per-map decision: `$decal 1`
(provenance `isDecalSurface`, 550 units) or family `decalmodulate` (38), **588** twins on the
corpus, none of them per-map. The twin carries only what the projector master exposes — its
`BaseTexture`, `Color`, `Alpha`, `SurfaceClassIndex`, `Emissive`/`EmissiveScale` from
`$selfillum`, `Unlit` from an `unlitgeneric` family — plus the surface instance's own
`PhysMaterial` and surface class, because a `$decal` face group binds the *twin* as its mesh slot
and a surface query there must answer what the wall answers. Both halves are `keep`-listed and
pruned together, and both share the unit's single provenance sidecar (which records `decalAsset`,
the twin's path). Three consumers resolve the twin by name, never by search: the bake's
`_place_decals` (one `ADecalActor` per `.decals` line), the bake's mesh-slot binding for an
`isDecalSurface` face group, and the runtime's `UElysiumDecalSubsystem::Lay`
(`FElysiumContentPaths::BakedDecalMaterial`). 23 of the 588 twins bind no `BaseTexture`: they are
the debug/wireframe families that carry `$decal` and take no master at all (see "No master,
provenance only"). They are staged because the rule is the unit's authored key, they are
referenced by no `.decals` line and no face group anywhere in the 108-map export, and they cost one
empty instance each.

**Class index and physical material** come from one resolution, run once per unit (owner rule,
`seam_migration.md` 2026-08-31). It always terminates, and it always records which tier answered,
as `surfaceClassSource`:

1. **`$surfaceprop`, lower-cased — 4,605 units.** `defualt` (10) folds to `default`. The four
   values the 63-entry `scripts/surfaceproperties.txt` table does not define — `cloth` 30,
   `bone` 9, `asphalt` 2, `leather` 1 — **get class rows of their own** rather than folding to
   `default`: they are real surfaces the artist named, and the class table is this lane's, not the
   sound table's. Their *physical* material still falls back to `PM_default`, because no
   `surfaceproperties.txt` entry and therefore no `PM_` asset exists for them; that split is
   recorded as `physMaterialFallback`. `surfaceClassSource: "surfaceprop"`.
2. otherwise the VMT's **top directory** when it names a class row — **559 units**. The list is a
   **curated allowlist**, not "any top directory that happens to match a class": only these 16
   names are read as a class, and a top directory outside the list falls through to tier 3 even
   when it looks like a material. `plaster` 124, `wood` 93, `stone` 70, `blends` 55, `brick` 36,
   `ground` 36, `metal` 35, `tile` 29, `drapery` 28, `carpet` 21, `cable` 12, `glass` 10,
   `grates` 5, `asphalt` 2, `water` 2, `grass` 1. `surfaceClassSource: "topdir"`.
3. otherwise the **per-family default row** — the remaining **6,460 units**. The table is keyed by
   the master the unit takes rather than by family name, so every family is covered by
   construction:

   | Master | Default class row | Units |
   |---|---|---|
   | `M_V2_Lit`, `M_V2_LitTranslucent` | `default` | 4,044 |
   | `M_V2_Unlit` (including the 80 no-master units) | `default` | 1,897 |
   | `M_V2_Eyes` | `flesh` | 406 |
   | `M_V2_Water` | `water` | 0 — all 24 author `$surfaceprop` |
   | `M_V2_Sprite` | `default` | 62 |
   | `M_V2_Decal` | `default` | 38 (the `decalmodulate` units; a projector twin never resolves a class of its own — it copies its surface unit's) |
   | `M_V2_TwoTexture` | `default` | 4 |
   | `M_V2_Refract` | `glass` | 11 |

   `surfaceClassSource: "familyDefault"`.

The resolved name drives two things: `PhysMaterial` on the instance, pointing at the Phase-2 asset
`/ElysiumBaked/SurfaceProperties/PM_<name>` (or `PM_default` for the four rows with no entry); and
the scalar `SurfaceClassIndex`. One key, two consumers, so a surface's sound and its shine can
never disagree. The class **name** rides in provenance beside the index, so an index is always
readable back as a name without the table.

**`SurfaceClassIndex` is stable.** It is not an array position that shifts when a row is added.
Each calibration row carries a fixed `Index`, assigned once at seeding from the pinned
`SURFACE_CLASSES` list in `importers/materials.py` — `default` at 0, then the sorted union of the
63 `surfaceproperties.txt` names, the six top-directory-only names (`asphalt`, `blends`, `cable`,
`drapery`, `grates`, `ground`) and the three tier-1 additions (`bone`, `cloth`, `leather`):
**72 rows**. The LUT texel a master samples is the row's `Index`, never its position in the `Rows`
array, so re-ordering the data asset in the editor cannot silently re-skin the world. The LUT is
therefore **128×1** (`MaxRows = 128`) rather than 64×1 — 72 rows already exceed 64, and the owner
will add more.

Every master exposes **`SurfaceClassLUT`** (`TextureObjectParameter`, default
`/Game/ElysiumGenerated/Materials/V2/T_SurfaceClassLUT`, `SAMPLERTYPE_LINEAR_COLOR`,
`TMVM_MipLevel` 0) beside `SurfaceClassIndex` (scalar, default 0). The three reads are named
**`ClassRoughness`** (`.R`), **`ClassSpecular`** (`.G`) and **`ClassMetallic`** (`.B`); those three
names are what the rest of this section means by "the class LUT row".

#### Patched map materials

A patched unit's parent is **its base instance, not a master**. `patch.asset` resolves to
`vtmb:material:<dir>/<stem>`, whose `MI_` already carries the base's master, switches and
parameters; the patched `MI_` is a second-level instance that overrides only what its
`replace`/`insert` blocks name. That is exactly VtMB's own `include` semantics, expressed in the
one Unreal mechanism that has the same shape, and it means the 7,499 patched instances cost 7,499
override sets rather than 7,499 full parameter sets. All 7,499 base references resolve. The two
`maps/sm_tattoo/…` units that carry no `patch` block are neither patches nor aliases; they take a
master by the ordinary selector.

`patchOf` is **filename-derived**: it exists exactly when the stem ends in a `_<x>_<y>_<z>`
coordinate suffix, because that suffix is the only place the probe origin is written down. 4,951
of the 7,499 carry one. The other 2,548 — 2,499 with no suffix and the 49 with a `_depth_<n>`
suffix — publish `patchOf: null` and state the base edge through `patch` instead. That one rule
covers the 2,502 `cubemapdefault` copies and the 49 `_depth_<n>` water copies; there is no second
formula.

What the patches actually touch, over the 7,499: **7,450 rows set `$envmap`** to the map's probe
(4,948 positioned `c<x>_<y>_<z>` probes, 2,502 the map's `cubemapdefault`), 49 set `$waterdepth`,
17 set `$crackmaterial`, 2 set `$bottommaterial`; 6,255 units operate through a `replace` block and
1,244 through an `insert`. Because a probe's pixels are never sampled, a unit whose only patched
parameter is `$envmap` overrides **nothing** on the material, and **7,431** instances are exactly
that. Of the 68 rows that are not `$envmap`, the 17 `$crackmaterial` and 2 `$bottommaterial` rows
override only a *provenance material reference*, so **only the 49 `$waterdepth` rows change a
material parameter**.

**The 7,431 no-op instances are a considered cost, not an oversight.** They exist so
`vtmb:material:maps/<map>/…` resolves to a real asset for SF-6.1's map lane. A `UMaterialInstance`
with no overrides is a few kilobytes and adds no shader permutation; the alternative — fold them
away and teach the map lane a second lookup — trades a cheap asset for a special case in the
consumer. Each records `PatchOf`, `EnvMapAssetId` and `EnvMapProbePath` in provenance.

### Master inventory (SF-3.1)

Nine masters. The split is by Unreal *material-only* property — shading model, translucency
lighting mode, refraction, the material domain — because those cannot be overridden on an
instance.
Blend mode, two-sidedness and the opacity-mask clip value **are** per-instance
(`FMaterialInstanceBasePropertyOverrides`), so they do not multiply masters; that is why there is
no separate "Additive" master, and why `M_V2_Lit` covers opaque and masked alike.

| Master | Shading model | Default blend | Two-sided | Covers | Materials |
|---|---|---|---|---|---|
| `M_V2_Lit` | Default Lit | Opaque | off | `lightmappedgeneric`, `vertexlitgeneric`, `teeth`, `cable` — opaque and masked | 7,925 |
| `M_V2_LitTranslucent` | Default Lit, `TLM_SurfacePerPixelLighting` | Translucent | off | the same four families plus `shatteredglass` — translucent and additive | 1,113 |
| `M_V2_Unlit` | Unlit | Opaque | off | `unlitgeneric`, `cloud` | 1,865 |
| `M_V2_Eyes` | Default Lit | Opaque | off | `eyes` | 406 |
| `M_V2_Water` | Single Layer Water (R7.1) | Opaque | off | `water` | 24 |
| `M_V2_Sprite` | Unlit, `bDisableDepthTest` | Translucent | on | `sprite`, plus the 5 `unlitgeneric` `$ignorez` world units | 67 |
| `M_V2_SpriteZ` (R7.3) | Unlit, depth test **on** | Translucent | on | no VMT unit — `M_V2_Sprite`'s graph plus the `ElysiumFog` parameters (`FogColor` / `FogStart` / `FogInvRange`, by parameter, inscatter × opacity); the particle floor's `MI_Particle` | 0 |
| `M_V2_SpriteZLit` (R7.3) | Default Lit, `TLM_VolumetricPerVertexNonDirectional` | Translucent | on | no VMT unit — the twin for the `lighting` leaves; `MI_ParticleLit` | 0 |
| `M_V2_Refract` | Default Lit, refraction enabled | Translucent | off | `refract`, `heatglow` | 11 |
| `M_V2_Decal` | Default Lit, domain **`MD_DeferredDecal`** | Translucent | off | every projector — the 550 `$decal` units and the 38 `decalmodulate` ones, each as a *second* instance beside its surface one | 588 |
| `M_V2_TwoTexture` | Default Lit | Opaque | off | `unlittwotexture`, `worldvertextransition`, `worldtwotextureblend` | 95 |

The two R7.3 twins exist because `bDisableDepthTest` is material-only and VtMB's particle mode 8
is depth test *on* (`effects-architecture.md` §5.4); no VMT unit routes to them and the exposed-
parameter table below does not list them. Beside them `make_v2_materials.make_particle_children`
authors the four particle children under `/ElysiumBaked/Materials/particles/` — `MI_Particle`
(`M_V2_SpriteZ`), `MI_ParticleLit` (`M_V2_SpriteZLit`), `MI_ParticleNoZ` (`M_V2_Sprite`), all
three `BlendMode = AlphaComposite` + `UseVertexColor` / `UseVertexAlpha` on, and
`MI_ParticleRefract` (`M_V2_Refract`, Translucent, `UseBaseTexture` on) — recipe-stamped on the
master's own recipe like `MI_V2_Missing`; `importers/materials.py` names the four under the
manifest's `keep` so the import lane's prune leaves them. `MI_ParticleNoZ` sits on `M_V2_Sprite`,
whose parameter table is pinned to `ElysiumSurfaceParamsSprite` in C++, so it carries no fog
parameters until that header grows them (the four `no_z_test` leaves draw unfogged).

The **Default blend** column is the master asset's own property, not a prediction about its
instances: every instance sets its own from the table below. It matters only for an instance the
table leaves alone, so each master carries the cheapest mode its family admits. `M_V2_Unlit` is the
one worth calling out — 1,381 of its 1,865 instances override to Translucent (1,150), Additive
(204) or Masked (27), and the master still ships Opaque, because the 484 that do not override are
the ones that should pay nothing.

11,544 of 11,624 units take a master. The remaining **80** are debug and tool families listed at
the end of this section as "no master, provenance only".

Per-instance overrides, from the VMT, in this order (first match wins):

| VMT state | `BlendMode` | Units |
|---|---|---|
| `$additive` non-zero | `BLEND_Additive` | 250 |
| `$translucent` non-zero | `BLEND_Translucent` | 2,241 |
| `$alphatest` or `$alphatested` non-zero | `BLEND_Masked`, `OpacityMaskClipValue` **0.5** | 184 |
| family `decalmodulate` | `BLEND_Modulate` | 38 |
| otherwise | `BLEND_Opaque` | 8,911 |

The counts are exclusive and sum to 11,624; the keys themselves overlap. `$translucent` is authored
non-zero on 2,392 units, 151 of which also set `$additive`; `$alphatest`/`$alphatested` on 231, 47
of which also set a mode above them.

**The clip value is 0.5 because Source's is.** VtMB's alpha-test path issues
`AlphaFunc GEQUAL 0.5` — a fixed comparison with no per-material reference — so 0.5 is a
transcription, not a taste call. `$alphatestreference`, Source's per-material override, **is a
registered shader parameter in this build but no corpus unit sets it** (0 of 11,624). It is
therefore a *latent* key: the parameter table maps it to `OpacityMaskClipValue`, the stage honours
it if a future unit ever authors one, and until then every masked instance gets 0.5.

`$nocull` non-zero sets `TwoSided` (588 units). `$translucent` carries three authored values —
`1` (2,390), `0` (369) and `2` (2); any non-zero is translucent and the `2` is recorded as an
anomaly rather than read as a mode. `$additive` is authored on 614 units and **zero on 364 of
them**, which is why the additive row counts 250 and not 614.

**`$ignorez` gets no material property.** `bDisableDepthTest` is a material-only flag, so an
instance cannot turn it on. Of the 1,491 units that set `$ignorez` non-zero, 1,473 are `hud/`,
`fonts/`, `interface/` and `vgui/` materials SF-6.5 draws through UMG, where depth is not tested
at all. The 18 world uses resolve like this *(provisional owner call)*: 9 `wireframe` units are in
the no-master set; 3 are already `sprite` and land on `M_V2_Sprite`, which is depth-test-off by
construction; the **5 `unlitgeneric` units** (`engine/lightsprite`, `engine/vertexcoloradditive`,
`engine/vertexcolorblend`, `sun/overlay`, `debug/debugportals`) are re-routed to **`M_V2_Sprite`**
rather than to a tenth master — it is Unlit, two-sided and depth-test-off, which is precisely what
they are, and blend mode and two-sidedness remain per-instance so nothing is lost; the 1
`vertexlitgeneric` unit (`models/scenery/furniture/displaytable/floating`) **keeps `M_V2_Lit`** and
records the flag as a named divergence, because a Default Lit surface cannot move to an Unlit
master and one floating display-table pane is not worth `M_V2_UnlitNoDepth`. The stage fails an
`$ignorez` re-route if the unit carries a key `M_V2_Sprite` does not expose; none does today.

**Static switches are not capped.** An earlier draft limited each master to four; that was a
guess, and it is lifted. The stage derives each instance's switch set from the resolved program
pair, and the corpus realizes **61 distinct combinations on `M_V2_Lit`/`M_V2_LitTranslucent`** and
**31 on `M_V2_Unlit`**; every other master realizes fewer than ten. Those are the numbers that
matter, because a static switch costs a *shader permutation per realized combination*, not per
declared switch: the cook builds 61 + 31 + the rest, not 2^15. SF-4.5 compiles the **first instance
of each (parent, switch-combination, blend mode, `TwoSided`, `OpacityMaskClipValue`)** tuple
(review finding 3 — the base-property overrides select their own shader map exactly like a static
switch does, so folding only `blendMode` in under-measured the corpus: 963 keys against 1,000 real
permutations) and records `compiledPermutations` so the number is measured on every run rather
than assumed. The probe also requires `MaterialEditingLibrary.get_num_shader_types` to report at
least one shader type. `list_shaders`' compiled-type names ride in `compiledPermutations[]` as
`probedShaderTypes`, alongside `missingShaderTypes` for a hit-proxy/depth-only shader that never
showed up -- **recorded, never fatal**: a real full-corpus run (2026-08-31) proved a headless
`-run=pythonscript` commandlet never compiles either through this path (both are populated on
demand by an actual viewport/hit-test, not by `get_statistics`/`update_material_instance` alone),
so an earlier version of this check that required them failed ~90 real entries — including
`cable/MI_cable`, the very unit the `TwoSided`-ordering defect above was reproduced on — on a
condition that was never true for any entry a headless import actually builds.

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
| `decalmodulate` | 38 | Decal | the string is absent from `stdshader_dx8.dll`; the engine falls back to `FindShader("wireframe")`, so these 38 never drew what they name. **They draw as translucent deferred decals** — the divergence is "an impact hole is a translucent stain", not a wireframe (owner call A, R7.2, below) | none |
| `refract` | 9 | Refract | `$refracttexture` is `_rt_WaterRefraction`, a render target with no Unreal twin; the master uses Unreal `Refraction` driven by `$dudvmap`/`$normalmap` and `RefractAmount` | none (`waterrefract` is the shape) |
| `cable` | 9 | Lit | the program is a hemispheric `dp3` of a normal map against one directional light — under Lumen that is an ordinary normal-mapped lit surface, so it takes `M_V2_Lit` with `UseNormalMap` and no bespoke master | `cable` |
| `shatteredglass` | 2 | LitTranslucent | `$crackmaterial` names a second material; recorded as a provenance material reference, not sampled | none |
| `cloud` | 2 | Unlit | `$cloudalphatexture` drives opacity, `$cloudscale` the UV scale | none |
| `heatglow` | 2 | Refract | distortion only; no base colour | none |
| `worldtwotextureblend` | 1 | TwoTexture | same slot layout as `worldvertextransition` | none |

**Owner call A (R7.2, 2026-09-03): the 38 `decalmodulate` units are translucent decals.** These 38
are exactly VtMB's runtime impact set — `decals/hits/{concrete,metal,wood,glass,flesh}/*`, the
`C_TEGunshotDecal` table's surface × 5 plus `soak1-5`, and `decals/break1-3` — so what they draw is
what a bullet hole looks like, and retail drew nothing at all (the shader is absent, the engine
fell back to wireframe). R5 picked `BLEND_Modulate` as the closest stand-in for the name. That
choice is now moot on two counts: the projector twin every one of them stages is `BLEND_Translucent`
by construction (owner call A), and on a DBuffer platform — `r.DBuffer` is 1 and the project does
not override it — the engine rewrites a Modulate decal to Translucent anyway
(`DecalRenderingCommon.cpp` 47–49). The rejected alternative was turning `r.DBuffer` off to keep a
true modulate, which trades Nanite decal receiving and the separate emissive pass for 38 materials
that never drew. Their *surface* instance stays on `M_V2_Decal` with the `BLEND_Modulate` override
below: it is the retail record of what the VMT says, and nothing binds it — a `decalmodulate` unit
is never a world face.

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
`T` texture, `S` scalar, `V` vector (a `VectorParameter` is a float4), `#` static switch. **Every
default is stated**, because the default is what an instance the stage never touches renders as.

**Every texture slot with a paired switch sets that switch when the texture is bound.**
`NormalMap` → `UseNormalMap`, `BaseTexture2` → `UseBaseTexture2`, `CloudAlphaTexture` →
`UseCloudAlpha` follow a plain "bound ⇒ on" rule; `EnvMap`/`EnvMapMask` have their own
precedence-aware rules instead (the reflection contract, below), and `BaseTexture` follows the
`UseBaseTexture` rule stated per master (a bound `$basetexture` whose resolved default pixel
program is *not* a `*_NoTexture` variant). `Iris`/`Glint`/`DuDvMap` have no paired switch on any
master — an eyes unit's `Iris` is always sampled when bound, `UseGlint` and the `DuDvMap` ripple
are gated by proxy/design rules of their own.

Four parameters are on **every** master and are not repeated in the tables below:

| Name | Kind | Default | Meaning |
|---|---|---|---|
| `SurfaceClassIndex` | S | `0` | the class row's fixed `Index` (`default` = 0) |
| `SurfaceClassLUT` | T (`TextureObjectParameter`) | `/Game/ElysiumGenerated/Materials/V2/T_SurfaceClassLUT` | 128×1, `SAMPLERTYPE_LINEAR_COLOR`, sampled at `TMVM_MipLevel` 0; reads named `ClassRoughness/.R`, `ClassSpecular/.G`, `ClassMetallic/.B` |
| `Alpha` | S | `1.0` | `$alpha`; feeds `MP_OPACITY` (and, times `Color`, VtMB's `c3` per-draw modulation) |
| `Color` | V | `(1, 1, 1, 1)` | `$color`; VtMB's `c3.rgb` |

**The animation and scroll lanes**, shared by every master that hosts a `texturescroll` or
`animatedtexture` proxy, and defined once here:

| Name | Kind | Default | Wiring |
|---|---|---|---|
| `BaseScrollRateU`, `BaseScrollRateV` | S | `0.0` | `Panner(Speed = Append(U, V))` on the base-texture UV. `0,0` is an exact no-op, so no switch |
| `BumpScrollRateU`, `BumpScrollRateV` | S | `0.0` | the same `Panner` on the normal/DuDv UV — an **independent** UV chain |
| `FrameRate`, `FrameCount` | S | `0.0`, `1.0` | base flipbook: `SliceIndex = floor(frac(Time * FrameRate) * FrameCount)`; `animationnowrap` (1 unit) clamps instead of wrapping |
| `BaseTextureFrames` | T (`TextureObjectParameter`, `Texture2DArray`) | `T_V2_DefaultFrames` (one white slice, authored by `make_v2_materials.py` beside `T_LinearWhiteMask`) | sampled by a `TextureSample` with the slice index above |
| `UseAnimatedFrames` | # | `false` | selects the array sample over `BaseTexture` |
| `NormalFrameRate`, `NormalFrameCount`, `NormalMapFrames`, `UseAnimatedNormalFrames` | S/S/T/# | `0.0`, `1.0`, `T_V2_DefaultFrames`, `false` | the same three nodes on the normal/DuDv lane |

A flipbook needs an *array-typed* slot, so it is a second parameter rather than a mode on the 2D
slot; `UseAnimatedFrames` is the only thing that decides which of the two is sampled. All 72
`animatedtexture` materials drive exactly one lane (measured: no material animates both), and the
two lanes are separate because 12 materials scroll base UVs and normal UVs at different rates.

**The stage binds the array.** `BaseTextureFrames`/`NormalMapFrames` bind the animated texture's
own `TA_`-prefixed `Texture2DArray` sibling (`/ElysiumBaked/Textures/<dir>/TA_<stem>`, staged by
the texture lane whenever the referenced unit's own `frames` exceeds 1), and `FrameCount`/
`NormalFrameCount` are read from that same texture's staged slice count — not left at the inert
default `1.0`. `UseAnimatedFrames`/`UseAnimatedNormalFrames` are only set `true` when that binding
actually resolves; when the animated texture never staged as an array (unresolved dependency, or
`frames <= 1`), the switch stays off rather than sampling `T_V2_DefaultFrames` and the unit's
provenance names the gap (`animatedFramesArrayUnavailable`). `BaseTextureFrames` samples through
`SAMPLERTYPE_LINEAR_COLOR`, matching `T_V2_DefaultFrames`'s own non-sRGB `TC_Default` compression
— a `SAMPLERTYPE_COLOR` object bound to that texture is a real compile error on every
`UseAnimatedFrames=true` permutation, one the default-false compile `mel.recompile_material` runs
never visits (see "Ordering, concurrency, tests, risks" -> the all-switches-true compile probe,
`pipeline/unreal/make_v2_materials.py`'s `_probe_all_switches_true`).

**`NormalMapFrames` samples through `SAMPLERTYPE_NORMAL` instead** (H3 review fix, corrected from
an earlier draft of this section that had it on `SAMPLERTYPE_LINEAR_COLOR` like the base lane):
`HLSLMaterialTranslator.cpp` only emits `UnpackNormalMap` (`2*x - 1`) for a `SAMPLERTYPE_Normal`
sample (~line 6951) — sampled linear, an animated normal frame's packed tangent normal never left
`[0,1]`, so `UseAnimatedNormalFrames=true` silently shipped a washed-out, un-unpacked normal on
every unit that set it. `NormalMapFrames` therefore binds its own default array,
`T_V2_DefaultNormalFrames` (`_make_default_normal_frames_array`, authored beside
`T_V2_DefaultFrames`/`T_LinearWhiteMask`) — a `Texture2DArray` packed `(128, 128, 255, 255)`, the
same flat-normal byte triple `/Engine/EngineMaterials/DefaultNormal` itself carries, with
`TC_Normalmap` compression — rather than a second binding of the all-white `T_V2_DefaultFrames`:
unpacked, all-white decodes to `(1, 1, 1)`, not the flat `(0, 0, 1)` a "no perturbation" default
must produce.

**The sine lane**, shared by every master that hosts a `sine` proxy, needs no static switch because
it is **neutral by construction** — at the defaults every factor below is exactly `1`:

```text
SineValue = lerp(SineMin, SineMax, 0.5 * (sin(2*pi * (Time + SineTimeOffset) / SinePeriod) + 1))
X        *= lerp(1, SineValue, SineTargetMask.<x|y|z|w> * SineChannelMask)
```

`MaterialExpressionSine.Period` is a plain float carrying `ShowAsInputPin` metadata, **not** a
connectable `FExpressionInput`, so the period is applied by dividing the phase:
`Sine(Input = (Time + SineTimeOffset) / clamp(SinePeriod, 1e-4, FLT_MAX))` with the node's own
`Period` left at its default `1`. That is the identical `sin(2*pi * phase / period)`.

| Name | Kind | Default | Meaning |
|---|---|---|---|
| `SineMin`, `SineMax` | S | `1.0`, `1.0` | `sinemin`/`sinemax`; equal defaults make the term a no-op |
| `SinePeriod` | S | `1.0` | `sineperiod`, seconds |
| `SineTimeOffset` | S | `0.0` | `timeoffset` |
| `SineTargetMask` | V | `(0, 0, 0, 0)` | which parameter the sine drives: `.x` `Alpha`, `.y` `Color`, `.z` `SelfIllumTint`, `.w` `EnvMapTint`. A master that does not expose one of those ignores its component, and the stage **fails** a unit whose sine target that master does not expose |
| `SineChannelMask` | V | `(1, 1, 1, 1)` | which channels of the chosen vector, for a `resultvar` of the form `$color[1]` |

One lane per material. 68 of the 87 `sine` materials resolve to a single index-collapsed target;
of the 19 that do not, the ones whose extra targets are `$temp*` chain links collapse to one output
during chain resolution, and any material still left with two exposed targets registers for the
runtime factory **whole**, exactly as the chain rule in "Proxy policy" already requires.

**The `sine` → `texturetransform` chain, and `SineUVTranslate`** (R7.1 ruling J,
`water-architecture.md` → "Ruling J — sm_pier_1 and the SineUVTranslate lane",
`pipeline/unreal/make_v2_materials.py::_uv_lanes`/`_sine_lane`). A `sine` whose `resultvar` is one
of the target rows above drives a shading term
through the table; a `sine` whose `resultvar` is a `$temp*` scratch register drives nothing by
itself — it is half of a *UV* chain, read back by a `texturetransform` proxy whose `translatevar`
names the same register and whose `resultvar` is `$basetexturetransform`. `objects/surf` (the
pier's 17 wave cards) writes exactly that pair: `sine $temp[0]` sliding `0 → 0.5` over 15 s, read
by `translatevar $temp`. `importers/materials.py::_apply_proxies` resolves the pair in two passes
— the proxy loop records every `sine` that targets a `$temp*` register (`temp_sines`, keyed by
register and vector component) and every `texturetransform`'s arguments, without emitting either;
after the loop, `_resolve_sine_uv_translate` joins each `texturetransform` targeting
`$basetexturetransform` to the `$temp*` register its `translatevar` names and emits one vector,
**`SineUVTranslate`** = `(SineMax.u − SineMin.u, SineMax.v − SineMin.v, SineMin.u, SineMin.v)` — an
amplitude/offset pair per UV axis, from whichever component (`u`/`v`) each half of the chain wrote.
The joined `sine` row's `destination` becomes `"graph"` (it does reach the graph, through this
vector, not a scalar of its own); a chain whose two sines disagree on `SinePeriod`/`SineTimeOffset`
stages the first and records `sineChainPeriodMismatch` rather than silently overwriting one with
the other. A `$temp*` sine no `texturetransform` consumes, any `rotatevar`/`scalevar` rider (this
lane is one UV translate, not a matrix), and a component-less `$temp` a `translatevar` reads (which
of U or V the author meant is undecidable — a whole-vector sine writes both components with one
number) all keep the ordinary `proxyTargetProvenanceOnly` omission, named on the register.

`SineUVTranslate` is exposed on `M_V2_Lit`/`M_V2_LitTranslucent` **only** (`_SINE_UV_LANE =
{"SineUVTranslate": "V"}`, merged into the Lit construction dict LitTranslucent shares); a VMT
whose chain resolves on any other master has the vector dropped by `_drop_unexposed_sine_uv`
(called immediately after `_apply_water_underside`) with the omission
`"<master> exposes no SineUVTranslate lane"`, the same "declared on the wrong master" shape
`_validate_exposed` already enforces for switches. In the graph, `_sine_lane` runs *before*
`_uv_lanes` on Lit/LitTranslucent alone (the only two masters that build it in that order — every
other master keeps the old order, sine lane after UV lanes, because it has no UV chain to feed),
so its bare `0..1` wave (`sine["wave"]`, before `SineMin`/`SineMax` scale it into a shading factor
— the UV chain carries its own amplitude and offset in `SineUVTranslate` itself) is available as an
input: `base_coord = transformed_uv + (SineUVTranslate.rg × wave + SineUVTranslate.ba)`, fed to the
**base** texture panner only — the bump/normal panner keeps the untranslated `transformed_uv`,
because Source's `$baseTextureTransform` moves the base texture and leaves `$bumpTransform` alone,
and the pier's surf cards carry no normal map to move anyway. The default `(0, 0, 0, 0)` makes the
whole add an exact no-op on every instance that never authored the chain. Surf as staged:
`SineUVTranslate = [0.5, 0, 0, 0]`, `SineTargetMask = [1, 0, 0, 0]`, `SinePeriod = 15`.

##### `M_V2_Lit` / `M_V2_LitTranslucent`

| Textures | Kind | Default | Class |
|---|---|---|---|
| `BaseTexture` | T2D | `/Engine/EngineResources/DefaultTexture` | colour (sRGB) |
| `NormalMap` | T2D | `/Engine/EngineMaterials/DefaultNormal` | data → `_linear` twin |
| `EnvMapMask` | T2D | `T_LinearWhiteMask` (1×1 linear white, **never** `DefaultTexture`) | data → `_linear` twin |
| `EnvMap` | TCube | `/Engine/EngineResources/DefaultTextureCube` | colour (sRGB) |

| Scalars | Default | | Vectors | Default |
|---|---|---|---|---|
| `SelfIllumAmount` | `0.0` | | `SelfIllumTint` | `(1, 1, 1, 1)` |
| `EnvMapMaskScale` | `1.0` | | `EnvMapTint` | `(1, 1, 1, 1)` |
| `BumpScale` | `1.0` | | `TexScaleOffset` | `(1, 1, 0, 0)` — `(scaleU, scaleV, offsetU, offsetV)` |
| the animation, scroll and sine lanes above | | | `SineTargetMask`, `SineChannelMask` | as above |
| the scene-fog lane (R5.4, below) — `FogStart`, `FogInvRange` primitive-driven, `FogInscatter` `1.0` | | | `FogColor` | primitive-driven (CPD 0..3) |
| | | | `SineUVTranslate` (R7.1 ruling J, Lit/LitTranslucent only — below) | `(0, 0, 0, 0)` |

| Static switch | Default | Set by |
|---|---|---|
| `UseBaseTexture` | `true` | `$basetexture` bound; the `*_NoTexture` programs clear it |
| `UseNormalMap` | `false` | `$bumpmap` bound (the bump pass-1 collapse) |
| `UseSelfIllum` | `false` | `$selfillum` non-zero |
| `UseVertexColor` | `false` | a `*_VertexColor` vertex program |
| `UseVertexAlpha` | `false` | `$vertexalpha` non-zero |
| `UseEnvMap` | `false` | `$envmap` present, in any form |
| `UseEnvMapMask` | `false` | `$envmapmask` bound |
| `UseBaseAlphaEnvMapMask` | `false` | `$basealphaenvmapmask` non-zero |
| `UseNormalMapAlphaEnvMapMask` | `false` | `$normalmapalphaenvmapmask` non-zero |
| `UseFixedCube` | `false` | `$envmap` names an `envmap/*` image |
| `MetallicTint` | `false` | `$envmaptint` is chromatic |
| `UseAnimatedFrames`, `UseAnimatedNormalFrames` | `false` | an `animatedtexture` proxy on that lane |
| `UseDetailSway` | `false` | **never the stage** — only the map bake's `MI_DetailSway_*` child of an imported instance (R6.3, "Detail sway on the model masters" below) |

##### `M_V2_Unlit`

**No normal map.** The two-slot rule below names `M_V2_Lit` and `M_V2_TwoTexture` only: zero
`unlitgeneric`/`cloud` units author `$bumpmap`, and this build's `UnlitGeneric` registers no bump
parameter at all (`shader_combos.md` → UnlitGeneric). The rule is retracted for Unlit rather than a
dead slot added.

| Textures | Kind | Default | Class |
|---|---|---|---|
| `BaseTexture` | T2D | `/Engine/EngineResources/DefaultTexture` | colour |
| `EnvMapMask` | T2D | `T_LinearWhiteMask` | data → `_linear` twin |
| `EnvMap` | TCube | `/Engine/EngineResources/DefaultTextureCube` | colour |
| `CloudAlphaTexture` | T2D | `T_LinearWhiteMask` | data → `_linear` twin |

| Scalars | Default | | Vectors | Default |
|---|---|---|---|---|
| `EnvMapMaskScale` | `1.0` | | `EnvMapTint` | `(1, 1, 1, 1)` |
| the base-scroll, base-animation and sine lanes | | | `TexScaleOffset` | `(1, 1, 0, 0)` |
| | | | `CloudScale` | `(1, 1, 1, 0)` — `$cloudscale` is a **vector** (`[2 2 2]`), not a scalar |
| | | | `SineTargetMask`, `SineChannelMask` | as above (`.z` unused: no `SelfIllumTint`) |
| the scene-fog lane (R5.4) — `FogStart`, `FogInvRange` primitive-driven, `FogInscatter` `1.0` | | | `FogColor` | primitive-driven (CPD 0..3) |

Static switches: `UseBaseTexture` `true`; `UseVertexColor`, `UseVertexAlpha`, `UseEnvMap`,
`UseEnvMapMask`, `UseBaseAlphaEnvMapMask`, `UseFixedCube`, `MetallicTint`, `UseAnimatedFrames`,
`UseCloudAlpha`, `UseDetailSway` — all `false` (`UseDetailSway` is never the stage's to set; R6.3 below). `$selfillum` on an unlit surface (9 units) is meaningless — the
surface is already pure emissive — and is recorded as an anomaly, not a switch.

**Usage flags** (H4 review fix, corrected from an earlier draft that carried only the world/ISM
flags): `used_with_instanced_static_meshes`, `used_with_nanite`, `used_with_skeletal_mesh` and
`used_with_morph_targets`. The last two were missing — the stage routes every model family with no
master of its own onto this one (`vertexlitgeneric_dx6`, `eyeball`, `shadowmodel`, `camo`,
`burnpeel`, `redvision`, `gooinglass` and the rest of `importers/materials.py`'s
`NO_MASTER_FAMILIES`, "No master, provenance only" above), several of which are character/prop
model geometry, so without them UE compiles no skeletal-mesh permutation and those primitives fall
back to the default grey material in a packaged build. `used_with_niagara_sprites` is dropped —
checked read-only against `importers/materials.py`'s own routing, no `NO_MASTER_FAMILIES` or
model-family unit lands on `M_V2_TwoTexture` either, so that master needs neither addition.

##### `M_V2_Eyes`

| Name | Kind | Default | Notes |
|---|---|---|---|
| `BaseTexture` | T2D | `/Engine/EngineResources/DefaultTexture` | the eyeball, `t0`; colour |
| `Iris` | T2D | `/Engine/EngineResources/DefaultTexture` | `$iris`, `t1`; colour |
| `Glint` | T2D | `/Engine/EngineResources/Black` | `$glint`, `t2`; **colour** (it is an additive emissive image, not data — no `_linear` twin) |
| `IrisFrame` | S | `0.0` | the iris slot the character lane writes at runtime when it swaps eye colour; `0` means "the `$iris` the VMT bound" |
| `VampireEyes` | # | `false` | `$vampire` (12 units) |
| `UseGlint` | # | `false` | `$glint` — set by **no** shipped material, so `false` on all 406 |

**`VampireEyes` is wired**, against the `psh/eyes_vampire` disassembly
(`docs/vtmb/facial_animation.md:503`, cited there by name):

    mul r0, t0, v0            ; BaseColor(lit) = BaseTexture(sclera, t0) x vertex lighting v0
    lrp r0, t1.w, t1, r0      ; result = Lerp(A=r0 (lit sclera), B=Iris(t1), Alpha=Iris.a)
    add r0.xyz, r0, t2        ; + Glint (t2), additive
    mov r0.w, t0.w            ; Opacity(Mask) = BaseTexture.a

Read against the non-vampire `eyes.psh` above, the difference is *where* lighting applies: the
non-vampire program lights the whole lerped result; the vampire program lights the sclera *before*
the lerp, so the iris is self-illuminated (unlit) while the sclera alone is lit. There is no
post-lighting `mul` a node graph can reproduce (Lumen lights whatever lands in `MP_BASE_COLOR`
uniformly), so the equivalent split routes the iris term to Emissive and darkens BaseColor by the
iris coverage it lost: `BaseColor = (1 - Iris.a) x BaseTexture`, `Emissive += Iris.rgb x Iris.a`.

##### `M_V2_Water`

**Single Layer Water** (R7.1 ruling A, `docs/architecture/water-architecture.md` §4):
`MSM_SingleLayerWater`, `BLEND_Opaque`, one-sided (ruling E — the underside is its own set of
faces on the same master, not a two-sided flip), `used_with_nanite` off (Nanite rejects the
shading model outright; the water faces already live in the non-Nanite `T_` chunk bucket),
`used_with_instanced_static_meshes` on. `GRAPH_VERSION` 8 → 9. `make_v2_materials.make_water` sets
the shading model *after* `_build_water` returns, not before: a fresh `UMaterial` is
`MSM_DEFAULT_LIT`, and authoring under `MSM_SINGLE_LAYER_WATER` while the ~95 expression writes
`_build_water` makes are still in flight recompiles the graph as it stands after every write —
`SingleLayerWater materials requires the use of SingleLayerWaterMaterial output node`
(`MaterialShared.cpp:6447`) until the output node exists, then `No inputs to Single Layer Water
Material` (`MaterialExpressions.cpp:21330`) until its four pins are wired. The output node is
therefore created last, wired to all four pins in one step, and the caller sets the shading model
only once the function returns; the blend mode is `Opaque` before and after, so no intermediate
compile the two passes take is ever an invalid domain/blend/shading-model triple.

There is **no `BottomMaterial` texture slot.** `$bottommaterial` (24 units, 22 of them water)
names a *material*, never a texture; all 24 bindings are unresolved in the corpus. It is a
provenance material reference, exactly like `$crackmaterial` and `$modelmaterial` — except for one
comparison the stage still makes: a unit whose `$bottommaterial` names *itself*
(`dev/dev_waterbeneath2`, `dev/oceanbeneath` — VBSP's own faces on the inward side of every water
brush) reads the authored value off its own provenance rows, not off `params.material_refs` (the
GLB decoder emits a dependency row only for a texture-shaped value, so `$bottommaterial` reaches no
dependency on any of the 26 units that author it), and normalises it (`\`→`/`, `.vmt` stripped,
case-folded) against the unit's own material key. A match stages the static switch **`Underside`**
(`_apply_water_underside`, `importers/materials.py:1369`, called at :1983): the engine strips
`$reflecttexture` from every down-facing water face (`Mod_LoadFaces`), and 5.8's SLW has no
camera-under-water branch to lean on instead (§8), so the instance says so once — zero specular,
zero volume extinction (below). Measured 2026-09-04 on the staged manifest: of the 24 water units, 22 carry the
switch and exactly one is true -- `dev/dev_waterbeneath2` (`MI_dev_waterbeneath2`), the 21 others
false, including `dev/oceanbeneath`, which names itself the second `Underside` unit and is now
measured.

**`BaseTexture` is not a required slot on `M_V2_Water` (R7.1 follow-up, 2026-09-04).**
`REQUIRED_TEXTURE_SLOTS` used to demand `BaseTexture` on every master alike; Water is now the one
exemption (`importers/materials.py`, the master-keyed dict). The base texture is SLW *coverage*
(`Opacity = UseBaseTexture ? Alpha × BaseTexture.a : 0`), not the surface's colour — the look is
the volume, the reflection and the refraction, none of which read it — so a water unit that cannot
bind one still draws as water, which is what every other water unit already does by default
(`UseBaseTexture` resolves per unit exactly as Lit/Unlit/Refract do, above). `dev/ocean` and
`dev/oceanbeneath` are the two units this rescues: their `$basetexture` `dev/water_dudv` is a
29-frame VTF that stages as a `Texture2DArray` where `BaseTexture` wants a `Texture2D`, and there
is no `BaseTextureFrames` lane on `M_V2_Water` to fall back onto (the DX6 fallback sheet the R7.1
water-architecture ruling names, not the shader's real animated DuDv input). They used to be 2 of
the corpus-wide staging failures (`seam_migration.md` -> R7.1 open issues); now they stage with
`UseBaseTexture` off, the `textureClassMismatch` anomaly still recorded, and every other slot
(`DuDvMap`, `NormalMap`, `EnvMap`, the scalars and vectors above) resolved the same as any other
water unit.

| Textures | Kind | Default | Class |
|---|---|---|---|
| `BaseTexture` | T2D | `/Engine/EngineResources/DefaultTexture` | colour; 6 of 24 bind one |
| `DuDvMap` | T2D | `/Engine/EngineMaterials/DefaultNormal` | data → `_linear` twin (`$bumpmap` here, per the slot rule); **declared, not wired** (below) — the sample node stands in the graph but feeds no pin |
| `NormalMap` | T2D | `/Engine/EngineMaterials/DefaultNormal` | data → `_linear` twin (`$normalmap`) — the flipbook lane R7.1 fixes (§4.4 below) |
| `EnvMap` | TCube | `/Engine/EngineResources/DefaultTextureCube` | colour; 7 of 24 |

| Scalar | Default | From | | Scalar | Default | From |
|---|---|---|---|---|---|---|
| `RefractAmount` | `20.0` | `$refractamount` — declared, not wired (below) | | `WaterTimeFreq1` | `0.0` | `$watertimefreq1` |
| `ReflectAmount` | `50.0` | `$reflectamount` — declared, not wired | | `WaterTimeFreq2` | `0.0` | `$watertimefreq2` |
| `BaseReflectFract` | `0.0` | declared, not wired — SLW's own Fresnel replaces the `Fresnel` node this pin used to feed; the *shipped binary's* default, not the design-era source's: `waterreflect_old`/`waterreflect_ps20_old` read no `c3` register at all, so R0 = 0 in the shipped game | | `WaterWaveHeight` | `0.0` | `$waterwaveheight` |
| `WaterDepth` | `64.0` | `$waterdepth` — VBSP's per-instance depth; the volume the map lane stages carries the real one | | `WaterWaveLength` | `0.0` | `$waterwavelength` |
| `WaterMurkiness` | `0.0` | `$watermurkiness` | | `CheapWaterStartDistance` | `0.0` | `$cheapwaterstartdistance` |
| `WaterBaseFactor` | `0.0` | `$waterbasefactor` | | `CheapWaterEndDistance` | `0.0` | `$cheapwaterenddistance` |
| `WaterBaseMovementDist` | `0.0` | `$waterbasemovementdist` | | `FogStart` | `1.0` | `$fogstart` — now the SLW extinction range, not a pixel-depth fog tail (below) |
| `WaterBaseMovementFreq` | `0.0` | `$waterbasemovementfreq` | | `FogEnd` | `400.0` | `$fogend` |
| `WaterSpecularMin` | `0.0` | `$waterspecularmin` | | `NormalFrameRate`, `NormalFrameCount` | `0.0`, `1.0` | the `animatedtexture` proxy (20 instances) |
| `WaterSpecularMax` | `1.0` | `$waterspecularmax` | | `BumpScrollRateU`, `BumpScrollRateV` | `0.0` | the `texturescroll` proxy (18 instances) |

| Vector | Default | From |
|---|---|---|
| `WaterColor` | `(0, 0, 0, 0)` | `$watercolor` |
| `RefractTint` | `(1, 1, 1, 1)` | `$refracttint` — VtMB's `c1` in `waterrefract.psh`; now **Color Scale Behind Water** (below) |
| `ReflectTint` | `(1, 1, 1, 1)` | `$reflecttint` — its luma now scales the class specular (below) |
| `FogColor` | `(0, 0, 0, 0)` | `$fogcolor` — the one key in the corpus authored in `{0–255}` form; gamma-decoded into the SLW extinction split (below) |
| `EnvMapTint` | `(1, 1, 1, 1)` | `$envmaptint` |
| `TexScaleOffset` | `(1, 1, 0, 0)` | `$scale` (20 units) → `.xy`; `$bumpoffset` (13) → `.zw`, and it is also the `texturescroll` target |

**Static switches**, every one defaulting `false` on the master: `CheapWater` (`$forcecheap`, 2
units), `UseFogEnable` (`$fogenable`, 23), `UseBaseTexture` (18 of 24 water units bind no
`$basetexture` at all, so a `true` default would grey-checker the majority; the stage resolves it
per unit the same way Lit/Unlit/Refract do), `UseAnimatedNormalFrames`, `UseNormalMap`, and the new
**`Underside`**. `UseFixedCube` still gates the fixed-cube emissive add. `UseEnvMap` is now
**declared, not wired**: the switch node stands in the graph (a review artefact of the old
Fresnel/`ReflectAmount` branch it used to gate) but drives nothing — the cube emissive is gated
`UseFixedCube` alone, matching every other master's reflection contract. Two water keys are
**housed outside the material**: `$bumpframe` (20) is the `animatedtexture` proxy's frame-number
variable and becomes the flipbook slice index rather than a parameter of its own, and
`$subdivsize` (13, values 64 and 16) is Source's water-surface tessellation size — geometry, owned
by the map lane, provenance only here.

**The SLW translation** (`_build_water`'s own account of ruling A, `make_v2_materials.py:1927`).
VtMB's `Water_Old` is two render-target passes — `_rt_WaterRefraction` perturbed by the DUDV and
tinted `$refracttint`, `_rt_WaterReflection` perturbed the same way, Fresnel'd and tinted
`$reflecttint`, additive — plus a linear fog of everything below the plane
(`SetFogVolumeState`/`MATERIAL_FOG_LINEAR_BELOW_FOG_Z`). Ruling A replaces the three with the one
SLW output node (`MaterialExpressionSingleLayerWaterMaterialOutput`, wired by pin name through
`connect_material_expressions` like the thin-translucent output `make_world_materials.py::
M_World_Glass` already uses), fed four ways:

- **`ScatteringCoefficients` / `AbsorptionCoefficients`** (1/cm): `range = max((FogEnd − FogStart)
  × 2.54, 1)`; `σ = WaterFogScale / range` (the `MPC_ElysiumSurfaces` knob
  `UElysiumSurfaceSettings::WaterFogScale`, default `1.386294` = 2·ln 2 — the value at which SLW's
  exponential transmittance and VtMB's linear fog agree at the half-fog distance, §4.2 of the
  architecture doc); `c = pow(FogColor.rgb, 2.2)` (the same decode `ElysiumFog::DecodeColor` gives
  the scene fog). `Scattering = c × σ`, `Absorption = (1 − c) × σ`. `UseFogEnable` off zeroes both
  (`$fogenable 0` is `FogMode(0)`: clear water). **`Underside` zeroes both too** — the
  `$bottommaterial` faces are seen only from inside the volume, whose fog is the post-process
  (§6), and 5.8's SLW camera-under-water branch is hardcoded off (`const bool CameraIsUnderWater =
  false;`, `BasePassPixelShader.usf:1698`), so the underside must not integrate "water" over the
  above-water world it refracts. `CheapWater` multiplies `σ` by `16` (ruling F: the cheap program
  never reads the refraction RT and lerps `lerp(fogcolor, cube, fresnel)`, so the body reads as its
  `$fogcolor` at any depth).
- **`ColorScaleBehindWater`** = `RefractTint` (`mul r0, t2, c1`, the refract pass's own tint).
- **`PhaseG`** = `0` — VtMB has no phase term.
- (the fourth wire is `MP_NORMAL`, below — SLW reads the surface normal directly, not a fifth
  output pin.)

**Normal**: the flipbook lane, unchanged in graph shape — `_flipbook_sample` over `NormalMap`/
`NormalMapFrames` at `NormalFrameRate`/`NormalFrameCount` (the `animatedtexture` proxy), over the
`BumpScrollRateU/V` panner, gated `UseNormalMap`, feeding `MP_NORMAL` at unit strength. Before
R7.1 every water instance shipped a **flat normal** regardless: `$normalmap` and `$bumpmap`
(`dev/water_normal`, `dev/water_dudv`) are both 29-frame VTFs that stage as `Texture2DArray`s
(`TA_water_normal` + its `_linear` twin, `TA_water_dudv`), so neither ever bound the master's plain
2D `NormalMap`/`DuDvMap` slots, and the `animatedtexture` proxy (which reads `$bumpmap` off
`animatedtexturevar`) bound the **DUDV** array into `NormalMapFrames` — the slot `$bumpmap` lands
on for the water family — while never setting `UseNormalMap` at all. The stage fix
(`importers/materials.py`'s `animatedtexture` branch, `family == "water" and normal_lane`): on the
water family the frames array bound into `NormalMapFrames` is `$normalmap`'s own texture, not
`$bumpmap`'s (the proxy still supplies the shared rate — `$bumpframe` indexes both textures'
frames in `Water_Old`); and `_apply_texture_switch_pairs` now treats a bound `NormalMapFrames` as
"the slot is bound" for the `UseNormalMap` gate, not only a bound static `NormalMap` — before this,
every water instance animated a normal that `UseNormalMap` then discarded. `DuDvMap` stays unbound
and declared, per the table above.

**Specular** = `class_specular × luma(ReflectTint)`, forced to `0` under `Underside`
(`Mod_LoadFaces`'s own strip). SLW applies its own Schlick from `Specular`; the graph's `Fresnel`
node and `BaseReflectFract` pin from the pre-R7.1 lane are gone, `BaseReflectFract` staying
declared above. **Roughness** / **Metallic** are the class LUT read, unchanged — Lumen honours SLW
roughness in 5.8 (§8). **Emissive** is the authored fixed-cube add, unchanged (`cube × EnvMapTint ×
FixedCubeStrength × ReflectTint`, gated `UseFixedCube`; `FixedCubeStrength` is the same
`MPC_ElysiumSurfaces` knob `M_V2_Lit`'s and `M_V2_Refract`'s reflection contract reads). **Base
Color** is unchanged (`BaseTexture × Color × RefractTint`, lerped toward `WaterColor` by
`WaterMurkiness`) and invisible while Opacity is `0`.

**Opacity is coverage, not murk** (`WaterVisibility = 1 − Opacity`, `BasePassPixelShader.usf:1140`
— §8): `UseBaseTexture ? Alpha × BaseTexture.a : 0`. The old fog tail this pin used to blend toward
`FogColor.a` — the shipped cheap program's own tail, `watercheap_ps11`/`watercheap_ps20_old`:
`mad r0.xyz, F, reflect, c0(g_FogColor)` / `mov r0.w, c0.w` — is gone: Absorption is the fog now,
and SLW has no refraction pin left to feed either (`MP_REFRACTION` is not written on this master
any more). The four base-textured `Water` units (`dev_water`, `nether01_water`,
`oilfieldwater a/b`) are placed on no water map; they keep their alpha as coverage, provenance-level
only.

**Declared, not wired** (provenance the instance still carries, none of it feeding a pin):
`DuDvMap` (the DX8 render-target-offset field; SLW refracts along `MP_NORMAL`, which has no second
channel to offset against — the sample node stands in the graph, unconnected), `RefractAmount` /
`ReflectAmount` (the DUDV's warp strengths, not intensities — meaningless once the DUDV offset is
gone), `BaseReflectFract`, `UseEnvMap` (the cheap cube is Lumen's mirror now, gated `UseFixedCube`
instead), and the wave-animation scalars (`WaterBaseFactor`, `WaterBaseMovementDist/Freq`,
`WaterTimeFreq1/2`, `WaterWaveHeight/Length`, `WaterSpecularMin/Max`,
`CheapWaterStartDistance/EndDistance`, `WaterDepth`) — vertex/World-Position-Offset concerns, out
of this generator's scope, unchanged from before R7.1.

**`M_ElysiumUnderwater`** is a second, separate asset this same generator authors
(`_build_underwater`, `/ElysiumGenerated/Materials/V2/M_ElysiumUnderwater`) — `MD_PostProcess`,
not one of the nine `M_V2_*` surface masters, so it carries none of the tables above. Its own three
parameters (`FogColor`, `FogStart`, `FogInvRange` — the same names `ElysiumFog::ApplyToDecalMID`
writes, `UNDERWATER_PARAM_TABLE` in the generator) drive `lerp(SceneTexture:PostProcessInput0,
FogColor, saturate((SceneDepth − FogStart) × FogInvRange))`, blended in by the water actor's
`IInterface_PostProcessVolume` implementation while the view sits inside a volume
(`water-architecture.md` §6). `Elysium.Substrate.WaterActor` pins the MID triple by reflection; a
tenth `FElysiumV2MasterCase` in `ElysiumV2MaterialTests.cpp` pins its two scalars and one vector
against the compiled graph the same way every `M_V2_*` case does.

##### `M_V2_Sprite`

| Name | Kind | Default | Notes |
|---|---|---|---|
| `BaseTexture` | T2D | `/Engine/EngineResources/DefaultTexture` | colour |
| the base-animation lane | | | 12 of 62 host an `animatedtexture` |
| `UseVertexColor` | # | `false` | `SpriteRenderTransColor` and the `*_vertexcolor` vertex programs |
| `UseVertexAlpha` | # | `false` | `$vertexalpha` — SF-4.3 part 3 orchestrator ruling: absorbs the rerouted `$ignorez` unit (`engine/vertexcolorblend`, one of the 5 `unlitgeneric` units re-routed to this master) that authors `$vertexalpha`, so it stages cleanly instead of failing "not exposed" (see "Per-unit divergences" below — this one is *not* in that allowlist, because it now has a real destination) |

`$spriterendermode` is **not** a parameter: it selects blend state, and blend state is a
per-instance override. `$spriteorigin` (54 units) is **not** a parameter either — like
`$spriteorientation` it belongs to the sprite *component*, and the sprite placement lane owns both.
`$curve` (3 units, `0.2`) is read by a `subtract` inside a `playerproximity` chain, so it is a
runtime-factory operand carried in provenance, not a master parameter.

`bDisableDepthTest` (from `$ignorez`) is set unconditionally on the `M_V2_Sprite` master itself —
material-only, never a per-instance override — but it only actually changes the visible result for
a *translucent* sprite instance: an opaque sprite still writes and tests depth through its own
opaque pass regardless of this flag (the render state that flag actually gates only applies to the
translucent pass). `used_with_instanced_static_meshes` is also set (a placement-lane world sprite
may use ISM, not only Niagara particles).

##### `M_V2_Refract`

| Name | Kind | Default | Notes |
|---|---|---|---|
| `BaseTexture` | T2D | `/Engine/EngineResources/DefaultTexture` | colour |
| `DuDvMap` | T2D | `/Engine/EngineMaterials/DefaultNormal` | data → `_linear` twin (`$dudvmap`, 9 units) |
| `NormalMap` | T2D | `/Engine/EngineMaterials/DefaultNormal` | data → `_linear` twin (`$normalmap`, 8) |
| `EnvMap` | TCube | `/Engine/EngineResources/DefaultTextureCube` | colour (`$envmap`, 9) |
| `RefractAmount` | S | `20.0` | `$refractamount` |
| `RefractTint` | V | `(1, 1, 1, 1)` | `$refracttint` |
| `EnvMapTint` | V | `(1, 1, 1, 1)` | `$envmaptint` (7) |
| `FogColor`, `FogStart`, `FogInvRange`, `FogInscatter` | V, S, S, S | primitive-driven, primitive-driven, primitive-driven, `1.0` | the scene-fog lane (R5.4, "Scene fog on the world masters") |
| `UseBaseTexture`, `UseNormalMap`, `UseEnvMap`, `UseFixedCube` | # | `false` | `UseBaseTexture` defaults `false` on the master (both heatglow units and most refract units bind no `$basetexture`); the stage resolves it explicitly per unit, same rule as Lit/Unlit/Water |

`ForceRefract` is **dropped**: `$forcerefract` is set by **0** of the 11,624 units and by no
proxy, so the switch had no source.

The `DuDvMap` ripple is scaled by `NormalMap.a x RefractAmount` (`fxc/refract_ps20`'s own `scale =
normalMap.a x RefractAmount`), not an unconditional additive delta — an unbound `DuDvMap` still
makes the whole perturbation an exact no-op regardless of the scale, the same "default makes the
knob inert" shape every other lane uses. `FixedCubeStrength` (an `MPC_ElysiumSurfaces` knob)
already scaled this master's fixed-cube add.

##### `M_V2_Decal`

**The projector master (R7.2).** Domain `MD_DeferredDecal`, blend `BLEND_Translucent`, shading
model Default Lit, not two-sided, no Nanite usage flag. This is the one material a
`UDecalComponent` will actually draw: `FDeferredDecalProxy` substitutes
`UMaterial::GetDefaultMaterial(MD_DeferredDecal)` for anything else (`DecalComponent.cpp` 5.8,
lines 12–19, 51–60), and every other V2 master is `MD_Surface`. The domain is why this master is
not simply `M_V2_LitTranslucent` with a flag: domain is material-only, so a projector cannot be an
instance of a surface. It is also what makes a `$decal` face group work — a non-Nanite static-mesh
section on a decal-domain material draws in the **mesh-decal** pass
(`PostProcessMeshDecals.cpp` 255), which is the native answer to Source's `$decal` polygon offset.

| Textures | Kind | Default | Class |
|---|---|---|---|
| `BaseTexture` | T2D | `/Engine/EngineResources/DefaultTexture` | colour — the stain |
| `Emissive` | T2D | `/Engine/EngineResources/DefaultTexture` | colour — Source's self-illum source *is* the base texture, so the stage binds the same image here and leaves `EmissiveScale` to decide (3 `$selfillum` units) |

| Scalar | Default | | Vector | Default |
|---|---|---|---|---|
| `Alpha` | `1.0` | | `Color` | `(1, 1, 1, 1)` |
| `EmissiveScale` | `0.0` — inert until `$selfillum` says otherwise | | `FogColor` | neutral (R5.3's instance-parameter fog) |
| `FogStart`, `FogInvRange` | neutral | | | |

Static switch: **`Unlit`** (`false`) — the 28 `unlitgeneric` projectors. There is no unlit *shading
model* to fall back to on a decal that must also be lit, so the switch routes the fogged base
colour into `MP_EMISSIVE_COLOR` and zeroes `MP_BASE_COLOR` instead. `UseVertexColor` is **gone**: a
projected decal has no vertex colour to read.

The graph is `BaseTexture.rgb × Color` → BaseColor, `BaseTexture.a × Alpha` → Opacity,
`Emissive × EmissiveScale` → Emissive, over a `(U, 1−V)` texture coordinate — a deferred decal's
projected V runs the other way, and the flip is ported verbatim from the retired
`make_decal_material.py`.

**Roughness, Specular, Metallic and Normal are not connected** (ruling 1). An unconnected property
pin means the receiver keeps its own value, which is exactly what a lightmapped `$decal` face did
in Source: the stain changes the wall's colour and nothing else about its surface. The master still
*declares* `SurfaceClassLUT`/`SurfaceClassIndex` (the four names every master carries) so the
three-way name pin covers it, with nothing downstream.

`DecalDepthOffset` is not on this master and is not a knob anywhere either — R7.2 retired it
outright (see "Four parameters that left the masters"): a mesh decal has nothing to bias.

##### `M_V2_TwoTexture`

| Textures | Kind | Default | Class |
|---|---|---|---|
| `BaseTexture` | T2D | `/Engine/EngineResources/DefaultTexture` | colour |
| `BaseTexture2` | T2D | `/Engine/EngineResources/DefaultTexture` | colour — bound by `$basetexture2` (88) **and** `$texture2` (5), which are the same slot under two family spellings |
| `NormalMap` | T2D | `/Engine/EngineMaterials/DefaultNormal` | data → `_linear` twin (`$bumpmap`, 5) |

| Scalar | Default | From | | Vector | Default | From |
|---|---|---|---|---|---|---|
| `AlphaBias` | `0.0` | `$alpha_bias` (4, float `0.2`) | | `TexScaleOffset` | `(1, 1, 0, 0)` | `$texscale` (4, **scalar** `.25`) → `.xy`; `$texoffset` (4, **vector** `[0 0]`) → `.zw`; also `$basetexturetransform` |
| the scene-fog lane (R5.4) — `FogStart`, `FogInvRange` primitive-driven, `FogInscatter` `1.0` | | | | `FogColor` | primitive-driven (CPD 0..3) | `ElysiumFog::Pack`, never the VMT |
| the base-scroll and sine lanes | | | | `Texture2ScaleOffset` | `(1, 1, 0, 0)` | `$tex2scale` (4, **scalar**) and `$texture2scale` (1, **scalar** `10.0`) → `.xy`; `$tex2offset` (4, **vector**) → `.zw`. The proxy component targets land here too: `$tex2offset[1]` → `.w`, `$texture2offset[0]` → `.z`, `$texture2transform` → the whole vector |

Static switches: `UseBaseTexture2` (`false`), `UseNormalMap` (`false`), **`UseBumpOnBaseTexture2`**
(`false` — `$bumpbasetexture2withbumpmap`, 4 units, now wired: `true` samples the shared `NormalMap`
through `BaseTexture2`'s own UV [`Texture2ScaleOffset` on the raw, untransformed coordinate — see
below], `false` keeps sampling it through `BaseTexture`'s; M6 review fix, corrected from an earlier
draft that declared the switch and never connected it to anything), `UseVertexColor` (`false`),
`UseVertexAlpha` (`false`).

**`Texture2ScaleOffset` is independent of `TexScaleOffset`, not composed on top of it** (M5 review
fix, corrected from an earlier draft that derived `BaseTexture2`'s UV from `TexScaleOffset`'s own
already-scaled/panned `base_uv`): both apply to the same raw, untransformed texture coordinate, so
scaling or offsetting one layer never drags the other's transform along with it, matching this
table's own framing of the two as independent vectors.

**`Opacity`/`OpacityMask` are saturated and share one source** (M6 review fix, corrected from an
earlier draft that left `Alpha + AlphaBias` unsaturated and wired no `MP_OPACITY_MASK` source at
all — a masked-override instance of this master clipped nothing regardless of `Alpha`): both
property sinks now read `saturate(Alpha + AlphaBias)` (times `VertexColor.a` under
`UseVertexAlpha`), the same shared-Opacity/OpacityMask-term pattern `M_V2_Unlit`/`M_V2_Sprite`/
`M_V2_Eyes` already use for their own Unlit-shaded outputs.

`$j_basescale` (4, integer `2`) is a `add`-proxy operand, not a shader value; it stays a proxy
scratch register. `$detail` on the one `worldtwotextureblend` unit is provenance only (see
"`Detail` is dropped").

#### `Detail` is dropped *(provisional owner call)*

`$detail` is authored by **28** units, and none of them is shipped-content detail texturing: 26
bind a `shadertest/*` image (`shadertest/shadertestdetail` 23, `shadertest/detail` 2,
`shadertest/WorldTwoTextureBlend_detail` 1) and the other 2 are `shatteredglass` units whose
`$detail` value is a *cracked-glass material path*, duplicating their own `$crackmaterial`.
`shader_combos.md` is explicit that **this build's `VertexLitGeneric` registers no `$detail`
parameter at all** and that all 13 `_detail*` combos are unreachable — retail never drew one.
Carrying `Detail`/`UseDetail`/`DetailScale` would therefore be a deliberate divergence built for
zero shipped surfaces, so `$detail`, `$detail2`, `$detailscale` and `$detailscale2` are
**provenance only** and the three parameters leave every master. Consequence to state once: the two
`sine` proxies whose `resultvar` is `$detailscale` (both on `shadertest/` units) name a
provenance-only key; they emit no nodes and record `proxyTargetProvenanceOnly` in `Omissions` —
that is a *named* destination, so the no-silent-drop rule still holds.

#### Four parameters that left the masters

Each was declared with no formula anywhere in this design; rather than ship a parameter the stage
writes and the graph ignores, each now has a real home.

| Was | Now | Why |
|---|---|---|
| `DecalDepthOffset` (S) | **nothing — retired entirely (R7.2)** | it was a knob looking for a bias to apply. The `$decal` faces now draw in the mesh-decal pass and the projectors are `UDecalComponent`s; neither has a depth to offset, so the scalar left `UElysiumSurfaceSettings`, `MPC_ElysiumSurfaces`, `Config/DefaultElysium.ini` and the knob test rather than sit unread |
| `IsDecalSurface` (#) | provenance `isDecalSurface` (`$decal`, 550 units) — **read (R7.2)** by the material stage, which names the unit's projector twin, and by the map stage, whose face group binds that twin as its mesh slot and stays out of the Nanite bucket | the flag never needed a switch on the surface master: it selects a *second instance on a different master*, which is a staging decision, not a shader branch |
| `MinLight`, `MaxLight` (S) | provenance only (11 units each) | they clamp Source's lightmap term; Lumen owns lighting, so there is no expression to feed. A named divergence |

`WetnessScale` **left this table** (R5.3, "Decal fog and wetness homes" below): it is a real
per-instance scalar again on the two masters that carry a wetness lane (`M_V2_Lit`,
`M_V2_LitTranslucent`), reversing the "provenance only" call this table made on 2026-08-31 for
those two masters specifically — every other master still carries it in provenance only, since it
declares no wetness lane at all.

#### Decal fog and wetness homes (R5.3)

Two axes vary **per map** (decal fog) or **live, per tick, world-scoped** (wetness), and neither
had a home on a V2 master before this revision — both are new here, not a "provenance only" call
being reversed like `WetnessScale`'s table row above is. The life sweep (`seam_migration.md`
→ "## Roadmap — one pipeline") named the defect: *"the shared-MI switch is blocked until decal
fog and wetness have a home that is not a per-map material instance."* R5.4's plan is one shared
`MI_<unit>` per corpus material, resolved by `vtmb:material:*` and used identically on every map
that places it — a home that bakes a map-specific value onto that shared instance (a per-map
`MaterialInstanceConstant` package, which is exactly what the legacy `M_World_*`/`M_Decal` lane
still does via `bake_map.py`'s `SC.is_map_scoped_material(decal=..., wetness_driven=...)`) would
keep every decal- or wetness-bearing material map-scoped forever, defeating the switch for exactly
the surfaces that need it most (**19** `globalwetness` units, **38** `decalmodulate` units).

**Two candidates were on the table:**

1. **MID-at-load for decals only** — an anonymous `UMaterialInstanceDynamic`, parented to the
   shared `MI_<unit>`, created once per decal placement and never itself a tracked asset.
2. **A per-map `MaterialInstanceConstant` child of the V2 master**, carrying only fog/wetness
   parameters sourced from the R4.4 `UElysiumMapEnvironment` asset — i.e., continuing the legacy
   lane's own pattern, ported onto the V2 masters.

**Ruling (owner call, R5.3): candidate 1 for fog, and — because wetness turns out not to need
either candidate — a third, lower-cost mechanism for wetness that neither the sweep nor the two
candidates named, but which the legacy `make_world_materials.py` graph already proves live:**

- **Decal fog is MID-at-load.** A `UDecalComponent` is a `USceneComponent`, not a
  `UPrimitiveComponent` (`ElysiumFog.h`'s own "WHY IT IS PER-PRIMITIVE" note): it carries no
  Custom Primitive Data, so the world/sky/prop mesh mechanism (CPD, already shared-MI-safe because
  the value rides the *primitive*, never the material) does not reach it. `M_V2_Decal` now
  declares three named instance parameters (`FogColor`/`FogStart`/`FogInvRange`,
  `mat_fog.fog_from_params`, mirroring the already-shipped legacy `M_Decal` graph exactly) that
  default neutral (unfogged) so an untouched instance renders exactly as before. `ElysiumFog::
  ApplyToDecalMID` (`Source/ElysiumUE/Public/ElysiumFog.h`) is the one function both a future
  runtime decal-spawn consumer and the placement lane call, reusing the
  exact `ElysiumFog::Pack` math the CPD path already uses. Decals are placed once each (never
  batched across maps into one shared mesh instance — `bake_map.py`'s `_place_decals` places one
  `ADecalActor` per `.decals` line), so an anonymous per-placement MID costs nothing the map does
  not already pay for every decal actor it places, and it is never a tracked, prunable, per-map
  `.uasset`. **The MID is created at map load, not at bake (R7.2, ruling 4)** — a bake cannot save
  a `UMaterialInstanceDynamic` into a level in the first place. `UElysiumDecalSubsystem` adopts
  every `elysium.decal` component `UElysiumMapVisuals::AdoptBakedLevel` walks, parents one MID per
  component to the bound projector twin and stamps it; `ApplySceneFog` re-stamps through the same
  owner, so a live fog change reaches every decal, baked or laid.
  No corpus unit authors a fog VMT key on a `decalmodulate` shader (only two non-decal
  families, `water/cheap_water` and `water/invisible_water`, do — both already provenance-only
  divergences above), so the stage never populates these three parameters; only the placement lane
  does, from the map's own fog, never from a per-map material package.
- **Wetness needs no per-map or per-placement instance at all**, because the "live, per tick"
  half of the term is genuinely global, not per-map: `MPC_ElysiumEnvironment`
  (`make_world_materials.py::make_environment_collection`, `AElysiumMapActor::ApplyWeatherTuning`
  its sole writer) already carries `GlobalWetness`/`WetnessOutputScale` as one world-scoped live
  value, read into every legacy `M_World_*` graph today via a `CollectionParameter` node — the
  exact "global settings page, never a per-instance literal" shape this doc's "Knob contract"
  already mandates for every other live system value. The only thing missing on the V2 side was
  the *static* half — `WetnessScale`, the per-unit multiplier `globalwetness`'s own `scale`
  argument authors — which is an ordinary per-instance scalar like `Color` or `EnvMapTint`, no
  different from every other value this stage bakes once at import. `M_V2_Lit`/
  `M_V2_LitTranslucent` now declare `WetnessScale` and its `WetnessDriven` gate as real scalars
  (`ElysiumSurfaceParamsLit::Scalars`) and read `GlobalWetness`/`WetnessOutputScale` off
  `MPC_ElysiumEnvironment` through their own `CollectionParameter` node (`_wetness_response` in
  `make_v2_materials.py`, ported verbatim from the legacy graph's own term), raising the
  reflection mask exactly like `M_World_*`'s `env_wet` term does. `WetnessDriven` defaults 0, so
  `lerp(1, wet_amount, 0) == 1` — a surface the stage never marked wetness-driven is untouched.
  The stage writes both only for units whose family resolves to `M_V2_Lit`/`M_V2_LitTranslucent`
  (`_LIT_FAMILIES`) — the only masters with a wetness lane; every other master still carries
  `wetnessScale` in provenance only, per the table above.

**Landed for the three-map corpus (R5.3, 2026-09-01):** the master-graph and stage-contract halves
above are in `make_v2_materials.py`/`pipeline/src/elysium_pipeline/importers/materials.py`/
`ElysiumSurfaceParams.h`/`ElysiumFog.h`, pinned by the existing offline parity tests
(`test_param_tables_are_pinned_against_the_stages_exposed_params`,
`test_lit_master_exposed_params_pinned_against_cpp_header`) plus a new one for decal
(`test_decal_master_exposed_params_pinned_against_cpp_header`, already existed and now covers the
three fog names too) and `Elysium.Substrate.FogDecalMID`. **Closed by R7.2 (2026-09-03):** the
caller is `UElysiumDecalSubsystem`, not the bake — it owns every decal MID in a world and is the
single place `ElysiumFog::ApplyToDecalMID` is called from, for a baked decal and a runtime-laid one
alike. Roadmap 3.13 ("decal fog: accept or extend") therefore closed as **extend**. The three
working maps re-import and re-bake clean on the new lane, and a boot reports one MID per baked
decal: 123 on `sp_tutorial_1`, 38 on `sm_pawnshop_1`, 219 on `sm_hub_1`.

#### Scene fog on the world masters (R5.4)

R5.4 rebinds every world, brush-model and 3D-skybox face of a converted map onto the imported
`MI_` its `vtmb:material:*` unit became (`seam_map_map.md` → "## Import — materials (R5.4)"), and
the task's own acceptance line asked for one check first: *"the V2 Unlit/Lit masters' fog CPD path
must match what `ApplySceneFog` stamps."* The check found that **no V2 master had a scene-fog term
at all.** `mat_fog.fog_from_primitive` — Source's per-map distance fog as the Custom-Primitive-Data
term `ElysiumFog.h` calls *"the whole contract between the material graph, the bake and
`AElysiumMapActor::ApplySceneFog`"* — was wired into every legacy `M_World_*`/`M_Refract`/
`M_Additive` master and into none of the nine V2 masters; only `M_V2_Decal` read `mat_fog` at all,
through R5.3's instance-parameter variant. The R5.3 ruling above leaned on *"the world/sky/prop
mesh mechanism (CPD, already shared-MI-safe because the value rides the primitive)"* as though it
already reached the V2 masters — it did not, and every R1 prop already standing on a V2 `MI_` on
the three converted maps was being stamped by `ApplySceneFog` and fogging nothing.

**Ruling (R5.4): the five masters a map surface or prop slot binds carry the primitive term,
verbatim from the legacy graph.** `M_V2_Lit`, `M_V2_LitTranslucent`, `M_V2_Unlit`,
`M_V2_TwoTexture` and `M_V2_Refract` (`importers.materials.SCENE_FOG_MASTERS`) each end with
`make_v2_materials._scene_fog`: `mat_fog.fog_from_primitive` reading `FogColor` (CPD 0..3),
`FogStart` (CPD 4) and `FogInvRange` (CPD 5) — the slots `ElysiumFog::Pack` writes, the bake
stamps as default primitive data and `UElysiumMapVisuals::ApplySceneFog` re-stamps live — and
applying it so the shaded result is exactly `lerp(shaded, fogColour, f)`: `BaseColor *= 1 - f`,
`Specular *= 1 - f` (or a Lumen reflection shines through the haze), `Emissive = Emissive · (1 - f)
+ fogColour · f`. Neutral by construction — an unwritten slot reads 0, `f = 0`, nothing changes —
so a prop standing in the lookdev map or a character on `M_V2_Lit` (R6.1) renders exactly as
before. `M_V2_Unlit` has no specular pin and its shading model renders `Emissive` alone, so the
inscatter lands there; `M_V2_TwoTexture` has no emissive of its own, so its fogged emissive is the
inscatter alone.

**One instance value, `FogInscatter` (S, default `1.0`).** Source forces the fog colour to black
under additive blending — an additive surface *fades out* in fog; inscattering the haze on top of
it would brighten the scene — and the graph cannot read its own blend mode, so the stage writes
`FogInscatter = 0` for an `Additive` instance of a scene-fog master (`_apply_scene_fog_inscatter`)
and nothing for any other. The three primitive-driven names are never written by the stage: they
are declared (`ElysiumSurfaceParams{Lit,Unlit,TwoTexture,Refract}`, `EXPOSED_PARAMS`) so the
three-way name pin (`header ↔ EXPOSED_PARAMS ↔ *_PARAM_TABLE ↔ graph`) covers them, and a fourth
pin checks the *indices*: `test_scene_fog_masters_read_the_custom_primitive_data_slots_apply_scene_fog_stamps`
parses `ElysiumFog.h`'s `SlotColor`/`SlotStart`/`SlotInvRange` and asserts each fog parameter node
on each of the five masters is CPD-driven at exactly that index — a name-only match would compile
and fog nothing.

**Not on the lane, and why.** `M_V2_Water` already exposes `FogColor`/`FogStart`/`FogEnd` as the
VMT's own *water-fog* keys (`$fogcolor`/`$fogstart`/`$fogend`), a different term under the same
names; this rebind puts 2 surfaces on `sm_hub_1` onto `M_V2_Water`. **R7.1 (2026-09-04) closed the
question rather than deferring it** (ruling H, `water-architecture.md` §1): `M_V2_Water` takes no
scene-fog term, on this master or ever — the refracted world already carries the scene's own fog
(it is the lit scene sampled through SLW's refraction), and the Lumen mirror carries whatever the
reflected primitives carry, so a second, additive fog term on the water's own scatter/specular
would double it. *Named modernization*: VtMB draws the water surface under `EnableWorldFog()`
like everything else; here it does not, and a far canal end that pops against its walls unfogged is
recorded as divergence 10 in `water-architecture.md` §9, a tuning-session witness rather than a
defect. `M_V2_Sprite` is R7.4's, `M_V2_Eyes` R6.1's, and `M_V2_Decal` keeps R5.3's
instance-parameter variant.

**The decal placement lane did not rebind in R5.4 — a domain fact, not a scope call, and R7.2
answered it.** R5.3 had planned the placement lane's `ADecalActor` onto an MID parented to the
shared `MI_<unit>`. Two facts stopped it: (1) `UDecalComponent` renders **only** an
`MD_DeferredDecal`-domain material — `DecalComponent.cpp` (5.8) substitutes
`UMaterial::GetDefaultMaterial(MD_DeferredDecal)` for anything else (lines 51–58, 97–99, 553) —
and every V2 master, `M_V2_Decal` included, was `MD_Surface`; (2) the materials the three maps'
`.decals` lines name are not `decalmodulate` units at all but ordinary `$decal` surfaces — 96 of
the 97 distinct decal materials over the three maps resolve to `M_V2_LitTranslucent`, one to
`M_V2_Unlit`. So there was no V2 asset a decal component could draw, and `_place_decals` kept
binding the legacy per-map `M_Decal` MIC. **R7.2 (2026-09-03) closed both**: `M_V2_Decal` is
re-cut as the `MD_DeferredDecal` projector master, and a projected unit stages a projector twin
beside its surface instance (see "Two instances" and the master's own section), which is what
`_place_decals` and an `isDecalSurface` face group bind by name. `/ElysiumBaked/<map>/Materials`
and `.../Materials/Decals` are now *pruned* on both bake lanes and authored by neither: a converted
map writes no material package of its own. The legacy `M_Decal` and `make_decal_material.py` are
retired; `ElysiumFog::ApplyToDecalMID` and `Elysium.Substrate.FogDecalMID` stayed, and their caller
is `UElysiumDecalSubsystem`.

#### Detail sway on the model masters (R6.3)

R6.3 (`seam_map_map.md` → "Detail props (R6.3)") places the `dprp` game lump as instanced
components, and the owner ruled that **sway is wired**: the record's `swayAmount` byte is in the
data on 35,521 of the 143,412 records, so the weeds move. The three masters a model family can
land on — `M_V2_Lit`, `M_V2_LitTranslucent` and `M_V2_Unlit` (every detail material on the working
corpus is `unlitgeneric` → `M_V2_Unlit`: `grassa`/`grassb`, `weedb`/`weedbleaves`, `weedc`,
`weedda`/`weeddb`, `rocksmall`, `trashpile`) — carry one World Position Offset term,
`make_v2_materials._detail_sway`, behind one static switch, `UseDetailSway` (default `false`):

```text
sway    = PerInstanceCustomData[0]                      -- swayAmount / 255, 0 on any non-instanced draw
weight  = saturate((local.z - bounds.min.z) / (bounds.max.z - bounds.min.z))
                                                        -- local = TransformPosition(WorldPosition, World -> Instance),
                                                        -- bounds = ObjectLocalBounds: the base stays put, the tip moves
phase   = (world.x + world.y) / 2.54                    -- Source's own per-object phase, in its own inches
s       = sin(Time + phase)
WPO     = (s, s, 0) * sway * weight * DetailSwayAmplitude
```

`Time` is the material's shared clock — the one wind every instance on every map swings to — and
`DetailSwayAmplitude` is the **one knob**, a `UElysiumSurfaceSettings` scalar pushed into
`MPC_ElysiumSurfaces` like every other surface knob (Project Settings → Elysium → Surfaces →
Detail Props), in centimetres.

**Why the amplitude is an owner call, and what it defaults to.** VtMB's own client never reads the
byte: `CDetailModel` in `client.dll` is five functions (`100e0250`…`100e0300`) — construct,
destroy, the lighting product and a draw-colour multiply — and `CDetailObjectSystem::vfunc10`
(`100e0d90`) registers only `cl_detaildist`/`cl_detailfade`; there is no sway cvar and no sine
anywhere in the detail path. The byte was authored by VBSP for a feature this build of the engine
shipped without. So there is no VtMB amplitude to transcribe; the closest faithful number is the
first Source build that *did* read it, whose sprite-sway amplitude is `swayAmount / 255 ×
cl_detail_max_sway` with Valve's shipped value **5 world units**, and that is the default:
**12.7 cm**. The choice — keep Source's 5 units, or another number once the weeds are seen live —
is filed in the R7 list of `seam_migration.md`; nothing here is tuned.

**Why a static switch and a child instance, not a term on the shared `MI_`.** The translator
marks a material as using WPO only when the compiled chain is not a constant zero
(`HLSLMaterialTranslator::IsMaterialPropertyUsed`, `MP_WorldPositionOffset`), and a Nanite mesh on
a WPO material goes through the programmable raster path. With the switch off, the branch is not
compiled at all and every world chunk, brush, prop and character on these masters keeps the
shader it had. The map bake (`bake_map_v2._detail_sway_material`) authors, once per detail
material, `/ElysiumBaked/Meshes/Detail/MI_DetailSway_<material path>`: a `MaterialInstanceConstant`
whose parent is the imported `MI_` (so every VMT-derived binding is inherited, never restated) and
whose only own value is `UseDetailSway = true`, bound on the instanced component's slots. The stage
(`importers/materials.py`) declares the switch in `EXPOSED_PARAMS` for the three-way name pin and
**never writes it** — it has no VMT key; a material on a master without the switch (`M_V2_Eyes`,
`M_V2_Water`, `M_V2_Sprite`, `M_V2_Refract`, `M_V2_Decal`, `M_V2_TwoTexture`) fails the map bake by
name if a detail model ever binds one (none does on the corpus census: every one of the 41 detail
models is `scenery/`).

`GRAPH_VERSION` 4 → 5 (the topology changed on three masters); `Elysium.Policy.V2MasterParams`
pins the switch on the three, and `_probe_all_switches_true` compiles the WPO branch on each.

#### Ropes on `MI_`, and the factory shape (R6.5)

R6.5 (`seam_migration.md` → "Roadmap — one pipeline") takes the last runtime consumer of the six
legacy world masters — the overhead cables — onto this lane, and with it retires the runtime
material *builder*. Three rulings, all wiring:

**The `.ropes` sidecar carries the material's identity, not its pixels.** One line per cable
segment, **12** whitespace-separated tokens:

```text
vtmb:material:<key>  ax ay az  bx by bz  width_cm rest_cm nodes texscale flags
```

`<key>` is `shared_corpus.material_key` over the entity's `RopeMaterial` (or the `RopeShader`
row: 0 → `cable/cable`, 1 → `cable/rope`, 2 → `cable/chain`), the same key every other placement
lane names a material by; the eleven numbers are unchanged from the 14-token line
(`docs/vtmb/entity_visuals.md` → §5). The decoded `tex`/`bump` PNG paths and the `matflags`
shader-mode bits are gone: every one of them was a restatement of the VMT that the material
lane's `MI_` already carries (`$alphatest` → `BlendMode Masked`, `$translucent`, `$bumpmap` →
`NormalMap`, `$envmap` → `UseEnvMap`), and the runtime no longer selects a master or binds a
texture. Both producers — `UE_map_sidecars.write_ropes` (the default, R3.5) and
`UE_bsp_to_scene.write_ropes` (the byte-comparable legacy twin the R3.3 differ still diffs) —
write the same 12 tokens, and neither reads the shared corpus any more.

**The cable binds the imported `MI_`, resolved by the R5.4 rule.** `UElysiumMapVisuals::
BuildRopes` reads the id and resolves it exactly as `importers.materials.asset_path_for` named
the asset at import (`### Identity and naming` above):

```text
vtmb:material:<dir>/<stem>  ->  /ElysiumBaked/Materials/<dir>/MI_<safe stem>
                                <safe> = runs of [^A-Za-z0-9_] -> "_", leading/trailing "_" stripped,
                                          "unnamed" when nothing survives   (asset_names.safe_name)
```

`FElysiumContentPaths::BakedMaterial(id)` is that fold in C++, pinned against the Python by
`Elysium.Substrate.Ropes`. One `MI_` per distinct id per map, loaded once; an id whose asset does
not load is a **warning naming the path** and a cable left on the engine default — never a
silent fallback to another master. On the working corpus every rope is one of `cable/cable`,
`cable/chain`, `cable/chainb` and `cable/cautiontape` (`MI_cable`, `MI_chain`, `MI_chainb`,
`MI_cautiontape` under `/ElysiumBaked/Materials/cable/`), and `bake_verify.verify_ropes` asserts, for every line of the three maps' `.ropes`, that the id folds
to a package under `/ElysiumBaked/Materials/` that exists and is a `MaterialInstanceConstant`.
This resolution is not gated on `MapsOnV2Models`: the material lane imported the whole install,
the `MI_` exists for every map's ropes, and one code path is the point.

**`FElysiumMaterialFactory` is `Create(MI_, Outer)`, nothing else.** The factory no longer
*builds* a material — no master selection by blend flag, no texture load through
`FElysiumTextureCache`, no feature switch. It returns a `UMaterialInstanceDynamic` whose parent is
the imported instance and which carries **no parameter override of its own**: every VMT-derived
value is inherited from the `MI_`, wetness arrives through the `MPC_ElysiumEnvironment` write
(`Decal fog and wetness homes (R5.3)`), scene fog through custom primitive data (`Scene fog on
the world masters (R5.4)`). The MID exists so that a runtime bind, when one is ruled, has a
per-map-actor home that dies with the map — today nothing writes one, and
`Elysium.Substrate.MaterialFactory` pins both halves (parent is the instance; zero scalar, vector
and texture overrides; `nullptr` in → `nullptr` out). With the builder gone the six legacy world
masters (`M_World_Opaque`/`_Masked`/`_Translucent`/`_Glass`, `M_Refract`, `M_Additive`) have **no
runtime reader**; the assets, `make_world_materials.py` and the legacy `bake_map.py` lane that
still binds them on unconverted maps stay until R9.2.

**What retires with the builder.** `elysium.EmissiveScale`, `elysium.BumpScale` and
`elysium.EnvReflect` (the three cvars only the builder read; enhancement is post-roadmap tuning on
the editor surfaces, not a console multiplier); `elysium.EnhancedTextures` and
`FElysiumContentPaths::SharedTexHiDir` (the `tex_hi/` toggle — its one remaining reader was the
legacy sky assembly, which now takes the faithful `tex/` set unconditionally, as the R5.2 bake
already did); `UElysiumMapVisuals`'s `FElysiumTextureCache` (no reader once no map material is
built from loose images; the eye pass keeps its own); and the Cog "Material look" tab's hardcoded
"Asphalt 0.56 / Six streets 0.60 / Seven surfaces 1.00" wetness table (`MaterialResponse`), which
restated three numbers that live on the `MI_`s' `WetnessScale`.

#### Texture slots, roles and the `_linear` twin

Two slot rules, because the same VMT key means different things per family:

- On `M_V2_Lit`/`M_V2_LitTranslucent` and `M_V2_TwoTexture`, `$bumpmap` **is** the tangent normal
  map and binds `NormalMap` (294 units). `M_V2_Unlit` is **not** in this rule (0 users).
- On `M_V2_Water` and `M_V2_Refract`, `$bumpmap`'s own registration comment is *"dudv bump map"*,
  so it binds `DuDvMap`, and `$normalmap` binds `NormalMap` (`docs/vtmb/shader_combos.md` → Water).

Every texture parameter is bound to the slice-2 asset for its unit, choosing the **`_linear`
twin** when this material's binding is a data read and the texture also has a colour binding
elsewhere — the twin exists exactly for that case (`seam_map_texture.md` → "Role, sRGB and
compression"). The classification is complete, one row per slot:

| Slot | Class | `_linear` twin | Default when unbound |
|---|---|---|---|
| `BaseTexture`, `BaseTexture2` | colour | no | `/Engine/EngineResources/DefaultTexture` |
| `Iris` | colour | no | `/Engine/EngineResources/DefaultTexture` |
| `Glint` | colour — an additive emissive image | **no** | `/Engine/EngineResources/Black` |
| `EnvMap` | colour (cube) | no | `/Engine/EngineResources/DefaultTextureCube` |
| `NormalMap` | data | **yes** | `/Engine/EngineMaterials/DefaultNormal` |
| `DuDvMap` | data | **yes** | `/Engine/EngineMaterials/DefaultNormal` |
| `EnvMapMask` | data | **yes** | `T_LinearWhiteMask` — 1×1 linear white, **never** `DefaultTexture` |
| `CloudAlphaTexture` | data | **yes** | `T_LinearWhiteMask` |
| `BaseTextureFrames` | colour (array) | no | `T_V2_DefaultFrames` |
| `NormalMapFrames` | data (array) | **yes** | `T_V2_DefaultFrames` |
| `SurfaceClassLUT` | neither — generated, not a VtMB texture | **n/a** | `T_SurfaceClassLUT` (`SRGB` off by construction) |

### Post-lighting math per master

What each master transcribes, and what it does not. In every VtMB program `v0` is the interpolated
vertex lighting (models) or the vertex colour (world) and `t1`/`t2` is the lightmap; **the lighting
ones are Lumen's** and are never transcribed.

**The constant registers are per family, not global.** There is no single `c0` meaning; each
family's shipped programs assign their own, and the stage reads the legend for the family it is
staging:

| Family | `c0` | `c1` | `c2` | `c3` |
|---|---|---|---|---|
| `lightmappedgeneric*`, `vertexlitgeneric*` (pass 0) | `overbrightFactor/2` → the `Overbright` knob | `$selfillumtint` | `$envmaptint` | `$color` × `$alpha` (the per-draw modulation; `c3.a` is the alpha) |
| `vertexlitgeneric_envmappedbumpmapv2*` (pass 1) | **the constant reflection colour** — `mul r0.rgb, t3, c0` and `mov r0.a, c0.a`; *not* the overbright factor | — | — | — |
| `cable` | the hemispheric ambient bias added to the `dp3` | — | — | — |
| `waterrefract*` | — | **the refract tint** (`mul r0, t2, c1`) → `RefractTint` | — | — |
| `waterreflect*` (design-era `waterreflect.psh` source only) | — | — | — | **`c3.a` is the Fresnel R0** (`mad r0.a, r0.a, 1-c3.a, c3.a`) → the `Fresnel` node's `BaseReflectFract` — but the *shipped* `waterreflect_old`/`waterreflect_ps20_old` binaries read no `c3` register at all, so R0 = 0 (`BaseReflectFract` default `0.0`) in the shipped game; this row describes the unshipped source's intent, not what ships |
| `eyes*` | — | — | — | — (no constants; everything is a texture stage) |

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

`vertexlitgeneric_maskedenvmapv2.psh` also issues **`texkill t3`** before its arithmetic. `texkill`
is Direct3D's user clip plane — it discards a pixel whose interpolated `t3` has a negative
component — and it is **not** an alpha test. It is dropped, recorded in `Omissions`, and it is
*not* a source of `BLEND_Masked`; masked comes only from `$alphatest`/`$alphatested`.

**Self-illum has two spellings and the master implements the plain one *(named divergence)*.**
The plain form, `lightmappedgeneric_selfilluminated.psh` and
`vertexlitgeneric_selfilluminated.psh`, *replaces* the lit base where the mask is 1:

```text
mul r0.rgb, t0, v0
mov r0.a, v0.a              ; co-issued -- opacity comes from the VERTEX alpha, not t0.a
mul r0.rgb, t1, r0
mul_x2 r0.rgb, c0, r0
mul r1, c1, t0              ; self illum * tint
lrp r0.rgb, t0.a, r1, r0    ; blend between self-illum and base * lightmap
```

so as `t0.a → 1` the lit base disappears. Transcribed:

```text
BaseColor *= (1 - BaseTexture.a)                                        ; under UseSelfIllum
Emissive   = BaseTexture.rgb * SelfIllumTint * BaseTexture.a * SelfIllumAmount
```

The masked V2 spelling, `lightmappedgeneric_selfilluminatedmaskedenvmapv2.psh`, is **different**:

```text
mul  r1, c1, t0.a           ; tint * mask
mad  r1, t0, r1, t1         ; base*tint*mask + lightmap
mul  r0.rgb, r1, r0         ; times (base * v0)  <- base appears twice: squared, multiplicative
mul_x2 r0.rgb, c0, r0
```

There the self-illum term is *multiplied into* the lit result and the base texture is effectively
squared, rather than replacing it. The two do not agree once the lightmap leaves. **The master
implements the plain form on both**, and the divergence is named here with both instruction lists
rather than hidden behind "the two agree". 352 + 152 + 3 materials take the V2 spelling.

Note also the plain program's opacity: `mov r0.a, v0.a` — the alpha is the **vertex** alpha, not
`t0.a`, which is why the self-illum mask is removed from the opacity path (`OpacityMask` is forced
to `1` under `UseSelfIllum`) instead of doubling as a cutout.

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
normal at the reflected eye vector, times a constant colour, optionally times the normal map's
alpha". Under Lumen that is *exactly* what a normal-mapped reflective surface already does, so pass
1 is **replaced, not transcribed**: it collapses into `UseNormalMap` on the pass-0 instance, and
`_MultByAlpha` sets `UseNormalMapAlphaEnvMapMask` so the normal map's alpha feeds the mask term.
No second material, no second draw. 98 materials.

**`M_V2_Unlit`** — `unlitgeneric.psh` is `tex t0 / mul r0, t0, v0`: Base Color goes straight to
Emissive, `v0` is the vertex colour under `UseVertexColor`. `unlitgeneric_envmapmask.psh` shows the
unlit envmap add explicitly, and its co-issued last line is the master's **opacity** source:

```text
mul r0.rgb, t1, t2
mul r0.rgb, c2, r0
mad r0.rgb, t0, v0, r0
mul r0.a, t0, v0            ; co-issued -- Unlit opacity is BaseTexture.a * VertexColor.a
```

An unlit surface has no lighting for Lumen to replace, so on this master the cube term **is**
transcribed as the literal additive Emissive of the reflection contract whenever a cube exists,
including `FixedCubeStrength` — the same `cube x mask x EnvMapTint x FixedCubeStrength` product
`M_V2_Lit`'s own fixed-cube branch uses, gated `UseEnvMap` here (this master has no separate
`UseFixedCube` path to switch onto).

**`M_V2_Eyes`** — `eyes.psh`:

```text
tex t0
tex t1
tex t2
lrp r0, t1.a, t1, t0      ; blend in the iris with the background
mad r0.rgb, r0, v0, t2 +  ; modulate by the illumination, add in the glint
mov r0.a, t0.a
```

The `lrp` is transcribed (`BaseTexture` ← eyeball, `Iris` ← `$iris`, alpha of the iris is the
blend); `v0` is lighting and is dropped; `t2` (`$glint`, bound at stage 2) is additive Emissive.
`$glint` is set by **no** shipped material, so `UseGlint` is off on all 406 and the slot exists for
completeness. `eyes_overbright2.psh` differs only in that it doubles the lighting and then adds the
glint as a **separate instruction** rather than folding it into a `mad` —
`mul_x2 r0, v0, r0` then `add r0.rgb, r0, t2`, where `eyes.psh` writes `mad r0.rgb, r0, v0, t2`.
Both reduce to "iris over eyeball, times lighting, plus glint" once the lighting term leaves, so
the two rows collapse to one instance.

**`VampireEyes` is wired.** The `Eyes_Vampire`/`Eyes_Vampire_Overbright2` programs (12 materials,
`$vampire 1`) ship compiled-only, but the compiled `psh/eyes_vampire` disassembles cleanly
(cited by name at `docs/vtmb/facial_animation.md:503`):

```text
mul r0, t0, v0            ; BaseColor(lit) = BaseTexture(sclera, t0) x vertex lighting v0
lrp r0, t1.w, t1, r0      ; result = Lerp(A=r0 (lit sclera), B=Iris(t1), Alpha=Iris.a)
add r0.xyz, r0, t2        ; + Glint (t2), additive
mov r0.w, t0.w            ; Opacity(Mask) = BaseTexture.a
```

Read against the non-vampire `eyes.psh` above, the only real difference is *where* lighting
applies: the non-vampire program lights the whole lerped result (Lumen lights whatever lands in
`MP_BASE_COLOR`, uniformly); the vampire program lights the sclera *before* the lerp, so the iris
never receives `v0` at all — a self-illuminated iris over a normally lit sclera. There is no
post-lighting `mul` a node graph can reproduce, so the equivalent split routes the iris term to
Emissive (self-illuminated) and darkens BaseColor by the iris coverage it lost:
`BaseColor = (1 - Iris.a) x BaseTexture`, `Emissive += Iris.rgb x Iris.a`. Alpha comes from the
sclera in both variants (`mov r0.w, t0.w` either way).

**`M_V2_Water`** — the shipped pair is compiled-only, but `waterrefract.psh` and `waterreflect.psh`
carry the shape. Refract is `texm3x2pad`/`texm3x2tex` through a DUDV map into a screen render
target times `c1` (`RefractTint`); reflect adds the quintic Fresnel:

```text
dp3_sat r1.rgba, v0_bx2, t3_bx2
mul r0.a, 1-r1.a, 1-r1.a   ; squared
mul r0.a, r0.a, r0.a       ; quartic
mul r0.a, r0.a, 1-r1.a     ; quintic
mad r0.a, r0.a, 1-c3.a, c3.a
mul r0, r0.a, t2
```

The last `mad` is Schlick with R0 = `c3.a`, which is exactly Unreal's `Fresnel` node with
`BaseReflectFract` = `BaseReflectFract` and `ExponentIn` = 5 — but see the register-legend note
above: the shipped binary reads no `c3` at all, so R0 = 0 (`BaseReflectFract` default `0.0`)
ships, and this transcription is the unshipped source's stated intent, kept live as a knob. Both
render-target passes are **replaced**: `_rt_WaterRefraction` and `_rt_WaterReflection` become
Unreal `Refraction` and Lumen reflection on one translucent surface, with the authored constants
(`$waterdepth`, `$watermurkiness`, `$waterwaveheight`, …) carried as scalars driving the same
shapes. The Fresnel is the one place VtMB *has* one, so it is transcribed rather than flattened.
`$forcecheap` sets `CheapWater`, which drops the refraction pass — both the `DuDvMap` perturbation
feeding `MP_NORMAL` and `MP_REFRACTION` itself, which reverts to the flat neutral `1.0`.
`MP_REFRACTION = 1 + RefractAmount/100` otherwise (`M_V2_Refract`'s own convention).

The shipped cheap program's own tail (no readable source at all, but `watercheap_ps11`/
`watercheap_ps20_old` both end identically) is also transcribed:

```text
mad r0.xyz, F, reflect, c0(g_FogColor)   ; Emissive += FogColor.rgb * a distance term F
mov r0.w, c0.w                            ; Opacity -> FogColor.a
```

`F` is `saturate((PixelDepth - FogStart) / (FogEnd - FogStart))`; both effects are gated
`UseFogEnable`.

Water hosts two proxies and the master hosts their parameters: `animatedtexture` on **20** of the
24 units (19 animating `$bumpmap` → `DuDvMap`, 1 animating `$normalmap` → `NormalMap`, all with
`animatedtextureframenumvar = $bumpframe`) drives `NormalFrameRate`/`UseAnimatedNormalFrames`, and
`texturescroll` on **18** (13 targeting `$bumpoffset`, 5 `$bumptransform`) drives
`BumpScrollRateU/V`. When two `texturescroll` proxies land on the same lane — 6 materials do —
their precomputed rates **sum**, which is exact: two translations of the same UV are one panner at
the sum.

**`M_V2_Sprite`** — the `SpriteRender*` programs are compiled-only. The family's whole variation is
blend state chosen by `$spriterendermode` (`shader_combos.md` → Sprite), so it becomes rows in the
per-instance blend table rather than a parameter:

The rows are the Sprite shader's own per-mode blend state (`stdshader_dx8.dll` `1000eca0`), not
the mode's Source *name* — the two differ, and the name is what the pre-fix table followed:

| `$spriterendermode` | Source name | Program | Source blend state | `BlendMode` | Units |
|---|---|---|---|---|---|
| 0 (or the key absent) | `kRenderNormal` | `SpriteRenderNormal` | opaque | `BLEND_Opaque` | 60 |
| 1, 2, 4 | `kRenderTransColor`/`TransTexture`/`TransAlpha` | `SpriteRenderTransColor` | `SRC_ALPHA, INV_SRC_ALPHA` | `BLEND_Translucent` | 0 |
| 3, 9 | `kRenderGlow`/`kRenderWorldGlow` | `SpriteRenderTransColor` | `SRC_ALPHA, ONE`, **depth test off** | `BLEND_Additive` | 0 |
| 5, 7 | `kRenderTransAdd` | `SpriteRenderTransAdd` | `SRC_ALPHA, ONE` | `BLEND_Additive` | 0 |
| 8 | `kRenderTransAlphaAdd` | `SpriteRenderTransColor` | `ONE, INV_SRC_ALPHA` (premultiplied) | `BLEND_AlphaComposite` | **2** |
| 6 | `kRenderEnvironmental` | none — the shader warns *"Unknown sprite rendermode"* | — | **stage failure** | 0 |

A glow (3, 9) is **additive**, which is the whole point of a corona: the pre-fix table read the
mode's name and sent both to `BLEND_Translucent`, which drew every corona as a grey card *over*
the light instead of adding to it. Mode 8 is premultiplied, which is Unreal's `AlphaComposite`,
not `Additive`. No premultiply term is wired for the additive rows: Unreal's additive base pass
already multiplies the colour by Opacity (`BasePassPixelShader.usf:2326`), which is exactly
`SRC_ALPHA, ONE`.

Only 2 of the 62 declare the key at all, both mode 8. Base Color → Emissive, `Color` ×
`VertexColor` × `ParticleColor`, blend from the row. Both colour terms, because this one master
draws through two vertex factories that each compile the *other* in as white: a Niagara sprite
(`NiagaraSpriteVertexFactory.ush`) hardcodes `VertexColor = 1` and fills `Particle.Color`, while
the `env_sprite` billboard has no particle data and reads `Particle.Color = (1,1,1,1)` — and VtMB
writes an `env_sprite`'s `rendercolor`/`renderamt` as per-corner *vertex* colour
(`CMeshBuilder::Color4ubv`, `1008232b`), with no material colour modulation anywhere in the Sprite
program. `UseVertexColor`/`UseVertexAlpha` gate the product.

**`M_V2_Refract`** — no shipped source and no transcribed selector; the master is Unreal
`Refraction` from `$dudvmap`/`$normalmap` scaled by `RefractAmount`, tinted by `RefractTint`.
Stated as a reconstruction, not a transcription. One real precedent is folded in, though:
`fxc/refract_ps20`'s own perturbation scale, `scale = normalMap.a x RefractAmount`, so the
`DuDvMap` ripple is not an unconditional additive delta but scaled by the bound `NormalMap`'s own
alpha channel times the knob.

**`M_V2_Decal`** — no program at all (the 38 materials fell back to `wireframe` in retail).
`BLEND_Modulate` of `BaseTexture` over the receiver. Three consequences of that blend mode, stated
because they are not optional:

- **the master must be Unlit**, and it is — Unreal only permits `BLEND_Modulate` on an unlit
  shading model;
- **modulated surfaces are excluded from the Lumen surface cache**, so these decals contribute
  nothing to global illumination and cannot be seen in a Lumen reflection;
- **`BLEND_Modulate` is not Nanite-compatible**, so `M_V2_Decal` does *not* set
  `used_with_nanite`, and a decal surface the map lane wants Nanite on is a placement error rather
  than a material one.

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

**The lerp direction is `1 - lightmapAlpha`**, and it selects `$basetexture2` at alpha 1: with
`lrp d, t, a, b` computing `t·a + (1-t)·b`, `t = 1 - t2.a` means `basetexture2` wins where the
lightmap alpha is 0 and `basetexture` wins where it is 1. Unreal has no lightmap alpha, so the
blend is sourced from the mesh's vertex-colour alpha (`UseVertexAlpha`) and authored as
`LinearInterpolate(A = BaseTexture, B = BaseTexture2, Alpha = 1 - VertexColor.a)` — the same
polarity. `mul r0, r0, t2` and `mul_x2 c0` are Lumen's and the `Overbright` knob. `texkill t3` is
the clip plane and is dropped, exactly as on `vertexlitgeneric_maskedenvmapv2`.

### Reflection contract

From the Settled entries of 2026-08-31 — *probes are not reflection content*, *the matte-world
premise is repudiated*, *calibration happens on knobs*. Everything below is a knob or a switch;
no numbers are chosen here.

**`$envmap` presence means the surface is reflective** — nothing more. 2,610 of 11,624 units carry
it. It does not mean "add this image"; under Lumen the renderer supplies the image and VtMB's data
says only *how shiny* and *where*.

| `$envmap` value | Units | What the instance does |
|---|---|---|
| `env_cubemap` (the symbol) | 2,217 | no texture asset at all, and **no runtime bind either**. `env_cubemap` is not a value a running game supplies; SF-6.2's `SphereReflectionCapture` actors at the map's own `cubemaps[]` origins supply the *image*, through Lumen, with nothing bound to a parameter. The symbol is recorded in provenance (`EnvMapSymbol`) and consumed by nobody. `UseEnvMap` on, `UseFixedCube` off |
| `envmap/<name>` (authored fixed cube) | 342 | `UseFixedCube` on; `EnvMap` binds `/ElysiumBaked/Textures/envmap/TC_<name>`. An art choice of image, not a room |
| `shadertest/*`, `dev/*` | 49 | same as authored, but these units are all in the debug/tool set or shader-test content |
| `lib/redenv_skyref` | 2 | authored, treated as `envmap/` |
| `maps/<map>/…` (patched, SF-1.4) | 7,450 rows | provenance only. The concrete `TC_maps/<map>/c…` asset id is **recorded on the instance** (`EnvMapAssetId`, plus a soft object path) and **never bound to a texture parameter** — that holds for the 4,948 positioned probes and the 2,502 `cubemapdefault` copies alike. Probes are renders of the 2004 lightmapped world; sampling them would put 2004 lighting into Lumen reflections. `UseFixedCube` off, and the patched instance inherits its base's reflection state unchanged |

**Masks decide roughness, specular and metallic**, per texel, and never opacity:

| Source | Units | Mask term |
|---|---|---|
| `$envmapmask` texture | 2,368 | the `_linear` twin of that texture, luma |
| `$normalmapalphaenvmapmask` | 136 | `NormalMap.a` — this is the pass-1 `_MultByAlpha` term |
| `$basealphaenvmapmask` | 14 | `1 - BaseTexture.a` — **inverted**, as `lightmappedgeneric_basealphamaskedenvmapv2.psh` writes it (`mul r1, t2, 1-t3.a`) |
| none of the three | 91 | constant 1 (uniform reflectivity) |

Those four rows are exactly disjoint and sum to the 2,610 `$envmap` units, because **the counts
restrict to `$envmap` units**. The parameter table's larger numbers count every author of the key:
2,392 units author `$envmapmask` but only 2,368 also carry `$envmap` (the other 24 declare a mask
for a reflection they never asked for — recorded as an anomaly, no mask emitted), and 146 author
`$normalmapalphaenvmapmask` of which 136 carry `$envmap`. The one `$normalalphaenvmapmask`
misspelling is folded in as the correct key, which is why this table says 91 rather than 92.

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
implicit white. The split is `max(rgb) - min(rgb) <= ChromaThreshold` after normalising the two
authored forms (`[r g b]` 0–1, `{r g b}` 0–255): at the shipped `ChromaThreshold` of **0.02**,
**362 grey, 104 chromatic**, with a clear gap between the populations. `ChromaThreshold` is a
settings knob like every other number here — the stage **reads it out of the ini**, and 0.02 never
appears as a literal in the stage, the masters or this design's implementation.

```text
grey       -> Specular *= luma(EnvMapTint) * EnvTintScale             ; MetallicTint off
chromatic  -> Metallic  = mask * MaskMetallicMax                       ; MetallicTint on
              BaseColor = lerp(BaseColor, BaseColor * EnvMapTint, ChromaticTintStrength)
```

`MaskMetallicMax` and `ChromaticTintStrength` are settings knobs: the chromatic branch is a
*hypothesis* about VtMB's intent, so its strength has to be tunable to zero without a re-import.
The 104 chromatic tints are VtMB naming its own metals — `[0.65 0.5 0.0]` and `[1.0 0.7 0.0]`
brass and gold, `[0.74 0.57 0.31]` and `[0.52 0.36 0.25]` copper — plus a blue/teal set
(`[.5 .6 .9]`, `[0.3 0.6 1.0]`, `[.4 .8 .8]`) that is tinted glass rather than metal. This is a
hand-authored metal mask and it is read, never inferred. `EnvTintScale` is a settings knob.

**The authored fixed cube is the one literal sample, and it beats the chromatic branch.** For the
342 (+49 debug) `envmap/<name>` units, and only for them, the master adds

```text
Emissive += TextureSampleParameterCube(EnvMap, reflect(V, N))
          * mask * EnvMapTint * FixedCubeStrength
```

and on those units `MetallicTint` is **off** even when `$envmaptint` is chromatic. The precedence
is one line: *an authored `envmap/*` unit takes the cube add and not the chromatic branch* —
otherwise the tint would be applied twice, once as a multiplier on a literal image and once as a
metal hypothesis on the base colour. The stage records `envMapTintChromatic` in provenance either
way, so the classification survives the precedence.

The add sits behind a **`Ray Tracing Quality Switch`** whose `RayTraced` input is black, so a
camera-dependent term cannot enter ray-traced reflections or Lumen's card lighting. **Caveat, and
it is a real one:** that node's `RayTraced` pin covers ray-traced and Lumen hardware-tracing
passes, *not* the path tracer, which is gated by the separate `!PATH_TRACING` condition. The path
tracer therefore **does** see the fixed-cube add and will render these 342 surfaces brighter than
the raster path. That is accepted — the path tracer is not this project's output — and it is stated
here so nobody debugs it twice.

The Emissive placement is `docs/vtmb/reflections.md`'s translation carried forward — it preserves
the visible additive term on a dark street, at the cost of not being VtMB's pre-lighting composite
point *(provisional owner call)*. `FixedCubeStrength` is a settings knob; SF-5.3 tunes it. The
cube's face order and handedness are the slice-2 `TC_` asset's, already proven headlessly.

**The `cube × mask × EnvMapTint × FixedCubeStrength` add is uniform across every master with a
fixed-cube lane** (`M_V2_Lit`, `M_V2_Water`, `M_V2_Unlit`, `M_V2_Refract`), gated `UseFixedCube` on
each (Water/Refract; Lit's own `UseFixedCube` branch) or `UseEnvMap` (Unlit, which has no separate
`UseFixedCube` path to switch onto). `mask` is `mask_sat` (the reflection-mask term above) where a
master computes one at all — Lit and Unlit — and dropped from the product on Water and Refract,
neither of which exposes a separate reflection-mask texture slot.

**Fresnel default: `BaseReflectFract = 0.0`, `Exponent = 5`.** These are the *shipped binary's*
values, not the design-era readable source's: `waterreflect_old`/`waterreflect_ps20_old` (and
their `_ps11`/`_ps20`-suffixed siblings) read no `c3` register at all in the compiled programs, so
R0 = 0 in the shipped game regardless of what `c3.a` the unshipped `waterreflect.psh` source reads.
The register legend elsewhere in this document that cites `c3.a` as the Fresnel R0 input describes
that unshipped source only — the shipped exponent is 5 (ps.1.1); the owner may raise
`BaseReflectFract` on the knob or a per-instance override once a reference render calls for it.

**The sphere variant needs nothing.** `$envmapsphere` is set on 11 units and `$envmapcameraspace`
on 1. Of those 12, **10 do select a `*_EnvMapSphere*` vertex program** (`UnlitGeneric_EnvMapSphere`,
`LightmappedGeneric_EnvMapSphere`, `LightmappedGeneric_EnvMapSphereVertexColor`) and 1 selects
`UnlitGeneric_EnvMapCameraSpace`, so the flag is not inert in the selector — it is inert in the
*content*: ten of the twelve are `shadertest/` or `dev/` units, and the two shipped-content users
are `glass/breaksurf/break_glass_1` (which has no `$envmap` at all, and therefore resolves plain
`LightmappedGeneric`) and `break_glass_2`. So the sphere-map projection has **two** shipped-content
users, one of which is inert. Those two take the ordinary reflection-vector path and the flag is
recorded in provenance; no switch, no master, no variant *(provisional owner call)*. `$envmapmode`
(127 units: 84 `1`, 43 `0`) is read by **no** transcribed selector and never co-occurs with
`$envmapsphere` — it is a Source key this renderer ignores, and it is provenance-only.

**Non-`$envmap` surfaces still reflect.** The other 9,014 units are not matte. They take
`DefaultSpecular`, `DefaultRoughness` and `DefaultMetallic` from the settings, modulated by their
class LUT row (`ClassRoughness`/`ClassSpecular`/`ClassMetallic` at `SurfaceClassIndex`). That is
the whole of the repudiated three-zeroes rule's replacement, and its values are the owner's to tune
in SF-5.1/5.2.

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
`$masktexture` (2) is **not** a `cloud` key — both authors are `screenfeedback` units, which is a
no-master family, so it is bound as a texture on a provenance-only instance and nothing samples it.
The `cloud` pair is `$cloudalphatexture` (2) → `CloudAlphaTexture` and `$maskscale` (2, a
**vector** `[1 1 1]`) → provenance, since `cloud`'s mask is `$cloudalphatexture` and `$maskscale`
scales a mask the V2 `M_V2_Unlit` graph does not have.
`$detail` (28), `$detail2` (1) and `$glassenvmap` (1) bind nothing: see "`Detail` is dropped".
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
`$cloudscale` is listed under **vectors**, not here: all 2 authors write `[ 2.00 2.00 2.00 ]`.
Eleven of these scalars do not reach a master, each for a stated reason: `$detailscale` 8 and
`$detailscale2` 1 (`Detail` is dropped), `$minlight` 11 and `$maxlight` 11 (no Lumen formula —
a named divergence), `$subdivsize` 14 (water tessellation, the map lane's), `$spriterendermode` 2
(it selects blend state, a per-instance override, not a parameter), `$curve` 3 (a `subtract`
operand inside a `playerproximity` chain — a runtime-factory value), `$j_basescale` 4 (an `add`
operand), `$halfwidth` 1 and `$mean` 1 (`gaussiannoise` operands authored at top level), and
`$wetbrightnessfactor` 1. The TwoTexture pair `$texscale` 4 and `$tex2scale` 4 **are** scalars —
`.25` and `0` — and fold into `TexScaleOffset.xy` and `Texture2ScaleOffset.xy`, while their
`$texoffset` 4 / `$tex2offset` 4 partners are **vectors** and fold into the `.zw` halves;
`$texture2scale` 1 (`10.0`) is a second spelling of `$tex2scale`. `$alpha_bias` 4 → `AlphaBias`.

**Vector parameter (17).** `$envmaptint` 466, `$color` 349, `$refracttint` 22, `$fogcolor` 22,
`$reflecttint` 19, `$selfillumtint` 15, `$bumpoffset` 23, `$scale` 25, `$watercolor` 5,
`$texoffset` 4, `$tex2offset` 4, `$maskscale` 2, `$cloudscale` 2, `$clampcolor` 1, `$maxcolor` 1,
`$leakcolor` 1, `$glassenvmaptint` 1.
`$spriteorigin` 54 has **moved out of this group**: like `$spriteorientation` it is a billboard
placement rule belonging to the sprite *component*, not a material value, so it is provenance and
the sprite placement lane owns it. `$bumpoffset` (23: water 13, `vertexlitgeneric` 6,
`lightmappedgeneric` 4) and `$scale` (25) fold into `TexScaleOffset` — `.zw` and `.xy` — and
`$bumpoffset` is also the commonest `texturescroll` target.

**Proxy-named parameters (4).** Four names appear only as a proxy's `resultvar` or
`texturescrollvar`; **no unit authors them as a `$`-key**, so the stage creates them, exactly as it
creates `$frame`. They are in the table because a proxy naming a parameter with no destination
would be a silent drop:

| Name | Named by | Rows | Destination |
|---|---|---|---|
| `$bumptransform` | `texturescroll` (water 5, Lit 6, other 1) | 12 | `BumpScrollRateU/V` |
| `$basetextureoffset` | `texturescroll` (Unlit) | 3 | `BaseScrollRateU/V` |
| `$texture2transform` | `texturetransform` (TwoTexture) | 4 | `Texture2ScaleOffset` |
| `$texture2offset` | `linearramp` 1, `multiply` 1 (component targets `[0]`, `[1]`) | 2 | `Texture2ScaleOffset.z`, `.w` |

`$compare` is **not** one of them: it appears nowhere in the corpus, as a key or as a proxy
variable, and it is not in the table.

**Static switch (13).** `$selfillum` 1240 *(also a scalar: the value is `1`, `0` or `0.2`, so it is
both the switch and `SelfIllumAmount`; 946 units author it non-zero)*, `$vertexcolor` 1475,
`$vertexalpha` 731, `$decal` 559 *(550 non-zero — not a master switch: it becomes the
provenance flag `isDecalSurface`, which since R7.2 is what makes the unit stage a projector twin
(`MI_<unit>_Decal`) and makes its face groups bind that twin; 483 `lightmappedgeneric`, 34
`unlitgeneric`, 12 `vertexlitgeneric`, 21 no-master)*,
`$normalmapalphaenvmapmask` 146, `$basealphaenvmapmask` 14, `$vampire` 12,
`$envmapsphere` 11 *(recorded only, see the reflection contract)*, `$envmapcameraspace` 1 (same),
`$forcecheap` 2, `$bumpbasetexture2withbumpmap` 4 → **`UseBumpOnBaseTexture2`** on
`M_V2_TwoTexture`, `$fogenable` 23, `$normalalphaenvmapmask` 1
*(a one-off misspelling of `$normalmapalphaenvmapmask`; recorded as an anomaly and treated as the
correct key)*.

**Master choice / instance property (10).** `$translucent` 2761, `$additive` 614, `$alphatest` 228,
`$nocull` 591, `$alphatested` 3 *(a misspelling of `$alphatest`, same treatment)*,
`$transparent` 3, `$translucency` 160 *(authored `0` on all 160; recorded, no effect)*,
`$ignorez` 1497, `$model` 198 *(world-vs-model draw, already folded into the resolved program)*,
`$flat` 9.
`$ignorez` sets no *material* property, because `bDisableDepthTest` is material-only: of the
1,491 units that author it non-zero, 1,473 are `hud/`, `fonts/`, `interface/` and `vgui/` UI
materials that SF-6.5 draws through UMG, where depth is not tested at all, and the **18** world
uses are resolved by master choice rather than by a property — see "Master inventory"
*(provisional owner call)*.

**Master choice, patched units only (1).** `include` — the 230th key, authored by every one of the
7,499 **patched** map units, by neither of the 2 standalone `maps/sm_tattoo/…` units, and by none
of the 11,624 install units. It names the base material, which
is the patched instance's parent; it never becomes a material parameter.

**Physical material and class index (1).** `$surfaceprop` 4605, plus the derived top-directory and
family-default fallbacks described under "Identity and naming".

**Runtime bind (4).** Values a running game supplies, wired by `FElysiumMaterialFactory` in
SF-6.4: `$reflecttexture` (19), `$refracttexture` (17), `$clientshader` 2 (`mouthshader` — the
facial path owns it), and every `resultvar` target of a runtime proxy (see the proxy table).
**`env_cubemap` is not in this group.** Nothing binds it at runtime: the image comes from Lumen and
SF-6.2's capture actors, with no parameter written, so the 2,217 symbols are provenance only.

**Proxy-owned (33).** Keys that only ever appear inside a proxy block and are read by the proxy,
never by the shader: `resultvar` 116, `sinemin` 87, `sinemax` 87, `sineperiod` 87,
`animatedtexturevar` 71, `animatedtextureframenumvar` 71, `animatedtextureframerate` 71,
`texturescrollvar` 48, `texturescrollrate` 48, `texturescrollangle` 48, `scale` 32, `timeoffset` 25,
`srcvar1` 19, `srcvar2` 19, `translatevar` 10, `alpha` 8, `halfwidth` 5, `rate` 5, `mean` 4,
`greatervar` 4, `lessequalvar` 4, `rotatevar` 2, `maxval` 2, `minval` 2, `animationnowrap` 1,
plus the six that belong to a single proxy instance: `camoboundingboxmax`, `camoboundingboxmin`,
`camopatterntexture`, `surfaceprop` (inside `camo`), `direction` (inside `particlesphereproxy`),
`dummy` (inside `waterlod`), and `halfwidthvar`/`meanvar` (1 each, inside a malformed
`gaussiannoise` block).

**Proxy scratch registers (26).** Declared at top level so the proxy chain has somewhere to write,
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

**Eight** keys appear in two groups because they genuinely have two destinations:
`$reflecttexture` and `$refracttexture` are texture bindings *and* runtime binds; `$envmap`,
`%tooltexture`, `$bottommaterial` and `$crackmaterial` are bindings *and* provenance; `$selfillum`
is a scalar *and* a static switch; `resultvar` is proxy-owned *and*, for a runtime proxy, a runtime
bind. Every other key of the 229 is listed exactly once. Each group's parenthesised number counts
the key names printed in that group, so the groups list eight names more than the 229 distinct
keys.

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

**`resultvar` values are matched case-insensitively.** VMT keys are case-insensitive and the corpus
proves it: the 116 `sine` `resultvar` rows spell their targets 18 distinct ways raw, **15**
case-folded (`$selfIllumTint`, `$ALPHA`, `$COLOR` and `$Alpha` fold onto four of the others) and 9
once the `[i]` component suffix is also collapsed. **The normalisation this design uses everywhere
is case-folded with the index preserved — 15 —** because the index is meaning (`$color[1]` writes
one channel) and the case is not. The stage lower-cases the value, splits a trailing `[i]`, and
resolves the remainder against the parameter table.

| Proxy | Mats | Inst | Destination | Keys it reads | Notes |
|---|---|---|---|---|---|
| `sine` | 87 | 114 | shader-time: the sine lane | `sinemin`, `sinemax`, `sineperiod`, `timeoffset`, `resultvar` | **15** distinct case-folded `resultvar` targets over 116 rows: `$alpha` 35, `$color[*]` 31, `$selfillumtint[*]` 25, `$envmaptint[*]` 10, `$temp*` 7, `$detailscale` 2, `$tempvec[1]` 1. A `[i]` component target writes one channel, through `SineChannelMask`; `$temp*`/`$tempvec` are chain links, not outputs; `$detailscale` is provenance-only, so those rows emit nothing and record `proxyTargetProvenanceOnly` — **except a `$temp*` a `texturetransform` reads back** (R7.1 ruling J, the section above at → "The `sine` → `texturetransform` chain"): that pair resolves to `SineUVTranslate` and the sine row keeps `destination: "graph"`. Measured 2026-09-04 over the staged corpus: 12 `proxyTargetProvenanceOnly` rows on 7 units, one fewer than before ruling J (`objects/surf`'s `$temp[0]`) |
| `animatedtexture` | 72 | 72 | shader-time: frame index | `animatedtexturevar`, `animatedtextureframenumvar`, `animatedtextureframerate`, `animationnowrap` | `animatedtexturevar` is `$basetexture` / `$bumpmap` / `$normalmap` and `animatedtextureframenumvar` is `$frame` or `$bumpframe` — `$frame` is never declared as a VMT key, so the stage creates the scalar. The slot binds the slice-2 `TA_` array asset and `FrameRate` drives the slice index; `animationnowrap` (1) clamps instead of wrapping |
| `texturescroll` | 48 | 55 | shader-time: `Panner`, on one of **two** independent lanes | `texturescrollvar`, `texturescrollrate`, `texturescrollangle` | the stage precomputes `rate·cos θ` and `rate·sin θ`, so the master needs no trig. The lane comes from `texturescrollvar`: `$basetexturetransform` 17 and `$basetextureoffset` 3 → **`BaseScrollRateU/V`**; `$bumpoffset` 23 and `$bumptransform` 12 → **`BumpScrollRateU/V`**. 12 materials scroll both lanes at different rates, which is why they are two parameters and not one. Two proxies on the same lane (6 materials) **sum** their rates — exact, since two translations of one UV are one panner |
| `texturetransform` | 12 | 16 | shader-time **when its inputs are** | `resultvar`, `translatevar`, `rotatevar` | writes `$basetexturetransform` (12) → `TexScaleOffset` or `$texture2transform` (4) → `Texture2ScaleOffset`, from another proxy's output; `translatevar` names `$texoffset` 4, `$tex2offset` 4, `$temp` 4, `$translate` 1, `$tempvec` 1 and `rotatevar` names `$temp` 2. Expressible only when `translatevar`/`rotatevar` resolve to a shader-time source; otherwise runtime. The `$temp` 4 `translatevar` rows are the ruling-J chain: joined to the `sine` that wrote the register they become `SineUVTranslate` (the section above at → "The `sine` → `texturetransform` chain"), not `TexScaleOffset` |
| `linearramp` | 5 | 5 | shader-time: `Time × rate` | `rate`, `resultvar` | targets `$tex2offset[1]` (4) and `$texture2offset[1]` (1) — both `Texture2ScaleOffset.w`, a scrolling second layer |
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
| `Master`, `BlendMode`, `TwoSided`, `SurfaceClass` (the class **name**), `SurfaceClassIndex` (the row's fixed `Index`), `SurfaceClassSource` (`surfaceprop`/`topdir`/`familyDefault`), `PhysMaterialFallback` | this lane's decisions |
| `IsDecalSurface`, `IgnoreZ`, `SpriteOrigin`, `SpriteOrientation`, `MinLight`, `MaxLight`, `WetnessScale`, `SubdivSize`, `Curve` | keys with a home outside the material — the placement, map and runtime-factory lanes read them here. `IsDecalSurface` is the one the *material* lane reads back: it decides whether the unit stages a projector twin (R7.2) |
| `decalAsset` (sidecar top level, not a provenance field) | the projector twin's asset path, written beside `assetPath` for both halves; the runtime does not read it — it folds the material id to the same path through `FElysiumContentPaths::BakedDecalMaterial` |
| `EnvMapSymbol`, `EnvMapAssetId`, `EnvMapAsset`, `EnvMapProbePath`, `bEnvMapTintChromatic`, `bPatchedProbe` | `environment` — `EnvMapAssetId`/`EnvMapProbePath` are a patched unit's un-bound probe id/path, `EnvMapAsset` is an authored-fixed-cube instance's own bound `EnvMap` asset path; neither pair is ever set together |
| `TextureBindings[]` (`Parameter`, `Value`, `Kind`, `Asset`, `Resolved`, `UsedLinearTwin`) | `textureBindings` plus the twin choice |
| `Dependencies[]` (`Role`, `Parameter`, `Asset`, `Resolved`) | `dependencies` |
| `SurfacePropertyAsset` | the surface-property dependency |
| `PatchOf`, `PatchKind` | `patch` (`vtmb:material:` of the base, and `replace`/`insert`) |
| `Anomalies[]` (`Kind`, `Extra`), `Omissions[]` (`Reason`, `Extra`), `Coverage` | as published — `Extra` is every field besides `Kind`/`Reason`, since a real sidecar's row shape varies by kind (`value`, `proxy`, `switch`, `key`, `role`, ...) |
| `Comments[]` (`Offset`, `Text`) | `comments` |

`ApplyJson` tolerates a missing key and refuses only a body that is not a JSON object, exactly as
the texture provenance does. Three fields are published as asset-registry tags in
`MetaDataTagsForAssetRegistry` beside `ElysiumRecipe`, so the Content Browser filters without
loading: **`ElysiumAssetId`**, **`ElysiumShaderProgram`** (the default-condition pixel program, or
the family name when unresolved) and **`ElysiumMaster`** (review finding 7: now the *root* master
for a patched instance too, walked through `parent` at stage time — see "Idempotency, pruning and
failure" below).

**Where to look for what (review finding 8).** `Alpha` and `Color` are ordinary bound instance
parameters — they show in the Material Instance Editor's own "Parameters" panel like any other
scalar/vector, the same place `BaseTexture` or `SelfIllumAmount` does. Every other field this
section describes (`SurfaceClass`, `EnvMapSymbol`, `PatchOf`, the anomaly/omission rows, …) is
`UElysiumMaterialProvenance` user data: not a material parameter at all, invisible in the
Parameters panel, read instead from the asset's Details panel (`UAssetUserData` array) in the
editor, from the three published registry tags above without loading the asset, or from
`bake_verify.py`/a Python script calling `unreal.ElysiumMaterialProvenance.find(instance)`.

### Knob contract

Every value that a human might want to change, and where it lives. Nothing that needs taste is a
Python or C++ literal (`seam_migration.md`, "Calibration happens on knobs inside the editor").

| Knob | Kind | Home | Read by |
|---|---|---|---|
| `DefaultSpecular`, `DefaultRoughness`, `DefaultMetallic` | settings scalar | `UElysiumSurfaceSettings` → `MPC_ElysiumSurfaces` | every master, for non-`$envmap` surfaces |
| `MaskRoughnessMin`, `MaskRoughnessMax`, `MaskSpecularScale` | settings scalar | same | the mask term of the reflection contract |
| `MaskMetallicMax` | settings scalar | same | the chromatic branch's `Metallic = mask * MaskMetallicMax` |
| `ChromaticTintStrength` | settings scalar | same | how far the chromatic branch tints Base Color; `0` disables the metal hypothesis without a re-import |
| `ChromaThreshold` | settings scalar, default **0.02** | same, and read **by the stage from the ini** | the grey/chromatic split of `$envmaptint`; never a literal in the stage |
| `EnvTintScale` | settings scalar | same | the grey-tint specular scale |
| `DetailSwayAmplitude` | settings scalar, default **12.7 cm** (Source's `cl_detail_max_sway` 5 units × 2.54) | same | the `UseDetailSway` World Position Offset term on `M_V2_Lit`/`M_V2_LitTranslucent`/`M_V2_Unlit` (R6.3, "Detail sway on the model masters") |
| `FixedCubeStrength` | settings scalar | same | the 342 authored-cube instances |
| `Overbright` | settings scalar, default **2.0** | same | `mul_x2 c0` in every lit program — the `_x2` and the `overbrightFactor/2` constant together |
| `MaxLaidDecals` | settings integer, default **2048** | `UElysiumSurfaceSettings` (not an `MPC_ElysiumSurfaces` row — no material samples it) | `UElysiumDecalSubsystem`'s pool: the cap on runtime-laid decals, oldest recycled first. A stated stand-in until VtMB's own `r_decals` default is read off `engine.dll` `staticinit_2007af40` |
| `LightSpecularScale` | settings scalar, default **1.0** | same | the light rig (R5.5, was SF-6.2): `UElysiumLightRig::ApplySettings` copies it into every non-overridden light's `SpecularScale`; one global, no per-map override; the bake stamps the same value (`seam_map_map.md` → "Import — reflection captures (R5.5)") |
| `CaptureRadius` | settings scalar, default **1,500 cm** | same | the reflection-capture placement (R5.5, was SF-6.2): every `ASphereReflectionCapture` the V2 bake spawns at a `cubemaps[]` origin takes it as `InfluenceRadius` |
| `ClassInfluence` | settings scalar | `UElysiumSurfaceSettings` → `MPC_ElysiumSurfaces` | the `Default*`-vs-class-table lerp weight every master applies before sampling `SurfaceClassLUT` |
| roughness / specular / metallic per surface class | class-table row (72 seeded, each with a fixed `Index`) | `UElysiumSurfaceCalibration` (`UDataAsset`) → a **128×1** lookup texture `T_SurfaceClassLUT`, created *inside* the data asset's own package (never a sibling asset) | every master, through `SurfaceClassLUT` at `SurfaceClassIndex` |
| `BaseTexture`, `BaseTexture2`, `NormalMap`, `DuDvMap`, `EnvMap`, `EnvMapMask`, `Iris`, `Glint`, `CloudAlphaTexture`, `*Frames` | per-instance texture | the VMT, via the stage | the master's texture slots |
| `Color`, `SelfIllumTint`, `EnvMapTint`, `RefractTint`, `ReflectTint`, `WaterColor`, `FogColor`, `CloudScale`, `TexScaleOffset`, `Texture2ScaleOffset`, `SineTargetMask`, `SineChannelMask` | per-instance vector | the VMT | as named |
| `Alpha`, `SelfIllumAmount`, `EnvMapMaskScale`, `BumpScale`, `AlphaBias`, `RefractAmount`, `ReflectAmount`, `BaseReflectFract`, `Water*`, `Fog*`, `FrameRate`/`FrameCount`, `NormalFrameRate`/`NormalFrameCount`, `BaseScrollRateU/V`, `BumpScrollRateU/V`, `Sine*`, `IrisFrame`, `SurfaceClassIndex` | per-instance scalar | the VMT | as named |
| `BlendMode`, `TwoSided`, `OpacityMaskClipValue` | per-instance base-property override | the VMT flags | the instance |
| every `Use*` / `MetallicTint` / `VampireEyes` / `CheapWater` switch | per-instance static switch | the VMT and the resolved program | the master |

**There is no per-material tuning layer** (owner answer, 2026-08-31). An edit to an imported `MI_`
is a throwaway experiment the next import overwrites; the settings page and the class table are the
whole authoring surface. The `UElysiumSurfaceSettings` object is the single writer of the surface
scalars in the collection — the Cog Environment window becomes a view onto it, not a second writer.

### Per-unit divergences

SF-4.3 part 3's cross-cutting ruling over the 13 stage failures the "No silent drop" rule surfaced
once all nine masters existed and every parameter table was final. Each is a unit whose own VMT
authors a key that resolves to a named parameter on *some* master, but not on the master *that
unit* takes — never a gap in the parameter table itself. `UNIT_DIVERGENCES`
(`pipeline/src/elysium_pipeline/importers/materials.py`) is the allowlist: `unit key -> {VMT key ->
reason}`, checked before classification, so an allowlisted key is recorded in provenance
(`unitDivergenceProvenanceOnly`) instead of failing the stage.

| Unit | Key(s) | Ruling |
|---|---|---|
| `models/character/npc/common/raver/males/male_raver_3/eyeball` | `$iris` | a `vertexlitgeneric` unit authoring an eyes-family key; `M_V2_Lit` has no `Iris` slot — provenance-only |
| `models/character/npc/unique/chinatown/ming-xiao/eyeball_r` | `$selfillum` | `eyes.psh` has no self-illum term and `M_V2_Eyes` exposes neither `SelfIllumAmount` nor `UseSelfIllum` — provenance-only |
| `models/character/npc/unique/santa_monica/ghost/eyeball_l` | `$selfillum` | same as above |
| `models/character/npc/unique/santa_monica/ghost/eyeball_r` | `$selfillum` | same as above |
| `stone/dincountertp` | `$envmapmask`, `$envmap` | `worldvertextransition`'s `InitShaderParams` deletes `$envmap` without `$bumpmap` (this unit has none), so `UseEnvMap` is forced off on `M_V2_TwoTexture`, which has no reflection lane at all — both provenance-only |
| `water/cheap_water` | `$forcecheap`, `$fogenable`, `$fogcolor`, `$fogstart`, `$fogend` | a non-water unit (family `lightmappedgeneric`, takes `M_V2_LitTranslucent`) authoring water-only keys despite not being a water surface; `M_V2_LitTranslucent` has no `CheapWater` switch or fog lane — provenance-only |
| `water/invisible_water` | `$fogenable`, `$fogcolor`, `$fogstart`, `$fogend` | a non-water unit (family `unlitgeneric`, takes `M_V2_Unlit`) authoring the same water-only fog keys; `M_V2_Unlit` has no fog lane — provenance-only |

`$vertexalpha` on the rerouted sprite unit (`engine/vertexcolorblend`, one of the 5 `unlitgeneric`
`$ignorez` units re-routed to `M_V2_Sprite`) is **not** in this table: `M_V2_Sprite` now exposes
`UseVertexAlpha` (this same ruling), so that key has a real destination and stages cleanly rather
than diverging.

The remaining 5 of the original 13 stage failures were never divergences of their own — they were
the 5 patched map units (`maps/ch_fulab_1/water/cheap_water`,
`maps/ch_fulab_1/water/cheap_water_1318_1990_273`, `maps/sm_diner_1/stone/dincountertp`,
`maps/sm_diner_1/stone/dincountertp_401_162_38`, `maps/sm_pier_1/water/invisible_water_depth_33`)
whose *base* material (`water/cheap_water`, `stone/dincountertp`, `water/invisible_water`) failed
to stage; once the base units above stage cleanly, so do their patches, with no divergence entry
of their own needed.

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

**A master rebuild orphans its `MI_` children in-session** (M8 review fix, `_fresh`'s own
docstring in `make_v2_materials.py`): `_fresh` deletes and recreates the `UMaterial` asset at each
master's path, and the editor session's already-loaded `MI_` instances do not automatically
re-resolve against the fresh object underneath the same path. `uv run elysium import materials`
must therefore re-run after any `make_v2_materials.py` rebuild to re-parent them — and
`build_content.py` (which runs the masters generator) must never share a process with
`import_materials.py` (which parents the `MI_`s) for exactly that reason, the same
never-share-a-process rule the concurrency note above already states for two agents' own editor
launches.

**The recipe states the full instance, not the parameters alone (review finding 2/4).** Two
findings surfaced against the first full run: `bake_lib.make_material_instance`'s
`clear_all_material_instance_parameters` on a reused asset clears non-static parameters only — a
static switch, a base-property override or a `PhysMaterial` set by an earlier recipe survives a
later run that no longer wants it, unless the entry states its *whole* decision every time, not
only what changed. So every entry now carries:

- **`switches` is full state, not "what turned on".** `allSwitches` (also mirrored into
  `recipe.params.allSwitches`) is the sorted list of every static-switch name the entry's master
  exposes (empty for a patched instance, which has no master and no switch of its own); `switches`
  states `True`/`False` for every one of those names, so the editor phase's per-switch
  `set_material_instance_static_switch_parameter_value` call always runs, never merely for the
  names a rule happened to set.
- **`basePropertyOverrides` always carries `blendMode`/`twoSided`/`opacityMaskClipValue`.**
  `twoSided` is always a real bool (never omitted); `opacityMaskClipValue` is the clip value only
  on a `Masked` blend, `null` otherwise. The editor phase reads absence-vs-presence the same way
  either side of the pair: `override_two_sided`/`override_opacity_mask_clip_value` are set
  `True`-with-a-value or explicitly `False` every entry, never left at whatever the asset already
  carried. A patched instance's `basePropertyOverrides` is `{}` (it authors no blend/two-sided/clip
  decision of its own), which clears all three explicitly too.
- **`physMaterial` is always applied, `None` clears it.** The editor phase always calls
  `mic.set_editor_property("phys_material", ...)`, with the loaded asset or `None`.
- **`recipe` covers the sidecar and the physical material, not only the bound parameters.**
  `recipe.physMaterial` mirrors the entry's `physMaterial`, and `recipe.provenanceSha256` is a
  sha256 of the exact bytes the provenance sidecar is written as. Any change to what the instance
  or its provenance carries — a corrected anomaly, an added omission row, a `physMaterial` no
  longer wanted, a switch that no longer turns on — changes this hash and re-imports the entry, so
  a `settingsVersion` bump is the coarse, whole-corpus lever and the recipe hash is the fine one;
  either covers a given change, and a change belongs to whichever one is cheaper to state.

**A `textureClassMismatch` on a required slot is a stage failure, not only an anomaly (review
finding 5).** `REQUIRED_TEXTURE_SLOTS` names `BaseTexture` as required on every one of the nine
masters — the one slot with no other colour source — so a unit whose only `$basetexture` resolved
to a texture that staged as the wrong class (typically an `$envmapsphere`/flipbook texture that
landed as a `TextureCube`/`Texture2DArray` where a plain `Texture2D` is wanted) fails the unit
loudly instead of authoring an instance with a silently-white `BaseTexture`. The same mismatch on
an optional slot (`EnvMap`, `EnvMapMask`, `NormalMap`, …) stays a recorded anomaly and the unit
still stages. `StageResult.summary()` and `import_report.json` both roll every anomaly/omission up
by `kind`, so `textureClassMismatch=N`/`animatedFramesArrayUnavailable=N`/etc. are visible without
walking every provenance sidecar by hand.

**A `textureClassMismatch` on `BaseTexture`/`NormalMap` binds the frame-0 array instead of failing,
on a master that exposes the frames lane (closing verification, 2026-08-31).** Source itself draws
a fixed frame (`$frame`/`$bumpframe`, default 0) of a multi-frame texture when no `animatedtexture`
proxy animates it, so a `$basetexture`/`$bumpmap`(`NormalMap` lane)/`$normalmap` that resolved but
staged as a `Texture2DArray` (`textures.py`'s own `frames > 1` rule) is not actually unbindable —
`_apply_static_frame_fallback` (`importers/materials.py`) binds the unit's own `TA_` sibling onto
`BaseTextureFrames`/`NormalMapFrames`, sets `FrameCount`/`NormalFrameCount` from the texture's own
sidecar, `FrameRate`/`NormalFrameRate = 0` (static — never animates), and turns on
`UseAnimatedFrames`/`UseAnimatedNormalFrames` plus the slot's own gate switch
(`UseBaseTexture`/`UseNormalMap`). This only fires on a master that exposes the matching lane —
`BaseTextureFrames` on Lit/LitTranslucent/Unlit/Sprite, `NormalMapFrames` on Lit/LitTranslucent/
Water — and only when no `animatedtexture` proxy already claimed the slot (an existing proxy that
found its own array wins outright, and now also marks the slot resolved for the required-slot
check below, closing a gap where a proxy-bound `BaseTexture` on a *required* slot still failed the
unit). A master with no lane for the mismatched slot (Eyes, Decal, Refract, Water's own
`BaseTexture`, TwoTexture) is untouched — the `textureClassMismatch` anomaly, and any
required-slot failure it causes, stands exactly as before, and so does a `TextureCube` mismatch
(no frames array exists to fall back onto — see the cube-`$basetexture` divergence below for the
two units that carry one). The `textureClassMismatch` anomaly itself is still recorded on
every bound-via-fallback unit — the mismatch is real, only its consequence changed — so the anomaly
rollup count rises by exactly the number of units this rule rescues. An authored `$frame`/
`$bumpframe` other than the default `0` is a genuine divergence from the frame this fallback always
samples: recorded as a `staticFrameOffsetUnsupported` omission (`parameter`, `key`, `value`) rather
than honoured (no per-instance frame-offset parameter exists) or silently dropped; no unit in the
current corpus authors either key non-zero.

**A cube `$basetexture` on the two units that author one is a named divergence, not a failure
(R7.1 follow-up, 2026-09-04).** `CUBE_BASE_TEXTURE_DIVERGENCE_UNITS`
(`importers/materials.py`) names the corpus's only two units whose `$basetexture` resolves to a
VTF that staged as a `TextureCube` (`faces == 6`):

| Unit | Texture | Ruling |
|---|---|---|
| `envmap/gioint` | `TC_gioint`, 32×32 DXT1 cube | `UnlitGeneric { $baseTexture envmap/gioint }` — the Giovanni-mansion interior probe, consumed as `$envmap` by 17 `stone/gio*` units |
| `skybox/hav_env` | `TC_hav_env`, 256×256 DXT1 cube | `UnlitGeneric { $basetexture skybox/hav_env, $nofog 1 }` — referenced by no other unit at all |

Neither is ever **drawn**: neither key appears as a face material in any exported map's `usemtl`
list, on any model, or in any `.env`/`.props`/`.ents` sidecar. Source could not draw them either —
`$basetexture` is a 2D sampler on `UnlitGeneric`, so a cube VTF bound there samples nothing
meaningful in the 2004 engine, and the cubemap reaches the screen only through *another* unit's
`$envmap`, which reads the texture directly and never this material. There is no lane to fall back
onto (no master carries a cube base-colour sampler, and adding one would author a look the VMT does
not describe), so the instance ships with `UseBaseTexture` off and the reason is written once into
provenance as a `cubeBaseTextureProvenanceOnly` omission beside the `textureClassMismatch` anomaly
that caused it. The list is per unit, like `IGNOREZ_NAMED_DIVERGENCE_UNITS` and not keyed on the
class mismatch itself: a cube `$basetexture` on a unit that *is* drawn keeps the loud failure.

**A patched instance's `ElysiumMaster` registry tag is the root master, not empty (review finding
7).** A patched unit's own provenance carries no master of its own (its parent is another `MI_`,
never a `M_V2_*` asset), so its `master` field used to serialize as `null` and the
`ElysiumMaster` tag stamped empty, dropping every one of the corpus's 7,499 patched instances out
of a Content Browser filter on that tag. `stage_materials` now walks a patched entry's `parent`
chain, inside the same run, to the non-patched base unit and writes *that* unit's own master onto
the patched provenance's `master` field before the sidecar is written, so the tag covers every
instance.
