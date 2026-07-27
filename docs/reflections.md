# Reflections — `$envmap` and the Unreal channel

What VtMB's cubemap reflection actually is, measured out of the shipped shader assembly and
the whole-game material set, and how the remaster reproduces it on a fully-dynamic Lumen path.

Per-task status belongs in `roadmap.md` (7.5). Related: `rendering-perf.md` (the render path,
and → "Lumen surface-cache engine facts" for why the surface cache is what a reflection ray
hits), `sky-ambience.md` → "K7" (the same `.psh`-as-data route), `asset-enhancement.md` (the
PBR-synthesis track that later feeds these same slots).

## The shipped shader is readable data

VtMB ships its DX8 pixel shaders as **ps.1.1 assembly source** with Valve's comments intact,
under `materials/dxshaders/*.psh`, resolved through `install.build_index` like any other asset
(`sky-ambience.md` → RE-A9 establishes the route). So the reflection composite is a file read,
not a decompile. `lightmappedgeneric_maskedenvmap.psh` is the world's masked path in full:

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
hand-authored metal mask, which is exactly what `asset-enhancement.md` requires before any
surface is allowed to go metallic — it is read here, never inferred.

## The Unreal channel

The render path is fully dynamic HWRT Lumen (`rendering-perf.md`) and the baked level gives
every surface DDC-fitted Lumen cards (`uasset-bake-spike.md`), so **Lumen produces the
reflection** and the exported `tex/cube/` faces are not sampled by the world or prop graph. They
remain the 2D sky's source.

The masters (`tools/make_world_materials.py`) carry the term as named parameters:

```
env       = saturate(EnvMask.r · EnvStrength)
Roughness = lerp(RoughBase, RoughReflect, env)
Specular  = lerp(SpecBase,  SpecReflect,  env) · (1 − f)
Metallic  = env                                   -- chromatic-tint materials only
BaseColor = ... tinted by EnvTint on those same materials
```

`f` is the per-primitive distance-fog term (`mat_fog.py`); the reflection response fades with
it, or a reflection would shine through fog the diffuse had already faded.

`RoughBase` 1.0 / `SpecBase` 0 makes the non-`$envmap` world **Lambert**, which is what the
material data says (`METALLIC 0`, `SPECULAR 0`, `ROUGHNESS 1`) and what the light rig already
assumes when it sets `specular_scale = 0` on every source. Reflective surfaces are the
exception, and the `$envmapmask` is what localises them — a mask puts the reflection on the
brass fittings, not on the whole wall.

**What a reflection can and cannot see.** The 2D backdrop and the 3D-skybox miniature are both
`SetVisibleInRayTracing(false)` — a scene-enclosing mesh is the canonical HWRT overlap cost — so
a reflection never shows either directly. The sky reaches a reflection only through the SkyLight
IBL, whose level is the map's own type-5 `emit_skyambient` magnitude and is **zero on the 83
maps that author no `light_environment`** (`sky-ambience.md` → C1/C2). On those maps an outdoor
reflective surface has no sky term in its reflection at all.

## The divergence

Faithful: `base + cube·mask·tint`, all of it multiplied by baked light, no Fresnel, one baked
cube per surface. Chosen: a roughness/specular channel Lumen resolves against the live scene,
plus `Metallic` from the mask on the 102 chromatic-tint materials. The dated owner call and the
reasoning: `decisions.md`.

## Known gaps

- **The reflection's *magnitude* is not derived from VtMB's own data.** The mask says *where* a
  surface reflects and the tint scales it, but VtMB's actual reflection brightness came from the
  radiance of the cube it sampled — a dark night cube contributes very little. Our channel
  replaces that with a fixed `RoughReflect`/`SpecReflect` pair, so a near-white mask means a
  strong reflection regardless of how dark the original cube was. The exported `tex/cube/` faces
  are still on disk, so a per-material strength derived from each cube's own mean radiance is
  available without new decoding, and would reproduce the original's magnitude rather than
  approximate it. Not attempted; the fixed pair is what is measured and baselined.
- **The metal path is thinly exercised.** 102 materials game-wide, and only 3 across the two
  re-baselined maps (`soccurtainrod`, `lantern`, one on `ch_temple_1`). The maps where it
  concentrates — Hollywood and Chinatown brass — have no shot baseline, so `Metallic` from the
  tint is verified as *bound and classified*, not as *looking right*.
- **Reflections can never show the sky or the miniature**, both being ray-tracing-excluded, and on
  the 83 maps whose SkyLight is zero an outdoor reflective surface has no sky term at all.
  Consequence of C1/C2 + the HWRT overlap rule, not of this task; unmeasured.
- **A live knob costs texture streaming.** `ApplyMaterialOverrides` is lazy precisely because a
  runtime `SetMaterial` drops the primitive's built streaming data; during an A/B session the
  world will be briefly blurry while mips settle. Acceptable for a tuning tool, wrong for the
  shipped path — hence the laziness.

## How to re-measure

Everything above comes from the install alone — no engine run, no Ghidra:

- the composite: read `materials/dxshaders/lightmappedgeneric*envmap*.psh` and
  `vertexlitgeneric*envmap*.psh` through `install.build_index(dirs=("materials",))`.
- the authoring survey: `vmt.parse` over every `materials/**/*.vmt`, counting `envmap`,
  `envmapmask`, `basealphaenvmapmask`, `envmaptint`, `envmapcontrast`, `envmapsaturation`.
- the grey/chromatic split: `max(tint) − min(tint)` against a 0.02 threshold.
