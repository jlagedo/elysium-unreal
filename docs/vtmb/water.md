# Water — surfaces, volumes, and the Source water path

How VtMB authors water, how VBSP compiles it, what the 2004 renderer draws, and what the game
does when a body enters it. This is the engine-neutral inventory and the evidence behind every
water ruling.

What stays elsewhere: movement constants (`WaterMove`, the accel tail, the water-jump pair) in
`docs/vtmb/source_movement.md`; rain, wet streets and `WaterDrops_Timer` in
`docs/vtmb/weather.md`; particle grammar in `docs/vtmb/effects.md`; `$envmap` on ordinary world
materials in `docs/vtmb/reflections.md`; the shader-combo selection table in
`docs/vtmb/shader_combos.md`; the `water` / `wade` surfaceprop rows in
`docs/vtmb/surface_properties.md`.

---

## Confidence

**Verified from the patched install** (patch-first `install.build_index`, 108 maps, 11,627 VMTs):
the water VMT inventory and key census; the 25 maps carrying `LEAFWATERDATA`; the readable
`ps.1.1` programs and the compiled combo set; the particle family that is water rather than
blood-named "splash"; the full decoded-datum census of `sm_pier_1` and `sm_hub_1` (every face,
leaf, brush, texture, entity and physics row that touches water on those two maps, cross-read
against the retail `.bsp` twins).

**Transcribed from `vampire.dll` / `client.dll` / `engine.dll` / `stdshader_dx8.dll`:**
`CheckWater`, `WaterMove`, `CheckWaterJump` / `WaterJump`, `PhysicsCheckWater`,
`CViewRender`'s EyeAbove / EyeUnder / WaterDX7 split, `GetWaterOffset`, `GetVisibleFogVolume`,
`Mod_LoadFaces`, `Mod_LoadPrimVerts`, `Shader_DrawSurfaceDynamic`, `BuildMSurfacePrimVerts`, the
fluid controller's creation guard and its stripped splash builder, the client's own water-entry
splash, `UpdateStepSound`, the `Water_Old` class and its three draw functions, and the readers of
leaf contents bits `0x200` and `0x800`.

**Raw reports** (out of repo, `ELYSIUM_WORK_ROOT/scratch/water_audit/`): `AUDIT.md` (the first
census pass), `CENSUS.md` (the consolidated 26-gap census with the auditor disagreements), the
Phase 0 lane reports under `phase0/` (`U1a`–`U9`, `G_sweeps.md`, `PHASE0_VERDICT.md`),
`LOOK_SPEC.md` (the shipped program transcribed against nine VtMB frames), the reference frames
under `reference/`, and the witness captures under `witness/` and `witness_look/`.

**Uncertain:** whether a given water volume is player-reachable, a kill slab, or visual-only.
`sm_pier_1`'s one unnamed `trigger_hurt` (damage 13, `DMG_BURN`) originates 61 in above the
water plane and is consistent with an ocean-kill slab but not proven. `hw_warrens_5` names three
`trigger_hurt` volumes `spawnwater` over the red spawn pool; the pairing is solid, the damage
intent is not.

---

## It is not one thing

VtMB water is four stacked facts that the 2004 engine happens to share a name for. They must not
be collapsed into "a water material":

| Fact | What it is | What reads it |
|---|---|---|
| **Surface look** | a `Water` shader, or a `LightmappedGeneric` that *looks* wet | the pixel programs below |
| **Volume** | a `%compilewater` brush that VBSP marks `CONTENTS_WATER` | `CheckWater`, the underwater fog, the fluid controller |
| **Underside** | every down-facing face of the water brush, painted with `$bottommaterial` | the view from below the plane, with reflection stripped at load |
| **Dressing** | drips, mist, waterfall sprites, the entry splash, footstep / impact audio | particles + `surfaceproperties` `water` |

There is **no `func_water` / `func_water_analog` / `water_lod_control` in any of the 108 maps.**
Those classes exist in `vampire.dll` RTTI and are instantiated nowhere. Water volumes are ordinary
world / `func_detail` / `func_illusionary` brushes whose material carries `%compilewater 1`.

---

## How a water surface is authored

27 VMTs classify as water (`shader == Water` or `%compilewater` set). 24 are the `Water` shader;
three compile as water but shade as something else: `water/cheap_water` (LightmappedGeneric),
`water/invisible_water` (UnlitGeneric, nodraw) and `dev/dev_waterbeneath` (the leftover
`WaterSurfaceBottom` shader name, Unofficial Patch only, registered by no stdshader DLL).

### Keys the `Water` shader uses

Census over the 27 water VMTs (a key counts if present and non-empty):

| Key | Count | Role |
|---|---:|---|
| `%compilewater` | 27 | VBSP: this brush is a water volume |
| `$surfaceprop` | 26 | almost always `water` |
| `$bottommaterial` | 24 | the underside material (below) |
| `$fogenable` | 23 | in-volume fog on/off |
| `$fogcolor` / `$fogstart` / `$fogend` | 22 | in-volume fog. Colour is `{r g b}` 0–255 or `[r g b]` 0–1; any channel > 1 reads as 0–255 |
| `$bumpmap` | 21 | the **DUDV** (`dev/water_dudv`), not a tangent normal. `AnimatedTexture` drives `$bumpframe` at 20–30 fps |
| `$normalmap` | 20 | the tangent ripple (`dev/water_normal`) |
| `$refracttexture` / `$reflecttexture` | 20 | `_rt_WaterRefraction` / `_rt_WaterReflection`, the two render targets |
| `$refractamount` / `$reflectamount` | 20 | warp strength. Authored 15–100; typical sewer/warrens 22 / 60 |
| `$refracttint` / `$reflecttint` | 20 / 19 | colour of the two RTs |
| `$envmap` | 9 | the cube the cheap pass samples (`env_cubemap` or a named cube) |
| `$forcecheap` | 4 | draw only the cheap pass |
| `$cheapwaterstartdistance` / `$cheapwaterenddistance` | 2 | the cheap-pass distance blend. `dev_water2_cheap` forces both to 0; `dev_waterbeneath2` uses 500 / 1000 |
| `$basetexture` | 9 | rare on real `Water`; required on the LMG fakes |
| `$subdivsize` | 13 | VBSP tessellation grid (64 or 16); geometry, not shading |
| `$watermurkiness` | 4 non-default | registered by **no** shipped shader; the 2004 renderer read it and did nothing |

No shipped water VMT sets `$fresnelreflection`, and `Water_Old` does not register it. Water's
Fresnel is hard-wired in the reflect program (Schlick, exponent 5, R0 = 0).

Both `dev/water_normal` and `dev/water_dudv` are 29-frame VTFs (256×256; the DUDV is signed
UVWQ8888, rms 0.027, peaks 0.17; the normal's xy rms is 0.03). `$bumpframe` steps both textures
together; `$bumpoffset` / `$bumptransform` scroll the bump UV through a `TextureScroll` proxy at
~0.05 u/s and 45°. **The 2004 shader never read the register that proxy wrote**, on 13 water units
including the sewer, so the authored scroll did not move on screen. There is no Gerstner, no flow
map, no vertex wave (below).

The wave-parameter block (`$waterbasefactor`, `$waterbasemovementdist/freq`, `$watertimefreq1/2`,
`$waterwaveheight/length`, `$waterspecularmin/max`) is read by a `Water_Old` vertex path that did
not ship: DX9 binds `Water_vs20_old`, which writes `oPos = dp4(v0, cModelViewProj)` on the untouched
input position, and no water vertex shader in the corpus contains a `sincos` or a time constant.

A `WaterLOD` proxy block appears on `dev/dev_water2` with a dummy key and a comment that the
material loader requires the block to exist. It is not a `water_lod_control` entity.

### Expensive vs cheap

The live GPU class is **`Water_Old`** (`Water_Old_dx80_dx81_dx90` in `stdshader_dx8.dll`,
fallback `Water_DX60`). `stdshader_dx9.dll` registers no Water class; DX9 hardware still runs
`Water_Old`, binding its SM2 `_old` programs. The one-pass `shaders/fxc/water_ps20.vcs` sits on
disk with no registering class — SDK payload, not a live path. There is no `$forceexpensive` and
no `r_waterforceexpensive` in the shipped binaries.

`$forcecheap` is a **material author's** choice, one bit, baked at content time — not a runtime
distance LOD a config can flip. An expensive-shaped material (both RTs named, `$forcecheap` unset)
fills both render targets unconditionally every frame it draws; the `$cheapwaterstart/enddistance`
pair is the blend weight of the cheap overlay drawn *on top* of that result, not a switch that turns
the RT fill off.

`dev/dev_water2` is the expensive template (its `$forcecheap 1` line is commented out).
`dev/dev_water2_cheap` forces `$forcecheap 1`, comments both RT slots and sets both cheap distances
to 0. Most *placed* surfaces (`water/sewer_water`, `warrwater`, `warrenwater*`, `mazewater`,
`spawnwater`, `bradbury_blood`, `dev/pool_water`, `dev/seacave_water`) are expensive-shaped.
`dev/ocean` is reflection-RT only (`$fogenable 0`) and is placed on no map.

### LightmappedGeneric that only looks like water

Not the `Water` shader: an albedo, a scrolling `dev/water_normal`, a cubemap. Closer to the
wet-street `$envmap` path than to planar water.

| Material | `%compilewater` | What it is |
|---|---|---|
| `water/blackwater` | no | Unofficial Patch addition (`// added by psycho-a`). Albedo `effects/ref_12`, animated bump at 24 fps, `env_cubemap`. `sm_pier_1`'s visible ocean card and `sm_beachhouse_1` |
| `water/bloody_water` | commented out | the Bradbury blood-pool look. Named cube `envmap/andrei_battle`, red `$envmaptint` |
| `water/warrenwater2b` | no | translucent cheap water with a strong envmap |
| `water/cheap_water` | **yes** | LMG shading *and* a water volume (`ch_fulab_1`, `func_illusionary`); authors `$forcecheap 1` and an `$envmapmask` the cheap program never reads |
| `water/placeholder_water` | no | unused translucent oilfield albedo |
| `blends/blend_pierc`, `concrete/oilsewerd` | no | world materials that only borrow `$surfaceprop water` |

`la_bradbury_3` is the hybrid: the faces are `WATER/BLOODY_WATER` (LMG), the map still carries one
`LEAFWATERDATA` row, but that row's `surfaceTexInfoID` names `tools/tools_shadow` and no
`%compilewater` brush stands at it. The volume linkage is a shadow-brush artefact.

### Invisible compilewater

`water/invisible_water` is `UnlitGeneric`, `%compilenodraw 1`, `%compilewater 1`, `$basetexture
tools/toolsinvisible`, `$bottommaterial` pointing at itself. It draws nothing and still compiles a
water volume plus fog. `sm_pier_1` uses it as the swimmable ocean (41 faces) plus the VBSP depth
instance `maps/sm_pier_1/water/invisible_water_depth_33` (9 faces); all 50 are `SURF_NODRAW`
(`0xC98`). The ocean the player *sees* is the `water/blackwater` card 21 in below the plane, a
`func_illusionary` inside the 3D skybox placed by the runtime at world `z = −644 in`,
31,744 × 98,019 in, meeting the harbour waterline at the horizon. A 2004 engine could not draw a
live ocean under a painted skybox card; the invisible volume under a painted card is that
shortcut.

### The underside

`$bottommaterial` is a **compile** key, not a `Water_Old` parameter. VBSP paints every non-top
side of a `%compilewater` brush — sides and floor alike — with that material, so a stepped canal
bed carries as many underside faces as it has non-top sides. Almost every live surface names
`dev/dev_waterbeneath2`, itself a full `Water` shader whose `$bottommaterial` is itself; face
counts on the water maps are near 1:1 between the top material and `maps/<map>/dev/
dev_waterbeneath2`. `water/invisible_water` and `dev/dev_waterbeneath2` name themselves (3
self-references among the 24 units that author the key).

**The engine decides "underside" per face, on the face's own plane, not per material.**
`Mod_LoadFaces` (`engine.dll 0x200b73d0`) compares `plane.normal.z` against the literal
`_DAT_201734e8 = 0.0` and, for a down-facing water face, calls `$reflecttexture->SetUndefined()`
on that face's **shared** material — a load-order-dependent mutation. The result: the underside
draws refraction and fog only, never the planar reflection. Measured corpus-wide, every
down-facing water face carries `side 1`, so the `side` bit cannot substitute for the normal test
without flipping every hub water face up-facing. On `sm_pier_1` the patched `_depth_33` instance
faces up while its parent `invisible_water` faces down — one per-unit switch cannot answer for
both.

`$abovewater` is authored nowhere. `$waterdepth` is not on the authored VMTs; VBSP writes it into
the map-local PAK instances.

### Map-local PAK instances

VBSP writes one patched VMT per water volume into the BSP `PAKFILE`, named
`maps/<map>/water/<stem>_<x>_<y>_<z>` or `maps/<map>/dev/dev_waterbeneath2`, whose body is
`patch { include <water.vmt> insert { $waterdepth N } }`. The patch-first install carries **46**
`*_depth_*` instances across 24 maps (depths 1 to 1376 in) plus 23 copied `dev_waterbeneath2`
files. They are depth-blend instances of the authored water, not a second authoring language; a
patched instance's own text carries only its delta, and every other key reads through the base.
Retail's `maps/sm_hub_1/dev/dev_waterbeneath2.vmt` is a 977-byte fully expanded block; the UP
replaced it with a 117-byte stub, and nothing is lost because the corpus-wide base unit authors
the same values.

---

## What the GPU draws

### The live program, transcribed

Read from the shipped binary and the Bloodlines SDK sources
(`ELYSIUM_WORK_ROOT/research/reference-source/Bloodlines SDK/.../stdshaders/`). `stdshader_dx8.dll`
registers `Water_Old_dx80_dx81_dx90` (`FUN_10013710`); its `SHADER_DRAW` is `water_dx80.cpp`'s, and
the three draw functions bind, on DX9 hardware, `FUN_100138a0`: `Water_vs20_old` +
`WaterRefract_ps20_old`; `FUN_10013b30`: `WaterReflect_ps20_old`; `FUN_10013d30`:
`WaterCheap_vs20_old` + `WaterCheap_ps20_old` (ps11 twins on DX8). Readable twins:
`WaterRefract_ps11.psh`, `WaterReflect_ps11.psh`, `WaterCheap_ps11.psh`, `Water_vs11.vsh`,
`WaterCheap_ps20.fxc`, `macros.vsh` (`WaterFog`); `water.cpp` / `water_dx80.cpp` set the constants.

1. **Refract pass** (`DrawRefraction`): `texbem` off the DUDV (`$bumpmap`, `$bumpframe`), bump
   matrix = `$refractamount` (VS c44), into `_rt_WaterRefraction` at the projected screen UV
   (c45 = (0,0,0,−1) flips Y); PS c1 = `$refracttint`. The RT is the scene below the plane, clipped
   at it, rendered with the **water fog** (`macros.vsh::WaterFog`): linear over the distance the
   eye ray travels *through water* (`(waterZ − vertZ) / (eyeZ − vertZ) × viewDist`) toward
   `$fogcolor`, over `$fogstart..$fogend`.
2. **Reflect pass** (`DrawReflection`, SRC_ALPHA / ONE_MINUS_SRC_ALPHA): the same `texbem` with
   `$reflectamount` into `_rt_WaterReflection` (the planar mirror, 512×512 halved per
   `mat_picmip`, clamped to the framebuffer; it draws entities and the 2D sky lump, never a 3D
   skybox); `rgb = reflection × $reflecttint`, `a = (1 − saturate(N·V))^5` on the `$normalmap`
   normal, R0 = 0 (c3 = (1,0,0,0); the shipped `_old` programs read no `c3` at all). Composite:
   `lerp(refract, reflect, fresnel)`.
3. **Cheap pass** (`DrawCheapWater`, SRC_ALPHA): `WaterCheap_ps20` is
   `$fogcolor + cube(reflect(eye, N_world)) × fresnel`; the ps11 twin is
   `lrp(fresnel, cube × $reflecttint, $fogcolor)` — the two differ only at grazing angles.
   `alpha = saturate((|eye − vert| − $cheapwaterstartdistance) / (end − start))`, defaults
   500 / 1000 in. It draws whenever `$envmap` is defined and `$forceexpensive` is not, and
   `SHADER_INIT_PARAMS` *defines* `$envmap` as `engine/defaultcubemap` (256², warm brown) when the
   VMT names none — so **every expensive unit is fully cheap past `$cheapwaterenddistance`**.
   `$forcecheap` draws only this pass, alpha 1. It never binds the refraction RT.
4. **No diffuse term.** Nothing in the program is lit; the RTs carry the light. A visible water
   face is `SURF 0x408` = `WARP | NOLIGHT` (no lightmap, `lightOffset −1`). The surface is
   range-fogged by the map fog like any surface (`CalcFog RANGE` in the vertex program).
5. **Motion** is shader-space only: `$bumpframe` steps both flipbooks; `$bumptransform` scrolls the
   bump UV (the register `Water_Old` never read). The authored amounts against the DUDV's
   magnitudes give a warp of 1–2 % of the frame in the owner's frames.
6. **`$fogcolor` is display bytes**, written to the framebuffer as-is. Measured in the owner's
   frames: the `sp_soc_3` basin ({22 20 10}) reads (27, 25, 15); the pier ocean (18, 18, 17); the
   `sm_hub_1` canal (34, 45, 21) near and (50, 65, 35) far, darker than its walls.
7. **Down-facing face**: pass 2 is skipped (`$reflecttexture` undefined at load, above).

The older readable `ps.1.1` programs say the same thing:

```
; waterreflect.psh
tex t0                 ; signed DUDV
texm3x2pad t1, t0
texm3x2tex t2, t0      ; sample _rt_WaterReflection, perturbed
tex t3                 ; normal map
dp3_sat r1, v0_bx2, t3_bx2          ; N·V
mul r0.a, 1-r1.a, 1-r1.a
mul r0.a, r0.a, r0.a
mul r0.a, r0.a, 1-r1.a              ; (1 - N·V)^5
mad r0.a, r0.a, 1-c3.a, c3.a        ; Schlick, R0 = c3.a
mul r0, r0.a, t2                    ; reflection * Fresnel

; waterrefract.psh live path: mul r0, t2, c1 after the same perturb (refract tint, no Fresnel)

; WaterCheap_ps11 (SDK source; matches the program Water_Old binds)
texm3x3vspec t3, t0_bx2     ; cubemap about the perturbed normal
mul r0.rgb, t3, c1          ; × $reflecttint
lrp r0.rgb, t1.a, r0, c0    ; lerp(fogcolor, cube·tint, fresnel)
```

`mat_waterswirl` (default 0.02) is a CPU-side rotation of the vertex **normal**
(`normal.xy = (sin, cos)(2·t + 0.117·x + 0.339·y) · swirl`), not the position, and like the
adaptive-subdivision branch (`maxLen = min(dA, dB)·0.22 + 2.0` in, midpoints and centroids of
coplanar points only) it is a pre-DX9 fallback gated on `!hwconfig->vtable[0x30]()` /
`[0x28]()` that does not run on the shipped DX9 path.

### The eye under the plane

`ViewDrawScene` (`client.dll FUN_1019b370`) picks `ViewDrawScene_EyeAboveWater`,
`_EyeUnderWater`, `_WaterDX7`, or `_NoWater` (`mat_drawwater` off, or no leaf near the eye carries
contents bit `0x200`, below). Under the plane it is two passes and one fog: the above-water world
plus the 2D sky into the refraction RT clipped at the plane, then the below-water world plus the
water surfaces into the framebuffer under `SetFogVolumeState(id, false)` — `FogMode(LINEAR)`
before, `FogMode(0)` after — plain linear fog over everything at the volume's `$fogcolor` /
`$fogstart` / `$fogend`. No warp, no tint, no reflection pass. The fog mode is chosen by the
caller, not per volume: every `LEAFWATERDATA` row gets the same linear treatment regardless of the
surface's own `$fogenable`, because the surface pass and the underwater pass are different code
reading the same keys.

`GetVisibleFogVolume` (`engine.dll FUN_20081390`) is not a PVS walk: it descends the plane tree
from the view point to the containing leaf and reads that leaf's `leafWaterDataID`. A view point is
in at most one leaf, so at most one fog volume; there is no tie-break.

`GetWaterOffset` (`client.dll FUN_10190900`) walks the view origin in 1-unit Z steps against
`MASK_WATER` when `m_nWaterLevel > 1`, using `cl_waterdist` (4 in): **upward at level 2**, out of
the volume (treading, the eye stays dry) and **downward at level 3**, back into it (submerged, the
eye stays wet).

`dsp_water` selects DSP preset 14 whenever the water level reaches 3. `mat_wateroverlaysize` and
`WaterWarp_old` are the overlay-card and warp knobs of the same underwater pass; how the three
combine into one frame is not decompiled (the composite sits behind an unresolved vtable slot).

---

## How VBSP compiles it

### Contents

`CONTENTS_WATER` is `0x20`. **Do not test that bit alone.** `%compileShadowOnly`
(`tools/tools_shadow`) writes brushes whose contents are `0x18000120`
(`DETAIL|TRANSLUCENT|TESTFOGVOLUME|0x20`) with only `TOOLS/TOOLS_SHADOW` sides — shadow casters,
not water: no `LEAFWATERDATA`, no water-named faces, and VBSP propagates no `0x20` into any leaf
containing one. Real water is `LEAFWATERDATA` nonempty **and** leaves with `CONTENTS_WATER`; those
brushes are typically `0x10000020` (`TRANSLUCENT|WATER`) with `WATER/*` or `DEV/DEV_WATER*` sides,
or `0x18000020` for `func_detail` water (`ch_lotus_1`). A brush is water exactly when a non-bevel
side's material authors `%compilewater`.

`MASK_WATER` (`0x4030` = `WATER|SLIME|MOVEABLE`) is a literal in `CBaseEntity::PhysicsCheckWater`
(`vampire.dll 1003e880`), and every reader tests it against a **leaf's** contents, not a brush's:
`GetPointContents` (`engine.dll 20068b10`) resolves to `CM_PointContents` (`200303b0`), which
returns `leaf->contents` verbatim. `CONTENTS_SLIME` (`0x10`) is unused. `CONTENTS_CURRENT_*` is
authored on no water brush in the corpus.

Visible `Water` faces carry SURF `0x408` (`WARP|NOLIGHT`). The pier's `invisible_water` is
`0xC98` (`WARP|TRANS|NODRAW|NOLIGHT|BUMPLIGHT`). `blackwater` / `bloody_water` are ordinary `0x800`
(`BUMPLIGHT`) cards.

### `LEAFWATERDATA` (lump 36)

Present on the same 25 maps. A 12-byte record: `float surfaceZ`, `float minZ`,
`int16 surfaceTexInfoID`, `int16 pad`. `surfaceZ` is the plane in inches, `minZ` the bottom;
depth is often 16–24 in on canals and hundreds of inches on the warrens / Giovanni / Society
basins. `surfaceTexInfoID == −1` (VBSP's `16384` sentinel, two rows in `hw_warrens_2`) marks no
volume. Each leaf's `leafWaterDataID` (`0` on exactly the 6 pier / 21 hub water leaves, `−1`
elsewhere) is the engine's own "is the eye under water" answer; the union of the hub's 21 leaf
boxes is 3056 × 3744 in against the authored brush's 3333 × 3890 in, so the brush over-runs the
carved leaves by 66–77 in per horizontal axis.

`LEAFMINDISTTOWATER` (lump 46) is written on 103 maps, all zeros on most, real distances on
`sm_pier_1` (534 nonzero), `hw_warrens_2b` and `hw_warrens_4` — and **never read**: lump 46
appears in no `CMapLoadHelper::LoadLump` call site in `engine.dll`, and retail carries it at
length 0 on both owner maps. `WATEROVERLAYS` (lump 50) and `OVERLAYS` (45) are length 0 on all
209 map files.

`dface_t.surfaceFogVolumeID` is `0` on exactly 27 pier faces (the 9 `_depth_33` tops on plane
644, normal +Z, and the 18 `invisible_water` undersides on plane 645, normal −Z) and `0xFFFF` on
the other 5,423 — but only in the UP recompile: the engine forces `0xFFFF` on every non-`WARP`
face and every `WARP` face on both builds ends at volume 0. A cross-check on the top/underside
split, never a source.

### Leaf contents bits `0x200` and `0x800`

`0x200` is VtMB's `CONTENTS_TESTFOGVOLUME`, the near-water annotation. Its one reader is
`CVRenderView::GetVisibleFogVolume`, and it gates `ViewDrawScene_EyeAboveWater` /
`_EyeUnderWater` / `_WaterDX7` against `_NoWater`. It equals `⋃PVS(water clusters)` with zero
discrepancy on every map measured (retail pier 527/527, hub 97/97, `hw_ash_sewer_1` 656/656,
`la_hub_1` 165/165, `sp_soc_3` 126/126). The UP pier carries stock Source's `0x100` instead, which
excludes the water leaves and which VtMB never reads.

`0x800` is **not** a water fact. Its one reader is `engine.dll FUN_200d3f10`, from
`CParticleManager::vfunc15`, only for a particle system whose definition carries the
`precipitation` key: a particle in a leaf without `0x800` gets the "do not draw" flag. It is the
precipitation render mask and, measurably, VBSP's sky-visible annotation (96 of 209 map files
carry it, never with `CONTENTS_SOLID`, 94 of the 96 alongside `TOOLSSKYBOX`; a single +Z ray per
empty leaf reproduces it on 92.4 % of hub leaves). It matters to water only because both bits are
destroyed by the UP recompile of `sm_pier_1` (below).

### Compiled tessellation: `$subdivsize`, primitives, `origFace`

VBSP tessellates a water face into a regular grid and stores it in lumps 37 / 38 / 39
(`PRIMITIVES` / `PRIMVERTS` / `PRIMINDICES`) when the material authors `$subdivsize`. Corpus
census (108 UP maps): 9 maps carry primitive-bearing water faces — `ch_hub_1`, `hw_ash_sewer_1`,
`hw_hub_1`, `hw_warrens_1/2/3/5`, `la_hub_1`, `la_plaguebearer_sewer_1` — 512 faces, 100 % of
them water at texinfo flags `0x408` and 100 % binding `$subdivsize 64`; `hw_warrens_1` face 2300
decodes to a 9-vertex / 13-index strip on a 64-unit grid. `hw_warrens_2b` and `hw_warrens_4` have
non-empty lumps with zero faces referencing them (a UP regression; retail `hw_warrens_4` has 4/4).
None of the converted maps (`sm_hub_1`, `sm_pier_1`, `sp_soc_3`) carries a primitive face. Even
where they exist every primitive vertex of a face shares one Z: a finer static grid, not a wave.

The `dface` dword at `+96` is `origFace` (i32), then `numPrims` (u16 @100) and `firstPrimID`
(u16 @102) — not `smoothingGroups`, confirmed at instruction level in `Mod_LoadFaces`
(`200b7648 MOV AX,[EDI+0x64]`, `200b7650 MOV CX,[EDI+0x66]`, msurface stride `0x98`, runtime
offsets `+0x50` / `+0x52`).

**Lump 38 carries positions only.** `Mod_LoadPrimVerts` (`FUN_200b71e0`) zero-fills a 28-byte
runtime record and copies the 12-byte position; the shipped engine draws compiled strips with
`TEXCOORD0/1 = (0, 0)`. A reproduction re-derives UV0 from the face's texinfo vectors.

**The strips are drawn, exclusively, and never perturbed.** `Shader_DrawSurfaceDynamic`
(`engine.dll FUN_2007d4e0`) reads `numPrims` first and, when non-zero, takes the primitive path
(`MATERIAL_TRIANGLES` / `MATERIAL_TRIANGLE_STRIP`) to the exclusion of the subdivision branch and
the surfedge fan; reached every frame from `CViewRender::DrawWorld` (`client.dll 10199b40`,
`DRAWWORLDLISTS_DRAW_WATERSURFACE` under `r_drawwatersurface`). `BuildMSurfacePrimVerts`
(`FUN_20074f40`) copies each position verbatim and writes the face **plane** normal.

**`origFace` groups VBSP's CSG shards back to one authored brush face and is not a drawable
polygon**: 79.5 % of `side=1` faces share it with a `side=0` front face, but
`originalFaces[].area` is 0.0 on all 2,182 rows and only 26.5 % of multi-shard groups reconstruct
within 10 % of their shards' area (`sm_hub_1`'s sewer body, `origFace` 6768, reconstructs to
12,965,370 in² against 2,496,000 in² of surviving shards). `faces[].area` is the compiler's own
post-CSG number: pier `blackwater` Σ 36,007,784 in² over 15 faces, `objects/surf` ≈ 2.45 M over 34;
hub `sewer_water` 4,704,256 over 23, `dev_waterbeneath2` 4,848,128 over 24.

### Flatness, proven

No decoded datum on either owner map supports relief, tessellation or vertex animation on the
water surface. `dispInfo = −1` on all 99 pier and 47 hub water faces; no displacement carries
`CONTENTS_WATER`; every per-vertex normal on every water face equals the plane normal exactly;
every water vertex of a surface sits at one height (the hub's 47 faces at `z = −5881.0`, the
pier's 9 `_depth_33` tops at `−623.0`); the primitive lumps are length 0 on both builds of both
maps; the mesh build subdivides nothing. The motion that is real on these maps is the two 29-frame
flipbooks.

### The physics side: `fluid`, the material table, the convex carve

The map's `PHYSCOLLIDE` keyvalue tail publishes a `fluid { }` block on both owner maps — pier:
`index "5"`, `density 1000`, `damping 0.01`, `surfaceplane 0 0 1 -623`, `currentvelocity 0 0 0`;
hub: `index "5"`, `surfaceprop "water"`, `damping 0.01`, `contents "268435488"` (`0x10000020`),
`surfaceplane 0 0 1 -5881` — naming solid 5, whose `upperLimitRadius` (17.07 m pier, 65.08 m
hub) matches each water brush's half-diagonal exactly. Solid 5 is the compiler's own convex
decomposition of the water: 1 ledge on the pier, 5 on the hub (the carve that fits the sewer
walls where the single brush spills 66–77 in). The physics `materialtable` carries a `water`
row on the pier (`WATER = 17` of 18) and none on the hub (16 rows).

### Which `sm_pier_1`: Unofficial Patch vs retail

The pier the pipeline exports is the UP's own recompile (mapRevision 218, 5,450 faces), not
Troika's (mapRevision 1994, 5,149 faces). The UP build **adds** the visible `blackwater` ocean
card, the `_depth_33` patch (retail's is `_depth_34`, `minZ −657`; UP wrote `−656` / `_depth_33`),
`BUMPLIGHT` on 88.5 % of faces, lump 46, and the 27 non-degenerate `surfaceFogVolumeID` values;
it **destroys** 12 `INVISIBLE_WATER` hull-floor faces at `z = −657` (1,205,760 in², 46 % of the
map's water-face area), two inert retail `CONTENTS_WATER` brushes (251, 254, `0x18000120`,
all-`TOOLS_SHADOW` sides, above the plane inside the pier structure, propagating `0x20` into no
leaf), and **both** leaf annotations (`0x200` 702 → 0, `0x800` 702 → 0) — so the shipping
Unofficial Patch renders `sm_pier_1` through `ViewDrawScene_NoWater` every frame. Both builds
author `fluid { index "5" }` on the same solid. `sm_hub_1` is retail-authentic (33 of 40 lumps
byte-identical).

### The foam cards' lightstyles

`sm_pier_1`'s 34 `objects/surf` faces are 17 `func_illusionary` brush bodies (2 shards each) whose
world z is `−618..−621` — 2 to 5 in above the water plane, the waterline. They are the only
lightstyle-bearing water geometry on any converted map: all 34 carry style 1
(`"mmnmmommommnonmmonqnmmo"`, registered by `CWorld::vfunc104`, `vampire.dll 0x1023c020`), and 21
also carry the switchable style 32, 18 of them in an *earlier* slot than style 1. `Mod_LoadFaces`
sets `surfflags |= 0x2000` on a face whose style is neither 0 nor 0xFF, and the engine sums one
lightmap page per style slot. `blackwater` carries static style 0; the hub's 47 water faces carry
no style and no lightmap. No `worldLight` or texlight is bound to a water face on either map
(closest texlight 586.6 in from the pier water, 6653.7 in from the hub's); `env_cubemap` probes
exist near both bodies (hub #17 five inches above the plane; three inside the pier's footprint)
and no water face binds one — the ocean card binds the 3D-skybox sample.

---

## Where water exists

25 of 108 maps. No slime. No `func_water`.

| Map | Vols | Water leaves | What the faces are | Notes |
|---|---:|---:|---|---|
| `sm_hub_1` | 1 | 21 | `sewer_water` + underside | sewer plane at −5881 in, `$waterdepth 20`; 36 `WaterDrops_Timer` in the sewer; six `Sewer Scheme` triggers straddle the plane (below) |
| `sm_pier_1` | 1 | 6 | `invisible_water` + `_depth_33` (nodraw) + `blackwater` card + 34 `objects/surf` foam cards | volume at −623 in, spawn at −560 — walk off the boards and you are in it |
| `sm_beachhouse_1` | 0 | 0 | `blackwater` `func_illusionary` only | no `LEAFWATERDATA` |
| `la_hub_1` | 1 | 39 | `warrwater` + underside | downtown canal at −1033 in |
| `la_dane_1` | 1 | 179 | `dev_water` (no `$normalmap`) + underside (~638 faces) | large cheap body; the owner's frame shows broad undulation |
| `la_bradbury_3` | (1) | 1 | `bloody_water` (LMG) | the `LEAFWATERDATA` row names `tools_shadow`; not a real volume |
| `la_plaguebearer_sewer_1` | 3 | 189 | `warrwater` + `pool_water` | 8 `Waterfallmist_emitter` |
| `hw_hub_1` | 1 | 34 | `warrenwater` | Hollywood canal |
| `hw_ash_sewer_1` | 4 | 90 | `warrwater` | largest sewer; `drip_emitter` |
| `hw_vesuvius_1` | 1 | 2 | `pool_water` | club pool at z ≈ 33 in |
| `hw_warrens_1` | 3 | 108 | `warrwater` | |
| `hw_warrens_2` | 12 | 458 | `warrwater` | two `16384` sentinel rows |
| `hw_warrens_2b` | 1 | 8 | `warrenwater` + `warrenwater2b` overlay | |
| `hw_warrens_3` | 7 | 88 | `warrwater` | waterfall trickle + mist |
| `hw_warrens_4` | 1 | 18 | `warrwater` + `warrenwater2b` | |
| `hw_warrens_5` | 4 | 60 | `spawnwater` (red, fog end 64) + `warrenwater` | three `trigger_hurt` named `spawnwater`, damage 5 |
| `ch_hub_1` | 1 | 45 | `warrenwater` | Chinatown canal |
| `ch_lotus_1` | 3 | 13 | `dev_water2` | `func_detail` water |
| `ch_temple_1` | 1 | 4 | `dev_water2_cheap` | |
| `ch_temple_2` | 2 | 6 | `pool_water` + `sewer_water` | 4 `SpaMist_Emitter` |
| `ch_fulab_1` | 1 | 1 | `cheap_water` `func_illusionary` | no leaf points at its row |
| `sp_giovanni_1` | 2 | 30 | `dev_water2_cheap` instances | |
| `sp_giovanni_2a` / `2b` | 1 | 2 | `pool_water` | fountain basins |
| `sp_soc_3` | 1 | 40 | `dev_water2_cheap` (31 top + 31 underside) | Society of Leopold, 464 in deep; 40 `drip_emitter` |
| `sp_endsequences_b` | 1 | 49 | `sewer_water` | `surfaceZ` 5932 in |

Counts diverge by feature, not by disagreement: 25 maps carry `LEAFWATERDATA`, 24 have a drawable
water brush, 22 units sit on the `Water` shader.

### Families of look

| Family | Materials | Fog | Tints | Reads as |
|---|---|---|---|---|
| Night canal / sewer | `sewer_water` {5 5 0} end 1024; `warrwater` {.2 .4 .2} end 1024; `warrenwater` {.4 .4 .2} end 128 | dark olive to black | grey-green 0.3–0.7 | murky, reflective, you cannot see far in |
| Clear-ish pool | `pool_water` {21 39 20} end 800; `dev_water2*` / `dev_waterbeneath2` / `cheap_water` / `invisible_water` {22 20 10} end 400 | green-grey / warm grey | near-white | a basin you can see into |
| Blood / ritual | `bradbury_blood`, `bloody_water`, `spawnwater` {5 0 0} end 64 | red-black | red | a pool that should feel wrong |
| Invisible volume | `invisible_water` | {22 20 10} | — | the pier's swimmable ocean, drawn by the `blackwater` card |
| Painted water | `blackwater` | none | cube only | a cheap card that reads wet |

---

## The events water raises

### The fluid controller creates, and emits nothing

Both owner maps get a fluid controller. The guard in `vampire.dll FUN_10158600` is
`if (fluid.index > 0)` on the first dword of `ParseFluid`'s output (`101586ff JLE skip`) — not
contents, not solid flags — and both maps author `index "5"`. Server and client
(`client.dll FUN_10120230`) both call `IPhysicsEnvironment::CreateFluidController` on solid 5 with
the hard-coded surfaceprop literal `"water"` (`10158743`).

`FUN_10151150` is Source's `PhysicsSplash` with both `DispatchEffect` calls stripped: zero string
literals over a 1,952-byte listing. It computes the basis, corners, speed and a `RandomInt(1, 4)`
count into its own frame and returns. `"watersplash"` exists as a literal nowhere in the corpus and
there is no effect-dispatch registry. Those numbers are dead code. What the controller still does
is vphysics' own buoyancy and damping off the authored `density` / `damping`, in `vphysics.dll`,
which is not in the Ghidra corpus.

### The splash is the client's, on the water-level transition

`client.dll FUN_10099630`, called from slot 9 (`DrawModel`) of the `IClientRenderable` sub-table
of both `C_BaseVCombatCharacter` and `C_BaseHLPlayer`:

- `waterbigsplash_emitter` on a `waterLevel 0 → ≥ 1` transition with `velocity.z < −200 in/s`;
- `watersplash_emitter` while `0 < waterLevel < 3` with horizontal speed ≥ 50 in/s, on a
  `5.0 − horizontal_speed · 7.8e-5` s cooldown;
- both at `GetRenderOrigin() − velocity.xy · 0.035` snapped to the water surface, plus
  `RandomInt(0, 8)` in z for the wade one.

Leaves: `waterbigsplash` (`watersplashes.tga`), `watersplash` (`cloud.tga`), `splash`
(`point_16.tga`). `waterbigsplash_emitter` is **authored-empty in the Unofficial Patch**
(`Unofficial_Patch/particles/waterbigsplash_emitter.txt`, 161 B, `// removed by wesp` on the spawn
token); the live definition is retail `pack001.vpk`'s (143 B). `watersplash_emitter.txt` (267 B,
loose in the UP tree) carries two live spawn blocks (`WaterSplash` burst 2, `Splash` burst 5).

### Sounds

`CBasePlayer::UpdateStepSound` (`vampire.dll 1011e940`) is a millisecond timer in the movement
code (`m_flStepSoundTime` gates the head; each pool writes its own interval back — 400 ms walking,
300 running, 600 wading, plus a 60/120 ms bias), not an animation notify. At water level 1 every
step plays `Surfaces/Water/Step*`; at level ≥ 2 the branch opens `if (DAT_1070b898 == 0)
{ DAT_1070b898 = 1; return; }` and then plays `Surfaces/Wade/Step*`, cycling 1→2→3→0 — **three
wading steps in four sound**. The pool is keyed off the classified level, never the surface
material under the foot (the pier's foam cards bind `PM_default`). `water.Impact` /
`water.Scrape` come off surfaceprop `water`; `Water.BulletImpact` / `Underwater.BulletImpact` for
gunfire. `player/pl_wade2.wav` is pushed on exit (`vampire.dll 1003f4d0`) and **is not in the
game**: an index of every VPK plus every loose override (67,469 packed entries, 78,538 files)
carries no `pl_wade*`. The HL2 leftovers in `scripts/sounds.txt` (`Player.Swim`, `Player.Wade`,
`Player.AmbientUnderWater`) point at the same missing files.

`scripts/surfaceproperties.txt` `water`: density 1000, elasticity 0.2, friction 0.8,
`gamematerial S`, left/right wade footsteps, three `bullet_norm_impact` waves. 35 world materials
name `$surfaceprop water`; `sound/surfaces/wade/` is a second shipped set no VMT names.

### Entity I/O near the water

`sm_hub_1`: six `trigger_multiple` volumes named `Sewer Scheme` / `Sewers Scheme` (entities
2491–2494, 2496, 2500) straddle the sewer plane, each firing `SchemeSewers,FadeIn` and
`Scheme_SM_Streets,FadeOut`; four more with identical I/O sit 15–27 in above it; six
manhole-pursuit triggers and one `trigger_changelevel` (`sewer_map_trigger`) reach below the
surface; 8 `npc_maker rat_swimming.mdl` and 5 `ambient_generic "Waterdrips"` sit 2–150 in above
the plane; `rain_on_timer` / `rain_off_timer` cycle the global wetness. `sm_pier_1`: `logic_auto`
idx 900 fires `OnMapLoad → world.FadeGlobalWetness(1)`; both maps author `wetness_fadetarget 0`.

### Particles placed on water maps

| Definition | Role | Where |
|---|---|---|
| `WaterDrops_Timer` → `WaterDrops_Emitter` → `Drip` | overhang drip, burst every 20–80 frames | `sm_hub_1` (36), warrens, Dane — `docs/vtmb/weather.md` |
| `drip_emitter` → `Drip` | steady drip (the same chain minus the timer) | ash sewer, `sp_soc_3` (40) |
| `WaterfallTrickle_emitter` → `WaterDrops2` | thin falling column | `hw_warrens_3` |
| `WaterFallMist_Emitter` / `waterfallmist_emitter` | large pale `furball` card, slow rise | warrens, plaguebearer sewer |
| `watermist_emitter` | cooler grey mist | `hw_warrens_2b` |
| `SpaMist_Emitter` | steam over a pool | `ch_temple_2`, `hw_warrens_5` |
| `warrens_tube_water_emitter` | scripted tube (`scene_water`) | warrens |
| `waterbigsplash_emitter` / `watersplash_emitter` | the entry and wade splashes | fired by the client, placed on no map |
| `Andrei_Splash_*` | boss dive | theatre, not map water |
| `rainsplash` / `rainsplash_new` | rain on ground / water | weather |

The 50-file "Water / splash / drip / spray" bucket in `docs/vtmb/effects.md` includes blood
sprays; the rows above are the water ones.

---

## The body in the volume

`CGameMovement::CheckWater` (`FUN_101252e0`) classifies with three `MASK_WATER` point queries
and writes `m_nWaterLevel` (`player+0x3e0`) and `m_nWaterType` (`+0x3e4`):

1. **Feet** — hull XY centre, `mins.z + 1`. Hit → level 1.
2. **Waist** — same XY, the hull midpoint (`0.5`). Hit → level 2.
3. **Eyes** — `origin.z + viewoffset.z`. Hit → level 3.

It returns `level > 1`, which `FullWalkMove` uses to skip gravity and enter `WaterMove`
(`FUN_101200c0`: full 3D wishvel, idle sink **40**, speed × 0.8, friction with no stopspeed floor,
accelerate on `sv_wateraccelerate`). `CheckWaterJump` (`FUN_1011fb50`, level 2 only) traces 24
units from waist+8 and on a wall sets `vel.z = 260`, `m_flWaterJumpTime = 2000`; `WaterJump`
(`FUN_1011ffe0`) ticks it. `CheckJumpButton` refuses a normal jump at level ≥ 2. Animation selects
`ACT_SWIM` / `ACT_TREADWATER` from the level (`docs/vtmb/animation_and_movers.md`).

**NPCs get a water level off a different caller.** Every `MOVETYPE_STEP` entity calls
`PhysicsCheckWater` (`FUN_1003e880`) at the top of `PhysicsStepRunTimestep` (`FUN_1003b190`) and
reads `m_nWaterLevel` to decide whether to apply gravity that tick.

**No drowning.** `CBasePlayer` carries the HL2 drown kit (`m_AirFinished`, `m_idrowndmg`,
`m_nDrownDmgRate = 2`), set once at spawn and read back by nothing but the save dumper. A vampire
does not need to breathe, and the code agrees.

**Reachability.** Collision leaves water passable (`BLOCK_MASK` excludes `0x20`). `sm_pier_1`'s
spawn is 63 in above `surfaceZ`; `sm_hub_1`'s sewer is `$waterdepth 20` (about 50 cm), so a
standing player is level 1 and a drop-in 2–3; `sp_soc_3`'s basin is 464 in deep. `hw_vesuvius_1`,
`ch_temple_2` and `sp_giovanni_2a/2b` have pools and no matching `trigger_hurt`. The
`barrel_fire_burn` hurt rows on `sm_hub_1` / `la_hub_1` sit at street level or at z = −7300, far
below the canal.

---

## Open questions

- The exact per-frame composite of `WaterWarp_old`, the overlay card and the `dsp_water` switch
  under the plane: each knob is identified, the combination is behind an unresolved vtable slot.
- `sm_pier_1`'s suspected ocean-kill `trigger_hurt` wants its brush hull read.
- Whether LMG `cheap_water` / `blackwater` bind the bumped-envmap program (the one with Fresnel).
- Whether vphysics applies fluid damping to the player's own shadow (needs `vphysics.dll`).
- `la_dane_1`'s `dev/dev_water` authors no `$normalmap` and the owner's frame shows broad
  undulation; the "dead vertex path" reading is proven only for units carrying `$normalmap`.
- Cheap sweeps nobody has run: leaf bit `0x1000`; whether the orphaned `PRIMITIVES` lumps on
  `hw_warrens_2b` / `4` are a general UP-recompile pattern; which numbered DSP preset is
  `dsp_water`'s 14 in the exported preset set; what worldspawn `sounds "1"` means.
