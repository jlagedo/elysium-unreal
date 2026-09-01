# Reflections — `$envmap` and the Unreal channel

What VtMB's cubemap reflection actually is, measured out of the shipped shader assembly and
the whole-game material set, and how the remaster reproduces it on a fully-dynamic Lumen path.

Per-task status belongs in `docs/project/roadmap.md` (7.5). Related: `docs/architecture/rendering-perf.md` (the render path,
and → "Lumen surface-cache engine facts" for why the surface cache is what a reflection ray
hits), `docs/vtmb/sky-ambience.md` → "K7" (the same `.psh`-as-data route), `docs/architecture/asset-enhancement.md` (the
PBR-synthesis track that later feeds these same slots).

## The shipped shader is readable data

The `.psh` shaders are readable ps.1.1 assembly, not compiled binary (`docs/vtmb/texture_format.md`; route
confirmed at `docs/vtmb/sky-ambience.md` → RE-A9), so the reflection composite is a file read, not a
decompile. `lightmappedgeneric_maskedenvmap.psh` is the world's masked path in full:

```
tex t0                      ; base texture
tex t1                      ; lightmap
tex t2                      ; cube
tex t3                      ; $envmapmask

mul    r0,     t0, v0       ; base times vertex color (with alpha)
mul    r1,     t2, t3       ; envmap * envmapmask
mad    r0.rgb, r1, c2, r0   ; + envmap * envmapmask * envmaptint (color only)
mul    r0.rgb, t1, r0       ; fold in lighting (color only)
mul_x2 r0.rgb, c0, r0       ; * 2 * (overbrightFactor/2)
```

## The composite

```
pixel = ( base·vertexColor + cube·mask·tint ) · lightmap · 2
```

**The reflection is an albedo term, and the lightmap multiplies it.** It is added to the base
colour *before* lighting is folded in, not composited over the lit result. Three consequences,
all load-bearing for the port:

- **A reflective surface in an unlit room stays dark.** The lightmap scales the reflection to
  nothing. This is why VtMB can mark 22% of its materials reflective without the scene reading
  as chrome, and it is the guard the naive "additive overlay" reading loses.
- **It is already a light-modulated specular response** in everything but name, so mapping it
  onto a real reflection channel is closer to the original than reproducing an additive term
  would be.
- **`$envmaptint` (`c2`) multiplies the cube's contribution**, so it is the reflection's colour
  and strength in one constant.

The variants agree term for term:

| Program | Reflection term |
|---|---|
| `lightmappedgeneric_envmap` | `cube · tint` (unmasked — uniform reflectivity) |
| `lightmappedgeneric_maskedenvmap` | `cube · mask · tint` |
| `lightmappedgeneric_basealphamaskedenvmap` | `cube · (1 − baseAlpha) · tint` — **inverted** alpha |
| `vertexlitgeneric_envmap` | `cube · tint`, then `· vertexLighting · 2` |
| `vertexlitgeneric_maskedenvmap` | `cube · mask · tint`, then `· vertexLighting · 2` |

The model path is the same shape with vertex lighting where the world has a lightmap, so world
and props take one treatment. There is **no Fresnel** on either — the mask is flat.
`$FRESNELREFLECTION` is water-only.

## `$envmapcontrast` and `$envmapsaturation` do not exist

**No term for either appears in any shipped `.psh`.** The assembly above is the whole reflection
path. Measured against the material set, the authoring agrees: over the install's 11,624 VMTs,
**2,610 carry `$envmap`**, and of those `$envmapsaturation` is authored **zero** times and
`$envmapcontrast` **18** times (17× `1.0`, 1× `0.85`). They are Source keys VtMB's renderer
never implemented. The exporter parses them into `<map>.mtl` as `envparams <contrast>
<saturation>`; nothing consumes them, and nothing should.

## What is authored

Whole game, the 2,610 `$envmap` materials:

| | Count | |
|---|---|---|
| `vertexlitgeneric` (models/props) | 1,419 | the **larger** half |
| `lightmappedgeneric` (world) | 1,124 | |
| other (`unlitgeneric`, `refract`, `water`, …) | 67 | |
| `$envmapmask` texture | 2,368 | |
| `$basealphaenvmapmask` | 14 | masked by `1 − alpha` |
| unmasked (uniform) | 228 | |
| `$envmaptint` unset (implicit white) | 2,146 | |
| `$envmaptint` grey (channel spread < 0.02) | 362 | 361 at exactly zero spread |
| `$envmaptint` chromatic | 102 | 52 props, 45 world, 5 refract |

The tint split is **bimodal, not a judgement call**: the channel-spread histogram runs 361
entries at exactly `0.00`, then nothing until `0.05`. A 0.02 threshold separates the two
populations with a clear gap on either side.

The grey tints are dim-downs — `0.5`, `0.33`, `0.25` — scaling reflection strength. The
chromatic 102 are the game naming its own materials: `0.65 0.5 0.0` and `1.0 0.7 0.0` (brass and
gold), `0.74 0.57 0.31` and `0.52 0.36 0.25` (copper), `1.0 0.0 0.0`, and a blue/teal set
(`0.5 0.6 0.9`, `0.3 0.6 1.0`, `0.4 0.8 0.8`) that is **tinted glass, not metal**. This is a
hand-authored metal mask, which is exactly what `docs/architecture/asset-enhancement.md` requires before any
surface is allowed to go metallic — it is read here, never inferred.

## The Unreal translation

The translation is one material graph with one reflection state and two coordinated terms. The
source term samples VtMB's authored cube; the optional native term changes the same surface's PBR
response so Lumen can reflect the rebuilt scene. `elysium.RainEnhancement` is a blend inside that
graph, not a selector between faithful and enhanced materials, controllers, or assets.

### Source cube term

For the primary view, the graph reconstructs the shipped input in linear colour:

```
source = SampleCube(SourceCube, (UE.X, -UE.Y, UE.Z)) · EnvMaskLinear · EnvTint
```

For a `GlobalWetness` material, `EnvTint` is the presented wetness multiplied by the proxy's
authored scale. Ordinary `$envmaptint` materials use their static RGB tint. The source cube is
colour data and receives the texture's normal sRGB decode; the mask is data, imported with sRGB
off/`TC_Masks` and sampled with UE's `Masks` sampler type. A linear one-pixel white mask is the
unmasked fallback. The Y negation is the established Source-to-Unreal handedness correction; face
order stays VTF/DDS `+X,-X,+Y,-Y,+Z,-Z`.

The primary view receives `source` as an additive Emissive contribution, after the stable surface
Base Color. This is a translation boundary rather than a claim that VtMB marks the term emissive:
VtMB adds the cube before its authored lightmap composite, but the rebuilt UE scene has dynamic
lighting instead of that lightmap. Putting the term in UE Base Color made it vanish on the dark
Santa Monica street even though the cube sample was valid. Emissive preserves the visible additive
environment term. `Ray Tracing Quality Switch` replaces it with black for ray shaders and Lumen
card capture, so the camera-dependent reflection vector cannot enter global illumination or
secondary reflections. The stable Base Color remains on both branches.

The six exported faces become one `UTextureCube` without upscaling or invented HDR range. Import
must prove Source-to-Unreal face orientation and handedness with a labelled axis cube before any
brightness judgement. UE's DDS face order is +X, -X, +Y, -Y, +Z, -Z; a yaw control cannot repair a
face swap or mirror. Default cube sampling and its mip chain provide the source-like filtered
lookup; no roughness value is inferred from cube resolution.

### Enhanced Lumen term

Let `E` be the `0..1` enhancement value. The source reflection remains the reference at `E=0`:

```
source_weight = lerp(1, SourceRetain, E)
coverage      = E · wetness · EnvMaskCoarse · Exposure · Upward
Roughness     = saturate(RoughBase - WetRoughnessReduction · coverage)
Specular      = lerp(SpecBase,  WetSpecular,  coverage)
```

`EnvMaskCoarse` is a coarser mip or otherwise low-frequency reading of the same mask. `Exposure` is
the per-map rain-height cover test and `Upward` is the upward-facing world-normal gate; both affect
the enhanced wet response only. The raw mask still controls the authored cube's intensity, including
under cover, because the VMT proxy does not encode roof exposure. Using the raw mask's cracks and
aggregate directly as PBR roughness creates glitter and broad local-light glare. `SourceRetain` is
tuned against fixed-camera captures because a material cannot read the completed Lumen reflection
and perform an exact energy-conserving crossfade. The enhancement therefore lowers the source term
as it enables Lumen, instead of stacking two full-strength reflections.

At `E=0`, `RoughBase=1` and `SpecBase=0` keep the source-reference world Lambert outside the sampled
cube term. At `E>0`, Lumen reflects the rebuilt geometry, props, characters, signs, and dynamically
lit surfaces. The light rig's local-light `SpecularScale` remains zero at the source baseline and is
an independent diagnostic, not an automatic part of wetness. Chromatic `$envmaptint` may still
classify an enhanced material as metallic, but that is a presentation divergence and is outside the
grey `sm_hub_1` wetness slice.

### Cubes are not reconstructed lights

The map sky cube and a material `$envmap` cube have different jobs. `ElysiumMapVisuals` assigns the
map's `sky_` cube to the movable Sky Light; Unreal filters it into specular mips and diffuse sky
irradiance, and Lumen applies the map-authored sky-ambient magnitude with sky occlusion. A material
cube is sampled locally by the material graph. Assigning it to the Sky Light would make it an
infinite global light, affect dry and non-proxy surfaces, and duplicate radiance already represented
by the reconstructed `.lights` rig.

`sm_hub_1` authors no type-5 sky-ambient row, so its faithful Sky Light magnitude is zero even though
the sky is visible. Its 32x32 LDR `cubemapdefault` remains material reflection data; it is not
normalised into a new sky light. Reflection Capture actors are also outside this translation: they
project UE's static PBR environment rather than VtMB's flat masked term, and UE 5.8 does not use
them as the default Lumen hit-lighting path. Extracting point or spot lights from the cube would be a
new inverse-lighting reconstruction with no depth or source positions, while those positions already
exist in the map light data.

## The divergence

Source evidence: `base + cube·mask·tint`, all multiplied by VtMB's lightmap, with no Fresnel and one
authored cube lookup per surface. UE source-reference presentation keeps Base Color stable and maps
the cube term to primary-view Emissive because the dynamic Lumen/direct-light solution cannot stand
in for the authored lightmap at this composite point. Enhanced presentation retains a tunable
fraction of that term while a low-frequency wet coverage enables Lumen roughness/specular response
against the live Unreal scene. The enhanced term changes appearance only; it does not change the
authored cube, mask, tint, wetness state, or map lighting.

## Known gaps

- **UE lighting is not VtMB's lightmap.** The primary-view Emissive placement preserves the visible
  additive cube on dark dynamically lit surfaces, but it cannot reproduce the exact per-luxel
  `lightmap · 2` modulation. Original-retail A/B decides the accepted magnitude.
- **Cube orientation needs an executable proof.** The exported faces exist, but the environment
  path's Source-to-Unreal face mapping must be validated for material reflection vectors rather
  than assumed from the sky backdrop.
- **The enhanced crossfade is empirical.** `SourceRetain`, wet roughness, and wet specular require
  fixed-camera tuning; a single material graph cannot sample Lumen's completed reflection energy.
- **The metal path is thinly exercised.** The grey `sm_hub_1` proxy corpus does not validate the
  102 chromatic-tint materials or their enhanced metallic classification.
- **What a reflection can and cannot see** remains constrained by Lumen. The 2D backdrop and the
  3D-skybox miniature are excluded from ray tracing; sky radiance reaches Lumen only through the
  map's authored Sky Light level.
- **The runtime A/B is gone, not replaced yet.** `ApplyMaterialOverrides` (a dynamic-instance pass
  standing a MID in front of each baked material so `elysium.RoughBase`/`SpecBase`/`RoughReflect`/
  `SpecReflect`/`EnvReflect` could be turned live) was retired in R4.5
  (`docs/project/seam_migration.md`), superseded by `UElysiumSurfaceSettings` as the project's one
  taste-tuning surface. It reached only the pre-V2 baked per-map materials and is not yet wired to
  them; per-material reflection tuning on that path is bake-time (`pipeline/unreal/make_world_materials.py`)
  until R5.4 rebinds maps onto the V2 masters `UElysiumSurfaceSettings` already drives.

## How to re-measure

Everything above comes from the install alone — no engine run, no Ghidra:

- the composite: read `materials/dxshaders/lightmappedgeneric*envmap*.psh` and
  `vertexlitgeneric*envmap*.psh` through `install.build_index(dirs=("materials",))`.
- the authoring survey: `vmt.parse` over every `materials/**/*.vmt`, counting `envmap`,
  `envmapmask`, `basealphaenvmapmask`, `envmaptint`, `envmapcontrast`, `envmapsaturation`.
- the grey/chromatic split: `max(tint) − min(tint)` against a 0.02 threshold.
