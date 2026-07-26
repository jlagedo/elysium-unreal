# Color & gamma

> **Reference — VtMB facts + Godot prototype.** The VtMB analysis (DX8/LDR gamma-space render,
> `mat_overbright`, the ConVar defaults) is engine-neutral and load-bearing. The "What the
> pipeline does now" / "Matching…" sections describe the read-only Godot prototype at
> `E:\dev\elysium` (`shaded.gdshader`, `GameScene`, SDFGI, Godot tonemap) — porting reference,
> not Elysium-Unreal's current state (only **M0** is built; see `docs/rebuild-strategy.md`). Bare
> `CLAUDE.md`/`docs/archive/…` paths are in the Godot repo.

How VtMB (2004, DX8-era Source fork) produces its final on-screen color, why the
Elysium viewer looked too bright / washed-out / high-contrast by comparison, and
what the pipeline does to match. **Every VtMB constant here is read from the game's
own binaries** (`Bin/engine.dll`, `Bin/stdshader_dx8.dll`, `Bin/MaterialSystem.dll`,
`Bin/shaderapidx9.dll`) via the Ghidra workspace (`tools/ghidra/`), not from the
public Source SDK — Troika forked early Source and the SDK post-dates it by ~10
years, so the SDK is a cross-reference only.

## What VtMB actually does

VtMB renders on the **DX8 fixed-function / ps.1.1 path, strictly LDR, entirely in
gamma space**, with **no post-processing color work of any kind**.

### No post-processing (proven from the binaries)

A string scan of `engine.dll`, `MaterialSystem.dll`, and `shaderapidx9.dll` returns
**zero** matches for `bloom`, `mat_hdr`, `hdr_level`, `tonemap`, `colorcorrection`,
`postprocess`, `autoexposure`. The whole subsystem does not exist in this build.
Bloom / HDR / tonemapping / color-correction arrived later (Lost Coast / HL2:Ep1)
and in the SDK are hard-gated behind `mat_dxlevel >= 80` (HDR) / `>= 90`
(color-correction) — VtMB is a DX8 title, so even later engines leave that path off.

The only "color post" VtMB has is the **hardware gamma ramp** on the final 8-bit
framebuffer (the video-card gamma LUT), a single power curve driven by the gamma /
brightness sliders. No S-curve, no highlight rolloff, no glow.

### The lightmapped combine: `base × lightmap × 2`, in gamma space

VtMB draws world surfaces with the DX8 `LightmappedGeneric` techniques, enumerated in
`stdshader_dx8.dll`: `LightmappedGeneric_DX8`, `LightmappedGeneric_NoBump_DX8`,
`LightmappedGeneric_BumpmappedLightmap`, plus an `..._Overbright2` family
(`VertexLitTexture_Overbright2`, `Eyes_Overbright2`, …). Setup functions:
`FUN_10002180` (BaseTexture), `FUN_10002210` (BumpmappedLightmap), `FUN_10006f50`
(technique dispatch).

The combine is a **modulate-2×** (overbright): `finalColor = baseTexture × lightmap
× 2`. The `× 2` is the `mat_overbright` factor, and VtMB states it in its own words:
the game ships its DX8 shader assembly as **data**, so
`materials/dxshaders/lightmappedgeneric.psh` reads
`mul r0, t0, v0` / `mul r0.rgb, t1, r0` / `mul_x2 r0.rgb, c0, r0   ; * 2 *
(overbrightFactor/2)` — base × modulation, fold in the lightmap, ×2. The factor is
**pinned**, not merely defaulted: `UpdateMaterialSystemConfig` (`engine.dll`
`0x200718d0`) accepts only `1.0` or `2.0` from `mat_overbright` and rewrites anything
else — and any hardware reporting no overbright support — back to `2.0`. The
unlit path has no such term: `unlitgeneric.psh` is `tex t0; mul r0, t0, v0`, one
multiply by the material's `$color`×`$alpha`, which is why VtMB's sky backdrop reaches
the framebuffer at exactly its texel value (`docs/sky-ambience.md` → "K7"). On DX8 there is **no sRGB texture
sampler** (that is a DX9 sampler state), so the base texture is sampled **raw /
gamma-encoded**, multiplied by the gamma-encoded lightmap texel, ×2, **clamped at
255**, and written to a **gamma-space framebuffer**. The entire diffuse combine
happens in gamma space; the gamma ramp is applied only at present. That
gamma-space-multiply-then-hard-clip is what gives the game its grounded, "real" look —
there is no tonemapper reshaping the midtones and no bloom smearing the highlights.

### The ConVar defaults (read from `engine.dll`)

Each value is the default-value string passed at the ConVar's registration
static-initializer:

| ConVar | default | reg. static-init | meaning |
|---|---|---|---|
| `mat_overbright` | **`2`** | `0x2007b2c0` | lightmap combine multiplier (`base × lm × 2`) |
| `texgamma` | **`2.2`** | `0x2010e310` | textures on disk are gamma-2.2 encoded |
| `gamma` | **`2.2`** | `0x2010e290` | target display gamma |
| `cl_v_gamma` | **`1.5`** | `0x20071890` | user gamma slider (video options) |

`mat_overbright = 2` is corroborated three independent ways: the ConVar default, the
`_Overbright2` technique names, and VtMB's shipped `Vampire/cfg/default.cfg`
(`mat_overbright "2"`). The user's `config.cfg` (saved video settings) additionally
carries `cl_v_gamma "1.5"` and `brightness "1.250000"` — these vary the final gamma
ramp per user and are **not** part of the scene render.

SDK cross-reference (`gameui/OptionsSubVideo.cpp`): the `mat_monitorgamma` slider
ranges 1.6–2.6; HDR/color-correction combos are gated on `mat_dxlevel.GetInt() >= 80`
/ `>= 90`. Consistent with the DX8/LDR-only reading above.

## Matching a gamma-space engine from a linear-HDR one

Godot renders in linear HDR and encodes to sRGB on output; VtMB rendered in gamma
space and clamped at 255. The match is the **Linear tonemap** (no S-curve, so the
output encode is the only nonlinearity) plus the calibrated `LightRig` — no filmic
tonemapper, no `exposure` fudge, the authentic overbright reference being
`mat_overbright = 2`.

**A subtlety that is *not* a bug:** `shaded.gdshader` multiplies `albedo_linear × light`
in linear space, whereas VtMB multiplied in gamma space. For a pure product these are
equivalent — a gamma power curve commutes with a scalar multiply:
`lin→srgb(srgb→lin(base) · k) = base · k^(1/2.2)`, so a linear-space multiply followed
by the sRGB output encode lands at the same place as a gamma-space multiply, up to the
overall scale and the clip point. So the diffuse combine needs only a calibrated light
scale, not a gamma re-encode.

## What the pipeline does now

The world/props are lit in real time by the `LightRig` (one Godot light per
WORLDLIGHTS source) plus a flat `Environment` ambient fill and real-time **SDFGI** —
**not** baked lightmaps, and the shaders are **lit, not `unshaded`**.
`GameScene.MakeEnvironment` sets:

- **Tonemap = Linear** — no S-curve. Godot still applies the linear→sRGB output
  encode, which matches VtMB's gamma-space framebuffer clamp at 255 + gamma display.
  Texels the lights push above 1.0 hard-clip to white, as VtMB's overbright combine
  does.
- **SSAO on** — the real-time rig carries no ambient occlusion, so the flat ambient
  fill floods creases the baked lightmap used to keep dark; SSAO restores that
  contact shadowing. Not literally VtMB (which baked AO into lightmaps), but it
  compensates for the render path.
- **Glow on, `GlowHdrThreshold = 1.0`** — emissive neon/bulbs that clip past 1.0 get
  a subtle bloom. VtMB (LDR) had no bloom; this is a deliberate embellishment for the
  HDR path, not authenticity.
- **SDFGI on** — real-time indirect bounce, the dynamic stand-in for the discarded
  baked lightmaps' radiosity fill (`ELYSIUM_SDFGI*`; see `docs/lighting.md` and
  `sdfgi_probe.py`).
- **Ambient energy per map** — `ApplyEnvironment` sets the fill energy from the map's
  skyambient (type 5) term (`ELYSIUM_RIG_AMBIENT`, 0.30 default).
- **Per-map color-grade LUT** — an optional `.cube` 3D LUT
  (`Environment.AdjustmentColorCorrection`, built by `build_grade_lut.py` from a Source
  reference frame) pulls the real-time palette toward VtMB's; absent → no grade. An
  optional `film_grain.gdshader` overlay (`ELYSIUM_GRAIN` / the `grain` console command,
  off by default) is the final stylistic pass.

## Gaps

- **The lighting is a calibrated real-time approximation, not VtMB's radiosity.** The
  `LightRig` energy/attenuation scales and the SDFGI gains are matched by capture, and
  the palette gap is closed by the per-map color-grade LUT — none of it is derived from
  VtMB's exact per-luxel combine (`MaterialSystem.dll`'s RGBE→8-bit lightmap conversion
  was not decompiled, and there is no baked lightmap in the pipeline anyway). See
  `docs/lighting.md` for the rig calibration knobs and `build_grade_lut.py` for the grade.
- **The user gamma ramp is not reproduced.** `cl_v_gamma`/`brightness` are per-user
  video-slider values applied as a final LUT; Elysium targets the scene render, not a
  particular player's monitor calibration. The reference screenshot was taken at that
  user's `cl_v_gamma 1.5` / `brightness 1.25`.
