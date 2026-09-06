# Water — Unreal reproduction

VtMB's water inventory, shaders, volumes and events are `docs/vtmb/water.md`. This document is
the Unreal side: the design as it stands, the seam between reproduced placement and modern look,
and the engine facts (this 5.8 install, verified in source) it rests on. Every named
modernization and divergence is listed once, in §12, beside the faithful behaviour and the owner
call that made it.

The goal is not a port of `_rt_WaterReflection` cameras or the 2004 DUDV perturb. The goal is that
a player who remembers the Santa Monica sewer, the downtown canal, a warrens basin or the Vesuvius
pool still *reads* those places — murky, reflective, a little wrong — built with Unreal's own water
shading, and that a body that walks into one of them is in water.

Status lives only in `docs/project/roadmap.md` (task **7.3 Water**; the migration log calls it
**R7.1**). The pipeline contracts are `seam_map_material.md` → `M_V2_Water` (the master's
parameter tables) and `seam_map_map.md` → "Import — water volumes and water faces" (the manifest
product and the actor's field contract); this file explains the design those contracts implement.
Player water **movement** (`WaterMove`, the swim/tread intent, the camera water band in play) is
out of scope by owner call and stays on the substrate tier until a `run play` witness spends
`sp_soc_3`'s basin.

---

## 1. The ruling in one line

**Presentation is modernized. Authored placement, parameters, the volume and the events are
reproduced.** VtMB water is four stacked facts the 2004 engine happens to share a name for; the
design keeps them apart and gives each its own Unreal home.

| Fact | VtMB | Unreal home |
|---|---|---|
| **Surface look** | `Water_Old`'s three passes over two render targets | `M_V2_Water`, a Single Layer Water master, on the chunk faces the bake already places (§3) |
| **Volume** | `%compilewater` brushes → `CONTENTS_WATER` leaves + `LEAFWATERDATA` | `water.volumes[]` → one `AElysiumWaterVolumes` actor per map (§4) |
| **Underside** | every down-facing water face, reflection stripped at load | the same faces as their own `#underside` sections, bound to an `MI_<unit>_Underside` twin (§3.4) |
| **Body in the volume** | `CheckWater` → `m_nWaterLevel` → `WaterMove` | `AElysiumMapActor::UpdatePlayerWater` → `SetWaterLevel` before the move (§5) |
| **Eye under the plane** | the eye leaf's `leafWaterDataID`, one linear fog over the scene | the actor as an `IInterface_PostProcessVolume` driving `M_ElysiumUnderwater` (§6) |
| **Camera** | `GetWaterOffset`, `cl_waterdist` | `ElysiumCam::SolveWaterOffset` (§7) |
| **Events** | the client's splash on the level transition, the step clock, vphysics buoyancy | `ElysiumWater::DecideSplash`, `UpdatePlayerWaterFootsteps`, `TickFluidBodies` (§8) |
| **Lightstyle on the waterline** | one lightmap page per style slot | a per-primitive brightness on CPD slot 6 (§9) |
| Dressing | drips, mist, `surfaceprop water` | R7.3's effect families; `PM_water` / `PM_wade` physical materials |

Three maps carry a real water body today: `sm_hub_1` (the sewer, plane −14937.74 cm, 50.8 cm
deep), `sm_pier_1` (the ocean off the boards, −1582.42 cm, 83.8 cm deep, drawn by the
`blackwater` card) and `sp_soc_3` (the Society of Leopold basin, −609.6 cm, 1178.56 cm deep —
the only converted volume deeper than the pawn's 92.45 cm half-height, so the only one where
`Waist` / `Eyes` are reachable).

---

## 2. Which units are water, and what draws

**`%compilewater` — the compiler key, not the shader name — selects the master.** VBSP is what
makes a brush a water brush, so `importers.materials.effective_family` reroutes any unit that
authors it onto `M_V2_Water` whatever its declared shader: `water/invisible_water` (from Unlit),
`water/cheap_water` (from LitTranslucent), `dev/dev_waterbeneath` (the `WaterSurfaceBottom` name
that never resolved a master) and, through its parent chain, `maps/sm_pier_1/water/
invisible_water_depth_33`. 27 instances parent directly to `M_V2_Water`. Two consequences are
recorded per unit rather than dropped silently: a `tools/` `$basetexture` on such a unit is a
compiler annotation, not a colour (`toolTextureOnWaterSurface`, `UseBaseTexture` lands off), and
`$translucent 1` is consumed by the water lane (`waterBlendConsumedByTheWaterLane`; SLW compiles
opaque or masked only). `water/cheap_water`'s `$envmapmask` has no destination
(`compileWaterRerouteProvenanceOnly`) and was already dead in 2004: the unit authors
`$forcecheap`, and the cheap program has no mask term.

**A face draws iff it is `%compilewater` and not `%compilenodraw`**
(`map_geometry.compile_water_predicate`, tested on the face's own texdata key so a patched unit is
caught like its base). A drawn water face is never dropped for `SURF_NODRAW`; a `%compilenodraw`
water face is dropped like any nodraw face, as in VtMB. So `sm_pier_1`'s 50 `invisible_water`
faces mesh nothing — the ocean is the `water/blackwater` LMG card, on `M_V2_Lit`'s fixed-cube path
with its animated normal — while the brush is still staged as a volume with its fog, body state,
splash and sounds. The drawn-sheet alternative (owner decision 2, 2026-09-04) was withdrawn the
next day against the owner's own VtMB frames: a Single Layer Water sheet over the nodraw volume
hid the card and shaded as a bare lit plane.

| Authored fact | Master | Volume? |
|---|---|---|
| `Water` shader (22 units: `sewer_water`, `warrwater`, `warrenwater*`, `pool_water`, `dev_water2*`, `spawnwater`, `bradbury_blood`, `mazewater`, `dev_waterbeneath2`, …) | `M_V2_Water` | yes where placed |
| `%compilewater` on another shader (`cheap_water`, `invisible_water`, `dev_waterbeneath`, the `_depth_33` patch) | `M_V2_Water` | yes; `cheap_water` draws, `invisible_water` does not |
| LMG water-look without `%compilewater` (`blackwater`, `bloody_water`, `warrenwater2b`) | `M_V2_Lit` / `M_V2_LitTranslucent` + envmap | no (`la_bradbury_3`'s row is a shadow-brush artefact and is dropped, named) |
| `sm_pier_1`'s 34 `objects/surf` foam cards | `M_V2_LitTranslucent` with the `SineUVTranslate` slide (§3.6) and style 1 (§9) | no |

---

## 3. The surface: `M_V2_Water` as Single Layer Water

### 3.1 Flags

`MD_Surface`, `BLEND_Opaque`, `MSM_SingleLayerWater`, one-sided, `used_with_instanced_static_
meshes` on, `used_with_nanite` **off** (Nanite rejects the shading model; the water faces already
live in the non-Nanite `T_` chunk bucket). No `bUsedWithWater`: the shading model does not need it.
`make_water` sets the shading model *after* `_build_water` returns, because a fresh `UMaterial`
recompiles on every write and SLW is invalid until its output node exists with all four pins
wired. Generated at `GRAPH_VERSION` 12 with the other V2 masters; any graph change bumps that
constant or the recipe stamp silently no-ops the rebuild.

### 3.2 The program it transcribes

`Water_Old` (`docs/vtmb/water.md` → "The live program, transcribed") is three passes with no
diffuse term: the refracted scene below the plane, water-fogged along the through-water distance
and warped by the DUDV; the planar mirror, warped the same way and blended by a Schlick Fresnel;
and a cheap overlay `$fogcolor + cube × fresnel`, blended in by distance, which is the whole draw
under `$forcecheap`. `M_V2_Water` maps each pass onto the one thing SLW offers for it. The pin
table below is the design; `_build_water`'s docstring in `pipeline/unreal/make_v2_materials.py`
is the same table beside the code, and `WaterParams`' docstring names every parameter that stays
declared-not-wired with the VtMB fact behind it.

| `Water_Old` | SLW / material input | Graph |
|---|---|---|
| refract pass, water-fogged by `$fogcolor` over `$fogstart..$fogend` (inches) | **Absorption / Scattering** (1/cm) | `range = max((FogEnd − FogStart) × 2.54, 1)`; `σ = WaterFogScale / range`; `c = pow(FogColor.rgb, 2.2)` (the same decode `ElysiumFog::DecodeColor` gives the scene fog); **Scattering = c × σ**, **Absorption = (1 − c) × σ**. Both 0 under `UseFogEnable` off (`$fogenable 0` is `FogMode(0)`) and under `CheapWater` (no refraction pass). The underside keeps the surface's coefficients |
| refract warp `dudv × $refractamount` | **Refraction** (`RM_2D_OFFSET`) | `dudv(frame) × RefractAmount × WaterWarpScale`, gated `UseAnimatedDuDvFrames` — the same screen-space warp, which SLW's base pass applies through `ComputeBufferUVDistortion` scaled by `saturate(thickness_cm / 30)` |
| `$refracttint` | **Color Scale Behind Water** | `RefractTint`; SLW fades it in over the first 50 cm of depth |
| — | **PhaseG** | 0. VtMB has no phase term |
| `$normalmap` (29 frames at the `animatedtexture` rate) + `texturescroll` | **Normal** | `_flipbook_sample(NormalMapFrames)` gated `UseNormalMap`, over the `BumpScrollRateU/V` panner (the scroll runs — divergence 4), then tilted by the reflection warp and renormalised |
| reflect warp `dudv × $reflectamount` | **Normal**, tilt | `normalize(N + dudv × ReflectAmount × WaterReflectWarpScale)`: the mirror image's warp as a reflection-direction tilt, because there is no reflection image to displace (modernization 3) |
| reflect pass, `× $reflecttint`, alpha `(1 − N·V)^5` | **Specular**, **Roughness** | `Specular = class_specular × luma(ReflectTint)` (luma only: SLW's Specular is scalar); **Roughness 0.05** (a planar mirror render). Lumen supplies the mirror through SLW's own Schlick. Under `Underside`: Specular 0, Roughness 1 |
| cheap pass `$fogcolor + cube(reflect(V, N)) × fresnel`, `alpha = saturate((dist − start) / (end − start))` | **Emissive** | `(fog_decoded + cube(reflect(V, N)) × EnvMapTint × Fresnel(BaseReflectFract, ^5)) × blend`, `blend` the distance blend with `CheapWaterStartDistance / EndDistance` (1 under `CheapWater`); the cube gated `UseFixedCube`, which the stage sets on every water instance (§3.3). `lerp(cube, cube × cube, EnvMapContrast)` applies on the Lit pair's fixed-cube path, not here |
| cheap overlay alpha | **Opacity** (coverage: `WaterVisibility = 1 − Opacity`) | `saturate(textured coverage + blend)`; past `EndDistance` nothing refracts through, as under the 2004 SRC_ALPHA overlay; a `$forcecheap` unit never refracts. `textured coverage = UseBaseTexture ? Alpha × BaseTexture.a : 0` |
| no diffuse term | **Base Color** | **black** unless a base texture is bound (four corpus units, none placed on a water map): `lerp(base × Color × RefractTint, WaterColor, WaterMurkiness)`. A white base lit by the scene was the pier's tan sheet and the basin's blue |
| a face lightstyle | **Base Color** and **Emissive** scale | `LightStyleBrightness`, CPD slot 6, default 1.0, written each tick by the light rig (§9) |
| `$fogcolor` display bytes | — | emitted as `pow(bytes / 255, 2.2)`; the filmic toe crushes it on screen (the basin measured (5, 4, 0) against VtMB's (22, 20, 10)) — one named calibration for the whole render, not a per-term fudge |

**Why 2·ln 2 for `WaterFogScale`.** VtMB's fog is linear, fully `$fogcolor` at `$fogend`; SLW's is
exponential. No σ makes the curves coincide; matching the half-fog distance
(`e^(−σ·end/2) = ½`) keeps how far into a canal you can see, while the far end stays a little more
transparent than 2004's clamp. What the six authored fog tuples become at that scale:

| Material | `$fogcolor` | `$fogend` (in) | σ (1/cm) | Reads as |
|---|---|---:|---:|---|
| `sewer_water` | `{5 5 0}` | 1024 | 5.3e-4 | black-green murk, nearly pure absorption |
| `warrwater` | `{.2 .4 .2}` | 1024 | 5.3e-4 | pure absorption |
| `warrenwater` | `{.4 .4 .2}` | 128 | 4.3e-3 | short, black |
| `pool_water` | `{21 39 20}` | 800 | 6.8e-4 | a green basin you can see into |
| `dev_water2*`, `dev_waterbeneath2`, `cheap_water`, `invisible_water` | `{22 20 10}` | 400 | 1.4e-3 | warm grey |
| `spawnwater` | `{5 0 0}` | 64 | 8.7e-3 | red-black, gone in a metre and a half |

**Declared, not wired**, and why (the same table in `WaterParams`' docstring): the wave block
(`WaterBaseFactor`, `WaterBaseMovementDist/Freq`, `WaterTimeFreq1/2`, `WaterWaveHeight/Length`,
`WaterSpecularMin/Max`) — VtMB's shipped water vertex program never moves a vertex, so there is
nothing to translate; `WaterDepth` — VBSP's per-instance hint, the volume carries the real depth;
`UseEnvMap` — `$envmap env_cubemap` binds no texture, the fixed-cube branch is the wired one; the
plain 2D `DuDvMap` slot — every corpus DUDV is a 29-frame array, so `DuDvMapFrames` is the bound
lane.

### 3.3 What the stage binds

- **Both flipbooks, from their own textures.** `NormalMapFrames` comes from `$normalmap`
  (`TA_water_normal`, one asset, no `_linear` twin since the VTF `NORMAL` bit became the
  role-conflict tie-breaker), `DuDvMapFrames` from `$bumpmap` (`TA_water_dudv`, signed UVWQ
  preserved byte-exact, `TC_VECTOR_DISPLACEMENTMAP`); the `animatedtexture` proxy supplies one
  rate for both (`$bumpframe` indexes both in `Water_Old`), and a bound frames array sets
  `UseNormalMap`. Before this every water instance shipped with a flat normal, on both lanes.
  The master's default DUDV array is the generated `T_V2_DefaultDuDvFrames` (2 slices,
  `(128, 128, 255, 255)`, sRGB off): the default normal-frames asset is `TC_NORMALMAP` where this
  lane samples `SAMPLERTYPE_LINEAR_COLOR`, and either mismatch is a real compile error.
- **A cube on every water instance.** The authored `$envmap`, else the map's VBSP-patched probe
  (the back-fill in `stage_materials`, LMG water fakes included), else `engine/defaultcubemap`
  (`materials._water_default_cube`). Only `$forceexpensive`, authored by no unit, leaves it
  unbound. The `env_cubemap → Lumen` rule does not apply to water: the probe is the authored
  reflection of the cheap pass.
- **`BaseTexture` is not a required slot** (`REQUIRED_TEXTURE_SLOTS["M_V2_Water"]` is empty). The
  surface's identity is the volume, the reflection and the refraction, none of which read it, so a
  unit whose `$basetexture` cannot bind stages with `UseBaseTexture` off and draws as water —
  which is how `dev/ocean` / `dev/oceanbeneath` (a 29-frame VTF in the base slot) stage at all.
- **The switches**: `CheapWater` (`$forcecheap`), `UseFogEnable`, `UseBaseTexture`,
  `UseAnimatedNormalFrames`, `UseNormalMap`, `UseAnimatedDuDvFrames`, `UseFixedCube`, and
  `Underside` — the last set only on the twin (§3.4), never on a surface instance.
- **`SineUVTranslate`** is not on this master; it is the Lit pair's (§3.6).

### 3.4 The underside is a face fact

`Mod_LoadFaces` decides "underside" per face on the face's own plane normal and answers by
mutating the *shared* material, which is load-order dependent and cannot be copied as a material
rule. So:

- **The face carries it.** `map_geometry.face_underside` reads the face's plane row
  (`normal.z < 0` in Unreal space) and `section_key` emits a `#underside` group, gated to
  `%compilewater` faces only (every ceiling in the game has a downward normal). The `side` bit is
  deliberately not consulted: every down-facing water face in the corpus carries `side 1`.
- **The twin is an instance, not a switch on the surface.** Every `M_V2_Water` instance,
  patched ones included, stages `MI_<unit>_Underside` in the same folder, parented to the surface
  instance and stating exactly `{"Underside": True}` (108 twins corpus-wide,
  `materials.UNDERSIDE_INSTANCE_SUFFIX`). The bake binds the `#underside` section to it
  (`_V2Material.slot_asset`), and `bake_verify._verify_water_section_bindings` catches the defect
  an existence check misses (twin imported, section still bound to the surface). `$bottommaterial`
  stays published as a resolved material reference and drives nothing.
- **The twin drops reflection and keeps extinction**: Specular 0, Roughness 1 (a mirror Lumen
  still resolved off a smooth surface would put back exactly what the engine strips); the
  coefficients stay the surface's, because the underside is the same body of water seen from its
  other side. 5.8's SLW camera-under-water branch is dead code (§11), so an underside face
  integrates the volume as if the eye were above it — which is why the view from inside is a
  post-process (§6) and why the two are witnessed together.

### 3.5 `M_ElysiumUnderwater`

A second asset the same generator authors: `MD_PostProcess`, before tonemapping, three parameters
(`FogColor`, `FogStart`, `FogInvRange` — the triple `ElysiumFog::ApplyToDecalMID` already writes,
so one packer serves the scene fog, the decals and the underwater view):
`lerp(SceneTexture:PostProcessInput0, FogColor, saturate((SceneDepth − FogStart) × FogInvRange))`.
The runtime holds one MID (§6).

### 3.6 The pier's foam cards: `SineUVTranslate` on the Lit pair

`sm_pier_1`'s "wave system" is 17 `objects/surf` `func_illusionary` cards on
`M_V2_LitTranslucent`, whose authored breathing is an alpha pulse (the `sine` lane) and a UV slide:
a `sine` proxy writes a `$temp*` scratch var and a `texturetransform` consumes it as
`translatevar`. The Lit pair gains a vector `SineUVTranslate` `(ampU, ampV, offU, offV)`, default
`(0,0,0,0)`; the stage resolves the chain in two passes (record the `$temp*` sines, then match each
`$basetexturetransform` transform whose `translatevar` names one, order-independently), and the
graph feeds it to the **base**-texture panner only, ahead of the bump panner. Unconsumed sines,
`rotatevar` targets and period mismatches keep their named omissions; masters that do not expose
the vector drop it, named. The pier stages `SineUVTranslate = [0.5, 0, 0, 0]` on the same 15 s
period the alpha pulse uses.

---

## 4. The volume: `water.volumes[]` and `AElysiumWaterVolumes`

### 4.1 The product

`resolve_water_volumes` (`importers/map_geometry.py`) publishes one row per real `LEAFWATERDATA`
record, in lump order, into the map manifest (`MANIFEST_VERSION` 11, pinned equal between the
stage and `bake_map_v2.py`). The full field table is `seam_map_map.md` → "Import — water volumes
and water faces"; the shape:

```
"water": {
  "volumes": [ { "index", "surfaceZCm", "minZCm", "material",
                 "fogEnable", "fogColor", "fogStartCm", "fogEndCm",   // off the VMT provenance, through patchBase
                 "brushes":   [ { "planes": [[nx,ny,nz,d], …], "boundsCm": {min,max} } ],
                 "fluid":     { "index", "density", "damping", "surfacePlane", "currentVelocityCm", "contents", "surfaceProp" } | null,
                 "pieces":    [ … same row shape as brushes … ],      // the compiler's convex carve
                 "leafBoxesCm": [ {min,max} ],                         // the LEAFWATERDATA leaves
                 "nearBoxesCm": [ {min,max} ],                         // ⋃PVS(water clusters)
                 "materialTableWaterIndex": 17 | null } ],
  "faces":   [ { "index", "scene", "group", "unit", "underside", "lightStyle", "lightStyles",
                 "surfaceFogVolumeID", "texInfo", "texdata", "plane", "side", "normal",
                 "primitive": {first, count}, "area", "areaCm2", "meshedAreaCm2", "triangles" } ],
  "dropped": [ {"index", "reason": "sentinel" | "no water brush"} ]
}
```

The rules that matter:

- **Rows.** The `16384` sentinel rows are dropped and named; a row no `%compilewater` brush stands
  at (`la_bradbury_3`'s `tools_shadow` caster) is dropped and named.
- **Brushes.** `collision.brushes[]` with `contents & 0x20` **and** a non-bevel side whose material
  authors `%compilewater`; a `0x18000120` all-`tools_shadow` caster never qualifies, a `0x18000020`
  `func_detail` brush does. A brush joins the row whose `surfaceZ` its horizontal top plane matches
  within one inch — no leaf join, so `ch_fulab_1`'s `func_illusionary` water (no leaf points at its
  row) still lands. Planes are outward normals in Unreal cm; bevel sides skipped; the AABB comes
  from the same hull solver the collision sidecar uses.
- **Fog keys** are read off the material's own VMT provenance, walking the `patchBase` chain
  base-first (a patched unit's own text is only its delta), never off the staged instance:
  `invisible_water` and `cheap_water` land on masters that stage no fog keys. A material with no
  fog keys stages `fogEnable: false`, `SetFogVolumeState`'s own answer.
- **`fluid`** is staged because VtMB's creation guard is `fluid.index > 0` alone; unauthored
  `density` / `damping` / `contents` stay `null`, never a substituted default.
- **`pieces`** carry the same row shape as `brushes` so one struct serves both; the runtime tests
  them first.
- **`nearBoxesCm`** is derived from the visibility sub-unit (`importers/map_visibility.py`,
  `pvs_union`), never read off the leaf `0x200` bit — measured set-equal to VtMB's own annotation
  where it survives, and the only answer on the UP pier, where it does not. Solid leaves excluded.
- **`faces[]`** exists so the underside split, the lightstyle split and the area pin are answerable
  off one product. `meshedAreaCm2` against VBSP's `area` is the corpus test's pin, per face and per
  section (worst face 3e-5 relative, every section 1e-7).

Measured at `MANIFEST_VERSION` 11: `sm_hub_1` 47 water faces / 24 underside / 1 volume / 2 bound
water sections / 1 style; `sm_pier_1` 50 / 18 / 1 / 3 / 2 styles over 211 styled groups; `sp_soc_3`
62 / 27 / 1 / 3 / 2 styles. A materially different count on re-run is a regression, not a new
baseline.

### 4.2 The actor

One `AElysiumWaterVolumes` per map (`Public/ElysiumWaterVolumes.h`), bake-placed by
`bake_map_v2._place_water` on every `MapsOnV2Models` map whose manifest carries rows (no rows, no
actor), tagged `elysium.water` (`ElysiumBakedTags::Water`), labelled `WaterVolumes`, folder
`Water`, standing at the first volume's bounds centre (every query reads the struct array, never
the transform). `UElysiumMapVisuals::AdoptBakedLevel` buckets it into `WaterVolumes`;
`bake_verify.verify_water` hard-fails the verify run on a count or plane mismatch.

```
USTRUCT() FElysiumWaterBrush  { TArray<FPlane> Planes; FBox BoundsCm; }
USTRUCT() FElysiumWaterFluid  { bool bHasFluid; int32 Index; float Density; float Damping;
                                FPlane SurfacePlane; FVector CurrentVelocityCm; int32 Contents; }
USTRUCT() FElysiumWaterVolume { int32 Index; float SurfaceZCm, MinZCm; FString Material;
                                bool bFogEnabled; FLinearColor FogColor; float FogStartCm, FogEndCm;
                                TArray<FElysiumWaterBrush> Brushes, Pieces;
                                FElysiumWaterFluid Fluid; TArray<FBox> LeafBoxesCm, NearBoxesCm; }
UCLASS()  AElysiumWaterVolumes : AActor, IInterface_PostProcessVolume
          { TArray<FElysiumWaterVolume> Volumes; FElysiumWaterSplashSignature OnSplash; … }
```

Every field is `EditAnywhere` without `CPF_EditConst`, asserted by `Elysium.Substrate.WaterActor`,
so a `set_editor_property` that cannot land fails a test instead of going quiet
(`WATER_ACTOR_SHAPE` 2 tracks the struct shape).

The queries are plain functions over the rows (`namespace ElysiumWater`, no world, no trace, so
the whole classification is asserted with no map):

- `FindVolumeAt(PointCm)` — `MASK_WATER` at a point: bounds first, then `Pieces` when the volume
  publishes any, else `Brushes`.
- `ClassifyBody(Feet, Waist, Eyes)` — `CheckWater`'s three queries in its order: feet → 1,
  waist → 2, eyes → 3, `None` when the feet are dry; the deciding volume comes back for the camera.
- `FindNearVolumeAt` / `IsNearWater` — over `NearBoxesCm`, degrading to the brush bounds on a level
  baked before the near set existed.
- Editor-only debug draw of the brush AABBs under `elysium.Water.Draw`.

---

## 5. The body

`AElysiumMapActor::UpdatePlayerWater`, from the map actor's tick after the player think and before
the move (`CheckWater`'s own ordering):

```
feet  = hull centre − (0, 0, GetBodyHalfHeight()) + 1 in   // origin.z + mins.z + 1
waist = hull centre                                        // the hull midpoint, not the eye
eyes  = the camera component's location                    // origin + viewoffset
Movement->SetWaterLevel(Water->ClassifyBody(feet, waist, eyes))
Camera->SetWaterState(level, the deciding volume's SurfaceZCm)
```

`FullWalkMove` already branches on `WaterLevel >= Waist` (skips gravity, `WaterMove`), the anim
intent already answers `Swim` / `Treadwater`, and `CheckJumpButton` refuses at level ≥ 2; this is
their first caller. No save state: the level is re-derived on the first tick. `CONTENTS_CURRENT_*`
is authored nowhere and `fluid.currentVelocityCm` is a query only, so the movement lane can read
it when it lands. The same tick publishes `IsPlayerNearWater()` off the near set.

On the two shallow maps `Waist` / `Eyes` are unreachable by the numbers (every pose whose waist is
inside the band puts the feet below `minZ`); `sp_soc_3` is the first volume past that bound, and
its swim path is the witness the owner left unspent.

---

## 6. The eye: underwater as a post-process

VtMB under the plane is two passes and one linear fog over everything (`docs/vtmb/water.md` → "The
eye under the plane"). Here:

- `AElysiumWaterVolumes` implements `IInterface_PostProcessVolume` (`bIsUnbound = false`,
  `BlendWeight = 1`, `Priority = 1` above the map's neutral unbound `elysium.ppv` at 0, a fixed
  `VolumeGuid`), registered with `UWorld::AddPostProcessVolume` on `BeginPlay`.
- **The gate is `bIsEnabled`, not `EncompassesPoint`'s return.** `UWorld::DoPostProcessVolume`
  discards a bounded volume's returned bool and blends on the distance it writes
  (`World.cpp:10710-10741`), so `EncompassesPoint` always writes distance 0 (full weight, no soft
  edge — a partial blend of a fog lerp reads as haze, not water) and returns `Properties.bIsEnabled`,
  which `UpdateViewPostProcess` sets per view from `FindVolumeAt(ViewLocation)` inside the
  `OnBeginPostProcessSettings` handler, broadcast before the engine's volume walk. That is the
  Water plugin's own arrangement (`UUnderwaterPostProcessVolume`), the only shipped precedent for a
  volume whose shape the engine cannot evaluate; neither needs the plugin.
- The same handler writes the found volume's fog triple (decoded by `ElysiumFog::Pack`) to the one
  `UnderwaterMID` (created lazily from `/Game/ElysiumGenerated/Materials/V2/M_ElysiumUnderwater`,
  added once via `AddBlendable`; a checkout without the masters logs a warning and still classifies
  the body), and sets `FSceneView::UnderwaterDepth` (`SurfaceZCm − ViewLocation.Z`, else `-1`) and
  `WaterIntersection` (`InsideWater` / `OutsideWater`) — the two writes
  `UWaterSubsystem::ComputeUnderwaterPostProcess` makes, for pass ordering only.
- **Why a post-process when the project's fog is per-primitive.** The distance-fog rule ("not an
  engine fog") exists because the world and the 3D-skybox miniature share screen depth. Under the
  plane that distinction is moot: everything above the water is seen *through the surface*, SLW
  writes the surface's depth, so a scene-depth fog fogs the whole above-water view at the plane's
  distance — exactly what VtMB's pass 2 does to the surface — and below-water geometry fogs at its
  true depth. What the post-process cannot do is *replace* the world fog below the plane (it adds;
  divergence 8).
- **Selection** is point-in-brush of the view location. VtMB's is point-location to the eye leaf;
  a view point is in at most one volume either way.
- The engine merges every blendable of one material into a single node whose parameters follow the
  highest-priority volume, so a test override pushed through the map's priority-0 volume is
  silently replaced under the plane; push diagnostics at priority 2.

Witnessed on `sp_soc_3` (the first deep basin): with `r.PostProcessing.DisableMaterials` A/B in one
pose, the below-water rock reads (0, 0, 0) off and a monotone depth ramp toward the fog colour on;
the above-water rock seen through the plane fogs at the slant distance to the plane (774 cm
solved against ~743 cm geometric), confirming the underside-writes-depth reading numerically. On
the two 50–84 cm canals the volume fog is faithful and invisible by the authored numbers; it
becomes a *look* only on a deep basin.

---

## 7. The camera

`GetWaterOffset` (`cl_waterdist` 4 in) raises the view out of the volume at level 2 and lowers it
back in at level 3, in one-unit steps. `ElysiumCam::SolveWaterOffset(WaterLevel, ViewZ, SurfaceZ,
WaterDistCm)` (`Public/ElysiumCameraSolve.h`) is the closed form: level 2 clamps
`SurfaceZ + WaterDist − ViewZ` to `[0, WaterDist]`, level 3 clamps `SurfaceZ − WaterDist − ViewZ`
to `[−WaterDist, 0]`, everything else 0; under the `FElysiumCameraCvars::WaterDist` cvar (default
`4 × U`). `UElysiumCameraComponent::ApplyBaseToView` applies it as a Z-only nudge **after** the
boom-blend block, because the case it exists for — a treading or swimming body — is first person,
where that block's weight is 0. The level and plane are pushed by `UpdatePlayerWater` through
`SetWaterState`, because the camera reaches only its owner and the volumes belong to the map actor.
The 1-unit quantization is dropped (divergence 12).

---

## 8. The events water raises

**Splash, on the water-level transition** (modernization 5). VtMB's live splash is
`client.dll FUN_10099630`, reached from `DrawModel`; the port has no draw hook and does not need
one — the transition is the event. `ElysiumWater::DecideSplash` is the pure rule (big on
`0 → ≥ 1` with `velocity.z < −200 in/s`; wade while `0 < level < 3` at ≥ 50 in/s horizontal on the
`5.0 − horiz · 7.8e-5` s cooldown; spawn at `origin − vel.xy · 0.035` snapped to the plane,
`+RandomInt(0, 8)` in z for the wade one, from a named RNG stream; one splash per entity per half
second on top). `AElysiumMapActor::RaiseWaterSplash` plays `NS_waterbigsplash_emitter` /
`NS_watersplash_emitter` through the effect actor's by-root API; until the R7.3 generator has run
for those roots it stands the floor system rather than failing, by design. The fluid controller's
own stripped `PhysicsSplash` numbers are deliberately not reproduced. Stated side effect: the
splash fires for a first-person player VtMB may never have drawn.

**Sounds, by classified level and by pool.** `AElysiumMapActor::UpdatePlayerWaterFootsteps` runs
VtMB's own millisecond step clock at the tail of `UpdatePlayerWater` (400 / 300 / 600 ms plus the
bias) and calls `PlayPlayerWaterFootstep(StepIndex, bRightFoot)`: `Surfaces/Water/Step*` on every
level-1 step, `Surfaces/Wade/Step*` at level ≥ 2 on the four-phase counter whose phase 0 is silent.
The pool is keyed off the level, never the surface material under the foot (the pier's foam cards
bind `PM_default`). `ElysiumWaterAudio` resolves through the baked `PM_water` / `PM_wade`
surfaceprop assets and `AElysiumMapActor::PlayVoice`. When a real locomotion step producer lands it
should call `PlayPlayerWaterFootstep` directly and this clock retires. Two honest gaps:
`water.Impact` / `water.Scrape` are sound-script names and this runtime has no script table, so the
pool is taken from the surfaceprop's folder until the sound-script lane lands (`Scrape` is a physics
friction event owned by the whole-game impact lane and has no caller yet); and leaving the water is
silent because `player/pl_wade2.wav` is not in the game (divergence 22).

**Buoyancy** (modernization 6). vphysics' fluid controller is not in the corpus; what is authored
is the fluid row's `density` and `damping`, so `AElysiumWaterVolumes::TickFluidBodies` spends them
on Archimedes over the body's AABB (`SubmergedFraction` / `DisplacedVolumeM3` / `BuoyantForceZ`)
and on the body's linear damping, over the simulating components a per-volume query box
(`ECC_PhysicsBody`) reports, with the exact carve test on those few. Nothing displaces the
surface; VtMB never did either.

**I/O, fog and wetness** are the entity lane's: the hub's `Sewer Scheme` outputs and the pier's
`logic_auto → FadeGlobalWetness(1)` are staged outputs the runtime fires generically
(`FElysiumWeatherState` writes `MPC_ElysiumEnvironment.GlobalWetness` each tick). No *water*
material reads wetness, deliberately — that is roadmap 7.9's contract for the ground beside water.

---

## 9. Lightstyles as a per-primitive brightness (owner decision 4)

VtMB animates a styled face by swapping which lightmap page it samples; Lumen replaced lightmaps
project-wide, so there is no page to swap. The port keeps the authored motion and spends it as
brightness, in one lane across four files:

| Where | What |
|---|---|
| `importers/map_geometry.py` | `face_light_style` picks the face's style; `section_key(key, light_style=n)` emits a `#style<n>` group, so a styled face is its own `(material, style)` chunk |
| `pipeline/unreal/bake_map.py` | `chunk_style_suffix` / `parse_chunk_style` name the chunk; `chunk_actor_tags` tags it `elysium.style=<n>`; `set_fog` always stamps CPD slot 6 = 1.0 beside the six fog floats |
| `Source/ElysiumUE` | `ElysiumLightStyle::SlotBrightness = 6` (`Public/ElysiumFog.h`, beside the fog slots 0–5), `UElysiumLightRig::AdoptStyledPrimitives` rewriting the float each tick off the same `StyleTime` / `StylePatterns` pair a styled light uses, `StampUnstyled` on every runtime-built primitive |
| `pipeline/unreal/make_v2_materials.py` | `LIGHT_STYLE_CPD_SLOT = 6`, `LightStyleBrightness` multiplying the lit base colour and the emissive on Lit, LitTranslucent, TwoTexture and Water, applied before `_scene_fog` so the haze is not scaled |

Neutral is **1.0**, written, not defaulted: a CPD-driven parameter always reads the slot, an
unwritten slot reads 0, and a brightness of 0 is black. **The lowest style wins**, not the first
slot (Quake's animated patterns are 1–11, a switchable light's is 32–63, and 18 of the pier's 34
foam cards name 32 before 1). Every style a face names still rides `water.faces[].lightStyles`.

**Two carriers, because a brush entity is not a chunk.** A world/sky chunk takes its style off the
actor tag. The pier's 17 foam cards are `func_illusionary` bodies the bake never places, so their
style rides the mesh's own material slot names (`safe_name(<group key>)` ends in `_style<n>`;
`asset_names.brush_slot_style` states the rule, `ElysiumLightStyle::StyleFromSlotNames` reads it
in `UElysiumMapVisuals::RegisterRuntimeBrush`). One primitive carries one slot, so a brush whose
sections disagree animates on none, and `bake_map.py` fails the bake if an unstyled group key ever
folds to the `_style<n>` shape.

The split is map-wide, not water-only (211 styled sections on `sm_pier_1`, 110 on
`sp_tutorial_1`); if a draw-call regression shows, the fix is merging sections sharing
`(material, style)` across scenes, not un-splitting them. A face naming two styles keeps one on its
chunk; a switchable style defaults to full brightness, so nothing is lost until an entity switches
that light.

---

## 10. Knobs

All on the **Surfaces** settings page (`UElysiumSurfaceSettings`), pushed into
`MPC_ElysiumSurfaces`, pinned in `Config/DefaultElysium.ini`. Shipped at their derivations and not
tuned ("wire first, tune later"):

| Knob | Default | What it is |
|---|---|---|
| `WaterFogScale` | 2·ln 2 ≈ 1.386 | σ = scale / range; the half-fog match (§3.2) |
| `WaterWarpScale` | 0.01 per authored unit | the refraction warp; the authored amounts are 15–100 against a DUDV rms of 0.027, and the frames show 1–2 % of the frame |
| `WaterReflectWarpScale` | 0.01 | the reflection-normal tilt, same measurement |
| `FixedCubeStrength` | shared with the Lit pair | the fixed-cube emissive add |
| `cl_waterdist` (`FElysiumCameraCvars::WaterDist`) | 4 in | the camera clearance band |

---

## 11. Engine facts (UE 5.8, verified in this install's source)

- **SLW is engine, not plugin.** `MSM_SingleLayerWater` and
  `MaterialExpressionSingleLayerWaterMaterialOutput` live in `Engine/`; the Water plugin only uses
  them, and its content is mounted only when it is enabled, so the project authors its own
  post-process master.
- **Rules a SLW material must meet** (`MaterialShared.cpp` 6432–6449): opaque or masked, the only
  shading model, the output node present. No two-sided, static-lighting or `bUsedWithWater`
  requirement. **Nanite rejects it** (`NaniteResources.cpp` 3407). Pixel Depth Offset and
  scene-depth reads are unsupported; `SceneDepthWithoutWater` is the node for a wall fade, later.
- **The camera-under-water branch is dead code**: `const bool CameraIsUnderWater = false;`
  (`BasePassPixelShader.usf:1698`, `SingleLayerWaterComposite.usf:68`); no cvar or plugin path
  enables it. What exists is `FSceneView::UnderwaterDepth`, a CPU flag any
  `OnBeginPostProcessSettings` subscriber may set.
- **Opacity is coverage**: `WaterVisibility = 1 − Opacity` (`BasePassPixelShader.usf:1140`);
  Opacity 1 removes the volume.
- **Coefficients are 1/cm** (`SingleLayerWaterShading.ush:200`); Color Scale Behind Water fades in
  over 50 cm (`:176`).
- **Lumen honours roughness on water.** Every SLW pixel traces (`LumenReflectionCommon.ush:335`)
  with the GBuffer roughness (`LumenReflections.usf:317/366/377`); the "forced mirror" wording is
  Epic's doc text for an earlier release. `r.Water.SingleLayer.Reflection` 1 means the scene's
  method (Lumen, else SSR). Planar reflections never feed SLW.
- **MegaLights does not shade water, but water is not dark**: SLW is lit forward through the light
  grid with MegaLights' lights still in it (`BasePassPixelShader.usf:1400/1478`), so the neon puts
  an analytic specular lobe on the canal, unshadowed.
- **Two-sided is allowed at raster level** (`SingleLayerWaterRendering.cpp:2140`) but the shading
  always assumes the camera is above.
- **Depth prepass** (`r.Water.SingleLayer.DepthPrepass`, default 1) requires VSM support; VSM
  *filtering* on water is off by default. Velocity moved to the prepass in 5.8. **No decals** land
  on SLW. Refraction culling is off by default. No `r.Water.*` entry exists in
  `BaseScalability.ini`.
- **`IInterface_PostProcessVolume`** is base engine, three pure virtuals; `UWorld::AddPostProcessVolume`
  is the 5.8 registration (`InsertPostProcessVolume` is deprecated).
- **Python authoring** sets `shading_model`, `blend_mode`, `two_sided` through
  `set_editor_property`; the output node's four inputs are bare `UPROPERTY()`s, wired by pin name
  through `connect_material_expressions`.

---

## 12. Named modernizations and divergences (owner-visible)

Each beside the faithful behaviour; an owner call is named where one was made.

1. **Two render targets → one SLW pass.** No planar camera. Refraction reads the lit scene behind
   the surface, warped by the DUDV in screen space exactly as the refract pass did; reflection is
   Lumen's through SLW's Schlick. The 2004 reflection RT drew entities and the 2D sky lump, never
   a 3D skybox; Lumen sees the placed miniature.
2. **Linear volume fog → exponential extinction**, half-distance matched by `WaterFogScale`.
3. **The reflection warp is a normal tilt**, not an image displacement: there is no reflection
   image to displace. Both warps are scaled by knobs tuned against the owner's frames.
   `mat_waterswirl` (pre-DX9 only) is not reproduced.
4. **The `$bumpoffset` scroll is honoured though the 2004 shader ignored it** (owner decision 3:
   authored intent over 2004 result, because the alternative drops motion the author wrote).
   Recorded at the proxy site in `importers/materials.py` and in `_build_water`.
5. **`$reflecttint` reaches the mirror as luma only.** SLW's Specular is scalar; the red on blood
   and spawn water reaches the reflection as brightness, and the volume colour carries the red.
6. **Fresnel is SLW's own Schlick from Specular** on the expensive path, not `(1 − N·V)^5` with
   R0 = 0; the cheap pass keeps its own Fresnel with `BaseReflectFract`.
7. **Cheap water samples the authored cube** (or the map's probe, or `engine/defaultcubemap`),
   blended by distance over every expensive unit as `Water_Old` does. `$forcecheap` zeroes the
   coefficients, forces Opacity to 1 and emits the whole 2004 lerp — the corpus settled it after
   a first cut multiplied extinction ×16 and read black wherever no light reached the basin: a
   scattering coefficient needs light, and the cheap program emits a constant on a `NOLIGHT` face.
8. **Underwater: the world fog is not suppressed.** VtMB replaces it below the plane; the
   post-process adds the volume's fog on top of the per-primitive scene fog. On the corpus the
   volume fog's range (1.6–26 m) is far inside the world fog's, so the double term is invisible in
   practice; recorded, not compensated.
9. **Fog-volume selection** is point-in-brush of the view location, not a plane-tree descent.
10. **The surface takes no scene fog.** VtMB draws the surfaces under `EnableWorldFog()`; the
    master's `FogStart` / `FogEnd` / `FogColor` names are the VMT volume keys, so the per-primitive
    scene-fog lane cannot share them. The refraction and reflection carry the scene's own fog. A
    far canal end that pops against its walls is a tuning-session witness; the fix, if wanted, is
    an additive fog-in on Emissive.
11. **Opacity as coverage, and an unbindable base texture no longer fails the unit.**
12. **The camera offset's 1-unit quantization is dropped** for the exact closed-form distance
    (owner call: build now, 2026-09-04).
13. **Water faces are lit, with a black base.** VtMB's are `SURF_NOLIGHT`; SLW is lit forward by
    the light grid and Lumen. A black base receives specular and nothing diffuse, the closest a lit
    shading model comes to a `NOLIGHT` face.
14. **The cheap overlay's alpha is coverage**, not a blend of two colours: past
    `$cheapwaterenddistance` nothing shows behind the surface.
15. **`$fogcolor` bytes are emitted as `pow(byte / 255, 2.2)`**; VtMB wrote them to the framebuffer
    unchanged. The witness measures the basin against the owner's frame; the display transform is
    the one calibration if it crushes.
16. **A lightstyle is a brightness, not a lightmap page** (owner decision 4), one style per chunk,
    the lowest winning.
17. **The splash is bound to the water-level transition**, not to a draw call, and the fluid
    controller's stripped `PhysicsSplash` numbers are not reproduced.
18. **Buoyancy is Archimedes over the body's AABB** off the authored `density` / `damping`,
    because vphysics is not in the corpus.
19. **The near-water set is derived from the PVS**, never read off leaf bit `0x200`; measured
    set-equal where the annotation survives.
20. **`WaterMurkiness` stays wired** though no shipped shader registered it (owner call, ruling N):
    authored intent with an obvious meaning, and un-wiring it puts nothing in its place.
21. **`waterbigsplash_emitter` is read from the retail pack member**, because the Unofficial Patch
    copy is an authored-empty stub. One key, one override
    (`exporters.particle_glb.RETAIL_PROVENANCE_DIVERGENCE_UNITS`); every other key stays UP-first.
22. **`sm_pier_1` stays on the UP recompile** (owner decision 1). Both destroyed leaf annotations
    are derived rather than read: `0x200` as the PVS union, `0x800` as the precipitation mask,
    which no rain gate may key on for this map (the UP build flags no leaf, the pier places 10
    `rain_box_emitter` roots).
23. **`water/cheap_water` loses its `$envmapmask`** on the reroute; already dead in 2004 on that
    `$forcecheap` unit.
24. **Leaving the water is silent**, because `player/pl_wade2.wav` is a Half-Life 2 asset Troika
    never packed. The name is kept so an install that carries the file plays what the code names.
25. **`CheapWaterStartDistance` / `EndDistance` no longer swap programs.** 5.8 compiles one shading
    model per material and Lumen LODs its own reflection by distance; the pair now drives the
    cheap-overlay blend only, which is the visible half of what it did.
26. **`sm_pier_1`'s nodraw water draws nothing**, as in VtMB. The 2026-09-04 "surface on nodraw
    water" modernization (owner decision 2) is withdrawn against the owner's frames.
27. **Sky-entity hulls are not composed into world collision** — a port-created divergence
    corrected: three sky `func_brush` hulls used to sit 21 in under the harbour surface with no
    VtMB counterpart.

---

## 13. Cost

One SLW mesh set per water material per map (a top and an underside), in the non-Nanite bucket
the bake already uses, drawn in the SLW pass after deferred lighting. The post-process runs only
while the view is inside a volume. The fluid tick runs only while a body is tracked. No extra
actors per brush, no plugin, no Water Zone, no fluid sim; the Lumen trace on water is the trace
every reflective surface already pays. A hub with a canal is fine; a hub with a canal, a FLIP pool
and a Water Body Ocean is not.

---

## 14. What to witness

Did-it-appear is the only acceptance. Passed on 2026-09-04/05: the sewer surface draws, refracts
the canal floor, mirrors the wall lamps and moves (9.3 % frame-to-frame change over the water
against 0.7 % on a static wall); the body reads `Feet` on the sewer floor and in the pier's ocean
band; the pier shows no sheet above or below the plane, the surf band animates and the foam cards
slide; the basin's underwater fog is applied and correct by depth; the underside from below is the
refracted above-water world with no specular.

Still owed, with the viewpoints in `ELYSIUM_WORK_ROOT/scratch/water_audit/WITNESS_PLAN.md` and
the reference frames beside it:

1. The look pass against the owner's frames: the sewer canal's lantern streaks and cheap-dark far
   end; the pier's `blackwater` card near-black with cube glints; the basin brown in the
   (27, 25, 15) class from above and readable from below.
2. The splash: drop into `sp_soc_3`'s basin fast for the big one, wade the hub canal for the wade
   one; and the negative — a physics prop dropped into the sewer raises no fluid-controller splash.
3. The pier waterline flickers on style 1 (freeze it with the lightstyle clock pin for an A/B).
4. The hub's `Sewer Scheme` outputs fire on entering the water (`elysium_io_history`,
   `elysium_audio_state`), and the pier's `FadeGlobalWetness(1)` reaches the MPC.
5. `TC_VectorDisplacementmap` vs `TC_Normalmap` on the water normal array, on one instance in the
   editor (scan the asset registry synchronously first; texture platform data is null without
   `-AllowCommandletRendering`).
6. The swim path on `sp_soc_3`: `Waist` / `Eyes`, `WaterMove`, the swim intent, `SolveWaterOffset`
   — out of scope by owner call, covered by the substrate tier meanwhile.
7. Later converted maps: `spawnwater` on `hw_warrens_5` reads as a bad pool, not a ruby one;
   `la_dane_1`'s undulating ocean re-checks the dead-vertex-path ruling on a unit with no
   `$normalmap`.

Two traps for the next witness: `elysium.lights` is a toggle with no output (run it twice), and a
post-process override must be pushed at priority 2 to beat the water volume under the plane.

---

## 15. What pins it

At the seam, in the count each change authorizes.

| Layer | File | What it pins |
|---|---|---|
| C++ automation | `Private/Tests/ElysiumWaterTests.cpp` → `Elysium.Substrate.Water` | `FindVolumeAt` / `ClassifyBody` against a fixed brush set (feet/waist/eyes, beside a brush, below `minZ`, sloped planes, two volumes); `DecideSplash`'s two rules and the wade cooldown; `SubmergedFraction` / `DisplacedVolumeM3` / `BuoyantForceZ`; the CPD slot-6 name and number agreeing with the bake and the graph; `ElysiumBakedTags::Water`; `WaterFogScale`'s default and collection binding |
| C++ automation | `Elysium.Substrate.WaterActor` | every struct field reflected and `CPF_Edit` without `CPF_EditConst`; `Pieces` preferred by `FindVolumeAt`; `FindNearVolumeAt`'s degrade; after `BeginPlay` the post-process properties; a simulated `OnBeginPostProcessSettings` call flipping `bIsEnabled` and writing the MID triple |
| C++ automation | `Private/Tests/ElysiumCameraTests.cpp` → `Elysium.Substrate.Camera` | `SolveWaterOffset`'s exact values at levels 2 and 3, 0 at levels 0/1; `WaterDist == 4 × U` |
| C++ automation | `Private/Tests/ElysiumV2MaterialTests.cpp` → `Elysium.Policy.V2MasterParams` | `M_V2_Water`'s compiled graph carries `Underside`; `M_ElysiumUnderwater`'s three bindings |
| pytest | `test_materials_stage.py` | the water-family normal fix; the `_Underside` twin (path fold, parent, exactly one switch); `effective_family` and the reroute's omission kinds; the default cube; `EnvMapContrast`; `authoredThenRemovedKey`; the `SineUVTranslate` chain; the header pin across `ElysiumSurfaceParams.h` / `EXPOSED_PARAMS` / `WaterParams` |
| pytest | `test_make_v2_materials_editor.py` | Opaque + SLW with the four output pins fed; the refraction warp on the Refraction pin and the reflection warp on the normal; `LightStyleBrightness` on CPD 6; the cheap lerp emitting `$fogcolor`; `SineUVTranslate` reaching the base lane only |
| pytest | `test_map_geometry.py` | `resolve_water_volumes` (sentinel drop, the `%compilewater` brush predicate, planes in cm, fog keys through `patchBase`, an unmatched row dropped and named, the corpus rows); `section_key` / `split_section_key`; `face_underside` on the plane row and not the `side` bit; `face_light_style` picking the lowest; `water.faces[]`; the drawn-water predicate; `meshed_area_cm2` per face and per section |
| pytest | `test_bake_map_water.py`, `test_bake_map_lightstyle.py`, `test_bake_verify_water.py` | the actor values; a zero-row manifest placing nothing; the four optional struct fields written only when staged; `WATER_ACTOR_SHAPE` and `MANIFEST_VERSION` pinned to the stage's; the chunk style suffix and its parse-back; `_verify_water_section_bindings` |
| pytest | `test_map_glb.py`, `test_texture_glb.py`, `test_material_glb.py`, `test_particle_glb.py`, `test_effects.py` | the `dface+96` split and `primitives[]`; the mip-chain recovery; `vtf_flag_names` and the no-twin rule; `materialReferences[]`; the single-key retail override; the four water roots resolving with zero unresolved children |
| bake verify | `bake_verify.verify_water` | one `elysium.water` actor iff the manifest has rows; row count, `surface_z_cm` and brush counts against the staged manifest; hard-fails the run |

---

## 16. Related docs

- `docs/vtmb/water.md` — the VtMB inventory, the shipped program, the volume rules, the events,
  and the census evidence.
- `docs/architecture/seam_map_material.md` → `M_V2_Water`; `seam_map_map.md` → "Import — water
  volumes and water faces" — the pipeline contracts.
- `docs/project/seam_migration.md` → "R7.1" — the migration log (what landed when, and the
  G1–G26 disposition table).
- `docs/project/effects_authoring.md` — the water splash and drip roots (A2, A11).
- `docs/vtmb/source_movement.md` — `WaterMove` / water level; `docs/vtmb/weather.md`,
  `docs/vtmb/effects.md` — drips, splashes; `docs/vtmb/reflections.md` — `$envmap` on the LMG
  water-look materials; `docs/vtmb/surface_properties.md` — the `water` / `wade` rows.
- `docs/architecture/effects-architecture.md` — the same seam for fire, steam, blood;
  `docs/architecture/rendering-perf.md` — the budget;
  `docs/project/reconstruction-direction.md` — presentation may modernize; logic reproduces.

---

## 17. The player's water footstep clock moved to the substrate (2026-09-06)

Appended by the footstep lane. `AElysiumMapActor::UpdatePlayerWaterFootsteps` /
`PlayPlayerWaterFootstep` and `ElysiumWaterAudio`'s `StepIntervalSeconds` / `IsSoundingStep` /
`StepCue` are gone. They were never a water feature: retail's water and wade steps are two arms of
one `CBasePlayer::UpdateStepSound` (`vampire.dll 1011e940`) switch that also has a dry arm and a
ladder arm, and running the wet half here left the game with two step timers and two independent
notions of which foot was next. The clock is now `ElysiumFootsteps::AdvanceStepClock`
(`Private/Substrate/ElysiumFootsteps.h`), sequenced by `FElysiumPlayer::TickStepClock` off the
locomotion sample, and the step pools come off the same baked `PM_water` / `PM_wade` assets through
`IElysiumEmbodiment::ResolveSurfaceSounds` — so there is exactly one producer. **What this actor
still owns is the classification**: `UpdatePlayerWater` settles the level every pre-move pass,
writes it onto the mover, and publishes it as `PlayerWaterLevelNow()`; D3/D4's rule — the pool is
keyed off the classified LEVEL and never off the material under the foot, because the pier's foam
cards bind `PM_default` — is unchanged and is now asserted in
`Elysium.Substrate.Footsteps.PlayerWater`. The impact, scrape and exit cues, the splashes and the
level transition all stay here. Two constants changed with the move and both are recoveries rather
than retunings (`docs/vtmb/footsteps.md` §2.2, §2.8): the term added to the re-armed interval is
`flduck` 100 ms, not `velwalk` 60 — so a level-1 walking step is 500 ms and a wading one 700 —
and the water band's 60 u/s minimum speed gates nothing, because the arm that reads it is dead
behind `ReduceTimers`' clamp at zero.
