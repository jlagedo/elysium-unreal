# Asset enhancement — code-driven remaster of VtMB's own art

Forward-looking design doc. **Not yet scheduled** — an accepted *direction* to build once the
vertical slice plays (P10-ish). Scaffolding already exists in `tools/` (see "What exists
today"). This doc is the plan of record so the choice is made deliberately, not by drift.

## North star

The baseline rebuild is **faithful** — dynamic GI anchored to VtMB's own baked lightmaps
(`rendering-perf.md` → the calibration), geometry and placement read 1:1. On top of that
baseline sits an **opt-in enhancement layer**: raise the *fidelity* of VtMB's assets with
offline, code-driven passes while **preserving its art direction**. This is a *remaster*
stance (fix + enrich the shipped art), not a *remake* (re-author it).

The layer is always a **toggle**, never a fork — the same A/B discipline as
`elysium.BrushCollision` / `elysium.props` / `elysium.lights`. The faithful set stays the
reference; enhancement is a switch on top of it. That keeps the legal posture intact
(everything is still a regenerable derivative of the user's own install under gitignored
`tools/out/`) and keeps the calibration honest (the calibration defines the *target look*;
richer surfaces just respond to it — they don't redefine it).

### The adjudication test

Every candidate enhancement answers one question:

> **Does it serve VtMB's art direction (or fix a technical deficit that fights it), or is it
> inventing/overriding an artist decision?**

Serve or fix → in. Invent or override → out. VtMB's direction is specific and load-bearing:
**gothic-punk, grimy, wet, moody, deliberately un-glamorous.** "Higher fidelity" that
de-grimes, glamorizes, or chromes the world fails the test even when it is technically
"better." Grime *is* the style.

### Three tiers

- **Tier 0 — technical-deficit fixes (unconditional).** These correct data that is objectively
  wrong for a dynamically-relit engine; they pass even the strict "faithful only" reading.
  *Delighting the albedo* and *super-resolution to kill DXT block artifacts + 2004 resolution*.
- **Tier 1 — style-anchored enhancement (the remaster layer; toggleable).** Adds detail the
  source never had, constrained to VtMB's material vocabulary. *PBR material synthesis*
  (roughness / normal / height / AO / envmask), curated. Allowed, but anchored and reviewed.
- **Tier 2 — out of bounds.** Glossy/chrome-everything, de-griming, saturation/contrast
  "pop", remodels, invented hero detail. Re-authors the art; excluded from every set.

## The pipeline is dynamically relit — sequence matters

VtMB's diffuse textures are hand-painted **with lighting and AO baked into the albedo** (2004
practice: the texture *is* the shading). The runtime relights every surface with Lumen, so
that painted-in shading is now *wrong data* — it double-darkens under dynamic light and it
fights the bounce the calibration proved carries the look. This makes **delighting the single
most technically-correct enhancement**, and it dictates the order of every other pass:

1. **Delight → recover true albedo.** Separate the painted shading from base color so the
   dynamic light can do its job. (Tier 0.)
2. **Super-resolve the delit albedo.** Upscaling *before* delighting bakes the shading in at
   higher resolution — wrong. (Tier 0.)
3. **Derive normal / height from the delit albedo.** A normal map generated from a *lit*
   diffuse encodes the painted shadows as fake geometry — doubly wrong under dynamic light.
   Delight first. (Tier 1.)
4. **Infer roughness / AO**, keyed off the delit albedo. (Tier 1.)
5. **Metallic by hand-gated mask**, not inference. (Tier 1 — see the vocabulary below.)

The whole PBR set is a **heuristic guess** — the source carries no roughness/metallic/normal
ground truth. Every reputable workflow says the same thing: AI material generation is a great
*starting point, never final*. So this is authored-by-recipe-plus-review, not a blind batch.

## The VtMB material vocabulary (the anchor for Tier 1)

Good news for a faithful remaster: VtMB is almost entirely **high-roughness dielectric** —
concrete, grime, worn fabric, painted/rusted metal, wet asphalt, brick, skin. That narrow
vocabulary *is* the synthesis recipe and the guard against the generic-upscaler failure mode
("everything becomes glossy plastic and chrome"):

- **Bias roughness high.** Most surfaces are matte-to-satin. Wetness (VtMB loves wet streets)
  is the one lower-roughness cue, and it should read as *wet grime*, not showroom polish.
- **Metallic ≈ 0 by default; gate the exceptions by hand-authored mask.** Genuinely metal
  surfaces are a small, enumerable set. Never let an inference model decide metalness
  globally — that is exactly what turns Santa Monica into a car showroom.
- **Gentle normals.** Surface micro-relief, not embossed geometry. Height/parallax stays
  subtle; VtMB's forms live in the mesh, not the texture.
- **Keep the grime.** Any pass that cleans, brightens, or evens-out a surface has failed the
  adjudication test regardless of "fidelity."

## What exists today (the seam is already half-built)

The offline half already has the scaffolding — this track *formalizes and extends* it, it does
not start from zero:

- **`tools/upscale_bench.py`** — spandrel runner (ESRGAN / RealPLKSR / DAT / Compact / HAT /
  SPAN from one `.pth`/`.safetensors`). Already **alpha-aware** (splits RGB/alpha, upscales
  each — VtMB PNGs carry real RGB under transparent pixels + a smooth mask), **block-aware**
  (prefers compression-aware models — Source shipped DXT, so clean-photo models ring on block
  edges), and **`--seamless`** (wrap-pad → upscale → crop so tiling textures still butt
  cleanly). Emits contact sheets for eyeball A/B before a bulk run.
- **`tools/sky_upscale.py`** — skybox-aware upscale (ring-composite the four horizon faces →
  one 360° strip → upscale once → re-slice, so no per-face seam invention). Writes
  `out/<map>/tex_hi/`.
- **`tools/retex_dds.py`** — emits `.dds` siblings preserving the original DXT blocks + mip
  chain (smaller uploads, real mips, bit-identical texels). The runtime prefers DDS; PNG stays
  the fallback and covers generated maps with no `.tth` source (`_ke` self-illum, `_envmask`,
  normals).

The runtime half is ready too: `M_VtMB_World` is a parameter-slot master material whose growth
path (`rebuild-strategy.md` → Master materials) already plans **bump/normal, envmap mask +
cube, WVT second layer**. PBR-synthesis outputs plug into those slots — no new master material,
just bind the extra maps and toggle the features on.

## Integration plan

- **Offline:** a new stage produces a parallel texture set under `out/<map>/tex_hi/` (sky
  already writes there) plus per-material PBR siblings — `<flat>_n.dds` (normal, BC5),
  `<flat>_r`/`_ao` (BC4), `<flat>_m` (metallic mask, BC4). **BCn-compress the generated maps**
  — do not ship them as PNG; unlike `retex_dds`'s block-preserving copies these are new data
  and need real GPU compression, or VRAM and bandwidth balloon (see the floor below). Delight
  + PBR inference run as `tools/` passes (the same spandrel/torch optional-dep footprint the
  upscalers already declare; a delighter/PBR model swaps in like any other `.pth`).
- **Runtime:** `FElysiumMaterialFactory` binds the `_hi` albedo and the PBR siblings into
  `M_VtMB_World`'s slots when present; a cvar (`elysium.EnhancedTextures`, off by default)
  selects `tex` vs `tex_hi` and enables the extra bindings. Absent maps → the faithful path
  runs unchanged. This is one more A/B toggle, consistent with the rest of the runtime.
- **Curation, not per-texture handwork:** ~thousands of textures across ~100 maps rules out
  hand-authoring each. The workflow is *one tuned recipe per material family* (concrete /
  metal / fabric / brick / skin), a batch run, and a spot-review contact sheet per map —
  `upscale_bench`'s sheet output is already this shape.

## Floor reality (the hard constraint)

The perf floor is **RTX 3060 12 GB @ 1080p, and HWRT Lumen already eats ~10–14 ms of the
16.6 ms frame** (`rendering-perf.md` → Floor reality) — there is essentially no headroom, and
VRAM is a real ceiling. So enhancement is **budget-gated**, not free:

- **Upscale factor is chosen against VRAM, not maxed.** 2× as the default; 4× reserved for
  hero surfaces the player gets close to. A blanket 4× over every texture × the added PBR maps
  can blow the 12 GB floor on its own.
- **BCn + mips are mandatory** on every generated map (the point of `retex_dds` for copies;
  the generated maps need genuine BC5/BC4/BC7 encoding). Uncompressed PNG at runtime is not an
  option at this budget.
- **Added samplers cost bandwidth**, which the floor can least afford. Roughness/AO can often
  be **packed into one RGBA texture** (e.g. R=rough, G=AO, B=metal-mask) rather than three
  samplers — decide this when the material slots are built.

## Risks / open questions

- **Delighting hand-painted art ≠ delighting photoscans.** Delighters (Unity De-Lighting,
  Agisoft De-Lighter, Substance Sampler) are built for photogrammetry, where the baked light is
  an unwanted accident. VtMB's painted shading is partly *artistic intent* (hand-placed form
  and grime). Over-delighting flattens that. → delight **conservatively**, per-family review;
  this is the pass most likely to need a human in the loop.
- **Set consistency.** Per-texture AI decisions drift — a wall and its trim can disagree on
  roughness/normal strength. → tune per material *family*, not per file; review adjacencies.
- **Tiling / seams.** Synthesis passes must honor the same wrap-pad discipline `upscale_bench
  --seamless` already applies, or seamless textures gain seams.
- **Curation labor at scale.** The batch-recipe-plus-spot-review workflow is the mitigation;
  budget it as real (not zero) work per map family.
- **Style regression is silent.** "Higher fidelity" that fails the adjudication test looks
  *fine* in isolation and only reads as wrong next to the faithful A/B. → the toggle and
  side-by-side sheets are the regression guard; keep the faithful set as the reference forever.

## Legal posture

Unchanged and load-bearing: enhanced textures are **derivative works of the user's own game
assets**, so they live under gitignored, regenerable `tools/out/` exactly like every other
decoded intermediate. Nothing enhanced is ever committed or distributed. Bring-your-own-game
holds (`CLAUDE.md` → "Bring-your-own-game").

## Non-texture modernizations (pointer, not scope)

Other "fix a technical deficit" upgrades are *code*, not asset passes, and are tracked
elsewhere: dynamic GI (done — `rendering-perf.md`), TSR upscaling (shipped), Source-movement
correctness (roadmap 4.7), native spatial audio (P6). This doc is scoped to the **offline
asset-enhancement track** only.

## Sources

- [From Diffuse to PBR: AI-powered material generation workflows — Tripo3D](https://www.tripo3d.ai/blog/explore/ai-3d-model-generator-converting-diffuse-only-to-full-pbr-sets)
- [Best AI texture generation for game-ready PBR materials — Scenario](https://www.scenario.com/blog/ai-texture-generation)
- [AI texture generation: create realistic PBR materials — Enviromn](https://www.enviromn.com/blog/ai-texture-generation-pbr-materials)
- [Unity Labs texture de-lighting tool — CG Channel](https://www.cgchannel.com/2017/07/download-unity-labs-free-texture-de-lighting-tool/)
- [Agisoft De-Lighter (free de-lighting tool) — CG Channel](https://www.cgchannel.com/2019/07/download-free-texture-de-lighting-tool-agisoft-de-lighter/)
- [Photogrammetry breakdown: de-lighting — Inu Games](https://inu-games.com/2022/03/23/photogrammetry-breakdown-de-lighting/)
- [How AI upscaling can help remaster game art — Game Developer](https://www.gamedeveloper.com/art/how-ai-upscaling-can-help-remaster-game-art)
- [Upscaling LithTech-engine games (NOLF ESRGAN pack) — ModDB tutorial](https://www.moddb.com/mods/no-one-lives-forever-esrgan-upscale-pack/tutorials/upscaling-lithtech-engine-games)
- [Real-ESRGAN: blind super-resolution with synthetic data — arXiv 2107.10833](https://arxiv.org/pdf/2107.10833)
- [Convert albedo/diffuse to normal map (limitations) — NormalMap.ai](https://normalmap.ai/learn/convert-albedo-diffuse-to-normal-map/)
- [Generating normal and roughness maps from diffuse/albedo — Unreal forums](https://forums.unrealengine.com/t/generating-normal-and-roughness-maps-from-diffuse-or-albedo/484501)
