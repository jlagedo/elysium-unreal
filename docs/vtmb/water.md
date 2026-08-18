# Water — surfaces, volumes, and the Source water path

How VtMB authors water, how VBSP compiles it, and what the 2004 renderer actually
draws. Unreal reproduction — modern look, reproduced placement and parameters —
lives in `docs/architecture/water-architecture.md`.

Movement constants (`WaterMove`, water level 0–3, the idle sink of 40) stay in
`docs/vtmb/source_movement.md`. Rain, wet streets and `WaterDrops_Timer` as
weather stay in `docs/vtmb/weather.md`. Particle grammar stays in
`docs/vtmb/effects.md`. `$envmap` on ordinary world materials stays in
`docs/vtmb/reflections.md`; Fresnel is water-only and is owned here.

Status of any runtime or bake work lives only in `docs/project/roadmap.md`.

---

## Confidence

**Verified from the user's patched install** (patch-first `install.build_index`,
108 maps, 11,627 VMTs):

- the water VMT inventory and key census;
- the 25 maps that carry `LEAFWATERDATA` and `CONTENTS_WATER` brushes;
- the two readable `ps.1.1` programs `materials/dxshaders/waterreflect.psh` and
  `waterrefract.psh`;
- the compiled combo set under `shaders/fxc/` and `shaders/psh/`
  (`water_ps20`, `watercheap_ps20`, `watercheap_ps11`, `waterreflect_ps11`,
  `waterrefract_ps11`, plus older `_old` / `ps14` variants);
- the particle family that is actually water rather than blood-named "splash";
- the `.water` sidecars already written for `sm_hub_1`, `sm_pier_1`, `la_hub_1`.

**Transcribed from `vampire.dll` / `client.dll` / `stdshader_dx8.dll`:**
`CheckWater` (feet / waist / eyes, mask `0x4030`), `WaterMove` including the
accel tail, `CheckWaterJump` / `WaterJump`, and `CViewRender`'s
EyeAbove / EyeUnder / WaterDX7 split. No shipped `func_water` instance
exists; those functions sit on `%compilewater` world volumes instead.

**Uncertain:** whether a given water volume is player-reachable, a kill slab, or
visual-only. Hull extents of the `sm_pier_1` `trigger_hurt` at Z = −562 in sit
~61 in above that map's water plane; that is consistent with an ocean-kill slab
but is not proven. `hw_warrens_5` names three `trigger_hurt` volumes
`spawnwater` on top of the red spawn pool — that pairing is solid; the damage
intent (drain, sewage, sunlight) is not.

---

## It is not one thing

VtMB water is four stacked facts that the 2004 engine happens to share a name
for. They must not be collapsed into "a water material":

| Fact | What it is | What reads it |
|---|---|---|
| **Surface look** | a `Water` shader, or a `LightmappedGeneric` that *looks* wet | the pixel programs below |
| **Volume** | a `%compilewater` brush that VBSP marks `CONTENTS_WATER` | `CheckWater`, underwater fog, the leaf-water plane |
| **Underside** | `$bottommaterial`, almost always another `Water` surface | the view from below the plane |
| **Dressing** | drips, mist, waterfall sprites, splash bursts, footstep / impact audio | particles + `surfaceproperties` `water` |

There is **no `func_water` / `func_water_analog` / `water_lod_control` in any of
the 108 maps.** Those classes exist in `vampire.dll` RTTI and are instantiated
nowhere (`docs/vtmb/animation_and_movers.md` → B.1). Water volumes are ordinary
world / `func_detail` brushes whose material carries `%compilewater 1`.

The movement note that "no exported map places a water brush" is about that
missing *classname*. Three of the currently exported maps (`sm_hub_1`,
`sm_pier_1`, `la_hub_1`) already emit a `.water` sidecar and carry
`CONTENTS_WATER` brushes. The swim state machine is live; whether the player
can enter those volumes on the current playable slice is a reachability
question, not an authoring absence.

---

## How a water surface is authored

27 VMTs classify as water (`shader == Water` or `%compilewater` set). 24 of
those are the `Water` shader; three compile as water but shade as something
else (`water/cheap_water` and `water/invisible_water` are the live ones;
`dev/dev_waterbeneath` uses the leftover `WaterSurfaceBottom` shader name).

### Keys the `Water` shader actually uses

Census over the 27 water VMTs (a key is counted if it is present and non-empty):

| Key | Count | Role |
|---|---:|---|
| `%compilewater` | 27 | VBSP: this brush is a water volume |
| `$surfaceprop` | 26 | almost always `water` |
| `$bottommaterial` | 24 | underside material (see below) |
| `$fogenable` | 23 | underwater / in-volume fog on/off |
| `$fogcolor` / `$fogstart` / `$fogend` | 22 | in-volume fog. Colour is `{r g b}` 0–255 or `[r g b]` 0–1; the parser treats any channel > 1 as 0–255 |
| `$bumpmap` | 21 | **DUDV** (`dev/water_dudv`), not a tangent normal. `AnimatedTexture` drives `$bumpframe` at 20–30 fps |
| `$normalmap` | 20 | tangent ripple (`dev/water_normal`) |
| `$refracttexture` | 20 | `_rt_WaterRefraction` — the scene-from-above render target |
| `$reflecttexture` | 20 | `_rt_WaterReflection` — the planar-reflection render target |
| `$refractamount` / `$reflectamount` | 20 | warp strength. Authored 15–100; typical sewer/warrens 22 / 60 |
| `$refracttint` / `$reflecttint` | 20 / 19 | colour of the two RTs |
| `$envmap` | 9 | cubemap used when the cheap path is taken (`env_cubemap` or a named cube) |
| `$forcecheap` | 4 | skip the planar RTs |
| `$cheapwaterstartdistance` / `$cheapwaterenddistance` | 2 | LOD blend into cheap. `dev_water2_cheap` forces both to 0 (always cheap). `dev_waterbeneath2` uses 500 / 1000 |
| `$basetexture` | 9 | rare on real `Water`; required on the LightmappedGeneric fakes |

No shipped water VMT sets `$fresnelreflection`. Water's live class
(`Water_Old_dx80_dx81_dx90` in `stdshader_dx8.dll`) does not even register
that key. The Fresnel term is hard-wired in `waterreflect.psh` (Schlick,
exponent 5, `c3.a` is R0). `$FRESNELREFLECTION` is an LMG / WVT / Refract
parameter; it is not how water Fresnels. Zero VMTs in the install author it.

`AnimatedTexture` + `TextureScroll` are the motion. The DUDV (`$bumpmap`) is
the frame-animated warp field; `$bumpoffset` / `$bumptransform` scroll at
~0.05 units and 45°. There is no Gerstner, no flow map, no vertex wave.

A `WaterLOD` proxy block appears on `dev/dev_water2` with a dummy key and a
comment that the material loader requires the block to exist. It is not a
`water_lod_control` entity.

### Expensive vs cheap

The live GPU class is **`Water_Old`** in `stdshader_dx8.dll`
(`Water_Old_dx80_dx81_dx90`, fallback `Water_DX60`). `stdshader_dx9.dll`
registers no Water class; DX9 hardware still runs Water_Old. Later HL2
one-pass programs (`shaders/fxc/water_ps20.vcs`) sit on disk with **no
registering class** — leftover SDK payload, not the live path. There is no
`$forceexpensive` and no `r_waterforceexpensive` anywhere in the shipped
binaries, so the cheap LOD cannot be forced off the way HL2 can.

**Expensive** (`Water` / Water_Old, `$reflecttexture _rt_WaterReflection`,
`$refracttexture _rt_WaterRefraction`, `$forcecheap` unset):

1. `CViewRender::ViewDrawScene_EyeAboveWater` fills the two RTs (512×512,
   halved per `mat_picmip`, clamped to the framebuffer), then draws the scene.
2. `waterrefract.psh` perturbs `_rt_WaterRefraction` with the signed DUDV
   and multiplies by `c1` (`$refracttint`). The Fresnel copy of that program
   is present in the file and commented out.
3. `waterreflect.psh` perturbs `_rt_WaterReflection`, computes Schlick
   Fresnel against the per-pixel normal, and outputs `reflection · Fresnel`.
4. The cheap cubemap pass is still available as a **distance blend** using
   `$cheapwaterstartdistance` / `$cheapwaterenddistance` (Water_Old
   registers both). There is no convar that disables that overlay.

Pass order is inferred from Water_Old's program names plus the same-era
DX80 `SHADER_DRAW` (Water_Old C++ itself is not decompiled): refract RT,
then reflect RT, then cheap cubemap by distance.

**Cheap** (`$forcecheap 1`, or the `WaterCheap_ps11` / `ps20` combos, or a
`LightmappedGeneric` that only pretends to be water):

- no planar cameras;
- `WaterCheap`: cubemap about the perturbed normal, × `$reflecttint`,
  Schlick Fresnel, lerp toward `$fogcolor`, alpha from the cheap-distance
  blend. **No refraction.**
- LMG fakes: albedo + scrolling `dev/water_normal` + `$envmap`.

`dev/dev_water2` is the expensive template (the `$forcecheap 1` line is
commented out). `dev/dev_water2_cheap` forces `$forcecheap 1`, comments
both RT slots, and sets both cheap distances to 0 (always cheap). Most
*placed* surfaces (`water/sewer_water`, `water/warrwater`,
`water/warrenwater*`, `water/mazewater`, `water/spawnwater`,
`water/bradbury_blood`, `dev/pool_water`, `dev/seacave_water`) are
expensive-shaped: they name both RTs and do not set `$forcecheap`.
`dev/ocean` is reflection-RT only (refract commented out, `$fogenable 0`).

Whether a given retail config actually fills the RTs or falls through to
cheap is a live-frame fact (no capture in this survey). The *authored
intent* on the sewer / warren / pool set is expensive water with a cheap
distance LOD that cannot be forced off.

### LightmappedGeneric that only looks like water

These are **not** the `Water` shader. They keep an albedo, a scrolling
`$bumpmap` (`dev/water_normal`), and a cubemap. They are closer to the wet-street
`$envmap` path than to planar water.

| Material | `%compilewater` | What it is |
|---|---|---|
| `water/blackwater` | no | patch-added. Albedo `effects/ref_12`, animated bump, `env_cubemap`. Faces on `sm_pier_1` and `sm_beachhouse_1` |
| `water/bloody_water` | commented out | Bradbury blood pool look. Named cube `envmap/andrei_battle`, red `$envmaptint` |
| `water/warrenwater2b` | no | translucent cheap water with a strong envmap |
| `water/cheap_water` | **yes** | LMG shading *and* a water volume (fog + bottom + compile flag) |
| `water/placeholder_water` | no | unused-looking translucent oilfield albedo |
| `blends/blend_pierc`, `concrete/oilsewerd` | no | world materials that only borrow `$surfaceprop water` |
| fountain / droplet / blood-pool *models* | no | props, not map water |

`la_bradbury_3` is the awkward hybrid: the *faces* are `WATER/BLOODY_WATER`
(LMG) but the map still has one `LEAFWATERDATA` entry and seven
`CONTENTS_WATER` brushes. The volume is real; the shading is the fake path.

### Invisible compilewater

`water/invisible_water` is `UnlitGeneric`, `%compilenodraw 1`, `%compilewater 1`,
`$basetexture tools/toolsinvisible`, `$bottommaterial` pointing at itself. It
draws nothing and still compiles a water volume plus fog. `sm_pier_1` uses it
as the ocean (41 faces) plus a VBSP depth instance
`maps/sm_pier_1/water/invisible_water_depth_33` (9 faces). That is why the
pier `.water` sidecar has no `normalmap` line.

### The underside

`$bottommaterial` is a **compile** key, not a Water_Old shader parameter.
VBSP emits a second set of faces using that material. Almost every live
surface names `dev/dev_waterbeneath2`, which is itself a full `Water` shader
(RTs, normal, DUDV, cheap-distance 500/1000) whose `$bottommaterial` is
*itself*. Face counts on the water maps are nearly 1:1 between the top
material and `maps/<map>/dev/dev_waterbeneath2`. The view from under the
plane is a second water surface, not the backface of the top.

`dev/dev_waterbeneath` (shader name `WaterSurfaceBottom`) is Unofficial
Patch only. No stdshader DLL registers that class; the vanilla underside
is `dev_waterbeneath2`.

`$abovewater` is not authored on any of these VMTs. `$waterdepth` is not
on the authored VMTs either; VBSP writes it into the map-local PAK
instances (`$waterdepth N` inside a `patch { include … }`).

---

## How VBSP compiles it

### Contents

`CONTENTS_WATER` is `0x20`. **Do not test that bit alone.**
`%compileShadowOnly` (`tools/tools_shadow`) writes brushes whose contents
are `0x18000120` (`DETAIL|TRANSLUCENT|TESTFOGVOLUME|0x20`) with only
`TOOLS/TOOLS_SHADOW` sides. Those are shadow casters, not swim water: no
`LEAFWATERDATA`, no water-named faces. Ramen, oceanhouse, theatre, diner
and others light up a naive `contents & 0x20` walk.

Real volumetric water is `LEAFWATERDATA` nonempty **and** leaves with
`CONTENTS_WATER`. Those world brushes are typically **`0x10000020`**
(`TRANSLUCENT|WATER`) with sides named `WATER/*` or `DEV/DEV_WATER*`.
A map with one canal therefore shows one `0x10000020` brush plus a pile
of `0x18000120` shadow casters.

Visible Water faces carry SURF `0x408` (`WARP|NOLIGHT`). Pier
`invisible_water` is `0xC98` (`WARP|TRANS|NODRAW|NOLIGHT|BUMPLIGHT`).
`blackwater` / `bloody_water` are ordinary `0x800` (`BUMPLIGHT`) cards.

Collision export already leaves water passable (`BLOCK_MASK` excludes
`0x20`). A body that enters the volume is not pushed out; `CheckWater`
is what is supposed to notice.

`CONTENTS_SLIME` (`0x10`) is unused. `dface_t.surfaceFogVolumeID` is **0
on every face of every map** — it is not the water-volume index.

### `LEAFWATERDATA` (lump 36)

Present on exactly the same 25 maps. Record is 12 bytes:
`float surfaceZ`, `float minZ`, `int16 surfaceTexInfoID`, `int16 pad`.
`surfaceZ` is the water plane in Source inches; `minZ` is the bottom of the
volume. Depth of the volume is `surfaceZ − minZ` (often 16–24 in on canals,
hundreds of inches on the warrens / Giovanni / SoC basins).

`LEAFMINDISTTOWATER` (lump 46) is a `uint16` per leaf and is written on 103 of
108 maps. A non-empty lump is **not** evidence of water; VBSP emits it
unconditionally and it is **all zeros** on most maps. Real distances appear
on `sm_pier_1` (534 nonzero), `hw_warrens_2b` and `hw_warrens_4`.
`sm_beachhouse_1` is all `0xFFFF` (no volume). `WATEROVERLAYS` (lump 50)
is empty on every map.

### Map-local PAK instances

VBSP writes one patched VMT per water volume into the BSP `PAKFILE`, named
`maps/<map>/water/<stem>_<x>_<y>_<z>` or `maps/<map>/dev/dev_waterbeneath2`.
The patch body is `patch { include <water.vmt> insert { $waterdepth N } }`.
The current patch-first install has **46** `*_depth_*` instances across 24
maps (depths from 1 to 1376 inches) plus 23 copied `dev_waterbeneath2`
files (89 water-named PAK entries in all). The exporter already treats
those as map-local materials. They are depth-blend instances of the
authored water, not a second authoring language.

### Plane height the sidecar already computes

The map exporter takes the median Unreal Z of the material's *world* faces
(skybox-only water is skipped) and writes `<map>.water`. On the three
exported water maps that Z equals `LEAFWATERDATA.surfaceZ × 2.54` to the
sidecar's four decimals:

| Map | `surfaceZ` (in) | sidecar `plane` (cm) | Top material |
|---|---:|---:|---|
| `sm_hub_1` | −5881 | −14937.7400 | `water/sewer_water` + `dev_waterbeneath2` |
| `sm_pier_1` | −623 | −1582.4200 | `water/invisible_water` + `invisible_water_depth_33` |
| `la_hub_1` | −1033 | −2623.8200 | `water/warrwater` + `dev_waterbeneath2` |

The sidecar already carries `normalmap`, `fogcolor`, `fogdist` (start/end in
cm), `reflecttint`. It does **not** carry `$refracttint`, `$reflectamount`,
`$refractamount`, `$forcecheap`, `$bottommaterial`, the DUDV, or the scroll /
animated-frame proxies.

---

## Where water exists

25 of 108 maps. No slime. No `func_water`.

| Map | Vols | Water leaves | What the faces are | Notes |
|---|---:|---:|---|---|
| `sm_hub_1` | 1 | 21 | `sewer_water` + underside | sewer plane at −5881 in, spawn at −103. 36 `WaterDrops_Timer` sit in that sewer (~−5766), not on the promenade |
| `sm_pier_1` | 1 | 6 | `invisible_water` + depth_33 + `blackwater` `func_illusionary` | volume at −623 in, spawn at −560 — walk off the pier and you are in it. Visible ocean is the LMG overlay |
| `sm_beachhouse_1` | 0 | 0 | `blackwater` `func_illusionary` only | no `LEAFWATERDATA`; `LEAFMINDIST` all `0xFFFF` |
| `la_hub_1` | 1 | 39 | `warrwater` + underside | downtown canal at −1033 in |
| `la_dane_1` | 1 | 179 | `dev_water2_cheap` + underside (~638 faces) | large cheap body |
| `la_bradbury_3` | 1 | 1 | `bloody_water` (LMG) | `LEAFWATERDATA` texinfo is `TOOLS/TOOLS_SHADOW` — volume linkage looks accidental |
| `la_plaguebearer_sewer_1` | 3 | 189 | `warrwater` + `pool_water` | sewer + a pool. 8 `Waterfallmist_emitter` |
| `hw_hub_1` | 1 | 34 | `warrenwater` | Hollywood canal |
| `hw_ash_sewer_1` | 4 | 90 | `warrwater` | largest sewer. `drip_emitter` |
| `hw_vesuvius_1` | 1 | 2 | `pool_water` | club pool at Z ≈ 33 in. No `trigger_hurt` |
| `hw_warrens_1` | 3 | 108 | `warrwater` | |
| `hw_warrens_2` | 12 | 458 | `warrwater` | two `16384` sentinel `surfaceZ` rows — ignore those |
| `hw_warrens_2b` | 1 | 8 | `warrenwater` + `warrenwater2b` overlay | |
| `hw_warrens_3` | 7 | 88 | `warrwater` | waterfall trickle + mist |
| `hw_warrens_4` | 1 | 18 | `warrwater` + `warrenwater2b` | |
| `hw_warrens_5` | 4 | 60 | `spawnwater` (red, fog end 64) + `warrenwater` | three `trigger_hurt` named `spawnwater`, damage 5 |
| `ch_hub_1` | 1 | 45 | `warrenwater` | Chinatown canal |
| `ch_lotus_1` | 3 | 13 | `dev_water2` | interior water |
| `ch_temple_1` | 1 | 4 | `dev_water2_cheap` | |
| `ch_temple_2` | 2 | 6 | `pool_water` + `sewer_water` | 4 `SpaMist_Emitter`. No `trigger_hurt` |
| `ch_fulab_1` | 1 | 1 | `cheap_water` `func_illusionary` | |
| `sp_giovanni_1` | 2 | 30 | `dev_water2_cheap` instances | mansion water |
| `sp_giovanni_2a` / `2b` | 1 | 2 | `pool_water` | fountain basins. No `trigger_hurt` |
| `sp_soc_3` | 1 | 40 | `dev_water2_cheap` | Society of Leopold. 40 `drip_emitter` |
| `sp_endsequences_b` | 1 | 49 | `sewer_water` | end-game sewer, `surfaceZ` 5932 in |

### Families of look (the intent, not the mechanism)

| Family | Typical materials | Fog | Reflect / refract tint | What it is supposed to read as |
|---|---|---|---|---|
| Night ocean / canal | `sewer_water`, `warrwater`, `warrenwater*` | dark olive / near-black, end 400–1500 in | grey-green 0.3–0.7 | murky, reflective, slightly green, you cannot see far in |
| Clear-ish pool | `pool_water`, `dev_water2`, `dev_water3/4` | green-grey, end 30–800 | near-white reflect, pale refract | a basin you can see into |
| Blood / ritual | `bradbury_blood`, `bloody_water`, `confession_water`, `malk_water`, `spawnwater` | red-black, end 10–64 | red | a pool that should feel wrong, not pretty |
| Invisible volume | `invisible_water` | authored fog, no albedo | — | ocean the mesh must not draw |
| Painted water | `blackwater` | none | cube only | a cheap card that reads wet |

`dev/ocean` exists as a `Water` VMT (`$fogenable 0`, dark `$reflecttint` 0.25)
and is not referenced by any of the 25 water maps' face names.

---

## What the GPU draws

The `.psh` files are readable ps.1.1, same route as
`docs/vtmb/reflections.md`.

`waterreflect.psh` (the live expensive reflection):

```
tex t0                 ; signed DUDV
texm3x2pad t1, t0
texm3x2tex t2, t0      ; sample _rt_WaterReflection, perturbed
tex t3                 ; normal map
dp3_sat r1, v0_bx2, t3_bx2          ; N·V
; Schlick: R0 + (1-R0) (1-cos q)^5
mul r0.a, 1-r1.a, 1-r1.a
mul r0.a, r0.a, r0.a
mul r0.a, r0.a, 1-r1.a
mad r0.a, r0.a, 1-c3.a, c3.a
mul r0, r0.a, t2                    ; reflection * Fresnel
```

`waterrefract.psh` live path is `mul r0, t2, c1` after the same DUDV perturb —
refract tint, no Fresnel. The Fresnel refract variant is in the file as
comments.

Compiled combos Water_Old actually names: expensive `WaterReflect_old` /
`WaterRefract_old` / `WaterWarp_old` (and `*_ps20_old` on DX9-inside-Water_Old);
cheap `WaterCheap_ps11` / `WaterCheap_vs11` (and `*_ps20_old`). The later
`water_ps20.vcs` / `watercheap_ps20.vcs` on disk have no registering class.

`WaterCheap_ps11` (no shipped `.psh`; the Bloodlines-SDK file of the same
name matches the program Water_Old binds) is:

```
texm3x3vspec t3, t0_bx2     ; cubemap about perturbed normal
mul r0.rgb, t3, c1          ; × $reflecttint
; Schlick (1-N·V)^5 in alpha
lrp r0.rgb, t1.a, r0, c0    ; lerp(fogcolor, cube·tint, fresnel)
```

**Underwater view** is a real `CViewRender` branch, not a leftover name.
`ViewDrawScene` (`client.dll` `FUN_1019b370`) picks
`ViewDrawScene_EyeAboveWater`, `ViewDrawScene_EyeUnderWater`,
`ViewDrawScene_WaterDX7`, or `ViewDrawScene_NoWater` (`mat_drawwater` off).
`GetWaterOffset` (`FUN_10190900`) walks the view origin in 1-unit Z steps
against `MASK_WATER` when `m_nWaterLevel > 1`, using `cl_waterdist`:
downward at level 2 (keep the camera out of the volume while treading),
upward at level 3 (keep it just under). `mat_waterswirl`,
`mat_wateroverlaysize`, `dsp_water` and `WaterWarp_old` are the warp /
overlay / muffled-audio knobs. Their exact composite is not decompiled;
the *intent* of an underwater view that is not just "the same scene,
tinted" is engine-real.

---

## Particles and audio

These are not the water surface. They land *on* it or dress a basin.

**Placed on water maps (the ones that matter):**

| Definition | Role | Where |
|---|---|---|
| `WaterDrops_Timer` → `WaterDrops_Emitter` → `Drip` | overhang / tunnel drip, burst every 20–80 frames | `sm_hub_1` (36), warrens, Dane, … — already in `docs/vtmb/weather.md` |
| `WaterfallTrickle_emitter` → `WaterDrops2` | thin falling column | `hw_warrens_3` |
| `WaterFallMist_Emitter` / `waterfallmist_emitter` | large pale `furball` card, slow rise | warrens, plaguebearer sewer |
| `watermist_emitter` | cooler grey mist | `hw_warrens_2b` |
| `SpaMist_Emitter` | steam over a pool | `ch_temple_2`, `hw_warrens_5` |
| `drip_emitter` | steady drip | ash sewer, `sp_soc_3` (40) |
| `warrens_tube_water_emitter` | scripted tube (targetname `scene_water`) | warrens |
| `sprinkler_emitter` | fire-sprite jets (targetname `sprinkler_water`) | a later map, not the current export set |
| `pipe_water` / `pipedripspour_emitter` / `pipedripstrickle_emitter` | pipe leak | authored, not map-counted here |
| `WaterBigSplash` / `WaterSplash` / `Splash` | impact bursts | ready for a body entering water; not auto-placed |
| `Andrei_Splash_*` | boss dive | theatre, not a map water system |
| `rainsplash` / `rainsplash_new` | rain on ground / water | weather |

The 50-file "Water / splash / drip / spray" bucket in
`docs/vtmb/effects.md` includes blood sprays. The rows above are the water
ones.

**Audio.** `scripts/surfaceproperties.txt` `water`: density 1000, elasticity
0.2, friction 0.8, `gamematerial S`, left/right wade footsteps, three
`bullet_norm_impact` waves, `water.Impact` / `water.Scrape`. 35 world
materials name `$surfaceprop water` (the 27 compilewater set plus the LMG
fakes and a few props). `sound/surfaces/wade/` is a second shipped set that
no VMT names. HL2 leftovers in `scripts/sounds.txt` (`Player.Swim`,
`Player.Wade`, `Player.AmbientUnderWater`) point at
`sound/player/pl_wade*` files that **are not in the install** — if
`WaterMove` is wired, footsteps must use `Surfaces/Water`, not those.

---

## Movement and reachability

`CGameMovement::CheckWater` (`FUN_101252e0`) classifies the body with three
`MASK_WATER` (`0x4030` = `WATER|SLIME|MOVEABLE`) point queries and writes
`m_nWaterLevel` at `player+0x3e0` (and `m_nWaterType` at `+0x3e4`):

1. **Feet** — hull XY centre, `mins.z + ε`. Hit → level 1.
2. **Waist** — same XY, Z = hull midpoint (`0.5`). Hit → level 2.
3. **Eyes** — `origin.z + viewoffset.z`. Hit → level 3.

It returns `level > 1`, which is what `FullWalkMove` uses to skip gravity
and enter `WaterMove`. `WaterMove` (`FUN_101200c0`): full 3D wishvel (pitch
included), idle sink **40** (not HL2's 60), speed × **0.8**, friction with
no stopspeed floor (zero below 0.1), then accelerate on
`sv_wateraccelerate` (not `sv_accelerate`). `CheckWaterJump`
(`FUN_1011fb50`, only at level 2) traces 24 units from waist+8; on a wall
with a clear −50 along the normal it sets `vel.z = 260` and
`m_flWaterJumpTime = 2000`. `WaterJump` (`FUN_1011ffe0`) ticks that timer
and copies the stashed XY. `CheckJumpButton` refuses a normal jump at
level ≥ 2. Constants that already lived in `docs/vtmb/source_movement.md`
stand; the accel tail and the jump pair are no longer unread.

`CBaseEntity::PhysicsCheckWater` (`FUN_1003e880`) writes the same 0–3 for
the toss/fly path. Animation selects `ACT_SWIM` / `ACT_TREADWATER` from
level (`docs/vtmb/animation_and_movers.md`).

What the map census adds:

- The *volumes exist* on 25 maps, including three already-exported ones.
- No map uses `func_water`. The entity class is dead content.
- Some basins are paired with `trigger_hurt` (`hw_warrens_5` `spawnwater`,
  damage 5, `DMG_GENERIC`). Those are hazards that happen to be water, not an
  invitation to swim.
- `sm_hub_1` / `la_hub_1` `trigger_hurt` rows named `barrel_fire_burn` sit at
  street-level barrel origins or at Z = −7300 in (far below the canal). They
  are not ocean-kill slabs.
- `sm_pier_1` spawn sits 63 in above `surfaceZ`. Walk off the boards and
  `CheckWater` will fire. One unnamed `trigger_hurt` (damage 13, `DMG_BURN`)
  originates 61 in above the plane; treat as a suspected ocean-kill until
  the brush hull is read.
- `hw_vesuvius_1`, `ch_temple_2`, `sp_giovanni_2a/2b` have pools and no
  matching hurt. Those are the first places a swim would be testable.

Collision already does the right thing for a volume: water does not block.
Wiring `SetWaterLevel` from a contents query is the missing runtime seam, not
a missing brush. `sm_hub_1`'s sewer volume is authored `$waterdepth 20`
(about 50 cm) — a standing player is level 1, a drop-in is 2–3.

---

## What the pipeline already emits

| Product | Water fact it carries |
|---|---|
| shared corpus material record | `water`, `water_normal`, `water_fog_color/start/end`, `water_reflect_tint` |
| `<map>.mtl` | `water 1` on face groups that are water in *this* map |
| `<map>.water` | per-material plane Z (cm), normalmap path, fog colour, fog distances (cm), reflect tint. The underside (`dev_waterbeneath2`) is a **second row at the same Z** |
| `<map>.materials.json` | the VBSP map-local water instances |
| bake | currently binds water to `M_World_Translucent`, not a Single Layer Water master |

Dropped, and load-bearing for the look: `$refracttint`, both `*amount`s,
`$forcecheap` / cheap distances, `$bottommaterial`, the DUDV, the
`AnimatedTexture` frame rate, the scroll rate/angle, and the
LightmappedGeneric-vs-`Water` distinction (a compilewater LMG and a real
`Water` surface become the same `water 1` flag).

---

## Open questions

- Does a retail frame of `sewer_water` actually fill `_rt_WaterReflection`,
  or does the config fall through to the cheap cubemap? `mat_showwatertextures`
  on `sm_hub_1` / `hw_warrens_2` answers it.
- Water_Old C++ `SHADER_DRAW` pass order (inferred, not decompiled).
- Exact `WaterWarp` / `mat_waterswirl` / `dsp_water` composite when
  `waterlevel == 3`.
- How `GetVisibleFogVolume` / `CWaterEnum` pick the active plane when a
  map has several `$waterdepth` instances (`hw_warrens_2` has twelve).
- Reachability of the three exported water planes (walk in, clip, or kill).
- Whether LMG `cheap_water` / `blackwater` bind the bumpmapped-envmap
  program (that one *has* Fresnel; the unbumped envmap path does not).
