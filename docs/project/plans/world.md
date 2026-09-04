# World look & scale plan — open-task specifications

Specifications for **open** tasks in the lighting/look lane (P3), dressing & parity (P7) and
scale-out (P10's look half). Status lives solely in `docs/project/roadmap.md`; a task that lands
is deleted here. No status marks in this file. **The whole lane is frozen under playable-path
rule 2** — gameplay-blocking rendering bugs excepted. Detail: `docs/architecture/rendering-perf.md`,
`docs/vtmb/lighting.md`.

### 3.1 Pin MegaLights per-light

Set each light's MegaLights Shadow Method property (Ray Tracing default; VSM the costly
alternative) explicitly in `UElysiumLightRig::Build`. *Acceptance:* the 0.2 check shows 100% rig
lights MegaLights-handled.

### 3.2 Shadow curation

Heuristic shadow-off for low-contribution lights + `elysium.ShadowMinIntensity` cvar.
*Acceptance:* measurable ms drop, no hero-shadow loss. *Deps:* 0.1, 3.1.

### 3.3 Attenuation-radius audit

Revisit `RadiusScale` / `FallbackRadiusCm=2500`; tighter reach, no black gaps. *Deps:* 0.1.

### 3.4 Texlight clustering

Bin type-0 patches by `(intensity, normal)`, single-linkage cluster, one shadowless point per
surface — **prefer exporter-side merge** in `UE_bsp_to_scene.py` (deterministic, free at
runtime; PL6). Fixes N-fold over-lighting on non-tutorial maps. *Deps:* 0.3.

### 3.6 Pinned exposure

Fixed EV/bias on the per-map post-process for deterministic LDR framing. The per-map PPV
(tagged `elysium.ppv`, adopted unbound) is the natural home; auto-exposure is already off in
config — this pins the *value* per map.

### 3.7 Grade/tonemapper fidelity

Stop the filmic curve crushing the look (neutralize or re-fit the tone curve; any grade
re-enters only through this task + `elysium.GradeIntensity`); add `elysium.*` A/B toggles for
sky/LUT/fog. Sky orientation is done (`docs/vtmb/sky-ambience.md`). Measured: the filmic toe
crushes a night sky by up to ×9 and unity crosses parity only at source ≈ 55, so the backdrop's
remaining gap to parity is this task's. *Deps:* 3.6.

### 3.8 Texture prewarm off the game thread

Worker-thread batch in `FElysiumTextureCache` (load is texture-bound).

### 3.9 Lightstyle clock pin + freeze

Cvar-pinned phase + freeze toggle for reproducible A/B captures.

### 3.10 Volumetric fog layer calibration

`ExponentialHeightFog` owns only the volumetric haze/light-shaft layer, near-invisible at its
current density — a mechanism, not yet a look. Calibrate it for its own sake; it no longer
rides the distance-fog numbers. Presentation layer, adjudicated by the direction test; A/B via
`elysium.Fog` + fog cvars.

### 3.12 `sm_hub_1` fill adjudication

The one map where hand survey and classifier genuinely disagree (104 of 409 fill candidates
project-wide). Judge the 15-light shortlist in the eastern strip by in-engine A/B (MCP teleport
+ screenshot per light) against C3's landed Skylight Leaking; kill a fill only where GI
demonstrably replaces it, recorded per map in `docs/vtmb/light-attribution.md`. Follow-on
threads: survey `hw_609_1`, re-score with type/style clauses, per-batch in-engine
counterfactual, batch-level voting.

### 7.1 Coronas

`.sprites` consumer: additive depth-tested billboards, StartOff spawnflag filtering.

### 7.3 Water

`.water` → `M_Water` Single Layer Water + Lumen reflections (no mirror cameras). Known engine
facts: Lumen reflections on SLW are forced mirror (acceptable for VtMB's water), and MegaLights
does not light water surfaces — verify the water direct-lighting path during this task.

### 7.5 Real reflections

The `$envmap` RE and inventory are complete (`docs/vtmb/reflections.md`); presentation
acceptance is open. Target: one graph samples the exported cube through the raw linear mask as
a primary-view additive term for the source endpoint, excludes that view-dependent term from
Lumen's Surface Cache, and crossfades against a coarse-mask roughness/specular response to the
live scene. First acceptance surface: the 14 patch-first `sm_hub_1` wet materials; general
world/prop cubes and the 102 chromatic-tint metal candidates follow the same contract. *Deps:*
3.5, 7.4.

### 7.6 Bloom/glow tuning

VtMB's overbright neon/selfillum vs pinned exposure. *Deps:* 3.6.

### 7.7 Shadow quality

Contact shadows on hero lights, penumbra softness, within 0.1 ms budget. *Deps:* 0.1, 3.1, 3.2.

### 7.8 A/B capture harness

Scripted fixed-camera captures vs **the original game** at the shared vantages. The local
half exists (2.9 + `shots_diff.py`); respect its measured noise floor —
re-baseline after any bake or content rebuild. *Deps:* 0.3, 3.9.

### 7.9 Weather & wetness

Facts + translation boundary: `docs/vtmb/weather.md`. The verified `sm_hub_1` work is retained
as data and logic; the patch-first controller slice is live through `IElysiumWeather`. Material
presentation shares 7.5's one-graph contract (bind `cubemapdefault` + linear mask for the
source endpoint; keep the view-dependent cube out of the Surface Cache; coarse mask for the
enhanced PBR response; one `RainEnhancement` value), with the panel exposing values and
mask/cube/source debug views before tuning. `env_particle` presentation is **not this slice's**:
it is R7.3's contract (`docs/architecture/effects-architecture.md` §5 — the staged `effects[]` /
`particleTrees{}` product, one `AElysiumEffectActor` per row, the generated per-root `NS_<root>`),
and weather only drives it by entity index through `IElysiumWeather::ApplyEmitter`. Deferred:
sewer drips, fixed `func_particle` boxes, NPC shelter, lightning, other maps. *Deps:* 7.5 material
slice, PL12, R7.3; RE23 still owes the wetness time units.

### 10.1 Horizontal scale-out

Export all ~100 maps; per-map light calibration (`probe_light_calibration.py` / `.lightfit`);
fix decoder edge cases as maps surface them. Calibration is in absolute units and only
meaningful on Troika's 81 retail bakes (provenance-gate first); the absolute sweep across the
other ~15 retail sky-pair maps rides along. *Deps:* P4 done (travel), P3 done.

### 10.2 Perf deepening

Lumen tuning ladder, light culling/max-influence cap, scale-up scalability tier for 4070+.
*Deps:* 0.1, P3.

### 10.3 Floor validation `[needs 4060]`

1440p/60 on a real RTX 4060-class 16 GB card; the one gate look cannot judge. *Deps:* P3, 10.2.

### 10.9 Asset enhancement

Run the offline delight → super-resolve → style-anchored PBR pipeline as imported `T_` siblings
the texture lane stages beside the faithful set, selected on the editor surfaces (the runtime
`elysium.EnhancedTextures` loose-file toggle retired at R6.5). *Acceptance:* the faithful
assets remain byte-for-byte faithful; curated Tier 0/1 outputs meet the floor budget; no game-derived output
committed. *Deps:* PP6, 10.3, 7.4. Design: `docs/architecture/asset-enhancement.md`; governing
test: `docs/project/reconstruction-direction.md`.
