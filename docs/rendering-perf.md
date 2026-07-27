# Rendering performance

## SM6 / DX12 is mandatory — check this FIRST

Lumen, MegaLights, Virtual Shadow Maps, and hardware ray tracing **all require Shader
Model 6 (DX12)**. If the renderer falls back to DX11/SM5, every one of those features is
**silently off** — no error, just a slideshow: the geometry renders, but the hundreds of
dynamic lights hit the render thread one-by-one with no MegaLights to batch them (CPU-bound,
GPU mostly idle), and there is no GI bounce. Symptom in `stat gpu`: no `Lumen`/`MegaLights`/
`VirtualShadowMaps` passes, `DistanceFields 0.00`, and the window title reads `PCD3D_SM5`.
The title must read **`PCD3D_SM6`**. This one config was the difference between ~20 and
~170 fps on an RTX 5070. It is pinned two ways:

- `Config/DefaultEngine.ini` → `[/Script/WindowsTargetPlatform.WindowsTargetSettings]`:
  `DefaultGraphicsRHI=DefaultGraphicsRHI_DX12` + `D3D12TargetedShaderFormats=PCD3D_SM6`.
- `play.bat` passes `-dx12` as belt-and-suspenders.
- HWRT additionally requires `r.SkinCache.CompileShaders=True` (a hard fatal error otherwise).

First launch after enabling SM6 recompiles the whole shader set (slow once, then cached).

## The cost profile is unusual

VtMB is 2004 early-Source geometry (~20k tris/map), so draw calls and triangles are free.
Once SM6 is on, the frame budget goes to **light shadowing + Lumen GI**, because each map
spawns hundreds of dynamic lights (`sp_tutorial_1` = 396). MegaLights keeps that many-light
cost ~constant; Lumen provides the bounce that carries VtMB's look.

## Why Lumen is load-bearing (the calibration)

`tools/probe_light_calibration.py` fits our dynamic-light model against VtMB's own baked
lightmaps (BSP lump 8 — the ground truth VRAD produced from the lump-15 light sources). It
is *not* a bake; it reads the baked result to learn how VtMB actually lit the scene. On
`sp_tutorial_1` (10,373 lit faces vs 394 sources) it found:

- Baked luminance is **high-contrast and dark** (median 2.1, 95th 43 — an 82× range). Real
  noir, not a flat fill.
- **Distance to the nearest light explains almost none of it** (Spearman ρ ≈ 0.10; model-free
  falloff slope ≈ 0). So it is **not** inverse-square pooling — VtMB light is ~flat within
  its authored radius, then cut. (This is why the LightRig uses a gentle exponent falloff,
  not inverse-square, and why chasing the exponent barely matters.)
- **Occlusion doesn't explain it either.** Adding real shadowing (70% of face→light rays are
  blocked) still leaves R² = 0 — even a physically-correct direct+shadow model has *zero*
  predictive power over the baked result.
- By elimination, the contrast is **indirect bounce (radiosity)**, which is spatially smooth
  and decorrelated from the point/spot positions.

**Consequence:** a pure direct-light rig fundamentally *cannot* reproduce VtMB's look — the
bounce is the look. So **Lumen GI is load-bearing, not overkill**, and the dynamic lights are
a modest contributor feeding it. Re-run the tool on any map; interior-only maps may differ.

## Lumen surface-cache engine facts

Generic UE 5.8 engine behavior, independent of this project's bake/runtime split.

**Three requirements for a primitive to enter the surface cache** (all three, or it is culled):

1. **Card representation.** `FLumenSceneData::AddMeshCards` reads
   `Proxy->GetMeshCardRepresentation()`; a null pointer takes the fallback branch and the
   primitive gets no cards. Only `FStaticMeshSceneProxy`, `FSkeletalMeshSceneProxy`, the Nanite
   proxies, and `FBaseDynamicMeshSceneProxy` override it — a `UProceduralMeshComponent` proxy
   never does.
2. **Static mesh batches.** The card-capture pass (`EMeshPass::LumenCardCapture`) draws from
   `PrimitiveSceneInfo->StaticMeshRelevances`. A proxy that only implements
   `GetDynamicMeshElements` (again, `UProceduralMeshComponent`) contributes no draws to that
   pass, so even a hand-attached card representation would capture nothing.
3. **Large enough.** The primitive's largest bounding-box face area must exceed
   `r.LumenScene.SurfaceCache.MeshCardsMinSize` (`LumenMeshCards::GetCardMinSurfaceArea`). Below
   it, the primitive is culled outright — Epic's own docs describe this as small meshes reading
   black in reflections, tunable via the post-process "Lumen Scene Detail" setting.

`FStaticMeshLODResources::CardRepresentationData` is a public raw pointer the LOD owns and
deletes; it can be assigned a custom `FCardRepresentationData` at runtime as long as this happens
before the scene proxy is constructed (the proxy copies the pointer at construction time).
`MeshCardRepresentation::SetCardsFromBounds` is `ENGINE_API` and reachable from game code with no
engine edit — it is what `UDynamicMeshComponent` itself uses.

**`SetCardsFromBounds` behavior.** It always emits exactly **six cards, one per face of the
bounding box**, each looking inward and extended outward by a small fixed offset
(`CardZOffset = 5`) to avoid degeneracy on thin bounds. A card captures only the *nearest*
surface along its facing direction, so six box faces cannot represent a volume that holds several
surfaces at different depths (e.g. a room with furniture) — Epic's own guidance is that only
meshes with simple interiors work this way, and that walls/floors/ceilings should be separate
meshes. Real static meshes built through the editor's offline surfel-fitting builder get up to 12
cards (raisable) placed against the actual surface; bounds-only cards are a strictly coarser
approximation.

**Hit lighting is a fallback, not a mode switch.** A reflection ray re-traces with hit lighting
only when hit lighting is force-enabled, *or* the surface-cache lookup at the hit point came back
incomplete (`LumenReflectionHardwareRayTracing.usf`). Enabling
`r.Lumen.Reflections.HardwareRayTracing.HitLighting` does not change the ray's lighting mode by
itself — it only permits a *miss* to fall through to a second, more expensive trace. Its cost
therefore scales with the fraction of the scene the surface cache fails to cover, and falls
automatically as coverage improves; it never regresses correctness, only cost. Nanite has no
runtime build path (`NaniteBuilder` is an editor-only module), so a runtime-constructed mesh
cannot itself be Nanite regardless of card coverage.

## Shipped defaults

Target hardware: `CLAUDE.md` → "Target hardware". One forced tier ships; there is no quality
ladder yet.

- Feature enables: `Config/DefaultEngine.ini` → `[/Script/Engine.RendererSettings]`
  (`r.DynamicGlobalIlluminationMethod=1`, `r.ReflectionMethod=1`, `r.RayTracing=True`,
  `r.SkinCache.CompileShaders=True`, `r.Lumen.HardwareRayTracing=True`,
  `r.MegaLights.EnableForProject=True`, `r.Shadow.Virtual.Enable=1`).
- Quality tier: `Config/DefaultEngine.ini` → `[SystemSettings]`, all eleven `sg.` scalability
  groups pinned to **Epic (3)** with `sg.ResolutionQuality=100`. Each `sg.` line pulls in the
  matching block of `Engine/Config/BaseScalability.ini` — ~45 cvars for GI alone — Epic's own
  recommendation over hand-rolled cvars, since the buckets keep indirect lighting consistent as
  they scale.
- Three deliberate deviations sit under the `sg.` block: HWRT scene culling
  (`r.RayTracing.Culling` 3 / Radius 15000 / Angle 0.5), 16x anisotropy (the Epic bucket stops at
  8), and a 3 GB texture streaming pool (Epic bucket 1000 MB).
- `[SystemSettings]` (not `DefaultScalability.ini`) is the right home for *forced* values:
  `ECVF_SetBySystemSettingsIni` is priority 0x05, above `ECVF_SetByScalability` (0x02) and
  `ECVF_SetByGameSetting` (0x03), so neither the first-run hardware benchmark nor a settings menu
  can pull the tier down. `DefaultScalability.ini` only defines what each quality *level* means.
- `sg.ResolutionQuality` is the main quality/perf dial — it maps directly to `r.ScreenPercentage`.
  Drop it live in console to trade image for frames.

## Floor reality (RTX 4060 tier)

- **Software Lumen is off the table.** It needs mesh distance fields, and the baked meshes carry
  Nanite but no DFs. HWRT is the only tracing path — no cheaper GI fallback, no non-RT mode.
- **There is real headroom:** 5.1–6.2 ms total GPU at 1440p **native** with Epic-tier Lumen
  (baseline below). The bake is what affords it — Nanite world geometry plus full DDC-fitted Lumen
  card coverage, so no reflection ray needs a hit-lighting second trace to find a lit surface.
- **VRAM:** the floor assumes **16 GB** — leaves the streaming pool at 3 GB and the Lumen
  surface-cache atlas at the Epic bucket's 4096.
- **The 4060 number is extrapolated, not measured.** Every capture here is on a 5070 Ti, which
  clears the budget comfortably; validating the floor needs actual 4060-class hardware. Until then
  the floor is judged by *look* plus the pass proportions below.

**Structural note:** the look is bounce-dominated (see the calibration above), so Lumen GI is
load-bearing rather than polish. Baking VtMB's own lump-8 lighting remains the fallback if the
floor ever has to drop below an RT-capable card — free at runtime, and literally the data VtMB
shipped — but at the 4060 floor the HWRT path is no longer living on the edge.

## Is MegaLights actually engaging? (do this first)

MegaLights makes the many-light cost **~constant** (bounded by `NumSamplesPerPixel`, not
light count) — *"there isn't a large difference between unshadowed and shadowed lights."*
If it is **not** engaging, the 396 movable lights fall back to per-light Virtual Shadow
Maps (≈396 shadow maps) and the frame collapses. That fallback is the most likely cause of
a big FPS drop, so confirm engagement before touching any other knob:

1. **Project Settings → Rendering → Direct Lighting → MegaLights = Enabled.** When it
   prompts to enable "Support Hardware Ray Tracing", accept. (Mirrors
   `r.MegaLights.EnableForProject=True`.)
2. **Console check:** `r.MegaLights.EnableForProject` and `r.MegaLights.Allow` both read
   non-zero.
3. **Per-light eligibility:** each light needs **Allow MegaLights** on (default for
   *movable* local lights — the rig sets every light `Movable`, so they qualify) and
   **MegaLights Shadow Method = Ray Tracing** (not Virtual Shadow Maps — VSM method is
   per-light and defeats the constant-cost win).
4. **Visualize:** `r.MegaLights.Debug 1` (or the MegaLights show-flag) highlights the lights
   MegaLights is handling. Lights it *isn't* handling are the expensive ones.
5. **Profile the proof:** `ProfileGPU` (or `stat GPU`) and compare the **MegaLights** pass
   against **ShadowDepths / VirtualShadowMaps**. If VSM dominates and MegaLights is ~0, the
   lights are falling through to VSM — fix steps 1-3 first. That single fix can be the whole
   problem; do not tune Lumen until this is confirmed.

## Tuning ladder (paste live, cheapest win first)

The shipped tier is Epic across the board, so tuning **down** is the direction of travel. Prefer
moving a whole `sg.` group over hand-picking cvars — the buckets are internally consistent.

| cvar | try | effect |
|---|---|---|
| `sg.ResolutionQuality` | `100` → `77` → `66` | render lower + TSR upscale — the biggest single win |
| `sg.GlobalIlluminationQuality` / `sg.ReflectionQuality` | `3` → `2` | whole Lumen tier down one step (~half the cost per step) |
| `r.Lumen.ScreenProbeGather.TracingOctahedronResolution` | `8` → `4` | *highest-impact* single Lumen GI knob (buckets: 8 High/Epic, 16 Cine) |
| `r.Lumen.ScreenProbeGather.DownsampleFactor` | `16` → `32` | fewer GI probes (buckets: 32 Med / 16 Epic / 8 Cine) |
| `r.Lumen.Reflections.DownsampleFactor` | `1` → `2` | quarter-res reflections |
| `r.Lumen.Reflections.Allow` | `0` | drop Lumen reflections → SSR (~20-30% back) |
| `r.MegaLights.NumSamplesPerPixel` | `4` → `2` | many-light shadow-ray budget (cost ~flat in light count) |
| `elysium.lights` | — | toggle the whole rig off to isolate lighting cost |

Nuclear brackets to attribute cost: `r.Lumen.HardwareRayTracing 0` (HWRT's share),
`r.RayTracing 0` (all RT), `r.MegaLights.Allow 0` (MegaLights' share).

## Profiling

**Headless and repeatable:** `profile.bat <map> [cam]` launches standalone
at 2560×1440 / SM6 and drives the `-ElysiumProfile` harness
(`Source/ElysiumUE/Private/Debug/ElysiumProfiler.cpp`) — it pins the camera to each fixed vantage
near spawn (the `GProfileCams[]` table), warms up 120 frames, captures 300 through the CSV
profiler (`-csvGpuStats` → per-pass GPU ms), dumps one `ProfileGPU` tree to the log, writes a
JSON summary, and exits. `tools/profile_report.py` turns the CSVs into the per-pass table
(`tools/out/_profile/<map>_report.md`) and the committed baseline below ("Profiling
baseline").
The harness also logs the SM6/adapter confirmation and the MegaLights-vs-ShadowDepths split, so
one run covers both the render-path check and the engagement check. Capture a new
vantage by flying there in-game and running `elysium.campos` (logs a paste-ready
`GProfileCams[]` row with the exact pitch the HUD omits).

The fixed vantages double as **rendering-regression test points**: re-run `profile.bat` after
any render-path change and diff the per-vantage GPU ms against the committed baseline below.

Manual knobs for interactive digging: `stat unit` (frame/game/GPU ms), `stat GPU` (per-pass
GPU time), `ProfileGPU` (one-frame pass breakdown), Unreal Insights for a timeline. Always
profile a **standalone** build (`play.bat` / `profile.bat`), not a PIE editor session — editor
overhead skews the numbers.

## Profiling baseline

Captured by `profile.bat` (see "Profiling" above for the harness). Full per-vantage reports
(incl. the heaviest-pass breakdown) regenerate at `tools/out/_profile/<map>_report.md`.

**Dev GPU: RTX 5070 Ti · D3D12 / `PCD3D_SM6` · 2560×1440 **native** · warmup 120 / capture 300
frames.** This card is far above the RTX 4060 floor, so read these for **pass proportions and
regression tracking**, not floor frame rate (the floor is judged by *look* — see "Floor
reality" above). Vantage coordinates are baked in `GProfileCams[]` and echoed in each
report's vantage headers.

**Resolution caveat.** The harness does not record the resolution it rendered at, and
`Saved/Config/WindowsEditor/GameUserSettings.ini` overrides `profile.bat`'s `-resx/-resy/-windowed`
— its `FullscreenMode=1` makes the window borderless, so on a 4K display the 1440p backbuffer is
composited up to fill the screen and *looks* native 4K. Confirm the real backbuffer by reading the
pixel dimensions of a `shots.bat` PNG (`tools/out/_shots/<map>/`); the captures below were
verified 2560×1440 that way.

### sp_tutorial_1 — 395 world lights — GPU ms per vantage

Epic-tier Lumen at 1440p native, on the baked level.

| Pass | spawn | t1 | t2 | t3 | t4 |
|---|---|---|---|---|---|
| Lumen GI (ScreenProbeGather) | 0.01 | 0.02 | 0.02 | 0.02 | 0.02 |
| Lumen reflections | 0.25 | 0.18 | 0.21 | 0.18 | 0.19 |
| MegaLights | 0.97 | 1.05 | 1.10 | 1.26 | 1.33 |
| ShadowDepths / VSM | 0.61 | 0.79 | 0.75 | 0.92 | 0.76 |
| **Total GPU** (whole frame) | **5.08** | **5.86** | **5.89** | **6.18** | **6.11** |

The two tables below are **not comparable** to the one above: they were captured at 66% screen
percentage with Medium-tier Lumen on the runtime `UProceduralMeshComponent` path, and neither map
is baked. Kept only for the many-light cost reading below; re-capture once those maps are baked.

### sm_hub_1 — 687 world lights — GPU ms per vantage

| Pass | spawn | h1 | h2 |
|---|---|---|---|
| Lumen GI (ScreenProbeGather) | 0.01 | 0.01 | 0.01 |
| Lumen reflections | 0.05 | 0.05 | 0.05 |
| MegaLights | 1.05 | 1.12 | 1.26 |
| ShadowDepths / VSM | 0.00 | 0.00 | 0.00 |
| **Total GPU** (whole frame) | **3.82** | **4.40** | **4.49** |

### sm_pawnshop_1 — 161 world lights — GPU ms per vantage

| Pass | spawn | p1 | p2 | p3 |
|---|---|---|---|---|
| Lumen GI (ScreenProbeGather) | 0.01 | 0.01 | 0.01 | 0.01 |
| Lumen reflections | 0.06 | 0.05 | 0.06 | 0.05 |
| MegaLights | 1.28 | 1.32 | 1.27 | 1.23 |
| ShadowDepths / VSM | 0.00 | 0.00 | 0.00 | 0.00 |
| **Total GPU** (whole frame) | **3.96** | **4.31** | **4.30** | **4.27** |

**MegaLights is engaging, with no VSM blow-up.** MegaLights (~1.0–1.3 ms) meets or
beats ShadowDepths on every vantage, and the many-light cost is ~**flat**: 687 lights
(sm_hub_1) and 161 (sm_pawnshop_1) both cost the same ~1 ms as 395 (sp_tutorial_1).
ShadowDepths never dominates and is ~0 on both `sm_` maps. MegaLights is carrying the local lights
as designed.

**No `[VSM] Non-Nanite Marking Job Queue overflow`.** Large single-section PMC world surfaces
drive that warning by covering a large shadow page area; the Nanite-baked world sidesteps it — a
full five-vantage capture logs **zero** overflows.

**Other reads.** MegaLights (~1.0–1.3 ms) is the single heaviest pass at every vantage;
TemporalSuperResolution is ~0.48 ms, since at `sg.ResolutionQuality=100` it does anti-aliasing
rather than a 66%→100% upscale. Nanite adds its own passes (NaniteBasePass ~0.38, NaniteVisBuffer
~0.28), which the PMC path does not have. Lumen GI reads ~free (0.02 ms) *on this card* — a
fast-GPU artifact, not proof Lumen is cheap. Render-thread time collapses to ~0 (idle-waiting on
the GPU): the title is GPU-bound, as expected.


## Sources

- [Lumen Performance Guide — Epic (UE5.8)](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine)
- [MegaLights — Epic (UE5.8)](https://dev.epicgames.com/documentation/unreal-engine/megalights-in-unreal-engine)
- [MegaLights: Stochastic Direct Lighting — SIGGRAPH 2025](https://advances.realtimerendering.com/s2025/content/MegaLights_Stochastic_Direct_Lighting_2025.pdf)
- [UE5 Lumen Optimization: 60fps on mid-range — StraySpark](https://www.strayspark.studio/blog/ue5-lumen-optimization-60fps)
- [Virtual Shadow Map Optimization UE5.7 — StraySpark](https://www.strayspark.studio/blog/virtual-shadow-map-optimization-open-worlds-ue5-7)
- [Lumen vs Lumen Lite in UE 5.8 — StraySpark](https://www.strayspark.studio/blog/lumen-vs-lumen-lite-ue5-8-performance)
- [r.Lumen.ScreenProbeGather.DownsampleFactor bucket values — UE CVar Wiki](https://indxzero.github.io/ue544cvarwiki/articles/r.lumen.screenprobegather.downsamplefactor/)
- [Lumen Lighting for Indies (software vs hardware RT) — Hyperdense](https://medium.com/@sarah.hyperdense/lumen-lighting-for-indies-good-results-without-melting-your-gpu-517cfe83c6c7)
- [Ray Tracing Performance Guide — Epic (UE5.8)](https://dev.epicgames.com/documentation/unreal-engine/ray-tracing-performance-guide-in-unreal-engine)
- `Engine/Config/BaseScalability.ini` — the authoritative per-tier cvar buckets
