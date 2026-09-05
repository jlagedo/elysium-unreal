# Water — the decoded-datum census

Every decoded datum that touches water on `sm_pier_1` (the pier and the beach) and `sm_hub_1`
(the sewers), each one CONSUMED, DROPPED or RULED_OUT on evidence, plus the eight read-only
Phase 0 lanes that settled what the first pass could not. This is the **evidence** behind the
water rulings; the rulings themselves and their Unreal design are
`docs/architecture/water-architecture.md`, and the engine-neutral inventory is `docs/vtmb/water.md`.

**Sources of truth.** The Unofficial-Patch builds of `sm_pier_1` / `sm_hub_1` (the units publish
`sourceResolution.policy = "up-first"`), cross-read against the retail `Vampire\maps\*.bsp` twins,
against the exported map / material / texture / particle units, against the staged import tree, and
against the decompiled game (`engine.dll`, `vampire.dll`, `client.dll`, `MaterialSystem.dll`,
`stdshader_dx8.dll`).

**Conventions.** Source units are inches unless marked; glTF/Unreal units are metres/centimetres
as the unit publishes them. `1 in = 2.54 cm`. Source→Unreal: the water plane at source
`z = -623 in` is `-1582.42 cm`; `z = -5881 in` is `-14937.74 cm`.

**Provenance of the census.** Six domain audits were commissioned; three delivered complete
inventories (faces-geometry, water-collision-visibility, materials — the last truncated after the
`$subdivsize` census). The textures, lighting and entities inventories reached the first pass only
through a completeness critic's cross-checks and four gap investigations. Eight read-only Phase 0
lanes (U1a materials, U1b textures, U1c lighting + entities, U1d effect assets, U2 primitives,
U3 fluid controller, U4 corpus, U9 port state) then measured what those gaps left open; §6–§8 are
their verdict. Where two auditors disagreed, both sides are named in §5.

---

## 1. Verdict

**No decoded datum on either map supports relief, tessellation or vertex animation on the water
surface. Both water surfaces are provably flat to the inch, and the port reproduces that flatness
faithfully. What VtMB *does* have on these two maps is shader-space motion, one genuine per-face
lightmap animation on the pier waterline, and at least two real water-triggered runtime events.
Separately, and more seriously than any missing motion: at the time of this census `sm_pier_1`
drew no water surface at all.**

**Flatness, proven six ways, not assumed.** (1) `dispInfo = -1` on all 99 pier and all 47 hub
water-material faces; no displacement anywhere on either map carries `CONTENTS_WATER` (the pier's
15 are contents `0x1` or `0x10000002`, the hub's 9 are all `0x1`), and `minTess` is `0` and
`smoothingAngle` `0.0` on all 24 displacements across both maps. (2) Every per-vertex normal on
every water face equals that face's plane normal exactly, with no variation — `sewer_water` yields
one distinct NORMAL over all 23 faces, `dev_waterbeneath2` one over 24, `blackwater` one over 15,
`objects/surf` one over 34, `invisible_water_depth_33` one over 9, and `invisible_water` exactly
five, one per brush plane, never mixed within a face. (3) Every water vertex of a given surface
sits at exactly one height: the hub's 47 faces all at source `z = -5881.0` (staged
`Z = -14937.739 cm` on all 314 vertices), the pier's 9 `depth_33` top faces all at `-623.0`,
`blackwater` at `+16.0` local, `objects/surf` at `+0.5` local. (4) The `PRIMITIVES` / `PRIMVERTS` /
`PRIMINDICES` triple — Source's carrier for a compiled water tessellation — is length 0 on **both**
the retail and the Unofficial-Patch build of both maps, and `numPrims` at `dface+100` is `0` on all
5,450 UP-pier, 14,032 UP-hub, 5,149 retail-pier and 14,032 retail-hub faces. (5) `WATEROVERLAYS`
(lump 50) and `OVERLAYS` (lump 45) are length 0 on **all 209 map-files in the corpus**, retail and
patch alike. (6) The mesh build subdivides nothing: the decoder emits a corner fan over the face's
own 4..17 corners and the importer re-winds it.

**The one place VtMB water genuinely is tessellated is elsewhere in the corpus** — 9 UP maps carry
`PRIMITIVES` on their water faces (512 faces: `ch_hub_1`, `hw_ash_sewer_1`, `hw_hub_1`,
`hw_warrens_1/2/3/5`, `la_hub_1`, `la_plaguebearer_sewer_1`), and 100 % of primitive-bearing faces
corpus-wide are water faces at texinfo flags `0x408`; `hw_warrens_1` face 2300 decodes to a
9-vertex / 13-index triangle strip on a 64-unit grid, matching `water/warrwater`'s
`$subdivsize 64.0` exactly, and 100 % of those 512 faces bind a material authoring `$subdivsize 64`.
Even there every primitive vertex of a face shares one Z, so it is a finer static tessellation, not
a wave.

**The motion that is real on these maps is shader-space.** `SURF_WARP` (`0x8`) is set on every
water texinfo on both maps (pier `0xC98`, hub `0x408`). The 29-frame `dev/water_normal` flipbook
and the 29-frame `dev/water_dudv` DUDV are the two halves of the authored animation, driven by one
shared `$bumpframe` at 30 fps with a `TextureScroll` at 0.05 / 45°.

**One genuine per-face runtime animation exists, on the pier waterline.** All 34 `OBJECTS/SURF`
faces carry lightstyle 1; 21 of them also carry the switchable style 32 (18 as `[0,32,1,255…]`,
3 as `[0,1,32,255…]`, 13 as `[0,1,255…]`). `CWorld::vfunc104` (`vampire.dll 0x1023c020`) registers
style 1's pattern at map load as `"mmnmmommommnonmmonqnmmo"`, and `Mod_LoadFaces`
(`engine.dll 0x200b73d0`) sets `surfflags |= 0x2000` on any face whose style is neither 0 nor 0xFF.
`blackwater` carries only static style 0; the hub sewer water and the pier invisible water carry
`[255]*8` — no styles at all, and `lightOffset -1`, so there is no page to key a style against.

**Water-triggered runtime events exist, in two independent places.** (a) The map's `PHYSCOLLIDE`
keyvalue tail publishes a `fluid { }` block on **both** maps — pier: `index "5"`,
`density 1000.000000`, `damping 0.010000`, `surfaceplane 0 0 1 -623`, `currentvelocity 0 0 0`; hub:
`index "5"`, `surfaceprop "water"`, `damping 0.010000`, `contents "268435488"` (`0x10000020`),
`surfaceplane 0 0 1 -5881`, `currentvelocity 0 0 0` — naming solid 5, whose `upperLimitRadius`
(17.0735 m pier, 65.079 m hub) matches each `CONTENTS_WATER` brush's half-diagonal exactly. Server
(`vampire.dll FUN_10158600`) and client (`client.dll FUN_10120230`) both parse it and call
`IPhysicsEnvironment::CreateFluidController` on a solid whose surfaceprop is `water`. (b) On
`sm_hub_1`, six `trigger_multiple` volumes named `Sewer Scheme` / `Sewers Scheme` (entities
2491-2494, 2496, 2500; models `*117`-`*120`, `*122`, `*126`) straddle the sewer water plane — world
z spans reaching `-5944` — and each carries the outputs `SchemeSewers,FadeIn` and
`Scheme_SM_Streets,FadeOut`. Six manhole-pursuit triggers (2400-2403, 2417, 2418) and one
`trigger_changelevel` (`sewer_map_trigger`, 2371, `-5885..-5805`) also reach below the surface.

**One provenance fact that conditions four of the findings.** The `sm_pier_1` the pipeline exports
is not Troika's map. Retail `sm_pier_1.bsp` is mapRevision 1994, 5,149 faces, 4,190,396 B; the
Unofficial-Patch build the exporter reads is mapRevision 218, 5,450 faces, 14,036,569 B — a fresh
recompile (the Hammer save counter went *down*). The UP build **adds** the entire visible ocean
card (`water/blackwater.vmt`'s first comment is literally `// added by psycho-a`), the `_depth_33`
patch, `BUMPLIGHT` on 88.5 % of faces (retail: 0.0 %), lump 46, lump 47, a 22,288-byte lump-8
average-colour prefix and 27 non-degenerate `surfaceFogVolumeID` values; and it **destroys** 12
water-hull bottom faces at `z = -657` (1,205,760 sq.in, −46 % of the map's `INVISIBLE_WATER` area),
two retail `CONTENTS_WATER` brushes, and both of VtMB's leaf annotations (`0x200` 702 → 0, `0x800`
702 → 0), which disables the shipping engine's entire expensive-water render path on that map.
`sm_hub_1` is retail-authentic: 33 of its 40 populated lumps are byte-identical between the two
builds.

---

## 2. The consolidated gap list, G1–G26, each with its resolution

What the datum is, what consuming it buys, and — the row this document exists to carry — **how it
was dispositioned** by the water-complete pass (`docs/project/seam_migration.md` → "R7.1 —
settled"). The rulings the dispositions rest on are `water-architecture.md` §1 and §1.1.

**G1. `sm_pier_1` drew no water surface at all.** All 41 `WATER/INVISIBLE_WATER` and all 9
`invisible_water_depth_33` faces are `SURF_NODRAW` (`0xC98`) and were dropped by the exporter's
nodraw rule, while `AElysiumWaterVolumes` is planes + AABB + fog with no mesh and no material — so
nothing replaced them. The legacy lane drew it (174 triangles at `-1582.42` and `-1666.24 cm`).
**Buys:** the entire visible water of the pier, at the authored plane `z = -623 in`.
→ **Fixed** in `exporters/UE_map_sidecars.py::meshed_faces` (a `%compilewater` face is never
dropped for `SURF_NODRAW`; the predicate is `importers/map_geometry.compile_water_predicate` and
takes the face's own TEXDATA key, not the base fold), in
`importers/materials.py::_apply_compile_water_reroute` (the unit resolves to `M_V2_Water` because
VBSP — not the shader name — is what makes a brush a water brush), and in the bake's section
binding. Owner decision 2, named modernization **"surface on nodraw water"**. Measured:
`sm_pier_1` meshes 50 water faces (23 vertical + 18 underside `water/invisible_water`, 9 up-facing
`_depth_33`).

**G2. No cubemap is bound to any water surface on either map.** Hub probe **#17**
`(-1612,-111,-5876)` is the only environment probe in the sewer and sits 5 inches above the water
plane; neither water face binds it. The pier has three probes inside the water footprint (#5, #7,
#30, 46-234 in above the surface) and none is bound; its ocean card binds `cubemaps[0]`, the
**3D-skybox** sample. Settled precondition: nothing was lost in export — the hub PAKFILE proves
vbsp never wrote a patch VMT naming probe #17.
→ **Ruled out** because the port's water reflection is Lumen's, not a cube's (ruling A). `$envmap
env_cubemap` means "the map's own probe", which binds no texture here (`WaterParams`'
declared-not-wired `UseEnvMap` row); only an **authored fixed cube** has a sample to read, and
neither water unit authors one. The reflection captures the bake now builds (19 on `sm_hub_1`,
41 on `sm_pier_1`, all reaching disk since the `stage_level` double-build fix) are what a bound
probe would have been, and they feed Lumen rather than a `$envmap` slot.

**G3. The sewer's 29-frame DUDV animation reached no parameter.** `water/sewer_water` authors
`$bumpmap dev/water_dudv` (256×256, 29 frames, UVWQ8888, flags `0x340`); the unit publishes it as
`DuDvMap`; `MI_sewer_water`'s baked `textures{}` contained only `NormalMapFrames`. U1a pinned the
mechanism exactly: `M_V2_Water` *does* declare `DuDvMap` and the importer *does* attempt the bind,
but `_bind_texture` rejects the 29-frame `Texture2DArray` against the `Texture2D` slot
(`textureClassMismatch`), and the `animatedtexture` proxy that would animate `$bumpmap` was
redirected to NormalMap's own frames — doubly unreachable. **Buys:** the sewer water's authored
refraction motion, the second half of its shader-space animation.
→ **Fixed** in `pipeline/unreal/make_v2_materials.py` (`DuDvMapFrames` texture, `DuDvFrameRate` /
`DuDvFrameCount` scalars, `UseAnimatedDuDvFrames` switch; the 29 signed-UVWQ slices are added into
the normal's tangent XY and renormalised through the new `matgraph.Graph.normalize`) and
`importers/materials.py` (on the water family the proxy binds the
DUDV lane instead of stealing the normal's, and the normal frames come from `$normalmap`'s own
texture). A generated default array
`/Game/ElysiumGenerated/Materials/V2/T_V2_DefaultDuDvFrames` ships with it — it cannot be
`T_V2_DefaultNormalFrames`, which is `TC_NORMALMAP` where this lane samples
`SAMPLERTYPE_LINEAR_COLOR`; either pairing is a real sampler-type compile error. Evidence on disk:
`MI_sewer_water` carries `{DuDvMapFrames, NormalMapFrames}`, `UseAnimatedDuDvFrames: true`,
`DuDvFrameCount 29`, `DuDvFrameRate 30`, `RefractAmount 35`, `ReflectAmount 60`. `Water_Old`
perturbed the UVs of BOTH render targets by that DUDV field, scaled per pass by `$refractamount` /
`$reflectamount` (VS `c44`, both `/100`); SLW has a single normal to offset the scene along, so the
two per-pass strengths fold into their mean, `(RefractAmount + ReflectAmount) / 200` — a named
divergence stated in `_build_water`'s docstring, not a silent averaging.

**G4. "The compiler's own resolved water parameters were destroyed upstream and are not
re-derived."** Retail `maps/sm_hub_1/dev/dev_waterbeneath2.vmt` is 977 B of fully expanded `Water`
block — `$refractamount 50.0`, `$refracttint [0.95 1.0 0.97]`, `$reflectamount 50.0`,
`$reflecttint [1 1 1]`, `$CHEAPWATERSTARTDISTANCE 500.0`, `$CHEAPWATERENDDISTANCE 1000.0`,
`$fogcolor {22 20 10}`, `$fogstart 1.00`, `$fogend 400.00`, `Proxies { AnimatedTexture($bumpmap,
$bumpframe,30.00) TextureScroll($bumptransform,.05,45.00) }` — and the UP replaced it with a 117 B
patch stub.
→ **Reframed, then fixed.** U1a measured that the values are *not* lost: the corpus-wide base unit
carries the full expanded set and patched instances inherit it through Unreal's
`MaterialInstanceConstant` parent chain, independent of `shaderResolution.resolved`. The gap was
graph wiring only, and the wiring landed: `RefractAmount` / `ReflectAmount` (as the DUDV warp
strength, G3), `BaseReflectFract`, the fog quadruple and the two scroll rates are all live on
`M_V2_Water`. `CheapWaterStartDistance` / `CheapWaterEndDistance` are deliberately **dropped** and
named — a 2004 two-program distance swap has no port under one shading model per material — and
every other unwired scalar is named in `WaterParams`' docstring with the measurement behind it
(§7, Lane C, and `water-architecture.md` §4.6).

**G5. The pier's ocean card is baked Opaque with its authored envmap knobs disabled or ignored.**
`$envmapcontrast 0.85` was explicitly ignored; `//"$envmaptint" "[.4 .4 .4]"` and
`//"$translucent" "1"` are authored-then-commented in `water/blackwater.vmt` and decoded into
`comments[]` with no consumer; and its probe `maps/sm_pier_1/c-1241_22_4950` loses its entire lower
mip pyramid (114,681 B under the mislabelled `low-res-cpu-sample` role, `vtfMipCount 8` vs
delivered `mipCount 1`).
→ **Fixed in three places.** (a) `$envmapcontrast` → `EnvMapContrast` on `M_V2_Lit` /
`M_V2_LitTranslucent`, restored with Source's own stated meaning `lerp(cube, cube×cube, contrast)`
on the fixed-cube branch only — authored intent the 2004 engine dropped (`docs/vtmb/reflections.md`
measured that no shipped `.psh` carries a term for it), default 0 so the 19,000+ instances that do
not author the key are bit-identical. (b) The mip collapse is fixed in
`formats/texture_glb/decode.py`: where the outer TTH mip table under-declares but the VTF header
names the full chain and the blob is exactly that pyramid, the VTF header's count wins. 577 of 592
candidate units recover their chain, 15 correctly keep the old reading, 0 decode or validation
failures across all 592; both pier probes fixed (`c-1241_22_4950` 1 → 8 mips, `cubemapdefault`
1 → 6) — U1b's extension of this row from "the ocean probe" to "both pier probes". (c) The
commented keys are recorded, not silently lost:
`materials.py::_record_authored_then_removed` emits an `authoredThenRemovedKey` omission for every
commented-out `"$key" "value"` line (1,162 lines across 787 units corpus-wide). An author's own
removal is not a port gap — the keys stay off.

**G6. The 34 waterline foam cards are lightstyle-animated in VtMB and were static in the port.**
All 34 carry style 1, 21 also carry the switchable style 32, and `Mod_LoadFaces` flags them
`surfflags |= 0x2000`. The runtime already had the pattern clock; what was missing was the
face → style binding. **Buys:** a visible flicker on the pier waterline — the only authored
per-face motion at the water on either map.
→ **Fixed** across four lanes as one contract, owner decision 4, named modernization **"lightstyle
as brightness"**: `map_geometry.face_light_style` splits a styled face into its own
`(material, style)` chunk under a `#style<n>` section key; `bake_map.chunk_style_suffix` /
`chunk_actor_tags` name and tag the chunk `elysium.style=<n>` and stamp CPD slot 6 = 1.0;
`ElysiumLightStyle::SlotBrightness` (`Source/ElysiumUE/Public/ElysiumFog.h`, `static_assert` on 6)
and `UElysiumLightRig::AdoptStyledPrimitives` rewrite that float each tick from the same
StyleTime/StylePatterns pair a styled light uses; `make_v2_materials._light_style_brightness`
multiplies the lit base colour and the emissive on the Lit, LitTranslucent, TwoTexture and Water
masters. VtMB sums one lightmap page per `styles[]` slot; Lumen replaced the lightmap, so there is
no page to swap and the style is spent as a brightness. The **lowest** style wins, not the first
slot — 18 of the 34 foam cards name the switchable 32 in the earlier slot, so slot order would have
split one waterline into two chunks on two patterns.

**G7. The `fluid { }` block — splash, buoyancy and water-entry state — was decoded and thrown
away.** It names solid 5, whose `upperLimitRadius` matches each water brush's half-diagonal
exactly. **Buys:** the entry event and `damping` / `density` for anything floating.
→ **Fixed, and its runtime rule rewritten from the right source.** The block is staged as
`water.volumes[].fluid` (`index`, `density`, `damping`, `surfacePlane` in Unreal cm,
`currentVelocityCm`, `contents`, `surfaceProp`) and carried onto `FElysiumWaterFluid`. U3 settled
the creation guard as `if (fluid.index > 0)` at the instruction level — not contents, not solid
flags — so **both maps get a controller**, the pier included. But **the controller emits nothing**:
`FUN_10151150` is Source's `PhysicsSplash` with both `DispatchEffect` calls stripped (0 strings
over a 1952-byte listing), i.e. dead code in the shipped build, so its speed-scaled,
count-randomised numbers are deliberately **not** reproduced. The splash comes from the client's
water-level transition instead (§6, U3). Buoyancy spends the two authored numbers as Archimedes
over the body's AABB and as linear damping — `ElysiumWater::SubmergedFraction` /
`DisplacedVolumeM3` / `BuoyantForceZ`, `AElysiumWaterVolumes::TickFluidBodies` — a named
modernization, **"vphysics buoyancy"**, because vphysics' fluid controller is not in the Ghidra
corpus and cannot be reproduced instruction for instruction. Nothing displaces the surface: U2
measured that VtMB never did either.

**G8. `sm_hub_1`'s six `Sewer Scheme` triggers straddle the water plane and their outputs were not
known to be fired.** Entities 2491-2494, 2496, 2500, each with `SchemeSewers,FadeIn` /
`Scheme_SM_Streets,FadeOut`. **Buys:** the ambient audio scheme change on entering the sewer water.
→ **Verification, not implementation.** The outputs are staged entity rows and the entity-I/O lane
fires staged outputs generically; U1c added that four more triggers with identical I/O sit 15-27 in
above the plane, and that the pier's `logic_auto` idx 900 fires `OnMapLoad →
world.FadeGlobalWetness(1)` — a chain the runtime already implements end to end (§8, contradiction
4). What remains is a game witness (U7): read `elysium_io_history` and `elysium_audio_state` after
teleporting into the sewer, and read the pier's `FadeGlobalWetness` beside it.

**G9. The hub's precomputed near-water leaf set was free and unread.** `leaf.contents & 0x200`
marks 97 leaves — the 21 water leaves plus 76 at or above the surface, median 26 in and max 449 in
from the water hull — and it is provably `⋃PVS(water clusters)` with zero discrepancy. In VtMB this
single bit gates `ViewDrawScene_EyeAboveWater` / `_EyeUnderWater` against `_NoWater`. **Buys:** a
free authored gate for an underwater post-process, a near-water audio submix or reflection LOD.
Caveat: the UP pier carries stock Source's `0x100` instead, which excludes the water leaves — a bit
VtMB never reads.
→ **Fixed, and derived rather than read.** `importers/map_visibility.py` (`read_visibility`,
`MapVisibility.pvs_union`) is the visibility sub-unit's first reader; `map_geometry._near_water_boxes`
publishes `water.volumes[].nearBoxesCm`, carried onto `AElysiumWaterVolumes::NearBoxesCm` and
answered by `ElysiumWater::FindNearVolumeAt` / `IsNearWater`. Named divergence: **the set is derived
from the PVS, never read off the leaf bit** — fidelity-preserving, measured set-equal to VtMB's own
`0x200` annotation on `sm_hub_1` (97 leaves) and `sp_soc_3` (126), and the only answer on
`sm_pier_1`, whose UP recompile carries the bit on no leaf at all (retail: 702). Solid leaves are
excluded — vbsp's dummy leaf 0 is a zero-extent box in cluster 0 that the pier's water can see, and
0 of the leaves VtMB annotated on either map are solid.

**G10. `leafWaterDataID` and the water leaves' bounding boxes reached no consumer, and our hull was
wrong by 66-77 inches.** `leafWaterDataID` is `0` on exactly the 6 pier / 21 hub water leaves and
`-1` elsewhere — the engine's own "is the eye under water" answer in one lookup. The 21 hub leaf
boxes union to 3056 × 3744 in against the staged brush's 3333 × 3890 in, so the port's hull spilled
66-77 in into the sewer walls on each horizontal axis.
→ **Fixed:** `water.volumes[].leafBoxesCm` (one `{min,max}` per LEAFWATERDATA leaf) →
`FElysiumWaterVolume::LeafBoxesCm`. 27 boxes across both maps.

**G11. `surfaceFogVolumeID` gives the pier a compiler-authored top/underside face split.** `0` on
exactly 27 faces — 9 `depth_33` on plane 644 (normal +Z) and 18 `invisible_water` on plane 645
(normal −Z), all at `z=-623` — and `0xFFFF` on the other 5,423. **Downgraded** by gap investigation
4: the engine forces `0xFFFF` on every non-WARP face and every WARP face on both builds ends at
volume 0, so the runtime state is identical retail↔UP, and the values exist only in the recompile.
→ **Published, not relied on.** `water.faces[].surfaceFogVolumeID` is staged per face. The
top/underside split is keyed off the **face's own plane normal** (`plane.normal.z < 0` in Unreal
space, `map_geometry.face_underside`, verdict B2), because that is what VtMB itself keys on and
because the field is a UP-only encoding. The two agree face for face on the pier's 27.

**G12. `origFace` is the only pointer back to the unsplit authored water quad.** The hub's entire
sewer water body (46 of 47 faces) points at `originalFaces[6768]`; the pier's `blackwater` is 15
shards of `originalFaces[3118]`; each of the 17 foam cards is exactly 2 shards of one 4-corner
original.
→ **Ruled out as a mesh source, kept as a grouper.** U4 measured it: `origFace` groups shards
reliably (79.5 % of `side=1` faces share their origFace index with a `side=0` front face) but it is
the **pre-CSG brush face**, not a drawable polygon — `originalFaces[].area` is 0.0 on all 2,182
rows, and only 26.5 % (36/136) of multi-shard groups reconstruct within 10 % of their shards' area
sum (`sm_hub_1` origFace 6768 reconstructs to 12,965,370 in² against 2,496,000 in² of surviving top
shards). The water mesh therefore takes the compiled primitive grid where one exists and the shard
fan otherwise (verdict B3), and the area pin uses `faces[].area`, never `originalFaces[].area`.

**G13. VtMB's compiled water tessellation was dropped corpus-wide, and the field that addresses it
was misdecoded.** `lumps.py` unpacked `<iI` at `dface+96` and named the second dword
`smoothingGroups`; it is `numPrims` @100 + `firstPrimID` @102, confirmed at instruction level
(`Mod_LoadFaces FUN_200b73d0`, `200b7648 MOV AX,[EDI+0x64]`, `200b7650 MOV CX,[EDI+0x66]`, msurface
stride `0x98`, runtime offsets `msurface+0x50` / `+0x52`). Consequence: `primitives[]` was
published and unreachable on every map.
→ **Fixed** in `formats/map_glb/lumps.py` (three fields: `origFace` i32 @96, `numPrims` u16 @100,
`firstPrimID` u16 @102; the `smoothingGroups` name is gone from the product), with the map seam at
SCHEMA_VERSION 1.1.0, `primitives[]` gaining `typeName` / `reachedBy` / `vertexAttributes`, and
`census.primitiveBearingFaces` measured on real data (`hw_warrens_1` 66 faces / 66 runs / all
`strip` / 0 out-of-range; `sm_pier_1` 0). Consumed as `water.faces[].primitive {first, count}`.
**Lump 38 carries positions only** (`Mod_LoadPrimVerts` zero-fills the 28-byte runtime record and
copies 12 bytes), so the shipped engine feeds `TEXCOORD0/1 = (0,0)` on compiled strips and the port
re-derives UV0 from the texinfo vectors and takes the normal off the face plane (verdict A2). Zero
effect on the three converted maps (0 primitive faces); the 512 faces are on 9 other maps, so the
primitive path is covered by synthetic-unit tests and the water look is not gated on it.

**G14. The pier's *drawn* water carries real baked lighting we bake nothing from.** `blackwater`:
`BUMPLIGHT`, 15 real `lightOffset`s, luxel pages up to 33×33, `avgLightColor` all-zero.
`objects/surf`: 34 real offsets, pages up to 33×19, three lightstyles, and non-zero `avgLightColor`
in slots 0 and 2. The luxel UV is computed, written as TEXCOORD_1 and then discarded, because the
importer's vertex emitter takes position and UV0 only.
→ **Ruled out** because Lumen replaces lightmaps project-wide — there is no lightmap lane to feed,
which is the same fact ruling M rests on. `avgLightColor`, the luxel pages and the offsets stay as
provenance. Note the caveat this row carries: the pier's bumped lightmaps are a UP recompile
switch; retail has none.

**G15. The VTF `NORMAL` bit would delete a 2.53 MB unused texture with a one-line change.**
`dev/water_normal` carries flags `0x2C0` with bit `0x80` set and nothing decoded the word. The only
reason the sRGB twin existed is that `%tooltexture ∈ COLOUR_PARAMETERS` — and `%tooltexture` is
provably Hammer's thumbnail key (`MaterialSystem.dll 10003180`, 2 strings in the whole corpus, no
shader reference). `TA_water_normal.uasset` was 2,534,259 B, bound by **0 of 19,713** material rows.
→ **Fixed** in `importers/textures.py`: `VTF_FLAG_BITS` / `VTF_NORMAL` / `VTF_NAMED_FLAGS` /
`vtf_flag_names`, and the sidecar gains `flagNames` + `unnamedFlagBits`. A role conflict whose flag
word carries `normal` (0x80) resolves to `(data-normal, srgb=False, conflict=False)` and stages ONE
asset, no `_linear` twin. Used **strictly as a conflict tie-breaker, never as an sRGB oracle** —
U1b measured that no VTF flag bit encodes sRGB at all (`PRE_SRGB 0x80000` clear on all nine
water-cast textures) and 27 units carry a normal binding with the bit clear. Verified against the
live corpus: `dev/water_normal` {`$bumpmap`, `$normalmap`, `%tooltexture`} 0x2C0 → data-normal /
no twin; `effects/ref_12` (0x0) keeps its twin — U1b's second finding, that `effects/ref_12`
carries a role conflict from 15 unrelated `$envmapmask` bindings; `dev/water_dudv` unchanged (the
vk-41 UVWQ carve-out still wins). Consequence for the material lane: `dev/water_normal` binds as
`TA_water_normal`, not `TA_water_normal_linear`.

**G16. `$bottommaterial` — the author's own statement of what the underside of the water is — was
dropped and misdecoded.** `water/sewer_water` names `dev/dev_waterbeneath2`, which is exactly what
vbsp painted on the hub's 24 down-facing faces; `water/invisible_water` names itself (which is why
retail carries a second coincident sheet at `z=-657`). The unit published it as
`textureBindings[].kind = "texture"` with `resolved = false` — a material path misclassified as a
texture, and the only unresolved bindings in the water cast.
→ **The decoder defect is fixed; the material-level rule it implied is retired.** The material unit
gains `materialReferences[]` (`{parameter, value, asset, resolved, selfReference}`) and
`$bottommaterial` / `$crackmaterial` / `$modelmaterial` / `$leaknoise` leave `textureBindings[]`
entirely (material seam SCHEMA_VERSION 1.2.0); corpus census: 59 units author a material-shaped
key, 58 resolve (24 `$bottommaterial`, 3 of them self-references; 21 `$crackmaterial`; 11
`$modelmaterial`), the one unresolved row being the malformed `shadertest/shootmetext`
`$leaknoise "0;"`. But the **underside is per face**, not per material (contract 1, verdict B2):
VtMB's own response to a down-facing water face is to undefine `$reflecttexture` on the shared
material at load, which is load-order dependent, so the port keys off the face and stages an
`MI_<unit>_Underside` twin beside **every** water instance (`materials.UNDERSIDE_INSTANCE_SUFFIX`,
parented to the surface instance, stating exactly `{"Underside": True}` and inheriting everything
else). The material key stays published as a resolved reference and the retirement is recorded per
unit as `bottomMaterialNotAnUndersideSwitch` (24 units). 108 `_Underside` twins imported
corpus-wide.

**G17. The physics `materialtable`'s water index was unread — and on the hub it does not exist.**
Pier: 18 rows with `WATER = 17`. Hub: 16 rows with **no water row**.
→ **Fixed (published):** `water.volumes[].materialTableWaterIndex`, int or null — 17 on the pier,
null on the hub, which is the honest answer for a map whose compiler wrote no water row.

**G18. The compiler's own convex decomposition of the water volume was unread.**
`physics.models[0].solids[5]`: pier 1 ledge / 12 triangles / 8 vertices, `upperLimitRadius
17.0735 m`, `massCenter [85.3439, 16.2560, -28.1432]`; hub 5 ledges of 12 triangles, `65.079 m`,
`rotationInertia [813.329, 1009.219, 597.512]`. **Buys:** 5 convex pieces instead of 1 box on the
hub.
→ **Fixed:** `water.volumes[].pieces` (`{planes, boundsCm}`, deliberately the same row shape as
`brushes[]` so one struct serves both) → `FElysiumWaterVolume::Pieces`, which
`ElysiumWater::FindVolumeAt` tests ahead of `Brushes` when the volume publishes any.

**G19. `water.leafMinDist` is a real per-leaf distance-to-water field on the pier and it is
dropped.** 534 real values 16..4528 in, 35 zeros (6 water leaves + 29 touching), 1,581 `0xFFFF`
sentinels; hub all-zero.
→ **Ruled out because the engine never loads it.** Lump 46 appears in no
`CMapLoadHelper::LoadLump` call site in `engine.dll` (exhaustive union
`{0-8, 10, 12-21, 26, 29-39, 41-45, 48}`) and is length 0 in retail on both maps. It is a
convenience, not fidelity; it is derivable from the hull the port stages; and the near-water gate it
would have served is G9's PVS set. Ruling G.

**G20. There is no lane for VtMB's global wetness.** `wetness_fadetarget` / `fadein` / `fadeout` are
decoded into a v1 sidecar; both maps author target `0.0`, so nothing is lost here.
→ **Ruled out because the lane exists.** The runtime implements the whole chain: `logic_auto` fires
`OnMapLoad`, the world entity exposes `FadeGlobalWetness`, the transition duration comes from the
worldspawn `wetness_fadein` / `fadeout` keys read into `FElysiumWeatherState::Configure`, and the
presented value is written to `MPC_ElysiumEnvironment.GlobalWetness` every tick — with a regression
test replicating the hub's on/off timers. What genuinely remains is that no *water* material reads
wetness, which is deliberate: wetness is a look for the ground **next to** water, and its material
presentation is roadmap 7.9's contract, not the water master's.

**G21. There is no lane for `fluid.currentvelocity`.** `(0,0,0)` on both maps, and no
`CONTENTS_CURRENT_*` bit is set on any of the 1,152 / 4,381 brushes or 2,150 / 4,407 leaves, so
nothing is lost on these maps — but a map that authors a current would lose it silently.
→ **Fixed:** `fluid.currentVelocityCm` staged → `FElysiumWaterFluid::CurrentVelocityCm`. A map that
authors a current no longer loses it silently.

**G22. VBSP's own 36-way split of the pier water sheet was dropped.** 36 texdata rows all named
`WATER/INVISIBLE_WATER` (index 37 plus 354..388), 35 of them named by exactly one texinfo and one
face.
→ **Fixed (published):** `water.faces[].texdata`, one row per `%compilewater` face in face order.
The value stays speculative, as the row said; it costs nothing to carry.

**G23. The whole `<map>.visibility.glb` sub-unit is unpaid.** 782 / 1,164 clusters, 98 / 146-byte
PVS+PAS rows, `portals: {}`. Read by no importer and no bake.
→ **Fixed:** `importers/map_visibility.py` is its first reader — thin by design (it walks the
already-decoded cluster rows and their PVS accessors, no run-length decode) — and it is the
derivation of G9.

**G24. Retail `sm_pier_1` water data the exported build does not contain.** 12 `INVISIBLE_WATER`
faces forming the hull floor at `z=-657` (1,205,760 in², −46 % of the map's water-face area) and two
extra `CONTENTS_WATER` brushes (251, 254, both `0x18000120`, all-`TOOLS_SHADOW` sides, 372-506 in
above the water plane inside the pier structure).
→ **Ruled by the owner (decision 1): the port stays on the UP recompile.** Named divergence
(ruling N): the pier the port converts is the Unofficial Patch's, which adds the visible ocean card
and the `_depth_33` patch and loses the retail hull floor. The two retail brushes are inert — U5
measured that `MASK_WATER = 0x4030` is tested against the **leaf's** contents, not the brush's
(`EngineTraceServer003` slot 0 = `GetPointContents` → `CM_PointContents` returns `leaf->contents`
verbatim), and vbsp propagated no `0x20` into any leaf containing them (all `0x800` / `0x0`, all
`leafWaterDataID = -1`), so water level stays 0 either way and the port's exclusion of
`0x18000120` shadow brushes is correct. The two annotations the recompile destroyed (`0x200`,
`0x800`) are **derived, not read**: `0x200`'s set is `⋃PVS(water clusters)` (G9), and `0x800` is the
precipitation render mask (§8, contradiction 7), which no rain gate may key on for this map — the
UP build has 0 flagged leaves against retail's 702, and the pier places 10 `rain_box_emitter` roots.

**G25. A port-created divergence at the pier: three sky brush hulls sat inside the play volume.**
`brush_8/9/10` are `func_brush → Solid`, sky-flagged, and the collision importer multiplied their
hulls by the sky scale, walking them from raw z ≈ 4,939 (above the map's own `world_maxs.z 512`)
down to world z `-644…-628` — three invisible collision slabs 21 inches under the harbour surface
with no VtMB counterpart. None overlaps the water brush.
→ **Fixed** in `exporters/UE_map_sidecars.build_entities` and `importers/map_collision.py`: sky
entity hulls are not composed into world collision. The miniature is drawn from its own camera and
nothing travels into it, so the faithful hull count is zero. This is the one unconditional `.ents`
divergence from the legacy `UE_bsp_to_scene.py` lane, recorded in `build_entities`' docstring
because it is a ruling, not a disagreement about how to read the lump.

**G26. `faces[].area` — the compiler's authoritative per-face surface area — was unread.** Pier
`blackwater` Σ 36,007,784 in² over 15 faces, `objects/surf` Σ ≈ 2.45 M over 34; hub `sewer_water`
Σ 4,704,256 over 23, `dev_waterbeneath2` Σ 4,848,128 over 24.
→ **Fixed, and tightened.** `water.faces[].area` (in², the compiler's number), `areaCm2`, and
`meshedAreaCm2` (`map_geometry.meshed_area_cm2` — the area of the triangles the port actually
emits) are staged per face, and the corpus test compares vbsp's area against the meshed area **per
face and per section**. Measured: worst face 3e-5 relative, every section 1e-7 — tighter than the
1 % the original contract asked for, and it needs no live-editor geometry query to run.

---

## 3. What the port reproduced at the time of the census

The left column is the measurement; the right is the port **as the census found it**, before the
water-complete pass. Every "nothing" in the right column is dispositioned in §2.

| VtMB, as measured | The port, as the census found it |
|---|---|
| **hub** `LEAFWATERDATA` surface `-5881 in`, one `CONTENTS_WATER` brush 3333 × 3890 × 20 in | `AElysiumWaterVolumes` with the exact planes and `surfaceZCm -14937.74`; hull over-runs the carved leaves by 66-77 in per axis (G10) |
| **hub** 23 `sewer_water` + 24 `dev_waterbeneath2` faces, coplanar at `-5881`, `SURF_WARP`, no lightmap | 109 + 111 = 220 triangles at `Z = -14937.739 cm`, `MI_sewer_water` / `MI_dev_waterbeneath2` on `M_V2_Water` |
| **hub** `$normalmap dev/water_normal` 29 frames + `$bumpmap dev/water_dudv` 29 frames + `TextureScroll(.05, 45°)` | `NormalMapFrames` (29 slices) **only**; no `DuDvMapFrames`; scroll rate not resolved (G3, G4) |
| **hub** `$fogenable 1`, `{5 5 0}`, `1 → 1024 in` | staged and read by the water actor |
| **hub** probe #17 five inches above the water | not bound to any water face; the underside used `cubemapdefault` (G2) |
| **hub** ladder in the water (`CONTENTS_LADDER` leaf 1268, brush 2814) | `0x20000000` reaches nothing; only `CONTENTS_WATER 0x20` is defined |
| **hub** six `Sewer Scheme` triggers straddling the water plane | staged as entity outputs; firing not verified (G8) |
| **pier** `LEAFWATERDATA` surface `-623 in`, brush 480 × 1256 × 34 in, 50 NODRAW faces | `AElysiumWaterVolumes` with the exact planes and fog — **zero triangles, no material, no pixels** (G1) |
| **pier** 34 `objects/surf` foam faces at world z `-618..-621`, lightstyles 1 and 32 | 17 cards × 4 triangles, `MI_surf` on `M_V2_LitTranslucent`, `sine` proxy live, **no lightstyle animation** (G6) |
| **pier** 15 `blackwater` faces on `*6` inside the 3D skybox | 54 triangles placed at world `z = -644 in`, 0.50 × 1.55 miles, 24 fps normal flipbook live, baked Opaque (G5) |
| **pier** `fluid { density 1000, damping 0.01, surfaceplane 0 0 1 -623 }` | nothing (G7) |
| **pier** (retail only) a second water sheet at `z=-657` and two extra water-contents brushes | absent from the source the pipeline exports (G24) |

---

## 4. Rulings that rest on inference, not measurement

Named so the owner can see exactly where the census stopped measuring and started reasoning. None
of these is believed wrong; all of them are cheaper to falsify than to trust. Three have since been
closed and say so.

| Row | The inference | What a measurement would look like |
|---|---|---|
| `faces[].table` | "It is a self-description of the container, so it cannot carry map data." Definitional — the field is genuinely not read from the BSP, but the ruling is a category argument, not a value measurement | none needed; stated as definitional so it is not counted as evidence elsewhere |
| `faces[].sourceOffset` | "Nothing about how water looks or moves can be encoded in a file offset." Definitional | as above |
| `bsp.nodes[].mins/maxs/firstFace/numFaces` | Two inferences: that the water faces are "already reachable" by other routes so the node span adds nothing, and that node ordering is "a rendering-order structure Unreal replaces wholesale" — a project ruling | check whether any water face is reachable *only* through a node's face span (it is not, but nobody measured it), and whether any node bbox tightens the water hull |
| `<map>.visibility.glb` `clusters[].pvs/pas` | "Generic per-cluster bitsets with no water bit, no water field and no water-only structure" + "Unreal owns visibility in this port" | **falsified in substance** — see §5 (8); the bitsets are the derivation of `0x200` and are now read (G9, G23) |
| `occluders` (lump 9) | Beyond the measured "count 0", the auditor added "even when populated, OCCLUSION has no content class; it has no water bit" | decode `hw_sinbin_1.bsp`'s 12-byte lump 9 — the only populated instance in 209 files |
| `displacementTriangleTags` | "A movement/authoring tag, no shading term" — a semantic reading of the WALKABLE/BUILDABLE bits | find (or fail to find) a reader of lump 48 in `engine.dll` |
| MOPP interiors, solids 3 and 4 | "The fluid names solid 5, therefore the MOPP solids are not water." The MOPP *interior* is undecoded, so "nothing water-related inside" is unverified — only the `staticsolid` contents keys were measured | decode one MOPP header and confirm it is a triangle-soup accelerator with no contents payload |
| `faces[].macroTexture` | Proof (3) is an argument from absence: keyword searches return nothing for "macro" | **upgraded to proof** by the exhaustive `CMapLoadHelper::LoadLump` call census — lump 47 is in no call site |
| textures `0x20 HINT_DXT5` and the alpha hint bits | "Compile-time hints whose result is already in the stored format" — measured for `objects/surf` (`0x2000` → BC3 with 8-bit alpha), inferred for the rest | read the stored format of every hinted unit in the cast |
| cubemap `low-end-spheremap` | The unit's own reason ("obsolete Source low-end cubemap fallback") was a bare assertion for the whole first pass | **now proven** by byte arithmetic (49,152 = 128×128×3, 1,392 = a full 32×32 DXT5 chain) |
| `scriptExpressions[]` | "Zero rows name water, splash, wave, drip, rain or surf" — a keyword search. A script can act on water without naming it | resolve what the 71/202 expressions actually call, or check which entities they are attached to against the water volume |
| leaf bit `0x800` | RULED_OUT *for water* on a measurement (absent from all 26 water leaves in both builds), but its actual meaning stayed unresolved | **closed by U6**: it is the precipitation render mask, measurably vbsp's sky-visible annotation — §8 (7) |

---

## 5. Where two auditors disagreed, both sides named

**(1) How far above the water are the pier's 34 `objects/surf` faces?** The **faces-geometry**
auditor read `+0.5 in` off the face plane and concluded "639 inches above the actual
`LEAFWATERDATA` surface … they read as surf/foam lines lying on the sand". The **completeness
critic**, measuring the entity origins: the faces' vertices are **model-local**; the owning
`func_illusionary` entities carry origin z `-618.5..-621.5`, so world z is `-618.0..-621.0`, i.e.
**2 to 5 inches above the water plane** — where a foam line sits. The **lighting** auditor
independently measured "2.5-4.5 in above the harbour water" and the **entities** auditor "1 to 5
inches above". The arithmetic in the faces auditor's own row does not reproduce its figure either
(`0.5 − (−623) = 623.5`, not 639). **Resolution: three domains against one; the "639 inches"
reading and the "beach decal" row are withdrawn. The foam cards are the waterline.**

**(2) Where is the pier's `blackwater` ocean card?** **faces-geometry**: "brush model 6 at
z = +16 in". **completeness critic**: the entity origin `(-1123, 74, 4923)` puts its 15 faces at
world z **+4,939** — inside the 3D skybox (`sky_camera` at `(-1241, 22.5, 4979.25)`, scale 16) — and
flagged the risk that the port bakes it as a giant black slab above the harbour. **Gap
investigation 1**: all three numbers are the same card in three frames — local `+16`, raw world
`+4,939`, and **as actually placed by the runtime `(origin − skyOrigin)·16 + 16·local` →
`z = -644.0 in`, 31,744 × 98,019 in**, meeting the harbour waterline at the horizon. The feared slab
does not exist; the "6938 × 6922" figure was the collision hull's bbox, not the 15 render faces
(1,984 × 6,126 in). **Resolution: the card is placed correctly; the faces auditor's frame was local
and its "639 in above" derivative is void.**

**(3) Does the world physics `materialtable` contain water on both maps?**
**water-collision-visibility**: "pier and hub carry the same table … WATER 17". **completeness
critic**, measured: **true on the pier** (18 rows, `water = 17`), **false on the hub** — 16 rows
with no water row. Both auditors read the same block; only the critic reported per-map.
**Resolution: the critic's per-map measurement stands; the sewer has no water surface index** (G17).

**(4) Did the pier lose its water material and its underwater fog?** **materials**: "`sm_pier_1` has
no water material at all in the port … and with them went the pier's only authored underwater fog
(`{22 20 10}`, 1.00 → 400.00)". **completeness critic**, measured: the staged manifest publishes
`water.volumes[0]` with `fogEnable true`, `fogColor [0.086275, 0.078431, 0.039216]`, `fogStartCm
2.54`, `fogEndCm 1016.0`, `dropped: []` — exactly `{22 20 10}` at 1 → 400 in — and the runtime reads
it; and the pier does stage `water/blackwater@c-1241_22_4950` and `objects/surf`. **Resolution: the
fog claim is false; the defensible narrow claim is that the pier had no `M_V2_Water` instance and no
drawn surface on the swimmable volume** (G1).

**(5) Is `surfaceFogVolumeID` a live water signal?** **faces-geometry** and
**water-collision-visibility** both: yes — 27 non-degenerate values on the pier, a compiler-authored
top/underside split. **Gap investigation 4**: the values exist only in the UP recompile, and
`Mod_LoadFaces` forces `0xFFFF` on every non-WARP face and copies `dface+46` only for WARP faces,
every WARP face on **both** builds ending at fog volume 0 — so the **runtime state is identical
retail↔UP**. **Resolution: all three measurements stand; the datum is downgraded from "the
compiler's exact water-fog boundary" to "a more informative on-disk encoding, present only in a
recompile the engine treats identically"** (G11).

**(6) Is `water.leafMinDist` a live datum we discard?** **water-collision-visibility**: yes — "the
only map in the whole UP corpus with a real distribution … Source's own input to the cheap/expensive
water switch". **Gap investigation 4**: lump 46 appears in **no** `CMapLoadHelper::LoadLump` call
site, and retail carries it at length 0 on both maps. **Resolution: the measurement of the field's
semantics stands (distance in inches, proven to a median ratio of 1.000); the claim that shipping
VtMB used it does not. It is a convenience for the port, not fidelity** (G19).

**(7) What is the leaf-contents marker bit?** **water-collision-visibility**: pier `0x100` and hub
`0x200` are the same compiler-set annotation under two bits, "I cannot name the bit with certainty".
**completeness critic**: they are *not* the same quantity — the pier's 6 water leaves carry **none**
of `0x100`, the hub's 21 all carry `0x200`. **Gap investigation 3** settles it: `0x200` **is**
VtMB's `CONTENTS_TESTFOGVOLUME`, read by exactly one site in the whole corpus
(`engine.dll FUN_20081390`, the body of `CVRenderView::GetVisibleFogVolume`), and it equals
`⋃PVS(water clusters)` with zero discrepancy on four maps; the UP pier's `0x100` is the stock Source
SDK vbsp's version of the same annotation (which excludes water leaves), a bit VtMB never reads.
**Resolution: the collision auditor's "two spellings of one marker" framing is superseded — and the
consequence is that the shipping Unofficial Patch renders `sm_pier_1` through
`ViewDrawScene_NoWater` every frame** (G9, G24).

**(8) Is there anything water-specific in the PVS?** **water-collision-visibility**: no — "the
PVS/PAS rows are generic per-cluster bitsets with no water bit". **Gap investigation 3**:
`⋃PVS(water clusters)` **is exactly** the set the engine flags `CONTENTS_TESTFOGVOLUME` and tests
every frame to decide whether to run the water render path — retail pier 527/527, hub 97/97,
`hw_ash_sewer_1` 656/656, `la_hub_1` 165/165, zero discrepancy. **Resolution: the ruling is
falsified in substance; the bitsets are the derivation of the single most water-relevant bit in the
leaf lump** (G23).

**(9) How many maps carry compiled water primitives?** **faces-geometry**: "11 have non-empty
PRIMITIVES/PRIMVERTS/PRIMINDICES", then listed **9** by name. **completeness critic**: "the 21
map-files corpus-wide that do carry primitives are the same **10** maps in both builds", adding
`hw_warrens_2b` and `hw_warrens_4`. **Resolution, by U4:** 9 UP maps have primitive-bearing
**faces**; `hw_warrens_2b` / `hw_warrens_4` have non-empty PRIMITIVES lumps with **zero** faces
referencing them (retail `hw_warrens_4`: 4/4 referenced) — a UP-recompile regression. The "10/11"
counts were counting lump presence, not reachability. Neither of the two owner maps carries any.

**(10) `$bottommaterial`.** **materials**: RULED_OUT — "the shipped engine has no such string".
**completeness critic**: it is authored on three units of the cast, on `water/sewer_water` it names
exactly the material vbsp painted on the hub's underside faces, and the decoder misclassifies it as
a texture binding. **Resolution: recorded as DROPPED, not RULED_OUT** — the shipped-shader argument
is sound about 2004 behaviour and irrelevant to whether the datum states something true about the
water. U1a's later split is finer still: the decoder defect is live *and* the Underside consequence
already worked through a provenance-row workaround; both halves are true (G16).

**(11) `smoothingGroups`.** The **lighting** auditor used "`smoothingGroups` is 0 on all 99 pier
water-cast faces and all 47 hub water faces" as one of four measured negatives against tessellation.
**faces-geometry** and the **critic** show the field does not exist in the VtMB 104-byte `dface` —
the dword is `numPrims` + `firstPrimID`. **Resolution: the conclusion (no tessellation) is unaffected
and independently proven, but any future ruling that leans on `smoothingGroups` is leaning on a name
that does not exist** (G13; the name is now gone from the product).

**(12) The pier's 1-inch `minZ` discrepancy.** **water-collision-visibility** left it UNCERTAIN
(brush bottom `-657` vs `minZ -656`). **Gap investigation 4**: retail says `minZ = -657.0`, matching
the brush plane, and its patched VMT is named `_depth_34`; the UP vbsp wrote `-656.0` and
`_depth_33`. **Resolution: settled — a recompile difference, not a compiler rounding rule.**

**(13) Does any water-triggered event exist?** **entities-events-audio**: "NO water-triggered runtime
event, sound or effect exists in VtMB's own data". **completeness critic**: six `Sewer Scheme`
volumes straddle the sewer water plane with `SchemeSewers,FadeIn` outputs, plus six manhole triggers
and a changelevel reaching below the surface. **water-collision-visibility** independently supplies
the second event source (the `fluid` controller). **Resolution: the negative is false; two
independent event sources exist** (G7, G8) — and U3 later found a third and more important one, the
client's own water-level transition (§6).

**(14) `startFrame` on the water cast.** The **textures** auditor: "startFrame is 0 on both water
arrays so it costs nothing here". The **critic**: it is `65535` on `effects/ref_12` (the ocean card's
`$basetexture`) and on `water/oilfieldwatera` (blackwater's `%tooltexture`). **Resolution: not a
contradiction but a scope correction** — the auditor measured the two *arrays*, the critic measured
the whole cast.

**(15) The hub's `sky` block.** Gap investigation 1 flags that `sm_hub_1`'s manifest carries a sky
block identical to the pier's and proves it is not a leak: `sm_hub_1.ents` contains a real
`sky_camera` at the same source origin and scale, differing only in fog (`fogenable 1`, 500..5000).
Recorded so nobody re-opens it.

---

## 6. Phase 0 — the unknowns, one row each

The eight read-only lanes that closed what §4 and §5 left open. Two rows remained pending at the end
of Phase 0 and are named as such.

| # | Verdict | One-line answer |
|---|---|---|
| U1a materials | **SETTLED** | 12 units inventoried per key; only 5 are bound to a face; G3 pinned to an exact bind failure, G4's "destroyed upstream" framing is stale |
| U1b textures | **SETTLED** | 9 textures measured; `TA_water_dudv` verified byte-exact signed UVWQ; two new defects (pier `cubemapdefault` mip collapse, `T_ref_12_linear` twin) |
| U1c lighting + entities | **SETTLED** | no light or texlight is bound to any water face on either map; the foam cards are the only lightstyle-bearing water geometry; three entity families near water previously unrecorded |
| U1d effect assets | **SETTLED** | the splash/drip roots and their sprite leaves named and resolvable; `waterbigsplash_emitter` is authored-empty in the UP build |
| U2 primitives drawn / perturbed | **SETTLED** | drawn, exclusively, on the shortest route; never perturbed on CPU or GPU |
| U3 fluid controller + splash | **SETTLED** | both maps get a controller (`index > 0`); the controller emits nothing; the splash is the client's water-level transition, roots named |
| U4 `$subdivsize` / `origFace` corpus | **SETTLED** | 9 maps / 512 primitive faces, 100 % `$subdivsize 64`; `origFace` groups shards reliably but its polygon is pre-CSG |
| U5 `TOOLS_SHADOW` @ `0x20` | **SETTLED** | `MASK_WATER` tests the LEAF, not the brush; no leaf carries the bit; the port's exclusion is correct |
| U6 leaf bit `0x800` | **SETTLED** | the precipitation render mask / VtMB's sky-visible annotation; destroyed on the UP pier |
| U9 port state | **SETTLED** | the water master's motion scalars are declared-not-wired; the water tag and the named-sound API pinned |
| U7 Sewer Scheme I/O | pending | game witness |
| U8 `TC_VectorDisplacementmap` | pending | editor probe |

**U1a (materials).** All 12 census units plus `effects/ref_12` carry a per-key table. Bound to a
face on the two maps: `water/sewer_water` (unpatched) and `dev/dev_waterbeneath2@cubemapdefault` on
the hub; `objects/surf` and `water/blackwater@c-1241_22_4950` on the pier. The other seven units
(`sewer_water_depth_20`, `invisible_water`, `invisible_water_depth_33`, the plain `blackwater`
patch, `oilfieldwatera/b`, `cheap_water`) are decoded and staged but bind no face on either map — a
lane that walks staged units must not treat them as in scope. Also measured:
`objects/surf` authors no `$surfaceprop` and binds `PM_default`; `water/sewer_water`,
`water/invisible_water`, `water/blackwater` and `dev/dev_waterbeneath2` bind `PM_water` (37
instances corpus-wide); patched per-map instances carry `physMaterial: null` **by design** and
inherit through the parent instance — not a gap.

**U1b (textures).** Nine textures measured (dimensions, VTF format, flag word, mip count, frame
count, `startFrame`) and eleven baked assets identified with their compression / sRGB settings.
Headline: `TA_water_dudv` preserves signed UVWQ byte-exactly (R=U, G=V, B=W, A=Q, no reorder, no
bias; the only change in the chain is a deliberate DXGI 30→28 relabel for Unreal's DDS reader).
No VTF flag bit encodes sRGB at all, so G15's `NORMAL` bit is a role-conflict tie-breaker only.

**U1c (lighting + entities).** Zero `worldLight`s of any type are bound to a water face on either
map (`owner`/`texinfo`/`flags` are 0 on all 21 pier / 96 hub near-water rows); no texlight is within
512 in of either water body (closest 586.6 in pier, 6653.7 in hub); the lightstyle census reproduces
G6 face by face; `env_cubemap` / `func_water_analog` / `water_lod_control` / `env_fog_controller`
do not exist as compiled entities on either map. New: the pier's `logic_auto` idx 900 fires
`OnMapLoad → world.FadeGlobalWetness(1)`; the hub's `rain_on_timer` (idx 1648, enabled) /
`rain_off_timer` (idx 1643, disabled) cycle it; 36 `env_particle WaterDrops_Timer`, 8 `npc_maker`
`rat_swimming.mdl` and 5 `ambient_generic "Waterdrips"` sit 2-150 in above the sewer plane; the six
`Sewer Scheme` triggers are exactly the six that straddle the plane, and four more with identical
I/O sit 15-27 in above it.

**U2 — are the primitive strips drawn, and are their vertices perturbed?** **Drawn, yes, and by the
shortest possible route.** `Shader_DrawSurfaceDynamic` (`engine.dll FUN_2007d4e0`) reads `numPrims`
at `msurface+0x50` as its first act and, when non-zero, takes the primitive path exclusively
(type 0 → `MATERIAL_TRIANGLES`, type 1 → `MATERIAL_TRIANGLE_STRIP`), pre-empting both the
water-subdivision branch and the surfedge fan. It is reached every frame:
`CViewRender::DrawWorld` ORs `0x8` (`DRAWWORLDLISTS_DRAW_WATERSURFACE`) under `r_drawwatersurface`.
**Perturbed, no — nowhere.** `BuildMSurfacePrimVerts` (`FUN_20074f40`) copies each primvert position
verbatim and writes the face PLANE normal; every shipped water vertex shader writes
`oPos = dp4(v0, cModelViewProj)` on the untouched input position; DX9 binds `Water_vs20_old`, which
does not displace; no water VS contains a `sincos` or a time constant. The one alternative path is a
genuine adaptive subdivision (`maxLen = min(dA,dB)·0.22 + 2.0`) that only inserts midpoints and
centroids of coplanar points, and the only per-vertex animation anywhere is `mat_waterswirl`
(default `0.02`), which perturbs the NORMAL: `normal.xy = (sin,cos)(2·t + 0.117·x + 0.339·y)·swirl`.
Both are pre-DX9 fallbacks.

**U3 — the fluid controller and the water-entry splash.** **Both maps get a controller.** The guard
in `vampire.dll FUN_10158600` is `if (fluid.index > 0)` on the first dword of `ParseFluid`'s output
— not contents, not solid flags. Both maps author `index "5"`. The controller wraps
`physics.models[0].solids[5]` and binds the hard-coded literal surfaceprop `"water"`. **The
controller emits nothing** (G7). **The live splash is client-side and keyed on the water level:**
`client.dll FUN_10099630`, called from slot 9 (`DrawModel`) of the `IClientRenderable` sub-table of
both `C_BaseVCombatCharacter` and `C_BaseHLPlayer`, spawns `waterbigsplash_emitter` on a
0 → non-zero water-level transition with `velocity.z < −200 in/s`, and `watersplash_emitter` while
`0 < waterLevel < 3` with horizontal speed ≥ 50 in/s on a `5.0 − horiz·7.8e-5` s cooldown, at
`GetRenderOrigin() − vel.xy·0.035` snapped to the water surface (`+RandomInt(0,8)` for the wade
one). Roots to generate: `NS_waterbigsplash_emitter`, `NS_watersplash_emitter`; leaves
`waterbigsplash` (`watersplashes.tga`), `watersplash` (`cloud.tga`), `splash` (`point_16.tga`).
**The sound hook is not in the splash builder:** `water.Impact` / `water.Scrape` off surfaceprop
`water`; `player/pl_wade2.wav` on exit (`vampire.dll 1003f4d0`); `Surfaces/Water/Step*` at
waterlevel 1 and `Surfaces/Wade/Step*` at ≥ 2 on a four-phase counter whose phase 0 is silent — **three wading
steps in four sound** (`1011e940`, `DAT_1070b898 == 0 → return`; verdict D3's "every 4th step" is
the inverse and is superseded);
`Water.BulletImpact` / `Underwater.BulletImpact` for gunfire.

**U4 — the corpus checks.** 9 of 108 UP maps carry primitive-bearing water faces (512 faces), and
100 % of them bind a material authoring `$subdivsize 64`. The reverse fails: `hw_warrens_2b` and
`hw_warrens_4` bind `$subdivsize` water on every water face and have non-empty PRIMITIVES lumps with
**zero** faces referencing them. `origFace` is trustworthy as a shard grouper and not as a drawable
polygon (G12). The three converted maps carry zero primitives; `sp_soc_3` gets its first water
census — 62 faces, 31 `dev/dev_water2_cheap` top + 31 `dev/dev_waterbeneath2` underside, exactly
1:1.

**U9 — the port's state at the start of the pass.** `M_V2_Water` wired none of the wave / motion
scalars and had no `GlobalWetness` and no sine-UV lane. Two facts closed for the implementers:
the water tag `elysium.water` is byte-identical across the bake and the verify, and `verify_water`'s
errors reach `main()`'s non-zero exit — it **hard-fails** the verify run; and the named-sound API
is `AElysiumMapActor::PlayVoice(const FString& Rel, const FElysiumPlayParams&)`, with the
surfaceprop layer already carrying the resolved pools (`SoundScriptImpact` / `SoundScriptScrape` /
`FootstepsLeft` / `FootstepsRight`, `PM_water.uasset` / `PM_wade.uasset` baked) and every
`Surfaces/Water/*` and `Surfaces/Wade/*` wav exported.

---

## 7. Phase 0 — the rules the unknowns changed

The amendments the verdict made to the implementation plan, as **where** → **what changes**. They
are recorded here because each one is a measurement, and because several of them overturned a rule
the first pass had written down.

**Decoders.** Replace the `<iI` unpack at `dface+96` with three fields and delete the
`smoothingGroups` name (G13). Publishing `primitives[]` must state that **lump 38 carries positions
only** — consumers re-derive UV0 from the texinfo vectors and take the face plane normal; do not
publish or consume primvert UVs. The VTF `NORMAL` bit is a role-conflict tie-breaker only, never an
sRGB oracle (G15), and the twin-deletion scope grows by one (`T_ref_12_linear`). The
`low-res-cpu-sample` mip collapse hits **both** pier probes, not just the ocean probe (G5). No
texture-side work is needed for G3: `TA_water_dudv` is verified byte-exact signed UVWQ.

**Map-geometry stage.** The face-binding rule must NOT trust the unit's own resolved family — the
drawn `%compilewater` face binds the water instance the material lane creates, and measured off the
staged tree, `water/invisible_water` and its `_depth_33` patch resolved to `M_V2_Unlit` and
`water/cheap_water` to `M_V2_LitTranslucent` before the reroute. The underside rule is exact:
`plane.normal.z < 0.0`, and VtMB's own response is to undefine `$reflecttexture`, so the twin
**drops reflection** rather than merely flipping a normal — and the port keys it off the FACE,
because VtMB mutates the shared material and is load-order dependent. The primitive mesh path buys
nothing on the three maps in scope; implement it behind synthetic-unit tests and do not gate the
water look on it. The fluid staging guard is `fluid.index > 0`, not contents. The wetness keys are
already read by the runtime — drop them from the water lane's list. Leaf bit `0x800` is free to
publish beside `0x200`, but no rain gate may key on it for `sm_pier_1`.

**Materials stage and masters.** G4's "resolve the patched unit's full parameter set through
`patchBase`" is struck — already implemented. G3's failure is pinned to `_bind_texture` and the
misdirected proxy; the fix is a `DuDvMapFrames` array folded into the SLW normal at
`RefractAmount/100` and `ReflectAmount/100`. The `%compilewater` reroute must move the **patch**
instance as well, or the pier's 9 up-facing top faces stay Unlit, and it must decide the blend
explicitly. `WaterMurkiness` is a **removal**, not a no-op — the master lerps BaseColor toward
`WaterColor` by it today and 4 of 24 instances carry non-default values, so un-wiring it changes
those four (owner call: keep it wired, ruling N). Any graph edit bumps `GRAPH_VERSION` or the
recipe-stamp skip silently no-ops the rebuild. Any new exposed parameter moves in lockstep across
three files, pinned by the exposed-params test.

**C++ runtime.** Rewrite the splash rule: it is **not** the fluid controller. Big splash on
`waterLevel 0 → ≥1` with `velocity.z < −200 in/s`; wade splash while `0 < waterLevel < 3` with
horizontal speed ≥ 50 in/s on a `5.0 − horiz·7.8e-5` s cooldown, spawned at `origin − vel.xy·0.035`
snapped to the water plane. Delete "speed-scaled and count-randomised as `FUN_10151150` does" —
those numbers are dead code in the shipped build. Bind to the water-level transition, not to a draw
call. `FElysiumWaterFluid` still earns its lane for state and buoyancy. The audio seam is concrete
and its assets exist. The pier's foam cards bind `PM_default`, so waterline footsteps must key off
the classified level, not the surface material. If surface motion is ever wanted, VtMB backs exactly
two numbers, both as named modernizations: adaptive tessellation to
`max_edge = 2.0 + 0.22·distance_to_eye` inches, and the `mat_waterswirl` normal-space swirl. Nothing
in VtMB justifies vertex displacement on water.

**Bake and verify.** `verify_water` hard-fails the verify run and reads the staged manifest fresh
off disk — re-stage before verifying a schema change, not just re-bake. The mesh-area check uses
`faces[].area`. `MANIFEST_VERSION` is pinned equal across the two halves; bump them together.

**Effects.** Generate `NS_waterbigsplash_emitter` and `NS_watersplash_emitter`. **New owner
decision:** `waterbigsplash_emitter` is authored-empty in the Unofficial Patch (`// removed by
wesp`; staged role `neither`, 0 blocks, unbalanced braces) and live in retail `pack001.vpk` (143 B)
— read the retail member for this one unit and record a named divergence, otherwise the entry splash
spawns nothing. Measured water scope: `sp_soc_3` = 40 × `drip_emitter`, `sm_hub_1` = 36 ×
`waterdrops_timer` (the single most-placed root on that map). Rain families placed nearby: pier
10 × `rain_box_emitter`, hub 4 × `rain_box_noprecip_emitter` + 2 × `rain_follow_emitter`.

**Plan level.** Owner decision 1 (stay on the UP recompile for `sm_pier_1`) gains one argument and
one cost. Gain: the pier gets a fluid controller in either build, and retail's two extra
`0x18000120` brushes are inert. Cost: staying on UP loses a **second** compiler annotation — leaf
bit `0x800` — on top of `0x200`. Any rain gate must derive it or read the retail twin for that map.

---

## 8. Phase 0 — contradictions resolved

1. **"The compiler's resolved water parameters were destroyed upstream" vs the measurement.** The
   base unit's provenance carries the full expanded set and the patched instance inherits it through
   `MaterialInstanceConstant` parenting. Only the hub's own 3-key `$envmap` override was reduced to
   a 117 B stub. **The gap is graph wiring, not value resolution** (G4).
2. **`WaterMurkiness` "becomes declared-not-wired" vs "it is now wired".** Both are true at
   different layers: the 2004 shader never registered `$watermurkiness`, and the port's own master
   wires it today. Resolved as an owner call, not an evidence contest — kept wired, ruling N.
3. **"The unit publishes it as `DuDvMap`, so the animation reaches no parameter" vs "it never
   reaches the instance at all".** The stricter reading is correct: the bind is attempted and fails
   on a texture-class mismatch, so the instances carry no `DuDvMap` key at all (G3).
4. **"The pier's runtime wetness target is driven to 1 and is lost" vs the runtime.** The discovery
   stands, the conclusion does not: the port implements the whole chain, worldspawn keys included,
   with a regression test replicating the hub's on/off timers. **"There is no lane for VtMB's global
   wetness" is stale**; what genuinely remains is that no water material reads wetness (G20).
5. **"9 vs 10 vs 11 primitive maps."** 9 UP maps have primitive-bearing **faces**; two more have
   non-empty PRIMITIVES lumps with zero face references. The "10/11" counts were counting lump
   presence, not reachability (§5 (9)).
6. **"If `[3]` is the parsed contents, the pier gets no controller."** The guard is
   `fluid.index > 0` at the instruction level; both maps get one (G7).
7. **Leaf `0x800`'s meaning.** One reader in the corpus: `engine.dll FUN_200d3f10`, from
   `CParticleManager::vfunc15`, only for systems whose definition carries the `precipitation` key.
   A particle in a leaf WITHOUT `0x800` gets particle flag `0x20`, whose only reader is the render
   list's "do not draw" test. So `0x800` is the **precipitation render mask**, and measurably it is
   VtMB's sky-visible / outdoor annotation: 96/209 map files carry it, never co-occurring with
   `CONTENTS_SOLID`, 94/96 have `TOOLSSKYBOX` in texdata, pure interiors carry zero, and a single
   +Z ray per empty leaf reproduces it on 2303/2493 hub leaves (92.4 %, 4 false positives). It
   remains correctly ruled out for water, and it is destroyed by the UP recompile on `sm_pier_1`
   (retail 702, UP 0) while `sm_hub_1` is identical in both builds (975).
8. **"`$translucent 1` DROPPED" on `water/invisible_water`.** Measured CONSUMED into
   `blendMode: "Translucent"` with no omission entry. (The `%compilewater` reroute later forces
   Opaque on the SLW master and records `waterBlendConsumedByTheWaterLane`.)
9. **`$bottommaterial` RULED_OUT vs DROPPED.** Split cleanly: the decoder defect is live, and the
   Underside consequence already worked through a provenance-row workaround. Both halves are true;
   this document says which is which (G16).
10. **`msurf+0x24/0x26` vs `+0x50/+0x52`.** Same fields: one quotes Ghidra's `base+0x2c` struct
    pointer, the other absolute offsets in the 0x98-byte record. Recorded so nobody re-opens it.
11. **The mip-collapse scope.** The same collapse is measured on the pier's `cubemapdefault`; the
    hub's two probes are unaffected. Extension, not contradiction (G5).
12. **"`sm_pier_1` binds only `objects/surf` + `blackwater`" vs "41 + 9 water faces".** Consistent:
    the nodraw water faces were dropped by the exporter, so they bound no material in the staged
    manifest (G1).

---

## 9. What remains open, and what would settle it

**Needs the game.** Whether the hub's `Sewer Scheme` outputs fire on entering the water (U7 —
`elysium_io_history`, `elysium_audio_state` after teleporting into the sewer; read the pier's
`logic_auto FadeGlobalWetness(1)` in the same pass). Whether the port's splash fires for the player
on the water-level transition, and that no fluid-controller splash is attempted for a dropped prop.

**Needs the editor.** `TC_VectorDisplacementmap` vs `TC_Normalmap` on the water normal array,
compared on one `M_V2_Water` instance (U8). Two things a naive probe run misses, recorded so nobody
rediscovers them: a fresh commandlet's asset registry has not walked the baked mount (scan
synchronously first), and texture platform data is null without `-AllowCommandletRendering` —
`BuiltExtent` returns all zeros and reads as "the asset is broken" when the asset is fine.

**Needs a corpus the project does not hold.** Whether vphysics calls `FluidStartTouch` for the
player's own shadow (i.e. whether VtMB's buoyancy damping applies to the player) — needs
`vphysics.dll` in the Ghidra corpus, or a live probe comparing a swimming player's velocity decay
against a dropped prop's. Whether `IPhysicsObject` vtable `+0x30` is `GetMass` (changes no port
decision — the expression is discarded by the shipped build). Whether `IMaterialVar` vtable `+0x64`
is `SetUndefined()`.

**Cheap sweeps nobody has run.** Leaf bit `0x1000` (same shape as `0x800`, no reader searched).
Whether `hw_warrens_2b` / `hw_warrens_4`'s orphaned PRIMITIVES lump is a general UP-recompile
pattern. Whether `0x800` is exactly vbsp's sky-visibility set (92.4 % single-ray agreement; a
9-sample ray run or a portal flood fill from sky brushes closes it). Why 40 retail maps carry a
skybox material and no `0x800` leaf at all. Why vbsp authored
`maps/sm_hub_1/water/sewer_water_depth_20` and the coordinate-less pier `blackwater` patch when no
face binds them. What the worldspawn `sounds "1"` key means (present and identical on both maps,
zero readers in the pipeline). Which numbered DSP-preset unit is VtMB's water DSP, and whether any
soundscape references it — the presets and all `Surfaces/Water|Wade` wavs are confirmed exported,
the water-preset identity is not. Whether VtMB's compiled-strip water really renders with all-zero
UVs (the port's takeaway is fixed either way).

---

## 10. Related docs

- `docs/vtmb/water.md` — the engine-neutral inventory, the shaders and the volume rules.
- `docs/architecture/water-architecture.md` — the Unreal rulings this census is the evidence for.
- `docs/vtmb/bsp_format.md` — `dface`, the primitive lumps, the leaf contents bits.
- `docs/vtmb/reflections.md` — `$envmap` and `$envmapcontrast` on the water-look materials.
- `docs/vtmb/surface_properties.md` — the `water` / `wade` physical and audio rows.
- `docs/architecture/seam_map_map.md`, `seam_map_material.md`, `seam_map_texture.md`,
  `seam_map_unit_contract.md` — the unit contracts the dispositions of §2 write into.
- `docs/project/seam_migration.md` → "R7.1 — settled" — the gap table as a migration record.
