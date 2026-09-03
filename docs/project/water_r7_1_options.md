# R7.1 Water — options draft

> Working note for the R7.1 ruling. It carries no task status (`docs/project/roadmap.md` owns
> that) and is not the seam contract. Once the owner rules, the ruling goes into
> `docs/architecture/water-architecture.md` (Unreal side) and the new VtMB facts into
> `docs/vtmb/water.md`; this note is then deleted.
>
> Sources, 2026-09-02: a read-only survey of the V2 lane (`make_v2_materials.py`,
> `importers/materials.py`, `importers/map_geometry.py`, `bake_map_v2.py`, `formats/map_glb`,
> `Source/ElysiumUE`), a decompile pass over `client.dll` / `engine.dll` / `stdshader_dx8.dll`
> for the open questions in `docs/vtmb/water.md`, and a fact-check of the 5.8 engine source
> (`SingleLayerWaterRendering.cpp`, `SingleLayerWaterShading.ush`, `BasePassPixelShader.usf`,
> `Lumen.cpp`, `MegaLightsInternal.h`, the Water plugin).

---

## 1. What VtMB actually does (new facts, from the decompile)

These close the open questions at the end of `docs/vtmb/water.md` and correct two of its
inferences. All verified unless marked.

**One fog volume per frame, chosen by a front-to-back walk.**
`CVRenderView::GetVisibleFogVolume` (engine `20081390`) descends the BSP to the eye's leaf; if
that leaf's contents carry the `0x200` test-fog-volume bit it runs `R_RecursiveWaterEnum`
(`20081470`): PVS-culled, frustum-culled, front-to-back from the eye, **first leaf with
`leafWaterDataID != -1` wins**. The result is one `leafwaterdata` index for the whole frame.

**"Eye is underwater" is the eye's own leaf**: `pEyeInFogVolume = leafs[eyeLeaf].leafWaterDataID
!= -1`. Not a plane test, not a distance.

**Fog parameters come from the volume, through `surfaceTexInfoID`.**
`SetFogVolumeState` (engine `2007fda0`): `leafwaterdata[id].surfaceTexInfoID → texinfo → material
→ $fogenable / $fogcolor / $fogstart / $fogend`. `$fogenable 0` means `SetFogMode(0)`, no fog at
all. This is how twelve `hw_warrens_2` volumes carry twelve fogs. Start and end are handed to the
material system negated. Above the plane the mode is `LINEAR_BELOW_FOG_Z` with `SetFogZ(surfaceZ)`
(only geometry below the plane is fogged, so you see murk *through* the surface); below the plane
it is plain linear fog over the whole scene.

**Underwater is fog plus the surface shader. Nothing else.** `ViewDrawScene_EyeUnderWater` (client
`1019add0`) is two passes: (1) the above-water world plus skybox into `_rt_WaterRefraction`, clipped
at the plane, with the ordinary world fog; (2) the below-water world plus the water surfaces into
the framebuffer with the volume's fog. **No reflection pass under water.** `mat_wateroverlaysize` is
a debug picture-in-picture of the two RTs; `WaterWarp_old` is the *vertex shader* of the refract and
reflect passes, not a screen warp; `mat_waterswirl` is a per-vertex sin/cos rotation of the surface
normal over world XY and time (verified structurally); `dsp_water` is a full DSP preset applied to
the mix only at water level 3. No tint pass, no overlay quad.

**The underside is refraction-only by engine decree.** `Mod_LoadFaces` (engine `200b73d0`) calls
`$reflecttexture->SetUndefined()` on the material of every water face whose plane normal points
down. `dev/dev_waterbeneath2` therefore draws perturbed refraction × `$refracttint`, no reflection,
no Fresnel term.

**Expensive water is unconditional; cheap is authoring, not LOD.** `Water_Old::SHADER_DRAW`
(`stdshader_dx8` `100138a0`): `if ($envmap defined && $forcecheap) cheap; else { refract if
$refracttexture; reflect if $reflecttexture }`. No distance test anywhere in the shader or the
client. `$cheapwaterstartdistance` / `$cheapwaterenddistance` (shader defaults 500 / 1000 in) are
pixel constants of the ps2.0 cheap program only. **This retracts `docs/vtmb/water.md`'s "cheap
cubemap by distance overlay" inference.** `ViewDrawScene_EyeAboveWater` fills both RTs every frame;
the only gates are `mat_drawwater`, `r_drawwatersurface` and `r_watercullenable/height`.

**Pass composition**: refract pass, no blend (or `SRC_ALPHA/INV_SRC_ALPHA` when the material is
translucent); reflect pass **additive `(ONE, ONE)`** on top. PS `c1` is `$refracttint` / `$reflecttint`
respectively. PS `c3 = (1,0,0,0)` in the reflect pass, so Schlick R0 = 0: **Fresnel is a pure
`(1 − N·V)^5`**, not authorable from the VMT. `$fogcolor` is bound as `c0` of the cheap program
(surface *and* volume). `SHADER_INIT_PARAMS` warns and sets red when `$fogcolor` is missing.

**`LEAFMINDISTTOWATER` (lump 46) is never loaded.** The engine's complete `LoadLump` set omits
`0x2e` and `0x32`. It is dead VBSP payload. **The roadmap's "underwater from `leafMinDist`" cannot
be a faithful derivation**; the source is `leafWaterDataID` per leaf plus `leafwaterdata[]`.

**No drowning, no slime, no water damage.** `m_AirFinished` and the drown fields are written once
and never read; `m_nWaterType` has no reader; no `drown` sound string exists. Water hazards are the
authored `trigger_hurt` brushes.

**AnimatedTexture rate** lives in the proxy block (`animatedTextureFrameRate`, default 15 fps),
nowhere in the shader.

---

## 2. What the V2 lane carries today

| Piece | State |
|---|---|
| `M_V2_Water` (`make_v2_materials.py::_build_water`, `GRAPH_VERSION 5`) | Default Lit, **Translucent**, `TLM_SURFACE_PER_PIXEL_LIGHTING`, `RM_PIXEL_NORMAL_OFFSET`, no Nanite flag. Wired: DuDv + NormalMap → Normal, `MP_REFRACTION = 1 + RefractAmount/100`, Fresnel(exp 5, R0 = `BaseReflectFract`) × `ReflectAmount/100` × `ReflectTint` → Specular, fixed-cube emissive add, the VMT fog tail (`Emissive += FogColor × saturate((PixelDepth − FogStart)/(FogEnd − FogStart))`, opacity toward `FogColor.a`). Declared, not wired: the wave scalars, `CheapWaterStart/EndDistance`, `WaterDepth` |
| Instance staging (`importers/materials.py`) | every water key lands (`$fogcolor/start/end`, both tints, both amounts, `$forcecheap → CheapWater`, `$waterdepth → WaterDepth`, `$envmap`). Provenance only: `$bottommaterial`, `$reflecttexture`, `$refracttexture`, `waterlod` proxy. `water/cheap_water` and `water/invisible_water` are named divergences (non-water families, their fog keys dropped) |
| Placement (`map_geometry.py`, `bake_map_v2.py`) | water faces are ordinary chunk faces in the `T_` non-Nanite bucket; the lane forces `Opaque` on the instance, which the master cannot honour (translucent). Nothing water-specific: no plane, no underside dedup, no volume |
| `leafData[]` / `leafMinDist[]` | decoded by `formats/map_glb/lumps.py::leaf_water` into the `vtmb:map:` unit's `water` block (`surfaceZ`, `minZ`, `surfaceTexInfoID`, metres), and `leafs[].leafWaterDataID` is parsed. **No stage, bake or C++ reads any of it** |
| `.water` sidecar | legacy lane only (`UE_bsp_to_scene.py`); `bake_map_v2.py` never reads it |
| Runtime | `UElysiumMovementComponent::WaterMove`, `EElysiumWaterLevel`, the sink/friction constants and the animation branch are implemented and tested; **`SetWaterLevel` has no caller**. No water actor, no volume, no underwater post, no `ElysiumFog` term on water |
| Textures | `T_dev_water_normal_n`, `T_dev_water_dudv_n`, `TA_water_normal` (+ `_linear` twin) exist on the texture lane |
| Render config | DX12 / SM6, Lumen HWRT, MegaLights, VSM. **Substrate off.** Water / WaterExtras / WaterAdvanced / Landmass **not enabled** |

---

## 3. Unreal 5.8 facts that bear on the choice

Verified against engine source. Five claims in `docs/architecture/water-architecture.md` are
wrong for 5.8 and are marked **[fix]**.

- **Single Layer Water (SLW)** is `MSM_SingleLayerWater`, Opaque or Masked only, its own pass after
  deferred lighting and before translucency, writes depth and velocity. Works on any
  `UStaticMeshComponent` with an SLW material; the Water plugin is not required. **Nanite rejects it**
  unconditionally. Works identically with Substrate on or off (auto-converted).
- **Reflections**: `r.Water.SingleLayer.Reflection` 0 off / 1 scene method (Lumen) / 2 captures / 3
  SSR. SLW pixels **always request a Lumen reflection trace** (`LumenReflectionCommon.ush:326-337`,
  no roughness gate); roughness is the material's own, clamped safe. **[fix]** the doc's
  "`LumenReflectionCommon.ush` returns roughness 0" is not in 5.8; the sharp look comes from
  authored low roughness plus the unconditional trace. **Planar reflection components do not feed
  SLW at all** (zero references in the SLW pass) and get no Lumen (`Lumen.cpp:250-259`).
- **MegaLights does not light water** (`EMegaLightsInput` has no water member; SLW draws after the
  MegaLights composite). Confirmed unchanged. Water sees the Lumen mirror of the lit world, which is
  the 2004 shape.
- **Units**: Scattering / Absorption Coefficients are **1/cm** (node tooltip). **[fix]** the doc's
  "reciprocal metres" and the `× 0.0254` sketch; Source inches → cm is `× 2.54`.
- **Opacity is a coverage mask, not murk.** `WaterVisibility = 1 − Opacity`; at Opacity 1 the water
  contribution is zero (`BasePassPixelShader.usf:1140`). **[fix]** the doc's "opacity high on blood /
  spawn". Murk is absorption and scattering only. Confirm in-editor (Opacity 0 vs 1 on
  `Water_Material_Lake`) before writing the binding.
- **Color Scale Behind Water** fades in over the first ~50 cm of water depth
  (`lerp(1, scale, saturate(depth × 0.02))`), so it also applies at shallow depth from below.
- **Camera under the plane** is a per-view flag (`bCameraIsUnderWater`) with its own branch in the
  shading (`SingleLayerWaterShading.ush:157-238`): volume depth becomes the surface depth, Fresnel
  applies to transmittance. **[fix]** "SLW has no backface" → one-sided by material convention
  (`TwoSided` off on every Epic water), not an engine limit. Whether a two-sided SLW plane reads
  correctly from below is **untested**; it is check 3 in §6.
- `r.Water.SingleLayer.DepthPrepass` (default 1, needed for VSM) is **read-only**: config, not
  console. New in 5.8: `r.Water.SingleLayer.Refraction.*Culling*` (skip scene behind water),
  `VelocityOutputPass`, `TiledSceneColorCopy`.
- **Translucent + Lumen reflections** exists in 5.8 only as Front Layer Translucency
  (`r.Lumen.TranslucencyReflections.FrontLayer.EnableForProject` + `.Enable`, per-material opt-in,
  frontmost layer only). No volumetric absorption on translucency; it shares the sorted translucent
  pass with rain.
- **`AWaterBodyCustom`** sets `bAffectsLandscape = false`, wraps a user-assigned
  `UStaticMeshComponent`, and finds a Water Zone by lookup without requiring one. **[fix]** the
  doc's blanket "Water Bodies want a Landscape, a Water Zone and a spline" is true of Ocean / Lake /
  River only. It is the only route to `FWaterBodyQueryResult`; there is no free function on
  `UWaterSubsystem`.
- **Underwater in the plugin** is `UUnderwaterPostProcessVolume`, a `UObject` implementing
  `IInterface_PostProcessVolume`, registered with `World->AddPostProcessVolume` and toggled hard
  (no distance blend) from a per-frame immersion query. That interface is base engine; the pattern
  reproduces without the plugin. Whether `M_UnderWater_PostProcess_Volume` samples the Water Zone's
  `WaterVelocityAndHeight` texture is unverified (binary asset, open in editor).
- Known 5.6–5.8 issues: directional-light shadow not occluding SLW highlights (open); Substrate +
  SLW + HWRT hit-shader compile error (only if Substrate is turned on); SLW writes depth, so
  translucency straddling the plane sorts wrongly.

---

## 4. The rulings R7.1 has to make, with options

Each ruling lists options, what each costs on the lanes this project already has, and a
recommendation. Recommendations are mine; the owner's call stands.

### R-A. Surface representation for `Water`-shader faces (24 units; 25 maps carry volumes per `docs/vtmb/water.md`, the roadmap entry says 22 — reconcile when the stage counts them)

**A1. Rebuild `M_V2_Water` as Single Layer Water.** Same master name, same exposed parameter set
(the three-way pin in `ElysiumSurfaceParams.h` / `EXPOSED_PARAMS` / `WATER_PARAM_TABLE` stays
green), shading model `MSM_SingleLayerWater`, blend Opaque, `SingleLayerWaterMaterial` output
node. The graph translates instead of transcribes:

| VMT | SLW pin |
|---|---|
| `$fogcolor`, `$fogend` (in → cm) | Absorption = `(1 − fogcolor) × k / fogend_cm`, Scattering = `fogcolor × k / fogend_cm` (k a Surfaces-page knob, faithful start value 1); `UseFogEnable` off → both ≈ 0 |
| `$refracttint` | Color Scale Behind Water |
| `$reflecttint` × `ReflectAmount/100` | Specular scale (Lumen supplies the image) |
| `$normalmap` + `animatedtexture` frames + `texturescroll` | the existing normal lane, unchanged |
| `$bumpmap` DuDv + `$refractamount` | normal strength; SLW refracts along the normal, no second DuDv |
| Fresnel R0 = 0 | keep `BaseReflectFract` = 0 default on the Fresnel node (already the case) |
| `$forcecheap` | `CheapWater` switch: Specular from fixed cube × `ReflectTint`, refraction disabled (Color Scale 0 is not right; use high absorption), Fresnel lerp toward `FogColor` |
| `$fogstart` | no SLW equivalent; provenance only, or the post-process fog start under water |
| Opacity | 0 (coverage), soft depth-fade optional later |

- Gains: absorption below the plane (VtMB's `LINEAR_BELOW_FOG_Z`), correct lit scene behind
  water, Lumen reflections without any opt-in, depth write (no rain sort fight), the
  `Opaque`-override problem disappears (the lane's forced `Opaque` becomes the master's truth).
- Costs: the lane's `Opaque` override on water becomes a no-op; `NANITE_CAPABLE_MASTERS` stays as
  is (SLW never Nanite); the R6 scene-fog follow-up ("`ElysiumFog` term on `M_V2_Water`") has to
  be re-expressed — SLW takes exponential height fog from the scene natively, so the CPD fog term
  may simply not be needed on this master (check 4 in §6). One `GRAPH_VERSION` bump regenerates all
  nine masters. The cheap path is a translation, not a transcription (named divergence).
- Risk: whether the Lumen mirror of MegaLights-lit neon actually appears on the plane in
  `sm_hub_1` (check 1 in §6). If black, the fix is Lumen surface cache coverage, not the choice.

**A2. Keep the translucent master, add Front Layer Translucency reflections.** No master rewrite;
enable `r.Lumen.TranslucencyReflections.FrontLayer.*` and opt the master in.
- Gains: smallest diff.
- Costs: no absorption (the murk stays the emissive fog tail, unlit and view-independent), the
  `Opaque` override on the lane stays a lie that `MaterialBinding.opaque` has to keep guarding, rain
  and water contend in one sorted pass, the "frontmost layer only" rule breaks the moment a sprite
  or rain sits in front of the canal. Does not answer `$reflecttexture` / `$refracttexture` "in one
  place" as the roadmap entry asks.

**A3. `AWaterBodyCustom` on the authored chunk meshes.** Enable the Water plugin; the bake wraps
each water chunk in a body actor with `SetWaterMeshOverride`.
- Gains: `FWaterBodyQueryResult` (immersion depth) and the plugin's underwater post for free;
  Epic's SLW master and panner recipe.
- Costs: a plugin the project has kept out on purpose; one actor per water chunk (chunks are
  per-cell, so a canal is several bodies); the body's own `UStaticMeshComponent` replaces the
  chunk's placement path in `bake_map_v2.py`; editor validation and HLOD paths run whether used or
  not; the Epic master's parameter names replace the pinned `WaterParams` contract, or the bake
  authors a second MI layer. The query API is the only thing A1 cannot give, and §R-E shows the
  query is a box test on data the unit already publishes.

**A4. Hybrid, already implicit.** The three LightmappedGeneric water-look units (`blackwater`,
`bloody_water`, `warrenwater2b`) stay on `M_V2_LitTranslucent` + envmap; `invisible_water` stays
`M_V2_Unlit` nodraw; only true `Water` faces take R-A's choice. This is the current family
resolution and needs no ruling, only a sentence in the seam doc.

**Recommendation: A1.** It is the only option that answers all three items the roadmap entry
names (reflect, refract, fog in one place) on the existing master contract without a new plugin.

### R-B. The water *volume* (what movement, fog and "underwater" test against)

The unit publishes `water.leafData[]` (`surfaceZ`, `minZ`, `surfaceTexInfoID`) and
`leafs[].leafWaterDataID`. Nothing reads them. Options for the stage product:

**B1. Per-volume record, faithful to `SetFogVolumeState`.** Stage emits `water.volumes[]`: one
row per `leafData` entry (drop the `16384` sentinel rows) with `surfaceZ` (cm), `minZ` (cm), the
resolved `vtmb:material:` id of `surfaceTexInfoID`, that material's `$fogenable/$fogcolor/
$fogstart/$fogend`, and the **AABB union of the leaves whose `leafWaterDataID` points at it**
(leaf mins/maxs are in the leaf lump). The runtime gets a box set per volume.
- Gains: one row per authored volume, which is exactly the granularity VtMB reads fog at
  (`hw_warrens_2` ×12). The eye test is point-in-box; movement's three-point test is the same
  box; no brush export needed. Fog keys ride with the volume, not the surface, so
  `invisible_water` (no drawn surface) still fogs and swims.
- Costs: leaf AABBs are conservative (a leaf can poke above `surfaceZ`); clamp the box top to
  `surfaceZ`. `la_bradbury_3`'s accidental `TOOLS_SHADOW` texinfo yields a volume with no fog keys
  (`$fogenable` absent → no fog, faithful).

**B2. Export `CONTENTS_WATER` brushes as hulls.** A second hull lane with `contents & 0x20`
(not `0x18000120` shadow casters).
- Gains: exact volume shape.
- Costs: a new hull family in the collision lane, a per-brush overlap actor, and no fog keys
  (brushes do not carry `surfaceTexInfoID`; you would join back to `leafData` anyway).

**B3. Plane only.** `surfaceZ` + the water chunk's XY bounds.
- Cheapest, wrong for stacked volumes and for `minZ` (a body below the floor of the basin is not
  in water).

**Recommendation: B1.** The data is already decoded; the stage work is a join and an AABB union.

### R-C. Runtime owner of the volume

**C1. One `AElysiumWaterVolumes` actor per map** (spawned by the bake beside the level's other
map actors, holding the `volumes[]` array as a `UDataAsset` row set). Per tick: eye point → which
volume box contains it (underwater flag + active volume); nearest-visible selection is replaced by
"the volume the camera is in, else the nearest volume whose box intersects the view frustum"
(the PVS walk is not reproducible without leaves; this is a named divergence, and nothing in the
corpus has two overlapping fogs visible at once). Drives: `SetWaterLevel` on the player's movement
component via the feet / waist / eyes point tests (`docs/vtmb/water.md` → Movement), the
underwater post (R-D), and the `dsp_water` submix at level 3 (audio plan, out of R7.1).
**C2. A `UBoxComponent` per volume with overlap events.** Simpler to see in the editor, but
overlap is per-actor, not per-point, and the three-point classification needs point tests anyway.

**Recommendation: C1**, with the boxes drawn as editor-only debug shapes so the volumes are
inspectable ("the editor is the tuning surface").

### R-D. Underwater view

VtMB: the volume's linear fog over the whole scene + the above-water world seen through the plane
with world fog. No warp, no tint.

**D1. Project-authored post-process material, linear fog only.** `Color = lerp(SceneColor,
FogColor, saturate((SceneDepth − FogStart)/(FogEnd − FogStart)))`, three parameters, bound from the
active volume; blend weight 1 when the eye is in a volume, 0 otherwise. One `APostProcessVolume`
(unbound) per map or the `IInterface_PostProcessVolume` pattern Epic uses.
- Faithful, tiny, no plugin content, no Water Zone texture dependency.
**D2. Duplicate `M_UnderWater_PostProcess_Volume` from the Water plugin.** Prettier defaults; has
to be checked for plugin-only inputs; brings wave/foam terms VtMB never had.
**D3. Nothing** for the first cut (the current playable slice may never submerge the camera).
Movement still needs R-B/R-C, so D3 saves only the material.

**Recommendation: D1.** It is the transcription of `SetFogMode(1)`; anything more is a look
decision for the tuning sessions.

### R-E. The underside (`$bottommaterial`, `dev_waterbeneath2` faces)

The corpus has the underside as real faces at the same Z, downward-facing, on their own
`vtmb:material:maps/<map>/dev/dev_waterbeneath2` instance (already staged on `M_V2_Water`).
Engine fact: reflection stripped, refraction only.

**E1. Keep the faces, one-sided SLW, and let the camera-under branch do the rest.** The bake places
them as today; from below, the underside faces are front-facing so they draw; SLW's
`bCameraIsUnderWater` branch handles the shading. Needs check 3 in §6 (does SLW from the under
side read correctly). Nothing new in the lane. Stage sets `ReflectAmount = 0` on units whose
faces all point down (or a `Underside` switch) to honour the strip.
**E2. Two-sided SLW on the top faces, drop the underside faces.** Fewer triangles; loses the
per-underside instance (`$waterdepth`, tint) and depends on the same untested branch.
**E3. Skip the underside entirely.** Faithful only while the camera never goes under; couples to
D3.

**Recommendation: E1**, pending check 3. If SLW from below is wrong, fall back to E1 with the
underside faces on `M_V2_Refract` (refraction-only is exactly what the engine leaves them).

### R-F. Cheap water and the LMG fakes

`$forcecheap` is authoring: `dev_water2_cheap` (la_dane_1, ch_temple_1, sp_giovanni_1, sp_soc_3)
draws cube × `$reflecttint` lerped to `$fogcolor` by Fresnel, no refraction, always.
`CheapWaterStart/EndDistance` are a ps2.0 fade only.

**F1.** `CheapWater` switch on the SLW master: refraction effectively off (absorption high, Color
Scale from `$fogcolor`), Specular from the fixed cube. One master. **F2.** Route `$forcecheap`
units to `M_V2_Lit` + envmap like the LMG fakes. Two families for one shader. **F3.** Ignore the
flag; every `Water` unit is expensive.

**Recommendation: F1**; it is one switch that already exists. The distance pair stays declared,
not wired (provenance), as today.

### R-G. `leafMinDist[]`

Never loaded by the engine. **Recommendation: drop it from the roadmap wording and leave it in
the unit as provenance.** Do not derive anything from it.

### R-H. Scene fog on the water surface (R6 follow-up)

VtMB draws the above-water surfaces with the world fog. SLW takes the scene's exponential height
fog natively. **Recommendation:** verify in check 4 that the project's fog (`ElysiumMapVisuals`)
reaches the SLW pass, and if so close the follow-up as "not needed on this master" rather than
adding the CPD term.

### R-I. Splash, drip, mist, audio

Out of R7.1: drips and mist are R7.3 families (`WaterDrops_Timer` etc.), footsteps and
`dsp_water` are `plans/audio.md`. R7.1 lands the surface, the volume and the swim seam.

---

## 5. What A1 + B1 + C1 + D1 + E1 changes, lane by lane

| Lane | Change |
|---|---|
| `make_v2_materials.py` | `_build_water` → SLW; `GRAPH_VERSION 6`; `CheapWater` re-expressed; `UseFogEnable` → coefficients; fog tail removed |
| `importers/materials.py` | absorption/scattering derived at stage from `$fogcolor/$fogend` (two new vector params, or derive in-graph from the existing `FogColor/FogEnd` — in-graph keeps the pin table unchanged; **prefer in-graph**) |
| `importers/map_geometry.py` / stage | `water.volumes[]` product (B1); `Opaque` override on water becomes redundant, keep or drop with a test row |
| `bake_map_v2.py` | spawn the volumes actor per map; nothing changes for the chunks |
| `Source/ElysiumUE` | `AElysiumWaterVolumes` (+ `UDataAsset`), the three-point water level tick calling `SetWaterLevel`, the underwater post binding; `ElysiumSurfaceParamsWater` unchanged |
| Config | `r.Water.SingleLayer.DepthPrepass=1` stated in `DefaultEngine.ini` (already default; pin it, read-only) |
| Tests (at the seam, per the standing rule) | one `test_materials_stage.py` row: a water unit stages to an SLW-parented instance with the pinned names; one `test_map_geometry.py` row: the `hw_warrens_2` twelve volumes each carry their own fog keys and the sentinels are dropped; one `Elysium.Policy` row: the water actor sets `Waist` for a point between `minZ` and `surfaceZ`; one Substrate pin on the master's shading model |
| Docs | `docs/vtmb/water.md`: fold §1 (retract the distance-LOD inference, add the fog-volume selection, the underside strip, dead lump 46, no drowning); `docs/architecture/water-architecture.md`: the five **[fix]** items in §3 and the ruling; `seam_map_material.md` → `M_V2_Water` section; `seam_map_map.md` → the `water.volumes[]` product |

---

## 6. Exploration checks before the ruling (no bake, no game code)

Per `effects-architecture.md` §6: engine content, a scratch folder under
`/Game/ElysiumAuthored/FX/_Explore/`, a disposable map. Each check has a yes/no result.

1. **SLW draws in this render path.** `/Water/Meshes/S_WaterPlane_256` + `/Engine/EngineMaterials/
   WaterMaterial` in a copy of the `sm_hub_1` level beside the sewer; Lumen HWRT + MegaLights on.
   Does the neon reflect? (Black surface = Lumen surface cache, not a water problem.)
2. **Opacity semantics.** Opacity 0 vs 1 on `Water_Material_Lake`. Expected: 1 makes the water
   vanish. Settles the binding.
3. **From below.** Drop the camera through the plane with a one-sided SLW plane and a two-sided
   copy. Which reads as a water surface from underneath? Settles R-E.
4. **Scene fog reaches SLW.** With the map's `ElysiumMapVisuals` fog active, does the plane fog
   with distance? Settles R-H.
5. **Absorption from the three numbers.** `sewer_water` (`{5 5 0}`, end 1024 in → 2601 cm) and
   `spawnwater` (red, end 64 in → 163 cm) on the 1/cm formula in R-A. Did-it-appear only; no
   tuning (wire first, tune later).

Time-box: one editor session. Results go in this note's §1–§3 as facts, then the ruling is
written into the seam docs and this note is deleted.
