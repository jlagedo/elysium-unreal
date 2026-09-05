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
- the 25 maps that carry `LEAFWATERDATA` (24 with a drawable water brush, 22
  with the `Water` shader on that brush — the counts diverge by feature, not
  by disagreement) and `CONTENTS_WATER` brushes;
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
The fluid controller (`FUN_10158600`/`10151150`), the client's own
water-entry splash (`FUN_10099630`), the compiled-primitive draw path
(`FUN_2007d4e0` and callers), `Mod_LoadPrimVerts`, `MASK_WATER`'s leaf-only
readers, and leaf contents bit `0x800` were all settled the same way in the
water-complete audit's Phase 0 pass — reports at
`E:/elysium-work/scratch/water_audit/phase0/` (`PHASE0_VERDICT.md` is the
synthesis; `U1a`–`U9` are the per-lane reports; `G_sweeps.md` the follow-up
cheap sweeps), against `E:/elysium-work/scratch/water_audit/AUDIT.md`.

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
   Fresnel against the per-pixel normal, and blends it over the refraction by
   that Fresnel (SRC_ALPHA).
4. The cheap cubemap pass **always** follows as a distance blend using
   `$cheapwaterstartdistance` / `$cheapwaterenddistance`: `$envmap` defaults
   to `engine/defaultcubemap` when the VMT names none, and only
   `$forceexpensive` (authored by no unit) suppresses it.

Pass order is `water_dx80.cpp`'s `SHADER_DRAW`, confirmed in the shipped
draw functions (`FUN_100138a0` → `FUN_10013b30` → `FUN_10013d30`): refract
RT, then reflect RT, then cheap cubemap by distance. See "The live program,
transcribed" below.

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

**Corrected:** the earlier text here called this a live-frame uncertainty —
whether a retail config actually fills the RTs, or a runtime distance LOD
quietly swaps it for the cheap cubemap. Neither read holds. Nothing in the
shipped binaries gates the RT fill on distance, frame budget or a video
setting: `$forcecheap` is a **material author's** choice (VMT-authored,
one bit, baked at content time), not a runtime level-of-detail system a
config or a distance query can flip. An expensive-shaped material — both
RTs named, `$forcecheap` unset — fills them **unconditionally**, every frame
it draws, regardless of distance from the camera; the `$cheapwaterstart/
enddistance` pair Water_Old also reads is a *blend weight* for the cubemap
overlay on top of that expensive result (item 4 above), not a switch that
turns the RT fill off. The sewer / warren / pool set is therefore not
"expensive with a cheap LOD fallback" — it is expensive, unconditionally,
with the cubemap always blended in by distance on top.

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
plane is a second water surface, not the backface of the top — but it is
never a *reflective* one: face loading strips the reflection render target
from every downward-facing water face before the renderer ever gets to it
(`Mod_LoadFaces`, `$reflecttexture` set undefined on load), so the underside
draws refraction and fog only, with no planar reflection pass to strip at
draw time.

`dev/dev_waterbeneath` (shader name `WaterSurfaceBottom`) is Unofficial
Patch only. No stdshader DLL registers that class; the vanilla underside
is `dev_waterbeneath2`.

`$abovewater` is not authored on any of these VMTs. `$waterdepth` is not
on the authored VMTs either; VBSP writes it into the map-local PAK
instances (`$waterdepth N` inside a `patch { include … }`).

VBSP does not restrict the `$bottommaterial` pairing to the water plane's top
face: every side of a `%compilewater` brush — top, sides and floor alike —
compiles its own `$bottommaterial` face, not just the one the player looks
up at from below. The near-1:1 top-vs-underside face count quoted above is
therefore a lower bound on the pairing, not a description of "one underside
face per top face"; a multi-sided brush (a stepped canal bed, a sloped
warrens basin) carries as many `dev_waterbeneath2` faces as it has non-top
sides.

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

`CONTENTS_SLIME` (`0x10`) is unused. `dface_t.surfaceFogVolumeID` is **not
uniformly 0** — that was an artefact of scanning maps with no primitive-
bearing water. `sm_pier_1` carries `0` on exactly 27 faces (the 9 `_depth_33`
top faces on plane 644, normal +Z, plus the 18 `invisible_water` underside
faces on plane 645, normal −Z, all at `z = -623 in`) and `0xFFFF` on its
other 5,423 (Phase 0 verdict, water-complete audit G11). It is still not a
runtime water-volume index worth trusting as a *source*: the engine forces
`0xFFFF` on every non-`WARP` face and every `WARP` face on both the retail
and Unofficial-Patch builds ends at volume 0 regardless, and the non-`0xFFFF`
values exist only in the UP recompile — the port reads it as a cross-check
against the geometrically-derived top/underside split, never as the split
itself.

`MASK_WATER` (`0x4030` = `CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_MOVEABLE`)
is confirmed as a literal in `CBaseEntity::PhysicsCheckWater`
(`vampire.dll 1003e880`, three `& 0x4030` tests) — but every reader tests it
against a **leaf's** contents, not a brush's: `EngineTraceServer003` slot 0
(`GetPointContents`, `engine.dll 20068b10`) resolves to `CM_PointContents`
(`200303b0`), which returns `leaf->contents` verbatim. A `tools/tools_shadow`
brush carrying `0x18000120` (contents `& 0x4030 = 0x20`, i.e. it *looks*
water-flagged) never raises a water level on that account alone: VBSP
propagates no `0x20` into any leaf containing one of those brushes (Phase 0
water-complete audit U5, confirming AUDIT §11.1's `RULED_OUT` row as a
measurement rather than an inference).

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

The lump is written and never read: no function in `vampire.dll`,
`client.dll` or `engine.dll` loads lump 46 at all. It is HL2 compiler
provenance carried through unused, not a distance hint any live system
consults — nothing in the water path (fog selection, LOD, the reflection
blend) is derived from it. The plane and depth a leaf sees come from
`surfaceTexInfoID` on `LEAFWATERDATA` itself, never from this lump.

### Leaf contents bit `0x800` — not a water fact, but adjacent to one

`0x800` on a leaf's `contents` word is **not** part of `MASK_WATER` and does
not gate `CheckWater`. Its one reader in the corpus is
`engine.dll FUN_200d3f10`, called from `CParticleManager::vfunc15`
(`200cecb0`), and only for a particle system whose definition carries the
`precipitation` key (flag `0x4000`, parsed at `FUN_200c8a90`): a particle in
a leaf *without* `0x800` gets particle flag `0x20`, whose only reader
(`FUN_200ceef0`) is the render-list gatherer's "do not draw" test. So `0x800`
is the precipitation render mask, and measurably it is VtMB's sky-visible /
outdoor annotation — 96 of 209 map files carry it, never co-occurring with
`CONTENTS_SOLID`, 94 of those 96 also carry `TOOLSSKYBOX` in texdata, pure
interiors carry zero, and a single +Z ray per empty leaf reproduces it on
2,303 of 2,493 hub leaves (92.4%, four false positives). **It is destroyed
by the Unofficial-Patch recompile on `sm_pier_1`** (retail carries it on 702
leaves, the UP build on 0); `sm_hub_1` is identical in both builds (975
leaves). This closes AUDIT §11.1's "meaning stays unresolved" row and §12
unknown 6 (Phase 0 verdict, water-complete audit U6). It matters to water
only because a rain gate keyed on it would silently do nothing on the pier's
UP build while the map still places 10 `rain_box_emitter` roots — the same
UP-vs-retail caveat as leaf contents bit `0x200` (VtMB's own near-water
annotation, gating `ViewDrawScene_EyeAboveWater`/`_EyeUnderWater`/`_WaterDX7`
against `_NoWater`; destroyed on the UP `sm_pier_1` the same way, retail 702
leaves vs UP 0). The port derives the near-water set from `⋃PVS(water
clusters)` instead of reading either bit off the leaf, which is set-equal to
`0x200` on both `sm_hub_1` (97 leaves) and `sp_soc_3` (126) and is the only
answer the UP `sm_pier_1` build has — see `docs/architecture/seam_map_map.md`
→ "Import — water volumes".

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

### Compiled tessellation: `$subdivsize`, primitives, and `origFace`

VBSP tessellates a water face into a regular grid and stores it in lumps
37 (`PRIMITIVES`) / 38 (`PRIMVERTS`) / 39 (`PRIMINDICES`) whenever the
material authors `$subdivsize`. Corpus census (Phase 0 water-complete audit
U4, 108 UP maps): 9 maps carry primitive-bearing water faces — `ch_hub_1`,
`hw_ash_sewer_1`, `hw_hub_1`, `hw_warrens_1/2/3/5`, `la_hub_1`,
`la_plaguebearer_sewer_1` — 512 faces total, and **100% of them bind a
material authoring `$subdivsize 64`**. The reverse fails: `hw_warrens_2b`
and `hw_warrens_4` bind `$subdivsize` water on every water face and have
non-empty `PRIMITIVES` lumps with **zero** faces referencing them (a
UP-recompile regression — retail `hw_warrens_4` has 4/4 referenced). None of
the three currently-exported maps (`sm_hub_1`, `sm_pier_1`, `sp_soc_3`)
carries a single primitive-bearing water face.

**Lump 38 (`PRIMVERTS`) carries positions only.** `Mod_LoadPrimVerts`
(`FUN_200b71e0`) zero-fills a 28-byte runtime record and copies only the
12-byte position out of it — the shipped engine's compiled water strips
draw with `TEXCOORD0/1 = (0, 0)`; there is no UV in the lump for a consumer
to read, faithfully or otherwise (`_DAT_201734e8 = 0.0`, closing AUDIT
§12.6). A reproduction has to re-derive UV0 from the face's own texinfo
vectors, exactly as an ordinary (non-primitive) face does.

**`origFace` (the field long misdecoded as `smoothingGroups`, at `dface+96`)
groups VBSP's CSG shards back to one authored brush face, but it is not a
drawable polygon.** It is trustworthy as a grouper — 79.5% of `side=1`
(back) faces share their `origFace` index with a `side=0` front face — and
untrustworthy as a shape: `originalFaces[].area` is `0.0` on all 2,182 rows
in the corpus, and only 26.5% (36 of 136) of multi-shard groups reconstruct
within 10% of their shards' own area sum. `sm_hub_1`'s sewer water body
(`origFace` 6768) reconstructs to 12,965,370 in² against 2,496,000 in² of
its surviving top shards — `origFace` names the pre-CSG brush face, most of
which the compiler discarded. A consumer that wants a real area pin reads
`faces[].area` (the compiler's own post-CSG number), never
`originalFaces[].area`.

**Are the compiled strips drawn, and are their vertices perturbed at
draw?** Drawn — exclusively, and by the shortest possible route.
`Shader_DrawSurfaceDynamic` (`engine.dll FUN_2007d4e0`) reads `numPrims` at
`msurface+0x50` as its first act and, when non-zero, takes the primitive
path (`MATERIAL_TRIANGLES` / `MATERIAL_TRIANGLE_STRIP`) to the exclusion of
both the adaptive-subdivision branch and the ordinary surfedge fan. Reached
every frame from `CViewRender::DrawWorld` (`client.dll 10199b40`, ORing
`DRAWWORLDLISTS_DRAW_WATERSURFACE` under `r_drawwatersurface`) through
`engine.dll FUN_2007f840` → `FUN_2007d990`/`FUN_2007fb30` →
`FUN_2007d4e0`. **Never perturbed.** `BuildMSurfacePrimVerts`
(`FUN_20074f40`) copies each primvert position verbatim (three dword moves)
and writes the face's PLANE normal; every shipped water vertex shader binds
`oPos = dp4(v0, cModelViewProj)` on the untouched input position (DX9's
`Water_vs20_old`, `stdshader_dx8.dll 100138a0`, does not displace); no water
vertex shader contains a `sincos` or a time constant. The one genuine
per-vertex animation anywhere near water is a normal-space swirl —
`mat_waterswirl` (default 0.02) rotates the vertex NORMAL, not the position,
`normal.xy = (sin, cos)(2·t + 0.117·x + 0.339·y) · mat_waterswirl` — and it,
like the adaptive-subdivision branch (`maxLen = min(dA, dB)·0.22 + 2.0`
inches, midpoints and centroids of coplanar points only), is a pre-DX9
fallback gated on `!hwconfig->vtable[0x30]()`/`[0x28]()` and does not run on
the shipped DX9 path (Phase 0 water-complete audit U2).

### Which `sm_pier_1`: Unofficial Patch vs retail

The currently exported `sm_pier_1` is the user's patched install — the
Unofficial Patch's own recompile, not retail. Measured differences that
touch water (Phase 0 water-complete audit, folding AUDIT §8/G24):

- Retail carries 12 additional `WATER/INVISIBLE_WATER` faces forming a hull
  floor at `z = -657 in` (1,205,760 in², about 46% of the map's total
  water-face area) and two extra `CONTENTS_WATER` brushes (`251`, `254`,
  both `0x18000120`, all-`TOOLS_SHADOW` sides, 372–506 in above the water
  plane inside the pier structure, never overlapping the real water brush).
  The UP recompile does not have them.
- Leaf contents bit `0x800` (precipitation / sky-visible mask, above) is 702
  leaves on retail and 0 on the UP build.
- Leaf contents bit `0x200` (near-water annotation) is destroyed the same
  way on the UP build.
- Both builds author `fluid { index "5" }` on the same physics model, so
  **the fluid controller exists in either build** — the guard is
  `fluid.index > 0`, not a leaf or contents test, and both indices agree.

The UP build is what the shipped install actually runs, and it is the only
build carrying the drawn ocean-card overlay this document's "Invisible
compilewater" section describes, so a reconstruction reading the corpus as
exported reads the UP build. Anything gated on the destroyed `0x200`/`0x800`
leaf bits for this one map needs a bit that does not survive — the near-water
derivation from `⋃PVS(water clusters)` (above) is unaffected either way,
because it never reads the leaf bit.

### The foam cards' lightstyle census

`sm_pier_1`'s 34 `objects/surf` faces (the only lightstyle-bearing water
geometry on either exported water map) all carry style 1
(`"mmnmmommommnonmmonqnmmo"`, registered by `CWorld::vfunc104`,
`vampire.dll 0x1023c020`) and 21 of the 34 also carry the switchable style
32 in an earlier slot than style 1. `sm_hub_1`'s 47 water faces carry no
lightstyle at all and have `lightOffset -1` — there is no lightmap page to
key a style against. No `worldLight` of any type and no texlight is bound
to a water face on either map (closest texlight: 586.6 in from the pier
water, 6653.7 in from the hub's). `env_cubemap`, `func_water_analog`,
`water_lod_control` and `env_fog_controller` do not exist as compiled
entities on either map (Phase 0 water-complete audit U1c).

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

### The live program, transcribed (2026-09-05)

Read this pass from the shipped binary and the Bloodlines SDK sources
(`ELYSIUM_WORK_ROOT/research/reference-source/Bloodlines SDK/.../stdshaders/`), not from a
summary. `stdshader_dx8.dll` registers `Water_Old_dx80_dx81_dx90` (`FUN_10013710`); its
`SHADER_DRAW` is `water_dx80.cpp`'s, and the three draw functions bind, on DX9 hardware, the SM2
`_old` programs (`FUN_100138a0`: `Water_vs20_old` + `WaterRefract_ps20_old`; `FUN_10013b30`:
`WaterReflect_ps20_old`; `FUN_10013d30`: `WaterCheap_vs20_old` + `WaterCheap_ps20_old`, with the
ps11 twins on DX8). `Water_ps20` — the SDK's one-pass class — is a string nowhere in the corpus.
The readable twins are `WaterRefract_ps11.psh`, `WaterReflect_ps11.psh`, `WaterCheap_ps11.psh`,
`Water_vs11.vsh`, `WaterCheap_ps20.fxc` and `Water_ps20.fxc`; `water.cpp` / `water_dx80.cpp`
set the constants.

1. **Refract pass** (`DrawRefraction`): `texbem` off the DUDV (`$bumpmap`, `$bumpframe`), bump
   matrix = `$refractamount` (SM2: VS c44), into `_rt_WaterRefraction` at the projected screen
   UV (c45 = (0,0,0,−1) flips Y); PS c1 = `$refracttint` with its luma in `.a`, c0 = ⅓, c2 = ½.
   The RT is the scene below the plane, clipped at it, rendered with the **water fog**
   (`macros.vsh::WaterFog`): linear over the distance the eye ray travels *through water*
   (`(waterZ − vertZ) / (eyeZ − vertZ) × viewDist`) toward `$fogcolor`, `$fogstart..$fogend`.
2. **Reflect pass** (`DrawReflection`, SRC_ALPHA / ONE_MINUS_SRC_ALPHA when a refract pass drew):
   the same `texbem` with `$reflectamount` into `_rt_WaterReflection` (the planar mirror);
   `rgb = reflection`, `a = (1 − saturate(N·V))^5` on the `$normalmap` normal through the
   normalizing cube, R0 = 0 (c3 = (1,0,0,0)); c1 = `$reflecttint`. Composite so far:
   `lerp(refract, reflect, fresnel)`.
3. **Cheap pass** (`DrawCheapWater`, SRC_ALPHA): `WaterCheap_ps20` is
   `$fogcolor + cube(reflect(eye, N_world)) × fresnel`, `alpha = saturate((|eye − vert| −
   $cheapwaterstartdistance) / (end − start))` (defaults 500 / 1000 in, `SHADER_INIT_PARAMS`);
   the ps11 twin is `lrp(fresnel, cube × $reflecttint, $fogcolor)`. It draws whenever `$envmap`
   is defined and `$forceexpensive` is not — and `SHADER_INIT_PARAMS` *defines* `$envmap` as
   `engine/defaultcubemap` (256², warm brown, exported) when the VMT names none — so **every
   expensive unit is fully cheap past `$cheapwaterenddistance`**. `$forcecheap` draws only this
   pass, alpha 1.
4. **No diffuse term.** Nothing in the program is lit; the RTs carry the light. The surface is
   range-fogged by the map fog like any surface (`CalcFog RANGE` in the vertex program).
5. **Motion**: `$bumpframe` (the `AnimatedTexture` proxy, 20–30 fps) steps both flipbooks;
   `$bumptransform` (`TextureScroll`, ~0.05 u/s at 45°) scrolls the bump UV (c91/c92). The DUDV
   is a 29-slice signed field with rms 0.027 and peaks 0.17; the normal's xy rms is 0.03. The
   authored amounts (15–100) against those magnitudes give a warp of 1–2 % of the frame in the
   owner's frames — the port's `WaterWarpScale` (0.01 per unit) is that measurement.
6. **`$fogcolor` is display bytes**: VtMB writes them to the framebuffer as-is. Measured in the
   owner's frames: the `sp_soc_3` basin ({22 20 10}) reads (27, 25, 15); the pier ocean (18, 18,
   17); the `sm_hub_1` canal (34, 45, 21) near and (50, 65, 35) far, darker than its walls.

What Unreal makes of each line is `docs/architecture/water-architecture.md` → ruling O.

### The older ps.1.1 reading

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
`water_ps20.vcs` / `watercheap_ps20.vcs` on disk have no registering class —
the `_old`-suffixed `ps20` combos are the ones that actually run on DX9
hardware, still inside Water_Old; the SDK-shaped one-pass `water_ps20.vcs`
sitting beside them on disk is unused payload, not a second live path.

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
The fog mode applied under the plane (`FogMode(LINEAR)` before the
below-water draw, `FogMode(0)` after) is chosen by the **caller** —
`ViewDrawScene_EyeUnderWater` itself — not authored per volume; every
`LEAFWATERDATA` row gets the same linear-fog treatment regardless of what
the material's own `$fogenable` says the *surface* pass should do, because
the surface pass and the underwater fog pass are different code paths
reading the same fog keys.

`GetWaterOffset` (`FUN_10190900`) walks the view origin in 1-unit Z steps
against `MASK_WATER` when `m_nWaterLevel > 1`, using `cl_waterdist` (4 in):
**upward at level 2**, out of the volume — treading water, the eye rises
clear of the plane and stays dry — and **downward at level 3**, back into
it — submerged, the eye is pushed under the plane and stays wet. (Corrected:
an earlier pass at this section had the two directions backwards.)
`mat_waterswirl` is a CPU-side per-vertex swirl of the plane normal
(amplitude 0.02, applied to the water mesh's vertices before the pixel
shaders run — not a shader term at all); `mat_wateroverlaysize` and
`WaterWarp_old` are the overlay-card and warp-strength knobs on the same
pass. `dsp_water` selects a fixed DSP preset (14) whenever the water level
reaches 3 (submerged), muffling audio for the underwater view the same way
the fog mutes the picture. The exact per-frame composite of warp, overlay
and DSP is not decompiled (`Water_Old`'s C++ draw call sits behind an
unresolved vtable slot); the *intent* of an underwater view that is not
just "the same scene, tinted" is engine-real and each individual knob's
identity now is too.

`GetVisibleFogVolume` (`engine.dll` `CVRenderView::GetVisibleFogVolume`,
`FUN_20081390`) is not a PVS walk. It descends the map's plane tree from the
view point — one dot-product-and-branch per node — straight to the leaf
that contains it, then reads that leaf's `leafWaterDataID` directly. "Which
volume is active" is a point-location query against the BSP, not a
front-to-back visibility search over candidate volumes; a view point is in
at most one leaf, so it is in at most one fog volume, full stop — there is
no tie-break to infer.

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
| `WaterBigSplash` / `WaterSplash` / `Splash` | impact bursts | fired by the client on the water-level transition — see below, this row corrects an earlier pass of this document |
| `Andrei_Splash_*` | boss dive | theatre, not a map water system |
| `rainsplash` / `rainsplash_new` | rain on ground / water | weather |

The 50-file "Water / splash / drip / spray" bucket in
`docs/vtmb/effects.md` includes blood sprays. The rows above are the water
ones.

### The fluid controller emits nothing; the splash is the client's, keyed on the water-level transition

**Corrected from an earlier pass of this document, which had it backwards.**
Both `sm_hub_1` and `sm_pier_1` DO get a fluid controller: the guard in
`vampire.dll FUN_10158600` is `if (fluid.index > 0)` on the first dword of
`ParseFluid`'s output (`101586f6 CALL [EDX+0x10]`, `101586f9 MOV EAX,
[ESP+0x44]`, `101586ff JLE skip`) — not `contents`, not solid flags — and
both maps author `fluid { index "5" }`. The controller wraps
`physics.models[0].solids[5]` and binds the hard-coded literal surfaceprop
`"water"` (`10158743`).

**The controller itself emits nothing.** `FUN_10151150` is Source's
`PhysicsSplash` with both `DispatchEffect` calls stripped: zero string
literals over a 1,952-byte listing, no string address pushed anywhere. It
still computes the basis, corners, speed, `flScale` and a
`RandomInt(1, 4)` secondary count into its own stack frame — and then
returns without dispatching them. `"watersplash"` does not exist as a
literal anywhere in the corpus, and there is no effect-dispatch registry
(`EffectDispatch` has zero hits). Those numbers are dead code in the shipped
build; nothing derived from them (speed scaling, the `RandomInt(1,4)` count)
should be treated as a live rule.

**The splash that does fire is client-side, on the water-level transition,
not on any fluid event.** `client.dll FUN_10099630` — called from
`FUN_10098800`, slot 9 (`DrawModel`) of the `IClientRenderable` sub-table of
both `C_BaseVCombatCharacter` and `C_BaseHLPlayer` — spawns
`waterbigsplash_emitter` on a `waterLevel` transition `0 → non-zero` with
`velocity.z < -200 in/s`, and `watersplash_emitter` while
`0 < waterLevel < 3` with horizontal speed ≥ 50 in/s on a `5.0 -
horizontal_speed·7.8e-5` second cooldown, spawned at `GetRenderOrigin() -
velocity.xy·0.035` snapped to the water surface (plus `RandomInt(0, 8)` in Z
for the wade one). Leaves: `waterbigsplash` (`watersplashes.tga`),
`watersplash` (`cloud.tga`), `splash` (`point_16.tga`).

**`waterbigsplash_emitter` is authored-empty in the Unofficial Patch.**
`Unofficial_Patch/particles/waterbigsplash_emitter.txt` (161 B) comments the
spawn token out (`// removed by wesp`), leaving an orphan block with no
spawn rule; the live definition is in retail `pack001.vpk` (143 B at
offset `70407456`). `watersplash_emitter.txt` (267 B, loose in the UP tree)
is unaffected and carries two live spawn blocks (`WaterSplash` burst 2,
`Splash` burst 5).

**The sound hook is separate from the splash builder.** `water.Impact` /
`water.Scrape` off surfaceprop `water`; `player/pl_wade2.wav` on exit
(`vampire.dll 1003f4d0`); `Surfaces/Water/Step*` at water level 1 and
`Surfaces/Wade/Step*` at level ≥ 2 on a four-phase counter whose phase 0 is
silent — **three wading steps in four sound** (`1011e940`: the level ≥ 2 branch
opens `if (DAT_1070b898 == 0) { DAT_1070b898 = 1; return; }` and then plays,
cycling 1→2→3→0, so only phase 0 returns before playing). Verdict D3's
shorthand "every 4th step" states the inverse and is superseded by this
reading;
`Water.BulletImpact` / `Underwater.BulletImpact` for gunfire.

(Phase 0 water-complete audit U3, closing AUDIT §12 unknown 7. Full
decompiled evidence, including the entry/exit thresholds' derivation, is at
`E:/elysium-work/scratch/water_audit/phase0/U3_fluid_controller.md`; the
read-only corpus sweeps that back the census claims throughout this file are
at `E:/elysium-work/scratch/water_audit/phase0/G_sweeps.md`.)

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

**NPCs get a water level too, off a different caller.** Every non-player
entity that steps physics through `PhysicsStepRunTimestep`
(`CBaseEntity::PhysicsStepRunTimestep`, `FUN_1003b190`) calls
`PhysicsCheckWater` directly at the top of the timestep — not through
`CGameMovement` at all — and reads `m_nWaterLevel` a few lines later to
decide whether to apply gravity that tick. This is the entity substrate's
own path into the same three-point classification the player uses; any
`MOVETYPE_STEP` NPC standing in a canal gets a real, ticked water level,
independent of whether the player ever notices the volume.

**No drowning.** `CBasePlayer` carries a full drown kit —
`m_AirFinished`, `m_idrowndmg`, `m_idrownrestored`, `m_nDrownDmgRate` — set
once at spawn (`this->m_AirFinished = fVar1; this->m_nDrownDmgRate = 2;`)
and otherwise touched only by the save/debug dumper. Nothing in the
decompiled corpus reads `m_AirFinished` back against the clock to apply
drowning damage; the HL2 mechanic's data fields survive, the tick that
would consume them does not exist. A vampire does not need to breathe, and
the code agrees: staying at level 3 has no time limit.

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

**Resolved since the last pass** (kept here only as a pointer, not a
question): whether the RT fill is config-gated — it is not, expensive
water fills unconditionally and `$forcecheap` is an author's binary choice,
not a distance LOD (see "Expensive vs cheap" above); which plane
`GetVisibleFogVolume` picks when a map authors several `$waterdepth`
instances — it is a point-location descent to the containing leaf, and
each leaf's own `surfaceTexInfoID` already names its exact patched
material, so there is no runtime "several instances, one wins" contest to
resolve.

- Water_Old C++'s own `SHADER_DRAW` dispatch and pass order is still
  inferred from program names and same-era DX80 conventions, not
  decompiled — the call sits behind an unresolved vtable slot.
- The exact per-frame composite of `WaterWarp_old`, the `mat_waterswirl`
  vertex swirl and the `dsp_water` preset switch at `waterlevel == 3`:
  each knob's identity and rough shape is now known (see "Underwater
  view" above), but not how they combine into one frame.
- Reachability of the three exported water planes (walk in, clip, or
  kill) — `sm_pier_1`'s suspected ocean-kill `trigger_hurt` still wants
  its brush hull read.
- Whether LMG `cheap_water` / `blackwater` bind the bumpmapped-envmap
  program (that one *has* Fresnel; the unbumped envmap path does not).
- Whether `mat_showwatertextures` on a captured frame confirms the RT
  fill visually (the code path is no longer in question; a frame capture
  would only add a picture to a fact already read from the binary).
