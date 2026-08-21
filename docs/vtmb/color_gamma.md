# Color & gamma

> **Reference — VtMB facts.** Engine-neutral and load-bearing. **Every constant here is read
> from the game's own binaries** (`Bin/engine.dll`, `Bin/stdshader_dx8.dll`,
> `Bin/MaterialSystem.dll`, `Bin/shaderapidx9.dll`) via the Ghidra workspace
> (`$ELYSIUM_WORK_ROOT/research/ghidra/`), not from the public Source SDK — Troika forked early Source and the SDK
> post-dates it by ~10 years, so the SDK is a cross-reference only.

How VtMB (2004, DX8-era Source fork) produces its final on-screen color.

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
× 2`. The `× 2` is the `mat_overbright` factor: the game ships its DX8 shader
assembly as **data**, so
`materials/dxshaders/lightmappedgeneric.psh` reads
`mul r0, t0, v0` / `mul r0.rgb, t1, r0` / `mul_x2 r0.rgb, c0, r0   ; * 2 *
(overbrightFactor/2)`. The factor is
**pinned**, not merely defaulted: `UpdateMaterialSystemConfig` (`engine.dll`
`0x200718d0`) accepts only `1.0` or `2.0` from `mat_overbright` and rewrites anything
else — and any hardware reporting no overbright support — back to `2.0`. The
unlit path has no such term: `unlitgeneric.psh` is `tex t0; mul r0, t0, v0`, one
multiply by the material's `$color`×`$alpha`, which is why VtMB's sky backdrop reaches
the framebuffer at exactly its texel value (`docs/vtmb/sky-ambience.md` → "K7"). On DX8 there is **no sRGB texture
sampler** (that is a DX9 sampler state), so the base texture is sampled **raw /
gamma-encoded**, multiplied by the gamma-encoded lightmap texel, ×2, **clamped at
255**, and written to a **gamma-space framebuffer**. The entire diffuse combine
happens in gamma space; the gamma ramp is applied only at present.

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
/ `>= 90`.

### Screenshot capture happens before the gamma ramp

`snapshot` (`TakeSnapshotTGA`/`TakeSnapshotJPEG`) does **not** see the gamma ramp described
above — this doc's "the gamma ramp is applied only at present" line (above) means a
`snapshot` file grab bypasses it entirely, because capture and the ramp are two separate
subsystems reading/writing two different surfaces:

- Capture reads rendered-surface memory: `GetBackBufferImage`/`GetFrontBufferImage` call
  `IDirect3DDevice::GetRenderTargetData`/`GetFrontBufferData` — a raw memory copy, no gamma
  math.
- The ramp (`SetHardwareGammaRamp`, driven by `mat_monitorgamma`) calls
  `IDirect3DDevice::SetGammaRamp` — a DAC/output-stage lookup table applied at scanout, never
  written back into the surface the capture path reads.

**Confidence: SDK cross-reference only, not yet confirmed against VtMB's own binaries** — this
section is read from the leaked/public Source engine source (`materialsystem/shaderapidx9/
shaderapidx8.cpp` + `shaderdevicedx8.cpp`, mirrored at `nillerusr/source-engine` on GitHub),
the same 2004-era DX8 architecture this doc's rest is drawn from via Ghidra, but not this
specific finding. What would raise it to verified: decompiling `engine.dll`'s own
`TakeSnapshotTGA`/`ReadPixels` and confirming it never calls `SetGammaRamp`/reads a
ramp-corrected surface. Practical consequence: an original-game reference
screenshot used for quantitative calibration needs the `mat_monitorgamma`/`cl_v_gamma` curve
re-applied as a post-process step before comparison, or the capture session's ramp cvars
pinned to engine default so the raw file and the on-screen image already match.
