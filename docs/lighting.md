# Real-time lighting

> **Reference — Godot prototype, not this repo's live state.** This document describes the
> read-only Godot prototype at `E:\dev\elysium` (its C#/Godot code, `.gdshader`, Godot nodes,
> and any "shipped/done" status below are the prototype's). Elysium-Unreal has only **M0** built
> so far — see `docs/rebuild-strategy.md` for the Unreal plan and current state. The
> **engine-neutral VtMB facts** here (WORLDLIGHTS lump, `dworldlight_t`, lightstyles, texlights)
> are valid and load-bearing; the Godot implementation detail is kept as the porting reference.
> Bare `CLAUDE.md` and `docs/archive/…` paths mentioned below are in the Godot repo.

How Elysium lights the world: **in real time by Godot lights**, one light per VtMB
**WORLDLIGHTS (lump 15)** source. There is no baked lightmap, no lightmap atlas, no UV2,
and no lightstyle atlas recomposite — VtMB's baked light *transport* (lump 8) is present
in the BSP but never decoded. This is the only render path; no flag toggles it.

Sources: `tools/bsp.py` (`read_worldlights`), `tools/bsp_to_scene.py` (`write_lights`,
`write_sprites`), `game/src/World/LightRig.cs`, `game/src/World/Lightstyles.cs`,
`game/src/World/CoronaField.cs`, `game/src/Core/GameScene.cs` (ambient + environment),
`game/src/Assets/MaterialFactory.cs`, `game/shaders/shaded.gdshader`.

Design rationale and the phase-by-phase migration record (baked → real-time) live in
`docs/archive/lighting_refactor.md` **(Godot repo)**. Its CLAUDE.md → "Real-time lighting
(WORLDLIGHTS lump 15 → LightRig)" is the concise fact summary; this doc is the detail.

## The input — WORLDLIGHTS (lump 15)

Lump 15 is VtMB's compiled light-*source* set: every point, spot, sun, sky-ambient, and
emit-surface (texlight) the artists placed, including texlights and skyambient that no
entity carries. It is the complete set the engine actually rendered from, and it is the
primary lighting input.

`bsp.read_worldlights` decodes the standard 88-byte `dworldlight_t`: `origin`@0,
`intensity`@12 (**linear RGB**, can exceed 1 — seen up to ~143 000), `normal`@24 (beam
direction), `type`@40, `style`@44, `stopdot`/`stopdot2`/`exponent`@48/52/56 (spot cone),
`radius`@60 (cutoff). Types: **0** emit_surface (texlight), **1** point, **2** spot,
**3** skylight (sun), **5** skyambient. (Type 4 quakelight exists in the enum but is
unused on the test maps.)

Counts across the test set: point (1) 10–300, spot (2) 12–464, texlight (0) 0–32. Only
`sp_ninesintro` and `sp_tutorial_1` carry a skylight (3) + skyambient (5); the rest are
interior-lit, with no sun/sky term.

## Export — the `.lights` sidecar (`write_lights`)

`bsp_to_scene.write_lights` dumps lump 15 **1:1** into `<map>.lights`, one source per
line, in Godot space. Intensity stays raw linear RGB; the runtime does the colour/energy
split. No clustering or merging happens here — the sidecar is a faithful dump; the
texlight merge is a runtime rendering choice (below).

```
type ox oy oz  dx dy dz  ir ig ib  radius_m  stopdot stopdot2 exponent  style
  type      0 emit_surface, 1 point, 2 spot, 3 skylight, 5 skyambient
  o*        origin, Godot metres  (sx,sz,-sy)*INCH_TO_M
  d*        beam direction, Godot unit vector (nx,nz,-ny); 0 0 0 if none
  i*        intensity, raw linear RGB (colour*brightness)
  radius_m  cutoff radius in metres (0 = no cutoff)
  stopdot/stopdot2/exponent   spot cone (cos inner / cos outer / falloff exp)
  style     lightstyle index (0 = constant)
```

Attenuation const/linear/quad are omitted: VtMB's are always `0/0/1` (pure
inverse-square), and Godot's falloff is a range-clamped exponent, not the VtMB
polynomial (see calibration below). `targetname` is not exported yet — it is only needed
for switchable-light I/O, which is deferred (`docs/entity_visuals.md` R6).

## The rig — `LightRig` (`World/`, one node per source)

`LightRig.TryLoad("<map>.lights")` reads the sidecar and spawns one Godot node per light,
freed/rebuilt with the map. Type mapping:

| VtMB type | Godot node | Mapping |
|---|---|---|
| 1 point | `OmniLight3D` | `omni_range` = radius (else fallback); `omni_attenuation` = `ATTEN` |
| 2 spot | `SpotLight3D` | `-Z` along `normal`; `spot_angle = acos(stopdot2)` clamped to [1°, 89°]; `spot_range` from radius |
| 3 skylight | `DirectionalLight3D` | `-Z` along `normal`; energy on its **own** scale (`SUN`) |
| 5 skyambient | **no node** | normalized colour → `Environment` ambient tint (`SkyAmbient`, applied by `GameScene`) |
| 0 emit_surface | clustered `OmniLight3D`s | texlights — see below |

**Unit conversion.** Each light's raw linear intensity is split into a **normalized
colour** (`intensity / max(intensity.rgb)`, set as the light's sRGB colour) and a scalar
**magnitude** `mag = max(intensity.rgb)`. Energy:

- omni/spot: `min(mag × SCALE, MAXE)` — the per-light clamp tames the huge intensity
  range (median ~2 100, up to ~143 000) under Godot's bounded falloff.
- sun (type 3): `mag × SUN` — sky intensities are near-unit, unlike the radiosity
  magnitudes of point/spot, so the sun needs a separate scale.

**Attenuation is calibrated, not 1:1.** Godot omni/spot lights are **hard-clamped to
their range** and fall off by an *exponent* (`*_attenuation`), not VtMB's unbounded
inverse-square with a `radius` cutoff. So `range = radius` but the exponent and energy
scale are matched by a capture sweep, accepting a different falloff curve. Radius-0 lights
fall back to `RANGE` metres.

**Shadows** on all omni/spot by default (`SHADOWS`). Cost is a non-issue on the target
hardware (CLAUDE.md → "Target hardware"); `distance_fade` curation is unneeded and not
plumbed.

### Calibration knobs (env vars; shipping defaults)

Read at rig build (no rebuild needed for a capture A/B):

| Env var | Default | Effect |
|---|---|---|
| `ELYSIUM_RIG_SCALE` | 0.003 | omni/spot energy = `max(intensity.rgb) × SCALE` |
| `ELYSIUM_RIG_SUN` | 1.5 | skylight energy = `max(intensity.rgb) × SUN` |
| `ELYSIUM_RIG_ATTEN` | 1.5 | omni/spot distance-attenuation exponent |
| `ELYSIUM_RIG_MAXE` | 8 | per-omni/spot energy clamp |
| `ELYSIUM_RIG_RANGE` | 20 | fallback range (m) for radius-0 lights |
| `ELYSIUM_RIG_SHADOWS` | 1 | cast shadows (1/0) |
| `ELYSIUM_RIG_CORONA` | 1.0 | corona additive-tint scale (CoronaField) |
| `ELYSIUM_RIG_AMBIENT` | 0.30 | `Environment` ambient-fill energy (GameScene) |

## Texlights (emit_surface, type 0)

VRAD emits **many patch samples** per glowing surface (e.g. 8 identical patches over an
11 m strip), so spawning one light per patch over-lights N-fold. Instead the rig bins
patches by `(intensity, normal)`, single-linkage clusters each bin (`TexLink = 4 m` gap —
a contiguous surface merges into one cluster, two distant surfaces sharing an exact
intensity/normal stay separate), and emits **one** `OmniLight3D` per cluster at its
centroid. Range covers the extent (`clamp(ext + 2, 3, 12) m`), energy = one patch's
`mag × SCALE` clamped, **shadows off** — a soft area-emission approximation. The surface
itself glows via `$selfillum`; this only adds the *cast*.

Limitation: two separate emitting surfaces that share an exact `(intensity, normal)` *and*
sit within `TexLink` merge into one (not observed on the test maps).

## Animated lightstyles (1–11)

A light's `style` field (1–11) makes it flicker/pulse. `Lightstyles` (`World/`) holds the
hardcoded Quake/Source pattern strings (each letter a brightness: `'a'` = 0 dark, `'m'` =
1.0 normal, `'z'` ≈ 2.08 over-bright), advanced at 10 Hz and lerped between keyframes.
`Lightstyles.Intensity(style, t)` returns the current multiplier; style 0, the unanimated
12–31, and the switchable 32+ all return 1.0 (constant / held ON).

At build, each style-1..11 light is recorded at its base (intensity-1) energy. Every frame
`LightRig._Process` sets `energy = baseEnergy × Lightstyles.Intensity(style, t)`.
`ELYSIUM_LS_TIME` pins the clock (seconds) for a reproducible flicker phase across
captures; the debug **F8** / `freeze` command holds the current phase (`LightRig.Frozen`).

Styled worldlights are sparse: mostly style 1, a few style 6/10; `sp_tutorial_1` has 10
(style-1 fluorescent warehouse), others 0–2.

## Ambient / GI

A flat `Environment` **ambient fill** plus **real-time SDFGI** — the dynamic stand-in for
the discarded baked lightmaps. Direct lights + emissive materials bounce indirect diffuse
fill into shadowed surfaces (the "inhabited" ambiance a flat fill alone can't do), with a
colored occlusion term. The flat fill still runs underneath so unlit areas never read pure
black. Set per map by `GameScene.MakeEnvironment` (SDFGI) + `ApplyEnvironment` (ambient):

- **Ambient tint** = the map's skyambient (type 5) normalized colour where present
  (`LightRig.SkyAmbient` → `WorldLoader.RigSkyAmbient`), else a fixed cool-night
  `(0.12, 0.13, 0.18)`.
- **Ambient energy** = `ELYSIUM_RIG_AMBIENT` (0.30) — lifts exterior foregrounds into
  readability without washing out the noir contrast.
- **SDFGI** (`ELYSIUM_SDFGI`, default on): energy `ELYSIUM_SDFGI_E` (6.0), bounce feedback
  `ELYSIUM_SDFGI_FB` (0.5), cell size `ELYSIUM_SDFGI_CELL` (0.15 m — beats VtMB's ~0.2 m
  room-dividing walls so indirect light doesn't leak through them; `sdfgi_probe.py`
  surveys the wall-thickness distribution), and `ELYSIUM_SDFGI_CASC` (6 cascades, keeping
  the fine cell while extending range). The rig is calibrated dim, so SDFGI needs the
  energy gain to replace the flat ambient its occlusion removes.

The `--capture` path renders `ELYSIUM_CAP_FRAMES` frames (default 8) before the
screenshot so SDFGI's cascades have time to converge for an A/B.

## env_sprite coronas (`CoronaField`)

The additive glow halos VtMB places at light sources (lamp halos, bulb glows, light
shafts). `bsp_to_scene.write_sprites` decodes each `env_sprite`'s Sprite-VMT
`$basetexture` to `tex/spr_*.png` and writes `<map>.sprites` (one line per sprite: `png ox
oy oz w_m h_m r g b amt orient`, Godot space, world size = Source `scale × textureSize`);
`start_hidden` sprites are skipped.

`CoronaField.TryLoad` spawns one **additive, unshaded, camera-facing `QuadMesh`** per
sprite — depth-tested (walls occlude) but no depth write (a glow occludes nothing). Tint =
`rendercolor × renderamt × ELYSIUM_RIG_CORONA`; orientation 0 (`vp_parallel`, full
billboard) or 1 (`parallel_upright`, Y-axis only). Counts 11–309 per map. The Sprite
material proxy fades (`PlayerProximity` distance/angle) are not reproduced — the corona
draws at its authored `renderamt`.

## Shaded materials (brief)

World and props use `shaded.gdshader` (`shaded_blend.gdshader` for `$translucent`), **lit
by the rig's real Godot lights** — not `unshaded`, no baked atlas. VtMB world is pure
Lambert (`METALLIC = 0`, `SPECULAR = 0`, `ROUGHNESS = 1`). The manual material terms
(`$envmap` additive reflection, WorldVertexTransition albedo blend, `$bumpmap`-perturbed
shading normal, alpha-masked `$selfillum` emission) are unchanged from the baked path and
documented in CLAUDE.md → "Material fidelity". Props are lit by the rig identically to the
world — no per-prop baked tint.

**Emission / exposure.** The `_ke.png` self-illum mask is premultiplied and masked at
export, so it adds correctly to the lit-but-dim surface with no blow-out.
`RenderOptions.EmisEnergy` (**1.0**, the `emis` console/launch knob) scales `$selfillum` +
`$additive` glow energy. `GameScene.MakeEnvironment` uses a **Linear tonemap** (no S-curve;
Godot still does the linear→sRGB output encode, matching VtMB's gamma-space framebuffer),
with **SSAO + Glow on** and **SDFGI** — render-path compensations the real-time rig needs
(VtMB itself is DX8/LDR with no post-processing). A per-map `.cube` color-grade LUT and an
optional `film_grain` overlay finish the frame. See `docs/color_gamma.md`.

## Debug views

The debug HUD's number row (`RenderOptions.DebugView`, keys 1–8; `view <name|0-9>` console
command / `--view` flag): `textured`, `flat`, **`lighting`** (the rig's contribution),
**`fullbright`** (albedo without lighting), `normals`, `envmap`, `uv-check`, `missing-tex`.
All but `flat` are done live in the shaded shader via its `debug_view` uniform (no
rebuild); `flat` swaps every material for a name-hashed colour.

## Gaps / not yet faithful

- **Switchable lightstyles (32+) can't be toggled — held ON.** The entity I/O
  (`light`/`env_*` inputs) that would switch them, and switch `start_hidden` coronas on,
  is not ported, so a light meant to spawn dark stays lit. Tracked as `docs/entity_visuals.md`
  R6 (it plugs into the entity-I/O substrate, not the lighting layer). Styles 0 and 1–11
  are fully driven.
- **Attenuation is a calibrated approximation.** Godot's range-clamped exponent falloff is
  not VtMB's inverse-square; the light-reach/pooling shape differs and is matched by
  capture, not derived.
- **Texlights are a merged point-light approximation** — one omni per clustered emitting
  surface, not the true area emission.
- **Corona proxy fades not reproduced** — `PlayerProximity` distance/angle alpha is
  ignored; coronas draw at their authored `renderamt`.
- **GI is real-time SDFGI, not baked** — an approximation of VtMB's radiosity, tuned by
  capture (energy/cell/cascades above); it can still leak through sub-voxel gaps and needs
  a few frames to converge. No VoxelGI, no `LightmapGI`.
- **Baked lump 8 (LIGHTING) is not decoded** *(by the Godot prototype this doc describes)*,
  nor are the HDR/ambient-cube lumps (empty in VtMB's data anyway). Displacements are lit by
  the rig like any other world surface. **Elysium-Unreal note:** lump 8 *is* now decoded
  offline — for lighting *analysis/calibration*, not runtime baking (`tools/lightmap.py`,
  `tools/probe_light_calibration.py`; see `rendering-perf.md` → "Why Lumen is load-bearing").

## Related docs

`docs/archive/lighting_refactor.md` **(Godot repo)** (design rationale + completed migration
record), `docs/color_gamma.md` (VtMB gamma/overbright — the LDR look), `docs/entity_visuals.md`
(env_sprite substrate + R6 switchable-light I/O), `docs/entity_io.md` (the light-toggling
inputs), CLAUDE.md ("Real-time lighting", "Material fidelity", "env_sprite coronas").
