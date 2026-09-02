# Water — Unreal reproduction

VtMB's water inventory, shaders and volumes are `docs/vtmb/water.md`. This
document is the Unreal side: the owner call, the seam between reproduced
placement and modern look, and the 5.8 assets worth opening first.

The goal is not a port of `_rt_WaterReflection` cameras or the 2004 DUDV
perturb. The goal is that a player who remembers the Santa Monica ocean, the
downtown canal, a warrens basin or the Vesuvius pool still *reads* those
places — murky, reflective, a little wrong — built with Unreal's own water
shading.

---

## 1. Owner call

**Presentation is modernized. Authored placement and parameters are
reproduced.**

- **Reproduced:** which faces are water; the plane height; the fog colour and
  in-volume distances; the reflect tint; which basins exist as
  `CONTENTS_WATER` (so `CheckWater` / swim / tread can fire); `$bottommaterial`
  as "there is an underside and an underwater look"; particle placements and
  rates; `surfaceproperties` `water` impacts and wades; a `trigger_hurt` that
  is authored on a pool stays a hurt volume.
- **Modernized:** how the pixel is built. Single Layer Water + Lumen/SSR
  replaces the planar-reflection camera and the DX8 DUDV. Scrolling normals
  replace `AnimatedTexture` + `TextureScroll`. Underwater is a post-process
  volume, not `waterwarp.vcs`. Splashes are Niagara, not `point_16` cards.
- **One presentation, not two.** There is no "classic water" mode that
  composites the 2004 RTs. Cheap vs expensive in Source is a cost LOD; the
  Elysium has one look, scaled by the authored numbers (murkier fog, stronger
  tint, less opacity), not by swapping shaders.

This matches the weather and effects rules
(`docs/vtmb/weather.md`, `docs/architecture/effects-architecture.md`).

**Default representation: Single Layer Water on the authored water mesh, at
the sidecar plane, with Lumen/SSR. Not the Water plugin's Ocean/Lake/River
bodies.** Those bodies want a Landscape, a Water Zone, and a spline mesh
that replaces the BSP. VtMB water is already a mesh at a known Z. Keep the
mesh.

---

## 2. What already exists in this project

Do not rebuild these to explore the look.

| Piece | Where | What it does |
|---|---|---|
| VMT parse | `pipeline/src/elysium_pipeline/formats/vmt.py` | `Water` shader and `%compilewater` → `water` |
| Corpus record | `shared_corpus.material_record` | `water_normal`, fog colour / start / end, reflect tint |
| Map sidecar | `<map>.water` | per-material plane Z (cm), normalmap, fog, reflect tint |
| MTL flag | `<map>.mtl` `water 1` | this face group is water *here* |
| Collision | `.hulls` + `BLOCK_MASK` | water is passable |
| Movement | `UElysiumMovementComponent::WaterMove` | formula-faithful, nothing sets `WaterLevel` yet |
| Bake | `bake_map.py` `_master_for` | water currently instances `M_World_Translucent` |
| Strategy target | `docs/project/rebuild-strategy.md` | names `M_Water` (Single Layer Water); that master is not generated |

The Water, Water Extras, Water Advanced and Niagara Fluids plugins are
**present in this 5.8 install and not enabled** in `ElysiumUE.uproject`.
Explore them with **Show Engine Content / Show Plugin Content**, or by
opening the sample maps below in the engine editor. Do not add those plugins
to the game project until a representation is chosen. Niagara (sprites) is
already on.

---

## 3. Decision tree

Pick the row from the authored fact, not from the map name.

| Authored fact | First Unreal primitive | Do not reach for |
|---|---|---|
| `Water` shader faces (`sewer_water`, `warrwater`, `pool_water`, …) | **Single Layer Water** on those triangles (or a horizontal plane at sidecar Z if the mesh is junk) | Water Body Ocean, a planar `SceneCapture2D` mirror |
| `LightmappedGeneric` water-look (`blackwater`, `bloody_water`, `warrenwater2b`) | existing `M_World_Translucent` + scrolling normal + the cube path already in `docs/vtmb/reflections.md` | SLW. These never had a volume integral |
| `invisible_water` | no surface. A volume (post-process + contents) at the plane | any visible material |
| `$bottommaterial` / underwater camera | SLW has no backface. A second underside plane *or* `M_UnderWater_PostProcess_Volume` when the camera crosses the plane | expecting SLW to draw from below |
| In-volume fog (`$fogcolor`, start/end) | SLW **Absorption Coefficients** + **Opacity**, plus the underwater post if the camera is in the volume | a second `UExponentialHeightFog` (the world already has two fog sets) |
| Drips / trickle / mist / spa steam | existing rain/drip Niagara family; Fountain / `HangingParticulates` | Fluids |
| Body / bullet splash | `SimpleSpriteBurst` first; `Grid2D_FLIP_Splash` only as a hero (Andrei, a jump-in the player is looking at) | 3D FLIP |
| Caustics on a pool floor | optional `M_Caustics_LightFunction_Sun` on one local light | Water plugin caustics generator, volumetric caustics |
| Intersection foam | skip on v1. A soft depth-fade in the SLW opacity is enough | shoreline Niagara, Gerstner foam |
| Player actually swimming | `SetWaterLevel` from a contents / plane test; the move and the anim are already written | Water plugin buoyancy, shallow-water sim |
| Hurt-on-contact pool (`spawnwater`) | the existing `trigger_hurt` entity, not a water look | making it a pretty swimmable basin |

**Ocean vs puddle is not a technology fork.** `sm_pier_1` and
`hw_vesuvius_1` are the same primitive at different size and tint. A Water
Body Ocean is only worth a second look if the pier horizon cannot be faked
with the authored mesh plus a far-plane colour match — try SLW on the mesh
first.

---

## 4. Parameter mapping

Source keys → the SLW graph. Numbers stay authored; the node they land on
is the translation.

| Source | SLW / material input | How to read it |
|---|---|---|
| `$normalmap` (`dev/water_normal`) + `AnimatedTexture` 20–30 fps + `TextureScroll` ~0.05 @ 45° | World-aligned panner on a normal texture, two layers at that rate / angle | Do not invent Gerstner. Dual-panned normals *are* the 2004 motion |
| `$bumpmap` (`dev/water_dudv`) | skip as a texture. The DUDV was a DX8 way to offset the RT UVs. SLW refraction already reads scene colour / depth along the normal | If the authored `$refractamount` is high, raise normal strength, not a second DUDV |
| `$reflecttint` | Specular colour × a scalar; also a mild Base Color tint | Grey-green 0.3–0.7 on canals; red on blood / spawn; near-white on pools |
| `$refracttint` | **Color Scale Behind Water** | Same numbers. This is how sewage stays brown-green when you look *through* it from above. Epic: this input does not apply once the camera is under the plane |
| `$reflectamount` / `$refractamount` (15–100) | normal intensity / a refraction scalar. Not 1:1 units | Author 60/22 as "strong reflect, modest warp". Start around normal 0.4–0.7 and tune against a capture |
| `$fogcolor`, `$fogstart`, `$fogend` | **Absorption Coefficients** (1 / distance-to-extinct per channel) and **Opacity** | Small `$fogend` (10–64, blood / spawn) → high absorption, high opacity. Large `$fogend` (1024–1500) → you can see into it. Reciprocal metres, not Source inches — convert |
| `$fogenable 0` | absorption near zero, opacity from Fresnel only | `dev/ocean`, `confession_water`, `malk_water` |
| Fresnel (hard-wired Schlick^5 in `waterreflect.psh`) | SLW already Fresnels. Leave it | Do not add a second Fresnel on Specular |
| `$envmap` on cheap / LMG water | the existing source-cube term, not SLW | `docs/vtmb/reflections.md` |
| `$forcecheap` / cheap distances | ignore as a shader swap. Use one SLW and let Lumen be the cheap reflection | A planar `SceneCapture` is the thing we are refusing |
| `$bottommaterial` | underwater post + optional underside plane using the same absorption | Do not try to make SLW two-sided. The sidecar currently emits the underside as a second row at the same Z — pick one drawn SLW and treat the other as the underside / skip |
| `%compilewater` / `LEAFWATERDATA` | a volume the movement query and the underwater post can test (plane Z + `minZ`) | The visual mesh and the volume can be the same actor |
| `surfaceprop water` | Unreal physical material + the wade / impact sounds | Independent of the look |

Absorption is the important translation. `$fogcolor {5 5 0}` with
`$fogend 1024` on `sewer_water` means "light dies over ~26 m, and it dies
greener than red" — that is an absorption vector, not a fog actor.

---

## 5. The Unreal toolkit (this 5.8 install)

Everything below is already on disk at `D:\Epic\UE_5.8`. Content Browser
paths assume **Show Engine Content** and **Show Plugin Content**.

### 5.1 Single Layer Water — the default

Docs:
[Single Layer Water Shading Model](https://dev.epicgames.com/documentation/unreal-engine/single-layer-water-shading-model-in-unreal-engine).

SLW is an **Opaque or Masked** shading model with its own pass after the
base pass and deferred lighting. It reads the lit scene and depth to do
refraction, runs a volume BSDF (scattering + absorption + PhaseG), and
composites reflection. Blend Mode stays Opaque. Opacity on the main node is
the surface / volume mix, not alpha blend.

Open, in this order:

| Content Browser path | Disk | Why |
|---|---|---|
| `/Engine/EngineMaterials/WaterMaterial` | `Engine/Content/EngineMaterials/WaterMaterial.uasset` | smallest SLW. Proves the shading model on a plane in five minutes |
| `/Water/Meshes/S_WaterPlane_256` | `Engine/Plugins/Experimental/Water/Content/Meshes/S_WaterPlane_256.uasset` | the plane to drop. Non-Nanite. Assign an SLW material and stop |
| `/Water/Materials/WaterSurface/Water_Material_Lake` | `Engine/Plugins/Experimental/Water/Content/Materials/WaterSurface/Water_Material_Lake.uasset` | **first game-like instance.** No ocean Gerstner. This is a canal / sewer / pool |
| `/WaterAdvanced/Materials/Water_Material_Simple` | `Engine/Plugins/Experimental/WaterAdvanced/Content/Materials/Water_Material_Simple.uasset` | still simpler than the full Water master |
| `/Water/Materials/WaterSurface/Water_Material` | `Engine/Plugins/Experimental/Water/Content/Materials/WaterSurface/Water_Material.uasset` | the plugin's one master. Other instances: `_Ocean`, `_River`, `_CustomMesh` |
| `/Water/Materials/WaterSurface/Water_PanningTextures` | same folder | the dual-panner recipe. Then swap in `dev/water_normal` |
| `/Water/Materials/WaterSurface/Water_Material_CustomMesh` | same folder | the instance meant to be dropped on *your* mesh, not a Water Body |
| `/Water/Materials/Functions/Water_Underside` | `…/Functions/Water_Underside.uasset` | how Epic does the view from below |
| `/Water/Materials/Debug/Debug_Absorption` | `…/Debug/Debug_Absorption.uasset` | tune `$fogcolor` → absorption without fighting waves |
| `/Water/Materials/Debug/DebugRefraction` | `…/Debug/DebugRefraction.uasset` | see the scene-colour read |

Graph to copy, not to ship: **Single Layer Water Material** node with

- Scattering Coefficients — keep low on canals (clear-ish volume, dark
  absorption), raise on spa steam-adjacent pools;
- Absorption Coefficients — from `$fogcolor` / `$fogend`;
- PhaseG — 0 (isotropic) until a sun-path shot asks for otherwise. VtMB is
  night-street; a strong PhaseG is a daylight trick;
- Color Scale Behind Water — `$refracttint`;
- Normal — dual panner on `dev/water_normal` (exported already as
  `../shared/tex/dev_water_normal_n.png`);
- Opacity — low on pools, high on blood / spawn / invisible-adjacent murk.

### 5.2 Underwater

| Path | Why |
|---|---|
| `/Water/Materials/PostProcessing/M_UnderWater_PostProcess_Volume` | first underwater look. Bind absorption / colour to the same fog numbers |
| `/Water/Materials/PostProcessing/MPP_Water_ChromaticAbberation` | optional; stands in for `waterwarp` |
| `/Water/Materials/PostProcessing/M_UnderWater_PostProcess_Mesh` | if a volume is easier to author as a box around the basin |
| `/WaterAdvanced/Materials/M_UnderWater_PostProcess_Volume_SW` | shallow-water variant; ignore until a swim exists |

Enable the volume only when the camera is below the sidecar plane. Epic's
own Water Bodies do this automatically; on an SLW mesh it is a plane test.

### 5.3 Water plugin bodies — reference, not the plan

Docs:
[Water System](https://dev.epicgames.com/documentation/unreal-engine/water-system-in-unreal-engine),
[Water Meshing and Surface Rendering](https://dev.epicgames.com/documentation/unreal-engine/water-meshing-system-and-surface-rendering-in-unreal-engine),
[Water Debugging and Scalability](https://dev.epicgames.com/documentation/unreal-engine/water-debugging-and-scalability-options-in-unreal-engine).

A Water Body needs a **Water Zone** in the level and wants a Landscape to
carve. That is the wrong data model for a BSP canal.

Open the sample maps to *see* SLW + underwater + waves, then close them:

| Map | Disk |
|---|---|
| `/WaterExtras/Maps/WaterTestMap` | `Engine/Plugins/Experimental/WaterExtras/Content/Maps/WaterTestMap.umap` |
| `/WaterExtras/Maps/WaterVelocityTest` | same folder |
| `/WaterExtras/Caustics/Maps/CausticsMap` | `…/Caustics/Maps/CausticsMap.umap` |
| `/Water/FluidSimulation/WaterFluidSimtestShallow` | `Engine/Plugins/Experimental/Water/Content/FluidSimulation/WaterFluidSimtestShallow.umap` |

Useful pieces inside the plugin that do **not** require a Water Body:

- `/Water/Materials/WaterSurface/Water_FarMesh` — a flat colour-matched
  card if the pier horizon needs a fill;
- `/Water/Materials/Functions/WaterOpacityMaskFromDepth` — soft edge against
  walls, stands in for intersection foam;
- `/Water/Content/MPC/MPC_Water` — if several basins must share time.

`r.Water.*` console variables only apply to Water Mesh tiles. They do
nothing for a standalone SLW static mesh.

### 5.4 Caustics — optional, one pool

| Path | Why |
|---|---|
| `/Water/Caustics/Materials/LightFunctions/M_Caustics_LightFunction_Sun` | a light function on one local light over Vesuvius / Giovanni. Cheap |
| `/Water/Caustics/Materials/LightFunctions/M_Caustics_LightFunction_SubUV` | animated sheet, still a light function |
| `/Water/Caustics/Materials/Functions/WaterCaustics_Static` | if a light function is too much, multiply into Color Scale Behind Water |

Do not run `GenerateCausticsTextures` or the volumetric / fluidsim caustics
for a 20k-triangle night map. Color Scale Behind Water is already the hook
SLW gives you for "bright pattern on the floor".

### 5.5 Niagara — dressing, not the surface

Same family rules as `docs/architecture/effects-architecture.md`. Water
adds:

| Asset | Use for |
|---|---|
| existing `NS_ElysiumRain` / drip path | `WaterDrops_Timer` |
| `/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst` | `Splash`, `WaterSplash`, bullet-in-water |
| `/Niagara/DefaultAssets/Templates/Emitters/Fountain` | trickle, pipe leak, `WaterfallTrickle` |
| `/Niagara/DefaultAssets/Templates/Emitters/HangingParticulates` | `watermist`, `waterfallmist`, `SpaMist` |
| `/NiagaraFluids/Templates/Liquid/2D/Systems/Grid2D_FLIP_Splash` | one hero splash (Andrei, a watched jump-in) |
| `/NiagaraFluids/Templates/Liquid/2D/Systems/ShallowWater/Grid2D_SW_Pool` | **do not** put under every basin. Look at it once if a warrens pool wants interactive ripples |
| `/WaterAdvanced/Niagara/Systems/Grid2D_SW_WaterBody` | same: a later polish, depends on the Water plugin |

Rain-on-water is weather (`rainsplash_new` is already a flat expanding
ring). It does not need Fluids.

### 5.6 What not to use

- **Water Body Ocean / Lake / River as the world representation.** Wrong
  input (Landscape + Zone + spline), wrong cost, and it discards the
  authored mesh.
- **Planar `SceneCapture2D` / `PlanarReflection` actors.** That *is* the
  2004 `_rt_WaterReflection` camera. Lumen on SLW is forced-mirror and is
  the accepted substitute.
- **Translucent-surface water** for real `Water` shader faces. The bake
  currently does this (`M_World_Translucent`). It cannot do absorption or a
  correct scene-colour refract, and it fights the translucency budget rain
  already spends.
- **Niagara 3D FLIP / heterogeneous volumes** for canals.
- **Gerstner waves** on the Santa Monica ocean. The authored motion is a
  24–30 fps normal and a 0.05 scroll. Waves would be a new artist decision.
- **Enabling Water + WaterAdvanced + Landmass on the project** just to
  explore. Open the engine content; decide; then enable what the chosen
  path actually needs (likely: nothing, if SLW lives in
  `make_world_materials.py`).

---

## 6. What to open this week (no bake, no game code)

Work in the Unreal editor against engine / plugin content. Duplicate into a
scratch `/Game/ElysiumAuthored/FX/_Explore/` folder (project-owned) or a
disposable map. Do not run `export` or `build`.

Suggested order. Each step is one proven example.

1. **`/Water/Meshes/S_WaterPlane_256` + `/Water/Materials/WaterSurface/Water_Material_Lake`.**
   Confirm the window title is `PCD3D_SM6`, Lumen on, MegaLights on. This
   is the "does SLW even draw in our render path" test. (Plugin content
   must be visible; do not add the plugin to the `.uproject`.)
2. **Retune that plane to `sewer_water`.** Absorption from `{5 5 0}` /
   fogend 1024 in, Color Scale Behind Water `[0.7 0.7 0.7]`, dual-panned
   `dev/water_normal` (steal the panner from
   `/Water/Materials/WaterSurface/Water_PanningTextures`), opacity
   ~0.25–0.4. Sit it next to a `sm_hub_1` or `la_hub_1` capture of the
   canal. This *is* the downtown / Santa Monica look. Judge the *reflection
   of neon*, not a MegaLights specular lobe on the water (MegaLights does
   not light water — see §7).
3. **Retune a copy to `pool_water` / Vesuvius.** Greener fog, longer
   `$fogend`, brighter Color Scale. Decide whether one material with
   instance parameters covers both families.
4. **Retune a copy to `spawnwater` / `bradbury_blood`.** Fog end 10–64,
   red absorption, red Color Scale, high opacity. Confirm it reads as a
   bad pool, not a ruby swimming pool.
5. **`M_UnderWater_PostProcess_Volume`**, camera dropped through the plane.
   Match the same fog numbers. Decide if an underside mesh is needed at
   all (for the current exported maps the player may never go under).
6. **`SimpleSpriteBurst` splash** on the plane, using the compiled
   `Splash` / `WaterBigSplash` rates as a starting point. This unlocks
   enter-water and bullet-water.
7. **Optional:** `M_Caustics_LightFunction_Sun` on one point light over the
   pool from step 3. Keep or drop as a family, not per basin.
8. **Look, do not adopt:** `WaterTestMap` and `Grid2D_FLIP_Splash`, so the
   Water Body / Fluids option is a memory, not a mystery.

After those eight, the representation is chosen. Remaining work is an
`M_Water` master, binding the `.water` sidecar, and a contents query into
`SetWaterLevel` — implementation, not research.

A first binding, when that work is called:

```
Absorption  ~=  1 / max($fogend * 0.0254, ε)   tinted by $fogcolor
ColorScale  =  $refracttint
Specular    *= $reflecttint
Normal      =  panner(dev_water_normal, 0.05, 45°) + panner(..., −0.03, −20°)
Opacity     =  saturate(c0 + c1 * (1 / $fogend))
```

Tune `c0`/`c1` against the four families in `docs/vtmb/water.md`, not
against a generic ocean screenshot.

---

## 7. Cost and engine facts

The render path is already Lumen + MegaLights + VSM at 1440p
(`docs/architecture/rendering-perf.md`). Water spends the same
translucency / lighting budget.

**Settled Unreal facts (Epic's docs and this 5.8's shaders):**

- Lumen reflections on Single Layer Water are **forced mirror** (roughness
  does not blur them). `LumenReflectionCommon.ush` returns roughness 0
  when the reflection pass is SLW. Acceptable for VtMB: the 2004
  reflection is a perturbed RT, also sharp. Specular input still scales
  brightness. Ripple comes from **normals**, not roughness.
- **MegaLights does not light water.** Epic's MegaLights limitations list
  Water (with clouds and volumetrics) as unsupported. Local neon in this
  project is MegaLights-owned, so it will not put a specular lobe on the
  SLW surface. What water *can* see is the **Lumen mirror of the already-lit
  world** — which is the 2004 shape (a reflection of the scene, not a
  many-light BRDF). Do not turn MegaLights off on a street of lights "so
  the water can see them." If a sewer is a black hole, the Lumen surface
  cache is missing the neon (`docs/architecture/rendering-perf.md`), not
  MegaLights.
- SLW has **one depth layer**. No backface, no stacked water. Two basins
  that overlap in screen space will fight; VtMB's authored planes do not.
- SLW refraction reads scene colour and depth *after* the deferred light
  pass. What is behind the water is the lit world, which is what we want.
  **Color Scale Behind Water is an above-water multiply** (Epic); it does
  not change how the floor looks once the camera is under the plane.
  Underwater is the post-process from §5.2.
- Nanite rejects SLW. Water stays on the non-Nanite bake bucket.
- On low-end / mobile Epic falls back to a simple translucent with no
  volume integral. This project does not ship that path (DX12/SM6 is
  mandatory).
- The Water plugin's tile mesh, LOD rings and `r.Water.WaterMesh.*` vars
  do not apply to a static-mesh SLW. The cvars that *do* are
  `r.Water.SingleLayer.Reflection` (default 1 = same as the scene, i.e.
  Lumen), `r.Water.SingleLayer.RefractionDownsampleFactor` (2 = cheaper,
  blurrier under-surface), and `r.Water.SingleLayer.DepthPrepass`
  (default 1, leave on for VSM).

**Risks to verify on this install, in the editor, before calling the look
done:**

- **Does the Lumen mirror of neon actually show up** on the plane from
  step 1, next to a captured `sm_hub_1` sewer? If yes, MegaLights-not-
  lighting-water is a non-issue. If the surface is black, chase Lumen
  cards on the neon, not a lighting-path change.
- **Forced-mirror Lumen** reflecting the 2D sky backdrop and missing the
  3D-skybox miniature. Same constraint as every other reflection
  (`docs/vtmb/reflections.md`). Canals mostly see architecture and neons,
  which Lumen has.
- **Underwater post cost.** One volume, one material. Do not run it when
  the camera is dry.
- **VSM hard shadows on SLW.** Filtering is off by default
  (`r.Water.SingleLayer.ShadersSupportVSMFiltering 0`). Indoor maps are
  local-light dominated; ignore until a sunlit pool exists.

Budget instinct: one SLW mesh per water material per map (usually one or
two), sprite drips already counted under weather, Fluids only if a single
hero splash is on screen. A hub with a canal is fine. A hub with a canal
and a 3D FLIP pool and a Water Body Ocean is not.

---

## 8. Related docs

- `docs/vtmb/water.md` — the inventory this mapping covers.
- `docs/vtmb/source_movement.md` — `WaterMove` / water level.
- `docs/vtmb/weather.md` — rain and `WaterDrops_Timer`.
- `docs/vtmb/effects.md` — splash / drip / spray particles.
- `docs/vtmb/reflections.md` — `$envmap` on the LMG water-look materials.
- `docs/vtmb/surface_properties.md` — `water` physical / audio row.
- `docs/architecture/effects-architecture.md` — the same seam for fire,
  steam, blood.
- `docs/architecture/rendering-perf.md` — the budget.
- `docs/project/reconstruction-direction.md` — presentation may modernize; logic
  reproduces.
