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

## Shipped defaults

Target hardware is in `CLAUDE.md` → "Target hardware": **RTX 3060-class floor, RTX
4070/5070 recommended.** The shipped config is tuned for the **floor** — we develop against
min-spec, so a stronger dev GPU renders exactly what the floor sees (a scale-UP tier for
better GPUs comes later via the scalability system, not by shipping high defaults).

- Feature enables: `Config/DefaultEngine.ini` → `[/Script/Engine.RendererSettings]`
  (`r.DynamicGlobalIlluminationMethod=1`, `r.ReflectionMethod=1`, `r.RayTracing=True`,
  `r.SkinCache.CompileShaders=True`, `r.Lumen.HardwareRayTracing=True`,
  `r.MegaLights.EnableForProject=True`, `r.Shadow.Virtual.Enable=1`).
- Quality cvars: `Config/DefaultEngine.ini` → `[SystemSettings]` (floor budget:
  `r.ScreenPercentage=66`, `r.MegaLights.NumSamplesPerPixel=2`, Lumen probe/reflection
  downsampled). `[SystemSettings]` (not `DefaultScalability.ini`) is the right home for
  *forced* values — it applies at startup and overrides the auto-detected scalability level,
  so the budget is deterministic; `DefaultScalability.ini` only defines what each quality
  *level* means and is subject to the first-run hardware benchmark.
- `r.ScreenPercentage` is the main quality/perf dial: bump to 100 live in console to preview
  what a stronger GPU could show. Floor-fps must be validated on an actual 3060 — a fast dev
  GPU always clears it, so here the floor is judged by *look*, not frame rate.

## Floor reality (RTX 3060 tier) — validated, and tight

Cross-checked against UE dev guidance for the 3060/mid-range RT tier:

- **Software Lumen is off the table for us.** Guidance recommends software RT below the
  3060+ tier, but software Lumen needs mesh distance fields, which our *runtime-built* PMC
  meshes don't have. HWRT is forced — we can't take the cheaper GI path.
- **The budget is razor-thin.** On a 3060 at 1080p, HWRT Lumen alone runs ~10–14 ms of the
  16.6 ms (60 fps) frame, leaving ~3–6 ms for everything else. We pair the *weakest* RT card
  with the *expensive* GI path, so the aggressive Lumen downsampling + 66% TSR aren't polish,
  they're what makes 60 fps possible at all. There is essentially no margin.
- **Bucket alignment:** Epic's `BaseScalability.ini` sets `ScreenProbeGather.DownsampleFactor`
  to Medium=32, High=16, Cine=8. The floor uses **32 (Medium)**; 16 is the High/console tier,
  too rich for the minimum RT card.
- **VRAM:** the floor assumes the **12 GB** 3060 (avoid the 8 GB variant).
- **Can't measure floor fps on a fast dev GPU** — validate the floor by *look*; real
  floor-frame-rate needs an actual 3060-class card, or a conservative margin.

**The honest structural takeaway:** because the look is bounce-dominated (see the calibration
above) *and* HWRT-Lumen-on-a-3060 has no headroom, the sustainable floor answer is **baking
VtMB's lump-8 lighting** — free GI at runtime, runs on anything, and it's literally the data
VtMB shipped. The HWRT path works but lives on the edge; baked GI is the real floor solution
(or honestly raise the floor to 3060 Ti / 4060 for an RT-required game).

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

| cvar | try | effect |
|---|---|---|
| `r.ScreenPercentage` | `66` → `50` | render lower + TSR upscale — the biggest single win |
| `r.MegaLights.NumSamplesPerPixel` | `2` | many-light shadow-ray budget (cost ~flat in light count) |
| `r.Lumen.ScreenProbeGather.TracingOctahedronResolution` | `2` | *highest-impact* Lumen GI knob |
| `r.Lumen.ScreenProbeGather.DownsampleFactor` | `32` | fewer GI probes (Epic buckets: 32 Med / 16 High / 8 Cine) |
| `r.Lumen.Reflections.DownsampleFactor` | `2` | quarter-res reflections |
| `r.Lumen.Reflections.Allow` | `0` | drop Lumen reflections → SSR (~20-30% back) |
| `sg.GlobalIlluminationQuality` / `sg.ReflectionQuality` | `1` | medium Lumen scalability |
| `elysium.lights` | — | toggle the whole rig off to isolate lighting cost |

Nuclear brackets to attribute cost: `r.Lumen.HardwareRayTracing 0` (HWRT's share),
`r.RayTracing 0` (all RT), `r.MegaLights.Allow 0` (MegaLights' share).

## Profiling

`stat unit` (frame/game/GPU ms), `stat GPU` (per-pass GPU time), `ProfileGPU` (one-frame
pass breakdown), Unreal Insights for a timeline. Profile a **standalone** build
(`play.bat`), not a PIE editor session — editor overhead skews the numbers.

## Sources

- [Lumen Performance Guide — Epic (UE5.8)](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine)
- [MegaLights — Epic (UE5.8)](https://dev.epicgames.com/documentation/unreal-engine/megalights-in-unreal-engine)
- [MegaLights: Stochastic Direct Lighting — SIGGRAPH 2025](https://advances.realtimerendering.com/s2025/content/MegaLights_Stochastic_Direct_Lighting_2025.pdf)
- [UE5 Lumen Optimization: 60fps on mid-range — StraySpark](https://www.strayspark.studio/blog/ue5-lumen-optimization-60fps)
- [Virtual Shadow Map Optimization UE5.7 — StraySpark](https://www.strayspark.studio/blog/virtual-shadow-map-optimization-open-worlds-ue5-7)
- [Lumen vs Lumen Lite in UE 5.8 — StraySpark](https://www.strayspark.studio/blog/lumen-vs-lumen-lite-ue5-8-performance)
- [r.Lumen.ScreenProbeGather.DownsampleFactor bucket values — UE CVar Wiki](https://indxzero.github.io/ue544cvarwiki/articles/r.lumen.screenprobegather.downsamplefactor/)
- [Lumen Lighting for Indies (software vs hardware RT) — Hyperdense](https://medium.com/@sarah.hyperdense/lumen-lighting-for-indies-good-results-without-melting-your-gpu-517cfe83c6c7)
- [Is RTX 3060 enough for Unreal? (RT entry card, 12 GB) — GLS](https://www.gameslearningsociety.org/wiki/is-rtx-3060-enough-for-unreal/)
