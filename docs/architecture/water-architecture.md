# Water — Unreal reproduction

VtMB's water inventory, shaders and volumes are `docs/vtmb/water.md`. This document is the
Unreal side: the R7.1 ruling, the seam between reproduced placement and modern look, and the
engine facts (this 5.8 install, verified in source) the design rests on.

The goal is not a port of `_rt_WaterReflection` cameras or the 2004 DUDV perturb. The goal is
that a player who remembers the Santa Monica sewer, the downtown canal, a warrens basin or the
Vesuvius pool still *reads* those places — murky, reflective, a little wrong — built with
Unreal's own water shading, and that a body that walks into one of them is in water.

Status lives only in `docs/project/roadmap.md` (task **7.3 Water**; the seam-migration track
calls the same task **R7.1** — one task, two ids).

The 2026-09-04 water-complete pass (rulings E revised, F, K, L, M, N — §1.1) closed R7.1's
remaining gaps; the evidence census it was built from is `docs/vtmb/water_data_census.md`.

---

## 1. The R7.1 ruling (2026-09-04)

**Presentation is modernized. Authored placement, parameters and the volume are reproduced.**
Fourteen rulings, each a transcription unless it says *named modernization*: A–J are R7.1's, of
which E and F were revised, and K–N are the water-complete pass's (§1.1).

| # | Ruling | In one line |
|---|---|---|
| **A** | **`M_V2_Water` is a Single Layer Water master.** Same name, same pinned parameter set plus one switch (`Underside`); `MSM_SingleLayerWater`, `BLEND_Opaque`, one-sided, never Nanite. `GRAPH_VERSION` 8 → 9. | *named modernization*: the two 2004 render-target passes become SLW's scene-colour refraction and Lumen's reflection, and the in-volume fog becomes SLW's absorption/scattering — the three things the roadmap asked to answer in one place |
| **B** | **`water.volumes[]` is a stage product of the map lane**: one row per real `LEAFWATERDATA` volume (sentinels and the `la_bradbury_3` shadow row dropped), each carrying `surfaceZ`/`minZ` in cm, the fog keys resolved through `surfaceTexInfoID` → texinfo → material, and the volume's `CONTENTS_WATER` brushes as convex plane sets in Unreal cm | transcription of what `SetFogVolumeState` reads and what `CheckWater`'s `MASK_WATER` traces hit |
| **C** | **One `AElysiumWaterVolumes` actor per converted map**, bake-placed, tagged `elysium.water`, adopted by `UElysiumMapVisuals`; `AElysiumMapActor::PreMoveTick` classifies the player's feet / waist / eyes against it and calls `SetWaterLevel` before the move | transcription of `CGameMovement::CheckWater` (three point queries, level 0–3), on the declared edge "player movement follows pre-move" |
| **D** | **Underwater is a post-process linear fog**: `M_ElysiumUnderwater` (`MD_PostProcess`, `lerp(scene, FogColor, saturate((SceneDepth − FogStart) × FogInvRange))`) registered by the water actor as an `IInterface_PostProcessVolume` whose `EncompassesPoint` is point-in-brush of the view location; the same actor writes `FSceneView::UnderwaterDepth` from `UWorld::OnBeginPostProcessSettings` | transcription of the `FogMode(LINEAR)` `ViewDrawScene_EyeUnderWater` selects; *named modernization*: the world fog is not suppressed under the plane (§9) |
| **E** (revised 2026-09-04) | **The underside is a property of the FACE, not of the material.** A water face is underside iff its own plane normal has `z < 0` in Unreal space; every water instance stages an `MI_<unit>_Underside` twin, and the underside sections bind it. The twin **drops reflection** — specular 0, roughness 1 — and keeps the surface's extinction | transcription of `Mod_LoadFaces` (`_DAT_201734e8 = 0.0`, then `$reflecttexture->SetUndefined()` on the material of the down-facing face). The per-face key is the correction: VtMB mutates the *shared* material and is therefore load-order dependent; the port cannot be. See §1.1 |
| **F** (revised 2026-09-04) | **`$forcecheap` is not a murkier volume, it is no volume**: `CheapWater` zeroes both coefficients, forces Opacity to 1 and **emits the whole 2004 lerp** — `lerp($fogcolor, cube × ReflectTint, Fresnel(BaseReflectFract, ^5))` on the authored-cube branch, the flat `$fogcolor` where the unit names only `env_cubemap` | transcription of `WaterCheap_ps11` — one instruction, `lrp r0.rgb, fresnel, cube × $reflecttint, c0($fogcolor)`, no refraction pass, on a `SURF 0x408` = `WARP\|NOLIGHT` face, so Emissive is the only pin that survives. *Named modernization* only in that Lumen supplies the mirror where the cube is the map's own probe |
| **G** | **`leafMinDist[]` is provenance.** The engine never loads lump 46; nothing is derived from it | fact, not a choice |
| **H** | **No scene-fog term on the water surface.** The R6 follow-up ("`ElysiumFog` term on `M_V2_Water`") closes as *not on this master*: the refracted world and the Lumen mirror already carry their own per-primitive fog; the surface's own scatter and specular are unfogged (§9) | *named modernization* — VtMB draws the surface under the world fog |
| **I** | **Out of R7.1**: drips, mist, splashes (R7.3 families), `dsp_water` (preset 14 at level 3) and wade footsteps (`plans/audio.md`), NPC water levels (`PhysicsCheckWater` on `MOVETYPE_STEP`, the entity substrate's), the `trigger_hurt` pools (already entities) | scope. **Corrected 2026-09-04**: R7.1's parenthesis "VtMB itself spawns no splash on entry" is withdrawn — it reasoned from the fluid controller, whose splash builder is dead code. VtMB *does* splash, from the client's water-level transition, and ruling L brings splashes and the wade/step sounds into scope |
| **J** | **`sm_pier_1`'s surf cards slide on a new `SineUVTranslate` vector.** `M_V2_Lit`/`M_V2_LitTranslucent` gain `(ampU, ampV, offU, offV)`, default `(0,0,0,0)`; the stage resolves a `sine` → `texturetransform` chain into it (two-pass: record the `sine`'s `$temp*` result, then consume it at the `texturetransform` that reads it back), and the graph feeds it to the base-texture panner only, ahead of the bump panner | transcription of the `objects/surf` wave proxy (17 cards, two 15 s sine chains) |
| **K** (2026-09-04) | **A `%compilewater` face draws, even when it is `SURF_NODRAW`.** `%compilewater` — not the shader name — selects `M_V2_Water`, and a `%compilewater` face is exempt from the exporter's nodraw drop | *named modernization* **"surface on nodraw water"** (owner decision 2): VBSP is what makes a brush water; VtMB drew nothing at `sm_pier_1`'s swimmable volume because a 2004 engine could not draw a live ocean under a painted skybox card. See §1.1 |
| **L** (2026-09-04) | **Water raises events: a splash on the level transition, footstep and exit sounds by pool, and buoyancy from the authored `fluid` block.** The splash is the client's own water-level rule, not the fluid controller's dead `PhysicsSplash` | transcription of `client.dll FUN_10099630` and `vampire.dll 1011e940`; *named modernizations* **"splash on the water-level transition"** and **"vphysics buoyancy"**. See §1.1 |
| **M** (2026-09-04) | **A face's lightstyle becomes a per-primitive brightness on CPD slot 6.** A styled face is its own `(material, style)` chunk, tagged `elysium.style=<n>`, written each tick by the existing style clock and multiplying the lit base colour and emissive | *named modernization* **"lightstyle as brightness"** (owner decision 4): VtMB swaps which lightmap PAGE a styled face samples; Lumen replaced lightmaps project-wide, so there is no page to swap. See §1.1 |
| **N** (2026-09-04) | **The four standing divergences the pass leaves named**: `WaterMurkiness` stays wired; the `$bumpoffset` scroll is honoured though the 2004 shader ignored it; `waterbigsplash_emitter` is read from the retail pack member; `sm_pier_1` stays on the UP recompile with both lost leaf annotations derived rather than read | one *named modernization* and three *named divergences*, each with the measurement behind it. See §1.1 |

The one knob: `WaterFogScale` on the **Surfaces** settings page, pushed into
`MPC_ElysiumSurfaces` like every other, default **2·ln 2 ≈ 1.386** — the value at which SLW's
exponential extinction and VtMB's linear fog agree at the half-fog distance (§4.2). It ships at
that value and is not tuned here ("wire first, tune later").

**What the ruling retracts** from the 2026-09-02 options note and the earlier text of this
document: five engine claims (§8), the "`leafMinDist` underwater" wording of the roadmap entry,
the "22 maps" count (it is 25 maps with `LEAFWATERDATA`, 24 with a drawable water brush, 22
*units* on the master), and the assumption that the V2 lane's normal lane was already right on
water (it was not — §4.4).

---

## 1.1 The water-complete pass (2026-09-04) — E revised, K withdrawn, L–N, and O (2026-09-05)

R7.1 shipped water that *is* water. The complete decoded-datum census
(`docs/vtmb/water_data_census.md`) then measured what R7.1 had left on the floor: 26 gaps, of which
this pass fixed 18, ruled 6 out on evidence and left 2 to a game witness. The rulings below are the
ones that changed a rule rather than filling a hole; every one of them traces to a numbered gap in
that census, and every named modernization or divergence here is also written into the code that
implements it.

### E, revised — the underside is a property of the face

`Mod_LoadFaces` decides "underside" per face, on the face's own plane: `plane.normal.z < 0`
compared against the literal `_DAT_201734e8 = 0.0`. Its *response* is to call
`$reflecttexture->SetUndefined()` on the **shared material** — which is why VtMB's own answer is
load-order dependent and why the port cannot copy it as a material rule. So:

- **The face carries it.** `map_geometry.face_underside` reads the face's own plane row and
  `map_geometry.section_key` emits a `#underside` group; the bake binds that group to a twin asset.
  The rule is gated to `%compilewater` faces only — every ceiling in the game has a downward normal,
  and only water gets a twin. The `side` bit is deliberately NOT applied: measured, *every*
  down-facing water face in the corpus carries `side 1`, so flipping on it would call all 47 hub
  water faces up-facing.
- **The twin is an instance, not a switch on the surface.** Every `M_V2_Water` instance —
  including every patched one — stages `MI_<unit>_Underside` in the same folder, parented to the
  surface instance (an instance of an instance, exactly as a VMT patch is), stating exactly
  `{"Underside": True}` and inheriting everything else. 108 twins corpus-wide. The surface
  instance itself carries `Underside` **off, always**; the material-level `$bottommaterial` rule
  R7.1 used is retired, and `$bottommaterial` stays published as a resolved material reference
  (census G16).
- **The twin drops reflection and keeps extinction.** Specular 0 and Roughness forced to 1 — a
  mirror Lumen still resolved off a smooth surface would put back exactly what the engine strips.
  R7.1's "zero extinction" is **withdrawn**: the underside is the same body of water seen from its
  other side, and its extinction is the surface's. 5.8's SLW camera-under-water branch is still
  hardcoded off (§8), so an underside face integrates the volume as if the eye were above it —
  which is why `M_ElysiumUnderwater` exists as a post-process rather than a shading-model branch,
  and why the two are witnessed together.

### K — surface on nodraw water (owner decision 2) — **withdrawn 2026-09-05**

The master half of K stands: `%compilewater` — the compiler key, not the shader name — is what
makes a brush a water brush, so it selects the master (`materials.effective_family`). Three base
units reroute corpus-wide (`water/invisible_water`, `water/cheap_water`, `dev/dev_waterbeneath`)
plus `maps/sm_pier_1/water/invisible_water_depth_33` through its parent chain, and the two
consequences are still recorded: a `tools/` `$basetexture` on such a unit is a compiler annotation
(omission `toolTextureOnWaterSurface`), and `$translucent 1` is consumed by the water lane
(`waterBlendConsumedByTheWaterLane`).

The surface half is withdrawn, against the owner's own VtMB frames (ruling O). `sm_pier_1`'s ocean
is the `water/blackwater` card 21 in below the plane — a bumped, cube-reflecting LMG surface that
*is* the authored look — and a Single Layer Water sheet drawn over the `%compilenodraw` volume hid
it and shaded as a bare lit plane (the "tan sheet" of the 2026-09-04 witness). VtMB draws nothing
on a nodraw water brush and neither does the port: `map_geometry.compile_water_predicate` answers
"drawn water" = `%compilewater` **and not** `%compilenodraw`, so `UE_map_sidecars.meshed_faces`
drops those 50 faces again while `resolve_water_volumes` still stages the brush as a volume — its
fog, its body state, its splash and its sounds are untouched. `water/cheap_water` (`ch_fulab_1`,
not nodraw) still draws on the water master.

### L — the events water raises

Water is not only a look. Three event families, each from its own measurement:

- **Splash, on the water-level transition** (*named modernization* **"splash on the water-level
  transition"**, census G7). VtMB's live splash hook is `client.dll FUN_10099630`, reached from
  `DrawModel`: big splash on `waterLevel 0 → ≥1` with `velocity.z < −200 in/s`; wade splash while
  `0 < waterLevel < 3` with horizontal speed ≥ 50 in/s on a `5.0 − horiz·7.8e-5` s cooldown, at
  `origin − vel.xy·0.035` snapped to the surface plane (`+RandomInt(0,8)` in z for the wade one).
  The port has no equivalent draw hook and does not need one — **the transition is the event** — so
  the same two rules bind to `AElysiumMapActor::UpdatePlayerWater`. `ElysiumWater::DecideSplash` is
  the pure rule and `AElysiumWaterVolumes::OnSplash` the delegate; the roots are
  `NS_watersplash_emitter` / `NS_waterbigsplash_emitter` through the effect actor's by-root play
  API. The fluid controller's own `FUN_10151150` numbers are deliberately **not** reproduced:
  that function is `PhysicsSplash` with both `DispatchEffect` calls stripped — dead code in the
  shipped build. Stated side effect: the splash fires for a first-person player VtMB may never have
  drawn.
- **Sounds, by classified level and by pool.** `water.Impact` / `water.Scrape` off surfaceprop
  `water`; `Surfaces/Water/Step*` at level 1 — every level-1 step sounds — and
  `Surfaces/Wade/Step*` at level ≥ 2 on a four-phase counter whose phase 0 is silent, so **three
  wading steps in four sound** (`1011e940`: `if (DAT_1070b898 == 0) { DAT_1070b898 = 1; return; }`
  guards the play, then the counter cycles 1→2→3→0). Verdict D3's "every 4th step" is the inverse
  of the decompile and is superseded here. The exit sound plays on leaving. The pool is keyed off the **classified level, never off the
  surface material under the foot** — the pier's foam cards bind `PM_default`, so a material-keyed
  rule would give the waterline dry footsteps. `ElysiumWaterAudio` resolves through the baked
  `PM_water` / `PM_wade` surfaceprop assets and `AElysiumMapActor::PlayVoice`. Two honest gaps sit
  beside it, both stated in the header: `water.Impact`/`Scrape` are sound *script* names and this
  runtime has no script table, so the pool is taken from the surfaceprop's own folder until the
  sound-script lane lands; and `player/pl_wade2.wav`, the literal `vampire.dll 1003f4d0` pushes, is
  **not in the game** — an index of every VPK plus every loose override (67,469 packed entries,
  78,538 files) carries no `pl_wade*` at all. Leaving the water is silent here because it was silent
  in 2004; the name is kept so an install that does carry the file plays what the code names.
- **Buoyancy** (*named modernization* **"vphysics buoyancy"**, census G7/G21). VtMB hands the
  `CONTENTS_WATER` solid to vphysics' fluid controller, which is not in the Ghidra corpus and
  cannot be reproduced instruction for instruction. What *is* authored is the fluid row's `density`
  and `damping`, so the port spends them on Archimedes (ρ × displaced volume × |g| over the body's
  AABB) and on the body's linear damping — the native Unreal expression of the same two numbers.
  Nothing displaces the surface: VtMB never did either.
- **I/O and fog** are already the entity lane's: the hub's six `Sewer Scheme` triggers and the
  pier's `logic_auto → FadeGlobalWetness(1)` are staged outputs the runtime fires generically
  (census G8, G20). What they need is a witness, not a mechanism.

**The step clock is VtMB's own, not an animation notify.** `CBasePlayer::UpdateStepSound`
(`1011e940`) is a millisecond timer in the movement code — `m_flStepSoundTime` gates the head of
the function and each pool writes its own interval back (400 ms walking, 300 running, 600 wading,
plus a 60/120 ms bias) — so reproducing it needs no footstep event at all.
`AElysiumMapActor::UpdatePlayerWaterFootsteps` runs exactly that timer at the tail of
`UpdatePlayerWater` and calls `PlayPlayerWaterFootstep(StepIndex, bRightFoot)`; the consumer is
live, not landed-and-uncalled. When a real locomotion step producer lands it should call
`PlayPlayerWaterFootstep` directly and this clock retires with it, rather than running beside it.
`water.Scrape` **is** still uncalled, and for its own reason — it is a physics friction event, and
the whole-game impact/scrape lane owns it for all 63 surfaceprops.

### M — lightstyles as a per-primitive brightness (owner decision 4, *named modernization*)

VtMB animates a styled face by swapping which lightmap **page** it samples (`Mod_LoadFaces`'
`0x2000`, the pattern registered by `CWorld::vfunc104` at `vampire.dll 0x1023c020`). Lumen replaced
lightmaps project-wide, so there is no page to swap. The port keeps the authored motion and spends
it as brightness, in one lane across four files:

| Where | What |
|---|---|
| `importers/map_geometry.py` | `face_light_style` picks the face's style; `section_key(key, light_style=n)` emits a `#style<n>` group, so a styled face becomes its own `(material, style)` chunk — a chunk is the only thing the runtime can write a CPD to |
| `pipeline/unreal/bake_map.py` | `chunk_style_suffix` / `parse_chunk_style` name and decode the chunk; `chunk_actor_tags` tags it `elysium.style=<n>`; `set_fog` always stamps slot 6 = 1.0 beside the six fog floats |
| `Source/ElysiumUE` | `ElysiumLightStyle::SlotBrightness = 6` (`static_assert`, in `Public/ElysiumFog.h` beside the fog slots 0–5), `ElysiumLightStyle::ParameterName = "LightStyleBrightness"`, `UElysiumLightRig::AdoptStyledPrimitives` rewriting the float each tick off the same StyleTime/StylePatterns pair a styled light uses |
| `pipeline/unreal/make_v2_materials.py` | `LIGHT_STYLE_CPD_SLOT = 6`, `LightStyleBrightness` multiplying the lit base colour **and** the emissive on Lit, LitTranslucent, TwoTexture and Water; applied *before* `_scene_fog` on the Lit pair, so the haze is not scaled by the style |

Neutral value is **1.0**, not 0 — the opposite convention to the fog block underneath it, because a
brightness of 0 is black. The **lowest** style wins, not the first slot: 18 of `sm_pier_1`'s 34
`objects/surf` foam cards name the switchable style 32 in the earlier slot and style 1 (Quake's
flicker) later, so slot order would have split one waterline into two chunks on two patterns.

Two consequences the owner should see, both accepted with their reason:

- **A face naming two styles keeps one on its chunk.** Every style the face names is published on
  `water.faces[].lightStyles`, but the binding carries one. VtMB's own result is a *sum* of lightmap
  pages and the port has no lightmap to sum; a switchable style defaults to full brightness, so
  dropping it loses nothing until an entity actually switches that light, while the flicker is the
  visible authored motion. Revisit only if a witness shows a pier light whose switch visibly fails
  to darken its wall.
- **The split is map-wide, not water-only** — 211 styled sections on `sm_pier_1`, 110 on
  `sp_tutorial_1`, 15 on `sp_soc_3`, 5 on `sm_hub_1`. Restricting it to water would leave the
  pier's 34 foam cards as the sole animated surface on a map where 200 other sections carry the
  same authored styles. If a draw-call regression shows, the cheap fix is to merge sections sharing
  `(material, style)` across scenes, not to un-split them.
- **Two carriers, because a brush entity is not a chunk.** The pier's foam cards are the census's
  motivating case for G6 and they are *not* world geometry: all 17 `objects/surf#style1` groups sit
  in BRUSH scenes (`brush_11`, `14`–`22`, `30`–`32`, `41`, `43`, `44`, `48` — `func_illusionary`
  bodies), and the bake never places a brush mesh, so no actor of theirs can carry
  `elysium.style=<n>`. A chunk takes the style off its actor tag; a brush mesh takes it off its own
  material slot names, which the bake already writes as `safe_name(<group key>)` and therefore
  already end in `_style<n>` (`asset_names.brush_slot_style` states the rule,
  `ElysiumLightStyle::StyleFromSlotNames` is its runtime twin, and
  `UElysiumMapVisuals::RegisterRuntimeBrush` hands the component to the same clock as it is built).
  One primitive carries one CPD slot, so a brush mesh whose sections disagree on a style — or that
  mixes styled with unstyled sections — animates on none; `bake_map.py` fails the bake if an
  unstyled group key ever folds to the `_style<n>` shape.

### N — the four standing divergences

1. **`WaterMurkiness` stays wired** (*named modernization*, verdict C4). No shipped VtMB shader ever
   registered `$watermurkiness` — the 2004 renderer read the key and did nothing. Four units author
   a non-default value (`dev/dev_water` 0.85, `dev/nether01_water` 0.8, `water/oilfieldwatera`/`b`
   1.0), none on the three in-scope maps. Un-wiring it puts nothing in its place, so the base colour
   still lerps toward `WaterColor` by it. Recorded in `_build_water`.
2. **The `$bumpoffset` scroll is honoured though the 2004 shader ignored it** (*named divergence*,
   owner decision 3). The decompile measured that `Water_Old` never read the register the
   `TextureScroll` proxy wrote, on 13 water units including the sewer — the authored scroll did not
   move on screen. Two readings of one piece of evidence, authored intent versus 2004 result; the
   owner's call is intent, because the alternative drops motion the author explicitly wrote
   (`TextureScroll($bumpoffset, .05, 45.00)` on `water/sewer_water`, `$bumptransform` on
   `dev/dev_waterbeneath2`, both landing `BumpScrollRateU/V = 0.035355`). Recorded at the proxy site
   in `importers/materials.py`.
3. **`waterbigsplash_emitter` is read from the retail pack member** (*named divergence*, verdict
   F2). The Unofficial Patch comments the spawn token out (`// removed by wesp`), leaving an orphan
   block that stages as `role: "neither"`, 0 spawn blocks, unbalanced braces — so the UP-first
   resolution the whole particle lane uses would give the water-entry splash nothing to spawn. One
   key, one override: `exporters.particle_glb.RETAIL_PROVENANCE_DIVERGENCE_UNITS` is a
   single-entry frozenset and every other key's UP-first resolution is untouched.
4. **`sm_pier_1` stays on the UP recompile** (*named divergence*, owner decision 1, census G24).
   The pier the port converts is not Troika's: the UP build adds the visible ocean card and the
   `_depth_33` patch and destroys 12 hull-floor faces, two inert `CONTENTS_WATER` brushes and
   **both** of VtMB's leaf annotations. Both are therefore **derived, not read**: `0x200`'s set is
   `⋃PVS(water clusters)` (`map_visibility.pvs_union`, measured set-equal to VtMB's own annotation
   on `sm_hub_1` and `sp_soc_3`), and `0x800` is the precipitation render mask, which no rain gate
   may key on for this map — the UP build flags no leaf where retail flags 702, and the pier places
   10 `rain_box_emitter` roots.

### O — the look program (2026-09-05): `M_V2_Water` rebuilt to `Water_Old`'s three passes

The owner put nine VtMB frames beside the 2026-09-04 witness and the port read as "flat
transparencies" against water with volume. The cause was that the surface had been designed from
the *physical* model and from summaries of the program; the program itself had not been read. It
was, in this pass (`docs/vtmb/water.md` → "The live program, transcribed"): the live class is
`Water_Old_dx80_dx81_dx90` in `stdshader_dx8.dll`, and its three draw functions bind the SM2
`_old` programs whose readable twins are the SDK sources. The master now transcribes them
(`make_v2_materials._build_water`'s docstring is the pin table):

| `Water_Old` | `M_V2_Water` |
|---|---|
| refract pass: the scene below the plane, water-fogged along the through-water distance, sampled at `projUV + dudv × $refractamount`, × `$refracttint` | SLW's scene-behind-water + extinction (unchanged); **Refraction pin, `RM_2D_OFFSET` = `dudv × RefractAmount × WaterWarpScale`** — the same screen-space warp, which the base pass applies through `ComputeBufferUVDistortion` (× `saturate(thickness_cm / 30)`); `ColorScaleBehindWater = RefractTint` |
| reflect pass, SRC_ALPHA: the planar mirror at `projUV + dudv × $reflectamount`, × `$reflecttint`, alpha `(1 − N·V)^5` (R0 = 0) | Lumen's forced-mirror reflection through SLW's Schlick, off a normal tilted by `dudv × ReflectAmount × WaterReflectWarpScale` (*named modernization*: no reflection image to displace, so the displacement becomes a reflection-direction tilt); **Roughness 0.05** (a mirror render, not the class row); `ReflectTint` reaches Specular as luma only (divergence 4) |
| cheap pass, SRC_ALPHA by `saturate((dist − $cheapwaterstartdistance) / (end − start))`, defaults 500 / 1000 in: `$fogcolor + cube(reflect(eye, N)) × fresnel`; `$envmap` defaults to `engine/defaultcubemap`; `$forcecheap` = this pass alone, alpha 1 | **Emissive = that colour × the blend; Opacity (coverage) = the blend** (1 under `CheapWater`). Every water instance binds a cube (the authored one, else the map's VBSP-patched probe, else `engine/defaultcubemap` — `materials._water_default_cube` and the probe back-fill in `stage_materials`; `$forceexpensive` alone leaves it unbound). The `env_cubemap → Lumen` rule does not apply to water: the probe is the authored reflection, and an LMG water fake (`water/blackwater`) binds it the same way onto `M_V2_Lit`'s fixed-cube path |
| no diffuse term — nothing in the program is lit | **Base Color black** unless a base texture is bound. A white base lit by the scene was the pier's tan sheet and the basin's blue |
| `$fogcolor` bytes written to the framebuffer as-is (the basin measures (27, 25, 15) for an authored {22 20 10}) | `pow(bytes / 255, 2.2)` emitted linear; measured against the frames in §11 |

The two warp scales are `MPC_ElysiumSurfaces` knobs (`WaterWarpScale`, `WaterReflectWarpScale`,
default 0.01 per authored unit: the amounts are authored 15–100, the DUDV's signed rms is 0.027,
and the frames show a warp of 1–2 % of the frame). Not carried, and recorded: the map's range fog
on the surface itself (`CalcFog RANGE` in `Water_vs11`; the master's `FogStart`/`FogEnd`/`FogColor`
names are the VMT volume keys, so the per-primitive scene-fog lane cannot share them — divergence
10 stands), and `$reflecttint` as a colour on the mirror (divergence 4). The wave block stays
declared-not-wired (below), with one new caveat: `la_dane_1`'s `dev/dev_water` authors no normal
map and the owner's frame shows broad undulation, so the "dead vertex path" ruling is only proven
for units that carry `$normalmap`; Dane's ocean is re-checked when it converts.

### The declared-not-wired scalars, and why (verdict U2)

`M_V2_Water` exposes VtMB's whole `Water_Old` parameter surface; every scalar is either wired or
named here with the fact behind it, and the same table lives in `WaterParams`' docstring so the
answer is never further away than the code. **The whole unwired set traces to one measurement:
VtMB's shipped water vertex program never moves a vertex.** DX9 binds `Water_vs20_old`
(`stdshader_dx8.dll 100138a0`), which writes `oPos = dp4(v0, cModelViewProj)` on the untouched input
position; no water VS in the corpus contains a `sincos` or a time constant; and
`BuildMSurfacePrimVerts` (`engine.dll FUN_20074f40`) copies every compiled primvert position
verbatim.

| Declared, not wired | Why |
|---|---|
| `WaterBaseFactor`, `WaterBaseMovementDist`, `WaterBaseMovementFreq`, `WaterTimeFreq1`, `WaterTimeFreq2`, `WaterWaveHeight`, `WaterWaveLength` | the dead `Water_Old` vertex-wave block. There is nothing to translate: reproducing a wave here would be inventing motion VtMB did not draw |
| `WaterSpecularMin`, `WaterSpecularMax` | the same block's specular ramp, read by the same absent program; the port's specular is `class_specular × luma(ReflectTint)` |
| `WaterDepth` | VBSP's per-instance depth hint. The map lane stages the volume's real depth (`surfaceZCm`/`minZCm`), which is the number anything downstream must use |
| `CheapWaterStartDistance`, `CheapWaterEndDistance` | *named modernization to drop*: the distance at which the 2004 renderer swapped the expensive program for `WaterCheap_ps11`. 5.8 compiles one shading model per material and Lumen already LODs its own reflection by distance, so the two-program split has no port. `CheapWater` (`$forcecheap`) still selects the cheap **look** |
| `UseEnvMap` | `$envmap env_cubemap` means "the map's own probe", which is Lumen's job here and binds no texture. An **authored** fixed cube (`UseFixedCube`) is the branch with a sample to read, and it is wired into both the emissive add and the cheap lerp |
| `DuDvMap` (the plain `Texture2D` slot) | every DUDV in the corpus is a 29-frame VTF that stages as a `Texture2DArray`, so the bound lane is `DuDvMapFrames`; the 2D slot stays declared because the binding contract's key → parameter map still names it |

`RefractAmount`, `ReflectAmount` and `BaseReflectFract` **left** this list in this pass: the first two
are the DUDV warp strength (§4.4), the third is the cheap program's own Fresnel base fraction.

### Scope: player water *movement* is not in this pass

Water look, water state and the events water triggers (splash, sounds, entity I/O, fog, wetness) are
this pass's. Player water **movement** — `WaterMove`, the swim/tread animation intent, the camera's
water band — is not, by owner call. `AElysiumMapActor::UpdatePlayerWater` classifies the body and
`UElysiumCameraComponent::SetWaterState` still receives it (ruling C, ruling I); what stays unspent
is a `run play` witness of the swim path on `sp_soc_3`'s basin, which the substrate tier covers in
the meantime (§12).

---

## 2. The four facts, and what reads each

VtMB water is four stacked facts that the 2004 engine happens to share a name for
(`docs/vtmb/water.md` → "It is not one thing"). The design keeps them apart:

| Fact | VtMB | Unreal home (R7.1) |
|---|---|---|
| **Surface look** | `Water` shader faces; `waterrefract`/`waterreflect` over two RTs | `M_V2_Water` (SLW) on the chunk faces the bake already places |
| **Volume** | `%compilewater` brushes → `CONTENTS_WATER` leaves + `LEAFWATERDATA` | `water.volumes[]` → `AElysiumWaterVolumes` |
| **Underside** | every down-facing water face, reflection stripped off the shared material | the same faces, as their own `#underside` sections bound to the `MI_<unit>_Underside` twin (§1.1 E) |
| **Eye under the plane** | the eye leaf's `leafWaterDataID`, the volume's linear fog over the whole scene | `IInterface_PostProcessVolume` + `M_ElysiumUnderwater`, `FSceneView::UnderwaterDepth` |
| **Body in the volume** | `CheckWater` → `m_nWaterLevel` → `WaterMove` | `PreMoveTick` → `SetWaterLevel` → the already-written `WaterMove` |
| Dressing | drips, mist, splash, `surfaceprop water` | R7.3 families; `PM_water` physical material (already staged) |

### Family resolution (revised 2026-09-04 by ruling K)

| Authored fact | Unit family | Master | Volume? |
|---|---|---|---|
| `Water` shader (22 units: `sewer_water`, `warrwater`, `warrenwater*`, `pool_water`, `dev_water2*`, `spawnwater`, `bradbury_blood`, `mazewater`, `dev_waterbeneath2`, …) | `water` | `M_V2_Water` (SLW) | yes where placed |
| `LightmappedGeneric` water-look (`blackwater`, `bloody_water`, `warrenwater2b`) | `lightmappedgeneric` | `M_V2_LitTranslucent` + envmap | no (`la_bradbury_3`'s row is a shadow-brush artefact, dropped by B) |
| `water/cheap_water` (`ch_fulab_1`), `water/invisible_water` + `..._depth_33` (`sm_pier_1` ocean), `dev/dev_waterbeneath` | `%compilewater` → `water` (ruling K's master half, `materials.effective_family`) | `M_V2_Water` (SLW) | **yes**. `cheap_water` draws; `invisible_water` is `%compilenodraw` and draws nothing (K's surface half withdrawn, ruling O) — the pier's ocean is the `blackwater` card |

The reroute is measured on a full private stage of all 19,125 units, 0 failures: 3 base units move
(`water/invisible_water` from `unlitgeneric`, `water/cheap_water` from `lightmappedgeneric`,
`dev/dev_waterbeneath` from `watersurfacebottom` — previously provenance-only) and
`maps/sm_pier_1/water/invisible_water_depth_33` follows through its parent chain. 27 instances now
parent directly to `M_V2_Water`, was 24. One authored key is lost with it and named per unit rather
than dropped silently: `water/cheap_water`'s `$envmapmask effects/ref_75` has no destination
(`M_V2_Water` has no reflection-mask lane; its reflection is Lumen's, scaled by
`luma(ReflectTint)`), recorded as `compileWaterRerouteProvenanceOnly`. That is not a fidelity loss:
the unit authors `$forcecheap 1`, so the program it actually ran was `WaterCheap_ps11`, which has no
mask term at all — the `$envmapmask` was already dead on this unit in 2004. It binds no face on
`sm_hub_1`, `sm_pier_1` or `sp_soc_3` (verdict C8); it does bind on `ch_fulab_1`, and if that map
ever witnesses as too shiny the fix is a `ReflectTint` scale on the instance, not a mask lane on the
master.

---

## 3. Where it lands, lane by lane

The first table is R7.1's, kept as the record of that task; where a row has since been
superseded the second table says so.

| Lane | Change |
|---|---|
| `pipeline/unreal/make_v2_materials.py` | `_build_water` → SLW graph (§4); `make_water` sets `shading_model` + `BLEND_Opaque`, drops the refraction / translucency-lighting properties; `GRAPH_VERSION` 9; `REQUIRED_MPC_SCALARS` + `WaterFogScale`; `M_ElysiumUnderwater` |
| `pipeline/unreal/make_surface_knobs.py`, `UElysiumSurfaceSettings` | the `WaterFogScale` row |
| `importers/materials.py` | `Underside` switch from a self-referencing `$bottommaterial`; the normal lane fix (§4.4); pin table + `ElysiumSurfaceParams.h` + `WATER_PARAM_TABLE` gain `Underside` |
| `importers/map_geometry.py` | `water.volumes[]` (§5), `MANIFEST_VERSION` 9 |
| `pipeline/unreal/bake_map_v2.py`, `bake_map.py` | `_place_water`: one actor, the rows written as `FElysiumWaterVolume` structs; `TAG_WATER`; `WATER_ACTOR_SHAPE`; the rows in `_level_recipe` |
| `Source/ElysiumUE` | `AElysiumWaterVolumes` (+ `FElysiumWaterVolume`, `FElysiumWaterBrush`), the `elysium.water` tag, `UElysiumMapVisuals` adoption, `AElysiumMapActor::PreMoveTick` classification, the post-process interface, `UnderwaterDepth` |
| Tests, at the seam, in the count the change authorizes | `test_make_v2_materials_editor.py`: the water row is Opaque + SLW with the four output pins fed; `test_materials_stage.py`: a water VMT stages `NormalMapFrames` from `$normalmap`'s array with `UseNormalMap` on, and a self-bottomed unit sets `Underside`; `test_map_geometry.py`: the volume rows (sentinel dropped, fog keys per row, planes in cm); `Elysium.Substrate.Water`: the three-point classification against a fixed brush set |
| Docs | this file; `docs/vtmb/water.md` (the engine facts); `seam_map_material.md` → `M_V2_Water`; `seam_map_map.md` → "Import — water volumes (R7.1)"; `seam_migration.md` R7.1 |

### The water-complete pass, lane by lane (2026-09-04)

| Lane | Change |
|---|---|
| `formats/map_glb/lumps.py`, `decode.py` | the `dface+96` split (`origFace` / `numPrims` / `firstPrimID`); `primitives[]` gains `typeName` / `reachedBy` / `vertexAttributes`; map seam SCHEMA_VERSION 1.1.0 (census G13) |
| `formats/material_glb/`, `exporters/validation/material_glb.py` | `materialReferences[]`; `$bottommaterial` / `$crackmaterial` / `$modelmaterial` / `$leaknoise` leave `textureBindings[]`; material seam 1.2.0 (G16) |
| `formats/texture_glb/decode.py`, `importers/textures.py` | the mip-chain recovery (577 of 592 units, both pier probes); `flagNames` / `unnamedFlagBits`, `vtf_flag_names`, the `NORMAL` bit as a role-conflict tie-breaker only (G5b, G15) |
| `importers/map_geometry.py` | `MANIFEST_VERSION` 11; `section_key` / `split_section_key` (`#underside`, `#style<n>`); `face_underside`, `face_light_style`; `water.faces[]`; `fluid` / `pieces` / `leafBoxesCm` / `nearBoxesCm` / `materialTableWaterIndex` on each volume; `meshed_area_cm2` (§5.1) |
| `importers/map_visibility.py` (new) | `read_visibility`, `MapVisibility.pvs_union` — the visibility sub-unit's first reader, the derivation of the near-water set (G9, G23) |
| `exporters/UE_map_sidecars.py`, `importers/map_collision.py` | the `%compilewater` nodraw exemption (ruling K); sky-entity hulls are not composed into collision (G25) |
| `importers/materials.py` | `UNDERSIDE_INSTANCE_SUFFIX` / `underside_asset_path_for` / `underside_entry` and the `undersideAsset` field; `effective_family` + `_apply_compile_water_reroute`; `EnvMapContrast`; `_record_authored_then_removed` |
| `pipeline/unreal/make_v2_materials.py`, `matgraph.py` | `GRAPH_VERSION` 11; `DuDvMapFrames` + `T_V2_DefaultDuDvFrames` + `Graph.normalize`; `EnvMapContrast` on the Lit pair; `LIGHT_STYLE_CPD_SLOT = 6` and `LightStyleBrightness` on Lit / LitTranslucent / TwoTexture / Water |
| `pipeline/unreal/bake_map.py`, `bake_map_v2.py`, `bake_verify.py` | `chunk_style_suffix` / `parse_chunk_style` / `chunk_actor_tags` and the `elysium.style=<n>` tag; `set_fog` stamping slot 6 = 1.0; `_V2Material.slot_asset` resolving the `_Underside` twin; `_place_water` writing the four new struct fields, `WATER_ACTOR_SHAPE` 2, `MANIFEST_VERSION` 11; `_verify_water_section_bindings` |
| `exporters/particle_glb.py` | `RETAIL_PROVENANCE_DIVERGENCE_UNITS` — the one-key retail override for `waterbigsplash_emitter` (ruling N.3) |
| `Source/ElysiumUE` | `FElysiumWaterFluid`, `Pieces` / `LeafBoxesCm` / `NearBoxesCm` and `FindNearVolumeAt` / `IsNearWater`; `ElysiumLightStyle` + `UElysiumLightRig::AdoptStyledPrimitives`; `ElysiumWater::DecideSplash` and `AElysiumMapActor::RaiseWaterSplash`; `ElysiumWaterAudio`; the buoyancy trio and `TickFluidBodies` |
| Docs | this file (§1.1); `docs/vtmb/water_data_census.md` (the evidence); `seam_migration.md` → "R7.1 — settled" (the G1–G26 table); `effects_authoring.md` (the water rows) |

---

## 4. The surface: `M_V2_Water` as Single Layer Water

### 4.1 Flags

`MD_Surface`, `BLEND_Opaque`, `MSM_SingleLayerWater`, `two_sided` off, `used_with_instanced_
static_meshes` on, `used_with_nanite` **off** (Nanite rejects the shading model; the water faces
already live in the non-Nanite `T_` chunk bucket and `MaterialBinding.opaque` already answers
`False` for this master). The material lane's per-instance `Opaque` blend override — the
fall-through of `_resolve_blend` for a VMT that authors no `$translucent` — becomes the master's
own truth instead of an override the translucent master could not honour. No `bUsedWithWater`:
the shading model does not require it (§8).

### 4.2 The graph, pin by pin

Numbers stay authored on the instance (the pin table is unchanged but for `Underside`); the
translation happens in the graph.

`Underside` is read straight off the unit's own **VMT provenance rows** (`$bottommaterial`), not
`params.material_refs`: the GLB decoder emits a `dependencies[]` row only for a texture-shaped
value, so `$bottommaterial` reaches no dependency on any of the 26 units that author it (measured
on the corpus) and a `material_refs` lookup was dead. The switch is true when the authored value,
normalised (`\`→`/`, `.vmt` stripped, case-folded), equals the unit's **own** material key —
`dev/dev_waterbeneath2` names itself, so it is its own underside — false otherwise, and unset on a
patched depth instance whose provenance carries only its `insert` delta.

| Source | SLW / material input | Graph |
|---|---|---|
| `$fogcolor` (authored /255), `$fogstart`, `$fogend` (Source inches) | **Absorption / Scattering Coefficients** (1/cm) | `range = max((FogEnd − FogStart) × 2.54, 1)`; `σ = WaterFogScale / range`; `c = pow(FogColor.rgb, 2.2)` (the same decode `ElysiumFog::DecodeColor` applies to the scene fog); **Scattering = c × σ**, **Absorption = (1 − c) × σ**. `UseFogEnable` off → both 0 (`$fogenable 0`: `FogMode(0)`, clear water). `Underside` → both 0 (ruling E). `CheapWater` → both 0, the colour moving to Emissive and Opacity to 1 (ruling F) |
| `$refracttint` | **Color Scale Behind Water** | `RefractTint` (default white). SLW fades it in over the first 50 cm of depth; on a 20-inch canal that is the whole depth |
| — | **PhaseG** | 0. VtMB has no phase term |
| `$normalmap` (`dev/water_normal`, 29 frames) + the `animatedtexture` proxy (30 fps, on `$bumpframe`) + `texturescroll` (~0.05 @ 45°) | **Normal** | the existing lane: `_flipbook_sample(NormalMapFrames, NormalFrameRate/Count)` under `UseAnimatedNormalFrames`, gated `UseNormalMap`, over the `BumpScrollRateU/V` panner — then tilted by the reflection warp below and renormalised |
| `$bumpmap` (`dev/water_dudv`, 29 frames) × `$refractamount` | **Refraction** (`RM_2D_OFFSET`) (ruling O) | `dudv × RefractAmount × WaterWarpScale`, gated `UseAnimatedDuDvFrames`: the refract pass's own screen-space warp (`texbem`, `water_dx80.cpp::DrawRefraction`), which SLW's base pass applies through `ComputeBufferUVDistortion` scaled by `saturate(thickness_cm / 30)` |
| `$bumpmap` × `$reflectamount` | **Normal**, tilt (ruling O) | `normalize(N + dudv × ReflectAmount × WaterReflectWarpScale)`: the reflect pass's warp of the mirror image, as a reflection-direction tilt (named modernization). The plain 2D `DuDvMap` slot stays declared — every corpus DUDV is an array |
| `$reflecttint` | **Specular** scale | `Specular = class_specular × luma(ReflectTint)`; `Underside` → 0 **and Roughness → 1** (the engine's strip; a mirror Lumen still resolved off a smooth surface would put it back). Luma only: SLW's Specular is scalar (§9) |
| Fresnel (`(1 − N·V)^5`, R0 = 0: PS `c3 = (1,0,0,0)`) | — on the expensive path; **the cheap pass's own Fresnel** on Emissive | SLW applies its own Schlick from Specular on the expensive path. The cheap pass's Fresnel node has exponent 5 and `BaseReflectFract` as its base fraction — that parameter's VtMB meaning |
| `$envmapcontrast` (on the Lit pair, not this master) | **Emissive** cube term | `lerp(cube, cube × cube, EnvMapContrast)` on the fixed-cube branch only, default 0 (census G5). An `env_cubemap` unit has no sample to contrast; its reflection is Lumen's |
| a face lightstyle | **Base Color** and **Emissive** scale | `LightStyleBrightness`, Custom Primitive Data slot 6, default 1.0, written each tick by the light rig on every component the bake tagged `elysium.style=<n>` (ruling M) |
| surface class `water` | class specular / metallic | the class LUT read for Specular and Metallic. **Roughness is 0.05** (ruling O): `_rt_WaterReflection` is a planar mirror render, and Lumen forces mirror reflections on SLW anyway; 1 under `Underside` |
| `$envmap` — the authored cube, else the map's VBSP-patched probe, else `engine/defaultcubemap` | **Emissive** (the cheap pass, ruling O) | `(fog_decoded + cube(reflect(V, N)) × EnvMapTint × fresnel^5) × blend`, `blend = saturate((dist_in − CheapWaterStartDistance) / (End − Start))`, 1 under `CheapWater`; the cube gated `UseFixedCube`, which the stage sets on every water instance that is not `$forceexpensive` |
| `$basetexture`, `$color`, `$watercolor`, `$watermurkiness` | **Base Color** | **black** unless a base texture is bound (ruling O — water has no diffuse term); `lerp(base × Color × RefractTint, WaterColor, WaterMurkiness)` when one is |
| — | **Opacity** | **coverage, not identity** (`WaterVisibility = 1 − Opacity`, §8): `UseBaseTexture ? Alpha × BaseTexture.a : 0`. `BaseTexture` is **not a required slot on this master** (`REQUIRED_TEXTURE_SLOTS["M_V2_Water"] = frozenset()`, R7.1 follow-up, 2026-09-04): the surface's identity is the volume, the reflection and the refraction, none of which read the base texture, so a unit whose `$basetexture` cannot bind stages with `UseBaseTexture` off and draws as water like every other water unit by default, instead of failing the unit outright. This is what unblocked `dev/ocean`/`dev/oceanbeneath` — the DX6 fallback sheet, a 29-frame VTF the master has no frames lane for — which previously refused to stage at all. The four base-textured `Water` units that do bind (`dev_water`, `nether01_water`, `oilfieldwater a/b`) are placed on no water map; they keep their alpha as coverage, provenance-level only |
| `$cheapwaterstartdistance` / `$cheapwaterenddistance` | **Opacity** (coverage) and the Emissive blend | `saturate(textured coverage + blend)`: past `End` nothing refracts through, as under the 2004 SRC_ALPHA overlay; a `$forcecheap` unit never refracts. The old fog tail on Emissive / Opacity is gone — absorption is the fog |
| the wave scalars, `WaterDepth` | — | declared, not wired (`WaterDepth` is VBSP's per-instance depth; the volume carries the real one) |

The SLW output node is `MaterialExpressionSingleLayerWaterMaterialOutput`, created through the
same `mel.create_material_expression` the thin-translucent output already uses
(`make_world_materials.py::M_World_Glass`), fed by pin name (`ScatteringCoefficients`,
`AbsorptionCoefficients`, `PhaseG`, `ColorScaleBehindWater`). The all-switches-true probe
(`_probe_all_switches_true`) must still compile: `Underside` and `CheapWater` both on is a legal
permutation (zero extinction wins; the instance never authors both).

**Why 2·ln 2.** VtMB's fog is linear: fully `$fogcolor` at `$fogend`, half at the midpoint.
SLW's is exponential: transmittance `e^(−σ·d)`. No `σ` makes the two curves coincide; matching
the half-fog distance (`e^(−σ·end/2) = ½ → σ = 2 ln 2 / end`) keeps the *reading* of a canal —
how far in you can see — while the far end stays a little more transparent than 2004's hard
clamp. That residual is the knob's business, not the graph's.

### 4.3 What the six authored fog tuples become (1/cm, at `WaterFogScale` 1.386)

| Material | `$fogcolor` | `$fogend` (in) | range (cm) | σ | decoded `c` |
|---|---|---|---:|---:|---|
| `sewer_water` (Santa Monica sewer, end sequences) | `{5 5 0}` | 1024 | 2598 | 5.3e-4 | (1.7e-4, 1.7e-4, 0): black-green murk, nearly pure absorption |
| `warrwater` (downtown canal, warrens, ash sewer, plaguebearer) | `{.2 .4 .2}` | 1024 | 2598 | 5.3e-4 | ≈ 0: pure absorption |
| `warrenwater` (Hollywood, Chinatown canals, warrens 5) | `{.4 .4 .2}` | 128 | 322 | 4.3e-3 | ≈ 0: short, black |
| `pool_water` (Vesuvius, Giovanni, temple, plaguebearer pool) | `{21 39 20}` | 800 | 2029 | 6.8e-4 | (4.5e-3, 1.7e-2, 4.0e-3): a basin you can see into, green |
| `dev_water2*`, `dev_waterbeneath2`, `cheap_water`, `invisible_water` | `{22 20 10}` | 400 | 1013 | 1.4e-3 | (5.4e-3, 4.4e-3, 1.0e-3): warm grey |
| `spawnwater` (warrens 5) | `{5 0 0}` | 64 | 160 | 8.7e-3 | (1.7e-4, 0, 0): red-black, gone in a metre and a half |

Did-it-appear is the only acceptance here. The magnitudes are the authored ones through one
formula; whether a sewer reads too black against a capture is the tuning session's question.

### 4.4 The normal lane was wrong, and how it is fixed

Both `dev/water_normal` and `dev/water_dudv` are 29-frame VTFs and stage as `Texture2DArray`s
(`TA_water_normal` + its `_linear` twin, `TA_water_dudv`), so neither binds to the master's 2D
`NormalMap` / `DuDvMap` slots (`textureClassMismatch`, correctly). The `animatedtexture` proxy
then bound the **DUDV** array into `NormalMapFrames` (it looked its dependency up under `DuDvMap`,
the slot `$bumpmap` lands on for the water family) and never set `UseNormalMap` — so every water
instance shipped with a **flat normal**: no ripple at all, on either lane. R7.1's stage fix: on the
water family the frames array comes from `$normalmap`'s own texture (the proxy still supplies the
rate — `$bumpframe` is the shared frame index of both textures in `Water_Old`), and a bound frames
array on the normal lane sets `UseNormalMap` for every family, not only through the static-frame
fallback.

**And the DUDV half landed in the water-complete pass** (census G3): `DuDvMapFrames` is a real
array parameter now, bound from `$bumpmap`, animated by the same `$bumpframe` proxy rate, and
folded into the normal (§4.2). Two things about it are worth keeping written down. The 2D
`DuDvMap` slot stays declared and unbound — every DUDV in the corpus is a 29-frame VTF, so there
is nothing for a `Texture2D` slot to hold. And the master's default array cannot be
`T_V2_DefaultNormalFrames`: that asset is `TC_NORMALMAP` where this lane samples
`SAMPLERTYPE_LINEAR_COLOR` (because `TA_water_dudv` stages `TC_VECTOR_DISPLACEMENTMAP`), and
either pairing is a real sampler-type compile error — hence the generated
`/Game/ElysiumGenerated/Materials/V2/T_V2_DefaultDuDvFrames`, a 2-slice `(128,128,255,255)` DX10
DDS with sRGB off and no mips. One further consequence of the texture lane's `NORMAL`-bit fix
(census G15): `dev/water_normal` now stages as a single `TA_water_normal` with no `_linear` twin,
so that — not `TA_water_normal_linear` — is the asset the normal lane binds.

### 4.5 Ruling J — `sm_pier_1` and the `SineUVTranslate` lane

`sm_pier_1`'s "wave system" is not on the water master at all: it is 17 `objects/surf` cards
(`M_V2_Lit`/`M_V2_LitTranslucent`, an `lightmappedgeneric` shape, not `water`) plus a `blackwater`
LMG ocean card with a 24 fps normal flipbook and the `invisible_water` volume the pier walks off
into (§2's family table). The cards' authored breathing — alpha pulse (already wired) and a UV
slide — was `$temp` → `texturetransform` provenance-only before R7.1: a `sine` proxy wrote its
result into a scratch `$temp*` var and a `texturetransform` consumed it as `translatevar`, and
nothing on the Lit masters read that chain.

A new vector, **`SineUVTranslate`** `(ampU, ampV, offU, offV)`, default `(0,0,0,0)` (neutral by
construction — every non-surf Lit instance is untouched):

- `_SINE_UV_LANE` merges `{"SineUVTranslate": "V"}` into the shared `M_V2_Lit` construction dict
  (`LitTranslucent` is the same dict object, so it gains the vector too);
  `LitParams.Vectors.SineUVTranslate` (`make_v2_materials.py`); `ElysiumSurfaceParamsLit::
  Vectors::SineUVTranslate` (`ElysiumSurfaceParams.h`).
- **Stage, two passes** in `_apply_proxies`: the loop pass records a `sine` whose `resultvar` is
  `$temp`/`$temp[i]` as `temp_sines[base][i] = (min, max)` and emits nothing for it; a
  `texturetransform` row is collected but not yet resolved. After the loop, a resolution pass
  matches each `texturetransform` whose `resultvar` is `$basetexturetransform` and whose
  `translatevar` names a recorded `$temp*`, and emits `SineUVTranslate = [maxU−minU, maxV−minV,
  minU, minV]` (each sine's component picks U or V; an unwritten component stays 0) with the
  provenance row's `destination: "graph"`. Every unconsumed `$temp*` sine, a `rotatevar`, `$tempvec`,
  or any other target keeps the pre-existing `proxyTargetProvenanceOnly` omission — reading VMTs
  order-independently: the transform may author before or after the sine it consumes. Two sines
  feeding different `sineperiod`s stage the first and name `sineChainPeriodMismatch`.
- `_drop_unexposed_sine_uv` runs immediately after `_apply_water_underside` and removes the vector,
  named, on any master that does not expose it (Unlit and the rest) — the chain resolves the same
  way regardless of the consuming unit's family, and only the master decides whether the number
  ships.
- **Graph** (`_build_lit`): the sine lane (`_sine_lane`, returns the normalized `wave` term) runs
  *before* the UV lanes; `base_coord = transformed_uv + (SineUVTranslate.rg × wave +
  SineUVTranslate.ba)` feeds the **base**-texture panner only — the bump panner keeps the plain
  `transformed_uv` it always had. Unlit / TwoTexture / Sprite / Water graphs are unchanged (the
  argument defaults to `None`).
- `sm_pier_1`'s surf chain stages `SineUVTranslate = [0.5, 0, 0, 0]`, `SineTargetMask = [1, 0, 0,
  0]`, `SinePeriod = 15` — the same 15 s breathing the alpha pulse already used, now also sliding
  the card up the sand.

---

## 5. The volume: `water.volumes[]`

### 5.1 The product (`seam_map_map.md` → "Import — water volumes (R7.1)")

One row per real `LEAFWATERDATA` record, in lump order:

```
"water": {
  "volumes": [
    {
      "index": 0,                        // the leafData row
      "surfaceZCm": -14937.74, "minZCm": -14988.54,
      "material": "vtmb:material:water/sewer_water",   // through surfaceTexInfoID -> texinfo -> texdata
      "fogEnable": true, "fogColor": [0.0196, 0.0196, 0.0],  // authored /255, undecoded (the `.env` convention)
      "fogStartCm": 2.54, "fogEndCm": 2600.96,
      "brushes": [ { "planes": [[nx, ny, nz, d], ...], "boundsCm": {"min": [...], "max": [...]} } ],

      // added by the water-complete pass (contract 4)
      "fluid": { "index": 5, "density": 1000.0, "damping": 0.01,       // or null when unauthored
                 "surfacePlane": [0, 0, 1, -14937.74],                 // Unreal cm
                 "currentVelocityCm": [0, 0, 0], "contents": 268435488,
                 "surfaceProp": "water" },
      "pieces": [ { "planes": [...], "boundsCm": {...} } ],            // the compiler's own convex solids
      "leafBoxesCm": [ {"min": [...], "max": [...]} ],                 // the LEAFWATERDATA leaves
      "nearBoxesCm": [ {"min": [...], "max": [...]} ],                 // the AABBs of union-PVS(water clusters)
      "materialTableWaterIndex": 17                                    // or null (the hub has no water row)
    }
  ],
  "faces": [                                                           // one row per %compilewater face, in face order
    { "index": 8197, "scene": "world", "group": "vtmb:material:water/invisible_water#underside",
      "unit": "water/invisible_water", "underside": true, "lightStyle": null, "lightStyles": [],
      "surfaceFogVolumeID": 0, "texInfo": 2425, "texdata": 37,
      "plane": [...], "side": 1, "normal": [...], "primitive": {"first": 0, "count": 0},
      "area": 26112.0, "areaCm2": 168465.6, "meshedAreaCm2": 168465.6, "triangles": 2 }
  ],
  "dropped": [ {"index": 3, "reason": "sentinel"}, {"index": 0, "reason": "no water brush"} ]
}
```

`counts` gains `waterFaces`, `waterUndersideFaces` and `lightStyleGroups`. Measured across the
converted maps: `sm_hub_1` 47 water faces / 24 underside / 1 volume / 1 style; `sm_pier_1` 50 / 18 /
1 / 2 styles over 211 styled groups; `sp_soc_3` 62 / 27 / 1 / 2 styles; `sp_tutorial_1` 0 water and
110 styled groups; `sm_pawnshop_1` 0 and 0.

- **`fluid`** is the map's `PHYSCOLLIDE` keyvalue tail (`physics.models[].keyValues.fluid`), staged
  because VtMB's own creation guard is `fluid.index > 0` — not contents, not solid flags — so both
  owner maps get a controller (§1.1 L, census G7).
- **`pieces`** carries the same row shape as `brushes[]` deliberately, so one runtime struct serves
  both; `ElysiumWater::FindVolumeAt` tests them ahead of the brushes when a volume publishes any.
- **`nearBoxesCm`** is derived from the visibility sub-unit (`map_visibility.pvs_union`), never read
  off the leaf `0x200` bit — see ruling N.4 for why the pier forces that.
- **`faces[]`** exists so the underside split, the lightstyle split and the area pin are all
  answerable off one product. `area` is vbsp's own per-face number (in², census G26) and
  `meshedAreaCm2` is the area of the triangles the port actually emits; the corpus test compares
  them per face **and** per section (worst face 3e-5 relative, every section 1e-7).

- **Rows.** `surfaceTexInfoID == −1` (the `16384` sentinel, two rows in `hw_warrens_2`) is
  dropped. A row no water brush matches (`la_bradbury_3`: `tools/tools_shadow`) is dropped and
  named.
- **Brushes.** `collision.brushes[]` with `contents & 0x20` **and** at least one non-bevel side
  whose texinfo material authors `%compilewater` (the material lane's provenance carries the key).
  `0x18000120` with only `tools/tools_shadow` sides is a shadow caster and never qualifies;
  `0x18000020` (`func_detail` water, `ch_lotus_1`) does. A brush belongs to the row whose
  `surfaceZ` its horizontal top plane matches within one inch — the association needs no leaf
  join, so `ch_fulab_1`'s `func_illusionary` water (no leaf points at its row) still lands.
- **Planes.** Outward normals and distances in Unreal cm (`n · p = d` on the plane, `n · p < d`
  inside), through the same reflection the geometry takes; bevel sides skipped (redundant
  half-spaces of the same hull). The AABB comes from the plane-intersection vertices.
- **Fog keys** are read off the volume's material unit's own **VMT provenance rows**
  (`$fogenable`/`$fogcolor`/`$fogstart`/`$fogend`), not the staged instance — `invisible_water`
  and `cheap_water` drop the keys from their staged row but still author them. A **patched**
  unit's own provenance carries only its `insert` delta, so the read walks the same `patchBase`
  chain `resolve_material_table` already walks (base then delta, the same `hops > 8` guard) —
  `ch_fulab_1`'s `cheap_water_1318_1990_273` reaches its fog keys and its `%compilewater` bit only
  this way. Start/end are converted to cm here because the actor stores final values. A material
  with no fog keys stages `fogEnable: false` — `SetFogVolumeState`'s own answer.

### 5.2 The actor

```
USTRUCT() FElysiumWaterBrush   { TArray<FPlane> Planes; FBox BoundsCm; }
USTRUCT() FElysiumWaterFluid   { bool bHasFluid; int32 Index; float Density; float Damping;
                                 FPlane SurfacePlane; FVector CurrentVelocityCm; int32 Contents; }
USTRUCT() FElysiumWaterVolume  { int32 Index; float SurfaceZCm; float MinZCm; FString Material;
                                 bool bFogEnabled; FLinearColor FogColor; float FogStartCm; float FogEndCm;
                                 TArray<FElysiumWaterBrush> Brushes;
                                 FElysiumWaterFluid Fluid;               // -> Python `fluid`
                                 TArray<FElysiumWaterBrush> Pieces;      // -> `pieces`
                                 TArray<FBox> LeafBoxesCm;               // -> `leaf_boxes_cm`
                                 TArray<FBox> NearBoxesCm; }             // -> `near_boxes_cm`
UCLASS()  AElysiumWaterVolumes : AActor, IInterface_PostProcessVolume
          { UPROPERTY(EditAnywhere) TArray<FElysiumWaterVolume> Volumes;
            FElysiumWaterSplashSignature OnSplash;                       // (ESplash, LocationCm)
            void TickFluidBodies(float DeltaSeconds); ... }
```

Every added field is `EditAnywhere`, and `Elysium.Substrate.WaterActor` asserts each is reflected
**and** carries `CPF_Edit` without `CPF_EditConst` — so a `set_editor_property` that cannot land
fails a test instead of going quiet.

- `int32 FindVolumeAt(const FVector& PointCm) const` — first volume with a brush containing the
  point (bounds test, then planes). This is `MASK_WATER` at a point.
- `EElysiumWaterLevel ClassifyBody(FeetCm, WaistCm, EyesCm) const` — the three queries of
  `CheckWater`, in its order: feet → 1, waist → 2, eyes → 3, `None` when the feet are dry.
- Editor-only debug: the brush AABBs drawn as boxes at the plane colour under `elysium.Water.Draw`
  ("the editor is the tuning surface": a volume is inspectable where it stands).
- Adopted by `UElysiumMapVisuals::AdoptBakedLevel` off `elysium.water`
  (`ElysiumBakedTags::Water`), exposed as `GetWaterVolumes()`.

### 5.3 The body (ruling C)

`AElysiumMapActor::PreMoveTick`, after the player think and before the move:

```
feet  = hull centre − (0, 0, GetBodyHalfHeight()) + 1 in   // `origin.z + mins.z + 1.0`
waist = hull centre                                        // `(mins.z + maxs.z) × 0.5`, not the eye
eyes  = the camera component's location                    // `origin + m_vecViewOffset`
Movement->SetWaterLevel(Water->ClassifyBody(feet, waist, eyes))
```

`FullWalkMove` already branches on `WaterLevel >= Waist` (skips gravity, `WaterMove`), the anim
intent already answers `Swim` / `Treadwater`, and `CheckJumpButton` refuses at level ≥ 2. None of
that changes; this is its first caller. No save state: the level is re-derived on the first tick
(`FElysiumWorldBlock` carries no movement state, by rule). `CONTENTS_CURRENT_*` (a base-velocity
push at `level × 50`) is not authored on any water brush in the corpus and is not reproduced.

---

## 6. The eye: underwater (ruling D)

VtMB under the plane is **two passes and one fog**: the above-water world plus the 2D sky (with
the world fog) into the refraction RT clipped at the plane, then the below-water world plus the
water surfaces into the framebuffer under `SetFogVolumeState(id, false)` — plain linear fog over
everything, `FogMode(0)` after. No warp, no tint, no reflection pass (`docs/vtmb/water.md`).

- **`M_ElysiumUnderwater`** — `MD_PostProcess`, before tonemapping, three parameters
  (`FogColor`, `FogStart`, `FogInvRange` — the same triple `ElysiumFog::ApplyToDecalMID` writes,
  so one packer serves the scene fog, the decals and the underwater view):
  `lerp(SceneTexture:PostProcessInput0, FogColor, saturate((SceneDepth − FogStart) × FogInvRange))`.
  Generated beside the V2 masters; the runtime holds one MID.
- **Registration.** `AElysiumWaterVolumes` implements `IInterface_PostProcessVolume`
  (`bIsUnbound = false`, `BlendWeight = 1`, `Priority = 1` above the map's neutral unbound
  `elysium.ppv` at priority 0, a fixed `VolumeGuid` set in the constructor). Registered with
  `UWorld::AddPostProcessVolume` on `BeginPlay`, removed with `RemovePostProcessVolume` on
  `EndPlay`. The engine's own `UPostProcessComponent` and the Water plugin's
  `UUnderwaterPostProcessVolume` are the two precedents; neither needs the plugin.
- **The gate is `bIsEnabled`, not `EncompassesPoint`'s return.** `UWorld::DoPostProcessVolume`
  discards a bounded volume's `EncompassesPoint` return and blends on the distance it writes
  alone (`World.cpp:10710-10741`), so `EncompassesPoint` always writes distance 0 (full weight, no
  soft edge — a partial blend of a fog lerp would read as haze, not water) and returns
  `Properties.bIsEnabled`, which `UpdateViewPostProcess` sets per view from
  `FindVolumeAt(ViewLocation) != INDEX_NONE` inside the `OnBeginPostProcessSettings` handler —
  broadcast before the engine's volume walk, so the bool is already correct when `EncompassesPoint`
  runs. The same handler writes the found volume's fog triple (`FogColor` decoded, `FogStart`,
  `FogInvRange`, packed by `ElysiumFog::Pack`) to the one `UnderwaterMID`, created lazily in
  `BeginPlay` from `/Game/ElysiumGenerated/Materials/V2/M_ElysiumUnderwater` and added to the
  settings once via `AddBlendable`; a checkout that has not exported the masters logs a warning and
  still classifies the body and writes `UnderwaterDepth`, just with nothing to fog with.
- **`SceneView->WaterIntersection`** is set alongside `UnderwaterDepth` (`-1.f` when the view is
  outside every volume, the engine's own "out of water"):
  `EViewWaterIntersection::InsideWater` when the view is in a volume, else `OutsideWater` — the
  same two writes `UWaterSubsystem::ComputeUnderwaterPostProcess` makes, both for pass-ordering
  only (5.8's `CameraIsUnderWater` is compile-time false; the look itself is the post-process
  above).
- **`FSceneView::UnderwaterDepth`**, the other write the same handler makes, is
  `SurfaceZCm − ViewLocation.Z` when the view is in a volume, else `-1.f`.
- **Why a post-process, when the project's fog is per-primitive.** `rebuild-strategy.md`'s rule
  ("the distance fog cannot be an engine fog") exists because the world and the 3D-skybox
  miniature share screen depth. Under the plane that distinction is moot: everything above the
  water — sky, miniature, buildings — is seen *through the surface*, and SLW writes the surface's
  depth, so a scene-depth fog fogs the whole above-water view at the plane's distance, which is
  exactly what pass 2 does to the surface in 2004. The below-water geometry is fogged at its own
  depth, also faithful. What the post-process cannot do is *replace* the world fog on the
  below-water geometry (it adds to it); §9 records that.
- **Selection.** VtMB picks one fog volume per frame by a front-to-back PVS walk from the eye
  leaf, and "eye under water" is the eye leaf's own `leafWaterDataID`. Nothing in the corpus
  shows two volumes at once; the transcription is "the volume the view point is in" (§9).

---

## 7. The camera (built)

`CViewRender::GetWaterOffset` (`cl_waterdist` 4 in) walks the view origin in one-unit Z steps
against `MASK_WATER` when the player's water level is above 1: at level 2 it **raises** the view
until it is out of the volume (treading: the camera stays dry), at level 3 it **lowers** it until
it is back inside (submerged: the camera stays wet) — the opposite of what the earlier VtMB note
said. The Elysium camera is a third-person boom most of the time, so the case is rarer than in
2004, but a boom that crosses the plane while the body treads would flicker the post-process.

Owner call (2026-09-04 review): **build now**. `ElysiumCam::SolveWaterOffset(WaterLevel, ViewZ,
SurfaceZ, WaterDistCm)` (`Public/ElysiumCameraSolve.h`, `Private/Player/ElysiumCameraSolve.cpp`) is
the step rule in closed form — level 2 clamps `SurfaceZ + WaterDist − ViewZ` to `[0, WaterDist]`,
level 3 clamps `SurfaceZ − WaterDist − ViewZ` to `[−WaterDist, 0]`, everything else is 0 — beside
`ElysiumRig::SolveBoomDistance`, under a new `cl_waterdist` cvar (`FElysiumCameraCvars::WaterDist`,
default `4 * ElysiumCam::U`). `UElysiumCameraComponent::ApplyBaseToView` applies it as a Z-only nudge
to `View.Location` **after** the boom-blend block closes (`ElysiumCameraComponent.cpp:278`, the block closing at `:273`) rather
than inside it, because the case it exists for — a treading or swimming body — is first person,
where that block's blend weight is 0 and it never runs. The level and surface plane are a push, not
a query: `AElysiumMapActor::UpdatePlayerWater` classifies the body each pre-move tick and calls
`UElysiumCameraComponent::SetWaterState(Level, SurfaceZCm)` on the body's own camera component,
because the camera reaches only its owner and the water volumes belong to the map actor.
Divergence: the 1-unit quantization is dropped — the loop's only purpose is the clearance distance,
and the closed form is that distance exactly rather than rounded up to the next inch (§9.12).

---

## 8. Engine facts (UE 5.8, verified in this install's source)

Where the earlier text of this document was wrong, the line says **[was wrong]**.

- **SLW is engine, not plugin.** `MSM_SingleLayerWater` + `MaterialExpressionSingleLayerWaterMaterialOutput`
  live in `Engine/`; the Water plugin only uses them. Plugin content is mounted only when the
  plugin is enabled (`FPluginManager::MountContentPlugins`), so no `/Water/...` asset is
  referenceable while it stays off — the project authors its own post-process master.
- **Rules a SLW material must meet** (`MaterialShared.cpp` 6432–6449): opaque **or masked**, the
  only shading model, and the output node present. Nothing else: no two-sided, static-lighting or
  `bUsedWithWater` requirement. **Nanite rejects it** (`NaniteResources.cpp` 3407). **Pixel Depth
  Offset** and scene-depth reads are unsupported on SLW; `SceneDepthWithoutWater` is the node for
  a wall fade, later.
- **The camera-under-water branch is dead code.** `BasePassPixelShader.usf:1698`,
  `SingleLayerWaterComposite.usf:68`: `const bool CameraIsUnderWater = false;`. No cvar, flag or
  plugin path enables it. **[was wrong]**: "SLW handles the view from below through
  `bCameraIsUnderWater`". What exists is `FSceneView::UnderwaterDepth` (`SceneView.h:1793`), a
  CPU flag the renderer reads for pass ordering and fog, whose only stock writer is the Water
  plugin and which any `UWorld::OnBeginPostProcessSettings` subscriber may set.
- **Opacity is coverage.** `WaterVisibility = 1 − Opacity` (`BasePassPixelShader.usf:1140`);
  Opacity 1 removes the water volume. **[was wrong]**: "opacity high on blood / spawn".
- **Coefficients are 1/cm** (`SingleLayerWaterShading.ush:200`, depth in Unreal units).
  **[was wrong]**: "reciprocal metres". Color Scale Behind Water fades in over 50 cm (`:176`).
- **Lumen honours roughness on water.** Every SLW pixel traces regardless of roughness
  (`LumenReflectionCommon.ush:335`), but the ray uses the GBuffer roughness
  (`LumenReflections.usf:317/366/377`). **[was wrong]**: "forced mirror" — that is Epic's doc text
  for an earlier release, not 5.8's shader. `r.Water.SingleLayer.Reflection` 1 means "the scene's
  method" (Lumen, else SSR), 2 captures, 3 SSR. Planar reflections never feed SLW.
- **MegaLights does not shade water, but water is not dark.** `EMegaLightsInput` has no water
  member; SLW is lit **forward through the light grid** with MegaLights' lights still in it
  (`BasePassPixelShader.usf:1400/1478`, `bExcludeMegaLights = false`), so the neon *does* put an
  analytic specular lobe on the canal, unshadowed. **[was wrong]**: "what water can see is only
  the Lumen mirror".
- **Two-sided is allowed at raster level** (`SingleLayerWaterRendering.cpp:2140`) but the shading
  always assumes the camera is above — which is why ruling E zeroes the underside's extinction
  instead of trusting a branch that does not run.
- **Depth prepass** (`r.Water.SingleLayer.DepthPrepass`, read-only, default 1) requires VSM
  support; VSM *filtering* on water is off by default behind two more cvars
  (`ShadersSupportVSMFiltering` read-only 0, `VSMFiltering` 0). Night maps, local lights: left
  alone. **Velocity** moved to the prepass in 5.8 (`VelocityOutputPass` 1); `ForceVelocity` is
  deprecated. **No decals** land on SLW (the pass reuses the decal uniform slot). Refraction
  culling (`r.Water.SingleLayer.Refraction.*`) is off by default; not touched. No `r.Water.*`
  entry exists in `BaseScalability.ini`.
- **`IInterface_PostProcessVolume`** is base engine (`Interface_PostProcessVolume.h`), three pure
  virtuals (`EncompassesPoint`, `GetProperties`, `GetDebugName`); `UWorld::AddPostProcessVolume`
  is the 5.8 registration (`InsertPostProcessVolume` is deprecated). A bounded volume is skipped
  when `EncompassesPoint` says no and blends at `BlendWeight` when it says yes at distance 0.
- **Python authoring** sets `shading_model`, `blend_mode`, `two_sided` through
  `set_editor_property`; the output node's four inputs are bare `UPROPERTY()`s, so they are wired
  by pin name through `connect_material_expressions`, never as properties.

---

## 9. Named divergences (owner-visible, recorded here and in `docs/vtmb/water.md`)

1. **Two render targets → one SLW pass** (revised by ruling O). No planar camera; refraction reads
   the lit scene behind the surface, **warped by the DUDV in screen space exactly as the refract
   pass did** (the Refraction pin), reflection is Lumen's forced mirror, warped as a normal tilt.
   The 2004 reflection RT drew entities and the 2D sky lump, never a 3D skybox; Lumen sees what
   the scene has, including the placed miniature.
2. **Linear volume fog → exponential extinction**, half-distance matched by `WaterFogScale`.
3. **`$refractamount` / `$reflectamount` → the two warp pins** (revised by ruling O), each scaled
   by an `MPC_ElysiumSurfaces` knob (`WaterWarpScale`, `WaterReflectWarpScale`, 0.01 per authored
   unit) tuned against the owner's frames. `mat_waterswirl` (a 0.02 per-vertex swirl of the plane
   normal, CPU-side, pre-DX9 only) is not reproduced.
4. **`$reflecttint` → luma.** SLW's Specular is scalar; the red tint on blood and spawn water
   reaches the reflection only as brightness. The volume colour carries the red.
5. **Fresnel** is SLW's own Schlick from Specular, not `(1 − N·V)^5` with R0 = 0.
6. **Cheap water samples the authored cube** (revised by ruling O: the map's VBSP probe, or
   `engine/defaultcubemap`), blended by distance over every expensive unit as `Water_Old` does;
   the SM2 program is `fog + cube × fresnel` where the ps11 twin was a lerp — the two differ only
   at grazing angles. The rest of this entry is the history of how the fog colour got there.
   The first cut multiplied the extinction by 16 to make the body read as its `$fogcolor`, which
   was an invention and a wrong one: a scattering coefficient scatters whatever light reaches the
   water, so on an unlit stretch the body read **0**. Measured on `sp_soc_3` (§11), the same
   surface gave (50, 74, 78) where it mirrors a lit wall and (0.2, 0.6, 0.8) twenty metres away
   under an unlit ceiling, against the (22, 20, 10) VtMB draws everywhere. The corpus settles it
   rather than an owner call: `WaterCheap_ps11` is `lrp r0.rgb, fresnel, cube × $reflecttint,
   c0` with `c0 = $fogcolor` (`docs/vtmb/water.md` → "What the GPU draws"), it never binds
   `_rt_WaterRefraction`, and a water face is `SURF 0x408` = `WARP|NOLIGHT`, so the constant is
   emitted and nothing about it is lit. The master now transcribes that: coefficients 0, Opacity
   1 (no refraction shows through), `$fogcolor` on Emissive, Lumen's mirror arriving through
   Specular as the other half of the same lerp.
7. **Underside = no reflection** (revised 2026-09-04). Specular 0 and Roughness 1 on the
   `MI_<unit>_Underside` twin, keyed off the face's own plane normal, transcribing
   `Mod_LoadFaces`' `$reflecttexture->SetUndefined()`. The extinction is **kept** — R7.1's "zero
   extinction" is withdrawn (§1.1 E) — because the underside is the same body of water seen from
   its other side. What remains a divergence is that 5.8's SLW camera-under-water branch is dead
   code, so an underside face integrates the volume as if the eye were above it; the actual
   view-from-inside is `M_ElysiumUnderwater`.
8. **Underwater: the world fog is not suppressed.** VtMB replaces it below the plane; here the
   post-process adds the volume's fog on top of the per-primitive scene fog. On the corpus the
   volume fog's range (1.6–26 m) is far inside the world fog's, so the double term is invisible
   in practice; recorded, not compensated.
9. **Fog-volume selection**: point-in-brush of the view location, not a PVS walk.
10. **The surface takes no scene fog.** VtMB draws the surfaces under `EnableWorldFog()`; here
    the refraction and reflection carry the scene's own fog and the surface's scatter and
    specular do not fade with distance. A far canal end that pops against its walls is a
    tuning-session witness, and the fix — if one is wanted — is an additive fog-in on Emissive,
    not a CPD term.
11. **Opacity as coverage, and an unbindable base texture no longer fails the unit.** `BaseTexture`
    is not a required slot on `M_V2_Water` (R7.1 follow-up, 2026-09-04): the four base-textured
    `Water` units that do bind (`dev_water`, `nether01_water`, `oilfieldwater a/b`, none placed)
    keep their alpha as coverage, provenance-level only, and `dev/ocean`/`dev/oceanbeneath` — whose
    `$basetexture` is a 29-frame VTF the master has no frames lane for — now stage with
    `UseBaseTexture` off instead of refusing to stage.
12. **Camera water offset's 1-unit quantization dropped.** `SolveWaterOffset` (§7) is the closed
    form of `GetWaterOffset`'s step loop; the clearance distance is exact rather than rounded up
    to the next Source inch. Owner call: build now (§7).
13. **Water faces are lit.** VtMB's are `SURF_NOLIGHT` (no lightmap; the RTs carry the light);
    SLW is lit forward by the light grid and Lumen. This is the modernization, not a slip.

Added by the water-complete pass (§1.1):

14. **Withdrawn (ruling O).** A `%compilenodraw` water face draws nothing, as in VtMB; the
    `tools/` `$basetexture` rule on such a unit stands.
15. **Superseded (ruling O).** The two warps land on two pins: the refraction warp on the
    Refraction pin, the reflection warp as a normal tilt (that tilt is the divergence: there is no
    reflection image to displace).
16. **A lightstyle is a brightness, not a lightmap page** (ruling M, owner decision 4), one style
    per chunk, the lowest style winning where a face names several.
17. **The splash is bound to the water-level transition, not to a draw call** (ruling L), and the
    fluid controller's own stripped `PhysicsSplash` numbers are not reproduced.
18. **Buoyancy is Archimedes over the body's AABB**, spending the authored `density` and `damping`,
    because vphysics' fluid controller is not in the corpus (ruling L). Nothing displaces the
    surface — VtMB did not either, so that half is transcription.
19. **The near-water set is derived from the PVS**, never read off the leaf `0x200` bit (ruling
    N.4); measured set-equal to VtMB's own annotation where the annotation survives.
20. **`WaterMurkiness` stays wired**, the **`$bumpoffset` scroll is honoured**,
    **`waterbigsplash_emitter` comes from the retail pack member**, and **`sm_pier_1` stays on the
    UP recompile** — the four standing calls of ruling N, each with its measurement.
21. **`water/cheap_water` loses its `$envmapmask`** on the `%compilewater` reroute; recorded per
    unit as `compileWaterRerouteProvenanceOnly`, and already dead in 2004 on that unit because it
    authors `$forcecheap` (§2).
22. **Leaving the water is silent**, because `player/pl_wade2.wav` is a Half-Life 2 asset Troika
    never packed — the name is kept, the sound does not exist (§1.1 L).

Added by the look pass (ruling O, 2026-09-05):

23. **Water faces are lit, but with a black base**: the surface receives Lumen's specular and
    nothing diffuse, which is the closest a lit shading model comes to a `SURF_NOLIGHT` face.
24. **The cheap overlay's alpha is coverage**, not a blend of two colours: past
    `$cheapwaterenddistance` SLW shows nothing behind the surface, and the emitted `fog + glint`
    sits on a black lit base rather than over the refracted image fading out.
25. **`$fogcolor` bytes are emitted as `pow(byte / 255, 2.2)`**; VtMB wrote them to the framebuffer
    unchanged. The witness measures the basin against the owner's frame and the scale is the fix
    if the display transform crushes it.

---

## 10. Cost

One SLW mesh set per water material per map (usually one top and one underside), in the
non-Nanite bucket the bake already uses, drawn in the SLW pass after deferred lighting. The
post-process runs only while `EncompassesPoint` is true. No extra actors per brush, no plugin,
no Water Zone, no fluid sim. The Lumen trace on water is the same trace every reflective
surface already pays; VSM water filtering is left off. Budget instinct unchanged from before:
a hub with a canal is fine; a hub with a canal, a FLIP pool and a Water Body Ocean is not.

---

## 11. What to witness (did-it-appear only, per "wire first, tune later")

The maps R7.1 converted are `sm_hub_1` (a drawn `water/sewer_water` surface, staged plane
Z **−14937.74 cm**), `sm_pier_1` (the `water/invisible_water` ocean, staged plane
Z **−1582.42 cm**) and — added by the 2026-09-04 content run — `sp_soc_3`, the society basin:
one volume, `maps/sp_soc_3/dev/dev_water2_cheap`, plane Z **−609.6 cm**, floor `minZ`
**−1788.16 cm**, so **1178.56 cm deep** (464 in) against the pawn's 92.45 cm half-height — the
first staged volume deeper than the body, and the first on which the volume fog is a look rather
than a hand's breadth (`$fogcolor {22 20 10}`, `$fogend 400` in = 1016 cm, `$forcecheap`).
`la_hub_1` and `hw_warrens_5` are not on `MapsOnV2Models` and are a later witness.

**Witnessed 2026-09-04** (`uv run elysium run play`, both maps). Passed: the sewer surface draws
and is not black — the canal floor refracts through it and the wall lamps put specular streaks on
it; the body state reads `Feet` on the sewer floor (`locomotion.water: 1`, pawn settled at
Z −14896.28) and in the pier's ocean; the pier's ocean card and its 17 surf cards draw and the
surf band animates (34% relative pixel change at the waterline between consecutive frames against
1.6% on the static pier deck). Two things did not:

- **The underwater post-process looked unfogged** (§4.3 / ruling D) — and on re-witness
  (2026-09-04, second session) it is applied and correct; the first measurement was two artefacts.
  (1) The underside surface writes its own depth, so every pixel that sees the above-water world
  through the plane fogs at the *plane's* distance — 20 cm overhead, one to three metres along the
  rows just above the horizon — which a 2,600 cm range barely touches; this is also what VtMB's
  pass 2 does to the surface it draws. The below-water world fogs by its true depth, and in a
  50 cm canal that is a hand's breadth of floor, so the "far patch" the first witness measured was
  the surface at two metres. (2) The engine merges every blendable of one material into a single
  node whose parameters follow the highest-priority volume, so a test override pushed through the
  map's priority-0 volume was silently replaced by the water volume's own values under the plane.
  Proof: the same red 1 m override at priority 2 turned the whole below-plane band red at once
  (`E:/elysium-work/witness/r71_diag_under_red_prio2.png`), and the volume's own MID applied above
  the plane fogs the tunnel mouth at the sewer's numbers. The volume fog becomes a *look* only on
  a deep basin (`hw_warrens_*`, `la_hub_1`); on the two converted maps it is faithful and invisible.
- **`water/invisible_water` drew.** The pier's ocean and its underside rendered as an opaque
  magenta `tools/toolsinvisible` sheet with the word INVISIBLE tiled across it, above and below
  the plane. The VMT authors `%compilenodraw 1` and §2 says "no drawn surface"; the staged
  instance is `M_V2_Unlit` with `UseBaseTexture` true and
  `BaseTexture=/ElysiumBaked/Textures/tools/T_toolsinvisible`. The GLB decoder records `noDraw`
  per face (`map_glb/decode.py:965`, `SURF_NODRAW`) and nothing consumed it; the exporter's tool
  skip was a name test on `tools/` only, which this unit's `water/` path escapes.

  **Fixed 2026-09-04** (R7.1 review): `UE_map_sidecars.meshed_faces` now skips a face whose lump
  row carries `noDraw`, *beside* the name test rather than instead of it — the trigger textures
  leave the flag clear and the flag catches what the name cannot. Measured over all 108 published
  root units, the flag alone adds exactly 50 faces, all on `sm_pier_1` (41 `water/invisible_water`,
  9 of its `..._depth_33` patch), all world-scene and none displacement, so no other map's mesh,
  `.dispcol` or `.sky` moves. **Landed in content the same day**: `import models` over the four
  working maps (582 reused) and a non-forced `export map sm_pier_1` rebuilt the five chunk meshes
  that carried the sheet; re-witnessed, the beach shows the surf cards, the `blackwater` card and
  the rig with no sheet above or below the plane, and the player standing at the staged plane
  reads `Feet` (`E:/elysium-work/witness/pier_beach_1.png`, `pier_in_water.png`). The plan's
  original instruction, for the record (`import models --maps sm_pier_1`, then `export map sm_pier_1 --verify`); the
  volume is unaffected, it comes off the collision brushes and the `LEAFWATERDATA` texinfo. The
  legacy `UE_bsp_to_scene.py` lane keeps the name test alone: it publishes no V2 map and reads no
  texinfo flags today.

**`sp_soc_3`, measured 2026-09-04** (the content run that converted it; no `run play` session this
round — see the owner call below). The bake places the one staged row as `[bake] water: 1 actor
placed with 1 volume(s)`, and `verify_water` answers `1 staged rows, 1 matched` against the
manifest's plane Z **−609.6 cm** / floor **−1788.16 cm**. Three things read straight off the
authored numbers and the family table (§2, §4.3), the same "did-it-appear" standard as the other
two maps applies once a `run play` session reaches this map:
- **The cheap-water look.** `maps/sp_soc_3/dev/dev_water2_cheap` is the per-map patched
  `dev_water2*` unit with `$forcecheap` set, so ruling F applies — the basin emits its
  `{22 20 10}` fog colour on an unlit face, shows nothing through itself (no refraction pass, so
  Opacity is 1), and takes its mirror from Lumen rather than the retired cubemap.
- **The deep-basin fog under the plane.** At 1178.56 cm (464 in) deep — past `sm_hub_1`'s
  50.8 cm and `sm_pier_1`'s 83.8 cm, and past the pawn's 92.45 cm half-height — this is the first
  converted volume where the underwater post-process's fog has room to become a *look* rather
  than the "a hand's breadth of floor" case §6 and the note below record for the other two; it
  joins `hw_warrens_*`/`la_hub_1` as a basin where the fog is visible, not invisible-by-numbers.
- **The underside.** The volume's brushes (2, 6 planes each) carry the same top/bottom face
  pairing the family always does; the underside instance draws the refracted above-water world
  with zero extinction (ruling E) wherever the basin is entered from below.

**Owner call (2026-09-04): the swim path and the camera clearance band are out of this session's
scope.** `sp_soc_3` is the first converted volume deep enough to reach `Waist`/`Eyes` and exercise
`WaterMove`, the swim intent and `SolveWaterOffset` in play, but no `run play` witness of that was
run this session — the deep basin's coverage stays on the substrate tests
(`Elysium.Substrate.Water`, `Elysium.Substrate.WaterActor`, `Elysium.Substrate.Camera`, §12) until
a later witness spends the play session on it.

Also measured, not a defect: **`Waist`/`Eyes` are unreachable on the two shallow converted maps.**
Both `sm_hub_1` and `sm_pier_1`'s staged volumes are shallower than the pawn (`sm_hub_1` 50.8 cm —
`$waterdepth 20` exactly — and `sm_pier_1` 83.8 cm) against a 92.45 cm body half-height, so every
pose whose waist is inside the band puts the feet below `minZ` and `ClassifyBody` answers `None`
(measured: pawn at Z −15012.1 → `water: 0`). `sp_soc_3`'s basin is the first converted volume past
that bound (above); until its swim path is witnessed in play, `WaterMove`, the swim intent and
`SolveWaterOffset` stay on the automation tier for all three maps.

1. `sm_hub_1` sewer from the promenade: the neon reflects, the surface is not black, the far end
   is murk-green not white. The SLW pass shows in `stat gpu`.
2. The ripple moves (the 30-fps normal flipbook plus the scroll) — `sm_hub_1`'s sewer, and
   `sm_pier_1`'s `water/blackwater` ocean card at 24 fps.
3. Drop the camera through the plane (noclip): the underside draws the above world, the
   post-process fogs at the volume's numbers (`sm_hub_1` `{5 5 0}` / 1024 in, `sm_pier_1`
   `{22 20 10}`); `elysium_player_get` reports the `water` channel.
4. Walk into the canal at `sm_hub_1`'s sewer floor (`$waterdepth 20`): level `Feet`. Off the
   pier's boards at `sm_pier_1`: `Feet` again, with no surface drawn. `Waist`/`Eyes`, `WaterMove`
   and the swim intent need a volume deeper than the body (see above).
5. `sm_pier_1`'s 17 `objects/surf` cards slide up the sand on the 15 s sine (`SineUVTranslate`).
6. `sp_soc_3`'s society basin emits its `$fogcolor` and its volume fog is a *look* under the
   plane — **witnessed below**. The witness caught the first cut reading black wherever no light
   reached the water; ruling F was rebuilt from `WaterCheap_ps11` as a result (divergence 6), and
   the basin now carries its authored colour on an unlit face the way the 2004 lerp does. Step
   off into the 464-in basin for `Waist`/`Eyes` and `WaterMove` — the swim path this session's
   owner call left for a later `run play` witness (see above).
7. `spawnwater` on `hw_warrens_5` reads as a bad pool, not a ruby one (a future converted map).

### Witnessed 2026-09-04, third session — `sp_soc_3`, the first deep basin

`uv run elysium run play`, noclip, captures under `E:/elysium-work/witness/soc_*.png`. Both
regressions re-passed on the way out: `sm_hub_1`'s sewer surface draws and is not black (the canal
floor refracts through it, the pillars and wall lamps mirror in it) and the pawn dropped onto the
canal floor settles at Z **−14896.257** with `locomotion.water 1`, the same two numbers as the
second session; `sm_pier_1` shows no `tools/toolsinvisible` sheet above or below the plane — the
beach, the surf band, the pier and the offshore rig all draw, and `water/invisible_water` is
absent from the map's face groups on disk. Zero `Failed to compile Material Instance` and zero
`LogElysiumWater` lines in `Saved/Logs/ElysiumUE.log`.

- **Surface, from above (ruling F).** The basin draws, ripples and mirrors. Two frames at the same
  camera differ by **9.31 %** over the water against **0.73 %** on a static lit rock wall in the
  same frames — the 29-frame normal flipbook at 30 fps over the 0.035 scroll is moving. The
  reflection is Lumen's and is the only term with any magnitude: (50.3, 74.8, 77.8) where the
  surface mirrors the lit north wall, (0.2, 0.6, 0.8) 20 m out under an unlit ceiling, (0.0, 0.0,
  0.0) in the far corner. **That last number is the finding**, and it is what sent ruling F back to
  the corpus: the first cut's ×16 extinction needed light to show a colour, while
  `WaterCheap_ps11` emits `$fogcolor` on a `NOLIGHT` face. The master now emits it (divergence 6);
  these three readings are the pre-fix measurement and a re-witness is owed on the next `run play`.
- **Under the plane, inside the volume (ruling D) — the first deep-basin witness.** The volume fog
  *is* applied and *is* correct by depth. Proof, camera at (−2300, 2100, −900), 290 cm under the
  plane over a floor 950 cm down, `r.PostProcessing.DisableMaterials` A/B in the same pose:
  the below-water rock reads **(0, 0, 0)** with the blendable off and **(1.07, 0.83, 0)** near /
  **(5.02, 3.95, 0)** far with it on — a monotone depth ramp toward the fog colour, on geometry
  that is otherwise pure black. On the same frame the above-water rock seen *through* the plane
  goes (92.2, 95.4, 63.4) → (26.0, 25.8, 11.1), which solves to a fog factor of 0.761 and a depth
  of **774 cm** — the slant distance from the eye to the water plane along that ray (290 cm of
  clearance at ~22° elevation ≈ 743 cm), not the distance to the rock. The underside-writes-depth
  artefact is therefore confirmed numerically, not just argued.
- **The magnitude, recorded.** Fully fogged, the screen value is **(5, 4, 0)/255**; VtMB draws its
  `$fogcolor` **(22, 20, 10)/255**. Not a water defect: `ElysiumFog::DecodeColor` hands the
  post-process the linear value (0.00456, 0.00369, 0.00080) and the blendable sits at
  `BL_SCENE_COLOR_BEFORE_DOF`, so the filmic toe crushes it — the same "one named calibration for
  the whole render, not a per-term fudge" the header already declares, and the same treatment the
  per-primitive scene fog gets. It is 4.4× darker than 2004 and blue clips to zero. Nothing in the
  water lane should compensate for it alone.
- **The underside, from below (ruling E).** At 540 cm under the plane looking up, the whole ceiling
  arrives as the above-water world **refracted**: heavy vertical smear along the animated normal,
  the dock stair distorted with it, moving 1.95 % frame to frame — and no mirror, no specular
  highlight anywhere on it. Ruling E reads correctly.
- **The 40 drip emitters.** Nothing draws. There is no `NS_drip_emitter` under
  `Content/ElysiumGenerated/VFX` (only `NS_BarrelFireEmitter`), so the 40 `env_particle` actors
  fall back to `NS_ElysiumParticle`, which logs `ParticleRead: Failed to 'Get Position By Index'`
  for Leaf14–19 and emits nothing. R7.3's known state, not R7.1's.
- **Not a defect, for the next witness's sake.** `elysium.lights` is a *toggle* with no output; run
  it once and the map's 78 world lights go out and stay out, and every later capture reads ~40×
  dark with only bloom left. Three screenshots this session were wasted on it before the second
  `elysium.lights` brought the frame back bit-for-bit (lit rock 124.35 → 2.80 → 124.33).

**The one owner call this witness leaves.** Ruling F's cheap body is unlit. Recommendation:
**leave it physical.** The whole of `sp_soc_3`'s basin is unlit below and mostly unlit above, so
the constant `$fogcolor` floor would land at (5, 4, 0) on screen anyway — visible only against
pure black, and only because the toe crushes both. If the owner wants the 2004 reading instead,
the change is one node: an Emissive add of `pow(FogColor, 2.2)` gated on `CheapWater` and off
under `Underside`, in `_build_water`, which is a ruling-F amendment and a `GRAPH_VERSION` bump —
not a bug fix, and not made here. **It was made in the water-complete pass** (ruling F, revised):
`CheapWater` now emits the whole 2004 lerp.

### What the water-complete pass adds to the witness list

Numbers Phase 4 can read straight off the bake: `sm_hub_1` 47 water faces / 24 underside / 1
volume / 2 bound water sections / 1 style; `sm_pier_1` 50 / 18 / 1 / 3 sections / 2 styles over 211
styled groups; `sp_soc_3` 62 / 27 / 1 / 3 sections / 2 styles; `sp_tutorial_1` 0 water, 110 styled
groups, 5 styles tagged; `sm_pawnshop_1` 0 and 0. 108 `_Underside` twins imported corpus-wide; 27
instances parent directly to `M_V2_Water`.

8. **`sm_pier_1`'s ocean draws.** The swimmable volume now carries an `M_V2_Water` surface with its
   authored `{22 20 10}` fog and `UseBaseTexture` off — no `tools/toolsinvisible` sheet above or
   below the plane, and the harbour reads as water rather than a hole under the foam cards
   (ruling K).
9. **The pier waterline flickers.** The 34 `objects/surf` foam cards animate on style 1's
   `"mmnmmommommnonmmonqnmmo"` through CPD slot 6; the rest of the map's 211 styled sections
   animate with them. Freeze it with the lightstyle clock pin (roadmap 3.9) for an A/B (ruling M).
10. **The sewer's refraction moves.** `MI_sewer_water` runs both flipbooks now — the normal at
    30 fps and the DUDV folded into it — so the sewer's distortion breathes, not just its
    highlight (census G3).
11. **The underside no longer reflects.** Look up at the plane from inside: no specular highlight,
    no mirror, and the volume's own extinction still applies (ruling E revised).
12. **The player splashes.** Drop into `sp_soc_3`'s basin fast for the big splash; wade the hub
    canal for the wade one. And the negative: drop a physics prop into the sewer and confirm the
    port does **not** reproduce a fluid-controller splash (ruling L).
13. **U7 and U8, the two Phase 0 rows a desk could not close.** Do the hub's `Sewer Scheme` outputs
    fire on entering the water (`elysium_io_history`, `elysium_audio_state`), and does the pier's
    `logic_auto FadeGlobalWetness(1)` reach the MPC? And `TC_VectorDisplacementmap` vs
    `TC_Normalmap` on the water normal array, compared on one instance.

## 12. What pins it

At the seam, in the count each change authorizes — no floor-wide sweep.

| Layer | File | What it pins |
|---|---|---|
| C++ automation | `Private/Tests/ElysiumWaterTests.cpp` → `Elysium.Substrate.Water` | `ElysiumWater::FindVolumeAt` / `ClassifyBody` against a fixed brush set: feet/waist/eyes → `Feet`/`Waist`/`Eyes` with the right `OutVolume`; beside a brush, below `minZ`, or outside a sloped brush's planes → `None`/`INDEX_NONE`; two volumes pick the right index; `ElysiumBakedTags::Water`, `WaterFogScale`'s `2 ln 2` default, the `EElysiumWaterLevel` values |
| C++ automation | `ElysiumWaterTests.cpp` → `Elysium.Substrate.WaterActor` | `AElysiumWaterVolumes` by reflection (`Volumes` resolves); after `BeginPlay`, `bIsUnbound false` / `Priority 1` / `BlendWeight 1` / `bIsEnabled false`; a simulated `OnBeginPostProcessSettings` call with a point inside flips `bIsEnabled` and writes the MID's `FogColor`/`FogStart`/`FogInvRange`; a point outside flips it back |
| C++ automation | `Private/Tests/ElysiumCameraTests.cpp` → `Elysium.Substrate.Camera` | `SolveWaterOffset` exact values: level 2 at 1 in below surface → `+3 in`, at 5 in below → 0, at exactly 4 in below → 0; level 3 at 1 in above → `−3 in`, at 5 in above → 0; levels 0/1 → 0 always; `FElysiumCameraCvars().WaterDist == 4 * U` |
| C++ automation | `Private/Tests/ElysiumV2MaterialTests.cpp` → `Elysium.Policy.V2MasterParams` | `M_V2_Water`'s compiled graph carries `Underside`; `M_ElysiumUnderwater`'s `FogStart`/`FogInvRange`/`FogColor` bindings |
| pytest | `pipeline/tests/test_materials_stage.py` | the water-family normal fix (`test_water_normal_frames_come_from_normalmap_not_the_dudv`, `test_lit_water_look_unit_animates_its_normal_and_turns_the_gate_on`); `Underside` (`test_self_bottomed_water_unit_is_the_underside`, `test_water_unit_bottomed_by_another_material_is_not_the_underside`, `test_patched_water_instance_stages_no_underside_switch`); the header pin (`test_water_master_exposed_params_pinned_against_cpp_header`); the `SineUVTranslate` chain (`test_surf_sine_chain_stages_the_uv_translate_vector`, `test_surf_sine_chain_resolves_with_the_transform_authored_first`, `test_surf_sine_chain_on_an_unlit_unit_drops_the_vector_and_names_it`, `test_temp_sine_with_no_transform_reading_it_takes_the_omission`, `test_component_less_temp_sine_read_by_a_translatevar_takes_the_omission`, `test_two_sines_at_different_periods_stage_the_first_and_name_the_mismatch`) |
| pytest | `pipeline/tests/test_make_v2_materials_editor.py` | `test_water_master_is_single_layer_water_with_the_volume_pins_fed` (Opaque + SLW, the four output pins); `test_sine_uv_translate_reaches_the_base_lane_only` (the base-texture sample is downstream of `SineUVTranslate`, the normal sample is not) |
| pytest | `pipeline/tests/test_map_geometry.py` | `resolve_water_volumes`: the sentinel drop, the `%compilewater` content-bit predicate (`tools_shadow` excluded, `func_detail` water included), planes/bounds in the Unreal frame with the bevel side skipped, fog keys off VMT provenance including the `patchBase` chain, an unmatched row dropped and named, and the corpus rows themselves (`sm_hub_1`, `sm_pier_1`) |
| pytest | `pipeline/tests/test_bake_map_water.py` | `water_actor_values` (label/tags/folder/position), a zero-row manifest placing nothing, the recipe carrying the staged rows and the actor shape, the module's `MANIFEST_VERSION` bound to the stage's |
| pytest | `pipeline/unreal/bake_verify.py` → `verify_water` | one tagged `elysium.water` actor iff the manifest has rows; row count, per-row `surface_z_cm` and brush counts match the staged manifest |

Added by the water-complete pass (2026-09-04):

| Layer | File | What it pins |
|---|---|---|
| C++ automation | `Elysium.Substrate.Water` | `ElysiumWater::DecideSplash`'s two rules and their thresholds; the wade cooldown curve; `SubmergedFraction` / `DisplacedVolumeM3` / `BuoyantForceZ`; the CPD slot-6 name and number agreeing with the bake and the material graph |
| C++ automation | `Elysium.Substrate.WaterActor` | `Fluid` / `Pieces` / `LeafBoxesCm` / `NearBoxesCm` resolve by reflection **and** carry `CPF_Edit` without `CPF_EditConst`; `FindVolumeAt` prefers `Pieces`; `FindNearVolumeAt` degrades to the brush bounds on a level baked before the near set existed |
| pytest | `test_map_geometry.py` | `section_key` / `split_section_key` round-trip; `face_underside` on the plane row (and *not* on the `side` bit); `face_light_style` picking the lowest style; `water.faces[]` rows; the `%compilewater` nodraw exemption; `meshed_area_cm2` against vbsp's `area` per face and per section |
| pytest | `test_materials_stage.py` | the `_Underside` twin (path fold, parent, exactly one switch); `effective_family` / the `%compilewater` reroute and its four omission kinds; `EnvMapContrast`; `authoredThenRemovedKey`; the three-way exposed-parameter pin including `DuDvMapFrames` and `LightStyleBrightness` |
| pytest | `test_make_v2_materials_editor.py` | the DUDV fold reaching the normal and nothing else; `LightStyleBrightness` on CPD 6 multiplying base colour and emissive; the cheap lerp emitting `$fogcolor` |
| pytest | `test_bake_map_lightstyle.py`, `test_bake_map_water.py`, `test_bake_verify_water.py` | the chunk style suffix and its parse-back; `chunk_actor_tags`; the four new water struct fields written only when staged; `_verify_water_section_bindings` catching the exact defect an existence check misses (twin imported, underside section still bound to the surface instance) |
| pytest | `test_map_glb.py`, `test_texture_glb.py`, `test_texture_corpus_import.py`, `test_material_glb.py` | the `dface+96` split and `primitives[].typeName`; the mip-chain recovery; `vtf_flag_names` and the no-twin rule; `materialReferences[]` |
| pytest | `test_particle_glb.py`, `test_effects.py` | the single-key retail override; all four water roots resolving with zero unresolved children and zero missing textures |

---

## 13. Related docs

- `docs/vtmb/water_data_census.md` — the decoded-datum census and the G1–G26 evidence these
  rulings rest on.

- `docs/vtmb/water.md` — the inventory and the engine facts this mapping covers.
- `docs/vtmb/source_movement.md` — `WaterMove` / water level.
- `docs/vtmb/weather.md`, `docs/vtmb/effects.md` — drips, splashes (R7.3's).
- `docs/vtmb/reflections.md` — `$envmap` on the LMG water-look materials.
- `docs/vtmb/surface_properties.md` — the `water` physical / audio row.
- `docs/architecture/seam_map_material.md` → `M_V2_Water`; `seam_map_map.md` → "Import — water
  volumes (R7.1)".
- `docs/architecture/effects-architecture.md` — the same seam for fire, steam, blood.
- `docs/architecture/rendering-perf.md` — the budget.
- `docs/project/reconstruction-direction.md` — presentation may modernize; logic reproduces.
