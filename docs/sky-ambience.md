# Sky & ambience — the verified RE reference

How VtMB produces its sky and its ambient light, settled end to end. The eight unknowns
this doc once tracked (K1–K8) are **all closed**, and the Unreal rework built on them —
Phases B and C, tasks B1–B8b and C0–C5 — **landed 2026-07-26**. This doc keeps the
engine-neutral facts and the instruments that measured them; status, history and decisions
live in the tracker set:

- **Status and next work:** `roadmap.md` — the SKY row (done), and the open residue promoted
  to **3.6/3.7** (the measured tonemapper toe — the one remaining visible gap to displayed
  parity), **3.10** (volumetric fog layer calibration), **3.11** (`elysium.LumenDiffuseBoost`
  verification), **3.12** (`sm_hub_1` fill adjudication), **3.13** (decal fog liveness), and
  **RE17** (owner-run reference captures, gated on the `snapshot` pre/post-gamma-ramp check).
- **The decisions** (D1–D7; D6 dissolved, D4 amended, D3 corrected) are stated as facts in the
  sections below.

Related: `docs/lighting.md` (WORLDLIGHTS facts), `docs/light-attribution.md`
(fixture-vs-fill), `docs/rendering-perf.md` (why Lumen is load-bearing), `docs/color_gamma.md`
(the LDR look), `docs/asset-enhancement.md` (upscaled sky faces as an A/B layer).

## Why this exists

VtMB's ambience was never explored as its own subject — the sky path was built bottom-up
(decode faces → cube → SkyLight + backdrop) on **assumed** conventions, and the ambience model
was inherited from an earlier calibration pass rather than derived from how the original
engine works. Two things force the revisit:

1. **The sky cube rendered wrong.** The decoded faces are correct as images (upright, level
   horizon — verified by eye on `sp_tutorial_1`'s `la` set), so the defect was in the
   assembly/sampling chain, which stacked *two* unverified conventions (K1 × K2). Both are
   settled below, and together they named the defect exactly: `BuildSkyCube` bound the Source
   faces to the wrong world axes (K1) **and** stored every slice without the rotation Unreal's
   D3D-derived cube layout requires (K2). B3 fixed both under one six-row transform; B1
   confirmed it.
2. **We could not say how the original engine produces its ambient light.** We knew what the
   *compiler* consumed (lump 15) and what it *emitted* (lump 8); the runtime half — what the
   engine itself adds per frame, and to what — was inference from later Source versions, not
   RE. RE-A3 closed that half (K3/K5, below), and RE-A4 disposed of the day/night bake the
   plan had been holding open (K4): there is only one bake. RE-A5 then closed the bake-time
   half (K6) with an answer that reframes the whole of Phase C — VRAD's photometric transfer
   is exactly recoverable from the shipped data, and it converts any worldlight into the luxel
   value it actually contributes, giving Phase C's model the absolute scale it needs.

## What we know (verified)

| Fact | Source |
|---|---|
| **World** ambience has no runtime GI and no global ambient constant: it = baked radiosity (lump 8) + author-sprayed fill lights + optional sky pair + fog. (Models are the exception — see the K3/K5 rows below) | `docs/lighting.md`, `docs/light-attribution.md`, RE-A3 |
| The look is **bounce-dominated**: a direct-light model (even correctly shadowed) fits the baked lightmaps with R² ≈ 0 | `tools/probe_light_calibration.py`, `docs/rendering-perf.md` |
| WORLDLIGHTS (lump 15) carries the compiled light-source set; type 3 `emit_skylight` (sun) and type 5 `emit_skyambient` come from `light_environment` | `docs/lighting.md`, `tools/bsp.py` |
| v17 `dface_t` reserves two extra lightstyle arrays (`day[8]` @56, `night[8]` @64) beside `styles[8]` @48, plus `avgLightColor[8]` at offset 0; the modern ambient-cube lumps are empty in VtMB data | `tools/CLAUDE.md` (byte-probed) |
| **K4 is settled** — the `day`/`night` arrays are **all-zero in all 108 maps** and **no engine code reads them**; lump 8 holds exactly **one** bake, keyed by `styles[8]` alone. There is no day/night set to choose between | RE-A4 (below), `tools/probe_daynight.py` |
| ~20% of the 2,001 measured lights are GI-substitute fill; curation is by hand survey (decided 2026-07-26) | `docs/light-attribution.md` |
| **All 108 maps** carry a `skyname` and every named set resolves with 6 faces, but they draw only **11** distinct sky sets (`santamonica` ×36, `la` ×22, `holly` ×22, `pier` ×12, `hav` ×5, `warehouse` ×4, `chinatown` ×3, and one each of `warehouse2`, `warehouse3`, `holylight`, `moon`), 512² except `holly`/`chinatown` at 256². Only **66** maps have `toolsskybox` brushwork, so 42 name a sky nothing draws | RE-A7, `tools/probe_sky_inventory.py` |
| **K6 is settled** — VRAD's keyvalue→`dworldlight_t.intensity` transfer is exactly `(colour/255)^2.2 · (brightness/255) · (const + 100·linear + 10000·quadratic)`, verified with **zero exceptions on 16,378 lights across all 108 maps**. The third factor is the light's own falloff denominator at d = 100 units, so a compiled intensity is that light's radiance at 2.54 m | RE-A5, `tools/probe_skyambient.py` |
| **Lump 8's absolute scale**: `stored luxel = 255 · intensity / (const + linear·d + quadratic·d²)`, so a light of brightness `B` lands `(colour/255)^2.2 · B` at 100 units — the authored brightness comes back out of the bake | RE-A5 |
| The **sun is baked**, at `255 · intensity · cos`, gated by a sky-visibility test along the sun direction. On the three maps whose sun outshines their own fill it lands at ×1.09 / ×0.87 / ×1.01 of that ceiling (median **1.01**) carrying the sun's own chromaticity; on the other 14 the bright end carries the **fill's** colour instead, which is the same diagnostic confirming itself | RE-A5 |
| Where sky is visible the pair is a **first-class term, not a tint**: the sun's ceiling is a median **332%** of its map's median lit face (max 3811%), the skyambient's **121%** (max 640%). What limits it is reach — VtMB's maps are mostly enclosed — not magnitude | RE-A5 |
| VRAD resolves the sky **ambient once, globally, first-entity-wins** and stamps it on every type-5 row, while each `light_environment` emits its **own** type-3. Proved on `sp_observatory_2`, whose two entities author `_ambient` 20 and 10 and whose two type-5 rows both read 20/255 | RE-A5 |
| The Unofficial Patch **recompiles 20 maps and adds 7**, with a later Source VRAD — so 27 of 108 bakes are not Troika's, and any lump-8 measurement must check provenance first | RE-A5, `probe_skyambient.py --provenance` |
| **K1 is settled** — the face→axis binding, per-face basis and texcoord flip are read out of `engine.dll`'s own tables and confirmed on the decoded faces (see below) | RE-A1, `tools/probe_sky_orientation.py` |
| **K2 is settled** — an Unreal cube slice is the plain **D3D** face table applied to the **raw** world vector, with no swizzle anywhere in the chain. Unreal being Z-up where that table assumes Y-up, four of the six slices are stored rotated against an upright view along their own axis (+X 90° CCW, −X 90° CW, +Y 180°, −Y none, ±Z with world +Y at the top) | UE 5.8 source ×4 + Epic's authoring doc (see below) |
| **K3/K5 are settled** — world surfaces render from lump 8 alone; lump 15 is read at runtime *only* by the light cache, which lights dynamic models and static props. A model's ambient term is a 6-face ambient cube built by a 162-ray radiosity sweep that samples `dface_t.avgLightColor` and multiplies by the hit material's reflectivity | RE-A3 (below) |
| VtMB **does** have a runtime one-bounce GI — for models only. `emit_skyambient`'s intensity is the colour a sky-hitting bounce ray returns; it contributes nothing through the direct-light path | RE-A3 |
| **25 of the 108 maps** carry the sun+skyambient pair, never one without the other; the other 83 have no `light_environment` at all. 5 maps carry several (`sm_warehouse_1` 4, `ch_temple_1` and `sp_taxiride` 3, `sp_observatory_2` and `sp_soc_2` 2), and `sm_hub_1` — the outdoor hub street — carries **none** | RE-A7, `tools/probe_sky_inventory.py` |
| **K7 is settled** — every sky VMT in the game is `UnlitGeneric` with `$basetexture` + `$nofog` and nothing else, and the whole draw is `mul r0, t0, v0` against a modulation the engine forces to white. **A sky pixel is the decoded texel**: no scaling, no overbright, no gamma op, no fog | RE-A9, `stdshader_dx8.dll` + the shipped `unlitgeneric.psh`/`.vcs` |
| The world's pixel is `albedo × lightmap × 2` — `lightmappedgeneric.psh`'s `mul_x2 … (overbrightFactor/2)` with the factor **pinned** to 2 (only 1.0 and 2.0 are accepted; anything else, and any hardware without overbright support, is rewritten to 2.0). That ×2 is the *only* sky-vs-world asymmetry in the framebuffer | RE-A9, `0x200718d0` |
| Gamma is frame-wide, never per-material: `gamma` 2.2, `texgamma` 2.2, `brightness` 0, `linearFrameBuffer` 0, and a display value of `1.6 − clamp(cl_v_gamma − 1, 0, 3)·0.5` (= 1.35 at the default `cl_v_gamma` 1.5), applied as a device gamma ramp at present | RE-A9, `docs/color_gamma.md` |
| VtMB ships its DX8 shader assembly as **data**, not compiled into a binary: `materials/dxshaders/*.psh` is readable source and `shaders/{vsh,psh}/*.vcs` the compiled form. Any question about what a Source-2003 shader does to colour is a file read, not a decompile | RE-A9, install index |
| Sky faces live at `materials/skybox/<skyname>{up,dn,lf,rt,ft,bk}` in the standard TTH/TTZ pair | `tools/UE_bsp_to_scene.py` |
| The retail sky sets are DXT5; `pier` is the one set the Unofficial Patch replaces loose, as an uncompressed BGR888 re-export | install index, measured 2026-07-26 |
| The 3D skybox is a **second full render of one BSP area** — world brushes, static props, brush entities, sprites, particles — not just backdrop geometry; transform `world(v) = scale·(v − origin)`, membership = `dleaf_t.area == sky_camera's area` | RE-A8 (below), `client.dll` + `vampire.dll` |
| `sky_camera` carries the fog of the **skybox pass only**; the world's own fog is on `worldspawn`. 43 maps have a `sky_camera` (`scale` 16 on every one) and on 27 of them the two fog sets disagree in colour/range/enable (30 counting `fogcolor2`/`fogdir`); 12 maps enable `worldspawn` fog but have no `sky_camera`, and 6 have a fogging `sky_camera` over a `worldspawn` that does not | RE-A8 + RE-A7 |
| `TOOLS/toolsskybox` faces mark where the engine draws sky; the exporter skips them | `tools/CLAUDE.md` |

The `sm_hub_1` datum matters beyond inventory: an outdoor night street whose sky contributes
**zero** light — its "sky glow" is entirely sprayed fill. That is the same map where the
fill-classifier fails and where the load-bearing-fill hypothesis lives
(`docs/light-attribution.md` → "Where to pick up"). The sky/ambience model and the fill
question are one subject.

## K1 — the Source sky-face orientation convention (settled)

VtMB draws its 2D sky as **six flat quads at a fixed radius**, translated to the view origin —
there is no cubemap and no runtime sampling convention to guess at. The whole convention is
three `.rdata` tables and one vertex builder, inherited from Quake unchanged.

### Where it lives (`engine.dll`, imagebase `0x20000000`)

| Address | What |
|---|---|
| `0x20087ce0` | the loader: `Q_snprintf("skybox/%s%s", skyname, suf[i])` over `suf[6] = {"rt","bk","lf","ft","up","dn"}` (pool `0x201a1fd4`–`0x201a1fe8`, pooled in reverse), storing six `IMaterial*` into `skyboxtextures` = `0x20b424e4` |
| `0x20087c20` | the matching unload, same suffix order |
| `0x200880c0` | `R_DrawSkyBox(dist, facemask, …)` — per axis 0–5: if the mask bit is set and the face passes a view-direction dot test, bind `skyboxtextures[skytexorder[axis]]` and emit one quad, `MakeSkyVec(∓1, ∓1, axis)` ×4 |
| `0x20087f10` | `MakeSkyVec(s, t, axis, dist, outVec, outUV)` |
| `0x20088480` | `CVRenderView::DrawNamedSkyBox` — swaps a named set in, calls the **same** `R_DrawSkyBox` with mask `0x3f`, restores. The only other draw path, and it shares the binding |

### The tables

| Address | Table | Value |
|---|---|---|
| `0x201a1ef8` | `st_to_vec[6][3]` | `{3,-1,2} {-3,1,2} {1,3,2} {-1,-3,2} {-2,-1,3} {2,-1,-3}` |
| `0x201a1f40` | `vec_to_st[6][3]` | `{-2,3,1} {2,3,-1} {1,3,2} {-1,3,-2} {-2,-1,3} {-2,1,-3}` |
| `0x201a1f88` | `skytexorder[6]` | `{0,2,1,3,4,5}` |
| `0x201a1fa0` | per-axis face normal, as an axis code (`±1/±2/±3` = `±X/±Y/±Z`) | `{1,-1,2,-2,3,-3}` |

`MakeSkyVec` builds `b = (s·d, t·d, d)` with `d = dist · 0.5773500` (1/√3), then
`v[j] = sign(k)·b[|k|−1]` for `k = st_to_vec[axis][j]`, and adds the view origin. Texcoords are
`u = (s+1)/2` and `v = 1 − (t+1)/2`, each clamped to `[1/512, 511/512]` — a half-texel seam
guard sized for the authored 512² face. That `1 − t` is present in the shipped **D3D** engine,
where `v = 0` is the first texture row, so **image row 0 is the top of the face**.

The normal table and `st_to_vec` are independent statements of the same thing and they agree,
which is the internal cross-check on the binding below.

### The convention

`skytexorder` maps a draw axis onto the load order `[rt, bk, lf, ft, up, dn]`:

| Axis | World face (Source) | Texture | Image right (u+) | Image up (v−) |
|---|---|---|---|---|
| 0 | +X | `rt` | −Y | +Z |
| 1 | −X | `lf` | +Y | +Z |
| 2 | +Y | `bk` | +X | +Z |
| 3 | −Y | `ft` | −X | +Z |
| 4 | +Z | `up` | −Y | −X |
| 5 | −Z | `dn` | −Y | +X |

Equivalently, for a pixel `(u, v)` of a face (`v = 0` the top row), with `s = 2u − 1` and
`t = 1 − 2v`, the **Source-space** direction it looks at is:

```
rt: ( 1, -s,  t)    lf: (-1,  s,  t)    bk: ( s,  1,  t)
ft: (-s, -1,  t)    up: (-t, -s,  1)    dn: ( t, -s, -1)
```

Three consequences:

- **No face needs a rotation or a flip.** Viewed from inside, each of the four horizon faces is
  stored upright and unmirrored, and `up`/`dn` are unrotated relative to `rt`. The decoded PNGs
  *are* the canonical orientation — B2 has nothing to transform, only to record.
- **`ft`/`bk`/`lf`/`rt` are ring names, not compass names.** `ft` is −Y and `bk` is +Y (`rt` +X,
  `lf` −X). The load order `rt, bk, lf, ft` is angular order around the horizon; `skytexorder`
  exists precisely to swap slots 1 and 2 onto the axis order `+X, −X, +Y, −Y`.
- The unfolded cross is

  ```
        up
   bk   rt   ft   lf
        dn
  ```

  — the horizon ring reads left-to-right and cyclically as `bk → rt → ft → lf`, `up` joins
  `rt`'s top edge and `dn` its bottom edge, none of the six transformed.

Two details the draw carries that are **not** K1. Each face is gated by its normal dotted
against a vector at `0x20a42cb8` — read as the view direction from context, not verified —
with threshold `−0.29289322`, a wide cone rather than Quake's `skymins`/`skymaxs` clip. And the
quad's vertices carry position + texcoord only, no colour: any sky brightness scaling is
material-side, which is K7.

### The data check

`tools/probe_sky_orientation.py` verifies the *relative* half of the convention on the real
decoded faces, independently of the decompile: it scores the horizon ring over every cyclic
order × per-face mirror, and `up`/`dn` over all eight dihedral transforms, by seam error against
the predicted cross. Measured 2026-07-26 over the 10 exported maps (5 distinct sky sets):

- **Ring:** `bk-rt-ft-lf`, no mirrors, wins on 4 of the 5 sets by 4.3×–4.8× over the runner-up.
- **`up`:** unrotated wins on the same 4 sets, by 3.0×–7.5×.
- **`dn`:** every VtMB ground face is a uniform near-black plate, so all eight transforms tie at
  0.00 error. This probe yields no image evidence for it; `dn` is settled instead by the
  in-game check below.
- **`chinatown`** is too low-contrast to discriminate anything (best-vs-runner-up ≤ 1.05×). It
  contradicts nothing, and it confirms nothing.

The probe cannot anchor to world axes — seam continuity is invariant under rotating the whole
cube — so it is a check on the decompile, not a substitute for it. Together they close K1.

### The in-game check (RE-A2)

The third leg: make the faces self-describing and let the shipped engine draw them. This
anchors to world axes (which the seam probe cannot) and is the only evidence that can speak
for `dn`. `tools/sky_probe.py` authors six labelled faces and installs them as a loose set;
`tools/tex_from_png.py` encodes them into the engine's own container (VtMB has no loose
`.vtf` path). Each face carries its suffix in large type, the Source axis K1 predicts for it,
a `TOP` banner and a to-image-up arrow, four tagged corner markers (`TL`/`TR`/`BL`/`BR`, each
a distinct shape), and the neighbour each edge meets in the unfolded cross — so a wrong axis,
a rotation, a mirror and a broken seam are four visibly different failures.

The prediction, restated as something to look at:

| Look along | Source view angles | Face that should fill the view |
|---|---|---|
| +X | yaw 0 | `RT` |
| +Y | yaw 90 | `BK` |
| −X | yaw 180 | `LF` |
| −Y | yaw 270 | `FT` |
| +Z | pitch −90 | `UP` |
| −Z | pitch +90 | `DN` |

upright and unmirrored in every case (`TOP` along the top, `TL` top-left, the arrow pointing
to screen-up, and each edge tag naming the face actually adjoining it).

**Capture protocol.** Install (`--skyname pier`, or `la`), launch VtMB, and load a map that
draws that sky. Source's yaw runs 0 = +X increasing toward +Y, and its pitch is inverted
(negative looks up). To read the current heading, the console falls through to `__main__`
(`python_bridge.md`), so `print FindPlayer().GetAngles()` reports `(pitch, yaw, roll)` — of
the **body**, so its yaw tracks the view but its pitch stays 0 however far the camera tilts.
Yaw is the one that matters here; the face's own label states the pitch it expects.
`snapshot` writes `screenshots/<name>%04d.tga`. Six captures — the four headings plus
straight up and straight down — settle it.

**`dn` needs the right map.** The engine draws a sky face only where a `toolsskybox` surface
is visible, so looking down inside a street shows pavement, not `dn`; the face is observable
only where sky sits *below* the eye. Counting flat skybox faces below `info_player_start`:
`sm_hub_1` and `sp_tutorial_1` have **none** — `dn` cannot be seen on either at any camera
angle — against 243 on `sp_observatory_1` (nearest at z −1099, under a hilltop), 30 on
`sp_soc_1` (z −3072) and 10 small ones on `sm_pawnshop_1`. So the ring and `up` come from an
open street and `dn` comes from `sp_observatory_1`, which draws the same `pier` set.

Sky sets and the maps that draw them: `pier` → `sm_hub_1` (open street), `sp_observatory_1`,
`sm_pawnshop_1`, `sp_soc_1`; `la` → `sp_tutorial_1`. `pier` is uncompressed, so its labels
are bit-exact; `la` is DXT5 and the label edges carry block-compression ringing.

`noclip` is cheat-flagged (`sv_cheats 1` first; the movement cvars are `sv_noclipspeed` and
`sv_noclipaccelerate`) and is not needed for any of the six captures.

Removal is `--uninstall`, **per skyname** (`--status` and `--uninstall` both take `--skyname`
and default to `pier`, so a set installed under another name is not reported by a bare
`--status`): a manifest records every file written with its sha256, and the `pier` originals —
the one sky set the Unofficial Patch ships loose — are moved aside on install and put back on
uninstall.

**Uninstall before re-exporting.** The probe set is a loose `materials/skybox/` install, and
the exporter resolves sky faces patch-first through the same index — so a probe left installed
silently exports its own labelled faces as that map's `tex/sky_*.png`, and every downstream
consumer believes them. The failure is invisible in the numbers (they are valid images of a
valid sky set, just not the game's), so the check is to look at a decoded face, not at a count.

**Result (captured 2026-07-26, `pier`): the engine draws all six faces exactly as K1
predicts** — every face on its predicted axis, upright, unmirrored, unrotated.

| Face | Axis | Confirmed by |
|---|---|---|
| `rt` | +X | yaw 45 (screen-right), yaw −45 (screen-left) |
| `bk` | +Y | yaw 45 (screen-left) |
| `ft` | −Y | yaw −45 (screen-right), yaw −135 (screen-left), yaw −86 head-on |
| `lf` | −X | yaw −135 (screen-right) |
| `up` | +Z | corners `BL`, `BR`, `TR` |
| `dn` | −Z | corners `TR`+`BR` (over `ft`), `TL` (over `bk`/`rt`) |

The corner markers carry the weight: at each seam the three meeting faces present exactly the
corner the unfolded cross requires — `up.BL` = `bk.TR` = `rt.TL`, `up.BR` = `rt.TR` = `ft.TL`,
`up.TR` = `ft.TR` = `lf.TL`, `dn.TR` = `ft.BL` = `rt.BR`, and `dn.TL` = `bk.BR` = `rt.BL`. Five
independent three-face agreements, two of them on `dn`. **This is the only image evidence for
`dn` that exists**, the seam probe being blind to it, so K1's weakest face is now its
best-witnessed. K1 rests on three independent legs: the `engine.dll` tables, the seam probe,
and this draw.

One observation, since resolved: grey mottling appears over the horizon faces near the
skyline but not over the downward faces, alongside visible rain streaks. RE-A8 supplies the
compositor — the 3D-skybox miniature (weather, cloud planes, sprites) draws over the 2D
backdrop through the ordinary material path — and RE-A9 rules out the sky material itself
(no fog, no modulation ever touches the backdrop). What was seen is skybox-pass content,
not a sky-shader effect.

## K2 — Unreal's manual-cubemap conventions (settled)

The whole convention is one table, and UE 5.8 states it four times in its own source — plus
once, from the authoring end, in Epic's documentation. All five agree: **a cube slice is the
plain D3D face table applied to the raw Unreal world vector, with no swizzle anywhere in the
chain.**

That "no swizzle" clause is what matters: the D3D table was specified for a **Y-up** world;
Unreal is **Z-up** and hands the hardware its world vector unmodified. So D3D's *pole* faces
(slices 2/3) land on Unreal's horizontal ±Y, and its *side* faces (slices 4/5) land on Unreal's
up and down. Four of the six slices are therefore stored **rotated** relative to an upright view
along their own axis — which is why a hand-assembled cube of six upright faces, ours, cannot be
fixed by re-ordering alone.

### Where it lives (UE 5.8, under `Engine/`)

| File | What |
|---|---|
| `Shaders/Private/ReflectionEnvironmentShaders.usf:99` | `GetCubemapVector(ScaledUVs, InCubeFace)` — slice + texel → world direction. **The table** |
| …`:193` | `CopyCubemapToCubeFaceColorPS` — the SkyLight *source-cubemap* path: it re-samples the assigned `UTextureCube` with `GetCubemapVector` of the **destination** slice (the identity at rotation 0), so an assigned cube asset is read by exactly this table. Its `CubeCoordinates.z < 0` = *"no sky lighting from below the horizon"* test is independent proof that the vector's **third component is world up** |
| `Runtime/Renderer/Private/ReflectionEnvironmentCapture.cpp:2262` | `CalcCubeFaceViewRotationMatrix(ECubeFace)` — *"Creates a transformation for a cubemap face, following the D3D cubemap layout"*: a `vDir`/`vUp` per slice, `vRight = vUp × vDir`. What a capture renders each face with; agrees with the shader term for term |
| `Developer/TextureCompressor/Private/TextureCompressorModule.cpp:2194` | `TransformSideToWorldSpace` — the longlat→cube import. The same D3D table, then a Y↔Z swap **into** the Y-up space its consumer `LookupLongLatDouble` wants (`acos(D.Y)` is that function's polar angle). The swap belongs to the longlat lookup, not to cube sampling — read the pair together or it reads as a contradiction |
| `Shaders/Private/Common.ush:328` | `TextureCubeSample(Tex, Sampler, UV)` is `Tex.Sample(Sampler, UV)` — **no swizzle**. A material samples the cube with whatever `float3` its graph produces; `M_Sky` produces `−CameraVector`, which is world space |

### The table

`ScaledUVs = UV·2 − 1` over the slice's own texels, `UV` derived from `SvPosition`, so **`sx`
grows to the right and `sy` grows downward**. The Unreal **world** direction of texel `(sx, sy)`:

```
slice 0: ( 1, -sy, -sx)   slice 2: ( sx,  1,  sy)   slice 4: ( sx, -sy,  1)
slice 1: (-1, -sy,  sx)   slice 3: ( sx, -1, -sy)   slice 5: (-sx, -sy, -1)
```

Read as image axes, and compared against the **upright view** along that axis (camera at the
origin looking down it, world +Z up — UE's own `vRight = vUp × vDir`):

| Slice | World axis | Image right (u+) | Image up | vs. the upright view |
|---|---|---|---|---|
| 0 | +X | −Z | +Y | rotated **90° CCW** |
| 1 | −X | +Z | +Y | rotated **90° CW** |
| 2 | +Y | +X | −Z | rotated **180°** |
| 3 | −Y | +X | +Z | **unrotated** |
| 4 | +Z (up) | +X | +Y | world +Y at the top |
| 5 | −Z (down) | −X | +Y | world +Y at the top |

Epic's authoring page states the same six from the other end — *"Positive X rotated 90 degrees
CCW, Negative X 90 CW, Positive Y 180, Negative Y no rotation, ±Z the side that must align with
positive Y should be at the top"* — which is the statement a search engine finds, and the reason
the "why is my cubemap rotated" folklore exists at all.

**One trap.** `GetCubemapTangent` (same file, `:132`) is *not* a sixth statement of the table:
its ±X entries hand back the du/dv axes transposed against `GetCubemapVector` (slice 0 gives
`OutX = −Y, OutY = −Z` where the vector table's are `−Z` and `−Y`). It is used only to offset
neighbour samples when building the mip chain, where the transpose does no visible harm. Read
`GetCubemapVector`.

### The memory layout

`BuildSkyCube` writes one mip whose bulk data holds the six faces contiguously, face-major, in
slice order, each face's rows top-down and otherwise untransformed. The pre-B3 render confirmed
that layout independently of the binding: all six decoded faces appeared, each intact and on *a*
face of the cube — only *which* face, and at what rotation, was wrong.

### What it makes our binding

K1 gives each Source face's own direction; `source_to_unreal` (`x, −y, z`) carries it into
Unreal space; solving each against the slice table above gives the complete transform — the
whole of B3, with nothing left assumed:

| UE slice | UE axis | Source face | Transform on the decoded PNG |
|---|---|---|---|
| 0 | +X | `rt` | rotate **90° CCW** |
| 1 | −X | `lf` | rotate **90° CW** |
| 2 | +Y | `ft` | rotate **180°** |
| 3 | −Y | `bk` | **none** |
| 4 | +Z | `up` | rotate **90° CCW** |
| 5 | −Z | `dn` | rotate **90° CCW** |

Rotations are of the image content; 90° CCW is `numpy.rot90(img)`, i.e.
`dst[y][x] = src[x][N−1−y]`.

`up` and `dn` reading the *same* rotation is not a slip. K1 stores both unrotated relative to
`rt`, and UE's two ±Z slices both put world +Y at the top; the two symmetries survive into one
answer.

Against that, the binding B3 replaced was wrong twice over: the old face order
`ft, bk, rt, lf, up, dn` bound the horizon ring by an **X↔Y swap** — a reflection, not a yaw,
so no camera angle could make it line up — and every slice was missing its rotation.

### What B1 confirmed

Fed the RE-A2 labelled faces through the corrected binding, the runtime draws every face upright
on its predicted axis — the same picture the shipped VtMB engine drew (RE-A2), with no residual
in the memory layout or `M_Sky`'s sampling vector.

## The 3D skybox — what the pass actually draws (RE-A8, settled)

The 2D sky (K1) is the six-quad backdrop. **In front of it VtMB draws a second, complete
render of one BSP area**: the miniature. It is not a special geometry class — it is the *same*
world/renderable draw path the main scene uses, run once more with a scaled view. Everything
the client leaf system holds in that area draws: world brushes, static props, brush entities,
sprites, particles, animating props.

### `sky3dparams_t` — the one struct the whole feature is

Recovered three independent ways (server datamap, player SendTable, client field reads) and all
three agree, so the layout is exact:

| Field | Type | `CSkyCamera` (`vampire.dll`) | player SendTable | client local-player mirror |
|---|---|---|---|---|
| `scale` | **int** | `+0x450` | `+0x148` | `+0x17a4` |
| `origin` | Vector | `+0x454` (inferred: the 12-byte gap, and it is the only missing member) | `+0x14c` | `+0x17a8` |
| `area` | int | `+0x460` | `+0x158` | `+0x17b4` |
| `fog.enable` | bool | `+0x464` | `+0x15c` | — |
| `fog.blend` | bool | `+0x465` | `+0x15d` | — |
| `fog.dirPrimary` | Vector | `+0x468` | `+0x160` | — |
| `fog.colorPrimary` | colour32 | `+0x474` | `+0x16c` (r/g/b as 3 bytes) | — |
| `fog.colorSecondary` | colour32 | `+0x478` | `+0x170` | — |
| `fog.start` | float | `+0x47c` | `+0x174` | — |
| `fog.end` | float | `+0x480` | `+0x178` | — |

The renderer reads it **off the local player**, not off a world singleton — the same field set
is a `m_skybox3d.*` SendTable block on the player and the client reads it from the player object
each frame. The server-side copy site (`CSkyCamera` → player) was not located; it does not matter
offline, since the `.bsp` keyvalues are the input either way. A map with no `sky_camera` leaves
`area` at its `0xff` init value, which is the whole "no 3D skybox" test.

### Where it lives

Server — `vampire.dll`, imagebase `0x10000000`:

| Address | What |
|---|---|
| `0x1018a4e0` | the `sky_camera` factory (`CSkyCamera`, `operator new(0x488)`, vftable `0x10471c54`) |
| `0x10589ad0` | `CSkyCamera`'s `datamap_t` (base = `CBaseEntity` `0x10552e18`); records at `0x10589b14`, count 9, written at static-init by the builder `0x1018a5b0` — a raw image read reports count 0, the half-static case `DumpDatamap` documents |
| `0x1018a730` | the player SendTable builder — the `m_skybox3d.*` props above |

`CSkyCamera`'s nine datamap records **are** the entity's whole Hammer surface — there is nothing
else on it, no inputs, no outputs, no think:

| Key | Field | Type |
|---|---|---|
| `scale` | `m_skyboxData.scale` | **`FIELD_INTEGER`** (VtMB code 4) |
| — (not keyable, flags `0x0002`) | `m_skyboxData.area` | computed at spawn |
| `fogenable` / `fogblend` | `…fog.enable` / `…fog.blend` | bool (code 5) |
| `fogdir` | `…fog.dirPrimary` | vector (code 3) |
| `fogcolor` / `fogcolor2` | `…fog.colorPrimary` / `…colorSecondary` | colour32 (code 8) |
| `fogstart` / `fogend` | `…fog.start` / `…fog.end` | float (code 1) |

There is no `origin` record: the entity's own `origin` *is* `m_skyboxData.origin`. `scale` being
an integer field is why every shipped map reads exactly `16` and why a fractional scale could
never have been authored — a `.vmf` saying `scale 1.5` would truncate to 1.

Client — `client.dll`, imagebase `0x10000000`:

| Address | What |
|---|---|
| `0x1019d600` | `CViewRender::Draw3dSkyboxworld` — the whole pass |
| `0x1019de36` | its **only** call site, inside the view-render frame (`0x1019dd80`–`0x1019df1b`) |
| `0x1019cf60` / `0x1019d590` | `Enable3dSkyboxFog` / `DisableFog` |
| `0x1019d0a0` / `0x1019d2e0` / `0x1019d3d0` / `0x1019d4c0` | `GetSkyboxFogColor` / `…FogStart` / `…FogEnd` / `…FogEnable` — the cvar-override accessors `Enable3dSkyboxFog` reads |
| `0x1019c7f0` / `0x1019c840` / `0x1019c890` / `0x1019c8e0` | the `fog_startskybox` / `fog_endskybox` / `fog_colorskybox` / `fog_enableskybox` console commands (dev overrides of the map's values) |
| `0x10199910` / `0x101997e0` | `BuildWorldRenderLists` → `SetupRenderList` |
| `0x10199b40` / `0x10199cc0` / `0x10199ed0` | `DrawWorld` / `DrawOpaqueRenderables` / `DrawTranslucentRenderables` |
| `0x1019a560` | `DrawObfuscatedEntities` — a VtMB addition in the same family; **not** called by the sky pass |
| `0x10197dc0` | registers `r_3dsky` — *"Enable the rendering of 3d sky boxes"* |
| `0x1019db90` | registers `cl_reportskybox`; prints `"Skybox is visible"` / `"Skybox is NOT visible"` at `0x102d5ffc` / `0x102d5fe4` |

Every function above names itself: they push their own name onto a scope-trace stack
(`s_CViewRender__…` strings), so re-identifying any of them is a string xref, not analysis.

### The pass, step by step

Read from the `0x1019d600` decompile; step numbers are its instruction order.

1. Early-out if the `r_3dsky` ConVar (object `0x10618b4c`, value at `[+0x2c]`) is off, if there is
   no local player (`0x104a0d50`), or if **`m_skybox3d.area == 0xff`**.
2. Set `m_bDrawingSkybox` — `CViewRender+0x49c`; cleared again at the end.
3. `render->GetAreaBits()` (render interface `0x104a57e4`, vtable `+0xa4`) returns the pointer
   slot; save it, build a 32-byte (256-bit, one per BSP area) stack vector zeroed, set
   `tmp[area >> 3] |= 1 << (area & 7)`, and write `tmp` into the slot. **This is the membership
   rule** — the pass sees exactly one area's leaves. Restored before return.
4. Copy the `CViewSetup` (0x25 dwords = 148 bytes) and divide two of its floats — the near and
   far planes, at struct `+0x5c` / `+0x60` — by `scale`.
5. `origin *= 1.0f/scale` then `origin += m_skybox3d.origin`, i.e.
   **`skyView.origin = view.origin / scale + skyOrigin`**. The sky viewpoint tracks the player,
   which is what makes the miniature parallax against the 2D backdrop. Inverted, the placement
   transform is `world(v) = scale · (v − origin)` — what `bake_map.py` already applies to
   `_sky.obj`.
6. `Enable3dSkyboxFog()` — pushes the `sky_camera`'s fog, *not* the world's.
7. `render->ViewSetupVis(false, 1, &skyOrigin)` (vtable `+0x70`) then `Push3DView` (`+0x74`).
8. `BuildWorldRenderLists(&list, areabits, 1, 0)` → `SetupRenderList` zeroes the list header
   (6 dwords at `list+0x48000`), then **for each leaf in the visible leaf array** calls the
   client leaf system (`0x102b9060`, vtable `+0x34`) to collate that leaf's renderables — the
   same call the main scene makes. Anything the client leaf system holds in those leaves is in.
9. `DrawWorld(&list, areabits, 0x870)`, `DrawOpaqueRenderables(&list, areabits, 1, 1, 0)`,
   `DrawTranslucentRenderables(&list, areabits, 1, 1, 0)`.
10. `DisableFog()`, restore the area bits, clear `m_bDrawingSkybox`, return `true`.

`DrawOpaqueRenderables` settles "what counts as a renderable": it
walks a flat array (count at `list+0x48004`, 12-byte entries from `list+0xc000`) of
`{flags, IClientRenderable*, …}`, masks flags against `8|0x10`, calls the renderable's
`+0x2c` (its fade/LOD test) and then `+0x24`/`+0x28` (`DrawModel`). It is type-blind — a static
prop, an NPC, a sprite and a brush entity are all just entries.

At the call site (`0x1019de36`) the frame does three more things worth knowing:

- Before calling, it runs a **skybox-visibility test** (a virtual on the render interface) and,
  under `cl_reportskybox`, prints the visible/not-visible line. Which leaf property that test
  reads is not chased (see the loose ends below) — but it gates only *whether* the pass runs,
  never *what* it draws.
- If `Draw3dSkyboxworld` returns true, it **zeroes the main pass's colour and depth clear
  flags** (three bytes in the caller's frame). So the world is drawn straight over the miniature
  with no shared depth: a 3D-skybox object is always behind everything, whatever its size, and
  can never z-fight or intersect world geometry.
- The 2D sky (K1) and the miniature are separate draws — the 2D sky is the backdrop the
  miniature composites over. That is the mottling/rain observation left behind for K7.

### The membership rule, measured

Our exporter classifies world faces by the **PVS of the `sky_camera` origin**; the engine
classifies by **BSP area**. Across all 10 exported maps the area set is a strict superset of the
PVS set and disjoint from the world's faces. The PVS misses up to 31 faces, because vis is set
up from the *player-derived* point of step 5, which moves — the sky_camera's own viewpoint is
not the one the engine uses:

| Map | sky area | areas in map | area faces | PVS faces | area∖PVS | PVS∖area | area∩world |
|---|---|---|---|---|---|---|---|
| `ch_temple_1` | 6 | 9 | 2767 | 2762 | 5 | 0 | 0 |
| `hw_609_1` | 6 | 7 | 307 | 298 | 9 | 0 | 0 |
| `la_malkavian_1` | 2 | 3 | 258 | 258 | 0 | 0 | 0 |
| `sm_hub_1` | 4 | 6 | 589 | 589 | 0 | 0 | 0 |
| `sm_oceanhouse_1` | 2 | 3 | 22 | 22 | 0 | 0 | 0 |
| `sm_pawnshop_1` | 3 | 6 | 534 | 524 | 10 | 0 | 0 |
| `sp_giovanni_1` | 1 | 3 | 236 | 236 | 0 | 0 | 0 |
| `sp_observatory_1` | 1 | 5 | 355 | 355 | 0 | 0 | 0 |
| `sp_soc_1` | 2 | 3 | 62 | 62 | 0 | 0 | 0 |
| `sp_tutorial_1` | 2 | 14 | 1680 | 1649 | 31 | 0 | 0 |

`dleaf_t.area` is the 9-bit low half of the `uint16` at **leaf offset 6** (`flags` is the top 7
bits); VtMB's `dleaf_t` is the modern 32-byte form, so this needs no new struct work. The
classifier is therefore `area(point_leaf(x)) == area(point_leaf(sky_camera.origin))` — one leaf
walk, no vis decompression, and it is what the engine itself does.

`scale` is `16` on all ten maps — and on all 43 maps in the game that have a `sky_camera`
(RE-A7). Every map carries exactly one `sky_camera` except `la_malkavian_4`, which has two.

### What lives in the sky area, per map

Measured 2026-07-26 by classifying every static prop, entity and worldlight by the leaf its
origin falls in. A brush entity's point is its model bbox centre **plus its `origin` key**:
vbsp re-centres the brushes of an entity that carries one, so a mover's stored bbox is
model-local (the sky's `func_rotating` ferris wheel sits at `(0, 0, −15)`). Only model-0 world
faces are split today; **every row below currently exports as ordinary world content at raw
miniature coordinates** — the floating debris:

| Map | static props | point ents | brush ents | worldlights in sky |
|---|---|---|---|---|
| `ch_temple_1` | 81 | 110 | 0 | 83 / 240 |
| `sm_hub_1` | 49 | 121 | 2 | 60 / 687 |
| `sp_tutorial_1` | 30 | 59 | 0 | 58 / 396 |
| `sm_pawnshop_1` | 21 | 77 | 2 | 31 / 161 |
| `sp_observatory_1` | 1 | 6 | 2 | 1 / 73 |
| `la_malkavian_1` | 0 | 15 | 1 | 3 / 109 |
| `hw_609_1` | 0 | 14 | 1 | 3 / 227 |
| `sm_oceanhouse_1` | 0 | 13 | 2 | 0 / 163 |
| `sp_soc_1` | 0 | 13 | 2 | 0 / 47 |
| `sp_giovanni_1` | 0 | 5 | 1 | 1 / 154 |

The entity mix is the same everywhere (`sky_camera` itself is one of the point entities in every
count):

- **`env_sprite`** — the moon and the lit-window/streetlight glows. 54 on `sm_hub_1`, 40 on
  `sm_pawnshop_1`, 22 on `ch_temple_1`; materials `sprites/volumelighta.vmt`,
  `sprites/glowa.vmt`, `sprites/moonchinatown.vmt`, `sprites/moonhalo.vmt`.
- **`light` / `light_spot`** — 81+2 on `ch_temple_1`, 46+14 on `sm_hub_1`, 40+18 on
  `sp_tutorial_1`. These are the Hammer entities behind the sky-area worldlight column.
- **`env_particle`** — 8 on `hw_609_1`, 9 on `la_malkavian_1`, 1–2 elsewhere.
- **`logic_timer`** — 3 on each Santa Monica map: they blink the sprites.
- **`prop_dynamic`** — the cloud planes (`structural/santamonica/clouds.mdl`,
  `structural/hollywood/clouds.mdl`), `life/bats/bats_smaller.mdl` on `sm_hub_1`/`sp_soc_1`,
  and `ChineseSigns05_sky.mdl` on `ch_temple_1`.
- **`func_rotating`** — 1–2 on eight of the ten maps: the pier ferris wheel and its companions.
  Plus one `func_brush` on `sp_observatory_1` and one `infodecal` on `ch_temple_1`.
  `ch_temple_1` and `sp_tutorial_1` are the two maps with no brush entity in the sky.

The static-prop sets name themselves. `sm_hub_1`: 23× `pawnshop/roofsky.mdl`, 8×
`street/pierlight/pierlightskybox_hol.mdl`, 7× `santamonica/parkingroofsky.mdl`, 3×
`apartment/roofcsky.mdl`, plus one each of `misc/fwheel/fwheelsky.mdl`,
`misc/coaster/coastersky.mdl`, `pier/roofsky.mdl`, `signs/asylum/asylumsky.mdl`,
`gallery/roofsky.mdl`, and 2× `santamonica/freewaysigna.mdl`. `ch_temple_1`: 42 assorted
`Ventrue_Tower/buildingtop0{1..5}.mdl`, 7× `asphole/bilboardasky.mdl`, 2× `la/clouds.mdl`.
`sp_tutorial_1`: 12× `la/handrailshort_sky.mdl`, 8× `angel/angel{,_group}.mdl`, 3×
`la/handraillong_sky.mdl`, 3× `bilboardasky.mdl`, 2× `Ventrue_Tower/buildingtop0{3,5}.mdl`, 1×
`gargoyle/mad_gargoyle_buildingleft.mdl`, 1× `theatarch/theatarch_sky.mdl`. `sp_observatory_1`'s single prop is
`observatory/trees2.mdl`.

The worldlight column matters twice over. Those lights are placed by the rig in world space at
miniature coordinates, so they light nothing they were authored to light; and they sit inside
the sample the fill-vs-fixture survey draws from (`docs/light-attribution.md`) — 60 of
`sm_hub_1`'s 687, on the very map where the classifier fails.

**What those lights were actually for** (RE-A3, and a refinement of B7's scope call): VtMB baked
the miniature's *world faces* into lump 8 like any other geometry, and lit the miniature's
*props* at runtime through the light cache, which reads lump 15. So sky-area worldlights had a
real runtime job — lighting the skybox props — they just never touched the playable world. In a
port where the miniature is real scaled geometry lit by real lights, the faithful reading is to
carry those lights **into the sky transform** (position scaled, reach scaled by `scale`), not to
delete them — which is what B7 does (owner call): re-placed inside
the transform, reach × `scale`, floored at `MinSkyReachCm`.

### Fog is two different things

`Enable3dSkyboxFog` proves the `sky_camera`'s fog belongs to the skybox pass. The world's own fog
is on **`worldspawn`** — same six keys (`fogenable`, `fogblend`, `fogdir`, `fogcolor`,
`fogcolor2`, `fogstart`, `fogend`). `<map>.env` carries both sets from their owners — `fog*`
off `worldspawn`, `skyfog*` off `sky_camera`, skybox distances ×`scale` (B8). The two differ
on 8 of the 10 exported maps:

| Map | worldspawn (enable, start→end, colour) | sky_camera | verdict |
|---|---|---|---|
| `ch_temple_1` | 1, 0→4000, `58 77 62` | 1, 0→4000, `58 77 62` | same |
| `sm_pawnshop_1` | 1, 500→5000, `17 20 25` | 1, 500→5000, `17 20 25` | same |
| `sm_hub_1` | 1, 500→5000, `17 20 25` | 1, 500→5000, `17 20 25` | differ only in `fogcolor2` |
| `la_malkavian_1` | 1, 1000→4000, `10 20 20` | 1, 5000→30000, `50 60 45` | **differ** |
| `sp_soc_1` | 0, 500→3800, `17 18 28` | 0, 500→5000, `17 20 25` | differ (both off) |
| `hw_609_1` | *no `fogenable`*, 500→2000, `255 255 255` | 0, 7800→8000, `2 2 2` | **differ** |
| `sm_oceanhouse_1` | *no `fogenable`* | 1, 500→5000, `17 20 25` | **we fog a map that has none** |
| `sp_tutorial_1`, `sp_giovanni_1`, `sp_observatory_1` | *no `fogenable`* | 0 | both off — no visible harm today |

`fogblend`, `fogcolor2` and `fogdir` are a two-colour directional blend (the fog colour lerps by
view direction against `fogdir`); the exact blend curve is not RE'd and nothing we ship uses it
yet. Every map above has `fogblend 0`.

### How to re-measure any of this

Everything in the three tables above comes from the BSP alone — no engine run:

- sky area: `area(point_leaf(sky_camera.origin))`, area = `uint16 @ leaf+6 & 0x1FF`.
- face split: union the `leafface` ranges of every leaf with that area (lumps 10/16).
- content split: classify each `sprp` prop origin, each entity `origin` (brush entities by
  `models[N]` bbox centre, lump 14), and each `dworldlight.origin` (lump 15) by its leaf's area.
- fog: `worldspawn` vs `sky_camera` keyvalues in lump 0 — already in the `.ents` sidecars.

### Loose ends (seen, deliberately not chased)

None of these block B7/B8. Listed so nobody re-derives them from scratch:

- **The skybox-visibility test** at the call site (a render-interface virtual, `+0xc0`). Likely
  the leaf sky-flag scan later Source calls `SkyboxVisibility_t`, but unread. It only gates
  whether the pass runs.
- **`CSkyCamera::Spawn`** was not located, so "`area` is computed from the entity's own leaf" is
  inferred from the data (the sky_camera's leaf area is exactly the area holding the miniature on
  all 10 maps), not read out of code. The `0xff` sentinel *is* read out of code.
- **`default_skybox`** (string `0x10288728`, referenced by `client.dll` `FUN_10067030`) and
  **`sv_skyname`** (`FUN_1010ff30`, default `"sky_urb01"`, *"Current name of the skybox
  texture"*) — the 2D-sky name plumbing, K1/K7 territory, not the 3D pass.
- **`"PlayerSky"`** (`0x102cb200`, no xref found) — an unused or vtable-reached name.
- **Detail props (`dprp`)**: the lump is present on all 10 maps and we consume none of it. How
  many sit in the sky area is unmeasured.
- **`m_bDrawingSkybox`** (`CViewRender+0x49c`) has consumers elsewhere in `client.dll` — probably
  renderables that suppress or alter themselves in the sky pass. Unenumerated.
- The **`0x870` flag word** `DrawWorld` takes, and the `1,1,0` triples the renderable draws take,
  are unnamed bit sets. They are the same values the main scene passes.

## K3 / K5 — runtime model lighting and the worldlight role (settled)

Read out of `engine.dll` (imagebase `0x20000000`) by RE-A3. The headline: **the world and the
models are lit by two disjoint systems.** World surfaces render from the baked lightmaps
(lump 8) and never touch lump 15. Lump 15 is read at runtime by exactly one subsystem — the
**light cache**, which lights dynamic models and static props — and by nothing else.

### The evidence for the split

`Mod_LoadWorldlights` (`0x200b6830`) reads lump 15 into `worldbrush + 0x13c` (count) /
`+0x140` (array), 88-byte `dworldlight_t`. A scan of every instruction in `engine.dll`
carrying those displacements enumerates the complete consumer set: the loader itself, the
level-init zero-light check (`0x2007cdb0`), one debug visualiser (`0x2006e3d0`), and the
`0x200a4e90`–`0x200ac4xx` band — which is the light cache. The lightmap pointer
(`worldbrush + 0x138`, set from lump 8) is consumed by a disjoint set in the
`0x20071xxx`–`0x20073xxx` band, the surface/lightmap builder. **No function reads both.**

Lump 15 is not *only* compiler input — it is live runtime data, but only for models.

### What the loader changes on the way in

`Mod_LoadWorldlights` is not a straight copy. Per light:

- **type 1 (`emit_point`) / type 2 (`emit_spotlight`)** with `constant_attn`, `linear_attn`
  and `quadratic_attn` all zero → `quadratic_attn = 1.0`.
- **type 2** with `exponent == 0` → `exponent = 1.0`.
- any light with `radius < 1.0` → `radius = 0` (which means *no* cutoff, not a tiny one).

It then builds a second index at `+0x144`/`+0x148`: pointers to every light with a non-zero
`style`. That list exists so lightstyle animation can be re-applied per frame without
rebuilding a cache entry.

`Mod_LoadFaces` (`0x200b73d0`) independently re-verifies our `dface_t` v17 map from the
engine's own loader: stride **104**, and it copies **8 dwords from `dface + 0x00`** to
`msurface + 0x30` and the **8 bytes at `dface + 0x30`** to `msurface + 0x60`. Those are
`avgLightColor[8]` and `styles[8]` — the two fields the sampler below reads.

### The light cache

One entry per **32 × 32 × 128** Source-unit grid cell (`0x200aaa30`), holding a
`LightingState`: a **6-face ambient cube** plus up to `r_worldlights` local light pointers.
An entry is built once (`0x200aaf00`) in two passes, then re-finished each frame
(`0x200ac400`).

**Pass 1 — the ambient cube (`0x200ab250`).** Branches on `r_radiosity` (default **2**):

| `r_radiosity` | Behaviour |
|---|---|
| 0 | cube zeroed |
| 1 | one ray per cube face |
| **2 (default)** | the 162-direction sweep below |
| 3 | one ray per cube face; static props get the 162-ray path |

`mat_fullbright 1` short-circuits the whole thing to a white cube — and level init
(`0x2007cdb0`) *forces* `mat_fullbright 1` on any map whose worldlight count is zero
(`"Level has no lights. Defaulting to 'mat_fullbright 1'"`).

The 162-direction sweep (`0x200ab5b0`) is the interesting one. For each direction in the
Quake `anorms` sphere (162 unit vectors, table at `0x20d64740`, built by the static init at
`0x200a88a0`) it traces a full-map-diagonal ray (57016.32 units) via `R_LightVec`
(`0x20077c70` → `RecursiveLightPoint` `0x200775a0`), and then:

- **hit a normal surface** → multiply the sampled colour by that material's **reflectivity**
  (material vtable `+0x84`);
- **hit a sky surface** (surface flag `0x4`) → replace the colour with the
  **`emit_skyambient` worldlight's `intensity`**;
- **hit nothing** → black.

Then each of the 6 cube faces is a cosine-weighted integral over those 162 samples,
normalised by the summed weight. That is a real **one-bounce radiosity gather off the baked
lightmap, evaluated at runtime** — VtMB does have runtime GI, for models.

The colour `R_LightVec` returns is **not** a luxel. `0x200779a0` projects the hit point
through the texinfo lightmap axes purely to bounds-check it against `LightmapMins`/
`LightmapExtents`, and then reads `msurface + 0x30 + style*4` — the face's
**`avgLightColor[style]`** — decoding RGBE through a `2^exp` table (`0x20ffa680`) and scaling
by `lightstyleValue[style] / 264`. Summed over the face's up-to-8 styles. There is no
per-luxel branch in the shipped sampler; `r_avglight` (default **1**) survives only as a
light-cache invalidation key (`0x200a9bf0`, `0x200abf70`). **This is what
`dface_t.avgLightColor[8]` at offset 0 is for** — it is Troika/early-Source's per-face
average of the bake, and it is the runtime's only read of baked light outside the lightmap
upload.

**Pass 2 — direct worldlights (`0x200abd90` → `0x200aa4b0`).** Every worldlight with
`style == 0` is offered to the state. Per light:

- `0x200a9ee0` produces intensity + direction:
  - **type 3 (`emit_skylight`)** — trace along `-normal`; contributes full intensity iff the
    trace lands on a sky surface, otherwise 0. At most **one** skylight is accepted per
    lighting state (a flag is set on the first that passes), so extra
    `light_environment` suns are silently dropped.
  - **type 5 (`emit_skyambient`)** — returns **0**. It never contributes through the direct
    path; its only runtime effect is as the sky colour in pass 1.
  - everything else — `lightstyleValue[style]/264 × Engine_WorldLightDistanceFalloff`
    (`0x200a5620`), culled when `max(intensity) × that < r_worldlightmin` (default
    **0.0078125**), then an occlusion trace toward the light (shortened by 8 units); any hit
    → 0.
- `Engine_WorldLightAngle` (`0x200a57e0`) applies the type-specific angular term —
  `emit_surface` gets a back-face test, `emit_spotlight` the `stopdot`/`stopdot2`/`exponent`
  cone (with the `exponent ∈ {0, 1}` linear shortcut), `emit_skyambient` returns 1.0.
- The light is ranked by luminance `0.299 R + 0.587 G + 0.114 B` (weights written at
  `0x200a9bd0`) times its falloff. The top **`r_worldlights`** (default **2**) become local
  lights the model shades against per-vertex; **every other light is folded into the ambient
  cube**, and a brighter latecomer evicts the dimmest incumbent into the cube.

`Engine_WorldLightDistanceFalloff` (`0x200a5620`) is the classic Source form — for point and
spot, `1 / (constant + linear·d + quadratic·d²)`, with `radius` as a hard cutoff when
non-zero; `emit_surface` uses `1/d²` (via `0x201a65a8`); `emit_quakelight` is `linear − d`
clamped at 0; everything else returns 1.0.

**Per frame (`0x200ac400`).** The cached static state is copied, then the styled worldlights
(the `+0x148` list), the 32 dlights and the elights are added on top through the same
`0x200aa4b0`. So lightstyle animation and dynamic lights are re-evaluated every frame; the
radiosity gather and the style-0 worldlights are not.

### Where the cube goes

`DAT_20d63ef0` is `IStudioRender` (`TStudioRender012`, `StudioRender.dll` imagebase
`0x2c000000`, factory `0x2c003ac0` → singleton `0x2c082b30`). It owns the cube basis: slot
`+0x20` returns the face count (6) and `+0x24` the face-direction table, which the engine
reads rather than hard-coding. `0x200a4500` pushes the model-render config — including
`r_useambientcube` (default **1**) — through slot `+0x10`. The six directions themselves were
not read out of `StudioRender.dll`; the engine code only ever treats them as an opaque
6-entry table.

### The ConVar table

Object addresses in `engine.dll`; int value at object `+0x30`, float at `+0x2c`.

| ConVar | Object | Default | Role |
|---|---|---|---|
| `r_worldlights` | `0x20d63f00` | `2` | max local (per-vertex) lights per model |
| `r_radiosity` | `0x20d64000` | `2` | ambient-cube build mode |
| `r_worldlightmin` | `0x20d64048` | `0.0078125` | luminance cull for a worldlight |
| `r_avglight` | `0x20d63f70` | `1` | cache-invalidation key only |
| `r_useambientcube` | `0x20d63ab0` | `1` | pushed to StudioRender |
| `mat_fullbright` | `0x20a6e370` | `0` | forced to 1 on a zero-worldlight map |
| `r_lightdebug` | `0x20d63fb8` | `0` | draws blocked-trace markers |
| `debug_lightatpoint` | `0x20d63c48` | `0` | the `0x200a4e90` text dump |

### What this changes for us

- **The faithfulness claim.** Our rig turns lump 15 into real Unreal lights for the
  *world*. The engine never did that: the authored world look is lump 8, and lump 15 lit only
  models. The rig + Lumen is therefore a reconstruction of lump 8, not a reproduction of a
  runtime the game had, confirming D1's "measured target against lump 8" as fact rather than
  hypothesis. D2 is unblocked on the same evidence.
- **`avgLightColor` is decodable ground truth we are not using.** It is at `dface_t + 0`,
  already in our byte map, 8 RGBE entries per face. It is the engine's own per-face summary of
  the bake — a cheap, exact second calibration target alongside the luxel grid, and the input
  to any attempt to reproduce the bounce gather.
- **Material reflectivity is available offline.** The bounce multiply reads the material's
  reflectivity, which VtMB ships in the VTF header embedded in every `.tth` (3 floats at
  offset 32 from the `VTF\0` marker; spot-checked on `materials/building/*` — e.g.
  `labldgbs05litsan` = 0.165, 0.129, 0.113). Nothing new needs decoding to reproduce the term.
- **First-wins, not last-wins.** `FindAmbientLight` (`0x200a9ea0`) returns the **first** type-5
  worldlight, and the direct path accepts the **first** type-3 that passes its sky trace. This
  answered D6 — the engine's behaviour is first-wins, not summing — and C0(a) corrected both
  consumers (`bake_map.py`, `UElysiumLightRig::Adopt`) from silent last-wins to first-wins by
  lump-15 order.
- **The loader fixups live in `bsp.read_worldlights`** (C0b): the engine rewrites
  zero-attenuation point/spot lights to `quadratic = 1`, zero-exponent spots to
  `exponent = 1`, and sub-unit radii to 0 — the decoder applies the same fixups, with
  `raw_values=True` opting out for probes that measure what VRAD wrote rather than what the
  engine lit with.
- **`r_worldlights 2`** is a striking number: in the original, a model was directly lit by at
  most two lights and got everything else as ambient. That is a feel/target datum for how much
  of VtMB's model lighting was ever directional.

## K4 — the day/night bake selection (settled: there isn't one)

The question was which of `dface_t`'s two extra lightstyle arrays the shipped bake uses, and
what picks between them at runtime — because whichever set lump 8 holds is the calibration
target for D1 and C4. **Both halves of the premise are false.** `day[8]` and `night[8]` are
unwritten in every shipped map, no engine code reads them, and lump 8 holds exactly one bake,
addressed by `styles[8]` alone. There is nothing to select and nothing to reproduce.

Four independent lines, three of them data. `tools/probe_daynight.py` is the instrument for
the data half; it runs over the whole install, not a sample.

### 1. The arrays were never written (108 maps, 567,340 faces)

Every one of the **4,538,720** bytes of `day[8]` and every one of `night[8]` is `0x00`. The
contrast with `styles[8]`, eight bytes away in the same struct, is what makes this decisive
rather than merely empty:

| Array | Offset | Byte histogram over all 108 maps |
|---|---|---|
| `styles[8]` | 48 | `0xff`: 4,043,875 (unused sentinel) · `0x00`: 432,067 (= exactly the lit-face count) · live indices `0x01`, `0x04`, `0x06`, `0x0a`, `0x20`–`0x3a`: 62,778 |
| `day[8]` | 56 | `0x00`: 4,538,720 — nothing else |
| `night[8]` | 64 | `0x00`: 4,538,720 — nothing else |

An authored-but-empty lightstyle slot reads `0xFF`; a field the compiler never touched reads
`0x00`. `styles` is full of both. The other two are untouched memory.

### 2. Lump 8 holds one bake, not two

Per lit face, the span from its `lightofs` to the next face's, divided by its style count,
measured in whole luxel grids (`(sizeX+1)·(sizeY+1)·4` bytes):

| Grids per style | Extra bytes | Faces | Reading |
|---|---|---|---|
| 1 | 0 | 288,848 | a plain bake — one grid per lightstyle |
| 4 | 4 | 111,421 | a bumped bake — `NUM_BUMP_VECTS+1` grids, preceded by one 4-byte average colour |
| 4 | 0/2/6/8/12… | ~30,000 | the same bumped bake; the remainder is block alignment at the gaps |

**No face in any map stores 2 or 8 grids per style** — which is what a day set plus a night set
would cost. (Summing the spans is *not* a check: they telescope to the lump length whatever the
contents. The per-face divisibility above is the test that can fail, and doesn't.)

### 3. Nothing in `engine.dll` reads offsets 56–71

The FACES lump has exactly three consumers. Enumerated exhaustively, not sampled: every one of
the **55** call sites of the lump accessor `FUN_200b6670` was located by scanning `.text` for
`CALL rel32` to it and decoding the lump index from the preceding `push`.

| Address | What | `dface_t` fields it touches |
|---|---|---|
| `0x200b73d0` | `Mod_LoadFaces` | `avgLightColor[8]` @0, `planenum` @32, `side` @34, `onnode` @35, `firstedge` @36, `numedges` @40, `texinfo` @42, `dispinfo` @44, **`styles[8]` @48**, `lightofs` @72, `LightmapMins` @80, `LightmapSize` @88, `smoothingGroups` @100 |
| `0x200b9c30` | face centroid + radius builder (writes 5 floats/face to `worldbrush + 0x124`) | none — walks the built `msurface` array (stride `0x98`), never the raw lump |
| `0x20033b30` | `CMod_LoadDispInfo` | `firstedge` @36, `numedges` @40, `dispinfo` @44 |

`Mod_LoadFaces` copies `avgLightColor` (dface+0 → `msurface+0x30`) and `styles` (dface+0x30 →
`msurface+0x60`) and steps the read pointer by the full 104-byte stride. **Offsets 56–71 are
skipped by all three.** Nothing else in the binary ever sees them.

### 4. No selector exists anywhere

- **No string.** `engine.dll` contains zero defined strings matching day/night in any case.
- **No ConVar.** The registration-table dump carries none (`r_lightstyle` is the nearest, and
  it is the per-frame lightstyle animation, not a bake switch).
- **No worldspawn key.** The complete key set across all 108 maps is `classname`, `skyname`,
  `levelscript`, `world_mins`/`world_maxs`, `MaxRange`, `sounds`, `comment`, `angles`,
  `message`, `newunit`, the fog set (`fogenable`/`fogstart`/`fogend`/`fogcolor`/`fogcolor2`/
  `fogdir`/`fogblend`), the Troika gameplay keys (`safearea`, `copwaitarea`, `nofrenzyarea`,
  `nosferatu_tolerrant`, `wetness_fadein`/`_fadeout`/`_fadetarget`), the compiler hints
  (`vrad_nolightcutoff`, `vrad_lightsmooth`, `detailvbsp`, `detailmaterial`,
  `maxpropscreenwidth`). Nothing time-of-day.
- **No entity key or value.** A regex sweep for day/night/dusk/dawn/sunset/sunrise/time-of-day
  over every key on every entity of every map returns nothing.

### The one "daylight" in the game is a screen effect

`m_daylight_level` is a **3-bit networked player field** — server SendTable `0x1018a730`
(`vampire.dll`) at player offset `0xdc`, which is the same table that carries `m_skybox3d.*`;
client RecvTable `0x100a3e00` (`client.dll`), member `+0x1738`. It has exactly **two** reads in
`client.dll`, at `0x1019dd08` and `0x1019dd9d` — in the view-render epilogue, a few instructions
before the `Draw3dSkyboxworld` call at `0x1019de36` — where it and `m_nightvision_level`
(`+0x1734`) are pushed into slots `+0x28`/`+0x30` of the screen-effect interface at
`[0x104a57e4]`. It is gated by `cl_obfuscate_daylight` (registered at `0x1019db40`, default
`"-100"`), which the shipped `autoexec.cfg` sets to `0`.

So it is a client-side screen overlay, not a lighting input: it never touches lump 8, lump 15
or the light cache, and `engine.dll` — which owns all three — has no daylight concept at all.

### Where the "two full bakes" reading came from

`day`/`night` are **bspsrc's** names for the two arrays (`DFaceVTMB.java`, both fields carrying
the comment *"Nightime lightmapping system"*) — a guess at what the reserved space was for; the
bytes never supported it. The names are kept in the struct map because they are the only
published ones.

**Scope of the claim.** This settles the shipped game: no map carries a second bake and no
shipped binary reads one. It says nothing about whether Troika's in-house compiler ever had the
feature, and it does not bear on K6 — what VRAD did with the sky pair when it baked the one set
that exists is still open, and is RE-A5's question.

### What this changes for us

- **D1 and C4 lose their caveat.** "lump 8 (night set, pending K4)" is simply **lump 8**,
  indexed by `styles[8]`. No branch of the calibration work was waiting on this.
- **The lighting variation VtMB actually ships is lightstyles**, not a bake pair: 466 light
  entities on style `32` and a long tail through style `58`, plus the classic animated styles
  1/4/6/10 — switched and animated per light at runtime (`lightstyleValue[style]/264`, RE-A3),
  not selected per face at load. If a map changes its lighting, that is the mechanism.

## The full-game inventory (RE-A7, settled)

Every *inventory*-shaped fact above — which maps have a sky, a pair, a `sky_camera`, what fog
they carry — came from the ten exported maps. RE-A7 asks those questions of all **108**, from
the BSPs alone (no export, no engine run) with `tools/probe_sky_inventory.py`, measured
2026-07-26. It closes K8, and it re-bases the inventory rows in "What we know" from a ten-map
sample onto the whole game.

### Headline

- **All 108 maps carry a `worldspawn.skyname`**, naming **11 distinct sky sets**, every one of
  them present in the install with all six faces. A `skyname` is therefore *not* evidence that
  a map shows sky.
- **66 maps have `toolsskybox` brushwork.** The other 42 name a sky no surface ever draws.
- **25 maps carry the sky pair**, and it is never half-present: those same 25 maps hold every
  type-3 `emit_skylight` and every type-5 `emit_skyambient` in the game. The other **83 have no
  `light_environment` at all** — for them the sky contributes no light, which is the faithful
  reading C2 implements (a zero SkyLight) rather than an `sm_hub_1` inference.
- **43 maps have a `sky_camera`** (the 3D-skybox pass), and `scale` is **16** on every one.
- Of the game's **19,197** worldlights, **1,442 (7.5%)** sit inside a sky area — lights that
  never lit the playable world, and that our rig places in it at 1/16 scale today (B7).

### Showing the sky and being lit by it are independent

| shows sky (`toolsskybox`) | lit by it (type 3/5 pair) | maps |
|---|---|---|
| yes | yes | 25 |
| yes | no | **41** |
| no | yes | 0 |
| no | no | 42 |

The empty cell is the informative one: **a sky pair implies brushwork, never the reverse.** No
map is lit by a sky it never draws — but 41 maps draw sky and take zero light from it.
`sm_hub_1` is not an oddity, it is the plurality case, and C2's "no sky pair → no sky light"
governs 83 of 108 maps.

The three sky features are independent of each other too: 26 of the 43 `sky_camera` maps carry
no pair, and 8 pair maps have no `sky_camera` (`ch_fishmarket_1`, `la_bradbury_1`,
`la_library_1`, `la_malkavian_3b`, `la_museum_1`, `sm_warehouse_1`, `sp_giovanni_2a`,
`sp_giovanni_2b`).

### The sky sets

| `skyname` | maps | faces | size | maps that show it (`toolsskybox`) |
|---|---|---|---|---|
| `santamonica` | 36 | 6/6 | 512x512 | 16 |
| `holly` | 22 | 6/6 | 256x256 | 15 |
| `la` | 22 | 6/6 | 512x512 *(probe installed)* | 16 |
| `pier` | 12 | 6/6 | 512x512 *(probe installed)* | 11 |
| `hav` | 5 | 6/6 | 512x512 | 1 |
| `warehouse` | 4 | 6/6 | 512x512 | 1 |
| `chinatown` | 3 | 6/6 | 256x256 | 2 |
| `holylight` | 1 | 6/6 | 512x512 | 1 |
| `moon` | 1 | 6/6 | 512x512 | 1 |
| `warehouse2` | 1 | 6/6 | 512x512 | 1 |
| `warehouse3` | 1 | 6/6 | 512x512 | 1 |

`holly` and `chinatown` are the two 256² sets — B5's enhancement layer inherits that split.
The `la` and `pier` rows resolve **loose** at the time of measurement because the RE-A2
labelled probe is installed over them; the shipped `pier` is the patch's uncompressed BGR888
re-export and `la` is retail DXT5 in the VPKs. `sky_probe.py` encodes at the same size and
format as the set it shadows, so only the pixels differ — but the scan flags it rather than
report probe art as game data.

### The sky-pair maps

| Map | `light_env` | `_light` | `_ambient` | lump-15 type 3 | lump-15 type 5 |
|---|---|---|---|---|---|
| `ch_fishmarket_1` | 1 | `255 255 255 200` | `255 255 255 20` | `0.784314 0.784314 0.784314` | `0.0784314 0.0784314 0.0784314` |
| `ch_temple_1` | 3 | `187 255 247 40` | `164 221 255 20` | `0.0792835 0.156863 0.14624` | `0.0297001 0.0572485 0.0784314` |
| `hw_609_1` | 1 | `73 123 177 7` | `85 123 151 8` | `0.00175178 0.00552028 0.0122945` | `0.00279823 0.00630889 0.00990628` |
| `hw_cemetery_1` | 1 | `103 184 250 0` | `72 141 217 20` | `0 0 0` | `0.00485549 0.0213002 0.0549937` |
| `hw_chateau_1` | 1 | `158 168 233 40` | `32 56 100 10` | `0.0547239 0.0626342 0.128622` | `0.000407757 0.00139664 0.00500117` |
| `hw_chinese_1` | 1 | `98 125 166 1000` | `81 94 183 0` | `0.478375 0.817097 1.52514` | `0 0 0` |
| `hw_jewelry_1` | 1 | `73 123 177 7` | `85 123 151 8` | `0.00175178 0.00552028 0.0122945` | `0.00279823 0.00630889 0.00990628` |
| `hw_luckystar_1` | 1 | `71 75 126 75` | `81 130 155 5` | `0.0176564 0.019919 0.062366` | `0.00157293 0.00445366 0.006558` |
| `la_bradbury_1` | 1 | `132 66 0 30` | `15 0 147 33` | `0.0276347 0.00601436 0` | `0.000254089 0 0.0385198` |
| `la_dane_1` | 1 | `104 113 140 50` | `104 113 140 50` | `0.0272593 0.0327201 0.0524232` | `0.0272593 0.0327201 0.0524232` |
| `la_library_1` | 1 | `254 255 206 10` | `255 255 112 12` | `0.0388782 0.0392157 0.0245233` | `0.0470588 0.0470588 0.00770074` |
| `la_malkavian_3b` | 1 | `56 223 255 300` | `50 67 82 60` | `0.0418993 0.875918 1.17647` | `0.00653065 0.0124333 0.0193916` |
| `la_museum_1` | 1 | `129 55 12 24` | `13 48 119 20` | `0.0210175 0.00322167 0.000113105` | `0.000112403 0.0019899 0.0146658` |
| `sm_warehouse_1` | 4 | `98 140 191 25` | `201 221 254 13` | `0.0119594 0.0262116 0.051914` | `0.0302027 0.0372116 0.0505416` |
| `sp_endsequences_b` | 1 | `104 113 140 1000` | `104 113 140 10` | `0.545186 0.654401 1.04846` | `0.00545186 0.00654401 0.0104846` |
| `sp_giovanni_1` | 1 | `156 210 228 40` | `171 173 254 20` | `0.0532115 0.102333 0.122628` | `0.0325606 0.0334043 0.0777563` |
| `sp_giovanni_2a` | 1 | `176 192 255 40` | `255 255 255 10` | `0.069384 0.0840222 0.156863` | `0.0392157 0.0392157 0.0392157` |
| `sp_giovanni_2b` | 1 | `176 192 255 40` | `255 255 255 10` | `0.069384 0.0840222 0.156863` | `0.0392157 0.0392157 0.0392157` |
| `sp_ninesintro` | 1 | `102 142 187 50` | `200 200 255 10` | `0.0261193 0.0540849 0.0991044` | `0.0229793 0.0229793 0.0392157` |
| `sp_observatory_1` | 1 | `188 211 254 60` | `255 255 255 0` | `0.120329 0.155112 0.233269` | `0 0 0` |
| `sp_observatory_2` | 2 | `188 211 254 80` | `255 255 255 20` | `0.120329 0.155112 0.233269` | `0.0784314 0.0784314 0.0784314` |
| `sp_soc_1` | 1 | `151 165 198 60` | `81 130 155 25` | `0.0742971 0.0902999 0.134861` | `0.00786463 0.0222683 0.03279` |
| `sp_soc_2` | 2 | `152 155 197 50` | `81 130 155 5` | `0.0628199 0.06558 0.111139` | `0.00157293 0.00445366 0.006558` |
| `sp_taxiride` | 3 | `167 210 245 50` | `255 255 255 25` | `0.0772712 0.127916 0.179559` | `0.0980392 0.0980392 0.0980392` |
| `sp_tutorial_1` | 1 | `152 155 197 50` | `81 130 155 5` | `0.0628199 0.06558 0.111139` | `0.00157293 0.00445366 0.006558` |

Three things this settles:

- **The fourth field of `_light`/`_ambient` is brightness, and the type-3 magnitude tracks it
  linearly** — `sp_observatory_1`'s `188 211 254 60` compiles to `0.120329 0.155112 0.233269`,
  and `sp_observatory_2`'s otherwise identical `… 80` to `0.160439 0.206815 0.311025`. C1
  derives SkyLight intensity from the **type-5** magnitude, and the range it must span is wide:
  `hw_609_1`'s `0.0028 0.0063 0.0099` to `sp_taxiride`'s flat `0.098`, a factor of ~35 — with
  `hw_chinese_1` and `sp_observatory_1` carrying a type-5 of **exactly zero** and
  `hw_cemetery_1` a type-3 of zero. A pair can be half-dark: magnitude 0 is a real authored
  value, not a missing one.
- **Multiple `light_environment`s are five maps, not one** — `sm_warehouse_1` (4),
  `ch_temple_1` and `sp_taxiride` (3), `sp_observatory_2` and `sp_soc_2` (2). On four of them
  every entity is byte-identical, so first-wins, last-wins and summing differ only by a factor.
  **On `sp_observatory_2` the two genuinely differ** (`_light … 80` / `_ambient … 20` against
  `… 60` / `… 10`): the one map in the game where D6's choice is observable.
- **First-wins is over lump 15, not over the entity lump.** `sp_observatory_2`'s first lump-15
  pair belongs to the *second* `light_environment` (the `60`-brightness one at
  `640 -12.25 -3968`). Our `.lights` sidecar preserves lump-15 order, one line per row, so
  C0a's fix is "take the first type-5 row" — what `FindAmbientLight` does — not "take the first
  `light_environment`".

Two data points recorded for K6/RE-A5, neither blocking: `sp_soc_2` has two
`light_environment`s but only **one** pair in lump 15 (the entity that produced none is the one
sitting in the sky area), and `sp_observatory_2`'s two pairs carry different type-3 magnitudes
but the **same** type-5 magnitude — `0.0784` = 20/255, the *first* entity's `_ambient`
brightness — although the two entities are authored at 20 and 10. VRAD did something to the
skyambient that the entity keys alone do not explain.

### The 3D skybox, all 43 maps

`scale` is `16` on every map. `la_malkavian_4` is the one map with **two** `sky_camera`s (all
others have exactly one), so "the sky_camera" is a per-map assumption with one exception the
exporter needs a rule for; this scan reads the first.

Across the 43: **35,738** world faces, **1,043** static props (82 unique models; 607 of the
placements carry `sky` in the model name), **2,173** point entities, **102** brush entities and
**1,442** worldlights live in a sky area. The entity mix generalises cleanly from the ten-map
sample — `light` 998, `env_sprite` 497, `light_spot` 307, `env_particle` 167, `prop_dynamic`
70, `func_rotating` 49, `logic_timer` 39, `mover_keyframe` 39, `func_keyframed_mover` 36,
`func_brush` 12, `func_illusionary` 5 — and 15 of the 43 have no static prop in the sky at all.

The worldlight column is where the ten-map sample understated the problem most: `la_hub_1` 136
of 756, `sp_endsequences_a` 136 of 327, `la_ventruetower_1` 91 of 243, `sp_endsequences_b` 91
of 281, `ch_hub_1` 89 of 496; nine maps keep over a fifth of their worldlights inside the
miniature. The ten exported maps hold 240 of the game's 1,442 sky worldlights, so B7's
correction — and its effect on the fill-vs-fixture sample in `docs/light-attribution.md` — is
about six times larger than the sample showed.

The RE-A8 content table above is corrected in place for `sp_tutorial_1` (30 props / 59 point
ents / 58 worldlights) and `sm_pawnshop_1` (21 props): those two rows had been classified by
the `sky_camera`'s **PVS** rather than by leaf area, and they are exactly the two maps whose
sky area exceeds that PVS by content — `sm_pawnshop_1`'s extra prop is `gallery/roofsky.mdl`.
The area rule is the engine's; the other eight rows are unaffected.

### Fog

Fog is two authored key sets — `worldspawn` the world's, `sky_camera` the skybox pass's —
and the full scan sizes how far they differ.

- **65 maps have no `sky_camera`.** On **12** of them `worldspawn` carries `fogenable 1` —
  `ch_temple_4`, `hw_ash_sewer_1`, `hw_sinbin_1`, `la_chantry_1`, `la_crackhouse_1`,
  `la_empire_1`, `sm_bailbonds_1`, `sm_pawnshop_2`, `sm_smoke_1`, `sm_tattoo`,
  `sm_warehouse_1`, `sp_genesisdevice_1` — fog only `worldspawn` sourcing can deliver.
- Of the 43 maps that do have one, the two sets disagree on **27** in the render-relevant
  fields (colour/range/enable; **30** counting `fogcolor2`/`fogdir`, which nothing we ship
  reads), and on **6** (`la_parkinggarage_1`, `sm_oceanhouse_1`, `sp_endsequences_a`,
  `sp_endsequences_b`, `sp_soc_2`, `sp_theatre`) the `sky_camera` enables fog the
  `worldspawn` never asked for.

B8 sources each set from its owner, which fixes both failure directions at once.

| Map | `worldspawn` (enable, start→end, colour) | `sky_camera` | verdict |
|---|---|---|---|
| `hw_609_1` | no fogenable | 0, 7800→8000, `2 2 2` | **differ** |
| `hw_cemetery_1` | no fogenable | 0, 7800→8000, `2 2 2` | **differ** |
| `hw_chateau_1` | 1, 1000→12000, `0 0 0` | 0, 500→12000, `158 168 233` | **differ** |
| `hw_chinese_1` | no fogenable | 0, 7800→8000, `2 2 2` | **differ** |
| `hw_hub_1` | no fogenable | 0, 7800→8000, `2 2 2` | **differ** |
| `hw_jewelry_1` | no fogenable | 0, 7800→8000, `2 2 2` | **differ** |
| `hw_luckystar_1` | 0, 500→3000, `41 41 56` | 0, 7800→8000, `2 2 2` | **differ** |
| `hw_tawni_1` | no fogenable | 0, 7800→8000, `2 2 2` | **differ** |
| `la_abandoned_building_1` | 1, 2000→15000, `10 10 40` | 1, 2000→15000, `10 10 40` | differ only in the blend keys |
| `la_expipe_1` | 1, 2000→15000, `10 10 40` | 1, 2000→15000, `10 10 40` | differ only in the blend keys |
| `la_hub_1` | 1, 2000→15000, `10 10 40` | 1, 2000→15000, `10 10 40` | differ only in the blend keys |
| `la_malkavian_1` | 1, 1000→4000, `10 20 20` | 1, 5000→30000, `50 60 45` | **differ** |
| `la_malkavian_2` | no fogenable | 0, 500→2000, `255 255 255` | **differ** |
| `la_malkavian_4` | no fogenable | 0, 500→2000, `255 255 255` | **differ** |
| `la_malkavian_5` | 1, 1000→4000, `10 20 20` | 1, 5000→30000, `50 60 45` | **differ** |
| `la_parkinggarage_1` | no fogenable | 1, 2000→15000, `10 10 40` | **we fog a map whose `worldspawn` does not** |
| `la_ventruetower_2` | no fogenable | 0, 500→10000, `50 0 5` | **differ** |
| `la_ventruetower_3` | no fogenable | 0, 500→10000, `50 0 5` | **differ** |
| `sm_beachhouse_1` | no fogenable | 0, 500→5000, `17 20 25` | **differ** |
| `sm_hub_1` | 1, 500→5000, `17 20 25` | 1, 500→5000, `17 20 25` | differ only in the blend keys |
| `sm_hub_2` | 1, 500→5000, `17 20 25` | 1, 500→5000, `17 20 25` | differ only in the blend keys |
| `sm_oceanhouse_1` | no fogenable | 1, 500→5000, `17 20 25` | **we fog a map whose `worldspawn` does not** |
| `sm_oceanhouse_2` | no fogenable | 0, 500→2000, `255 255 255` | **differ** |
| `sm_pier_1` | 0, 2000→8000, `1 1 1` | 0, 500→5000, `17 20 25` | **differ** |
| `sp_endsequences_a` | 0, 2000→15000, `10 10 40` | 1, 2000→15000, `10 10 40` | **differ** |
| `sp_endsequences_b` | no fogenable | 1, 2000→15000, `10 10 40` | **we fog a map whose `worldspawn` does not** |
| `sp_epilogue` | no fogenable | 0, 7800→8000, `2 2 2` | **differ** |
| `sp_giovanni_1` | no fogenable | 0, 500→2000, `255 255 255` | **differ** |
| `sp_ninesintro` | no fogenable | 0, 500→5000, `17 20 25` | **differ** |
| `sp_observatory_1` | no fogenable | 0, 500→2000, `255 255 255` | **differ** |
| `sp_observatory_2` | no fogenable | 0, 500→2000, `255 255 255` | **differ** |
| `sp_soc_1` | 0, 500→3800, `17 18 28` | 0, 500→5000, `17 20 25` | **differ** |
| `sp_soc_2` | no fogenable | 1, 500→3800, `17 18 28` | **we fog a map whose `worldspawn` does not** |
| `sp_taxiride` | 1, 256→2048, `0 0 0` | 0, 256→2048, `0 0 0` | **differ** |
| `sp_theatre` | no fogenable | 1, 500→5000, `17 20 25` | **we fog a map whose `worldspawn` does not** |
| `sp_tutorial_1` | no fogenable | 0, 500→2000, `255 255 255` | **differ** |

wrote tools\out\_sky\inventory.json (108 maps)

### Per-map inventory

`sky brush` counts faces whose material name contains `SKYBOX` under `TOOLS/`; `sun/amb` is the
lump-15 type-3 / type-5 count; `sky area` is the `sky_camera`'s leaf area over the map's
distinct area count; `props`/`ents`/`lights in sky` are that area's population (`ents` = point
+ brush); `fog ws/cam` is `fogenable` on `worldspawn` / on `sky_camera`, where `–` means the
key is absent (or the map has no `sky_camera`).

| Map | `skyname` | sky brush | `light_env` | sun/amb | worldlights | `sky_camera` | sky area | props | ents | lights in sky | fog ws/cam |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `ch_cloud_1` | `la` | 0 | 0 | 0/0 | 11 | 0 | – | – | – | – | –/– |
| `ch_dragon_1` | `la` | 0 | 0 | 0/0 | 89 | 0 | – | – | – | – | –/– |
| `ch_fishmarket_1` | `santamonica` | 10 | 1 | 1/1 | 45 | 0 | – | – | – | – | –/– |
| `ch_fulab_1` | `holly` | 0 | 0 | 0/0 | 341 | 0 | – | – | – | – | –/– |
| `ch_glaze_1` | `chinatown` | 0 | 0 | 0/0 | 226 | 0 | – | – | – | – | –/– |
| `ch_hub_1` | `chinatown` | 478 | 0 | 0/0 | 496 | 1 | 4/6 | 93 | 128 | 89 | on/on |
| `ch_lotus_1` | `la` | 0 | 0 | 0/0 | 158 | 0 | – | – | – | – | –/– |
| `ch_ramen_1` | `la` | 0 | 0 | 0/0 | 22 | 0 | – | – | – | – | –/– |
| `ch_shrekhub` | `santamonica` | 0 | 0 | 0/0 | 72 | 0 | – | – | – | – | –/– |
| `ch_temple_1` | `chinatown` | 296 | 3 | 3/3 | 240 | 1 | 6/9 | 81 | 110 | 83 | on/on |
| `ch_temple_2` | `santamonica` | 0 | 0 | 0/0 | 448 | 0 | – | – | – | – | –/– |
| `ch_temple_3` | `santamonica` | 0 | 0 | 0/0 | 93 | 0 | – | – | – | – | –/– |
| `ch_temple_4` | `santamonica` | 0 | 0 | 0/0 | 60 | 0 | – | – | – | – | on/– |
| `ch_tsengs_1` | `santamonica` | 0 | 0 | 0/0 | 9 | 0 | – | – | – | – | –/– |
| `ch_zhaos_1` | `holly` | 57 | 0 | 0/0 | 46 | 0 | – | – | – | – | –/– |
| `hw_609_1` | `holly` | 133 | 1 | 1/1 | 227 | 1 | 6/7 | – | 15 | 3 | –/off |
| `hw_ash_sewer_1` | `holly` | 20 | 0 | 0/0 | 68 | 0 | – | – | – | – | on/– |
| `hw_asphole_1` | `santamonica` | 0 | 0 | 0/0 | 299 | 0 | – | – | – | – | –/– |
| `hw_cemetery_1` | `holly` | 321 | 1 | 1/1 | 233 | 1 | 5/6 | 5 | 78 | 66 | –/off |
| `hw_chateau_1` | `holly` | 263 | 1 | 1/1 | 193 | 1 | 2/3 | 2 | 14 | 2 | on/off |
| `hw_chinese_1` | `la` | 73 | 1 | 1/1 | 37 | 1 | 3/4 | – | 4 | – | –/off |
| `hw_hub_1` | `holly` | 827 | 0 | 0/0 | 504 | 1 | 3/6 | 5 | 90 | 78 | –/off |
| `hw_jewelry_1` | `holly` | 88 | 1 | 1/1 | 251 | 1 | 4/6 | – | 15 | 3 | –/off |
| `hw_luckystar_1` | `holly` | 91 | 1 | 1/1 | 125 | 1 | 1/8 | 4 | 76 | 63 | off/off |
| `hw_metalhead_1` | `santamonica` | 0 | 0 | 0/0 | 29 | 0 | – | – | – | – | –/– |
| `hw_netcafe_1` | `santamonica` | 0 | 0 | 0/0 | 106 | 0 | – | – | – | – | –/– |
| `hw_redspot_1` | `holly` | 0 | 0 | 0/0 | 33 | 0 | – | – | – | – | –/– |
| `hw_sinbin_1` | `santamonica` | 0 | 0 | 0/0 | 42 | 0 | – | – | – | – | on/– |
| `hw_tawni_1` | `santamonica` | 120 | 0 | 0/0 | 79 | 1 | 1/4 | 1 | 27 | 14 | –/off |
| `hw_vesuvius_1` | `holly` | 0 | 0 | 0/0 | 260 | 0 | – | – | – | – | –/– |
| `hw_warrens_1` | `holly` | 13 | 0 | 0/0 | 136 | 0 | – | – | – | – | off/– |
| `hw_warrens_2` | `santamonica` | 11 | 0 | 0/0 | 268 | 0 | – | – | – | – | –/– |
| `hw_warrens_2b` | `la` | 0 | 0 | 0/0 | 61 | 0 | – | – | – | – | –/– |
| `hw_warrens_3` | `la` | 4 | 0 | 0/0 | 264 | 0 | – | – | – | – | –/– |
| `hw_warrens_4` | `la` | 9 | 0 | 0/0 | 171 | 0 | – | – | – | – | –/– |
| `hw_warrens_5` | `santamonica` | 0 | 0 | 0/0 | 226 | 0 | – | – | – | – | –/– |
| `la_abandoned_building_1` | `la` | 181 | 0 | 0/0 | 127 | 1 | 1/3 | 35 | 41 | 63 | on/on |
| `la_bradbury_1` | `warehouse3` | 69 | 1 | 1/1 | 70 | 0 | – | – | – | – | off/– |
| `la_bradbury_2` | `santamonica` | 0 | 0 | 0/0 | 199 | 0 | – | – | – | – | –/– |
| `la_bradbury_3` | `santamonica` | 0 | 0 | 0/0 | 43 | 0 | – | – | – | – | –/– |
| `la_chantry_1` | `la` | 15 | 0 | 0/0 | 90 | 0 | – | – | – | – | on/– |
| `la_confession_1` | `warehouse` | 0 | 0 | 0/0 | 125 | 0 | – | – | – | – | off/– |
| `la_crackhouse_1` | `santamonica` | 6 | 0 | 0/0 | 73 | 0 | – | – | – | – | on/– |
| `la_dane_1` | `warehouse` | 952 | 1 | 1/1 | 118 | 1 | 1/9 | – | 14 | – | on/on |
| `la_empire_1` | `la` | 0 | 0 | 0/0 | 510 | 0 | – | – | – | – | on/– |
| `la_empire_2` | `santamonica` | 27 | 0 | 0/0 | 327 | 0 | – | – | – | – | –/– |
| `la_empire_3` | `santamonica` | 5 | 0 | 0/0 | 35 | 0 | – | – | – | – | –/– |
| `la_expipe_1` | `la` | 77 | 0 | 0/0 | 128 | 1 | 1/3 | 32 | 34 | 22 | on/on |
| `la_hospital_1` | `santamonica` | 0 | 0 | 0/0 | 84 | 0 | – | – | – | – | off/– |
| `la_hub_1` | `la` | 691 | 0 | 0/0 | 756 | 1 | 5/8 | 100 | 142 | 136 | on/on |
| `la_library_1` | `la` | 131 | 1 | 1/1 | 251 | 0 | – | – | – | – | –/– |
| `la_malkavian_1` | `holly` | 143 | 0 | 0/0 | 109 | 1 | 2/3 | – | 16 | 3 | on/on |
| `la_malkavian_2` | `holly` | 83 | 0 | 0/0 | 334 | 1 | 12/14 | 84 | 93 | 45 | –/off |
| `la_malkavian_3` | `holly` | 0 | 0 | 0/0 | 162 | 0 | – | – | – | – | –/– |
| `la_malkavian_3b` | `holylight` | 13 | 1 | 1/1 | 91 | 0 | – | – | – | – | –/– |
| `la_malkavian_4` | `holly` | 88 | 0 | 0/0 | 186 | 2 | 10/11 | 66 | 75 | 30 | –/off |
| `la_malkavian_5` | `holly` | 110 | 0 | 0/0 | 120 | 1 | 2/3 | – | 16 | 3 | on/on |
| `la_museum_1` | `la` | 405 | 1 | 1/1 | 388 | 0 | – | – | – | – | –/– |
| `la_parkinggarage_1` | `la` | 132 | 0 | 0/0 | 184 | 1 | 1/3 | 10 | 31 | 32 | –/on |
| `la_plaguebearer_sewer_1` | `la` | 11 | 0 | 0/0 | 59 | 0 | – | – | – | – | –/– |
| `la_skyline_1` | `santamonica` | 7 | 0 | 0/0 | 149 | 0 | – | – | – | – | –/– |
| `la_ventruetower_1` | `santamonica` | 152 | 0 | 0/0 | 243 | 1 | 3/7 | 72 | 82 | 91 | on/on |
| `la_ventruetower_1b` | `la` | 534 | 0 | 0/0 | 380 | 1 | 1/18 | 58 | 79 | 69 | on/on |
| `la_ventruetower_2` | `la` | 191 | 0 | 0/0 | 488 | 1 | 1/13 | 2 | 33 | 5 | –/off |
| `la_ventruetower_3` | `hav` | 413 | 0 | 0/0 | 203 | 1 | 2/6 | 2 | 28 | – | –/off |
| `sm_apartment_1` | `holly` | 18 | 0 | 0/0 | 157 | 0 | – | – | – | – | –/– |
| `sm_asylum_1` | `holly` | 0 | 0 | 0/0 | 448 | 0 | – | – | – | – | –/– |
| `sm_bailbonds_1` | `santamonica` | 0 | 0 | 0/0 | 7 | 0 | – | – | – | – | on/– |
| `sm_basement_1` | `santamonica` | 0 | 0 | 0/0 | 42 | 0 | – | – | – | – | –/– |
| `sm_beachhouse_1` | `pier` | 365 | 0 | 0/0 | 268 | 1 | 2/3 | – | 15 | – | –/off |
| `sm_coffee_1` | `hav` | 0 | 0 | 0/0 | 15 | 0 | – | – | – | – | –/– |
| `sm_diner_1` | `holly` | 0 | 0 | 0/0 | 34 | 0 | – | – | – | – | –/– |
| `sm_gallery_1` | `santamonica` | 0 | 0 | 0/0 | 48 | 0 | – | – | – | – | –/– |
| `sm_hub_1` | `pier` | 729 | 0 | 0/0 | 687 | 1 | 4/6 | 49 | 123 | 60 | on/on |
| `sm_hub_2` | `pier` | 674 | 0 | 0/0 | 599 | 1 | 2/3 | 48 | 123 | 59 | on/on |
| `sm_junkyard_1` | `pier` | 163 | 0 | 0/0 | 120 | 1 | 2/3 | 16 | 102 | 51 | on/on |
| `sm_medical_1` | `holly` | 0 | 0 | 0/0 | 204 | 0 | – | – | – | – | –/– |
| `sm_oceanhouse_1` | `santamonica` | 323 | 0 | 0/0 | 163 | 1 | 2/3 | – | 15 | – | –/on |
| `sm_oceanhouse_2` | `moon` | 263 | 0 | 0/0 | 322 | 1 | 2/25 | 50 | 25 | 6 | –/off |
| `sm_pawnshop_1` | `pier` | 248 | 0 | 0/0 | 161 | 1 | 3/6 | 21 | 79 | 31 | on/on |
| `sm_pawnshop_2` | `pier` | 0 | 0 | 0/0 | 9 | 0 | – | – | – | – | on/– |
| `sm_pier_1` | `pier` | 474 | 0 | 0/0 | 371 | 1 | 2/3 | – | 23 | – | off/off |
| `sm_shreknet_1` | `santamonica` | 0 | 0 | 0/0 | 26 | 0 | – | – | – | – | –/– |
| `sm_smoke_1` | `hav` | 0 | 0 | 0/0 | 27 | 0 | – | – | – | – | on/– |
| `sm_tattoo` | `hav` | 0 | 0 | 0/0 | 13 | 0 | – | – | – | – | on/– |
| `sm_vamparena` | `santamonica` | 0 | 0 | 0/0 | 37 | 0 | – | – | – | – | –/– |
| `sm_warehouse_1` | `warehouse2` | 782 | 4 | 4/4 | 335 | 0 | – | – | – | – | on/– |
| `sp_endsequences_a` | `la` | 395 | 0 | 0/0 | 327 | 1 | 1/5 | 93 | 156 | 136 | off/on |
| `sp_endsequences_b` | `santamonica` | 111 | 1 | 1/1 | 281 | 1 | 2/5 | 72 | 88 | 91 | –/on |
| `sp_epilogue` | `la` | 449 | 0 | 0/0 | 150 | 1 | 1/4 | – | 18 | 3 | –/off |
| `sp_genesisdevice_1` | `hav` | 0 | 0 | 0/0 | 1 | 0 | – | – | – | – | on/– |
| `sp_giovanni_1` | `holly` | 477 | 1 | 1/1 | 154 | 1 | 1/3 | – | 6 | 1 | –/off |
| `sp_giovanni_2a` | `santamonica` | 2 | 1 | 1/1 | 275 | 0 | – | – | – | – | –/– |
| `sp_giovanni_2b` | `santamonica` | 2 | 1 | 1/1 | 276 | 0 | – | – | – | – | –/– |
| `sp_giovanni_3` | `warehouse` | 0 | 0 | 0/0 | 55 | 0 | – | – | – | – | –/– |
| `sp_giovanni_4` | `warehouse` | 0 | 0 | 0/0 | 372 | 0 | – | – | – | – | –/– |
| `sp_giovanni_5` | `santamonica` | 10 | 0 | 0/0 | 61 | 0 | – | – | – | – | –/– |
| `sp_masquerade_1` | `santamonica` | 0 | 0 | 0/0 | 16 | 0 | – | – | – | – | –/– |
| `sp_ninesintro` | `pier` | 372 | 1 | 1/1 | 33 | 1 | 1/5 | – | 4 | – | –/off |
| `sp_observatory_1` | `pier` | 473 | 1 | 1/1 | 73 | 1 | 1/5 | 1 | 8 | 1 | –/off |
| `sp_observatory_2` | `pier` | 540 | 2 | 2/2 | 99 | 1 | 1/9 | 2 | 9 | 2 | –/off |
| `sp_soc_1` | `pier` | 250 | 1 | 1/1 | 47 | 1 | 2/3 | – | 15 | – | off/off |
| `sp_soc_2` | `santamonica` | 436 | 2 | 1/1 | 138 | 1 | 2/3 | – | 2 | – | –/on |
| `sp_soc_3` | `santamonica` | 12 | 0 | 0/0 | 78 | 0 | – | – | – | – | off/– |
| `sp_soc_4` | `santamonica` | 0 | 0 | 0/0 | 55 | 0 | – | – | – | – | –/– |
| `sp_taxiride` | `pier` | 334 | 3 | 3/3 | 63 | 1 | 1/7 | – | 87 | 2 | on/off |
| `sp_theatre` | `santamonica` | 256 | 0 | 0/0 | 154 | 1 | 3/4 | 9 | 77 | 41 | –/on |
| `sp_tutorial_1` | `la` | 534 | 1 | 1/1 | 396 | 1 | 2/14 | 30 | 59 | 58 | –/off |

### How to re-measure

```
python tools/probe_sky_inventory.py             # the rollup, every map in the install
python tools/probe_sky_inventory.py --markdown  # + the four tables above
python tools/probe_sky_inventory.py sm_hub_1    # one map
```

It writes `tools/out/_sky/inventory.json` — every measured field per map, including the sky
area's prop model histogram and entity-class histogram that the tables above summarise. BSP
data only, so it re-runs after any install change without an export.

## K6 — what VRAD did with the sky pair (RE-A5, settled)

VtMB **ships no map compiler**. `Bin/` holds the engine, the renderers and the shader DLLs;
the whole install contains exactly two executables (`Vampire.exe`, `Loader.exe`) and no
`.fgd`, and the Unofficial Patch adds none. So unlike K1, K3/K5 and K8 there is no binary to
decompile and no fallback to one: VRAD is observable **only through its output**, and RE-A5 is
a data argument end to end. The instrument is `tools/probe_skyambient.py`.

### Whose bake is it? — read this before trusting any lump-8 measurement

The Unofficial Patch **recompiles 20 maps and adds 7 of its own**, and its compiler is a later
Source VRAD, not Troika's. On a recompiled map essentially nothing about the bake survives:
`sp_tutorial_1` goes from 7,580 faces / 1.07 MB of lump 8 / 203 worldlights (retail) to
15,610 / 8.53 MB / 396 (patch), and the per-style luxel-set count goes from **1** (flat) to
**4** (bumped) — the patch's VRAD writes normal-mapped lightmaps, which Troika's did only on
selected faces.

| | maps |
|---|---|
| **patch-recompiled** (20) | `ch_shrekhub`, `ch_zhaos_1`, `hw_609_1`\*, `hw_warrens_4`, `la_confession_1`, `la_hospital_1`, `la_malkavian_4`, `la_museum_1`\*, `la_skyline_1`, `la_ventruetower_1b`, `sm_apartment_1`, `sm_asylum_1`, `sm_basement_1`, `sm_beachhouse_1`, `sm_hub_2`, `sm_medical_1`, `sm_pier_1`, `sm_vamparena`, `sp_soc_2`\*, `sp_tutorial_1`\* |
| **patch-only** (7) | `hw_chateau_1`\*, `hw_warrens_2b`, `la_bradbury_1`\*, `la_library_1`\*, `la_malkavian_3b`\*, `sm_coffee_1`, `sm_smoke_1` |

(\* carries a sky pair.) 81 maps are byte-identical between the two trees. Of RE-A7's **25**
sky-pair maps, 8 are starred above, so **17 carry Troika's own bake** — and everything below
reads those 17 only. `probe_skyambient.py` refuses a non-retail bake unless asked;
`--provenance` prints the split.

Two corrections this forces:

- **`sp_tutorial_1` is the wrong map for this question.** It is patch-recompiled, and in
  **retail it carries no sky pair at all** — its `light_environment` is the patch's addition.
- RE-A7's inventory is patch-first, which is correct for "what does the engine load", but a
  sky-pair observation drawn from it can be the patch compiler's behaviour rather than
  Troika's. Its `sp_soc_2` note — two `light_environment`s but only one lump-15 pair — is one:
  **retail `sp_soc_2` emits both pairs.**

### The transfer function (exact, whole game)

Comparing every `light`/`light_spot`/`light_environment` keyvalue against the `dworldlight_t`
row at the same origin gives VRAD's photometric normalisation exactly. Over **16,378
origin-matched lights across all 108 maps there is not one exception**; worst relative error
2.25e-7, which is float32:

```
intensity = (colour/255)^2.2 · (brightness/255) · (constant_attn + 100·linear_attn + 10000·quadratic_attn)
```

Gamma **2.2** on the colour, **linear** on the brightness — so `_light "187 255 247 40"` is not
40/255 of that colour, it is 40/255 of its *gamma-expanded* value, and a dim channel falls away
much faster than the keyvalue reads. The third factor is the light's **own falloff denominator
evaluated at d = 100 units**; since the engine divides by that same denominator
(`Engine_WorldLightDistanceFalloff`, RE-A3), a compiled intensity is simply **that light's
radiance at 100 units (2.54 m)**, whatever its falloff order. The `10000` that has always sat
in a quadratic light's intensity is 100², not a brightness unit.

Lump 8 stores radiance **×255** (the RGBE mantissa is a 0–255 byte), so the whole chain closes:

```
stored luxel = 255 · intensity / (constant + linear·d + quadratic·d²)
```

and therefore a light of brightness `B` lands a stored luxel of `(colour/255)^2.2 · B` at 100
units — **the authored brightness comes back out of the bake**. The sky pair carries no falloff
at all (`attn` is 0,0,0), so its ceiling in stored-luxel units is exactly `(colour/255)^2.2 · B`:
for a white `_ambient "255 255 255 20"` that is literally **20**.

The ×255 is not assumed — the sun measures it, below.

### The sun is baked, at 255 × intensity, gated by a sky-visibility test

The sun is the half of the pair whose rule is not in doubt (trace toward it; on a sky hit add
`intensity·cos`), so it is what calibrates the scale and proves the bake reads the sky at all.
Dividing a sun-visible luxel by `cos` removes the geometry, so the population should pile up
against its ceiling — and, decisively, its **bright end should carry the sun's own
chromaticity**. That second signature is what no confound can fake: if "sky-visible luxels are
brighter" were merely the author's outdoor lamps, the bright end would be the colour of *those*.

`sp_endsequences_b` is the cleanest case — 1,363 sun-visible luxels, a sun that outshines
everything else on the map:

| | measured | predicted |
|---|---|---|
| baked/cos, p50 / p90 / p99 | 170.3 / 170.4 / 170.6 | **168.2** (= 255 · intensity) |
| bright-end chromaticity | (0.238, 0.286, 0.476) | sun (0.243, 0.291, 0.466), **d = 0.019** |
| — against the map's own fill | | fill (0.420, 0.365, 0.214), d = 0.523 |

A distribution whose 50th, 90th and 99th percentiles agree to three digits is a pure
directional term with a hard visibility gate, and it sits on its predicted ceiling at **×1.01**.

Over all 17 retail maps the colour test cleanly partitions the set. **Exactly three maps have a
sun that outshines their own fill** — the three brightest, ceilings 203/200/168 — and all three
land on the prediction:

| map | sun ceiling | measured p90 | ×pred | d(sun) | d(fill) |
|---|---|---|---|---|---|
| `hw_chinese_1` | 203.0 | 220.9 | **1.09** | 0.027 | 0.064 |
| `ch_fishmarket_1` | 200.0 | 174.1 | **0.87** | 0.022 | 0.164 |
| `sp_endsequences_b` | 168.2 | 170.4 | **1.01** | 0.019 | 0.523 |

**Median ×1.01.** On every other map the estimator runs away (up to ×26.7 on `hw_jewelry_1`,
whose sun ceiling is 1.3) — and on every one of those the bright-end colour matches the **fill**
instead, `sp_ninesintro` most starkly at d = 0.927 to its sun against **0.008** to its fill. The
diagnostic fails exactly where it says it is failing, which is what makes the three fits above
worth believing.

Under the alternative reading (no ×255) the same numbers come out at 172–1506× predicted. The
scale is 255.

### The magnitude: the sky pair is a first-class term, not a tint

With the scale settled, the sun's ceiling (normal incidence, sky in view) and the skyambient's
(a fully open hemisphere) can be quoted straight against each map's own lump-8 distribution.
Everything here is in stored-luxel units:

| map | env | sun | amb | lump 8 p50 | p90 | max | sun/p50 | amb/p50 |
|---|---|---|---|---|---|---|---|---|
| `ch_fishmarket_1` | 1 | 200.0 | 20.0 | 9.14 | 40.5 | 314 | 2189% | 219% |
| `ch_temple_1` | 3 | 35.6 | 13.5 | 3.01 | 71.9 | 684 | 1184% | 449% |
| `hw_cemetery_1` | 1 | **0.0** | 5.2 | 2.92 | 66.1 | 10400 | 0% | 177% |
| `hw_chinese_1` | 1 | 203.0 | **0.0** | 5.33 | 92.5 | 366 | 3811% | 0% |
| `hw_jewelry_1` | 1 | 1.3 | 1.5 | 4.06 | 79.8 | 6796 | 33% | 37% |
| `hw_luckystar_1` | 1 | 5.7 | 1.0 | 7.91 | 124.8 | 426 | 73% | 13% |
| `la_dane_1` | 1 | 8.4 | 8.4 | 1.31 | 24.4 | 352 | 640% | 640% |
| `sm_warehouse_1` | 4 | 6.4 | 9.4 | 6.57 | 83.9 | 1776 | 97% | 143% |
| `sp_endsequences_b` | 1 | 168.2 | 1.7 | 0.00 | 29.4 | 388 | — | — |
| `sp_giovanni_1` | 1 | 23.8 | 9.3 | 8.87 | 68.7 | 853 | 268% | 105% |
| `sp_giovanni_2a` | 1 | 22.0 | 10.0 | 9.26 | 78.5 | 1057 | 237% | 108% |
| `sp_giovanni_2b` | 1 | 22.0 | 10.0 | 8.81 | 76.9 | 1057 | 249% | 114% |
| `sp_ninesintro` | 1 | 13.1 | 6.2 | 2.82 | 52.3 | 1002 | 464% | 218% |
| `sp_observatory_1` | 1 | 39.1 | **0.0** | 2.38 | 75.2 | 455 | 1641% | 0% |
| `sp_observatory_2` | 2 | 39.1 | 20.0 | 4.97 | 82.2 | 781 | 788% | 403% |
| `sp_soc_1` | 1 | 23.0 | 5.1 | 5.80 | 62.7 | 380 | 396% | 88% |
| `sp_taxiride` | 3 | 30.8 | 25.0 | 19.44 | 77.8 | 1204 | 159% | 129% |

(`sp_endsequences_b` bakes more than half its lit faces to zero, so its p50 is 0 and the ratio
columns are undefined; it is excluded from the roll-up.)

**The sun's ceiling is a median 332% of its map's median lit face (max 3811%); the skyambient's
is 121% (max 640%).** So where the sky is visible the pair is not a tint — it is comparable to
or brighter than ordinary surface lighting, and on the bright-sun maps it is the brightest thing
in the level. What keeps it from dominating the *look* is not its magnitude but its reach: VtMB's
maps are overwhelmingly enclosed, so only a small minority of luxels ever see sky. Two maps
author the skyambient to exactly zero and one authors the sun to zero, so a zero is a real
authored value, not a missing reading.

This is the number C1 carries — nothing like the flat `SKYLIGHT_INTENSITY = 1.0` it replaced.

### Multiple `light_environment`s: the ambient is global, the sun is per-entity

`sp_observatory_2` is the one map whose two `light_environment`s carry different values, and it
answers the bake-time half of D6 outright:

| entity (lump order) | `_light` | `_ambient` | its lump-15 type-3 | its lump-15 type-5 |
|---|---|---|---|---|
| `193.68 -12.25 739` | `188 211 254 80` | `255 255 255 20` | 0.160439 — its own | 0.078431 — its own |
| `640 -12.25 -3968` | `188 211 254 60` | `255 255 255 10` | 0.120329 — its own | **0.078431 — the *other* entity's** |

Each entity emits a type-3 carrying **its own** `_light`, but **both** type-5 rows carry
`20/255` — the `_ambient` of the entity that comes **first in the entity lump**. The second
entity's authored `10` appears nowhere in the BSP. So VRAD resolves the sky ambient **once,
globally, first-entity-wins**, and stamps that one value onto every type-5 row it writes, while
the sun stays per-entity. That is the answer to RE-A7's open observation that "VRAD did
something to the skyambient that the entity keys alone do not explain".

Note the ordering: lump-15 order is *reversed* relative to the entity lump here (the first
type-5 row belongs to the second entity), so "first" means first in the entity lump, not first
in lump 15.

Two consequences:

- **D6's ambient half is moot.** Every type-5 row in a map carries the same value by
  construction, so first-wins, last-wins and "pick any" are the same choice — including on the
  four maps whose `light_environment`s are byte-identical. C0(a) is still the right fix (the
  engine's `FindAmbientLight` takes the first row), it just cannot change a number.
- **D6's sun half is one map wide** (RE-A7), and the runtime is settled — first-wins, RE-A3.
  Whether VRAD *summed* the two suns into the bake is answered by the sun fit above wherever a
  multi-`light_environment` map is bright enough to measure: a summed bake reads N× its
  single-pair ceiling.

### The skyambient's weighting: present, but its aperture is not pinned

The same two signatures applied to the skyambient — fit baked RGB against measured sky
visibility over sun-shadowed, fill-free luxels, then read the slope's magnitude against
`255 · intensity` and its colour against `_ambient` vs the fill.

**The colour signature is positive.** On the maps where `_ambient` and the fill are different
hues the fitted slope lands on `_ambient`: `hw_cemetery_1`'s slope is (−0.05, 0.19, 0.86)
against an `_ambient` of (0.06, 0.26, 0.68) and a **warm** fill of (0.46, 0.36, 0.18) — a
distance of 0.36 to the ambient against 1.36 to the fill. Warm lamps bouncing off neutral stone
do not produce a blue slope; the skyambient is in the bake.

**The magnitude signature is not conclusive.** Six maps put the fitted slope near `_ambient`
(distance < 0.5, and nearer to it than to their fill); their ratios against the
full-hemisphere prediction are `ch_fishmarket_1` ×0.91, `sp_soc_1` ×0.46, `hw_cemetery_1` ×0.34,
`sp_taxiride` ×0.33, `sp_observatory_2` ×0.04 and `sp_endsequences_b` ×7.34 — **median ×0.40**,
spread over two orders of magnitude. The fit is reading a term of order 1–25 luxel units against
a bake whose fill is spatially correlated with the very thing being regressed on (open to the
sky = near the author's outdoor lamps), and our sky-visibility fraction is measured against
world brushes only — displacements, static props and brush entities do not occlude in the
tracer, so `f` over-estimates what VRAD saw and biases every ratio **downward**. A median of
0.34 against a systematic downward bias is consistent with a full-hemisphere gather and with a
narrower one alike; it does not choose between them.

So: **the skyambient is baked, hemispherically, with an effective aperture we can bound but not
pin.** Distinguishing a cosine-weighted gather from a uniform one is the whole residue, and it
is a factor of ~2 on a term whose *ceiling* we already know exactly from the transfer. Settling
it would need a compiler binary (does not exist) or a controlled recompile against Troika's own
VRAD (we have none — the patch's is demonstrably a different compiler). **K6 closes with the
transfer, the ×255 storage scale, the sun's rule and magnitude, the authored ceilings and the
multi-entity rule all exact; only how the skyambient weights its hemisphere is left bounded.**

### What this changes for us

- **C1 gets a real number and stops being a stub.** The type-5 magnitude converts to stored-luxel
  units by ×255, giving a per-map ambient ceiling of 0–25 (median **8.4**) against median lit
  faces of 0–19.4. That is a *substantial* IBL term where sky is visible, not the near-zero a
  flat `SKYLIGHT_INTENSITY = 1.0` implies, and not a tint to be waved through. RE-A3 still fixes
  its semantics (an IBL/bounce term, never a lamp), and RE-A7 the range and the honoured zeros.
- **C2 loses its magnitude argument and keeps its absence argument.** "Sky contributes no
  meaningful light" is **not** true where sky is visible — it contributes plenty. What remains
  true is that 83 of 108 maps carry no sky pair at all (RE-A7), and that VtMB's maps are mostly
  enclosed, so the *reach* is small. The policy has to be driven by the pair's presence and by
  actual sky visibility, not by a blanket "sky is negligible".
- **C0's fourth item** (landed as C0d): `dworldlight_t.intensity` is radiance ÷ the light's own
  falloff denominator at 100 units, so any fit against it — C4's especially — must multiply the
  denominator back (and ×255 for lump-8 units, gamma 2.2 to return to keyvalues).
  `read_worldlights` returns `falloff` beside the intensity and `probe_light_calibration.py`
  applies it; C0(b)'s loader fixups rewrite the same attenuations, which is why the two landed
  together.
- **C4 gets a calibration anchor it did not have.** `probe_light_calibration.py` fits a
  direct-light model against lump 8 with a free scale; the scale is now known
  (`255 · intensity / falloff`), so the fit has one fewer degree of freedom and its residual
  becomes a real measurement of the bounce term rather than a scale/bounce mixture.
- **`docs/light-attribution.md` gets a cross-check for free.** The transfer law converts any
  worldlight back to `(colour, brightness)` as the level designer typed it. Fill and fixture may
  separate more cleanly in authored space — a round `_light "255 255 255 20"` reads differently
  from a hand-tuned `_light "173 142 96 37"` — than in compiled intensity.
- **Any future lump-8 work must check provenance first.** The 27-map split above is not a
  footnote: a quarter of the game's bakes are a different compiler's.

## K7 — the sky's brightness chain (RE-A9, settled)

The chain is four operations and three of them are the identity. **A sky pixel in VtMB's
framebuffer is the decoded texel, unscaled and unfogged.** The `Brightness 4` constant in our
runtime has nothing behind it.

### The premise correction: there is no `SkyBox` shader

Every one of the **79** `materials/skybox/*.vmt` in the install is **`UnlitGeneric`**, and the
only parameters any of them use are `$basetexture` (79) and `$nofog` (67). No `$color`, no
`$alpha`, no per-set brightness, no sky-specific shader anywhere. The 12 faces without `$nofog`
are the `black*` and `mm_skybox*` sets, which no map's `skyname` selects — all 108 skynames
resolve to the 11 sets in the inventory above, and **all 66 of those faces carry `$nofog 1`**.

Dispatch, read out of `stdshader_dx8.dll`: the shader registered as `UnlitGeneric` is a pure
alias — its `GetFallbackShader` (vtable slot 1, `0x1000fa00`) returns the constant string
`"UnlitGeneric_DX8"` unconditionally, and its init/draw slots are empty stubs (`0x10001330`,
`0x100125c0`). `stdshader_dx9.dll` implements **no** shaders at all (it holds fallback-name
tables and not one `Help for …` string), so the dx8 build is the top of the chain;
`stdshader_dx6.dll` carries the `UnlitGeneric_DX6` floor.

### The chain, end to end

1. **The engine forces the modulation white.** `R_DrawSkyBox` is called from `0x2008158b` as
   `R_DrawSkyBox(dist, 0x3f, 1.0f, 1.0f, 1.0f)` — mask `0x3f` is all six faces, `dist` comes
   from an interface call, and the three trailing floats are pushed straight into the bound sky
   material's vtable `+0x78` (an `IMaterial` method taking three floats — colour modulation by
   signature and by what the shader then reads) immediately before `materials->Bind` at
   `+0xd4`. So `$color` is set to (1,1,1) *explicitly at draw time*, not merely left at its
   default.
2. **The shader's modulation constant is white.** `UnlitGeneric_DX8::OnInitShaderParams`
   (`0x100101a0`) defaults `$color` to (1,1,1) and `$alpha` to 1.0 when the material sets
   neither — which no sky VMT does. The dynamic pass calls `SetModulationDynamicState`
   (`0x10001ca0`): it starts from `{1,1,1,1}`, folds in `ComputeModulationColor` (`0x10016a70`
   — `$color` as a vector or a broadcast float, then `$alpha` clamped to `[0,1]` by
   `0x10016890`), and uploads the result to **vertex-shader constant c38**. Nothing else is
   folded in: there is no engine-side colour-modulation term in this build's
   `ComputeModulationColor`.
3. **The vertex shader copies that constant to the diffuse register.** Every one of the **8**
   combos in `shaders/vsh/unlitgeneric.vcs` ends `MOV oD0, c38` (dst token `d00f0000`), and
   that token is the *only* `oD0` write anywhere in the file.
4. **The pixel shader multiplies once.** VtMB ships its shader assembly as readable data —
   `materials/dxshaders/unlitgeneric.psh` is, in full:

   ```
   ps.1.1
   tex t0	; base color
   mul r0, t0, v0
   ```

   and the compiled `shaders/psh/unlitgeneric.vcs` is exactly that: one combo, `TEX` + `MUL` +
   END, 60 bytes.

`v0` is the interpolated `oD0`, so the pixel is `texel × (1,1,1,1)`. **No overbright, no ×2,
no gamma operation, no clamp beyond the framebuffer's own.**

### The one asymmetry: the world gets ×2 and the sky does not

The shader 612 of the 626 distinct world materials on `sp_tutorial_1` + `sm_hub_1` use is
`LightmappedGeneric`, and `materials/dxshaders/lightmappedgeneric.psh` carries Valve's own
comment:

```
tex t0
tex t1
mul r0, t0, v0			; base times vertex color (with alpha)
mul r0.rgb, t1, r0		; fold in lightmap (color only)
mul_x2 r0.rgb, c0, r0   ; * 2 * (overbrightFactor/2)
```

and the overbright factor is **pinned to 2**. `UpdateMaterialSystemConfig` (`0x200718d0`) reads
`mat_overbright` into the config, and any value that is not exactly `1.0` or `2.0` — and any
hardware reporting no overbright support — is rewritten by `mat_overbright.SetValue(2.0f)`.

So the framebuffer relationship is exact and needs no capture to state:

> **sky = texel.  world = albedo × lightmap × 2.**

There is no separate sky exposure to discover. What B4 has to calibrate is not "how bright was
VtMB's sky" — that is 1:1 — but how to land an Unreal sky texel at the same *displayed* value
relative to a world that Lumen and the tonemapper reconstruct rather than multiply.

### Gamma is frame-wide, never sky-specific

`UpdateMaterialSystemConfig` builds `MaterialSystem_Config_t` at `0x20a6bc80`: `+0x00` `gamma`
(2.2), `+0x04` `texgamma` (2.2), `+0x08` overbright (2.0 per above), `+0x0d`
`linearFrameBuffer` (0), then `mat_polyoffset`, `mat_picmip` and the rest. It ends by calling
`IMaterialSystem` vtable `+0x4c` with `1.6 − clamp(cl_v_gamma − 1, 0, 3) × 0.5` — the video
options' gamma slider, default `cl_v_gamma` 1.5, so **1.35**. The same four convars are copied
into `StudioRenderConfig_t` at `0x20d63dc0` by `0x200a4500` (`+0x18` gamma, `+0x1c` texgamma,
`+0x20` brightness, `+0x24` overbright, the last 1.0 unless the hardware reports support) for
model rendering.

None of it is per-material. Sky, world and models all land in the same 8-bit gamma-space
framebuffer and the ramp is a display LUT applied at present (`shaderapidx9.dll` tracks
`m_HasSetDeviceGammaRamp` as a hardware cap). This corroborates `docs/color_gamma.md` from a
second direction and adds the pinning; the registration static-inits there are unchanged
(`gamma`/`texgamma` `0x213064c0`/`0x21306618`, `brightness` `0x21306510`, `linearFrameBuffer`
`0x21306558`, all built at `0x2010e290`).

### Fog: the 2D sky is exempt, game-wide

`$nofog` is material-var-flag **bit 14**. The flag names sit as a reverse-ordered string table
in `MaterialSystem.dll` `.data` — `$debug` at `0x1004d9e0` is bit 0, running back to
`$opaquetexture` at `0x1004d888` — and the indexing is confirmed five independent ways by the
shader code that tests it: bit 11 `$model` (`0x10010480`), bit 15 `$ignorez` → depth off, bit
16 `$decal` → polyoffset, bit 13 `$nocull` → culling off (all three in `SetInitialShadowState`,
`0x10016500`), bit 5 `$vertexalpha` / bit 8 `$alphatest` in the blend setup (`0x10016b50`), and
bit 20 `$basealphaenvmapmask` selecting the `UnlitGeneric_BaseAlphaMaskedEnvMap` program
(`0x10001e10`).

`CBaseShader`'s fog helpers are five near-identical functions — `0x10016ca0`, `0x10016cd0`,
`0x10016d00`, `0x10016d30`, `0x10016d60` — each calling `IShaderShadow::FogMode` (vtable
`+0xb4`) with 1 / 6 / 2 / 3 / 4, **unless `flags & 0x4000`, in which case it passes 0**.
`0x10016d90` is the unconditional `FogMode(0)`. `UnlitGeneric_DX8`'s draw ends in `0x10016db0`,
which picks mode 2 when `$additive` (bit 7) is set and mode 4 otherwise — and both routes go
through the `$nofog` test. (Only mode 0 = disabled is named by the binary; the other numbers
are not.)

With `$nofog 1` on every shipped sky face, the backdrop is drawn with fog mode 0. **The
`sky_camera`'s fog never blends over the 2D sky, at `fogend` or anywhere else** — the blend the
task went looking for does not exist. The miniature is the opposite case: it draws through the
ordinary material path with `Enable3dSkyboxFog` pushed (RE-A8), so it *is* fogged, by the
`sky_camera`'s own set. **Fog in VtMB's sky view is entirely a property of the miniature and
never of the backdrop** — which is a constraint on B8, not just a fidelity note.

### Where it lives

| Address | Binary | What |
|---|---|---|
| `0x1001ea98` / `0x1001eae0` | `stdshader_dx8` | the 17-slot `IShader` vtables for `UnlitGeneric` and `UnlitGeneric_DX8`; slots 14/15/16 are `OnInitShaderParams` / `OnInitShaderInstance` / `OnDrawElements`, reached through `CBaseShader`'s wrappers at slots 3/4/5 (`0x100163f0`, `0x10016420`, `0x10016470`) |
| `0x1000fa00` | `stdshader_dx8` | `UnlitGeneric::GetFallbackShader` → the literal `"UnlitGeneric_DX8"` |
| `0x100101a0` | `stdshader_dx8` | `UnlitGeneric_DX8::OnInitShaderParams` — the `$color` (1,1,1) / `$alpha` 1.0 defaults |
| `0x10010480` → `0x10001e60` | `stdshader_dx8` | `OnDrawElements` and the shared unlit draw: bind base texture (`0x100166e0`), texture transform (`0x10001910`), modulation, fog, draw |
| `0x10001ca0` / `0x10016a70` / `0x10016890` | `stdshader_dx8` | `SetModulationDynamicState` → c38, `ComputeModulationColor`, `ComputeAlpha` |
| `0x10016500` | `stdshader_dx8` | `SetInitialShadowState` — the flag-bit cross-check |
| `0x10016ca0`–`0x10016d90`, `0x10016db0` | `stdshader_dx8` | the `FogMode` helpers and the `$nofog` (bit 14) gate |
| `0x200880c0`, caller `0x20081560` | `engine.dll` | `R_DrawSkyBox` and the `(dist, 0x3f, 1,1,1)` call site |
| `0x200718d0` → `0x20a6bc80` | `engine.dll` | `UpdateMaterialSystemConfig` and the config struct; overbright pinned to {1, 2} |
| `0x200a4500` → `0x20d63dc0` | `engine.dll` | the `StudioRenderConfig_t` copy of the same four values |
| `0x1004d888`–`0x1004d9e0` | `MaterialSystem` | the reverse-ordered material-var flag name table |
| `materials/dxshaders/*.psh`, `shaders/{vsh,psh}/*.vcs` | shipped data | the shader assembly and its compiled form — **not in a binary at all** |

### What this changes for us

- **B4's unknown is gone, and its character changes.** There is no VtMB-side sky exposure to
  measure: the transfer is 1:1 and fog-free. `Brightness 4` is therefore a **divergence**
  (D7), not an uncalibrated constant, and its faithful value is whatever makes the Unreal sky
  texel display at parity with the same texel through VtMB's `texel → 8-bit gamma-space
  framebuffer` path. RE-A6's captures (roadmap **RE17**) are still wanted, but for the *world* half of the ratio
  (what Lumen + the tonemapper do to a lit surface), not for the sky.
- **B8 gains a hard rule.** The backdrop is never fogged; the miniature always is, from
  `sky_camera`. A single fog volume covering both reproduces neither.
- **B5 / `docs/asset-enhancement.md` inherit a constraint.** Because the transfer is identity, an
  upscaled sky face must preserve *absolute* texel values, not just structure — a super-resolver
  that shifts the mean shifts the sky's brightness one-for-one.
- **`docs/color_gamma.md` gets a stronger source.** Its `base × lightmap × 2` reading was
  inferred from technique names and the ConVar default; the shipped `lightmappedgeneric.psh`
  states it in Valve's own words, and `0x200718d0` shows the factor is pinned rather than
  merely defaulted.

### Loose ends (seen, deliberately not chased)

- **Whether `snapshot` captures pre- or post-ramp.** The gamma ramp is a device LUT, so a
  back-buffer grab would not include it — which would make RE-A6 captures and our `shots.bat`
  PNGs comparable to each other but not to what a player's monitor showed. The engine's
  screenshot path was not read; confirm before any RE-A6 (RE17) capture is used *quantitatively*.
- **`IMaterialSystem` vtable `+0x4c`** takes the `1.6 − …` gamma value; the slot was not named
  from `MaterialSystem.dll`'s own vtable. The formula and its inputs are read directly.
- **The lightmap luxel → 8-bit page encoding** (where `texgamma` is actually spent, and how
  lump-8 RGBE lands in the range the `mul_x2` restores) is *not* part of the sky chain and was
  left alone. It is the world-side term C4 needs, not B4.
- **`dist`** in `R_DrawSkyBox` comes from `[0x201a11f0]`'s vtable `+0x48`; the value was not
  chased, because with fog disabled the backdrop's distance has no effect on its colour.

## The unknowns (K1–K8) — all settled

Each K was an unknown this doc tracked to closure; the sections above are the full
write-ups. Nothing downstream of them rests on an assumption any more.

| K | Question | Settled by | One line |
|---|---|---|---|
| K1 | Source's sky-face orientation | RE-A1 + RE-A2 + the seam probe | six flat quads; no face rotated or mirrored; `ft`/`bk` are ring names (−Y/+Y) |
| K2 | Unreal's cubemap convention | UE 5.8 source ×4 + Epic's authoring doc | the D3D face table on the raw Z-up world vector — four of six slices stored rotated |
| K3 | how VtMB lights models at runtime | RE-A3 | a 162-ray ambient cube off `avgLightColor` × reflectivity, plus ≤ `r_worldlights` (2) direct worldlights |
| K4 | day/night bake selection | RE-A4 | premise false — one bake keyed by `styles[8]`; `day[8]`/`night[8]` unwritten, no reader |
| K5 | lump 15's runtime role | RE-A3 | the model light cache only; world surfaces render from lump 8 alone |
| K6 | VRAD's sky-pair semantics | RE-A5 | the transfer and lump 8's absolute scale are exact; only the skyambient's hemisphere aperture stays bounded (~2×) |
| K7 | the sky's brightness chain | RE-A9 | the identity — a sky pixel is the decoded texel, unfogged; world = albedo × lightmap × 2 |
| K8 | the full-game inventory | RE-A7 | 11 sky sets; 66 maps draw sky, 25 are lit by it, 43 run the miniature, all at scale 16 |

## Ground truth and instruments

The rework is calibrated against data we already hold plus the original game:

- **Lump 8** — the authored ambience, and the only bake there is (K4); decoded by
  `tools/lightmap.py`, and `probe_light_calibration.py` already fits against it.
- **The user's VtMB install, running** — reference screenshots at known map/camera positions
  (`elysium.campos` vantages have Source-space equivalents via the inverse transform). Loose
  files shadow the VPKs, so a labelled skybox drops in without repacking (RE-A2).
- **Our headless harnesses** — `shots.bat` (same vantages, PNG + manifest), `probe.bat`,
  `elysium.lightprobe`, the MCP screenshot/teleport tools for scripted A/Bs.
- **`tools/probe_sky_orientation.py`** — scores the decoded faces against a predicted cube
  assembly (ring order × mirrors, poles × the eight dihedral transforms) by seam error,
  reporting the margin so an uninformative face reads as a tie rather than a match.
- **`tools/sky_probe.py`** + **`tools/tex_from_png.py`** — the labelled-sky set and the
  `.tth`/`.ttz` writer that gets it into the original game (RE-A2 above). The same faces feed
  B1, so the two ends of the orientation chain are checked with one instrument.
- **`tools/probe_sky_inventory.py`** — the K8 instrument (RE-A7): per map, the `skyname` and
  whether its faces resolve, the `light_environment` rows, the worldlight type histogram, the
  `toolsskybox` face count, both fog sets, and the RE-A8 area split (sky area, its faces, and
  the props/entities/worldlights inside it). Prints the rollup, `--markdown` prints the doc
  tables, and it writes `tools/out/_sky/inventory.json`.
- **`tools/probe_daynight.py`** — the K4 instrument (RE-A4): over every map in the install, the
  byte histogram of `styles`/`day`/`night`, the per-face closure of lump 8 against a single
  bake, the complete worldspawn key inventory, and a day/night regex sweep of every entity key
  and value. Re-run it after any install change to confirm the arrays are still dead.
- **`tools/probe_skyambient.py`** — the K6 instrument (RE-A5), in four modes.
  `--provenance` splits the 108 maps into Troika's bake and the patch's; `--inventory` is the
  tracing-free half (the transfer law checked per map, and the sky pair's ceiling against that
  map's own lump-8 percentiles); the default per-map run adds the tracing half — luxel world
  positions rebuilt from `lightmapVecs` + plane, sky visibility by a vectorised brush trace
  against `SURF_SKY`, and the sun as the control on the whole method; `--all` sweeps every
  retail sky-pair map and prints the cross-map roll-up. Writes
  `tools/out/_skyambient/<map>.json`. It refuses a patch-recompiled bake unless asked, which is
  the guard the rest of this section depends on.
- **The shipped shader assembly** — `materials/dxshaders/*.psh` (readable ps.1.1 source, Valve's
  own comments intact) and `shaders/{vsh,psh}/*.vcs` (the compiled combos). This is the ground
  truth for what any material does to colour, and it is a file read rather than a decompile —
  RE-A9's headline came out of `unlitgeneric.psh` and `lightmappedgeneric.psh` in two lines each.
  Resolve through `install.build_index`, patch-first like everything else.
- **UE 5.8's own source** — the ground truth for our side of every seam, and the only one that
  needs no probe. K2 came out of five agreeing statements of one table
  (`ReflectionEnvironmentShaders.usf`, `ReflectionEnvironmentCapture.cpp`,
  `TextureCompressorModule.cpp`, `Common.ush`, plus Epic's authoring page); a convention we
  currently *assume* about Unreal is nearly always written down in there.
- **Ghidra** — `tools/ghidra/run.ps1`; targets `engine.dll` (sky draw, fog, day/night),
  `stdshader_dx8.dll` + `MaterialSystem.dll` (the material/shader state around those programs),
  `StudioRender.dll` (model ambient). Findings cited into this doc per `docs/CLAUDE.md`.
  **Not available for VRAD:** the game ships no compiler (K6), so the bake is data-only.

## Status, history, and what remains

This doc is the **facts** reference; status and next work, and the D1–D7 decision set (D6
dissolved, D4 amended, D3 corrected), are cited at the top of this doc — the tracker set owns
everything else.

The one deliberately *bounded* fact: the skyambient's hemisphere aperture (cosine vs
uniform, a factor of ~2) is **not identifiable from the shipped data** — on an enclosed
map the faces that see sky are also the faces farthest from the author's fill, so sky
visibility carries the fill's sign (C4's regression lands at a physically impossible
negative on `ch_temple_1`). Closing it would need a fill-subtracted target or a compiler
binary that does not exist; every consumer carries the exact authored ceiling instead.
